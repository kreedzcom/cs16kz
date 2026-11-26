#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class KzUploadAttempts
{
    using Clock = std::chrono::steady_clock;

public:
    uint64_t start(const std::string& uid, Clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto current = active_.find(uid);
        if (current != active_.end() && current->second.deadline > now)
        {
            return 0;
        }
        const auto id = ++next_id_;
        active_.insert_or_assign(uid, Attempt{id, Clock::time_point::max()});
        return id;
    }

    bool is_current(const std::string& uid, uint64_t id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto current = active_.find(uid);
        return current != active_.end() && current->second.id == id;
    }

    void sent(const std::string& uid, uint64_t id, Clock::time_point deadline)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto current = active_.find(uid);
        if (current != active_.end() && current->second.id == id)
        {
            current->second.deadline = deadline;
        }
    }

    void release(const std::string& uid, uint64_t id = 0)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto current = active_.find(uid);
        if (current != active_.end() && (id == 0 || current->second.id == id))
        {
            active_.erase(current);
        }
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_.size();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_.clear();
    }

private:
    struct Attempt
    {
        uint64_t id;
        Clock::time_point deadline;
    };

    std::mutex mutex_;
    std::unordered_map<std::string, Attempt> active_;
    uint64_t next_id_ = 0;
};
