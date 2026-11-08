#include "kz_replay_uid.h"

#include <filesystem>
#include <fstream>
#include <string>

static int g_failures = 0;

static void expect(bool cond)
{
    if (!cond)
    {
        ++g_failures;
    }
}

static void touch(const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary);
    out.put('\0');
}

int run_replay_uid_tests(void)
{
    g_failures = 0;

    uint8_t run_class = 0;
    uint32_t time_ms = 0;

    expect(kz_replay_parse_uid("0_00012345_steam_abc.krpz", run_class, time_ms));
    expect(run_class == 0);
    expect(time_ms == 12345U);

    expect(kz_replay_parse_uid("1_100000000_steam_xyz.krpz", run_class, time_ms));
    expect(run_class == 1);
    expect(time_ms == 100000000U);

    expect(kz_replay_parse_uid("9_00012345_steam_abc.krpz", run_class, time_ms));
    expect(run_class == 9);

    expect(!kz_replay_parse_uid("bad.krpz", run_class, time_ms));
    expect(!kz_replay_parse_uid("10_00012345_steam_abc.krpz", run_class, time_ms));
    expect(!kz_replay_parse_uid("", run_class, time_ms));

    expect(kz_replay_uid_is_better(0, 99999U, 1, 1U));
    expect(!kz_replay_uid_is_better(1, 1U, 0, 99999U));
    expect(kz_replay_uid_is_better(0, 100U, 0, 200U));
    expect(!kz_replay_uid_is_better(0, 200U, 0, 100U));

    const std::filesystem::path map_dir =
        std::filesystem::temp_directory_path() / "cs16kz_replay_uid_test";
    std::error_code ec;
    std::filesystem::remove_all(map_dir, ec);
    std::filesystem::create_directories(map_dir, ec);
    expect(!ec);

    touch(map_dir / "1_00005000_bbb_ts.krpz");
    touch(map_dir / "0_00010000_aaa_ts.krpz");
    touch(map_dir / "0_00005000_ccc_ts.krpz");
    touch(map_dir / "not-a-valid-name.krpz");
    touch(map_dir / "readme.txt");

    const std::filesystem::path best = kz_replay_find_best_in_dir(map_dir);
    expect(best.filename() == "0_00005000_ccc_ts.krpz");

    std::filesystem::remove_all(map_dir, ec);

    expect(kz_replay_find_best_in_dir(map_dir).empty());
    expect(kz_replay_find_best_in_dir({}).empty());

    return g_failures;
}
