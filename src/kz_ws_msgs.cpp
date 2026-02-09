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

#include <filesystem>

#define ACK_CHECK_MISSING(X) \
    do { \
        if (!json_object_dotget_value(obj, #X)) { \
            kz_log(nullptr, "[%s] Error: missing %s.", __FUNCTION__, #X); \
            return; \
        } \
    } while(0)

WSMessageFunc g_callback_table[ectoi(WSMessageType::_MAX)];

void kz_ws_run_tasks(int max_tasks_per_frame)
{
    // TODO: check storage if we need to resent something
    int tasks_done = 0;

    while(g_incoming_queue.front() && tasks_done < max_tasks_per_frame)
    {
        JSON_Value* root_val    = *g_incoming_queue.front();
        JSON_Object* root_obj   = json_value_get_object(root_val);
        int index               = json_object_get_number(root_obj, "msg_type");

        if(index <= 0 || index >= ectoi(WSMessageType::_MAX))
        {
            g_callback_table[ectoi(WSMessageType::invalid)](root_obj);
        }
        else
        {
            g_callback_table[index](root_obj);
        }

        json_value_free(root_val);
        g_incoming_queue.pop();
        tasks_done++;
    }
    if(g_websocket_state.load() == WSState::Connected && g_websocket.getReadyState() == ix::ReadyState::Open)
    {
        while(g_outgoing_queue.front() && tasks_done < max_tasks_per_frame)
        {
            std::string* message = g_outgoing_queue.front();
            kz_ws_send_msg(*message, 0);

            g_outgoing_queue.pop();
            tasks_done++;
        }
    }
}
void kz_ws_register(WSMessageType type, WSMessageFunc pfn)
{
    g_callback_table[ectoi(type)] = pfn;
}
void kz_ws_event_client_connect(edict_t* pEntity)
{
    int id = indexOfEdict(pEntity);

    JSON_Value* data_val = json_value_init_object();
    JSON_Object* data_obj = json_value_get_object(data_val);

    std::string message;
    int64_t msg_id = kz_storage_get_next_id(StorageTable::outgoing_queue);

    json_object_set_string(data_obj, "nickname", g_players[id].nickname);
    json_object_set_string(data_obj, "ipaddr", g_players[id].ipaddr);
    json_object_set_string(data_obj, "steamid", g_players[id].steamid);

    kz_ws_build_msg(WSMessageType::client_info, data_val, message, msg_id);
    kz_storage_save(message, msg_id, StorageTable::outgoing_queue);
    kz_ws_queue_msg(message, msg_id);
}
void kz_ws_ack_invalid(JSON_Object* obj)
{
    kz_log(nullptr,"[kz_ws_ack_invalid] Invalid msg_id: %d", json_object_get_number(obj, "msg_id"));
}
void kz_ws_ack_hello(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.heartbeat_interval);

    int heartbeat_interval = json_object_dotget_number(obj, "data.heartbeat_interval");
    g_websocket.setPingInterval(heartbeat_interval);
    kz_log(nullptr,"[kz_ws_ack_hello] Heartbeat interval: %d", heartbeat_interval);
}
void kz_ws_ack_map_info(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.mapname);
    ACK_CHECK_MISSING(data.wr_txt);
    ACK_CHECK_MISSING(data.type);
    ACK_CHECK_MISSING(data.length);
    ACK_CHECK_MISSING(data.difficulty);

    const char* mapname = json_object_dotget_string(obj, "data.mapname");
    const char* wr      = json_object_dotget_string(obj, "data.wr_txt");

    int map_props[3];
    map_props[0]    = json_object_dotget_number(obj, "data.type");
    map_props[1]    = json_object_dotget_number(obj, "data.length");
    map_props[2]    = json_object_dotget_number(obj, "data.difficulty");

    int64_t msg_id = json_object_get_number(obj, "msg_id");
    auto it = g_plugin_callbacks.find(msg_id);

    if(it != g_plugin_callbacks.end())
    {
        MF_ExecuteForward(it->second.fwd, mapname, wr, MF_PrepareCellArray(map_props, sizeof(map_props)));
        MF_UnregisterSPForward(it->second.fwd);
    }
    else
    {
        kz_log(nullptr,"[kz_ws_ack_map_info] Failed to find %lld in g_plugin_callbacks", msg_id);
    }
}
void kz_ws_ack_client_info(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.banned);
    bool is_banned = json_object_dotget_boolean(obj, "data.banned");
    
    if(!is_banned)
    {
        return;
    }

    ACK_CHECK_MISSING(data.steamid);
    const char* authid = json_object_dotget_string(obj, "data.steamid");
    if(!authid || !authid[0])
    {
        return;
    }

    edict_t* pEntity = find_player_by_authid(authid);
    if(FNullEnt(pEntity))
    {
        return;
    }

    ACK_CHECK_MISSING(data.by);
    char buff[192];
    const char* banned_by = json_object_dotget_string(obj, "data.by");

    snprintf(buff, sizeof(buff), "kick #%d \"You've been cross-community banned by %s\"\n", GETPLAYERUSERID(pEntity), banned_by);
    SERVER_COMMAND(buff);

    snprintf(buff, sizeof(buff), "banid 2 %s\n", authid);
    SERVER_COMMAND(buff);
}
void kz_ws_ack_add_record(JSON_Object* obj)
{
     ACK_CHECK_MISSING(data.rec_id);
     ACK_CHECK_MISSING(data.local_uid);

     ws_upload_replay upload = {0};
     const char* local_uid = json_object_dotget_string(obj, "data.local_uid");
     upload.rec_id = json_object_dotget_number(obj, "data.rec_id");

     std::filesystem::path replay = g_data_dir / "replays" / STRING(gpGlobals->mapname) / local_uid;
     replay.replace_extension(".krpr");

     snprintf(upload.filepath, sizeof(upload.filepath), "%s", replay.c_str());
     snprintf(upload.local_uid, sizeof(upload.local_uid), "%s", local_uid);

     if (std::filesystem::exists(replay) && std::filesystem::is_regular_file(replay))
     {
         kz_log(nullptr, "[WS] Starting compression/upload of replay: %s", replay.c_str());
         kz_rp_compress_and_upload_async(upload);
     }
     else
     {
         kz_log(nullptr, "[WS] Replay file does not exist or is not regular file: %s", replay.c_str());
         return;
     }
     //kz_storage_set_ack(std::string(local_uid), rec_id);

}
void kz_ws_ack_del_record(JSON_Object* obj)
{
    kz_log(nullptr, "[kz_ws_ack_del_record]");
}
void kz_ws_ack_add_replay(JSON_Object* obj)
{
    kz_log(nullptr, "[kz_ws_ack_add_replay]");
}
void kz_ws_ack_get_replay(JSON_Object* obj)
{
    kz_log(nullptr, "[kz_ws_ack_get_replay]");
}
