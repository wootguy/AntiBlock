#include "mmlib.h"
#include <string>
#include <iostream>
#include <ctime>
#include <fstream>

using namespace std;

// Description of plugin
plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION,	// ifvers
    "AntiBlock",	// name
    "1.0",	// version
    __DATE__,	// date
    "w00tguy",	// author
    "github",	// url
    "ANTIBLOCK",	// logtag, all caps please
    PT_ANYTIME,	// (when) loadable
    PT_ANYPAUSE	// (when) unloadable
};

float g_cooldown = 0.5f;
int lastButtons[33];
float lastAntiBlock[33];

bool isBspModel(edict_t* ent) {
	return ent && (ent->v.solid == SOLID_BSP || ent->v.movetype == MOVETYPE_PUSHSTEP);
}

edict_t* TraceLook(edict_t* plr, Vector swapDir, float dist = 1) {
	Vector vecSrc = plr->v.origin;

	plr->v.solid = SOLID_NOT;

	TraceResult tr;
	Vector dir = swapDir * dist;
	int hullType = (plr->v.flags & FL_DUCKING) ? head_hull : human_hull;
	TRACE_HULL(vecSrc, vecSrc + dir, dont_ignore_monsters, hullType, NULL, &tr);

	// try again in case the blocker is on a slope or stair
	if (swapDir.z == 0 && tr.pHit && isBspModel(tr.pHit)) {
		Vector verticalDir = Vector(0, 0, 36);
		if ((plr->v.flags & FL_ONGROUND) == 0) {
			// probably on the ceiling, so try starting the trace lower instead (e.g. negative gravity or ladder)
			verticalDir.z = -36;
		}

		TRACE_HULL(vecSrc, vecSrc + verticalDir, dont_ignore_monsters, hullType, NULL, &tr);
		if (!tr.pHit || isBspModel(tr.pHit)) {
			TRACE_HULL(tr.vecEndPos, tr.vecEndPos + dir, dont_ignore_monsters, hullType, NULL, &tr);
		}
	}

	plr->v.solid = SOLID_SLIDEBOX;

	return tr.pHit;
}

