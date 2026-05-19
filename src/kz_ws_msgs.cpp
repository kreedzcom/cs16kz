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

static void kz_ws_format_time(char* buf, size_t sz, int64_t time_ms)
{
    if (time_ms < 0) time_ms = 0;
    int64_t minutes  = time_ms / 60000;
    int64_t seconds  = (time_ms % 60000) / 1000;
    int64_t millis   = time_ms % 1000;
    snprintf(buf, sz, "%lld:%02lld.%03lld", (long long)minutes, (long long)seconds, (long long)millis);
}

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
        std::shared_ptr<std::string> message = *g_outgoing_queue.front();
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
            if ((ts - it->timestamp) > (int)kz_api_retries_delay->value)
            {
                if (it->message->length() == 0 || it->retry_count > (int)kz_api_retries_max->value)
                {
                    it = g_retry_queue.erase(it);
                    continue;
                }

                // The following messages doesn't require ACK (data send back to us)
                if (it->msg_type == WSMsgOut::PLAYER_LEAVE || it->msg_type == WSMsgOut::MAP_CHANGE)
                {
                    it = g_retry_queue.erase(it);
                    continue;
                }
                if (it->table == StorageTable::outgoing_queue)
                {
                    kz_ws_send_msg(*(it->message), it->msg_id);
                }
                else if (it->table == StorageTable::upload_queue)
                {
                    std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
                    if (g_active_uploads.find(*(it->message)) == g_active_uploads.end())
                    {
                        ws_upload metadata = {};
                        metadata.id = 0;

                        std::filesystem::path replay = g_data_dir / "kz_global" / "replays" / *(it->message);
                        replay.replace_extension(".krpz");

                        snprintf(metadata.local_uid, sizeof(metadata.local_uid), "%s", it->message->c_str());
                        if (!std::filesystem::exists(replay))
                        {
                            replay.replace_extension(".krpr");
                        }
                        snprintf(metadata.filepath, sizeof(metadata.filepath), "%s", replay.string().c_str());

                        if (std::filesystem::exists(replay))
                        {
                            g_active_uploads.insert(*(it->message));
                            if (kz_api_log_upload->value > 0.0f)
                            {
                                kz_log(nullptr, "[UPLOAD] Retry (%d): %s", it->retry_count + 1, std::filesystem::relative(replay, g_data_dir).c_str());
                                kz_rp_compress_and_upload_async(metadata);
                            }
                        }
                        else
                        {
                            if (kz_api_log_upload->value > 0.0f)
                            {
                                kz_log(nullptr, "[UPLOAD] File does not exist: %s", std::filesystem::relative(replay, g_data_dir).c_str());
                            }
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
void kz_ws_register(int type, WSMessageFunc pfn)
{
    g_callback_map[type] = pfn;
}
void kz_ws_event_client_connect(edict_t* pEntity)
{
    int id = indexOfEdict(pEntity);

    JSON_Value* data_val = json_value_init_object();
    JSON_Object* data_obj = json_value_get_object(data_val);

    std::string message;
    int64_t msg_id = kz_storage_get_next_id(StorageTable::outgoing_queue);

    json_object_set_string(data_obj, "nickname",   g_players[id].nickname);
    json_object_set_string(data_obj, "ip_address", g_players[id].ipaddr);
    json_object_set_string(data_obj, "steamid",    g_players[id].steamid);

    kz_ws_build_msg(WSMsgOut::PLAYER_JOIN, data_val, message, msg_id);

    auto shared_msg = std::make_shared<std::string>(std::move(message));

    kz_storage_save(shared_msg, WSMsgOut::PLAYER_JOIN, msg_id, StorageTable::outgoing_queue);
    kz_ws_queue_msg(shared_msg, msg_id);
}
void kz_ws_event_client_disconnect(edict_t* pEntity)
{
    int id = indexOfEdict(pEntity);
    if (!g_players[id].steamid[0])
    {
        return;
    }

    JSON_Value* data_val = json_value_init_object();
    JSON_Object* data_obj = json_value_get_object(data_val);

    std::string message;
    int64_t msg_id = kz_storage_get_next_id(StorageTable::outgoing_queue);

    json_object_set_string(data_obj, "steamid", g_players[id].steamid);

    kz_ws_build_msg(WSMsgOut::PLAYER_LEAVE, data_val, message, msg_id);

    auto shared_msg = std::make_shared<std::string>(std::move(message));

    kz_storage_save(shared_msg, WSMsgOut::PLAYER_LEAVE, msg_id, StorageTable::outgoing_queue);
    kz_ws_queue_msg(shared_msg, msg_id);
}
void kz_ws_event_map_change(void)
{
    JSON_Value* data_val = json_value_init_object();
    JSON_Object* data_obj = json_value_get_object(data_val);

    std::string message;
    int64_t msg_id = kz_storage_get_next_id(StorageTable::outgoing_queue);

    json_object_set_string(data_obj, "map_name", g_header.map.name);

    kz_ws_build_msg(WSMsgOut::MAP_CHANGE, data_val, message, msg_id);

    auto shared_msg = std::make_shared<std::string>(std::move(message));

    kz_storage_save(shared_msg, WSMsgOut::MAP_CHANGE, msg_id, StorageTable::outgoing_queue);
    kz_ws_queue_msg(shared_msg, msg_id);
}
std::function<void()> kz_ws_ack_invalid(JSON_Object* obj)
{
    kz_log(&g_ws_log,"[kz_ws_ack_invalid] Unhandled msg_type: %d", (int)json_object_get_number(obj, "msg_type"));
    return nullptr;
}
std::function<void()> kz_ws_ack_error(JSON_Object* obj)
{
    kz_log(&g_ws_log, "[kz_ws_ack_error] INFO: %s", json_object_dotget_string(obj, "data.message"));
    return nullptr;
}
std::function<void()> kz_ws_ack_hello(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.heartbeat_interval);

    int heartbeat_interval = (int)json_object_dotget_number(obj, "data.heartbeat_interval");
    g_websocket.setPingInterval(heartbeat_interval);
    g_websocket.setMinWaitBetweenReconnectionRetries(5000);
    g_websocket.setMaxWaitBetweenReconnectionRetries(15000);

    JSON_Value* map_info_val = json_object_dotget_value(obj, "data.map_info");

    if (map_info_val != nullptr && json_value_get_type(map_info_val) == JSONObject)
    {
        JSON_Value* temp_root = json_value_init_object();
        json_object_set_value(json_value_get_object(temp_root), "data", json_value_deep_copy(map_info_val));

        // bit of a hack because im too lazy to do proper parsing
        auto ret = kz_ws_ack_map_info(json_value_get_object(temp_root));

        json_value_free(temp_root);
        return ret;
    }
    kz_log(&g_ws_log,"[kz_ws_ack_hello] Heartbeat interval: %d", heartbeat_interval);
    return nullptr;
}
std::function<void()> kz_ws_ack_map_info(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.map_name);

    char szMap[64];
    snprintf(szMap, sizeof(szMap), "%s", json_object_dotget_string(obj, "data.map_name"));

    char szWR_Pro[128]  = {0};
    char szWR_Noob[128] = {0};

    const char* wr_pro_steamid = json_object_dotget_string(obj, "data.wr_pro_steamid");
    double wr_pro_time_ms      = json_object_dotget_number(obj, "data.wr_pro_time_ms");
    if (wr_pro_steamid && wr_pro_steamid[0])
    {
        char time_str[32];
        kz_ws_format_time(time_str, sizeof(time_str), (int64_t)wr_pro_time_ms);
        snprintf(szWR_Pro, sizeof(szWR_Pro), "%s (%s)", wr_pro_steamid, time_str);
    }

    const char* wr_nub_steamid = json_object_dotget_string(obj, "data.wr_nub_steamid");
    double wr_nub_time_ms      = json_object_dotget_number(obj, "data.wr_nub_time_ms");
    if (wr_nub_steamid && wr_nub_steamid[0])
    {
        char time_str[32];
        kz_ws_format_time(time_str, sizeof(time_str), (int64_t)wr_nub_time_ms);
        snprintf(szWR_Noob, sizeof(szWR_Noob), "%s (%s)", wr_nub_steamid, time_str);
    }

    int map_props[3] = {-1, -1, -1};
    if (json_object_dothas_value_of_type(obj, "data.type", JSONNumber))
    {
        map_props[0] = json_object_dotget_number(obj, "data.type");
    }
    if (json_object_dothas_value_of_type(obj, "data.length", JSONNumber))
    {
        map_props[1] = json_object_dotget_number(obj, "data.length");
    }
    if (json_object_dothas_value_of_type(obj, "data.difficulty", JSONNumber))
    {
        map_props[2] = json_object_dotget_number(obj, "data.difficulty");
    }

    int64_t msg_id = (int64_t)json_object_get_number(obj, "msg_id");
    return [szWR_Pro, szWR_Noob, szMap, map_props, msg_id]() mutable {
        auto it = g_plugin_callbacks.find(msg_id);

        if (it != g_plugin_callbacks.end())
        {
            kz_call_map_info_forward(it->second.fwd, szMap, szWR_Pro, szWR_Noob, map_props, ARRAYSIZE(map_props));
        }
        else if (FStrEq(szMap, STRING(gpGlobals->mapname)))
        {
            g_current_map_info.map_props[0] = map_props[0];
            g_current_map_info.map_props[1] = map_props[1];
            g_current_map_info.map_props[2] = map_props[2];

            snprintf(g_current_map_info.szWR_Pro, sizeof(szWR_Pro), "%s", szWR_Pro);
            snprintf(g_current_map_info.szWR_Noob, sizeof(szWR_Noob), "%s", szWR_Noob);

            g_current_map_info.updated = true;
        }
        else
        {
            kz_log(nullptr,"[kz_ws_ack_map_info] Failed to find %lld in g_plugin_callbacks", msg_id);
        }
    };
}
std::function<void()> kz_ws_ack_player_join(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.is_banned);
    ACK_CHECK_MISSING(data.steamid);

    bool is_banned = json_object_dotget_boolean(obj, "data.is_banned") != 0;

    char szAuth[35] = {0};
    snprintf(szAuth, sizeof(szAuth), "%s", json_object_dotget_string(obj, "data.steamid"));

    if (!szAuth[0])
    {
        return nullptr;
    }
    return [szAuth, is_banned]() {
        edict_t* pEntity = find_player_by_authid(szAuth);
        if (!FNullEnt(pEntity) && is_banned)
        {
            char buff[192];
            snprintf(buff, sizeof(buff), "kick #%d \"You've been cross-community banned\"\n", GETPLAYERUSERID(pEntity));
            SERVER_COMMAND(buff);

            snprintf(buff, sizeof(buff), "banid 5 %s\n", szAuth);
            SERVER_COMMAND(buff);
        }
    };
}
std::function<void()> kz_ws_ack_record_ack(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.local_uid);

    const char* local_uid = json_object_dotget_string(obj, "data.local_uid");
    if (!local_uid || !local_uid[0])
    {
        kz_log(&g_ws_log, "[kz_ws_ack_record_ack] Empty local_uid.");
        return nullptr;
    }

    ws_upload metadata = {};
    metadata.id = 0;

    std::filesystem::path replay = g_data_dir / "kz_global" / "replays" / local_uid;
    replay.replace_extension(".krpr");

    snprintf(metadata.local_uid, sizeof(metadata.local_uid), "%s", local_uid);
    snprintf(metadata.filepath, sizeof(metadata.filepath), "%s", replay.string().c_str());

    if (std::filesystem::exists(replay))
    {
        {
            std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
            g_active_uploads.insert(local_uid);
        }

        auto shared_msg = std::make_shared<std::string>(metadata.local_uid);

        if (kz_api_log_upload->value > 0.0f)
        {
            kz_log(&g_ws_log, "[UPLOAD] File: %s", std::filesystem::relative(replay, g_data_dir).c_str());
        }

        kz_storage_save(shared_msg, 0, kz_storage_get_next_id(StorageTable::upload_queue), StorageTable::upload_queue);
        kz_rp_compress_and_upload_async(metadata);
    }
    else
    {
        if (kz_api_log_upload->value > 0.0f)
        {
            kz_log(&g_ws_log, "[UPLOAD] File does not exist: %s", std::filesystem::relative(replay, g_data_dir).c_str());
        }
    }
    return nullptr;
}
std::function<void()> kz_ws_ack_file_ack(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.local_uid);

    char local_uid[64] = {0};
    bool status = json_object_dotget_boolean(obj, "data.status") != 0;

    snprintf(local_uid, sizeof(local_uid), "%s", json_object_dotget_string(obj, "data.local_uid"));
    if (status)
    {
        // Always clean up the upload queue and active-uploads set — the server
        // accepted the upload regardless of whether local file operations succeed.
        auto cleanup = [&]() {
            {
                std::lock_guard<std::mutex> lock(g_retry_mtx);
                auto it = g_retry_queue.begin();
                while (it != g_retry_queue.end())
                {
                    if (it->table == StorageTable::upload_queue && strcmp(local_uid, it->message->c_str()) == 0)
                    {
                        g_retry_queue.erase(it);
                        break;
                    }
                    ++it;
                }
            }
            {
                std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
                g_active_uploads.erase(local_uid);
            }
            kz_storage_delete_by_value(local_uid, StorageTable::upload_queue);
        };

        std::string mapname;
        bool file_moved = false;
        {
            std::filesystem::path filepath = g_data_dir / "kz_global" / "replays" / local_uid;
            filepath.replace_extension(".krpr");

            FILE* fp = fopen(filepath.string().c_str(), "rb");
            if (!fp)
            {
                kz_log(&g_ws_log, "[ACK] Upload accepted but .krpr missing for: %s", local_uid);
                cleanup();
                return nullptr;
            }

            mapname = kz_rp_mapname_from_header(fp);
            fclose(fp);
            fp = nullptr;

            filepath.replace_extension(".krpz");
            if (!std::filesystem::exists(filepath))
            {
                kz_log(&g_ws_log, "[ACK] Compressed replay not found: %s", std::filesystem::relative(filepath, g_data_dir).c_str());
                cleanup();
                return nullptr;
            }

            std::filesystem::path n_filepath = g_data_dir / "kz_global" / "replays" / mapname / local_uid;
            n_filepath.replace_extension(".krpz");

            std::error_code ec;
            std::filesystem::create_directories(n_filepath.parent_path(), ec);
            if (ec)
            {
                kz_log(&g_ws_log, "[ACK] Failed to create replay directory: %s", ec.message().c_str());
                cleanup();
                return nullptr;
            }

            std::filesystem::rename(filepath, n_filepath, ec);
            if (ec)
            {
                kz_log(&g_ws_log, "[ACK] Failed to move replay %s -> %s: %s",
                    std::filesystem::relative(filepath, g_data_dir).c_str(),
                    std::filesystem::relative(n_filepath, g_data_dir).c_str(),
                    ec.message().c_str());
                cleanup();
                return nullptr;
            }

            std::filesystem::path raw_path = g_data_dir / "kz_global" / "replays" / local_uid;
            raw_path.replace_extension(".krpr");
            std::filesystem::remove(raw_path, ec);
            file_moved = true;
        }

        cleanup();

        if (file_moved && !mapname.empty())
        {
            return [mapname]() {
                if (FStrEq(mapname.c_str(), STRING(gpGlobals->mapname)))
                {
                    std::filesystem::path file = kz_pb_find_fastest(mapname.c_str());
                    if (!file.empty())
                    {
                        if (!g_pb_bot_data || !FStrEq(file.filename().string().c_str(), g_pb_bot_data->filepath.filename().string().c_str()))
                        {
                            kz_pb_parse_file_async(file);
                        }
                    }
                }
            };
        }
    }
    return nullptr;
}
