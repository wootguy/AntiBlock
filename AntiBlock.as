
void print(string text) { g_Game.AlertMessage( at_console, text); }
void println(string text) { print(text + "\n"); }

CCVar@ g_disabled;
CCVar@ g_cooldown;
CCVar@ g_stomp_mode;

enum STOMP_MODE {
	STOMP_OFF = 0,			// normal fall damage logic
	STOMP_SPLIT,			// split damage between all players involved
	STOMP_SPLIT_BOTTOM,		// split damage across the players that got stomped on
	STOMP_DUPLICATE,		// apply full damage to each player
	STOMP_MODES
}

void PluginInit()
{
	g_Module.ScriptInfo.SetAuthor( "w00tguy" );
	g_Module.ScriptInfo.SetContactInfo( "w00tguy123 - forums.svencoop.com" );
	
	g_Hooks.RegisterHook( Hooks::Player::PlayerUse, @PlayerUse );
	g_Hooks.RegisterHook( Hooks::Player::PlayerTakeDamage, @PlayerTakeDamage );
	g_Hooks.RegisterHook( Hooks::Player::ClientSay, @ClientSay );
	
	@g_disabled = CCVar("disabled", 0, "disables AntiBlock", ConCommandFlag::AdminOnly);
	@g_cooldown = CCVar("cooldown", 0.6f, "Time before a swapped player can be swapped with again", ConCommandFlag::AdminOnly);
	@g_stomp_mode = CCVar("stomp", STOMP_SPLIT, "Stomp mode (0=off, 1=split, 2=bottom only, 3=duplicate)", ConCommandFlag::AdminOnly);
}

CBaseEntity@ TraceLook(CBasePlayer@ plr, float dist=1)
{
	Vector vecSrc = plr.pev.origin;
	Vector angles = plr.pev.v_angle;
	
	// snap to 90 degree angles
	angles.y = (int((angles.y + 180 + 45) / 90) * 90) - 180;
	angles.x = (int((angles.x + 180 + 45) / 90) * 90) - 180;
	
	// vertical unblocking has priority
	if (angles.x != 0) {
		angles.y = 0; 
	} else {
		angles.x = 0;
	}

	Math.MakeVectors( angles );
	
	TraceResult tr;
	Vector dir = g_Engine.v_forward * dist;
	HULL_NUMBER hullType = plr.pev.flags & FL_DUCKING != 0 ? head_hull : human_hull;
	g_Utility.TraceHull( vecSrc, vecSrc + dir, dont_ignore_monsters, hullType, plr.edict(), tr );
	
	// try again in case the blocker is on a slope or stair
	if (angles.x == 0 and g_EntityFuncs.Instance( tr.pHit ) !is null and g_EntityFuncs.Instance( tr.pHit ).IsBSPModel()) {
		Vector verticalDir = Vector(0,0,36);
		if (plr.pev.flags & FL_ONGROUND == 0) {
			// probably on the ceiling, so try starting the trace lower instead (e.g. negative gravity or ladder)
			verticalDir.z = -36; 
		}
		
		g_Utility.TraceHull( vecSrc, vecSrc + verticalDir, dont_ignore_monsters, hullType, plr.edict(), tr );
		if (g_EntityFuncs.Instance( tr.pHit ) is null or g_EntityFuncs.Instance( tr.pHit ).IsBSPModel()) {
			g_Utility.TraceHull( tr.vecEndPos, tr.vecEndPos + dir, dont_ignore_monsters, hullType, plr.edict(), tr );
		}
	}

	return g_EntityFuncs.Instance( tr.pHit );
}

string format_float(float f)
{
	uint decimal = uint(((f - int(f)) * 10)) % 10;
	return "" + int(f) + "." + decimal;
}

