#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "CBasePlayer.h"
#include "PluginHooks.h"

using namespace std;

float g_cooldown = 0.5f;
int lastButtons[33];
float lastAntiBlock[33];

HLCOOP_PLUGIN_HOOKS g_hooks;

bool isBspModel(edict_t* ent) {
	return ent && (ent->v.solid == SOLID_BSP || ent->v.movetype == MOVETYPE_PUSHSTEP);
}

edict_t* TraceLook(CBasePlayer* plr, Vector swapDir, float dist = 1) {
	Vector vecSrc = plr->pev->origin;

	plr->pev->solid = SOLID_NOT;

	TraceResult tr;
	Vector dir = swapDir * dist;
	int hullType = (plr->pev->flags & FL_DUCKING) ? head_hull : human_hull;
	TRACE_HULL(vecSrc, vecSrc + dir, dont_ignore_monsters, hullType, NULL, &tr);

	// try again in case the blocker is on a slope or stair
	if (swapDir.z == 0 && tr.pHit && isBspModel(tr.pHit)) {
		Vector verticalDir = Vector(0, 0, 36);
		if ((plr->pev->flags & FL_ONGROUND) == 0) {
			// probably on the ceiling, so try starting the trace lower instead (e.g. negative gravity or ladder)
			verticalDir.z = -36;
		}

		TRACE_HULL(vecSrc, vecSrc + verticalDir, dont_ignore_monsters, hullType, NULL, &tr);
		if (!tr.pHit || isBspModel(tr.pHit)) {
			TRACE_HULL(tr.vecEndPos, tr.vecEndPos + dir, dont_ignore_monsters, hullType, NULL, &tr);
		}
	}

	plr->pev->solid = SOLID_SLIDEBOX;

	return tr.pHit;
}

vector<edict_t*> getAntiblockTargets(CBasePlayer* plr, Vector swapDir) {
	vector<edict_t*> targets;

	for (int i = 0; i < 4; i++) {
		edict_t* target = TraceLook(plr, swapDir);

		if (!target) {
			break;
		}
		
		if (!IsValidPlayer(target)) {
			continue;
		}

		targets.push_back(target);
		target->v.solid = SOLID_NOT;
	}

	for (uint32_t i = 0; i < targets.size(); i++) {
		targets[i]->v.solid = SOLID_SLIDEBOX;
	}

	return targets;
}

bool swapCooledDown(CBasePlayer* swapper, float maxSwapTime) {
	if (gpGlobals->time - maxSwapTime < g_cooldown) {
		return false;
	}
	return true;
}

Vector getSwapDir(CBasePlayer* plr) {
	Vector angles = plr->pev->v_angle;

	// snap to 90 degree angles
	angles.y = (int((angles.y + 180 + 45) / 90) * 90) - 180;
	angles.x = (int((angles.x + 180 + 45) / 90) * 90) - 180;

	// vertical unblocking has priority
	if (angles.x != 0) {
		angles.y = 0;
	}
	else {
		angles.x = 0;
	}

	MAKE_VECTORS(angles);

	return gpGlobals->v_forward;
}

