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
std::mutex g_storage_mutex;

std::vector<retry_msg> g_retry_queue(64);

static thread_local SQLite::Database* kz_storage_database = nullptr;
static thread_local bool kz_storage_initialiazed = false;
static thread_local kz::queue<std::string> g_storage_log(64);

void kz_storage_init(void)
{
    if(!kz_storage_initialiazed)
    {
        kz_log_addq(&g_storage_log);

        std::filesystem::path dir = g_data_dir / "sqlite3";
        if (!std::filesystem::exists(dir))
        {
            std::error_code ec;
            if (std::filesystem::create_directories(dir, ec))
            {
                kz_log(&g_storage_log, "Directory created: %s", dir.c_str());
            }
            else
            {
                kz_log(&g_storage_log, "Failed to create directory (%s): %s", dir.c_str(), ec.message().c_str());
                return;
            }
        }
        try
        {
            std::filesystem::path file = g_data_dir / "sqlite3" / "storage.sq3";
            kz_storage_database = new SQLite::Database(file.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
            kz_storage_database->exec("PRAGMA journal_mode=WAL;");
            kz_storage_database->exec("CREATE TABLE IF NOT EXISTS outgoing_queue(id INTEGER PRIMARY KEY AUTOINCREMENT, msg TEXT NOT NULL, created_at DATETIME DEFAULT CURRENT_TIMESTAMP)");
            kz_storage_database->exec("CREATE TABLE IF NOT EXISTS replay_up_queue(id INTEGER PRIMARY KEY AUTOINCREMENT, fs_uid TEXT NOT NULL, created_at DATETIME DEFAULT CURRENT_TIMESTAMP)");
            kz_storage_database->setBusyTimeout(5000);

            kz_storage_initialiazed = true;
        }
        catch (const std::exception& e)
        {
            kz_log(&g_storage_log, "[Storage] init: %s", e.what());

        }
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
int64_t kz_storage_get_next_id(StorageTable table)
{
    try
    {
        char statement[64];
        switch(table)
        {
            case StorageTable::outgoing_queue:
            {
                snprintf(statement, sizeof(statement), "SELECT seq FROM sqlite_sequence WHERE name='outgoing_queue'");
                break;
            }
            case StorageTable::replay_up_queue:
            {
                snprintf(statement, sizeof(statement), "SELECT seq FROM sqlite_sequence WHERE name='replay_up_queue'");
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
void kz_storage_save(const std::string& text, int64_t msg_id, StorageTable table)
{
    {
        std::lock_guard<std::mutex> lock(g_storage_mutex);
        
        auto now = std::chrono::system_clock::now();
        g_retry_queue.push_back({msg_id, text, std::chrono::system_clock::to_time_t(now), table});
    }
    try
    {
        char statement[1024];
        switch(table)
        {
            case StorageTable::outgoing_queue:
            {
                snprintf(statement, sizeof(statement), "INSERT INTO outgoing_queue (id, msg) VALUES (?, ?)");
                break;
            }
            case StorageTable::replay_up_queue:
            {
                snprintf(statement, sizeof(statement), "INSERT INTO replay_up_queue (id, fs_uid) VALUES (?, ?)");
                break;
            }
        }
        SQLite::Statement query(*kz_storage_database, statement);
        query.bind(1, static_cast<long long>(msg_id));
        query.bind(2, text);
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
        // TODO: deletion from retry queue
        std::lock_guard<std::mutex> lock(g_storage_mutex);
    }
    try
    {
        char statement[64];
        switch(table)
        {
            case StorageTable::outgoing_queue:
            {
                snprintf(statement, sizeof(statement), "DELETE FROM outgoing_queue WHERE id = ?");
                break;
            }
            case StorageTable::replay_up_queue:
            {
                snprintf(statement, sizeof(statement), "DELETE FROM replay_up_queue WHERE id = ?");
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
