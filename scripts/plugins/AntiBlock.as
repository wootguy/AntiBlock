
void print(string text) { g_Game.AlertMessage( at_console, text); }
void println(string text) { print(text + "\n"); }

CCVar@ g_disabled;
CCVar@ g_cooldown;

void PluginInit()
{
	g_Module.ScriptInfo.SetAuthor( "w00tguy" );
	g_Module.ScriptInfo.SetContactInfo( "w00tguy123 - forums.svencoop.com" );
	
	g_Hooks.RegisterHook( Hooks::Player::PlayerUse, @PlayerUse );
	
	@g_disabled = CCVar("disabled", 0, "disables AntiBlock", ConCommandFlag::AdminOnly);
	@g_cooldown = CCVar("cooldown", 0.6f, "Time before a swapped player can be swapped with again", ConCommandFlag::AdminOnly);
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
