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
 */
native kz_api_run_paused(id);

/**
 * Notifies the API that a player has resumed their run.
 */
native kz_api_run_unpaused(id);

/**
 * Notifies the API that a run has been rejected or invalidated.
 * @param delete_file   If true, tells the backend to discard local replay data.
 */
native kz_api_run_rejected(id, bool:delete_file);

/**
 * Notifies the API that a run has been successfully completed.
 */
native kz_api_run_finished(id, Float:time);

/**
 * Notifies the API that a player made a checkpoint.
 */
native kz_api_run_checkpoint(id);

/**
 * Notifies the API that a player teleported to his last checkpoint.
 */
native kz_api_run_gocheck(id);
