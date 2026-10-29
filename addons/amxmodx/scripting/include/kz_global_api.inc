#if defined _kz_global_api_included
  #endinput
#endif
#define _kz_global_api_included

#include <kz_global_api_stocks>
#include <kz_global_api_version>

/**
 * Requests map details for a specific map.
 *
 * @note The handler function should follow this signature:
 * public handler(mapname[], wr_pro[], wr_noob[], map_props[3]);
 *
 * @note map_props[3] layout:
 * [0] = map type       (KZM_BHOP - KZM_SPECIAL)
 * [1] = map length     (KZM_VERY_SHORT - KZM_VERY_LONG)
 * [2] = map difficulty (KZM_BEGINNER - KZM_DEATH)
 *
 * @param mapname       The name of the map to look up
 * @param handler       The public callback function to receive the data
 *
 * @return              1 on success, 0 otherwise
 */
native kz_api_get_map_details(mapname[], handler[]);

/**
 * Notifies the API that a player has started a new run.
 */
native kz_api_run_started(id);

/**
 * Notifies the API that a player has paused their run.
 *
 * @return              1 on success, 0 if pause is forbidden (kz_api_allow_pause 0)
 */
native kz_api_run_paused(id);

/**
 * Notifies the API that a player has resumed their run.
 *
 * @return              1 on success, 0 if pause is forbidden (kz_api_allow_pause 0)
 */
native kz_api_run_unpaused(id);

/**
 * Notifies the API that a run has been rejected or invalidated.
 * @param delete_file   If true, tells the backend to discard local replay data.
 */
native kz_api_run_rejected(id, bool:delete_file);

/**
 * Notifies the API that a run has been successfully completed.
 *
 * @param id            Player index.
 * @param time          Run time in seconds.
 * @param submit_run    true  = submit the run and its replay to the API (default).
 *                      false = keep the replay locally only: it is compressed
 *                      and stored in the map's replay folder, and nothing is
 *                      sent to the API. Runs from players with submission
 *                      disabled are never submitted.
 * @param char_prefix   Optional character that replaces the first character of
 *                      the replay filename, which is normally the gocheck flag
 *                      (0 or 1). 0 = no change (default). Pass a character that is
 *                      safe in a filename.
 */
native kz_api_run_finished(id, Float:time, bool:submit_run = true, char_prefix = 0);

/**
 * Notifies the API that a player made a checkpoint.
 */
native kz_api_run_checkpoint(id);

/**
 * Notifies the API that a player teleported to his last checkpoint.
 */
native kz_api_run_gocheck(id);

/**
 * Requests the WR replay for a map from the API (GET_REPLAY).
 */
native kz_api_get_replay(mapname[]);

/**
 * Deletes local replay files for a record (same as DEL_RECORD_NOTIFY handler).
 */
native kz_api_del_record(mapname[], local_uid[]);
