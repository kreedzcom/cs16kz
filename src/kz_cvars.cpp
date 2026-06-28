#include "amxxmodule.h"
#include "resdk/mod_rehlds_api.h"

#include "pdata.h"
#include "kz_ws.h"
#include "kz_util.h"
#include "kz_cvars.h"
#include "kz_storage.h"

const pr_cvar_t g_server_cvars[] = {
    {"edgefriction",        "2",        nullptr},
    {"sv_gravity",          "800",      nullptr},
    {"sv_stopspeed",        "75",       nullptr},
    {"sv_maxspeed",         "320",      nullptr},
    {"sv_accelerate",       "5",        nullptr},
    {"sv_airaccelerate",    "10",       nullptr},
    {"sv_wateraccelerate",  "10",       nullptr},
    {"sv_friction",         "4",        nullptr},
    {"sv_waterfriction",    "1",        nullptr},
    {"sv_stepsize",         "18",       nullptr},
    {"sv_maxvelocity",      "2000",     nullptr},
    {"sv_cheats",           "0",        nullptr},

    {"mp_footsteps",        "1",        nullptr},
    {"sys_ticrate",         "128",      nullptr},
    {"sv_maxrate",          "100000",   nullptr},
    {"sv_minrate",          "0",        nullptr},
    {"sv_maxupdaterate",    "102",      nullptr},
    {"sv_minupdaterate",    "10",       nullptr},
};
const size_t g_server_cvars_size = (sizeof(g_server_cvars) / sizeof(pr_cvar_t));

const pr_cvar_t g_player_cvars[] = {
    {"cl_lw",           "1",    nullptr},
    {"fps_max",         "99.5", nullptr},
    {"fps_override",    "0",    nullptr},
    {"developer",       "0",    nullptr},
    {"gl_vsync",        "0",    nullptr},
    {"cl_forwardspeed", "400",  nullptr},
    {"cl_backspeed",    "400",  nullptr},
    {"cl_sidespeed",    "400",  nullptr},
    {"cl_movespeedkey", "0.52", nullptr},
};
const size_t g_player_cvars_size = (sizeof(g_player_cvars) / sizeof(pr_cvar_t));
static_assert(g_player_cvars_size <= 64, "???? How many cvars did you there ???");

cvar_t* kz_api_url      = nullptr;
cvar_t* kz_api_token    = nullptr;
cvar_t* kz_api_log_send = nullptr;
cvar_t* kz_api_log_recv = nullptr;
cvar_t* kz_api_log_upload = nullptr;
cvar_t* kz_api_log_parse = nullptr;

cvar_t* kz_api_retries_max = nullptr;
cvar_t* kz_api_retries_delay = nullptr;
cvar_t* kz_api_replays_clevel = nullptr;

cvar_t* kz_api_bot_prefix = nullptr;
cvar_t* kz_api_bot_team = nullptr;
cvar_t* kz_api_bot_use_cmd = nullptr;