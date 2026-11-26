#ifndef KZ_WS_H
#define KZ_WS_H

#include "kz_path_validate.h"
#include "kz_ws_protocol.h"

#include <parson.h>
#include <rigtorp/SPSCQueue.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <string>
#include <set>
#include <unordered_map>

enum class WSState : int
{
    Uninitialized = 0,
    Initialized,
    Admitting,
    Connected,
    Disconnected,
    DisconnectedButWorthRetrying,
    _MAX,
};

namespace kz {
    using websocket = ix::WebSocket;
    template <typename T> using queue = rigtorp::SPSCQueue<T>;
};

#pragma pack(push, 1)

typedef struct
{
    char        filepath[255];
    char        local_uid[64];
    uint64_t    id;
} ws_upload;

typedef struct
{
    char        local_uid[64];
    uint64_t    id;
    int32_t     chunk_checksum;
    uint64_t    chunk_index;
    uint64_t    chunk_total;
} ws_uchunk_header;

#pragma pack(pop)

typedef struct {
    uint64_t nano_ts;
    char message[512];
} log_entry;


typedef std::function<void()> (*WSMessageFunc)(JSON_Object*);
extern std::unordered_map<int, WSMessageFunc> g_callback_map;

extern kz::websocket g_websocket;
extern std::atomic<WSState> g_websocket_state;
extern kz::queue<log_entry> g_ws_log;
extern kz::queue<std::shared_ptr<std::string>> g_outgoing_queue;
extern kz::queue<std::function<void()>> g_incoming_queue;

extern void kz_ws_release_active_upload(const char* local_uid, uint64_t attempt = 0);
extern bool kz_ws_is_active_upload(const ws_upload& upload);
extern void kz_ws_wait_for_file_ack(const ws_upload& upload);
extern void kz_ws_cancel_active_uploads();
extern size_t kz_ws_active_upload_count();

extern void kz_ws_start_download_worker();
extern void kz_ws_stop_download_worker();
extern void kz_ws_init(void);
extern void kz_ws_uninit(void);

extern void kz_ws_start(std::string url, std::string token);
extern void kz_ws_stop(void);
/** Re-allow ixwebsocket auto-reconnect after kz_api_url / kz_api_token change. */
extern void kz_ws_reset_auto_reconnect_policy(void);

struct retry_msg;
extern retry_msg kz_ws_store_msg(int type, JSON_Value* data_val, kz::queue<log_entry>* log_queue = &g_ws_log);
extern void kz_ws_build_msg(int type, JSON_Value* data_val, std::string& output, int64_t msg, kz::queue<log_entry>* log_queue = &g_ws_log);
extern void kz_ws_queue_msg(std::shared_ptr<std::string> msg, int64_t msg_id);
extern void kz_ws_send_msg(std::string& msg, int64_t msg_id);

extern void kz_ws_run_tasks(int max_jobs_per_frame);
extern void kz_ws_register(int type, WSMessageFunc pfn);
extern void kz_ws_event_client_connect(edict_t* pEntity);
extern void kz_ws_event_client_disconnect(edict_t* pEntity);
extern void kz_ws_event_map_change(void);

extern std::function<void()> kz_ws_ack_invalid(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_hello(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_map_info(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_player_join(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_record_ack(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_file_ack(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_error(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_get_replay(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_del_record_notify(JSON_Object* obj);

extern bool kz_ws_try_fetch_replay(const char* mapname);
extern void kz_ws_on_map_loaded(bool force);
extern void kz_ws_delete_record_replay(const char* mapname, const char* local_uid);

#endif
