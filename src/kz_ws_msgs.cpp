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
            kz_log(&g_ws_log, "[%s] Error: missing %s.", __FUNCTION__, #X); \
            return nullptr; \
        } \
    } while(0)

std::mutex g_active_uploads_mtx;
std::set<std::string> g_active_uploads;

WSMessageFunc g_callback_table[ectoi(WSMessageType::_MAX)];

void kz_ws_run_tasks(int max_tasks_per_frame)
{
    int tasks_done = 0;

    while (g_incoming_queue.front() && tasks_done < max_tasks_per_frame)
    {
        std::function<void()>* fn = g_incoming_queue.front();
        if (fn && *fn)
        {
            (*fn)();
        }

        g_incoming_queue.pop();
        tasks_done++;
    }
    if (g_websocket_state.load() != WSState::Connected || g_websocket.getReadyState() != ix::ReadyState::Open)
    {
        return;
    }
    while (g_outgoing_queue.front() && tasks_done < max_tasks_per_frame)
    {
        std::string* message = g_outgoing_queue.front();
        kz_ws_send_msg(*message, 0);

        g_outgoing_queue.pop();
        tasks_done++;
    }
    if (!g_outgoing_queue.empty() || tasks_done >= max_tasks_per_frame)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_retry_mtx);

        auto it  = g_retry_queue.begin();
        auto now = std::chrono::system_clock::now();
        auto ts  = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        while (it != g_retry_queue.end())
        {
            if ((ts - it->timestamp) > 5)
            {
                if (it->message.length() == 0 || it->retry_count > 4)
                {
                    it = g_retry_queue.erase(it);
                    continue;
                }
                if (it->table == StorageTable::outgoing_queue)
                {
                    kz_ws_send_msg(it->message, it->msg_id);
                }
                else if (it->table == StorageTable::upload_queue)
                {
                    std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
                    if (g_active_uploads.find(it->message) == g_active_uploads.end())
                    {
                        ws_upload metadata = {};
                        metadata.id = it->msg_type;

                        std::filesystem::path replay = g_data_dir / "replays" / STRING(gpGlobals->mapname) / it->message;
                        replay.replace_extension(".krpz");

                        snprintf(metadata.local_uid, sizeof(metadata.local_uid), "%s", it->message.c_str());

                        if (!std::filesystem::exists(replay) || !std::filesystem::is_regular_file(replay))
                        {
                            replay.replace_extension(".krpr");
                        }


                        snprintf(metadata.filepath, sizeof(metadata.filepath), "%s", replay.c_str());

                        if (std::filesystem::exists(replay) && std::filesystem::is_regular_file(replay))
                        {
                            g_active_uploads.insert(it->message);

                            kz_log(nullptr, "[WS] Retrying upload of replay: %s", replay.c_str());
                            kz_rp_compress_and_upload_async(metadata);
                        }
                        else
                        {
                            kz_log(nullptr, "[WS] Retry upload failed (file doesnt exist): %s", replay.c_str());
                        }
                    }
                }

                it->timestamp = ts;
                it->retry_count++;
                break;
            }
            ++it;
        }
        if (!g_retry_queue.empty())
        {
            return;
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
    kz_storage_save(message, ectoi(WSMessageType::client_info), msg_id, StorageTable::outgoing_queue);
    kz_ws_queue_msg(message, msg_id);
}
std::function<void()> kz_ws_ack_invalid(JSON_Object* obj)
{
    kz_log(&g_ws_log,"[kz_ws_ack_invalid] Invalid msg_id: %d", json_object_get_number(obj, "msg_id"));
    return nullptr;
}
std::function<void()> kz_ws_ack_hello(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.heartbeat_interval);

    int heartbeat_interval = json_object_dotget_number(obj, "data.heartbeat_interval");
    g_websocket.setPingInterval(heartbeat_interval);
    g_websocket.setMinWaitBetweenReconnectionRetries(5000);
    g_websocket.setMaxWaitBetweenReconnectionRetries(15000);

    kz_log(&g_ws_log,"[kz_ws_ack_hello] Heartbeat interval: %d", heartbeat_interval);
    return nullptr;
}
std::function<void()> kz_ws_ack_map_info(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.mapname);
    ACK_CHECK_MISSING(data.wr_txt);
    ACK_CHECK_MISSING(data.swr_txt);
    ACK_CHECK_MISSING(data.type);
    ACK_CHECK_MISSING(data.length);
    ACK_CHECK_MISSING(data.difficulty);


    char szWR[128];  snprintf(szWR, sizeof(szWR), "%s", json_object_dotget_string(obj, "data.wr_txt"));
    char szSWR[128]; snprintf(szWR, sizeof(szSWR), "%s", json_object_dotget_string(obj, "data.swr_txt"));
    char szMap[64];  snprintf(szMap, sizeof(szMap), "%s", json_object_dotget_string(obj, "data.mapname"));

    int map_props[3];
    map_props[0]    = json_object_dotget_number(obj, "data.type");
    map_props[1]    = json_object_dotget_number(obj, "data.length");
    map_props[2]    = json_object_dotget_number(obj, "data.difficulty");

    int64_t msg_id = json_object_get_number(obj, "msg_id");
    return [szWR, szSWR, szMap, map_props, msg_id]() mutable {
        auto it = g_plugin_callbacks.find(msg_id);

        if (it != g_plugin_callbacks.end())
        {
            auto props = MF_PrepareCellArray(map_props, ARRAYSIZE(map_props));
            MF_ExecuteForward(it->second.fwd, szMap, szWR, szSWR, props);
            MF_UnregisterSPForward(it->second.fwd);
        }
        else
        {
            kz_log(nullptr,"[kz_ws_ack_map_info] Failed to find %lld in g_plugin_callbacks", msg_id);
        }
    };
}
std::function<void()> kz_ws_ack_client_info(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.banned);
    ACK_CHECK_MISSING(data.steamid);

    bool is_banned = json_object_dotget_boolean(obj, "data.banned");
    if (is_banned)
    {
        ACK_CHECK_MISSING(data.banned_by);
    }

    char szAuth[35] = {0};
    char szBy[32] = {0};
    snprintf(szAuth, sizeof(szAuth), "%s", json_object_dotget_string(obj, "data.steamid"));
    snprintf(szBy, sizeof(szBy), "%s", json_object_dotget_string(obj, "data.banned_by"));

    if (!szAuth[0] || (is_banned && !szBy[0]))
    {
        return nullptr;
    }
    return [szAuth, szBy, is_banned]() {
        edict_t* pEntity = find_player_by_authid(szAuth);
        if (!FNullEnt(pEntity))
        {
            if (is_banned)
            {
                char buff[192];

                snprintf(buff, sizeof(buff), "kick #%d \"You've been cross-community banned by %s\"\n", GETPLAYERUSERID(pEntity), szBy);
                SERVER_COMMAND(buff);

                snprintf(buff, sizeof(buff), "banid 5 %s\n", szAuth);
                SERVER_COMMAND(buff);
            }
        }
    };
}
std::function<void()> kz_ws_ack_add_record(JSON_Object* obj)
{
     ACK_CHECK_MISSING(data.id);
     ACK_CHECK_MISSING(data.local_uid);

     ws_upload metadata = {};
     metadata.id = json_object_dotget_number(obj, "data.id");

     const char* local_uid = json_object_dotget_string(obj, "data.local_uid");
     std::filesystem::path replay = g_data_dir / "replays" / STRING(gpGlobals->mapname) / local_uid;
     replay.replace_extension(".krpz");

     snprintf(metadata.local_uid, sizeof(metadata.local_uid), "%s", local_uid);

     if (!std::filesystem::exists(replay) || !std::filesystem::is_regular_file(replay))
     {
         replay.replace_extension(".krpr");
     }

     snprintf(metadata.filepath, sizeof(metadata.filepath), "%s", replay.c_str());

     if (std::filesystem::exists(replay) && std::filesystem::is_regular_file(replay))
     {
         {
              std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
              g_active_uploads.insert(local_uid);
         }
         kz_log(&g_ws_log, "[WS] Starting upload of replay: %s", replay.c_str());
         kz_storage_save(metadata.local_uid, metadata.id, kz_storage_get_next_id(StorageTable::upload_queue), StorageTable::upload_queue);
         kz_rp_compress_and_upload_async(metadata);
     }
     else
     {
         kz_log(&g_ws_log, "[WS] Replay file does not exist: %s", replay.c_str());
     }
     return nullptr;
}
std::function<void()> kz_ws_ack_del_record(JSON_Object* obj)
{
    kz_log(&g_ws_log, "[kz_ws_ack_del_record]");
    return nullptr;
}
std::function<void()> kz_ws_ack_add_replay(JSON_Object* obj)
{
    kz_log(&g_ws_log, "[kz_ws_ack_add_replay]");
    return nullptr;
}
std::function<void()> kz_ws_ack_get_replay(JSON_Object* obj)
{
    kz_log(&g_ws_log, "[kz_ws_ack_get_replay]");
    return nullptr;
}
std::function<void()> kz_ws_ack_file(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.id);
    ACK_CHECK_MISSING(data.local_uid);

    char local_uid[32] = {0};
    uint64 id = json_object_dotget_number(obj, "data.id");
    bool status = json_object_dotget_boolean(obj, "data.status");

    snprintf(local_uid, sizeof(local_uid), "%s", json_object_dotget_string(obj, "data.local_uid"));
    if (status)
    {
        std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
        g_active_uploads.erase(local_uid);

        kz_storage_delete(id, StorageTable::upload_queue);
    }
    return nullptr;
}
