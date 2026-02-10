#include "amxxmodule.h"

#include "pdata.h"
#include "kz_ws.h"
#include "kz_util.h"
#include "kz_cvars.h"
#include "kz_replay.h"
#include "kz_storage.h"
#include "kz_natives.h"

static inline int validate_params(AMX* amx, cell* params, int min_params)
{
    int total_params = (params[0] / sizeof(cell));
    if (total_params < min_params)
    {
        MF_LogError(amx, AMX_ERR_NATIVE, "Expected at least %d params, got %d", min_params, total_params);
        return 0;
    }
    return 1;
}
static inline int validate_player(AMX* amx, int id) 
{
    if (id < 1 || id > gpGlobals->maxClients)
    {
        MF_LogError(amx, AMX_ERR_NATIVE, "Invalid player index %d", id);
        return 0;
    }
    if (!MF_IsPlayerIngame(id))
    {
        MF_LogError(amx, AMX_ERR_NATIVE, "Player not in-game");
        return 0;
    }
    if (MF_IsPlayerBot(id)) 
    {
        MF_LogError(amx, AMX_ERR_NATIVE, "Bots can't make records!");
        return 0;
    }
    if (!MF_IsPlayerAlive(id)) 
    {
        MF_LogError(amx, AMX_ERR_NATIVE, "Player not alive");
        return 0;
    }
    return id;
}

/* native kz_api_get_map_details(mapname[], handler[]); */
static cell AMX_NATIVE_CALL kz_api_get_map_details(AMX* amx, cell* params)
{
    if (!validate_params(amx, params, 2))
    {
        return 0;
    }

    int len = 0;
    char* mapname = MF_GetAmxString(amx, params[1], 0, &len);
    char* handler = MF_GetAmxString(amx, params[2], 1, &len);

    int fwd = MF_RegisterSPForwardByName(amx, handler, FP_STRING, FP_STRING, FP_STRING, FP_ARRAY, FP_DONE);
    if (fwd == -1)
    {
        MF_LogError(amx, AMX_ERR_NATIVE, "Function is not present \"%s\"", handler);
        return 0;
    }

    JSON_Value* data_val = json_value_init_object();
    JSON_Object* data_obj = json_value_get_object(data_val);
    json_object_set_string(data_obj, "mapname", mapname);

    std::string message;
    int64_t msg_id = kz_storage_get_next_id(StorageTable::outgoing_queue);

    g_plugin_callbacks[msg_id] = { fwd, std::vector<int>() };

    kz_ws_build_msg(WSMessageType::map_info, data_val, message, msg_id);
    kz_storage_save(message, ectoi(WSMessageType::map_info), msg_id, StorageTable::outgoing_queue);
    kz_ws_queue_msg(message, msg_id);
    return 1;
}

/* native kz_api_run_started(id) */
static cell AMX_NATIVE_CALL kz_api_run_started(AMX* amx, cell* params)
{
    if (!validate_params(amx, params, 1))
    {
        return 0;
    }

    int id = validate_player(amx, params[1]);;
    return (id ? kz_rp_run_started(id) : 0);
}

/* native kz_api_run_checkpoint(id) */
static cell AMX_NATIVE_CALL kz_api_run_checkpoint(AMX* amx, cell* params)
{
    if (!validate_params(amx, params, 1))
    {
        return 0;
    }

    int id = validate_player(amx, params[1]);;
    return (id ? kz_rp_run_checkpoint(id) : 0);
}

/* native kz_api_run_gocheck(id) */
static cell AMX_NATIVE_CALL kz_api_run_gocheck(AMX* amx, cell* params)
{
    if (!validate_params(amx, params, 1))
    {
        return 0;
    }

    int id = validate_player(amx, params[1]);;
    return (id ? kz_rp_run_gocheck(id) : 0);
}

/* native kz_api_run_paused(id) */
static cell AMX_NATIVE_CALL kz_api_run_paused(AMX* amx, cell* params)
{
    if (!validate_params(amx, params, 1))
    {
        return 0;
    }

    int id = validate_player(amx, params[1]);;
    return (id ? kz_rp_run_paused(id) : 0);
}

/* native kz_api_run_unpaused(id) */
static cell AMX_NATIVE_CALL kz_api_run_unpaused(AMX* amx, cell* params)
{
    if (!validate_params(amx, params, 1))
    {
        return 0;
    }

    int id = validate_player(amx, params[1]);;
    return (id ? kz_rp_run_unpaused(id) : 0);
}

/* native kz_api_run_rejected(id, bool:delete_file) */
static cell AMX_NATIVE_CALL kz_api_run_rejected(AMX* amx, cell* params)
{
    if (!validate_params(amx, params, 2))
    {
        return 0;
    }

    int id = validate_player(amx, params[1]);;
    return (id ? kz_rp_run_rejected(id, static_cast<bool>(params[2])) : 0);
}
/* native kz_api_run_finished(id, Float:time) */
static cell AMX_NATIVE_CALL kz_api_run_finished(AMX* amx, cell* params)
{
    if (!validate_params(amx, params, 2))
    {
        return 0;
    }

    int id = validate_player(amx, params[1]);;
    return (id ? kz_rp_run_finished(id, amx_ctof(params[2])) : 0);
}

static cell AMX_NATIVE_CALL kz_api_del_record(AMX* amx, cell* params)
{
    MF_LogError(amx, AMX_ERR_NATIVE, "Not implemented.");
    return 1;
}

static cell AMX_NATIVE_CALL kz_api_get_replay(AMX* amx, cell* params)
{
    MF_LogError(amx, AMX_ERR_NATIVE, "Not implemented.");
    return 1;
}

AMX_NATIVE_INFO kz_api_natives[] =
{
    {"kz_api_get_map_details",  kz_api_get_map_details},
    {"kz_api_run_started",      kz_api_run_started},
    {"kz_api_run_checkpoint",   kz_api_run_checkpoint},
    {"kz_api_run_gocheck",      kz_api_run_gocheck},
    {"kz_api_run_paused",       kz_api_run_paused},
    {"kz_api_run_unpaused",     kz_api_run_unpaused},
    {"kz_api_run_rejected",     kz_api_run_rejected},
    {"kz_api_run_finished",     kz_api_run_finished},
    {"kz_api_del_record",       kz_api_del_record},
    {"kz_api_get_replay",       kz_api_get_replay},
    {NULL, NULL},
};

int fwd_on_record_added = -1;
int fwd_on_replay_downloaded = -1;

std::map<int64_t, plugin_callback_data> g_plugin_callbacks;

void kz_api_add_forwards(void)
{
    int fwd = MF_RegisterForward("__kz_global_api_version_check", ET_IGNORE, FP_CELL, FP_CELL, FP_DONE);
    MF_ExecuteForward(fwd, MODULE_VERSION_MAJOR, MODULE_VERSION_MINOR);

    fwd_on_record_added      = MF_RegisterForward("kz_api_on_record_added", ET_IGNORE, FP_CELL, FP_ARRAY, FP_CELL, FP_DONE);
    fwd_on_replay_downloaded = MF_RegisterForward("kz_api_on_replay_downloaded", ET_IGNORE, FP_CELL, FP_STRING, FP_DONE);
}
void kz_api_add_natives(void)
{
    MF_AddNatives(kz_api_natives);
}
