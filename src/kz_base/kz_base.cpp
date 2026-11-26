#include "amxxmodule.h"

#include "pdata.h"
#include "kz_api.h"
#include "kz_base.h"
#include "kz_util.h"
#include "kz_mpbhop.h"

#include <algorithm>

kz_player_t g_players[KZ_MAX_PLAYERS + 1];

void kz_base_reset_checkpoints(int id)
{
    if (!is_player(id))
    {
        return;
    }
    auto& player = g_players[id];
    const kz_checkpoint_t empty{Vector(0.0f, 0.0f, 0.0f), Vector(0.0f, 0.0f, 0.0f)};
    std::fill_n(player.checkpoints, KZ_MAX_CHECKPOINT_CACHE, empty);
    std::fill_n(player.paused_checkpoints, KZ_MAX_CHECKPOINT_CACHE, empty);
    player.cp_count = 0;
    player.cp_index = 0;
    player.tp_count = 0;
    player.paused_cp_count = 0;
    player.paused_cp_index = 0;
}

void kz_cmd_checkpoint(edict_t* pEntity)
{
    const int id = indexOfEdict(pEntity);
    if (!is_player(id) || !MF_IsPlayerAlive(id))
    {
        return;
    }
    if (!is_on_ground(pEntity) && !is_on_ladder(pEntity))
    {
        // "Checkpoints mid-air are not allowed."
        return;
    }
    if (kz_mpbhop_on_frozen_block(id))
    {
        // "Checkpoints on bhop blocks are not allowed."
        return;
    }

    // TODO: fwd_checkpoint_pre

    kz_player_t& p = g_players[id];
    const Vector& origin = pEntity->v.origin;
    const Vector& angles = pEntity->v.v_angle;

    if (p.timer_paused)
    {
        p.paused_checkpoints[p.paused_cp_index].origin  = origin;
        p.paused_checkpoints[p.paused_cp_index].v_angle = angles;

        p.paused_cp_index = (p.paused_cp_index + 1) % KZ_MAX_CHECKPOINT_CACHE;
        p.paused_cp_count++;
    }
    else
    {
        if (g_api_initialized)
        {
            kz_rp_run_checkpoint(id); // report to kz_global_api for the recording
        }

        p.checkpoints[p.cp_index].origin  = origin;
        p.checkpoints[p.cp_index].v_angle = angles;

        p.cp_index = (p.cp_index + 1) % KZ_MAX_CHECKPOINT_CACHE;
        p.cp_count++;
    }
    // TODO: fwd_checkpoint_post
    return;
}
void kz_cmd_gocheck(edict_t* pEntity)
{
    const int id = indexOfEdict(pEntity);
    if (!is_player(id) || !MF_IsPlayerAlive(id))
    {
        return;
    }

    kz_player_t& p = g_players[id];
    Vector origin(0.0f, 0.0f, 0.0f);
    Vector angles(0.0f, 0.0f, 0.0f);

    if (p.timer_paused)
    {
        int index = p.paused_cp_index - 1;
        if (index < 0)
        {
            index = KZ_MAX_CHECKPOINT_CACHE - 1;
        }
        if (!is_vec_empty(p.paused_checkpoints[index].origin))
        {
            origin = p.paused_checkpoints[index].origin;
            angles = p.paused_checkpoints[index].v_angle;
        }
    }
    else
    {
        int index = p.cp_index - 1;
        if (index < 0)
        {
            index = KZ_MAX_CHECKPOINT_CACHE - 1;
        }
        if (!p.cp_count)
        {
            // "You don't have any Checkpoints."
        }
        else
        {
            origin = p.checkpoints[index].origin;
            angles = p.checkpoints[index].v_angle;
        }
    }
    if (is_vec_empty(origin))
    {
        return;
    }

    // TODO: fwd_gocheck_pre
    kz_teleport_player(pEntity, origin, &angles);
    if (g_api_initialized)
    {
        kz_rp_run_gocheck(id);
    }
    if (!p.timer_paused)
    {
        p.tp_count++;
    }
}
