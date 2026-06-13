#ifndef KZ_REPLAY_H
#define KZ_REPLAY_H

#ifndef USERCMD_H
#include "usercmd.h"
#endif

#include <filesystem>

enum : uint8_t
{
    BIT_FRAMETYPE_EVENT,
    BIT_FRAMETYPE_DELTA,
    BIT_FRAMETYPE_KEYFRAME,
};

enum : uint8_t
{
    KRP_EVENT_TYPE_CHECKPOINT,
    KRP_EVENT_TYPE_GOCHECK,
};

enum : uint8_t
{
    KRP_SIGNAL_FRAME,
    KRP_SIGNAL_EVENT,
    KRP_SIGNAL_START,
    KRP_SIGNAL_PAUSE,
    KRP_SIGNAL_UNPAUSE,
    KRP_SIGNAL_REJECT,
    KRP_SIGNAL_FINISH,
};

typedef struct
{
    float x;
    float y;
    float z;
} v3f;

#pragma pack(push, 1)
typedef struct
{
    uint64_t m1;
    uint64_t m2;
} krp_mask;

typedef struct
{
    v3f origin;
    v3f velocity;
    v3f v_angle;
    int32_t fixangle;
    int32_t movetype;
    int32_t flags;
    int32_t button;
    int32_t oldbuttons;
    float fuser2;
} krp_entvars;

typedef struct  {
    char steamid_short[35];
    char nickname[32];
    union {
        bool delete_file;
        float time;
        uint64_t ts;
        uint8_t event;
    };
} krp_signal;

typedef struct
{
    int16_t lerp_msec;
    uint8_t msec;
    v3f viewangles;

    float forwardmove;
    float sidemove;
    float upmove;
    uint8_t lightlevel;
    uint16_t buttons;
    uint8_t impulse;
    uint8_t weaponselect;

    int32_t impact_index;
    v3f impact_position;
} krp_usercmd;

typedef struct
{
    float frametime;
} krp_glbvars;

typedef struct  {
    krp_usercmd cmd;
    krp_entvars vars;
    krp_glbvars glb;
} krp_frame;

static_assert(sizeof(krp_mask) * 8 >= sizeof(krp_frame), "krp_mask needs to be >= than krp_frame");

typedef struct {
    uint8_t type;
    uint8_t player_index;

    uint8_t flags[(sizeof(krp_frame) + 7) / 8];
    uint8_t data[sizeof(krp_frame) > sizeof(krp_signal) ? sizeof(krp_frame) : sizeof(krp_signal)];
} krp_packet;

typedef struct
{
    uint64_t    magic;
    uint64_t    version;

    struct { char name[32]; char steamid[35]; }          player;
    struct { char name[64]; uint32_t checksum; }         map;
    struct { uint32_t checkpoints; uint32_t teleports; } run;

    uint64_t    timestamp;
    uint32_t    server_ip;
    uint16_t    server_port;

    uint32_t    size_types;
    uint32_t    size_flags;
    uint32_t    size_data;
} krp_header;

#pragma pack(pop)

// Smaller ver. of krp_frame->vars
typedef struct
{
    v3f origin;
    v3f v_angle;
    int32_t flags;
    int32_t button;
    int32_t oldbuttons;
} krp_playback_frame;

typedef struct
{
    uint8_t frame_type;
    uint8_t frame_mask[sizeof(krp_mask)];
    uint8_t data[sizeof(krp_frame)];
} krp_playback_entry;

// Actual playback data (in-memory)
typedef struct
{
    bool use_cmd;
    bool double_speed;

    int32_t use_count;
    int32_t plr_sound;
    int32_t step_left;
    int32_t team;

    int32_t frame_counter;
    int32_t finish_frame;
    int32_t delay_counter;

    float time_s;
    char timer_str[32];
    std::filesystem::path filepath;

    krp_header header;
    std::vector<krp_playback_frame> frames;
} krp_playback;

extern krp_header    g_header;
extern krp_playback* g_pb_bot_data;

extern int kz_rp_run_started(int id);
extern int kz_rp_run_checkpoint(int id);
extern int kz_rp_run_gocheck(int id);
extern int kz_rp_run_paused(int id);
extern int kz_rp_run_unpaused(int id);
extern int kz_rp_run_rejected(int id, bool delete_file);
extern int kz_rp_run_finished(int id, float time);

extern void kz_rp_init(void);
extern void kz_rp_uninit(void);
extern void kz_rp_update_header(void);
extern void kz_rp_set_cmd(int id, const usercmd_t* cmd);
extern void kz_rp_set_vars(int id, const entvars_t* vars);
extern void kz_rp_compress_and_upload_async(ws_upload upr);
extern void kz_rp_write_frame(int id);

extern void kz_pb_init(void);
extern void kz_pb_uninit(void);
extern void kz_pb_frame(void);
extern void kz_pb_server_deactivate_post(void);
extern void kz_pb_spawn(edict_t* pent);
extern void kz_pb_think(edict_t* pent);
extern void kz_pb_addtofullpack(entity_state_s* state, int e, edict_t* ent, edict_t* host, int hostflags, int player, unsigned char* pset);
extern int kz_pb_check_visibility(const edict_t* pEntity, unsigned char* pset);
extern void kz_pb_parse_file_async(std::filesystem::path file);
extern std::string kz_rp_mapname_from_header(FILE* fp);
extern std::filesystem::path kz_pb_find_fastest(const char* mapname);
#endif