HookReturnCode PlayerUse( CBasePlayer@ plr, uint& out uiFlags )
{	
	if (plr.m_afButtonPressed & IN_USE == 0 or g_disabled.GetBool()) {
		return HOOK_CONTINUE;
	}
	
	CBaseEntity@ target = TraceLook(plr);
	
	if (target !is null and target.IsPlayer() and plr.pev.flags & FL_ONTRAIN == 0) {
		CustomKeyvalues@ pCustom = plr.GetCustomKeyvalues();
		CustomKeyvalues@ tCustom = target.GetCustomKeyvalues();
		CustomKeyvalue pValue( pCustom.GetKeyvalue( "$f_lastAntiBlock" ) );
		CustomKeyvalue tValue( tCustom.GetKeyvalue( "$f_lastAntiBlock" ) );
		
		// don't let blockers immediately swap back to where they were
		float maxLastUse = Math.max(pValue.GetFloat(), tValue.GetFloat());
		if (g_Engine.time - maxLastUse < g_cooldown.GetFloat()) {
			if (g_cooldown.GetFloat() > 1) {
				float waitTime = (maxLastUse + g_cooldown.GetFloat()) - g_Engine.time;
				g_PlayerFuncs.PrintKeyBindingString(plr, "Wait " + format_float(waitTime) + " seconds\n");
			}
			return HOOK_CONTINUE;
		}
	
		Vector srcOri = plr.pev.origin;
		bool srcDucking = plr.pev.flags & FL_DUCKING != 0;
		bool dstDucking = target.pev.flags & FL_DUCKING != 0;
		
		plr.pev.origin = target.pev.origin;
		if (dstDucking) {
			plr.pev.flDuckTime = 26;
			plr.pev.flags |= FL_DUCKING;
		} else if (srcDucking) {
			plr.pev.origin.z -= 18;
		}
		
		target.pev.origin = srcOri;
		if (srcDucking and !dstDucking) {
			target.pev.flDuckTime = 26;
			target.pev.flags |= FL_DUCKING;
		}
		
		pCustom.SetKeyvalue( "$f_lastAntiBlock", g_Engine.time );
		tCustom.SetKeyvalue( "$f_lastAntiBlock", g_Engine.time );
		
		uiFlags |= PlrHook_SkipUse;
	}
	
	return HOOK_CONTINUE;
}

