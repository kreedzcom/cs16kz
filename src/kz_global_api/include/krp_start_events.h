#pragma once

#include <cstdint>
#include <vector>

struct KrpStartEvents
{
    bool has_frame = false;
    std::vector<uint8_t> deferred;

    bool defer_until_keyframe(uint8_t event)
    {
        if (has_frame)
        {
            return false;
        }
        deferred.push_back(event);
        return true;
    }

    template <typename WriteEvent>
    void after_keyframe(WriteEvent write_event)
    {
        has_frame = true;
        for (const auto event : deferred)
        {
            write_event(event);
        }
        deferred.clear();
    }

    void reset()
    {
        has_frame = false;
        deferred.clear();
    }
};
