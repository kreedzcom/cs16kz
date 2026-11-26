#include "kz_outbox_transaction.h"

#include <future>
#include <stdexcept>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 1;
    }
    const std::string path(argv[1]);
    {
        SQLite::Database database(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        database.exec("CREATE TABLE outgoing_queue(id INTEGER PRIMARY KEY AUTOINCREMENT, msg_type INTEGER, msg TEXT NOT NULL)");
    }
    auto producer = [&](int type) {
        SQLite::Database database(path, SQLite::OPEN_READWRITE, 5000);
        return kz_persist_queue_message(database, type, true,
            [type](int64_t id) { return std::to_string(type) + ":" + std::to_string(id); }, {}, nullptr);
    };

    auto first = std::async(std::launch::async, producer, 8);
    auto second = std::async(std::launch::async, producer, 5);
    const auto record = first.get();
    const auto lookup = second.get();
    if (record.first == lookup.first)
    {
        throw std::runtime_error("outbox producers allocated the same ID");
    }

    SQLite::Database reopened(path, SQLite::OPEN_READWRITE);
    try
    {
        kz_persist_queue_message(reopened, 8, true, [](int64_t) { return std::string(); }, {}, nullptr);
        throw std::logic_error("an invalid envelope committed");
    }
    catch (const std::runtime_error&)
    {
        // A failed serialization rolls back the provisional row.
    }
    SQLite::Statement rows(reopened, "SELECT id, msg FROM outgoing_queue");
    int matched = 0;
    while (rows.executeStep())
    {
        const auto id = rows.getColumn(0).getInt64();
        const auto payload = rows.getColumn(1).getString();
        if ((id == record.first && payload == *record.second) ||
            (id == lookup.first && payload == *lookup.second))
        {
            ++matched;
        }
        else
        {
            throw std::runtime_error("an invalid or inconsistent outbox row survived");
        }
    }
    if (matched != 2)
    {
        throw std::runtime_error("a committed outbox message was lost");
    }

    SQLite::Statement acknowledge(reopened, "DELETE FROM outgoing_queue WHERE id = ?");
    acknowledge.bind(1, lookup.first);
    acknowledge.exec();
    SQLite::Statement remaining(reopened, "SELECT id FROM outgoing_queue");
    if (!remaining.executeStep() || remaining.getColumn(0).getInt64() != record.first || remaining.executeStep())
    {
        throw std::runtime_error("acknowledging a lookup removed a different transaction");
    }
}
