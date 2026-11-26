#include <amxmodx>
#include <kz_global_api>

public plugin_init()
{
    register_plugin("KZ Global E2E", "1", "KZ");
    register_srvcmd("kz_e2e_initial", "request_initial");
    register_srvcmd("kz_e2e_reconnect", "request_reconnect");
    register_srvcmd("kz_e2e_mismatch", "request_mismatch");
    server_print("[KZ-E2E] READY");
}

public request_initial()
{
    if (!kz_api_get_map_details("kz_e2e_first", "check_initial"))
        server_print("[KZ-E2E] initial FAIL native");
}

public request_reconnect()
{
    if (!kz_api_get_map_details("kz_e2e_second", "check_reconnect"))
        server_print("[KZ-E2E] reconnect FAIL native");
}

public request_mismatch()
{
    if (!kz_api_get_map_details("kz_e2e_first", "check_mismatch"))
        server_print("[KZ-E2E] mismatch FAIL native");
}

public check_initial(map[], pro[], noob[], props[3])
{
    assert_map("initial", map, props, "kz_e2e_first", 1, 2, 3);
}

public check_reconnect(map[], pro[], noob[], props[3])
{
    assert_map("reconnect", map, props, "kz_e2e_second", 0, 4, 7);
}

public check_mismatch(map[], pro[], noob[], props[3])
{
    assert_map("mismatch", map, props, "kz_e2e_first", 1, 2, 4);
}

assert_map(const scenario[], const map[], const props[3], const expected_map[],
    expected_type, expected_length, expected_difficulty)
{
    if (!equal(map, expected_map) || props[0] != expected_type ||
        props[1] != expected_length || props[2] != expected_difficulty)
    {
        server_print("[KZ-E2E] %s FAIL callback %s %d %d %d", scenario, map,
            props[0], props[1], props[2]);
        return;
    }

    server_print("[KZ-E2E] %s PASS", scenario);
}
