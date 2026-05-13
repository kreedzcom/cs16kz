#ifndef KZ_WS_H
#define KZ_WS_H

#include <parson.h>
#include <rigtorp/SPSCQueue.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <string>
#include <set>

enum class WSState : int
{
    Uninitialized = 0,
    Initialiazed,
    Connected,
    Disconnected,
    DisconnectedButWorthRetrying,
    _MAX,
};

enum class WSMessageType : int
{
    invalid = 0,

    hello,
    map_info,
    client_info,

    add_record,
    del_record,
    get_replay,

    file,
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
extern WSMessageFunc g_callback_table[];

extern kz::websocket g_websocket;
extern std::atomic<WSState> g_websocket_state;
extern kz::queue<log_entry> g_ws_log;
extern kz::queue<std::shared_ptr<std::string>> g_outgoing_queue;
extern kz::queue<std::function<void()>> g_incoming_queue;

extern std::mutex g_active_uploads_mtx;
extern std::set<std::string> g_active_uploads;

extern void kz_ws_init(void);
extern void kz_ws_uninit(void);

extern void kz_ws_start(std::string url, std::string token);
extern void kz_ws_stop(void);

extern void kz_ws_build_msg(WSMessageType type, JSON_Value* data_val, std::string& output, int64_t msg, kz::queue<log_entry>* log_queue = &g_ws_log);
extern void kz_ws_queue_msg(std::shared_ptr<std::string> msg, int64_t msg_id);
extern void kz_ws_send_msg(std::string& msg, int64_t msg_id);

extern void kz_ws_run_tasks(int max_jobs_per_frame);
extern void kz_ws_register(WSMessageType type, WSMessageFunc pfn);
extern void kz_ws_event_client_connect(edict_t* pEntity);

extern std::function<void()> kz_ws_ack_invalid(JSON_Object* obj);

extern std::function<void()> kz_ws_ack_hello(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_map_info(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_client_info(JSON_Object* obj);

extern std::function<void()> kz_ws_ack_add_record(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_del_record(JSON_Object* obj);
extern std::function<void()> kz_ws_ack_get_replay(JSON_Object* obj);

extern std::function<void()> kz_ws_ack_file(JSON_Object* obj);

template <typename T> constexpr int ectoi(T ec) { return static_cast<int>(ec); }
#endif
