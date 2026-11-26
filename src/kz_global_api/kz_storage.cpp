#include "amxxmodule.h"

#include "pdata.h"
#include "kz_ws.h"
#include "kz_util.h"
#include "kz_cvars.h"
#include "kz_replay.h"
#include "kz_storage.h"
#include "kz_outbox_transaction.h"
#include "kz_natives.h"

#include <exception>
#include <filesystem>
#include <SQLiteCpp/SQLiteCpp.h>


std::filesystem::path g_data_dir;

std::mutex g_retry_mtx;
std::vector<retry_msg> g_retry_queue;

static thread_local SQLite::Database* kz_storage_database = nullptr;
static thread_local bool kz_storage_initialized = false;
static thread_local kz::queue<log_entry> g_storage_log(64);

/* Registers this thread's storage log queue exactly once (survives WS
 * reconnect init/uninit cycles) and unregisters it at thread exit, so
 * kz_log_flush never iterates a dangling thread_local queue. */
namespace {
struct storage_log_registration
{
    storage_log_registration()  { kz_log_addq(&g_storage_log); }
    ~storage_log_registration() { kz_log_removeq(&g_storage_log); }
};
}

void kz_storage_init(void)
{
    static thread_local storage_log_registration s_log_registration;

    if (!kz_storage_initialized)
    {
        std::filesystem::path dir = g_data_dir / "kz_global" / "sqlite3";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
        {
            kz_log(&g_storage_log, "Failed to create directory (%s): %s",
                std::filesystem::relative(dir, g_data_dir).string().c_str(), ec.message().c_str());
            return;
        }
    }
    if (kz_storage_initialized && kz_storage_database)
    {
        return;
    }
    try
    {
        std::filesystem::path file = g_data_dir / "kz_global" / "sqlite3" / "storage.sq3";
        kz_storage_database = new SQLite::Database(file.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        kz_storage_database->exec("PRAGMA journal_mode=WAL;");
        kz_storage_database->exec("CREATE TABLE IF NOT EXISTS outgoing_queue(id INTEGER PRIMARY KEY AUTOINCREMENT, msg_type INTEGER, msg TEXT NOT NULL, created_at INTEGER DEFAULT (strftime('%s','now')))");
        kz_storage_database->exec("CREATE TABLE IF NOT EXISTS upload_queue(id INTEGER PRIMARY KEY AUTOINCREMENT, msg_type INTEGER, local_uid TEXT NOT NULL, created_at INTEGER DEFAULT (strftime('%s','now')))");
        kz_storage_database->exec("CREATE TABLE IF NOT EXISTS replay_identity(local_uid TEXT PRIMARY KEY, record_id TEXT NOT NULL)");
        kz_storage_database->setBusyTimeout(5000);

        kz_storage_initialized = true;
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] init: %s", e.what());
        if (kz_storage_database)
        {
            delete kz_storage_database;
            kz_storage_database = nullptr;
        }
        kz_storage_initialized = false;
    }
}
void kz_storage_uninit(void)
{
    if(kz_storage_database)
    {
        delete kz_storage_database;
        kz_storage_database = nullptr;
        kz_storage_initialized = false;
    }
}
void kz_storage_load()
{
    std::lock_guard<std::mutex> lock(g_retry_mtx);
    g_retry_queue.clear();

    if (!kz_storage_database)
    {
        kz_log(&g_storage_log, "[Storage] load skipped: database not initialized");
        return;
    }

    try
    {
        kz_storage_database->exec("PRAGMA wal_checkpoint(TRUNCATE);");
        SQLite::Statement discard_presence(*kz_storage_database, "DELETE FROM outgoing_queue WHERE msg_type IN (?, ?)");
        discard_presence.bind(1, WSMsgOut::PLAYER_JOIN);
        discard_presence.bind(2, WSMsgOut::PLAYER_LEAVE);
        discard_presence.exec();
        SQLite::Statement outgoing(*kz_storage_database, "SELECT id, msg_type, msg, created_at FROM outgoing_queue");
        SQLite::Statement upload(*kz_storage_database, "SELECT id, msg_type, local_uid, created_at FROM upload_queue");

        while (outgoing.executeStep())
        {
            g_retry_queue.push_back({
                    0, // retry_count
                    outgoing.getColumn(1).getInt64(), // msg_type
                    outgoing.getColumn(0).getInt64(), // msg_id
                    outgoing.getColumn(3).getInt64(), // timestamp
                    StorageTable::outgoing_queue,
                    std::make_shared<std::string>(std::move(outgoing.getColumn(2).getText()))
            });
        }
        while (upload.executeStep())
        {
            g_retry_queue.push_back({
                    0,
                    upload.getColumn(1).getInt64(), // msg_type as -> rec_id
                    upload.getColumn(0).getInt64(), // msg_id
                    upload.getColumn(3).getInt64(), // timestamp
                    StorageTable::upload_queue,
                    std::make_shared<std::string>(std::move(upload.getColumn(2).getText())) // text as -> local_uid
            });
        }
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] load: %s", e.what());
    }
}
void kz_storage_clear()
{
    std::lock_guard<std::mutex> lock(g_retry_mtx);
    if (g_retry_queue.empty())
    {
        return;
    }

    std::vector<int64_t> delete_list[2];
    auto it = g_retry_queue.begin();

    while (it != g_retry_queue.end())
    {
        bool delete_msg = false;
        if (it->msg_type == WSMsgOut::WANT_MAP_INFO || it->msg_type == WSMsgOut::PLAYER_JOIN || it->msg_type == WSMsgOut::PLAYER_LEAVE)
        {
            delete_msg = true;
        }
        if (it->msg_type == WSMsgOut::WANT_MAP_INFO)
        {
            kz_api_cancel_map_callback(it->msg_id);
        }
        if (delete_msg)
        {
            delete_list[static_cast<int>(it->table)].push_back(it->msg_id);
            it = g_retry_queue.erase(it);
        }
        else
        {
            ++it;
        }
    }

    kz_storage_batch_delete(delete_list[0], StorageTable::outgoing_queue);
    //kz_storage_batch_delete(delete_list[1], StorageTable::upload_queue);
}

