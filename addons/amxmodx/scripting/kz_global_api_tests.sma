#include <amxmodx>
#include <kz_global_api>

new Float:g_fTimer[33];
new Float:g_fPauseTime[33];

public plugin_init()
{
	register_concmd("run_start",   "cmd_start");
	register_concmd("run_pause",   "cmd_pause")
	register_concmd("run_unpause", "cmd_unpause");
	register_concmd("run_reject",  "cmd_reject")
	register_concmd("run_finish",  "cmd_finish");

	kz_api_get_map_details("de_dust2",     "map_details_handler");
	kz_api_get_map_details("kz_longjump2", "map_details_handler");
	kz_api_get_map_details("bkz_goldbhop", "map_details_handler");
}
public map_details_handler(mapname[], wr[], sr[], map_props[3])
{
	new szType[32], szLength[32], szDifficulty[32];

	kz_api_get_map_type(map_props[0], szType, charsmax(szType));
	kz_api_get_map_length(map_props[1], szLength, charsmax(szLength));
	kz_api_get_map_difficulty(map_props[2], szDifficulty, charsmax(szDifficulty));

	server_print("[AMXX] Received details for map (%s): [type: %d][length: %d][diff: %d] - [%s][%s][%s]", mapname, map_props[0], map_props[1], map_props[2], szType, szLength, szDifficulty);
}
public cmd_start(id)
{
    g_fTimer[id] = get_gametime();

    server_print("[TEST] Starting run for ID %d...", id);
    client_print_color(id, print_team_red, "[^3TEST^1] Your run has started.");
    kz_api_run_started(id);
}

public cmd_pause(id)
{
    g_fPauseTime[id] = get_gametime();

    server_print("[TEST] Pausing run for ID %d...", id);
    client_print_color(id, print_team_red, "[^3TEST^1] Run paused.");
    kz_api_run_paused(id);
}

public cmd_unpause(id)
{
    g_fTimer[id] += get_gametime() - g_fPauseTime[id];

    server_print("[TEST] Unpausing run for ID %d...", id);
    client_print_color(id, print_team_red, "[^3TEST^1] Run unpaused.");
    kz_api_run_unpaused(id);
}

public cmd_finish(id)
{
    new Float:finishTime = get_gametime() - g_fTimer[id];

    server_print("[TEST] Finishing run for ID %d with time %.3f", id, finishTime);
    client_print_color(id, print_team_red, "[^3TEST^1] Run finished! Time: %.3f", finishTime);
    kz_api_run_finished(id, finishTime);
}

public cmd_reject(id)
{
    server_print("[ TEST] Rejecting run for ID %d (deleting temp files)", id);
    client_print_color(id, print_team_red, "[^3TEST^1] Run rejected and files cleared.");
    kz_api_run_rejected(id, true);
}