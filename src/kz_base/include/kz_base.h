#ifndef KZ_BASE_H
#define KZ_BASE_H

constexpr int KZ_MAX_PLAYERS            = 32;
constexpr int KZ_MAX_CHECKPOINT_CACHE   = 31;

typedef struct
{
    Vector origin;
    Vector v_angle;
} kz_checkpoint_t;

typedef struct
{
    bool            has_custom_startpos;
    kz_checkpoint_t custom_startpos;

    kz_checkpoint_t checkpoints[KZ_MAX_CHECKPOINT_CACHE];
    int             cp_count;
    int             cp_index;
    int             tp_count;

    kz_checkpoint_t paused_checkpoints[KZ_MAX_CHECKPOINT_CACHE];
    int             paused_cp_count;
    int             paused_cp_index;

    bool            timer_running;
    bool            timer_paused;
    float           start_time;       // gpGlobals->time when the run started 
    float           paused_at;        // gpGlobals->time when the timer was paused
    float           paused_total;     // accumulated paused seconds
    float           finish_time;      // elapsed time recorded on finish
} kz_player_t;

extern kz_player_t g_players[KZ_MAX_PLAYERS + 1];

extern void kz_base_reset_checkpoints(int id);
extern void kz_cmd_checkpoint(edict_t* pEntity);
extern void kz_cmd_gocheck(edict_t* pEntity);
#endif