void kz_storage_requeue_all_pending(void)
{
    std::lock_guard<std::mutex> lock(g_retry_mtx);
    for (auto& r : g_retry_queue)
    {
        r.timestamp   = 0;
        r.retry_count = 0;
    }
}

retry_msg kz_storage_save(int64_t msg_type, StorageTable table, const std::function<std::string(int64_t)>& build, const char* record_id)
{
    if (!kz_storage_database)
    {
        kz_storage_init();
        if (!kz_storage_database)
        {
            return {};
        }
    }

    try
    {
        std::lock_guard<std::mutex> lock(g_retry_mtx);
        const bool outgoing = table == StorageTable::outgoing_queue;
        const std::string upload_uid = outgoing ? std::string() : build(0);
        if (!outgoing)
        {
            const auto existing = std::find_if(g_retry_queue.begin(), g_retry_queue.end(), [&](const retry_msg& row) {
                return row.table == StorageTable::upload_queue && *row.message == upload_uid;
            });
            if (existing != g_retry_queue.end())
            {
                return *existing;
            }
        }
        if (g_retry_queue.size() == g_retry_queue.capacity())
        {
            g_retry_queue.reserve(std::max<size_t>(64, g_retry_queue.size() * 2));
        }
        const auto [id, message] = kz_persist_queue_message(
            *kz_storage_database, msg_type, outgoing, build, upload_uid, record_id);

        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        const retry_msg entry{0, msg_type, id, outgoing ? timestamp : 0, table, message};
        g_retry_queue.push_back(entry);
        return entry;
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] save: %s", e.what());
        return {};
    }
}
std::string kz_storage_get_record_id(const std::string& local_uid)
{
    if (!kz_storage_database)
    {
        return {};
    }
    try
    {
        SQLite::Statement query(*kz_storage_database, "SELECT record_id FROM replay_identity WHERE local_uid = ?");
        query.bind(1, local_uid);
        if (query.executeStep())
        {
            return query.getColumn(0).getString();
        }
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] Read replay identity: %s", e.what());
    }
    return {};
}

