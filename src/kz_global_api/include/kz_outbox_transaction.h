#pragma once

#include <SQLiteCpp/SQLiteCpp.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

inline std::pair<int64_t, std::shared_ptr<std::string>> kz_persist_queue_message(
    SQLite::Database& database, int64_t msg_type, bool outgoing,
    const std::function<std::string(int64_t)>& build, const std::string& upload_uid, const char* record_id)
{
    SQLite::Transaction transaction(database);
    SQLite::Statement insert(database, outgoing
        ? "INSERT INTO outgoing_queue (msg_type, msg) VALUES (?, '')"
        : "INSERT INTO upload_queue (msg_type, local_uid) VALUES (?, '')");
    insert.bind(1, msg_type);
    insert.exec();
    const int64_t id = database.getLastInsertRowid();
    // Parson serializes IDs through double.
    if (outgoing && id > 9007199254740991LL)
    {
        throw std::runtime_error("outgoing ID exceeds the exact JSON integer range");
    }
    auto message = std::make_shared<std::string>(outgoing ? build(id) : upload_uid);
    if (message->empty())
    {
        throw std::runtime_error("cannot persist an empty message");
    }

    SQLite::Statement update(database, outgoing
        ? "UPDATE outgoing_queue SET msg = ? WHERE id = ?"
        : "UPDATE upload_queue SET local_uid = ? WHERE id = ?");
    update.bind(1, *message);
    update.bind(2, id);
    update.exec();
    if (record_id)
    {
        SQLite::Statement identity(database,
            "INSERT INTO replay_identity (local_uid, record_id) VALUES (?, ?) "
            "ON CONFLICT(local_uid) DO UPDATE SET record_id = excluded.record_id");
        identity.bind(1, upload_uid);
        identity.bind(2, record_id);
        identity.exec();
    }
    transaction.commit();
    return {id, std::move(message)};
}
