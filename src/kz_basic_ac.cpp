#include "amxxmodule.h"
#include "resdk/mod_rehlds_api.h"

#include "pdata.h"
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
    memcpy(&g_current_cmd[id], ucmd, sizeof(usercmd_t));
}
void kz_ac_postthink(int id, edict_t* pEdict)
{
    float fmv = fabs(g_current_cmd[id].forwardmove);
    float smv = fabs(g_current_cmd[id].sidemove);

    if ((pEdict->v.button & IN_BACK) && (pEdict->v.button & IN_FORWARD) && (fmv == pEdict->v.maxspeed || fmv > pEdict->v.maxspeed))
    {
            kz_log(nullptr, "[AC] Illegal movespeed from player (%d)(fmv: %.2f)", id, g_current_cmd[id].forwardmove);
    }
    if ((pEdict->v.button & IN_MOVELEFT) && (pEdict->v.button & IN_MOVERIGHT) && (smv == pEdict->v.maxspeed || smv > pEdict->v.maxspeed))
    {
            kz_log(nullptr, "[AC] Illegal movespeed from player (%d)(smv: %.2f)", id, g_current_cmd[id].sidemove);
    }
}
