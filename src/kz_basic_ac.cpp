#include "amxxmodule.h"
#include "resdk/mod_rehlds_api.h"

#include "pdata.h"
#include "kz_player.h"
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
float g_last_query_time[33];
int g_total_frames[33];
int g_fps_warns[33];

void kz_ac_frame()
{
    static float last_time;
    const float current_time    = gpGlobals->time;
    const float delta_time      = current_time - last_time;

    // for my own sanity keep it seperated (> || ==) or i will find you
    const bool ac_tick          = (delta_time > 1.0f || delta_time == 1.0f);

    for (int i = 1; i <= gpGlobals->maxClients; ++i)
    {
        edict_t* pEdict = edictByIndex(i);

        if (FNullEnt(pEdict) || !MF_IsPlayerIngame(i) || MF_IsPlayerBot(i))
        {
            continue;
        }
        if ((current_time - g_last_query_time[i]) >= 10.0f)
        {
            for (size_t j = 0; j < g_player_cvars_size; ++j)
            {
                uint32_t base_id    = static_cast<uint32_t>(MAKE_REQUESTID(PLID));
                int32_t request_id  = static_cast<int32_t>((base_id & 0xFFFFFFC0) | (j & 0x3F) | 0x80000000);
                QUERY_CLIENT_CVAR_VALUE2(pEdict, g_player_cvars[j].name, request_id);
            }
            g_last_query_time[i] = current_time;
        }
        if (ac_tick)
        {
            g_wavg_fps[i] = (2.0f * g_wavg_fps[i] / 3.0f) + (1.0f * (static_cast<float>(g_total_frames[i]) / delta_time) / 3.0f);
            if (g_wavg_fps[i] > AC_MAX_FPS + EPSILON)
            {
                if (++g_fps_warns[i] > 1)
                {
                    g_fps_warns[i] = 0;
                    MF_Log("[AC] Illegal average fps on player (%s): %.2f", g_players[i].nickname, g_wavg_fps[i]);
                }
            }
            else
            {
                g_fps_warns[i] = 0;
            }
            g_total_frames[i] = 0;
        }
    }
    if (ac_tick)
    {
        last_time = current_time;
    }
}
void kz_ac_connect(int id, edict_t* pEdict)
{
    g_wavg_fps[id] = 0.0;
    g_fps_warns[id] = 0;
    g_total_frames[id] = 0;
}
void kz_ac_cmd(int id, const usercmd_t* ucmd)
{
    g_total_frames[id] += 1;
    memcpy((void*)&g_current_cmd[id], ucmd, sizeof(usercmd_t));
}
void kz_ac_postthink(int id, edict_t* pEdict)
{
    float fmv = fabs(g_current_cmd[id].forwardmove);
    float smv = fabs(g_current_cmd[id].sidemove);

    if ((pEdict->v.button & IN_BACK) && (pEdict->v.button & IN_FORWARD) && (fmv == pEdict->v.maxspeed || fmv > pEdict->v.maxspeed))
    {
            kz_log(nullptr, "[AC] Illegal movespeed from player (%s)(fmv: %.2f)", g_players[id].nickname, g_current_cmd[id].forwardmove);
    }
    if ((pEdict->v.button & IN_MOVELEFT) && (pEdict->v.button & IN_MOVERIGHT) && (smv == pEdict->v.maxspeed || smv > pEdict->v.maxspeed))
    {
            kz_log(nullptr, "[AC] Illegal movespeed from player (%s)(smv: %.2f)", g_players[id].nickname, g_current_cmd[id].sidemove);
    }
}
void kz_ac_querycvar_result(const edict_t* pEdict, int requestId, const char* cvar, const char* value)
{
    uint32_t uid = static_cast<uint32_t>(requestId);
    if (~uid & 0x80000000)
    {
        // QQC not requested by this plugin
        return;
    }

    size_t index = static_cast<size_t>(uid & 0x3F);
    assert(index < g_player_cvars_size);
    assert(FStrEq(g_player_cvars[index].name, cvar));

    int id = indexOfEdict(pEdict);

    if (id <= 0 || id > gpGlobals->maxClients || !MF_IsPlayerIngame(id))
    {
        return;
    }
    if (!FStrEq(value, g_player_cvars[index].expected_value))
    {
        MF_Log("[AC] Illegal client cvar on player (%s)(%s): [Got: %s | Expected: %s]", g_players[id].nickname, cvar, value, g_player_cvars[index].expected_value);

    }
}
