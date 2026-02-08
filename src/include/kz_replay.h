#ifndef KZ_REPLAY_H
#define KZ_REPLAY_H

#ifndef USERCMD_H
#include "usercmd.h"
#endif

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

#pragma pack(push, 1)
typedef struct
{
    uint64_t m1;
    uint64_t m2;
} krp_mask;

typedef struct
{
    vec3_t origin;
    vec3_t velocity;
    vec3_t v_angle;
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
    vec3_t viewangles;

    float forwardmove;
    float sidemove;
    float upmove;
    uint8_t lightlevel;
    uint16_t buttons;
    uint8_t impulse;
    uint8_t weaponselect;

    int32_t impact_index;
    vec3_t impact_position;
} krp_usercmd;

typedef struct  {
    krp_usercmd cmd;
    krp_entvars vars;
} krp_frame;

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

    struct { char name[32]; char steamid[35]; }   player;
    struct { char name[64]; uint32_t checksum; }  map;

    uint64_t    timestamp;
    uint32_t    server_ip;
    uint16_t    server_port;

    uint32_t    size_types;
    uint32_t    size_flags;
    uint32_t    size_data;
} krp_header;

typedef struct
{
    char        filepath[255];
    char        local_uid[32];
    uint64_t    rec_id;
} ws_upload_replay;

typedef struct
{
    char        local_uid[32];
    uint64_t    rec_id;
    int32_t     chunk_checksum;
    uint64_t    chunk_index;
} ws_upload_chunk_header;
#pragma pack(pop)

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
extern void kz_rp_compress_and_upload_async(ws_upload_replay upr);
extern void kz_rp_write_frame(int id);
#endif