HookReturnCode PlayerTakeDamage(DamageInfo@ info)
{
	CBasePlayer@ plr = cast<CBasePlayer@>(g_EntityFuncs.Instance(info.pVictim.pev));
	entvars_t@ pevInflictor = info.pInflictor !is null ? info.pInflictor.pev : null;
	entvars_t@ pevAttacker = info.pAttacker !is null ? info.pAttacker.pev : null;
	
	int stomp_mode = g_stomp_mode.GetInt();
	bool stomping_enabled = stomp_mode > STOMP_OFF || stomp_mode < STOMP_MODES;

	if (stomping_enabled && info.bitsDamageType & DMG_FALL != 0) {		
		array<CBaseEntity@> ent_hits;
		array<int> ent_hit_old_solid;
		
		ent_hits.insertLast(plr);
		ent_hit_old_solid.insertLast(plr.pev.solid);
		
		int splitCount = 1;
		int infiniteLoopSafety = 1000;
		for (int i = 0; i < infiniteLoopSafety; i++) {
			TraceResult tr;
			Vector dir = Vector(0,0,-1);
			HULL_NUMBER hullType = plr.pev.flags & FL_DUCKING != 0 ? head_hull : human_hull;
			g_Utility.TraceHull( plr.pev.origin, plr.pev.origin + dir, dont_ignore_monsters, hullType, plr.edict(), tr );
			
			CBaseEntity@ phit = g_EntityFuncs.Instance( tr.pHit );
			if (phit is null || phit.IsBSPModel())
				break;
		
			ent_hits.insertLast(phit);
			ent_hit_old_solid.insertLast(phit.pev.solid);
			
			if (phit.IsPlayer()) { // monsters take damage from stomping already, and that can't be bypassed(?)
				splitCount += 1;
			}
			
			if (splitCount >= 5) {
				break; // don't split damage across players that are stuck inside each other
			}
			
			if (i == infiniteLoopSafety-1) {
				println("WARNING: Infinite loop in AntiBlock plugin stomp logic. Tell w00tguy how that happened pls.");
			}
			
			phit.pev.solid = SOLID_NOT; // check for other ents in the same spot
		}
		
		if (stomp_mode == STOMP_SPLIT_BOTTOM) {
			splitCount -= 1;
		}
		
		if (splitCount > 0) {
			int dmgSplit = int(info.flDamage / float(splitCount));
			
			if (stomp_mode == STOMP_DUPLICATE) {
				dmgSplit = int(info.flDamage);
				//println("Applying " + info.flDamage + " damage to " + splitCount + " players");
			}
			else {
				//println("Splitting " + info.flDamage + " damage across " + splitCount + " players = " + dmgSplit);
			}
			
			for (uint i = 0; i < ent_hits.size(); i++) {
				ent_hits[i].pev.solid = ent_hit_old_solid[i];
				
				if (stomp_mode == STOMP_SPLIT_BOTTOM && i == 0) {
					continue; // first idx is always the stomper
				}
				
				if (ent_hits[i].IsPlayer()) {
					ent_hits[i].pev.dmg_take += dmgSplit;
					ent_hits[i].pev.health -= dmgSplit;
					if (ent_hits[i].pev.health <= 0) {
						ent_hits[i].Killed( pevAttacker, GIB_NORMAL );
					}
					if (i != 0) {
						// fall damage effect for victims (best guess)
						ent_hits[i].pev.punchangle.x += 4 + 12*(Math.min(100, dmgSplit) / 100.0f);
						g_PlayerFuncs.ScreenShake(ent_hits[i].pev.origin, 255.0f, 255.0f, 0.5f, 1.0f);
					}
				}
			}
			
			// bypass sven damage logic
			info.flDamage = 0;
		}
	}
	
	return HOOK_CONTINUE;
}


bool doCommand(CBasePlayer@ plr, const CCommand@ args)
{	
	if ( args.ArgC() > 0 )
	{
		if ( args[0] == ".antiblock" )
		{
			g_PlayerFuncs.SayText(plr, "AntiBlock version 2\n");
			
			if (g_disabled.GetBool()) {
				g_PlayerFuncs.SayText(plr, "    Disabled on this map\n");
				return true;
			}
			
			g_PlayerFuncs.SayText(plr, "    Cooldown = " + g_cooldown.GetFloat() + "\n");
			
			string stompMode = "" + g_stomp_mode.GetInt();
			
			switch(g_stomp_mode.GetInt()) {
				
				case STOMP_SPLIT: stompMode += " (split damage)"; break;
				case STOMP_SPLIT_BOTTOM: stompMode += " (bottom only)"; break;
				case STOMP_DUPLICATE: stompMode += " (duplicate damage)"; break;
				case STOMP_OFF:
				default:
					stompMode += " (disabled)";
			}
			
			g_PlayerFuncs.SayText(plr, "    Stomp mode = " + stompMode + "\n");
			
			return true;
		}
	}
	return false;
}

HookReturnCode ClientSay( SayParameters@ pParams )
{	
	CBasePlayer@ plr = pParams.GetPlayer();
	const CCommand@ args = pParams.GetArguments();
	
	if (doCommand(plr, args))
	{
		pParams.ShouldHide = true;
		return HOOK_HANDLED;
	}
	
	return HOOK_CONTINUE;
}

CClientCommand _antiblock("antiblock", "Anti-rush status", @consoleCmd );

void consoleCmd( const CCommand@ args )
{
	CBasePlayer@ plr = g_ConCommandSystem.GetCurrentPlayer();
	doCommand(plr, args);
}