HOOK_RETURN_DATA PlayerUse(CBasePlayer* plr) {
	int eidx = plr->entindex();
	int buttonsPressed = plr->pev->button & (~lastButtons[eidx]);
	lastButtons[eidx] = plr->pev->button;

	if ((buttonsPressed & IN_USE) == 0 || (plr->pev->iuser1 != 0)) {
		return HOOK_CONTINUE;
	}

	Vector swapDir = getSwapDir(plr);
	vector<edict_t*> targets = getAntiblockTargets(plr, swapDir);
	if (targets.size() == 0)
		return HOOK_CONTINUE;

	edict_t* target = targets[0];

	if (target && (plr->pev->flags & FL_ONTRAIN) == 0) {
		bool swappedMultiple = false;

		if (targets.size() > 1) {
			bool allSafeSwaps = true;

			float mostRecentSwapTime = lastAntiBlock[eidx];
			for (uint32_t i = 0; i < targets.size(); i++) {
				float time = lastAntiBlock[ENTINDEX(targets[i])];
				if (time > mostRecentSwapTime) {
					mostRecentSwapTime = time;
				}
			}

			if (!swapCooledDown(plr, mostRecentSwapTime)) {
				return HOOK_CONTINUE;
			}

			vector<Vector> newTargetPos;

			float maxDist = -999;
			plr->pev->solid = SOLID_NOT;
			for (uint32_t i = 0; i < targets.size(); i++) {
				targets[i]->v.solid = SOLID_NOT;
			}

			for (uint32_t i = 0; i < targets.size(); i++) {
				Vector vecSrc = targets[i]->v.origin;

				newTargetPos.push_back(targets[i]->v.origin);
				if (swapDir.z != 0) {
					newTargetPos[i].z = plr->pev->origin.z;
				}
				else if (swapDir.y != 0) {
					newTargetPos[i].y = plr->pev->origin.y;
				}
				else if (swapDir.x != 0) {
					newTargetPos[i].x = plr->pev->origin.x;
				}

				float dist = (newTargetPos[i] - targets[i]->v.origin).Length();
				if (dist > maxDist) {
					maxDist = dist;
				}

				TraceResult tr;

				int spaceNeeded = head_hull;
				TRACE_HULL(vecSrc, newTargetPos[i], dont_ignore_monsters, spaceNeeded, targets[i], &tr);

				if (tr.flFraction < 1.0f) {
					allSafeSwaps = false;
					break;
				}
			}

			if (allSafeSwaps) {
				TraceResult tr;
				TRACE_HULL(plr->pev->origin, plr->pev->origin + swapDir * maxDist, dont_ignore_monsters, head_hull, NULL, &tr);
				allSafeSwaps = tr.flFraction >= 1.0f;
			}

			if (allSafeSwaps) {
				for (uint32_t i = 0; i < targets.size(); i++) {
					targets[i]->v.origin = newTargetPos[i];
					/*
					if (isValidPlayer(targets[i])) {
						targets[i]->v.flDuckTime = 26;
						targets[i]->v.flags |= FL_DUCKING;
						targets[i]->v.view_ofs = Vector(0, 0, 12);
					}
					else {
						// monster origins are on the floor and player origins aren't
						float zDiff = plr->pev->flags & FL_DUCKING != 0 ? 18 : 36;
						plr->pev->origin.z += zDiff;
						target->v.origin.z -= zDiff;
					}
					*/
					targets[i]->v.flDuckTime = 26;
					targets[i]->v.flags |= FL_DUCKING;
					targets[i]->v.view_ofs = Vector(0, 0, 12);

					lastAntiBlock[ENTINDEX(targets[i])] = gpGlobals->time;
				}

				plr->pev->origin = plr->pev->origin + swapDir * maxDist;
				plr->pev->flDuckTime = 26;
				plr->pev->flags |= FL_DUCKING;
				plr->pev->view_ofs = Vector(0, 0, 12);

				swappedMultiple = true;
			}

			plr->pev->solid = SOLID_SLIDEBOX;
			for (uint32_t i = 0; i < targets.size(); i++) {
				targets[i]->v.solid = SOLID_SLIDEBOX;
			}
		}

		if (!swappedMultiple) {
			// don't let blockers immediately swap back to where they were
			if (!swapCooledDown(plr, V_max(lastAntiBlock[eidx], lastAntiBlock[ENTINDEX(target)]))) {
				return HOOK_CONTINUE;
			}

			Vector srcOri = plr->pev->origin;
			bool srcDucking = (plr->pev->flags & FL_DUCKING) != 0;
			bool dstDucking = (target->v.flags & FL_DUCKING) != 0 || !IsValidPlayer(target);

			plr->pev->origin = target->v.origin;
			target->v.origin = srcOri;

			if (!IsValidPlayer(target)) {
				float zDiff = srcDucking ? 18 : 36;
				plr->pev->origin.z += zDiff;
				target->v.origin.z -= zDiff;
			}

			bool dstElev = !FNullEnt(target->v.groundentity) && target->v.groundentity->v.velocity.z != 0;
			bool srcElev = !FNullEnt(plr->pev->groundentity) && plr->pev->groundentity->v.velocity.z != 0;

			// prevent elevator gibbing
			if (dstElev) {
				plr->pev->origin.z += 18;
			}
			if (srcElev) {
				target->v.origin.z += 18;
			}

			if (dstDucking) {
				plr->pev->flDuckTime = 26;
				plr->pev->flags |= FL_DUCKING;
				plr->pev->view_ofs = Vector(0, 0, 12);

				// prevent gibbing on elevators when swapper is crouching and swappee is not
				// (additional height needed on top of the default extra height)
				if (!srcDucking && dstElev) {
					plr->pev->origin.z += 18;
				}
			}

			if (srcDucking) {
				target->v.flDuckTime = 26;
				target->v.flags |= FL_DUCKING;
				target->v.view_ofs = Vector(0, 0, 12);


				if (!dstDucking && srcElev) {
					target->v.origin.z += 18;
				}
			}

			lastAntiBlock[eidx] = gpGlobals->time;
			lastAntiBlock[ENTINDEX(target)] = gpGlobals->time;
		}

		EMIT_SOUND_DYN(plr->edict(), CHAN_BODY, "weapons/xbow_hitbod2.wav", 0.7f, 1.0f, 0, 130 + RANDOM_LONG(0, 10));
	
		return HOOK_HANDLED_OVERRIDE(0);
	}

	return HOOK_CONTINUE;
}

HOOK_RETURN_DATA MapInit() {
	memset(lastButtons, 0, sizeof(int) * 33);
	memset(lastAntiBlock, 0, sizeof(float) * 33);

	PRECACHE_SOUND_ENT(NULL, "weapons/xbow_hitbod2.wav");

	return HOOK_CONTINUE;
}

extern "C" int DLLEXPORT PluginInit() {
	g_hooks.pfnMapInit = MapInit;
	g_hooks.pfnPlayerUse = PlayerUse;

	return RegisterPlugin(&g_hooks);
}

extern "C" void DLLEXPORT PluginExit() {
	// nothing to clean up
}