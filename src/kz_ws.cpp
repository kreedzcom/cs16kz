#include "amxxmodule.h"

#include "pdata.h"
#include "kz_ws.h"
#include "kz_util.h"
#include "kz_cvars.h"
#include "kz_storage.h"
#include <chrono>

std::string g_hello_msg;
kz::websocket g_websocket;
std::atomic<WSState> g_websocket_state;

kz::queue<log_entry> g_ws_log(64);
kz::queue<std::shared_ptr<std::string>> g_outgoing_queue(64);
kz::queue<std::function<void()>> g_incoming_queue(64);

static void kz_ws_onmessage(const ix::WebSocketMessagePtr& msg)
{
    switch(msg->type)
    {
        case ix::WebSocketMessageType::Open:
        {
            kz_storage_init();
            kz_log(&g_ws_log, "[WS] Connection established.");
            g_websocket_state.store(WSState::Connected);
            kz_ws_send_msg(g_hello_msg, 0);
            break;
        }
        case ix::WebSocketMessageType::Message:
        {
            const char* c_data = msg->str.c_str();
            if(kz_api_log_recv->value > 0.0f)
            {
                kz_log(&g_ws_log, "[WS] Received: %s", c_data);
            }

            JSON_Value* root_val = json_parse_string(c_data);
            if(!root_val)
            {
                kz_log(&g_ws_log, "[WS] Failed to parse json.");
                break;
            }
            if (json_value_get_type(root_val) != JSONObject)
            {
                kz_log(&g_ws_log, "[WS] Expected a JSON object, got type %d.", json_value_get_type(root_val));
                json_value_free(root_val);
                break;
            }

            JSON_Object* root_obj = json_value_get_object(root_val);
            int32_t msg_type = json_object_get_number(root_obj, "msg_type");
            int64_t msg_id = json_object_get_number(root_obj, "msg_id");

            std::function<void()> fn = nullptr;
            if (msg_type <= 0 || msg_type >= ectoi(WSMessageType::_MAX))
            {
                fn = g_callback_table[ectoi(WSMessageType::invalid)](root_obj);
            }
            else
            {
                fn = g_callback_table[msg_type](root_obj);
            }

            bool delete_msg = true;
            if (fn)
            {
                auto start_time = std::chrono::steady_clock::now();
                while (!g_incoming_queue.try_push(std::move(fn)))
                {
                    auto curr_time = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(curr_time - start_time).count();
                    if (elapsed >= 10)
                    {
                        delete_msg = false;
                        kz_log(&g_ws_log, "[WS] Incoming queue is full");
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }
            if (delete_msg && msg_id > 0)
            {
                kz_storage_delete(msg_id, StorageTable::outgoing_queue);
            }
            json_value_free(root_val);
            break;
        }
        case ix::WebSocketMessageType::Fragment:
        {
            break;
        }
        case ix::WebSocketMessageType::Ping:
        {
            kz_log(&g_ws_log, "[WS] Ping.");
            break;
        }
        case ix::WebSocketMessageType::Pong:
        {
            kz_log(&g_ws_log, "[WS] Pong.");
            break;
        }
        case ix::WebSocketMessageType::Close:
        {
            kz_storage_uninit();
            kz_log(&g_ws_log, "[WS] Connection closed (%d): %s", msg->closeInfo.code, msg->closeInfo.reason.c_str());
            switch(msg->closeInfo.code)
            {
                default:
                {
                    g_websocket_state.store(WSState::Disconnected);
                    break;
                }
            }
            break;
        }
        case ix::WebSocketMessageType::Error:
        {
            kz_storage_uninit();
            kz_log(&g_ws_log, "[WS] Error occured (%d): %s", msg->errorInfo.http_status, msg->errorInfo.reason.c_str());
            switch(msg->errorInfo.http_status)
            {
                case 401: // Unauthorized
                case 403: // Forbidden
                {
                    g_websocket.disableAutomaticReconnection();
                    g_websocket_state.store(WSState::Disconnected);
                    break;
                }
                case 429: // Too Many Requests
                case 500: // Internal Server Error aka gLp fucked up.
                case 502: // Bad gateway
                {
                    g_websocket.setMinWaitBetweenReconnectionRetries(10000);
                    g_websocket.setMaxWaitBetweenReconnectionRetries(30000);
                    g_websocket_state.store(WSState::DisconnectedButWorthRetrying);
                    break;
                }
                default:
                {
                    g_websocket_state.store(WSState::DisconnectedButWorthRetrying);
                    break;
                }
            }
            break;
        }
    }
}
void kz_ws_init()
{
    kz_log_addq(&g_ws_log);
    ix::initNetSystem();

    JSON_Value* data_val = json_value_init_object();
    JSON_Object* data_obj = json_value_get_object(data_val);

    char szIP[16];
    char szPort[6];
    const char* addr = CVAR_GET_STRING("net_address");
    split_net_address(addr, szIP, sizeof(szIP), szPort, sizeof(szPort));

    json_object_dotset_string(data_obj, "plugin.version", MODULE_VERSION);
    //json_object_dotset_string(data_obj, "plugin.checksum", MODULE_CHECKSUM);

    json_object_dotset_string(data_obj, "server.hostname", CVAR_GET_STRING("hostname"));
    json_object_dotset_string(data_obj, "server.address", szIP);
    json_object_dotset_string(data_obj, "server.port", szPort);

    kz_ws_build_msg(WSMessageType::hello, data_val, g_hello_msg, (int64_t)0);
}
void kz_ws_uninit(void)
{
    kz_ws_stop();
    ix::uninitNetSystem();
}
void kz_ws_start(std::string url, std::string token)
{
    if(url.empty() || url.size() < 4 || token.empty())
    {
        return;
    }
    if(url.substr(0, 5) != "ws://" && url.substr(0, 6) != "wss://")
    {
        return;
    }

    ix::SocketTLSOptions tls_options;
    tls_options.caFile = std::filesystem::path("cstrike/addons/amxmodx/data/kz_global/cacert.pem").string();


    ix::WebSocketHttpHeaders headers; 
    headers["Authorization"] = std::string("Bearer ") + token;

    g_websocket.setUrl(url);
    g_websocket.setTLSOptions(tls_options);
    g_websocket.setExtraHeaders(headers);
    g_websocket.enablePerMessageDeflate();
    g_websocket.enableAutomaticReconnection();
    g_websocket.setOnMessageCallback(kz_ws_onmessage);
    g_websocket.start();

    g_websocket_state.store(WSState::Initialiazed);
}
void kz_ws_stop(void)
{
    g_websocket.stop();
}
void kz_ws_build_msg(WSMessageType type, JSON_Value* data_val, std::string& msg, int64_t msg_id, kz::queue<log_entry>* log_queue)
{
    JSON_Value* root_val = json_value_init_object();
    JSON_Object* root_obj = json_value_get_object(root_val);

    json_object_set_number(root_obj, "msg_type", ectoi(type));
    json_object_set_number(root_obj, "msg_id", msg_id);

    data_val ? json_object_set_value(root_obj, "data", data_val) : json_object_set_null(root_obj, "data");

    char* serialized = json_serialize_to_string(root_val);
    if(serialized)
    {
        msg = serialized;
        json_free_serialized_string(serialized);
    }
    else
    {
        kz_log(log_queue, "[WS] Failed to serialize json (msg_type: %d)", ectoi(type));
    }
    json_value_free(root_val);
}
void kz_ws_queue_msg(std::shared_ptr<std::string> msg, int64_t msg_id)
{
    if(!msg || msg->empty())
    {
        kz_log(&g_ws_log, "[WS] Tried to queue empty message.");
        return;
    }
    if(!g_outgoing_queue.try_push(msg))
    {
        kz_log(&g_ws_log, "[WS] Failed to queue message [sid: %lld]: %s", msg_id, msg->c_str());
    }
    return;
}
void kz_ws_send_msg(std::string& msg, int64_t msg_id)
{
    if(msg.empty())
    {
        kz_log(&g_ws_log, "[WS] Tried to send empty message.");
        return;
    }

    ix::WebSocketSendInfo result = g_websocket.sendUtf8Text(msg);
    if (!result.success)
    {
        kz_log(&g_ws_log, "[WS] Failed to send message [sid: %lld]: %s", msg_id, msg.c_str());
    }
    else if(kz_api_log_send->value > 0.0f)
    {
        kz_log(&g_ws_log, "[WS] Send: %s", msg.c_str());
    }
}
