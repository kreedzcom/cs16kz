#include "amxxmodule.h"
#include "common/hookchains.h"
#include "enginecallbacks.h"
#include "moduleconfig.h"
#include "resdk/mod_rehlds_api.h"

#include "amtl/os/am-shared-library.h"
#include "memtools/MemoryUtils.h"
#include "memtools/CDetour/detours.h"

#include "pdata.h"
#include "kz_player.h"
#include "kz_ws.h"
#include "kz_util.h"
#include "kz_cvars.h"
#include "kz_replay.h"
#include "kz_storage.h"
#include "kz_natives.h"
#include "kz_basic_ac.h"

#include <filesystem>

edict_t* g_pEdicts = nullptr;
player_t g_players[33];

int g_msg_teaminfo = -1;
bool g_initialized = false;
bool g_module_disabled = false;
bool g_early_mapchange = false;
float g_wait_after_load = 0.0f;

bool kz_init_rehooks(void);
bool kz_init_detours(void);

void kz_api_cmd(void);

void (*Cvar_DirectSet_Actual)(cvar_t* var, const char* value);
static CDetour* g_detour_Cvar_DirectSet = nullptr;

void (*SV_DropClient_Actual)(client_t* cl, qboolean crash, const char* format, ...);
static CDetour* g_detour_SV_DropClient = nullptr;

/***************************************************************************************************************/
/***************************************************************************************************************/
int FN_AMXX_CHECKGAME(const char* game)
{
    return (FStrEq(game, "cstrike") ? AMXX_GAME_OK : AMXX_GAME_BAD);
}

