#ifndef KZ_REPLAY_H
#define KZ_REPLAY_H

#include <filesystem>
#include "usercmd.h"
#include "krp_format.h"

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

typedef struct {
    uint8_t type;
    uint8_t player_index;

    uint8_t flags[(sizeof(krp_frame) + 7) / 8];
    uint8_t data[sizeof(krp_frame) > sizeof(krp_signal) ? sizeof(krp_frame) : sizeof(krp_signal)];
} krp_packet;

#pragma pack(pop)

// Smaller ver. of krp_frame->vars
typedef struct
{
    krp_v3f origin;
    krp_v3f v_angle;
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

extern krp_header kz_rp_get_header(void);
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
