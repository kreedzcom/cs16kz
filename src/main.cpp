#include "amxxmodule.h"
#include "resdk/mod_rehlds_api.h"

#include "pdata.h"
#include "kz_ws.h"
#include "kz_util.h"
#include "kz_cvars.h"
#include "kz_replay.h"
#include "kz_storage.h"
#include "kz_natives.h"
#include "kz_basic_ac.h"

#include <filesystem>

edict_t* g_pEdicts = nullptr;
bool g_initialiazed = false;
std::filesystem::path g_data_dir;

void RH_Cvar_DirectSet(IRehldsHook_Cvar_DirectSet* chain, cvar_t* var, const char* value);
void KZ_Cvar_DirectSet(const char* const varname, const char* const value);

/***************************************************************************************************************/
/***************************************************************************************************************/
int FN_AMXX_CHECKGAME(const char* game)
{
    return (FStrEq(game, "cstrike") ? AMXX_GAME_OK : AMXX_GAME_BAD);
}

void FN_AMXX_ATTACH()
{
    if (RehldsApi_Init())
    {
        RehldsHookchains->Cvar_DirectSet()->registerHook(RH_Cvar_DirectSet, HC_PRIORITY_UNINTERRUPTABLE);
    }

    g_pEdicts = (*g_engfuncs.pfnPEntityOfEntIndex)(0);
    
    kz_ws_register(WSMessageType::invalid,     kz_ws_ack_invalid);
    kz_ws_register(WSMessageType::hello,       kz_ws_ack_hello);
    kz_ws_register(WSMessageType::map_info,    kz_ws_ack_map_info);
    kz_ws_register(WSMessageType::client_info, kz_ws_ack_client_info);
    kz_ws_register(WSMessageType::add_record,  kz_ws_ack_add_record);
    kz_ws_register(WSMessageType::del_record,  kz_ws_ack_del_record);
    kz_ws_register(WSMessageType::add_replay,  kz_ws_ack_add_replay);
    kz_ws_register(WSMessageType::get_replay,  kz_ws_ack_get_replay);

    kz_api_url      = register_cvar("kz_api_url",  "", FCVAR_EXTDLL | FCVAR_PROTECTED | FCVAR_SPONLY);
    kz_api_token    = register_cvar("kz_api_token","", FCVAR_EXTDLL | FCVAR_PROTECTED | FCVAR_SPONLY);

    kz_api_log_send = register_cvar("kz_api_log_send", "1", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_log_recv = register_cvar("kz_api_log_recv", "1", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_log_upload = register_cvar("kz_api_log_upload", "1", FCVAR_EXTDLL | FCVAR_SPONLY);

    kz_api_replays_clevel = register_cvar("kz_api_replays_clevel", "10", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_add_natives();
    kz_log_init(std::this_thread::get_id());
}
void FN_AMXX_PLUGINSLOADED()
{
    g_data_dir = std::filesystem::path("cstrike") / MF_GetLocalInfo("amxx_datadir", "addons/amxmodx/data") / "kz_global";
    if (!std::filesystem::exists(g_data_dir))
    {
        std::error_code ec;
        if(std::filesystem::create_directories(g_data_dir, ec))
        {
            kz_log(nullptr, "Directory created: %s", g_data_dir.c_str());
        }
        else
        {
            kz_log(nullptr, "Failed to create directory (%s): %s", g_data_dir.c_str(), ec.message().c_str());
        }
    }

    kz_rp_update_header();
    kz_api_add_forwards();

    if (!g_initialiazed)
    {
        g_initialiazed = true;

        kz_storage_init();
        kz_ws_init();
        kz_rp_init();
    }

}
void FN_META_DETACH()
{
    if (g_initialiazed)
    {
        kz_rp_uninit();
        kz_ws_uninit();
        kz_storage_uninit();
    }
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void FN_StartFrame()
{
    if (!g_initialiazed)
    {
        RETURN_META(MRES_IGNORED);
    }
    kz_log_flush();
    kz_ac_frame();
    kz_run_cvar_checker();
    kz_ws_run_tasks(5);

    RETURN_META(MRES_IGNORED);
}
void FN_ServerActivate(edict_t* pEdictList, int edictCount, int clientMax)
{
    RETURN_META(MRES_IGNORED);
}
void FN_ServerDeactivate_Post(void)
{
    g_pEdicts = nullptr;
    RETURN_META(MRES_IGNORED);
}
void FN_DispatchKeyValue(edict_t* pentKeyvalue, KeyValueData* pkvd)
{
    if (FClassnameIs(pentKeyvalue, "worldspawn"))
    {
        g_pEdicts = pentKeyvalue;
    }
    RETURN_META(MRES_IGNORED);
}
int FN_DispatchSpawn(edict_t* pent)
{
    if (FClassnameIs(pent, "worldspawn"))
    {
        g_pEdicts = (*g_engfuncs.pfnPEntityOfEntIndex)(0);
    }
    RETURN_META_VALUE(MRES_IGNORED, FALSE);
}
void FN_CvarValue2(const edict_t* pEdict, int requestId, const char* cvar, const char* value)
{
    kz_qqc_handler(pEdict, requestId, cvar, value);
    RETURN_META(MRES_IGNORED);
}

/* This never gets called and i dont know why. Tried without rehlds/regamedll installed */
/* Maybe it works on amxmodx <= 1.8.2 (not tested) */
void FN_Cvar_DirectSet_Post(struct cvar_s *var, char *value) 
{
    if(!var || !value || FStrEq(var->string, value))
    {
        RETURN_META(MRES_IGNORED);
    }
    if(!g_rehlds_available)
    {
        KZ_Cvar_DirectSet(var->name, value);
    }
    RETURN_META(MRES_IGNORED);
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void FN_CmdStart(const edict_t* player, const struct usercmd_s* cmd, unsigned int random_seed)
{
    int id = indexOfEdict(player);
    if (!MF_IsPlayerBot(id) && MF_IsPlayerAlive(id))
    {
        kz_rp_set_cmd(id, cmd);
        kz_ac_cmd(id, cmd);
    }
    RETURN_META(MRES_IGNORED);
}
void FN_PlayerPreThink(edict_t* pEntity)
{
    int id = indexOfEdict(pEntity);
    if (!MF_IsPlayerBot(id))
    {
    }
    RETURN_META(MRES_IGNORED);
}
void FN_PlayerPostThink(edict_t* pEntity)
{
    int id = indexOfEdict(pEntity);
    if (!MF_IsPlayerBot(id) && MF_IsPlayerAlive(id))
    {
        kz_rp_set_vars(id, &(pEntity->v));
        kz_rp_write_frame(id);
        kz_ac_postthink(id, pEntity);
    }
    RETURN_META(MRES_IGNORED);
}
BOOL FN_ClientConnect_Post(edict_t* pEntity, const char* pszName, const char* pszAddress, char szRejectReason[128])
{
    int id = ENTINDEX(pEntity);
    if (!MF_IsPlayerBot(id))
    {
        for(size_t i = 0; i < g_player_cvars_size; ++i)
        {
            CLIENT_COMMAND(pEntity, (char*)"%s %s\n", g_player_cvars[i].name, g_player_cvars[i].expected_value);
        }
        kz_ws_event_client_connect(pEntity);
    }
    RETURN_META_VALUE(MRES_IGNORED, TRUE);
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void RH_Cvar_DirectSet(IRehldsHook_Cvar_DirectSet* chain, cvar_t* var, const char* value)
{
    if (!var || !value || FStrEq(var->string, value))
    {
        chain->callNext(var, value);
        return;
    }
    chain->callNext(var, value);
    KZ_Cvar_DirectSet(var->name, value);
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void KZ_Cvar_DirectSet(const char* const varname, const char* const value)
{
    for (size_t i = 0; i < g_server_cvars_size; ++i)
    {
        if (FStrEq(varname, g_server_cvars[i].name) && !FStrEq(value, g_server_cvars[i].expected_value))
        {
            MF_Log("Illegal cvar value: %s %s", varname, value);
            CVAR_SET_STRING(varname, g_server_cvars[i].expected_value);
            return;
        }
    }
    if (!FStrEq(varname, kz_api_url->name) && !FStrEq(varname, kz_api_token->name))
    {
        return;
    }
    if (!kz_api_url->string || !kz_api_token->string || !kz_api_url->string[0] || !kz_api_token->string[0])
    {
        return;
    }
    if (g_websocket_state.load() > WSState::Uninitialized)
    {
        MF_Log("[WS] API settings change detected. Reconnecting...");
        kz_ws_stop();
    }
    kz_ws_start(kz_api_url->string, kz_api_token->string);
    return;
}