void FN_AMXX_ATTACH()
{
    g_pEdicts = (*g_engfuncs.pfnPEntityOfEntIndex)(0);
    
    kz_ws_register(0,                       kz_ws_ack_invalid);
    kz_ws_register(WSMsgIn::HELLO_ACK,      kz_ws_ack_hello);
    kz_ws_register(WSMsgIn::MAP_INFO,       kz_ws_ack_map_info);
    kz_ws_register(WSMsgOut::PLAYER_JOIN,   kz_ws_ack_player_join); // API echoes type 3 as the join ACK
    kz_ws_register(WSMsgIn::RECORD_ACK,     kz_ws_ack_record_ack);
    kz_ws_register(WSMsgIn::FILE_ACK,       kz_ws_ack_file_ack);
    kz_ws_register(WSMsgIn::ERROR_MSG,      kz_ws_ack_error);

    kz_api_url      = register_cvar("kz_api_url",  "wss://api.kreedz.com/ws/game", FCVAR_EXTDLL | FCVAR_PROTECTED | FCVAR_SPONLY);
    kz_api_token    = register_cvar("kz_api_token","", FCVAR_EXTDLL | FCVAR_PROTECTED | FCVAR_SPONLY);

    kz_api_log_send   = register_cvar("kz_api_log_send",   "1", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_log_recv   = register_cvar("kz_api_log_recv",   "1", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_log_upload = register_cvar("kz_api_log_upload", "1", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_log_parse  = register_cvar("kz_api_log_parse",  "1", FCVAR_EXTDLL | FCVAR_SPONLY);

    kz_api_retries_max    = register_cvar("kz_api_retries_max",    "4", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_retries_delay  = register_cvar("kz_api_retries_delay",  "5", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_replays_clevel = register_cvar("kz_api_replays_clevel", "10", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_replays_max    = register_cvar("kz_api_replays_max",    "-1", FCVAR_EXTDLL | FCVAR_SPONLY);

    kz_api_bot_prefix  = register_cvar("kz_api_bot_prefix", "[SR]", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_bot_team    = register_cvar("kz_api_bot_team", "1", FCVAR_EXTDLL | FCVAR_SPONLY);
    kz_api_bot_use_cmd = register_cvar("kz_api_bot_use_cmd", "0", FCVAR_EXTDLL | FCVAR_SPONLY);

    REG_SVR_COMMAND("kz_api", kz_api_cmd);
    kz_api_add_natives();
}
void FN_AMXX_PLUGINSLOADED()
{
    g_data_dir = std::filesystem::path("cstrike") / MF_GetLocalInfo("amxx_datadir", "addons/amxmodx/data");
    g_early_mapchange = false;
    g_wait_after_load = 1.0f;

    if (!g_initialized)
    {
        if (g_module_disabled)
        {
            return;
        }
        if (!kz_init_rehooks() && !kz_init_detours())
        {
            g_module_disabled = true;
            MF_Log("ERROR: Could not hook the engine (Cvar_DirectSet / SV_DropClient) - the module is DISABLED.");
            MF_Log("ERROR: Install ReHLDS, or add signatures for both functions to the common.games gamedata.");
            return;
        }
        g_initialized = true;

        kz_log_init(std::this_thread::get_id());
        kz_storage_init();
        kz_ws_init();
        kz_rp_init();
        kz_pb_init();
    }

    std::filesystem::path kz_config = std::filesystem::path("cstrike") / MF_GetLocalInfo("amxx_configsdir", "addons/amxmodx/configs") / "kz_global.cfg";
    if (std::filesystem::exists(kz_config) && std::filesystem::is_regular_file(kz_config))
    {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "exec %s\n", kz_config.string().c_str());

        SERVER_COMMAND(buffer);
        SERVER_EXECUTE();
    }
    kz_rp_update_header();

    if (g_initialized && g_websocket_state.load() == WSState::Connected)
    {
        g_current_map_info.updated = false;
        kz_ws_event_map_change();
    }

    kz_api_add_forwards();
    kz_storage_load();
}
void FN_GameShutdown()
{
    kz_log_flush(-1);
    if (g_initialized)
    {
        kz_rp_uninit();
        kz_pb_uninit();
        kz_ws_uninit();
        kz_storage_uninit();
    }
    if (g_detour_Cvar_DirectSet)
    {
        g_detour_Cvar_DirectSet->DisableDetour();
        g_detour_Cvar_DirectSet = nullptr;
    }
    if (g_detour_SV_DropClient)
    {
        g_detour_SV_DropClient->DisableDetour();
        g_detour_SV_DropClient = nullptr;
    }
}

/***************************************************************************************************************/
/***************************************************************************************************************/
void FN_StartFrame()
{
    if (!g_initialized)
    {
        RETURN_META(MRES_IGNORED);
    }
    kz_log_flush(50000000);
    kz_ac_frame();
    kz_ws_run_tasks(5);
    kz_pb_frame();

    RETURN_META(MRES_IGNORED);
}
void FN_ServerActivate_Post(edict_t* pEdictList, int edictCount, int clientMax)
{
    g_msg_teaminfo = GET_USER_MSG_ID(PLID, "TeamInfo", NULL);
    RETURN_META(MRES_IGNORED);
}
void FN_ServerDeactivate_Post(void)
{
    g_pEdicts = nullptr;
    if (g_initialized)
    {
        kz_pb_server_deactivate_post();
    }
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
    if (g_initialized)
    {
        kz_pb_spawn(pent);
    }
    RETURN_META_VALUE(MRES_IGNORED, FALSE);
}
void FN_DispatchThink(edict_t* pent)
{
    if (g_initialized)
    {
        kz_pb_think(pent);
    }
    RETURN_META(MRES_IGNORED);
}
int FN_AddToFullPack(struct entity_state_s *state, int e, edict_t *ent, edict_t *host, int hostflags, int player, unsigned char *pSet)
{
    if (g_initialized)
    {
        kz_pb_addtofullpack(state, e, ent, host, hostflags, player, pSet);
    }
    RETURN_META_VALUE(MRES_IGNORED, 0);
}
int FN_CheckVisibility(const edict_t *entity, unsigned char *pset)
{
    if (g_initialized)
    {
        int ret = kz_pb_check_visibility(entity, pset);
        if (ret != -1)
        {
            return(ret);
        }
    }
    RETURN_META_VALUE(MRES_IGNORED, 0);
}
void FN_ChangeLevel(const char *s1, const char *s2)
{
    g_early_mapchange = true;
    if (g_initialized)
    {
        kz_storage_clear();
    }
    RETURN_META(MRES_IGNORED);
}
void FN_MessageBegin(int msg_dest, int msg_type, const float *pOrigin, edict_t *ed)
{
    if(msg_type == SVC_INTERMISSION)
    {
        g_early_mapchange = true;
        if (g_initialized)
        {
            kz_storage_clear();
        }
    }
    RETURN_META(MRES_IGNORED);
}

void FN_CvarValue2(const edict_t* pEdict, int requestId, const char* cvar, const char* value)
{
    if (g_initialized)
    {
        kz_ac_querycvar_result(pEdict, requestId, cvar, value);
    }
    RETURN_META(MRES_IGNORED);
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void FN_CmdStart(const edict_t* player, const struct usercmd_s* cmd, unsigned int random_seed)
{
    if (!g_initialized)
    {
        RETURN_META(MRES_IGNORED);
    }
    int id = indexOfEdict(player);
    if (!MF_IsPlayerBot(id) && MF_IsPlayerAlive(id))
    {
        kz_rp_set_cmd(id, cmd);
        kz_ac_cmd(id, cmd);
    }
    RETURN_META(MRES_IGNORED);
}
void FN_PlayerPostThink(edict_t* pEntity)
{
    if (!g_initialized)
    {
        RETURN_META(MRES_IGNORED);
    }
    int id = indexOfEdict(pEntity);
    if (!MF_IsPlayerBot(id) && MF_IsPlayerAlive(id))
    {
        kz_rp_set_vars(id, &(pEntity->v));
        kz_rp_write_frame(id);
        kz_ac_postthink(id, pEntity);
    }
    RETURN_META(MRES_IGNORED);
}
BOOL FN_ClientConnect_Post(edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[ 128 ])
{
    if (g_initialized)
    {
        int id = indexOfEdict(pEntity);
        kz_ac_connect(id, pEntity);
    }
    RETURN_META_VALUE(MRES_IGNORED, TRUE);
}
void FN_ClientPutInServer_Post(edict_t* pEntity)
{
    int id = ENTINDEX(pEntity);
    g_players[id].is_bot = MF_IsPlayerBot(id);

    if (g_initialized && !g_players[id].is_bot)
    {
        for(size_t i = 0; i < g_player_cvars_size; ++i)
        {
            CLIENT_COMMAND(pEntity, (char*)"%s %s\n", g_player_cvars[i].name, g_player_cvars[i].expected_value);
        }

        char szIP[16];
        const char* szAuth = GETPLAYERAUTHID(pEntity);
        split_net_address(MF_GetPlayerIP(id), szIP, sizeof(szIP), nullptr, 0);

        snprintf(g_players[id].nickname, sizeof(g_players[0].nickname), "%s", MF_GetPlayerName(id));
        snprintf(g_players[id].ipaddr, sizeof(g_players[0].ipaddr), "%s", szIP);
        snprintf(g_players[id].steamid, sizeof(g_players[0].steamid), "%s", szAuth);

        snprintf(g_players[id].steamid_short, sizeof(g_players[0].steamid_short), "%s", szAuth);
        remove_substring(g_players[id].steamid_short, "STEAM_");
        remove_substring(g_players[id].steamid_short, ":");
        remove_substring(g_players[id].steamid_short, ":");

        kz_ws_event_client_connect(pEntity);
    }
    RETURN_META(MRES_IGNORED);
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void KZ_Cvar_DirectSet(cvar_t* var, const char* const value, IRehldsHook_Cvar_DirectSet* chain)
{
    if (!var || !value || FStrEq(var->string, value))
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
void KZ_SV_DropClient(edict_t* client)
{
    if (g_early_mapchange)
    {
        return;
    }

    int id = indexOfEdict(client);
    if (id <= 0 || id > gpGlobals->maxClients)
    {
        return;
    }
    if (g_players[id].is_bot)
    {
        return;
    }

    kz_ws_event_client_disconnect(client);
    memset(&g_players[id], 0, sizeof(g_players[0]));
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void RH_SV_DropClient(IRehldsHook_SV_DropClient* chain, IGameClient* cl, bool crash, const char* format)
{
    KZ_SV_DropClient(cl->GetEdict());

    // https://github.com/alliedmodders/amxmodx/blob/master/amxmodx/meta_api.cpp#998
    //
    char buffer[1024];
    ke::SafeStrcpy(buffer, sizeof(buffer), format);
    chain->callNext(cl, crash, buffer);

}
void DT_SV_DropClient(client_t* cl, qboolean crash, const char* format, ...)
{
    KZ_SV_DropClient(cl->edict);

    // https://github.com/alliedmodders/amxmodx/blob/master/amxmodx/meta_api.cpp#982
    //
    char buffer[1024];
    va_list ap;
    va_start(ap, format);
    ke::SafeVsprintf(buffer, sizeof(buffer) - 1, format, ap);
    va_end(ap);

    SV_DropClient_Actual(cl, crash, "%s", buffer);
}
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
bool kz_init_rehooks(void)
{
    if (RehldsApi_Init())
    {
        RehldsHookchains->Cvar_DirectSet()->registerHook(RH_Cvar_DirectSet, HC_PRIORITY_UNINTERRUPTABLE);
        RehldsHookchains->SV_DropClient()->registerHook(RH_SV_DropClient, HC_PRIORITY_UNINTERRUPTABLE);
        return true;
    }
    return false;
}
bool kz_init_detours(void)
{
    IGameConfigManager* ConfigManager = MF_GetConfigManager();
    IGameConfig* CommonConfig = nullptr;

    if (!ConfigManager)
    {
        MF_Log("ERROR: AMX Mod X 1.8.2 is not supported - live with it and upgrade...");
        return false;
    }

    char error[256] = {0};
    if (!ConfigManager->LoadGameConfigFile("common.games", &CommonConfig, error, sizeof(error)) || error[0] != '\0')
    {
        MF_Log("ERROR: common.games gamedata could not be read: %s", error);
        return false;
    }

    void* address = nullptr;

    /* Cvar_DirectSet */
    if (!CommonConfig->GetMemSig("Cvar_DirectSet", &address) || !address)
    {
        MF_Log("ERROR: Failed to find \"Cvar_DirectSet\" function.");
        return false;
    }

    g_detour_Cvar_DirectSet = CDetourManager::CreateDetour((void*)&DT_Cvar_DirectSet, (void**)&Cvar_DirectSet_Actual, address);
    if (!g_detour_Cvar_DirectSet)
    {
        MF_Log("ERROR: Failed to create \"Cvar_DirectSet\" detour");
        return false;
    }
    g_detour_Cvar_DirectSet->EnableDetour();

    /* SV_DropClient */
    address = nullptr;
    if (!CommonConfig->GetMemSig("SV_DropClient", &address) || !address)
    {
        MF_Log("ERROR: Failed to find \"SV_DropClient\" function.");

        g_detour_Cvar_DirectSet->DisableDetour();
        g_detour_Cvar_DirectSet = nullptr;
        return false;
    }

    g_detour_SV_DropClient = CDetourManager::CreateDetour((void*)&DT_SV_DropClient, (void**)&SV_DropClient_Actual, address);
    if (!g_detour_SV_DropClient)
    {
        MF_Log("ERROR: Failed to create \"SV_DropClient\" detour");

        g_detour_Cvar_DirectSet->DisableDetour();
        g_detour_Cvar_DirectSet = nullptr;
        return false;
    }
    g_detour_SV_DropClient->EnableDetour();
    return true;
}
/***************************************************************************************************************/
/***************************************************************************************************************/
static void kz_api_cmd_usage()
{
    MF_PrintSrvConsole("Usage:\n");
    MF_PrintSrvConsole("  kz_api status                                   - websocket, queue and storage overview\n");
    MF_PrintSrvConsole("  kz_api reconnect                                - force a websocket reconnect\n\n");

    MF_PrintSrvConsole("  kz_api playback reload                          - reload the best replay for the current map\n");
    MF_PrintSrvConsole("  kz_api playback load <file>                     - load a specific replay (kicks the active one)\n");
    MF_PrintSrvConsole("  kz_api playback speed <1|2>                     - playback speed of the SR bot\n\n");

    MF_PrintSrvConsole("  kz_api krp compress <file>                      - .krpr -> .krpz\n");
    MF_PrintSrvConsole("  kz_api krp decompress <file>                    - .krpz -> .krpr\n\n");

    MF_PrintSrvConsole("  kz_api storage checkpoint                       - force a WAL checkpoint (truncate)\n\n");
    MF_PrintSrvConsole("  kz_api storage requeue <id|all>                 - resend pending message(s) now\n");
    MF_PrintSrvConsole("  kz_api storage list <outgoing|upload> [n]       - first n rows; negative n = last n (default 10)\n");
    MF_PrintSrvConsole("  kz_api storage show <outgoing|upload> <id>      - print a row's full payload\n");
    MF_PrintSrvConsole("  kz_api storage delete <outgoing|upload> <id>\n");
    MF_PrintSrvConsole("  kz_api storage clear <outgoing|upload> confirm\n");
}
void kz_api_cmd(void)
{
    const int argc = CMD_ARGC();

    if (argc >= 2 && FStrEq(CMD_ARGV(1), "version"))
    {
        MF_PrintSrvConsole("[%s] version:  %s\n", MODULE_LOGTAG, MODULE_VERSION);
        MF_PrintSrvConsole("[%s] checksum: %s\n\n", MODULE_LOGTAG, MODULE_CHECKSUM);
        return;
    }

    if (!g_initialized)
    {
        MF_PrintSrvConsole("[%s] Module is DISABLED: could not hook the engine (no ReHLDS and no usable gamedata signatures).\n", MODULE_LOGTAG);
        return;
    }
    if (argc < 2)
    {
        kz_api_cmd_usage();
        return;
    }

    const char* domain = CMD_ARGV(1);
    if (FStrEq(domain, "status"))
    {
        size_t retry_size = 0;
        size_t active_uploads_size = 0;

        uintmax_t db_size = 0;
        uintmax_t wal_size = 0;

        {
            std::lock_guard<std::mutex> lock(g_retry_mtx);
            retry_size = g_retry_queue.size();
        }
        {
            std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
            active_uploads_size = g_active_uploads.size();
        }
        {
            std::error_code ec;
            std::filesystem::path db_path  = g_data_dir / "kz_global" / "sqlite3" / "storage.sq3";
            std::filesystem::path wal_path = db_path;
            wal_path += "-wal";

            db_size = std::filesystem::file_size(db_path, ec);
            if (ec)
            {
                db_size = 0;
                ec.clear();
            }
            wal_size = std::filesystem::file_size(wal_path, ec);
            if (ec)
            {
                wal_size = 0;
            }
        }

        MF_PrintSrvConsole("[%s] websocket:      %s\n", MODULE_LOGTAG, ws_state_name(g_websocket_state.load()));
        MF_PrintSrvConsole("[%s] writer queue:   %zu/%zu\n", MODULE_LOGTAG, g_replay_writer_queue.size(), g_replay_writer_queue.capacity());
        MF_PrintSrvConsole("[%s] upload queue:   %zu/%zu\n", MODULE_LOGTAG, g_replay_upload_queue.size(), g_replay_upload_queue.capacity());
        MF_PrintSrvConsole("[%s] incoming queue: %zu/%zu\n", MODULE_LOGTAG, g_incoming_queue.size(), g_incoming_queue.capacity());
        MF_PrintSrvConsole("[%s] outgoing queue: %zu/%zu\n", MODULE_LOGTAG, g_outgoing_queue.size(), g_outgoing_queue.capacity());
        MF_PrintSrvConsole("[%s] retry queue:    %zu\n", MODULE_LOGTAG, retry_size);
        MF_PrintSrvConsole("[%s] active uploads: %zu\n", MODULE_LOGTAG, active_uploads_size);
        MF_PrintSrvConsole("[%s] storage rows:   outgoing=%lld upload=%lld\n", MODULE_LOGTAG, static_cast<long long>(kz_storage_count(StorageTable::outgoing_queue)),static_cast<long long>(kz_storage_count(StorageTable::upload_queue)));
        MF_PrintSrvConsole("[%s] db size:        %s (wal: %s)\n\n", MODULE_LOGTAG, format_bytes(db_size).c_str(), format_bytes(wal_size).c_str());
        return;
    }
    else if (FStrEq(domain, "reconnect"))
    {
        if (!kz_api_url->string || !kz_api_url->string[0] || !kz_api_token->string || !kz_api_token->string[0])
        {
            MF_PrintSrvConsole("[%s] Configure kz_api_url / kz_api_token first.\n\n", MODULE_LOGTAG);
            return;
        }
        if (g_websocket_state.load() > WSState::Uninitialized)
        {
            kz_ws_stop();
        }

        MF_PrintSrvConsole("[%s] Manual reconnect invoked...\n\n", MODULE_LOGTAG);
        kz_ws_start(kz_api_url->string, kz_api_token->string);
        return;
    }
    else if (FStrEq(domain, "playback"))
    {
        if (argc < 3)
        {
            kz_api_cmd_usage();
            return;
        }

        const char* action = CMD_ARGV(2);
        if (FStrEq(action, "reload"))
        {
            std::filesystem::path file = kz_pb_find_fastest(STRING(gpGlobals->mapname));
            if (file.empty())
            {
                MF_PrintSrvConsole("[%s] No replays found for the current map.\n\n", MODULE_LOGTAG);
                return;
            }
            kz_pb_parse_file_async(file);
            MF_PrintSrvConsole("[%s] Queued replay for playback: %s\n\n", MODULE_LOGTAG, file.filename().string().c_str());
        }
        else if (FStrEq(action, "load") && argc >= 4)
        {
            std::filesystem::path file = g_data_dir / "kz_global" / "replays" / CMD_ARGV(3);
            if (file.extension() != ".krpz")
            {
                MF_PrintSrvConsole("[%s] \"playback load\" expects a .krpz file \n\n", MODULE_LOGTAG);
                return;
            }

            std::error_code ec;
            if (!std::filesystem::is_regular_file(file, ec))
            {
                MF_PrintSrvConsole("[%s] File not found: %s\n\n", MODULE_LOGTAG, CMD_ARGV(3));
                return;
            }
            kz_pb_parse_file_async(file);
            MF_PrintSrvConsole("[%s] Queued replay for playback: %s\n\n", MODULE_LOGTAG, file.filename().string().c_str());
        }
        else if (FStrEq(action, "speed") && argc >= 4)
        {
            if (!g_pb_bot_data)
            {
                MF_PrintSrvConsole("[%s] No active playback.\n\n", MODULE_LOGTAG);
                return;
            }

            g_pb_bot_data->double_speed = FStrEq(CMD_ARGV(3), "2");
            MF_PrintSrvConsole("[%s] Bot playback speed: %s\n\n", MODULE_LOGTAG, g_pb_bot_data->double_speed ? "2x":"1x");
        }
        else
        {
            kz_api_cmd_usage();
            return;
        }
        return;
    }
    else if (FStrEq(domain, "krp"))
    {
        if (argc < 4)
        {
            kz_api_cmd_usage();
            return;
        }

        const char* action = CMD_ARGV(2);
        if (!FStrEq(action, "compress") && !FStrEq(action, "decompress"))
        {
            kz_api_cmd_usage();
            return;
        }

        const bool compress = FStrEq(action, "compress");
        const char* want_ext = compress ? ".krpr" : ".krpz";
        std::filesystem::path file = g_data_dir / "kz_global" / "replays" / CMD_ARGV(3);

        if (file.extension() != want_ext)
        {
            MF_PrintSrvConsole("[%s] %s expects a %s file.\n\n", MODULE_LOGTAG, action, want_ext);
            return;
        }

        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec))
        {
            MF_PrintSrvConsole("[%s] File not found: %s\n\n", MODULE_LOGTAG, CMD_ARGV(3));
            return;
        }

        const bool ok = compress ? kz_rp_compress_replay(file) : kz_rp_decompress_replay(file);
        std::filesystem::path out_path(file);
        out_path.replace_extension(compress ? ".krpz" : ".krpr");

        if (ok)
        {
            uintmax_t in_size = std::filesystem::file_size(file, ec);
            if (ec)
            {
                in_size = 0;
                ec.clear();
            }

            uintmax_t out_size = std::filesystem::file_size(out_path, ec);
            if (ec)
            {
                out_size = 0;
            }
            MF_PrintSrvConsole("[%s] %s -> %s (%s -> %s)\n\n", MODULE_LOGTAG, file.filename().string().c_str(), out_path.filename().string().c_str(), format_bytes(in_size).c_str(), format_bytes(out_size).c_str());
        }
        else
        {
            MF_PrintSrvConsole("[%s] Failed to %s %s (corrupt or unreadable file).\n\n", MODULE_LOGTAG, action, CMD_ARGV(3));
        }
        return;
    }
    else if (FStrEq(domain, "storage"))
    {
        if (argc < 3)
        {
            kz_api_cmd_usage();
            return;
        }

        const char* action = CMD_ARGV(2);
        StorageTable table = StorageTable::outgoing_queue;
        const bool has_table = (argc >= 4) && (FStrEq(CMD_ARGV(3), "outgoing") || FStrEq(CMD_ARGV(3), "upload"));
        if (has_table && FStrEq(CMD_ARGV(3), "upload"))
        {
            table = StorageTable::upload_queue;
        }

        if (FStrEq(action, "list") && has_table)
        {
            int limit = (argc >= 5) ? atoi(CMD_ARGV(4)) : 10;
            const bool from_end = (limit < 0); // negative n = last n rows (newest first)
            if (from_end)
            {
                limit = -limit;
            }
            if (limit == 0)
            {
                limit = 10;
            }
            kz_storage_print(table, limit, from_end);
        }
        else if (FStrEq(action, "show") && has_table && argc >= 5)
        {
            int64_t id = static_cast<int64_t>(atoll(CMD_ARGV(4)));
            kz_storage_print_row(table, id);
        }
        else if (FStrEq(action, "requeue") && argc >= 4)
        {
            const char* arg  = CMD_ARGV(3);
            const bool all   = FStrEq(arg, "all");
            const int64_t id = all ? 0 : static_cast<int64_t>(atoll(arg));

            int requeued = 0;
            {
                std::lock_guard<std::mutex> lock(g_retry_mtx);
                for (auto& r : g_retry_queue)
                {
                    if (all || r.msg_id == id)
                    {
                        r.timestamp   = 0; // due immediately on the next retry pass
                        r.retry_count = 0;
                        requeued++;
                    }
                }
            }
            MF_PrintSrvConsole("[%s] Requeued %d message(s) for immediate retry.\n\n", MODULE_LOGTAG, requeued);
        }
        else if (FStrEq(action, "delete") && has_table && argc >= 5)
        {
            int64_t id = static_cast<int64_t>(atoll(CMD_ARGV(4)));
            kz_storage_delete(id, table);
            MF_PrintSrvConsole("[%s] Deleted id %lld from %s (if it existed).\n\n", MODULE_LOGTAG, static_cast<long long>(id), CMD_ARGV(3));
        }
        else if (FStrEq(action, "clear") && has_table)
        {
            if (argc < 5 || !FStrEq(CMD_ARGV(4), "confirm"))
            {
                MF_PrintSrvConsole("[%s] This purges the whole table. Repeat with: kz_api storage clear %s confirm\n\n", MODULE_LOGTAG, CMD_ARGV(3));
                return;
            }
            kz_storage_delete_all(table);
            MF_PrintSrvConsole("[%s] Cleared %s (rows + pending retries).\n\n", MODULE_LOGTAG, CMD_ARGV(3));
        }
        else if (FStrEq(action, "checkpoint"))
        {
            MF_PrintSrvConsole(kz_storage_checkpoint()
                ? "[%s] WAL checkpoint done.\n\n"
                : "[%s] WAL checkpoint failed (see log).\n\n", MODULE_LOGTAG);
        }
        else
        {
            kz_api_cmd_usage();
        }
        return;
    }
    else
    {
        kz_api_cmd_usage();
    }
}
