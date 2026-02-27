#include "amxxmodule.h"
#include "resdk/mod_rehlds_api.h"

#include "pdata.h"
#include "pm_defs.h"
#include "in_buttons.h"
#include "kz_ws.h"
#include "kz_util.h"
#include "kz_cvars.h"
#include "kz_replay.h"
#include "kz_storage.h"
#include "kz_natives.h"

#define AC_MAX_FPS  100.1f
#define EPSILON     0.001f

usercmd_t g_current_cmd[33];
float g_wavg_fps[33];
int g_total_frames[33];
int g_fps_warns[33];

void kz_ac_frame(void)
{
    static float last_time;
    const float current_time    = gpGlobals->time;
    const float delta_time      = current_time - last_time;

    // for my own sanity, keep it seperated (> || ==) or i will find you
    if (delta_time > 1.0f || delta_time == 1.0f)
    {
        for (int i = 1; i <= gpGlobals->maxClients; ++i)
        {
            if (!MF_IsPlayerIngame(i) || MF_IsPlayerBot(i) || MF_GetPlayerTime(i) < 5.0f)
            {
                continue;
            }

            g_wavg_fps[i]   = (2.0f * g_wavg_fps[i] / 3.0f) + (1.0f * (static_cast<float>(g_total_frames[i]) / delta_time) / 3.0f);
            if (g_wavg_fps[i] > AC_MAX_FPS + EPSILON)
            {
                if (++g_fps_warns[i] > 1)
                {
                    kz_log(nullptr, "[AC] Illegal average fps on player (%d): %.2f", i, g_wavg_fps[i]);
                }
            }
            else
            {
                g_fps_warns[i] = 0;
            }
            g_total_frames[i] = 0;
        }
        last_time = current_time;
    }
}
void kz_ac_cmd(int id, const usercmd_t* ucmd)
{
    g_total_frames[id] += 1;
    memcpy((void *)&g_current_cmd[id], ucmd, sizeof(usercmd_t));
}

// https://github.com/alliedmodders/hlsdk/blob/a0edb7792a96998d349325bebab8ea41ec5cb239/ricochet/cl_dll/input.cpp#L495
void kz_ac_pm_move(int id, struct playermove_s *ppmove, int server)
{
	const entvars_t *entvars = PEV(id);
    float wishmove[2] = {
		g_current_cmd[id].forwardmove,
		g_current_cmd[id].sidemove
	};
	float maxspeeds[4] = {
		floorf(ppmove->clientmaxspeed),
		floorf(ppmove->clientmaxspeed * 0.75f),
		floorf(ppmove->clientmaxspeed * 0.5f),
		floorf(ppmove->clientmaxspeed * 0.25f),
	};
	float diagMaxspeed = ppmove->clientmaxspeed * 0.70710678118654f;
	float diagMaxspeeds[4] = {
		floorf(diagMaxspeed),
		floorf(diagMaxspeed * 0.75f),
		floorf(diagMaxspeed * 0.5f),
		floorf(diagMaxspeed * 0.25f),
	};
	
	// 4 buttons, 2^4 combinations = 16
	int buttonCombinations[16] = {
		0,
		IN_FORWARD,
		IN_BACK,
		IN_FORWARD | IN_BACK,
		IN_MOVELEFT,
		IN_MOVELEFT | IN_FORWARD,
		IN_MOVELEFT | IN_BACK,
		IN_MOVELEFT | IN_BACK | IN_FORWARD,
		IN_MOVERIGHT,
		IN_MOVERIGHT | IN_FORWARD,
		IN_MOVERIGHT | IN_BACK,
		IN_MOVERIGHT | IN_BACK | IN_FORWARD,
		IN_MOVERIGHT | IN_MOVELEFT,
		IN_MOVERIGHT | IN_MOVELEFT | IN_FORWARD,
		IN_MOVERIGHT | IN_MOVELEFT | IN_BACK,
		IN_MOVERIGHT | IN_MOVELEFT | IN_BACK | IN_FORWARD,
	};
	int validMoves[16][4] = {
		
	};
	int buttons = g_current_cmd[id].buttons;
	
	if (wishmove[0] > ppmove->clientmaxspeed || wishmove[1] > ppmove->clientmaxspeed)
	{
		//MF_Log("wishmove bigger than maxspeed %f %f %f\n", wishmove[0], wishmove[1], ppmove->clientmaxspeed);
	}
	//MF_Log("wishmove %f %f\n", wishmove[0], wishmove[1]);
#if 0
    for (int i = 0; i < 2; i++)
	{
		if (wishmove[i] != 0
			&& wishmove[i] != maxspeed * 0.25f
			&& wishmove[i] != maxspeed * 0.5f
			&& wishmove[i] != maxspeed * 0.75f
			&& wishmove[i] != maxspeed)
		{
			return false;
		}
	}
#endif
	
}

void kz_ac_postthink(int id, edict_t* pEdict)
{
	
}