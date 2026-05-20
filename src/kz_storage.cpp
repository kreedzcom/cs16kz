#include "amxxmodule.h"

#include "pdata.h"
#include "kz_ws.h"
#include "kz_util.h"
#include "kz_cvars.h"
#include "kz_replay.h"
#include "kz_storage.h"
#include "kz_natives.h"

#include <exception>
#include <filesystem>
#include <SQLiteCpp/SQLiteCpp.h>


std::filesystem::path g_data_dir;

std::mutex g_retry_mtx;
std::vector<retry_msg> g_retry_queue(64);

static thread_local SQLite::Database* kz_storage_database = nullptr;
static thread_local bool kz_storage_initialiazed = false;
static thread_local kz::queue<log_entry> g_storage_log(64);

void kz_storage_init(void)
{
    if(!kz_storage_initialiazed)
    {
        kz_log_addq(&g_storage_log);

        std::filesystem::path dir = g_data_dir / "kz_global" / "sqlite3";
        if (!std::filesystem::exists(dir))
        {
            std::error_code ec;
            if (std::filesystem::create_directories(dir, ec))
            {
                kz_log(&g_storage_log, "Directory created: %s", std::filesystem::relative(dir, g_data_dir).c_str());
            }
            else
            {
                kz_log(&g_storage_log, "Failed to create directory (%s): %s", std::filesystem::relative(dir, g_data_dir).c_str(), ec.message().c_str());
                return;
            }
        }
    }
    try
    {
        std::filesystem::path file = g_data_dir / "kz_global" / "sqlite3" / "storage.sq3";
        kz_storage_database = new SQLite::Database(file.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        kz_storage_database->exec("PRAGMA journal_mode=WAL;");
        kz_storage_database->exec("CREATE TABLE IF NOT EXISTS outgoing_queue(id INTEGER PRIMARY KEY AUTOINCREMENT, msg_type INTEGER, msg TEXT NOT NULL, created_at INTEGER DEFAULT (strftime('%s','now')))");
        kz_storage_database->exec("CREATE TABLE IF NOT EXISTS upload_queue(id INTEGER PRIMARY KEY AUTOINCREMENT, msg_type INTEGER, local_uid TEXT NOT NULL, created_at INTEGER DEFAULT (strftime('%s','now')))");
        kz_storage_database->setBusyTimeout(5000);

        kz_storage_initialiazed = true;
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] init: %s", e.what());
    }
}
void kz_storage_uninit(void)
{
    if(kz_storage_database)
    {
        delete kz_storage_database;
        kz_storage_database = nullptr;
        kz_storage_initialiazed = false;
    }
}
void kz_storage_load()
{
    std::lock_guard<std::mutex> lock(g_retry_mtx);
    g_retry_queue.clear();

    kz_storage_database->exec("PRAGMA wal_checkpoint(TRUNCATE);");
    try
    {
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
            auto p = g_plugin_callbacks.find(it->msg_id);
            if (p != g_plugin_callbacks.end())
            {
                MF_UnregisterSPForward(p->second.fwd);
                g_plugin_callbacks.erase(p);
            }
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

int64_t kz_storage_get_next_id(StorageTable table)
{
    try
    {
        char statement[64];
        switch (table)
        {
            case StorageTable::outgoing_queue:
            {
                snprintf(statement, sizeof(statement), "SELECT seq FROM sqlite_sequence WHERE name='outgoing_queue'");
                break;
            }
            case StorageTable::upload_queue:
            {
                snprintf(statement, sizeof(statement), "SELECT seq FROM sqlite_sequence WHERE name='upload_queue'");
                break;
            }
        }
        int64_t next_id = 1;
        SQLite::Statement query(*kz_storage_database, statement);
        if(query.executeStep())
        {
            next_id = query.getColumn(0).getInt64() + 1;
        }
        return next_id;
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] get_next_id: %s", e.what());
    }
    return 1;
}
void kz_storage_save(std::shared_ptr<std::string> text, int64_t msg_type, int64_t msg_id, StorageTable table)
{
    if (table == StorageTable::outgoing_queue || table == StorageTable::upload_queue)
    {
        std::lock_guard<std::mutex> lock(g_retry_mtx);
        
        static int64_t last_ts;
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        if ((ts - last_ts) < 5)
        {
            last_ts = ts;
            ts += 5;
        }
        else
        {
            last_ts = ts;
        }
        g_retry_queue.push_back({0, msg_type, msg_id, ts, table, text});
    }
    try
    {
        char statement[1024];
        switch (table)
        {
            case StorageTable::outgoing_queue:
            {
                snprintf(statement, sizeof(statement), "INSERT INTO outgoing_queue (id, msg_type, msg) VALUES (?, ?, ?)");
                break;
            }
            case StorageTable::upload_queue:
            {
                snprintf(statement, sizeof(statement), "INSERT INTO upload_queue (id, msg_type, local_uid) VALUES (?, ?, ?)");
                break;
            }
        }

        SQLite::Statement query(*kz_storage_database, statement);
        query.bind(1, static_cast<long long>(msg_id));
        query.bind(2, static_cast<long long>(msg_type));
        query.bind(3, *text);
        query.exec();
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] save: %s", e.what());
    }
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
        query.bind(1, value);
        query.exec();
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] delete_by_value: %s", e.what());
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
        char statement[4096];
        int written = 0;

        switch (table)
        {
            case StorageTable::outgoing_queue:
            {
                written = snprintf(statement, sizeof(statement), "DELETE FROM outgoing_queue WHERE id IN (");
                break;
            }
            case StorageTable::upload_queue:
            {
                written = snprintf(statement, sizeof(statement), "DELETE FROM upload_queue WHERE id IN (");
                break;
            }
        }

        char* ptr = statement + written;
        size_t remaining = sizeof(statement) - written;
        for (size_t i = 0; i < ids.size(); ++i)
        {
            int n = snprintf(ptr, remaining, "%lld%s", static_cast<long long>(ids[i]), (i == ids.size() - 1) ? ")":",");
            ptr += n;
            remaining -= n;
        }
        kz_storage_database->exec(statement);
    }
    catch (const std::exception& e)
    {
        kz_log(&g_storage_log, "[Storage] batch delete: %s", e.what());
    }
}
