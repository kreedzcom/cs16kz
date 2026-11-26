#ifndef KZ_REPLAY_UID_H
#define KZ_REPLAY_UID_H

#include <cstdint>
#include <filesystem>
#include <string>

/** Replay filename: <class:1>_<time_ms>_<steamid>_<timestamp>.krpz */
std::string kz_replay_cache_uid(const std::string& uid, const std::string& record_id);

bool kz_replay_parse_uid(const std::string& filename, uint8_t& run_class, uint32_t& time_ms);

/** PRO (0) beats NUB (1); within a class, lower time_ms wins. */
bool kz_replay_uid_is_better(uint8_t cls_a, uint32_t time_a, uint8_t cls_b, uint32_t time_b);

/** Best .krpz in map_dir by class then time; empty if none valid. */
std::filesystem::path kz_replay_find_best_in_dir(const std::filesystem::path& map_dir);

/** Why kz_rp_prune_replays was called. The cvar selects which reasons run. */
enum class kz_replay_prune_reason : uint8_t
{
    Upload,
    MapChange,
    MapLoad,
};

/**
 * trigger 0: Upload and MapLoad.
 * trigger 1: MapChange and MapLoad.
 * Any other trigger: MapLoad only.
 * A null cvar is passed as 0.
 */
bool kz_replay_should_prune(int trigger, kz_replay_prune_reason reason);

/** 0 leaves the class digit. Any other value must be one ASCII letter or digit. */
uint8_t kz_replay_filename_prefix(uint8_t prefix);

#endif
