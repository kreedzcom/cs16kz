#include "amxxmodule.h"
#include "common/hookchains.h"
#include "resdk/mod_rehlds_api.h"

#include "IGameConfigs.h"
#include "memtools/CDetour/detours.h"

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

void (*Cvar_DirectSet_Actual)(cvar_t* var, const char* value);

void RH_Cvar_DirectSet(IRehldsHook_Cvar_DirectSet* chain, cvar_t* var, const char* value);
void DT_Cvar_DirectSet(cvar_t* var, const char* value);
void KZ_Cvar_DirectSet(cvar_t* var, const char* value, IRehldsHook_Cvar_DirectSet* chain);

static bool kz_init_hooks(void)
{
    if (RehldsApi_Init())
    {
        RehldsHookchains->Cvar_DirectSet()->registerHook(RH_Cvar_DirectSet, HC_PRIORITY_UNINTERRUPTABLE);
        return true;
    }

    // We are supposed to unhook and destroy for cleanup but..
    // This module is not intended to be reloaded or unloaded in the 1st place, so we dont care

    IGameConfigManager* ConfigManager = MF_GetConfigManager();
    IGameConfig* CommonConfig = nullptr;
    char error[256] = {0};

    if (!ConfigManager || !ConfigManager->LoadGameConfigFile("common.games", &CommonConfig, error, sizeof(error)) || error[0] != '\0')
    {
        return false;
    }

    CDetour* detour_Cvar_DirectSet = nullptr;
    void* address = nullptr;
    if (CommonConfig && CommonConfig->GetMemSig("Cvar_DirectSet", &address) && address)
    {
        detour_Cvar_DirectSet = CDetourManager::CreateDetour((void*)&DT_Cvar_DirectSet, (void**)&Cvar_DirectSet_Actual, address);
        detour_Cvar_DirectSet->EnableDetour();
        return true;
    }
    return false;
}
/***************************************************************************************************************/
/***************************************************************************************************************/
int FN_AMXX_CHECKGAME(const char* game)
{
    return (FStrEq(game, "cstrike") ? AMXX_GAME_OK : AMXX_GAME_BAD);
}

void FN_AMXX_ATTACH()
{
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
}
void FN_AMXX_PLUGINSLOADED()
{
    g_data_dir = std::filesystem::path("cstrike") / MF_GetLocalInfo("amxx_datadir", "addons/amxmodx/data") / "kz_global";

    if (!g_initialiazed)
    {
        if (!kz_init_hooks())
        {
            MF_Log("?????");
            *((char *) NULL) = 0;
        }
        g_initialiazed = true;

        kz_log_init(std::this_thread::get_id());
        kz_storage_init();
        kz_ws_init();
        kz_rp_init();
    }
    kz_rp_update_header();
    kz_api_add_forwards();
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
    KZ_Cvar_DirectSet(var, value, chain);
}
void DT_Cvar_DirectSet(struct cvar_s *var, const char *value) 
{
    KZ_Cvar_DirectSet(var, value, nullptr);
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void KZ_Cvar_DirectSet(cvar_t* var, const char* const value, IRehldsHook_Cvar_DirectSet* chain)
{
    if (!var || !value || FStrEq(var->name, value))
    {
        (chain) ? chain->callNext(var, value) : Cvar_DirectSet_Actual(var, value);
        return;
    }
    (chain) ? chain->callNext(var, value) : Cvar_DirectSet_Actual(var, value);

    for (size_t i = 0; i < g_server_cvars_size; ++i)
    {
        if (FStrEq(var->name, g_server_cvars[i].name) && !FStrEq(value, g_server_cvars[i].expected_value))
        {
            MF_Log("Illegal cvar value: %s %s", var->name, value);
            CVAR_SET_STRING(var->name, g_server_cvars[i].expected_value);
            return;
        }
    }
    if (!FStrEq(var->name, kz_api_url->name) && !FStrEq(var->name, kz_api_token->name))
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
