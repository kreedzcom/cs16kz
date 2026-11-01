#include "kz_replay_uid.h"

#include <cstdlib>
#include <limits>

bool kz_replay_parse_uid(const std::string& filename, uint8_t& run_class, uint32_t& time_ms)
{
    std::string stem = filename;
    if (stem.size() >= 5 && stem.compare(stem.size() - 5, 5, ".krpz") == 0)
    {
        stem.resize(stem.size() - 5);
    }

    const size_t sep1 = stem.find('_');
    if (sep1 == std::string::npos || sep1 == 0)
    {
        return false;
    }
    const size_t sep2 = stem.find('_', sep1 + 1);
    if (sep2 == std::string::npos || sep2 <= sep1 + 1)
    {
        return false;
    }

    char* end = nullptr;
    const std::string class_str = stem.substr(0, sep1);
    const long cls = strtol(class_str.c_str(), &end, 10);
    if (!end || *end != '\0' || cls < 0 || cls > 9)
    {
        return false;
    }

    const std::string time_str = stem.substr(sep1 + 1, sep2 - sep1 - 1);
    end = nullptr;
    const unsigned long ms = strtoul(time_str.c_str(), &end, 10);
    if (!end || *end != '\0')
    {
        return false;
    }

    run_class = static_cast<uint8_t>(cls);
    time_ms = static_cast<uint32_t>(ms);
    return true;
}

bool kz_replay_uid_is_better(uint8_t cls_a, uint32_t time_a, uint8_t cls_b, uint32_t time_b)
{
    if (cls_a != cls_b)
    {
        return cls_a < cls_b;
    }
    return time_a < time_b;
}

std::filesystem::path kz_replay_find_best_in_dir(const std::filesystem::path& map_dir)
{
    std::error_code ec;
    if (!std::filesystem::exists(map_dir, ec) || ec || !std::filesystem::is_directory(map_dir, ec) || ec)
    {
        return {};
    }

    std::filesystem::path best_file;
    uint8_t best_class = 2;
    uint32_t best_time_ms = std::numeric_limits<uint32_t>::max();
    bool have_best = false;

    for (const auto& entry : std::filesystem::directory_iterator(map_dir, ec))
    {
        if (ec || !entry.is_regular_file(ec) || ec)
        {
            continue;
        }
        if (entry.path().extension() != ".krpz")
        {
            continue;
        }

        const std::string filename = entry.path().filename().string();
        uint8_t run_class = 0;
        uint32_t time_ms = 0;
        if (!kz_replay_parse_uid(filename, run_class, time_ms))
        {
            continue;
        }

        if (!have_best || kz_replay_uid_is_better(run_class, time_ms, best_class, best_time_ms))
        {
            have_best = true;
            best_class = run_class;
            best_time_ms = time_ms;
            best_file = entry.path();
        }
    }

    return best_file;
}