vector<edict_t*> getAntiblockTargets(edict_t* plr, Vector swapDir) {
	vector<edict_t*> targets;

	for (int i = 0; i < 4; i++) {
		edict_t* target = TraceLook(plr, swapDir);

		if (!target) {
			break;
		}

		if (!isValidPlayer(target)) {
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

bool swapCooledDown(edict_t* swapper, float maxSwapTime) {
	if (gpGlobals->time - maxSwapTime < g_cooldown) {
		return false;
	}
	return true;
}

Vector getSwapDir(edict_t* plr) {
	Vector angles = plr->v.v_angle;

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

void PostThink(edict_t* plr) {
	int eidx = ENTINDEX(plr);
	int buttonsPressed = plr->v.button & (~lastButtons[eidx]);
	lastButtons[eidx] = plr->v.button;

	if ((buttonsPressed & IN_USE) == 0) {
		RETURN_META(MRES_IGNORED);
	}

	Vector swapDir = getSwapDir(plr);
	vector<edict_t*> targets = getAntiblockTargets(plr, swapDir);
	if (targets.size() == 0)
		RETURN_META(MRES_IGNORED);

	edict_t* target = targets[0];

	if (target && (plr->v.flags & FL_ONTRAIN) == 0) {
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
				RETURN_META(MRES_IGNORED);
			}

			vector<Vector> newTargetPos;

			float maxDist = -999;
			plr->v.solid = SOLID_NOT;
			for (uint32_t i = 0; i < targets.size(); i++) {
				targets[i]->v.solid = SOLID_NOT;
			}

			for (uint32_t i = 0; i < targets.size(); i++) {
				Vector vecSrc = targets[i]->v.origin;

				newTargetPos.push_back(targets[i]->v.origin);
				if (swapDir.z != 0) {
					newTargetPos[i].z = plr->v.origin.z;
				}
				else if (swapDir.y != 0) {
					newTargetPos[i].y = plr->v.origin.y;
				}
				else if (swapDir.x != 0) {
					newTargetPos[i].x = plr->v.origin.x;
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
				TRACE_HULL(plr->v.origin, plr->v.origin + swapDir * maxDist, dont_ignore_monsters, head_hull, NULL, &tr);
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
						float zDiff = plr->v.flags & FL_DUCKING != 0 ? 18 : 36;
						plr->v.origin.z += zDiff;
						target->v.origin.z -= zDiff;
					}
					*/
					targets[i]->v.flDuckTime = 26;
					targets[i]->v.flags |= FL_DUCKING;
					targets[i]->v.view_ofs = Vector(0, 0, 12);

					lastAntiBlock[ENTINDEX(targets[i])] = gpGlobals->time;
				}

				plr->v.origin = plr->v.origin + swapDir * maxDist;
				plr->v.flDuckTime = 26;
				plr->v.flags |= FL_DUCKING;
				plr->v.view_ofs = Vector(0, 0, 12);

				swappedMultiple = true;
			}

			plr->v.solid = SOLID_SLIDEBOX;
			for (uint32_t i = 0; i < targets.size(); i++) {
				targets[i]->v.solid = SOLID_SLIDEBOX;
			}
		}

		if (!swappedMultiple) {
			// don't let blockers immediately swap back to where they were
			if (!swapCooledDown(plr, Max(lastAntiBlock[eidx], lastAntiBlock[ENTINDEX(target)]))) {
				RETURN_META(MRES_IGNORED);
			}

			Vector srcOri = plr->v.origin;
			bool srcDucking = (plr->v.flags & FL_DUCKING) != 0;
			bool dstDucking = (target->v.flags & FL_DUCKING) != 0 || !isValidPlayer(target);

			plr->v.origin = target->v.origin;
			target->v.origin = srcOri;

			if (!isValidPlayer(target)) {
				float zDiff = srcDucking ? 18 : 36;
				plr->v.origin.z += zDiff;
				target->v.origin.z -= zDiff;
			}

			if (dstDucking) {
				plr->v.flDuckTime = 26;
				plr->v.flags |= FL_DUCKING;
				plr->v.view_ofs = Vector(0, 0, 12);

				// prevent gibbing on elevators when swapper is crouching and swappee is not
				edict_t* dstElev = target->v.groundentity;
				if (!srcDucking && dstElev && dstElev->v.velocity.z > 0) {
					plr->v.origin.z += 18;
				}
			}
			if (srcDucking) {
				target->v.flDuckTime = 26;
				target->v.flags |= FL_DUCKING;
				target->v.view_ofs = Vector(0, 0, 12);

				edict_t* srcElev = plr->v.groundentity;
				if (!dstDucking && srcElev && srcElev->v.velocity.z > 0) {
					target->v.origin.z += 18;
				}
			}

			lastAntiBlock[eidx] = gpGlobals->time;
			lastAntiBlock[ENTINDEX(target)] = gpGlobals->time;
		}

		// for half-life only
		g_engfuncs.pfnEmitSound(plr, CHAN_BODY, "weapons/xbow_hitbod2.wav", 0.7f, 1.0f, 0, 130 + RANDOM_LONG(0, 10));
	}

	RETURN_META(MRES_IGNORED);
}

void MapInit(edict_t* edict_list, int edictCount, int clientMax) {
	memset(lastButtons, 0, sizeof(int) * 33);
	memset(lastAntiBlock, 0, sizeof(float) * 33);
	RETURN_META(MRES_IGNORED);
}

void PluginInit() {
    g_dll_hooks.pfnPlayerPostThink = PostThink;
	g_dll_hooks.pfnServerActivate = MapInit;
}

void PluginExit() {
}