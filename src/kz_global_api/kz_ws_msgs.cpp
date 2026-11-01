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
#include "kz_path_validate.h"
#include "kz_replay_uid.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ixwebsocket/IXHttpClient.h>
#include <set>
#include <thread>
#include <vector>

extern bool g_initialized;
extern bool g_early_mapchange;
extern float g_wait_after_load;

#define ACK_CHECK_MISSING(X) \
    do { \
        if (!json_object_dotget_value(obj, #X)) { \
            kz_log(&g_ws_log, "[%s] Error: missing %s.", __FUNCTION__, #X); \
            return nullptr; \
        } \
    } while(0)

static const char* kz_json_dot_string(JSON_Object* obj, const char* key)
{
    const char* value = json_object_dotget_string(obj, key);
    return value ? value : "";
}

std::mutex g_active_uploads_mtx;
std::set<std::string> g_active_uploads;

void kz_ws_release_active_upload(const char* local_uid)
{
    if (!local_uid || !local_uid[0])
    {
        return;
    }
    std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
    g_active_uploads.erase(local_uid);
}

static std::mutex g_replay_fetch_mtx;
static std::set<std::string> g_replay_fetch_pending;
static std::set<std::string> g_replay_pro_upgrade_failed;

static bool kz_ws_map_has_pro_wr(void)
{
    return g_current_map_info.updated && g_current_map_info.szWR_Pro[0] != '\0';
}

static bool kz_ws_local_has_pro_replay(const char* mapname)
{
    const std::filesystem::path file = kz_pb_find_fastest(mapname);
    if (file.empty())
    {
        return false;
    }

    uint8_t run_class = 0;
    uint32_t time_ms = 0;
    if (!kz_replay_parse_uid(file.filename().string(), run_class, time_ms))
    {
        return false;
    }
    return run_class == 0;
}

static bool kz_ws_local_replay_is_sufficient(const char* mapname)
{
    const std::filesystem::path file = kz_pb_find_fastest(mapname);
    if (file.empty())
    {
        return false;
    }
    if (kz_ws_map_has_pro_wr())
    {
        return kz_ws_local_has_pro_replay(mapname);
    }
    return true;
}

static void kz_ws_clear_replay_pro_upgrade_failed(const char* mapname)
{
    if (!mapname || !mapname[0])
    {
        g_replay_pro_upgrade_failed.clear();
        return;
    }
    g_replay_pro_upgrade_failed.erase(mapname);
}

struct replay_download_retry
{
    std::string mapname;
    int64_t     timestamp;
    int32_t     retry_count;
};

static std::mutex g_replay_download_retry_mtx;
static std::vector<replay_download_retry> g_replay_download_retries;

static void kz_ws_clear_replay_fetch_pending(const char* mapname)
{
    if (!mapname || !mapname[0])
    {
        return;
    }
    std::lock_guard<std::mutex> lock(g_replay_fetch_mtx);
    g_replay_fetch_pending.erase(mapname);
}

static void kz_ws_clear_replay_fetch_pending_except(const char* keep_mapname)
{
    std::lock_guard<std::mutex> lock(g_replay_fetch_mtx);
    if (!keep_mapname || !keep_mapname[0])
    {
        g_replay_fetch_pending.clear();
        return;
    }
    for (auto it = g_replay_fetch_pending.begin(); it != g_replay_fetch_pending.end();)
    {
        if (*it != keep_mapname)
        {
            it = g_replay_fetch_pending.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

static ix::SocketTLSOptions kz_ws_replay_download_tls_options(void)
{
    ix::SocketTLSOptions tls_options;
    tls_options.caFile = std::filesystem::path("cstrike/addons/amxmodx/data/kz_global/cacert.pem").string();
    return tls_options;
}

static void kz_ws_schedule_replay_download_retry(const std::string& mapname)
{
    const int max_retries = kz_api_retries_max ? static_cast<int>(kz_api_retries_max->value) : 4;
    const int delay_sec   = kz_api_retries_delay ? static_cast<int>(kz_api_retries_delay->value) : 5;

    std::lock_guard<std::mutex> lock(g_replay_download_retry_mtx);

    for (auto& entry : g_replay_download_retries)
    {
        if (entry.mapname == mapname)
        {
            entry.retry_count++;
            if (entry.retry_count > max_retries)
            {
                kz_log(&g_ws_log, "[GET_REPLAY] Download retries exhausted for map=%s", mapname.c_str());
                kz_ws_clear_replay_fetch_pending(mapname.c_str());
                g_replay_download_retries.erase(
                    std::remove_if(g_replay_download_retries.begin(), g_replay_download_retries.end(),
                        [&mapname](const replay_download_retry& r) { return r.mapname == mapname; }),
                    g_replay_download_retries.end());
                return;
            }
            auto now = std::chrono::system_clock::now();
            entry.timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() + delay_sec;
            kz_ws_clear_replay_fetch_pending(mapname.c_str());
            return;
        }
    }

    replay_download_retry entry = {};
    entry.mapname     = mapname;
    entry.retry_count = 1;
    auto now = std::chrono::system_clock::now();
    entry.timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() + delay_sec;
    g_replay_download_retries.push_back(std::move(entry));
    kz_ws_clear_replay_fetch_pending(mapname.c_str());
}

static void kz_ws_process_replay_download_retries(void)
{
    if (g_websocket_state.load() != WSState::Connected)
    {
        return;
    }

    const int delay_sec = kz_api_retries_delay ? static_cast<int>(kz_api_retries_delay->value) : 5;
    auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::vector<std::string> due_maps;
    {
        std::lock_guard<std::mutex> lock(g_replay_download_retry_mtx);
        for (const auto& entry : g_replay_download_retries)
        {
            if (entry.timestamp <= now_ts)
            {
                due_maps.push_back(entry.mapname);
            }
        }
    }

    for (const auto& mapname : due_maps)
    {
        if (!FStrEq(mapname.c_str(), STRING(gpGlobals->mapname)))
        {
            std::lock_guard<std::mutex> lock(g_replay_download_retry_mtx);
            g_replay_download_retries.erase(
                std::remove_if(g_replay_download_retries.begin(), g_replay_download_retries.end(),
                    [&mapname](const replay_download_retry& r) { return r.mapname == mapname; }),
                g_replay_download_retries.end());
            continue;
        }

        if (kz_ws_local_replay_is_sufficient(mapname.c_str()))
        {
            std::lock_guard<std::mutex> lock(g_replay_download_retry_mtx);
            g_replay_download_retries.erase(
                std::remove_if(g_replay_download_retries.begin(), g_replay_download_retries.end(),
                    [&mapname](const replay_download_retry& r) { return r.mapname == mapname; }),
                g_replay_download_retries.end());
            continue;
        }

        kz_ws_try_fetch_replay(mapname.c_str());

        std::lock_guard<std::mutex> lock(g_replay_download_retry_mtx);
        for (auto& entry : g_replay_download_retries)
        {
            if (entry.mapname == mapname)
            {
                entry.timestamp = now_ts + delay_sec;
                break;
            }
        }
    }
}

void kz_ws_on_map_loaded(bool force)
{
    if (!g_initialized)
    {
        return;
    }

    const char* mapname = STRING(gpGlobals->mapname);
    if (!mapname || !mapname[0] || !kz_ws_valid_replay_segment(mapname))
    {
        return;
    }

    static char s_last_map_loaded[64] = {0};
    if (!force && FStrEq(mapname, s_last_map_loaded))
    {
        return;
    }

    snprintf(s_last_map_loaded, sizeof(s_last_map_loaded), "%s", mapname);

    g_early_mapchange = false;
    g_wait_after_load = 1.0f;

    kz_ws_clear_replay_fetch_pending_except(mapname);
    kz_ws_clear_replay_pro_upgrade_failed(mapname);

    kz_rp_update_header();
    g_current_map_info.updated = false;

    if (g_websocket_state.load() == WSState::Connected)
    {
        kz_ws_event_map_change();
    }
    else
    {
        std::filesystem::path file = kz_pb_find_sr_replay(mapname);
        if (!file.empty())
        {
            kz_pb_parse_file_async(file);
        }
    }
}

static constexpr size_t KZ_MAX_REPLAY_DOWNLOAD_BYTES = 104857600ULL;

static bool kz_ws_has_zstd_magic(const uint8_t* data, size_t len)
{
    return len >= 4 && data[0] == 0x28 && data[1] == 0xB5 && data[2] == 0x2F && data[3] == 0xFD;
}

static void kz_ws_delete_replay_files(const char* local_uid, const char* mapname);

void kz_ws_delete_record_replay(const char* mapname, const char* local_uid)
{
    if (!mapname || !local_uid || !mapname[0] || !local_uid[0]
        || !kz_ws_valid_replay_segment(mapname) || !kz_ws_valid_replay_segment(local_uid))
    {
        return;
    }

    kz_ws_delete_replay_files(local_uid, mapname);

    std::filesystem::path path = g_data_dir / "kz_global" / "replays" / mapname / local_uid;
    path.replace_extension(".krpz");
    kz_pb_drop_parsed_replay(path);
    kz_pb_stop_if_playing(path);

    kz_pb_reload_sr_bot(mapname);
}

bool kz_ws_try_fetch_replay(const char* mapname)
{
    if (kz_api_replays_download->value <= 0.0f)
    {
        return false;
    }
    if (!mapname || !mapname[0] || !kz_ws_valid_replay_segment(mapname))
    {
        return false;
    }
    if (g_websocket_state.load() != WSState::Connected)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_replay_fetch_mtx);
        if (g_replay_fetch_pending.find(mapname) != g_replay_fetch_pending.end())
        {
            return false;
        }
        if (g_replay_pro_upgrade_failed.find(mapname) != g_replay_pro_upgrade_failed.end())
        {
            return false;
        }
        if (kz_ws_local_replay_is_sufficient(mapname))
        {
            return false;
        }
        g_replay_fetch_pending.insert(mapname);
    }

    JSON_Value* data_val = json_value_init_object();
    JSON_Object* data_obj = json_value_get_object(data_val);
    json_object_set_string(data_obj, "map_name", mapname);

    std::string message;
    int64_t msg_id = kz_storage_get_next_id(StorageTable::outgoing_queue);
    kz_ws_build_msg(WSMsgOut::GET_REPLAY, data_val, message, msg_id);

    auto shared_msg = std::make_shared<std::string>(std::move(message));
    kz_storage_save(shared_msg, WSMsgOut::GET_REPLAY, msg_id, StorageTable::outgoing_queue);
    kz_ws_queue_msg(shared_msg, msg_id);
    return true;
}

static void kz_ws_download_replay_async(std::string url, std::string mapname, std::string local_uid)
{
    std::thread([url = std::move(url), mapname = std::move(mapname), local_uid = std::move(local_uid)]() {
        std::vector<uint8_t> body;
        bool ok = false;

        if (url.rfind("https://", 0) == 0)
        {
            ix::HttpClient client;
            client.setTLSOptions(kz_ws_replay_download_tls_options());
            auto args = std::make_shared<ix::HttpRequestArgs>();
            args->followRedirects = false;
            auto response = client.get(url, args);
            if (response && response->errorCode == ix::HttpErrorCode::Ok && response->statusCode == 200)
            {
                const auto& payload = response->body;
                if (payload.size() <= KZ_MAX_REPLAY_DOWNLOAD_BYTES && kz_ws_has_zstd_magic(
                        reinterpret_cast<const uint8_t*>(payload.data()), payload.size()))
                {
                    body.assign(payload.begin(), payload.end());
                    ok = true;
                }
            }
        }

        if (!g_incoming_queue.try_push([ok, body = std::move(body), mapname, local_uid]() mutable {
            if (!ok)
            {
                kz_log(&g_ws_log, "[GET_REPLAY] Download failed for map=%s uid=%s", mapname.c_str(), local_uid.c_str());
                kz_ws_schedule_replay_download_retry(mapname);
                return;
            }

            if (!FStrEq(mapname.c_str(), STRING(gpGlobals->mapname)))
            {
                kz_ws_clear_replay_fetch_pending(mapname.c_str());
                return;
            }

            std::filesystem::path out_path = g_data_dir / "kz_global" / "replays" / mapname / local_uid;
            out_path.replace_extension(".krpz");

            std::error_code ec;
            std::filesystem::create_directories(out_path.parent_path(), ec);
            if (ec)
            {
                kz_log(&g_ws_log, "[GET_REPLAY] Failed to create directory: %s", ec.message().c_str());
                kz_ws_schedule_replay_download_retry(mapname);
                return;
            }

            FILE* fp = fopen(out_path.string().c_str(), "wb");
            if (!fp)
            {
                kz_log(&g_ws_log, "[GET_REPLAY] Failed to write replay: %s", out_path.string().c_str());
                kz_ws_schedule_replay_download_retry(mapname);
                return;
            }
            const size_t written = fwrite(body.data(), 1, body.size(), fp);
            fclose(fp);

            if (written != body.size())
            {
                std::filesystem::remove(out_path, ec);
                kz_log(&g_ws_log, "[GET_REPLAY] Incomplete write for %s", local_uid.c_str());
                kz_ws_schedule_replay_download_retry(mapname);
                return;
            }

            kz_log(&g_ws_log, "[GET_REPLAY] Saved replay: %s", std::filesystem::relative(out_path, g_data_dir).string().c_str());
            kz_ws_clear_replay_fetch_pending(mapname.c_str());
            kz_ws_clear_replay_pro_upgrade_failed(mapname.c_str());
            {
                std::lock_guard<std::mutex> lock(g_replay_download_retry_mtx);
                g_replay_download_retries.erase(
                    std::remove_if(g_replay_download_retries.begin(), g_replay_download_retries.end(),
                        [&mapname](const replay_download_retry& r) { return r.mapname == mapname; }),
                    g_replay_download_retries.end());
            }
            kz_pb_parse_file_async(out_path);
        }))
        {
            kz_ws_schedule_replay_download_retry(mapname);
        }
    }).detach();
}

static void kz_ws_delete_replay_files(const char* local_uid, const char* mapname)
{
    if (!local_uid || !local_uid[0])
    {
        return;
    }

    std::error_code ec;
    for (const char* ext : {".krpr", ".krpz"})
    {
        std::filesystem::path path;
        if (mapname && mapname[0])
        {
            path = g_data_dir / "kz_global" / "replays" / mapname / local_uid;
        }
        else
        {
            path = g_data_dir / "kz_global" / "replays" / local_uid;
        }
        path.replace_extension(ext);
        std::filesystem::remove(path, ec);
    }
}

static bool kz_ws_requeue_replay_upload(const char* local_uid)
{
    if (!local_uid || !local_uid[0])
    {
        return false;
    }

    std::filesystem::path replay = g_data_dir / "kz_global" / "replays" / local_uid;
    replay.replace_extension(".krpz");

    if (!std::filesystem::exists(replay))
    {
        replay.replace_extension(".krpr");
    }
    if (!std::filesystem::exists(replay))
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
        if (g_active_uploads.find(local_uid) != g_active_uploads.end())
        {
            return false;
        }
        g_active_uploads.insert(local_uid);
    }

    ws_upload metadata = {};
    metadata.id = 0;
    snprintf(metadata.local_uid, sizeof(metadata.local_uid), "%s", local_uid);
    snprintf(metadata.filepath, sizeof(metadata.filepath), "%s", replay.string().c_str());

    auto shared_uid = std::make_shared<std::string>(local_uid);
    kz_storage_save(shared_uid, 0, kz_storage_get_next_id(StorageTable::upload_queue), StorageTable::upload_queue);
    if (!kz_rp_compress_and_upload_async(metadata))
    {
        kz_ws_release_active_upload(local_uid);
        return false;
    }
    return true;
}

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
    kz_ws_process_replay_download_retries();
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
                                kz_log(nullptr, "[UPLOAD] Retry (%d): %s", it->retry_count + 1, std::filesystem::relative(replay, g_data_dir).string().c_str());
                            }
                            if (!kz_rp_compress_and_upload_async(metadata))
                            {
                                g_active_uploads.erase(*(it->message));
                            }
                        }
                        else
                        {
                            if (kz_api_log_upload->value > 0.0f)
                            {
                                kz_log(nullptr, "[UPLOAD] File does not exist: %s", std::filesystem::relative(replay, g_data_dir).string().c_str());
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

    krp_header header = kz_rp_get_header();
    json_object_set_string(data_obj, "map_name", header.map.name);

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
    const char* message = json_object_dotget_string(obj, "data.message");
    int64_t msg_id = (int64_t)json_object_get_number(obj, "msg_id");

    kz_log(&g_ws_log, "[kz_ws_ack_error] INFO: %s", message ? message : "(no message)");

    if (msg_id <= 0 || !message)
    {
        return nullptr;
    }

    int64_t msg_type = 0;
    std::string stored;

    if (!kz_storage_try_get_outgoing(msg_id, &msg_type, &stored))
    {
        kz_log(&g_ws_log, "[kz_ws_ack_error] Failed to find msg_id (%lld) in storage", static_cast<long long>(msg_id));
        return nullptr;
    }

    JSON_Value* stored_val = json_parse_string(stored.c_str());

    if (!stored_val)
    {
        return nullptr;
    }

    std::function<void()> callback = nullptr;

    if (msg_type == WSMsgOut::ADD_RECORD)
    {
        const char* r_uid = json_object_dotget_string(json_value_get_object(stored_val), "data.local_uid");
        if (r_uid)
        {
            callback = [msg = std::string(message), uid = std::string(r_uid)]() {
                kz_ws_delete_replay_files(uid.c_str(), nullptr);
                kz_storage_delete_by_value(uid, StorageTable::upload_queue);
                {
                    std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
                    g_active_uploads.erase(uid);
                }
                kz_log(nullptr, "[kz_ws_ack_error] Discarded replay for uid=%s (%s)", uid.c_str(), msg.c_str());
            };
        }
    }
    else if (msg_type == WSMsgOut::GET_REPLAY)
    {
        const char* map_name = json_object_dotget_string(json_value_get_object(stored_val), "data.map_name");
        if (map_name && map_name[0] && kz_ws_map_has_pro_wr() && !kz_ws_local_has_pro_replay(map_name))
        {
            g_replay_pro_upgrade_failed.insert(map_name);
        }
        kz_ws_clear_replay_fetch_pending(map_name);
    }
    json_value_free(stored_val);
    return callback;
}
std::function<void()> kz_ws_ack_hello(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.heartbeat_interval);

    int heartbeat_interval = (int)json_object_dotget_number(obj, "data.heartbeat_interval");
    g_websocket.setPingInterval(heartbeat_interval);
    g_websocket.setMinWaitBetweenReconnectionRetries(5000);
    g_websocket.setMaxWaitBetweenReconnectionRetries(15000);
    kz_log(&g_ws_log,"[kz_ws_ack_hello] Heartbeat interval: %d", heartbeat_interval);

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
    return nullptr;
}
std::function<void()> kz_ws_ack_map_info(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.map_name);

    char szMap[64];
    snprintf(szMap, sizeof(szMap), "%s", kz_json_dot_string(obj, "data.map_name"));

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

            kz_ws_try_fetch_replay(szMap);
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

    const char* steamid = json_object_dotget_string(obj, "data.steamid");
    if (!steamid || !steamid[0])
    {
        return nullptr;
    }

    char szAuth[35] = {0};
    snprintf(szAuth, sizeof(szAuth), "%s", steamid);

    return [szAuth, is_banned]() {
        edict_t* pEntity = find_player_by_authid(szAuth);
        if (FNullEnt(pEntity) || !is_banned)
        {
            return;
        }

        const int action = kz_api_ban_action ? static_cast<int>(kz_api_ban_action->value) : 3;

        if (action == 3)
        {
            g_players[indexOfEdict(pEntity)].no_submit.store(true, std::memory_order_relaxed);
            return;
        }

        char buff[192];
        snprintf(buff, sizeof(buff), "kick #%d \"You've been cross-community banned\"\n", GETPLAYERUSERID(pEntity));
        SERVER_COMMAND(buff);

        if (action != 2)
        {
            return;
        }
        if (!kz_ws_valid_steam_authid(szAuth))
        {
            kz_log(&g_ws_log, "[kz_ws_ack_player_join] Skipping banid: invalid steamid.");
            return;
        }

        snprintf(buff, sizeof(buff), "banid 5 %s\n", szAuth);
        SERVER_COMMAND(buff);
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
    if (!kz_ws_valid_replay_segment(local_uid))
    {
        kz_log(&g_ws_log, "[kz_ws_ack_record_ack] Invalid local_uid.");
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
            kz_log(&g_ws_log, "[UPLOAD] File: %s", std::filesystem::relative(replay, g_data_dir).string().c_str());
        }

        kz_storage_save(shared_msg, 0, kz_storage_get_next_id(StorageTable::upload_queue), StorageTable::upload_queue);
        return [metadata]() {
                if (!kz_rp_compress_and_upload_async(metadata))
                {
                    kz_ws_release_active_upload(metadata.local_uid);
                }
            };
    }
    else
    {
        if (kz_api_log_upload->value > 0.0f)
        {
            kz_log(&g_ws_log, "[UPLOAD] File does not exist: %s", std::filesystem::relative(replay, g_data_dir).string().c_str());
        }
    }
    return nullptr;
}
std::function<void()> kz_ws_ack_file_ack(JSON_Object* obj)
{
    ACK_CHECK_MISSING(data.local_uid);

    char local_uid[64] = {0};
    bool status = json_object_dotget_boolean(obj, "data.status") != 0;

    const char* uid_ptr = json_object_dotget_string(obj, "data.local_uid");
    if (!uid_ptr || !uid_ptr[0])
    {
        kz_log(&g_ws_log, "[kz_ws_ack_file_ack] Empty local_uid.");
        return nullptr;
    }
    if (!kz_ws_valid_replay_segment(uid_ptr))
    {
        kz_log(&g_ws_log, "[kz_ws_ack_file_ack] Invalid local_uid.");
        return nullptr;
    }
    snprintf(local_uid, sizeof(local_uid), "%s", uid_ptr);
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

            if (mapname.empty() || !kz_ws_valid_replay_segment(mapname.c_str()))
            {
                kz_log(&g_ws_log, "[ACK] Invalid map name in replay header for: %s", local_uid);
                cleanup();
                return nullptr;
            }

            filepath.replace_extension(".krpz");
            if (!std::filesystem::exists(filepath))
            {
                kz_log(&g_ws_log, "[ACK] Compressed replay not found: %s", std::filesystem::relative(filepath, g_data_dir).string().c_str());
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
                    std::filesystem::relative(filepath, g_data_dir).string().c_str(),
                    std::filesystem::relative(n_filepath, g_data_dir).string().c_str(),
                    ec.message().c_str());
                cleanup();
                return nullptr;
            }

            std::filesystem::path raw_path = g_data_dir / "kz_global" / "replays" / local_uid;
            raw_path.replace_extension(".krpr");
            std::filesystem::remove(raw_path, ec);
            file_moved = true;

            kz_rp_prune_replays(mapname.c_str(), &g_ws_log);
        }

        cleanup();

        if (file_moved && !mapname.empty())
        {
            return [mapname]() {
                if (FStrEq(mapname.c_str(), STRING(gpGlobals->mapname)))
                {
                    std::filesystem::path file = kz_pb_find_sr_replay(mapname.c_str());
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

    kz_log(&g_ws_log, "[ACK] Upload rejected for uid=%s", local_uid);
    return [uid = std::string(local_uid)]() {
        kz_ws_requeue_replay_upload(uid.c_str());
    };
}

std::function<void()> kz_ws_ack_get_replay(JSON_Object* obj)
{
    const char* map_name = json_object_dotget_string(obj, "data.map_name");

    if (!json_object_dotget_value(obj, "data.url"))
    {
        kz_log(&g_ws_log, "[kz_ws_ack_get_replay] Error: missing data.url.");
        kz_ws_clear_replay_fetch_pending(map_name ? map_name : "");
        return nullptr;
    }
    if (!json_object_dotget_value(obj, "data.local_uid"))
    {
        kz_log(&g_ws_log, "[kz_ws_ack_get_replay] Error: missing data.local_uid.");
        kz_ws_clear_replay_fetch_pending(map_name ? map_name : "");
        return nullptr;
    }
    if (!json_object_dotget_value(obj, "data.map_name"))
    {
        kz_log(&g_ws_log, "[kz_ws_ack_get_replay] Error: missing data.map_name.");
        return nullptr;
    }

    const char* url = json_object_dotget_string(obj, "data.url");
    const char* local_uid = json_object_dotget_string(obj, "data.local_uid");
    map_name = json_object_dotget_string(obj, "data.map_name");

    if (!url || !local_uid || !map_name)
    {
        kz_ws_clear_replay_fetch_pending(map_name ? map_name : "");
        return nullptr;
    }
    if (!kz_ws_valid_replay_segment(map_name) || !kz_ws_valid_replay_segment(local_uid))
    {
        kz_log(&g_ws_log, "[GET_REPLAY_ACK] Invalid map_name or local_uid.");
        kz_ws_clear_replay_fetch_pending(map_name);
        return nullptr;
    }
    if (kz_ws_map_has_pro_wr() && local_uid[0] == '1' && local_uid[1] == '_')
    {
        kz_log(&g_ws_log, "[GET_REPLAY_ACK] Ignoring nub replay for map=%s while pro WR exists.", map_name);
        g_replay_pro_upgrade_failed.insert(map_name);
        kz_ws_clear_replay_fetch_pending(map_name);
        return nullptr;
    }

    kz_ws_download_replay_async(url, map_name, local_uid);
    return nullptr;
}

std::function<void()> kz_ws_ack_del_record_notify(JSON_Object* obj)
{
    const char* record_id = json_object_dotget_string(obj, "data.record_id");
    const char* map_name = json_object_dotget_string(obj, "data.map_name");
    const char* local_uid = json_object_dotget_string(obj, "data.local_uid");

    if (!map_name || !local_uid)
    {
        kz_log(&g_ws_log, "[DEL_RECORD_NOTIFY] Missing map_name or local_uid.");
        return nullptr;
    }
    if (!kz_ws_valid_replay_segment(map_name) || !kz_ws_valid_replay_segment(local_uid))
    {
        kz_log(&g_ws_log, "[DEL_RECORD_NOTIFY] Invalid map_name or local_uid.");
        return nullptr;
    }

    return [map = std::string(map_name), uid = std::string(local_uid), rid = std::string(record_id ? record_id : "")]() {
        kz_log(&g_ws_log, "[DEL_RECORD_NOTIFY] record=%s map=%s uid=%s", rid.c_str(), map.c_str(), uid.c_str());
        kz_ws_delete_record_replay(map.c_str(), uid.c_str());
    };
}