void kz_storage_delete(int64_t msg_id, StorageTable table)
{
    {
        std::lock_guard<std::mutex> lock(g_retry_mtx);

        auto rif = std::remove_if(g_retry_queue.begin(), g_retry_queue.end(),
                [msg_id, table](const retry_msg& r) {
                    return r.msg_id == msg_id && r.table == table;
                });
        if (rif != g_retry_queue.end())
        {
            g_retry_queue.erase(rif, g_retry_queue.end());
        }
    }
    try
    {
        char statement[64];
        switch (table)
        {
            case StorageTable::outgoing_queue:
            {
                snprintf(statement, sizeof(statement), "DELETE FROM outgoing_queue WHERE id = ?");
                break;
            }
            case StorageTable::upload_queue:
            {
                snprintf(statement, sizeof(statement), "DELETE FROM upload_queue WHERE id = ?");
                break;
            }
        }
        SQLite::Statement query(*kz_storage_database, statement);
        query.bind(1, static_cast<long long>(msg_id));
        query.exec();
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] delete: %s", e.what());
    }
}
bool kz_storage_try_get_outgoing(int64_t msg_id, int64_t* msg_type_out, std::string* msg_out)
{
    if (!kz_storage_database || msg_id <= 0)
    {
        return false;
    }
    try
    {
        SQLite::Statement query(*kz_storage_database, "SELECT msg_type, msg FROM outgoing_queue WHERE id = ?");
        query.bind(1, static_cast<long long>(msg_id));
        if (!query.executeStep())
        {
            return false;
        }
        if (msg_type_out)
        {
            *msg_type_out = query.getColumn(0).getInt64();
        }
        if (msg_out)
        {
            *msg_out = query.getColumn(1).getText();
        }
        return true;
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] try_get_outgoing: %s", e.what());
        return false;
    }
}
void kz_storage_delete_by_value(const std::string& value, StorageTable table)
{
    try
    {
        char statement[128];
        switch (table)
        {
            case StorageTable::upload_queue:
            {
                snprintf(statement, sizeof(statement), "DELETE FROM upload_queue WHERE local_uid = ?");
                break;
            }
            default:
                return;
        }
        SQLite::Statement query(*kz_storage_database, statement);
        std::lock_guard<std::mutex> lock(g_retry_mtx);
        query.bind(1, value);
        query.exec();
        g_retry_queue.erase(std::remove_if(g_retry_queue.begin(), g_retry_queue.end(), [&](const retry_msg& row) {
            return row.table == table && *row.message == value;
        }), g_retry_queue.end());
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] delete_by_value: %s", e.what());
    }
}
static const char* kz_storage_table_name(StorageTable table)
{
    switch (table)
    {
        case StorageTable::outgoing_queue: return "outgoing_queue";
        case StorageTable::upload_queue:   return "upload_queue";
    }
    return "?";
}
static const char* kz_storage_msg_type_name(int64_t type)
{
    switch (type)
    {
        case WSMsgOut::HELLO:         return "hello";
        case WSMsgOut::MAP_CHANGE:    return "map_change";
        case WSMsgOut::PLAYER_JOIN:   return "player_join";
        case WSMsgOut::PLAYER_LEAVE:  return "player_leave";
        case WSMsgOut::WANT_MAP_INFO: return "want_map_info";
        case WSMsgOut::ADD_RECORD:    return "add_record";
        case WSMsgOut::GET_REPLAY:    return "get_replay";
    }
    return "?";
}
static const char* kz_storage_msg_data(const char* text)
{
    const char* data = strstr(text, "\"data\"");
    return (data ? data + 7 : text);
}
int64_t kz_storage_count(StorageTable table)
{
    if (!kz_storage_database)
    {
        return -1;
    }
    try
    {
        char statement[64];
        snprintf(statement, sizeof(statement), "SELECT COUNT(*) FROM %s", kz_storage_table_name(table));

        SQLite::Statement query(*kz_storage_database, statement);
        if (query.executeStep())
        {
            return query.getColumn(0).getInt64();
        }
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] count: %s", e.what());
    }
    return -1;
}
void kz_storage_print(StorageTable table, int limit, bool from_end)
{
    if (!kz_storage_database)
    {
        MF_PrintSrvConsole("[Storage] Database unavailable.\n\n");
        return;
    }
    try
    {
        const bool outgoing = (table == StorageTable::outgoing_queue);
        const char* payload = outgoing ? "msg" : "local_uid";

        char statement[128];
        snprintf(statement, sizeof(statement), "SELECT id, msg_type, created_at, %s FROM %s ORDER BY id %s LIMIT ?", payload, kz_storage_table_name(table), from_end ? "DESC" : "ASC");

        SQLite::Statement query(*kz_storage_database, statement);
        query.bind(1, limit);

        MF_PrintSrvConsole("%-8s %-14s %-12s %s\n", "id", outgoing ? "type" : "rec_id", "created_at", payload);
        while (query.executeStep())
        {
            char excerpt[101]; // keep console lines readable
            const char* text = query.getColumn(3).getText();
            snprintf(excerpt, sizeof(excerpt), "%s", outgoing ? kz_storage_msg_data(text) : text);

            if (outgoing)
            {
                MF_PrintSrvConsole("%-8lld %-14s %-12lld %s\n",
                    static_cast<long long>(query.getColumn(0).getInt64()),
                    kz_storage_msg_type_name(query.getColumn(1).getInt64()),
                    static_cast<long long>(query.getColumn(2).getInt64()),
                    excerpt);
            }
            else
            {
                MF_PrintSrvConsole("%-8lld %-14lld %-12lld %s\n",
                    static_cast<long long>(query.getColumn(0).getInt64()),
                    static_cast<long long>(query.getColumn(1).getInt64()),
                    static_cast<long long>(query.getColumn(2).getInt64()),
                    excerpt);
            }
        }
        MF_PrintSrvConsole("\n");
    }
    catch (const std::exception& e)
    {
        MF_PrintSrvConsole("[Storage] list failed: %s\n\n", e.what());
    }
}
void kz_storage_print_row(StorageTable table, int64_t id)
{
    if (!kz_storage_database)
    {
        MF_PrintSrvConsole("[Storage] Database unavailable.\n\n");
        return;
    }
    try
    {
        const char* payload = (table == StorageTable::outgoing_queue) ? "msg" : "local_uid";

        char statement[128];
        snprintf(statement, sizeof(statement), "SELECT id, msg_type, created_at, %s FROM %s WHERE id = ?", payload, kz_storage_table_name(table));

        SQLite::Statement query(*kz_storage_database, statement);
        query.bind(1, static_cast<long long>(id));

        if (!query.executeStep())
        {
            MF_PrintSrvConsole("[Storage] No row with id %lld in %s.\n\n", static_cast<long long>(id), kz_storage_table_name(table));
            return;
        }

        if (table == StorageTable::outgoing_queue)
        {
            MF_PrintSrvConsole("id: %lld, type: %s, created_at: %lld, %s:\n",
                static_cast<long long>(query.getColumn(0).getInt64()),
                kz_storage_msg_type_name(query.getColumn(1).getInt64()),
                static_cast<long long>(query.getColumn(2).getInt64()),
                payload);
        }
        else
        {
            MF_PrintSrvConsole("id: %lld, rec_id: %lld, created_at: %lld, %s:\n",
                static_cast<long long>(query.getColumn(0).getInt64()),
                static_cast<long long>(query.getColumn(1).getInt64()),
                static_cast<long long>(query.getColumn(2).getInt64()),
                payload);
        }

        // The engine print buffer clips long lines; emit the payload in chunks.
        const char* text = query.getColumn(3).getText();
        const size_t len = strlen(text);

        char buffer[513];
        for (size_t off = 0; off < len; off += (sizeof(buffer) - 1))
        {
            const size_t n = ((len - off) < (sizeof(buffer) - 1)) ? (len - off) : (sizeof(buffer) - 1);
            memcpy(buffer, text + off, n);
            buffer[n] = '\0';
            MF_PrintSrvConsole("%s", buffer);
        }
        MF_PrintSrvConsole("\n\n");
    }
    catch (const std::exception& e)
    {
        MF_PrintSrvConsole("[Storage] show failed: %s\n\n", e.what());
    }
}
void kz_storage_delete_all(StorageTable table)
{
    {
        std::lock_guard<std::mutex> lock(g_retry_mtx);
        g_retry_queue.erase(std::remove_if(g_retry_queue.begin(), g_retry_queue.end(),
                [table](const retry_msg& r) { return r.table == table; }), g_retry_queue.end());
    }
    if (!kz_storage_database)
    {
        return;
    }
    try
    {
        char statement[64];
        snprintf(statement, sizeof(statement), "DELETE FROM %s", kz_storage_table_name(table));
        kz_storage_database->exec(statement);
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] delete_all: %s", e.what());
    }
}
bool kz_storage_checkpoint(void)
{
    if (!kz_storage_database)
    {
        return false;
    }
    try
    {
        kz_storage_database->exec("PRAGMA wal_checkpoint(TRUNCATE);");
        return true;
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] checkpoint: %s", e.what());
        return false;
    }
}
void kz_storage_batch_delete(const std::vector<int64_t>& ids, StorageTable table)
{
    if(ids.empty())
    {
        return;
    }
    try
    {
        const char* statement = nullptr;

        switch (table)
        {
            case StorageTable::outgoing_queue:
            {
                statement = "DELETE FROM outgoing_queue WHERE id = ?";
                break;
            }
            case StorageTable::upload_queue:
            {
                statement = "DELETE FROM upload_queue WHERE id = ?";
                break;
            }
        }

        SQLite::Transaction transaction(*kz_storage_database);
        SQLite::Statement query(*kz_storage_database, statement);

        for (int64_t id : ids)
        {
            query.bind(1, static_cast<long long>(id));
            query.exec();
            query.reset();
        }

        transaction.commit();
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] batch delete: %s", e.what());
    }
}