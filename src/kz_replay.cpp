#include "amxxmodule.h"

#include "pdata.h"
#include "kz_player.h"
#include "kz_ws.h"
#include "kz_util.h"
#include "kz_cvars.h"
#include "kz_replay.h"
#include "kz_storage.h"

#include <condition_variable>
#include <filesystem>

#include "zstd.h"
#include "zstd_errors.h"

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif 

krp_header g_header;
krp_packet g_current_frame[33];
std::atomic<bool> g_krp_running;

kz::queue<log_entry> g_replay_writer_log(64);
kz::queue<log_entry> g_replay_upload_log(64);

kz::queue<krp_packet> g_replay_writer_queue(4096); // (32 players * 100 fps each = 3096 + some additonal room)
kz::queue<ws_upload> g_replay_upload_queue(64);

std::mutex g_replay_writer_mtx;
std::mutex g_replay_upload_mtx;
std::condition_variable g_replay_writer_cv;
std::condition_variable g_replay_upload_cv;

static std::thread g_replay_writer_thread;
static std::thread g_replay_upload_thread;
static void kz_rp_writer_thread(void);
static void kz_rp_upload_thread(void);

#define CHUNK_SIZE (64*1024) // 64KB


int kz_rp_run_started(int id) 
{
    krp_packet item = {};
    item.player_index = id;
    item.type = KRP_SIGNAL_START;
    
    krp_signal* sig = reinterpret_cast<krp_signal*>(item.data);
    sig->ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    snprintf(sig->steamid_short, sizeof(sig->steamid_short), "%s", g_players[id].steamid_short);
    snprintf(sig->nickname, sizeof(sig->nickname), "%s", g_players[id].nickname);

    g_current_frame[id].player_index = id;
    if (!g_replay_writer_queue.try_push(item))
    {
        kz_log(nullptr, "[KRP] The queue is full");
        return 0;
    }
    return 1;
}
int kz_rp_run_checkpoint(int id)
{
    krp_packet item = {};
    item.player_index = id;
    item.type = KRP_SIGNAL_EVENT;

    krp_signal* sig = reinterpret_cast<krp_signal*>(item.data);
    sig->event = KRP_EVENT_TYPE_CHECKPOINT;

    g_current_frame[id].player_index = id;
    if (!g_replay_writer_queue.try_push(item))
    {
        kz_log(nullptr, "[KRP] The queue is full");
        return 0;
    }
    return 1;
}
int kz_rp_run_gocheck(int id)
{
    krp_packet item = {};
    item.player_index = id;
    item.type = KRP_SIGNAL_EVENT;

    krp_signal* sig = reinterpret_cast<krp_signal*>(item.data);
    sig->event = KRP_EVENT_TYPE_GOCHECK;

    g_current_frame[id].player_index = id;
    if (!g_replay_writer_queue.try_push(item))
    {
        kz_log(nullptr, "[KRP] The queue is full");
        return 0;
    }
    return 1;
}
int kz_rp_run_paused(int id)
{
    krp_packet item = {};
    item.player_index = id;
    item.type = KRP_SIGNAL_PAUSE;

    g_current_frame[id].player_index = id;
    if (!g_replay_writer_queue.try_push(item))
    {
        kz_log(nullptr, "[KRP] The queue is full");
        return 0;
    }
    return 1;
}
int kz_rp_run_unpaused(int id)
{
    krp_packet item = {};
    item.player_index = id;
    item.type = KRP_SIGNAL_UNPAUSE;

    krp_signal* sig = reinterpret_cast<krp_signal*>(item.data);
    snprintf(sig->steamid_short, sizeof(sig->steamid_short), "%s", g_players[id].steamid_short);
    snprintf(sig->nickname, sizeof(sig->nickname), "%s", g_players[id].nickname);

    g_current_frame[id].player_index = id;
    if (!g_replay_writer_queue.try_push(item))
    {
        kz_log(nullptr, "[KRP] The queue is full");
        return 0;
    }
    return 1;
}
int kz_rp_run_rejected(int id, bool delete_file)
{
    krp_packet item = {};
    item.player_index = id;
    item.type = KRP_SIGNAL_REJECT;

    krp_signal* sig = reinterpret_cast<krp_signal*>(item.data);
    sig->delete_file = delete_file;

    g_current_frame[id].player_index = id;
    if (!g_replay_writer_queue.try_push(item))
    {
        kz_log(nullptr, "[KRP] The queue is full");
        return 0;
    }
    return 1;
}

int kz_rp_run_finished(int id, float time)
{
    krp_packet item = {};
    item.player_index = id;
    item.type = KRP_SIGNAL_FINISH;

    krp_signal* sig = reinterpret_cast<krp_signal*>(item.data);
    sig->time = time;
    snprintf(sig->steamid_short, sizeof(sig->steamid_short), "%s", g_players[id].steamid_short);
    snprintf(sig->nickname, sizeof(sig->nickname), "%s", g_players[id].nickname);

    g_current_frame[id].player_index = id;
    if (!g_replay_writer_queue.try_push(item))
    {
        kz_log(nullptr, "[KRP] The queue is full");
        return 0;
    }
    return 1;
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void kz_rp_init(void)
{
    kz_log_addq(&g_replay_writer_log);
    kz_log_addq(&g_replay_upload_log);
    g_krp_running.store(true);

    g_replay_writer_thread = std::thread(kz_rp_writer_thread);
    g_replay_upload_thread = std::thread(kz_rp_upload_thread);
}
void kz_rp_update_header(void)
{
    char szIP[16];
    char szPort[16];
    const char* addr = CVAR_GET_STRING("net_address");
    split_net_address(addr, szIP, sizeof(szIP), szPort, sizeof(szPort));

    if (FStrEq(szIP, "0.0.0.0"))
    {
        snprintf(szIP, sizeof(szIP), "%s", CVAR_GET_STRING("ip"));
        snprintf(szPort, sizeof(szPort), "%s", CVAR_GET_STRING("port"));
    }

    g_header.magic          = 0x4B52502146494C45;
    g_header.version        = 0;
    g_header.server_ip      = inet_addr(szIP);
    g_header.server_port    = static_cast<uint16_t>(atoi(szPort));

    g_header.map.checksum = get_map_crc32(STRING(gpGlobals->mapname));
    snprintf(g_header.map.name, sizeof(g_header.map.name), "%s", STRING(gpGlobals->mapname));

    std::filesystem::path dir = g_data_dir / "kz_global" / "replays" / g_header.map.name;
    if (!std::filesystem::exists(dir))
    {
        std::error_code ec;
        if (std::filesystem::create_directories(dir, ec))
        {
            kz_log(nullptr, "Directory created: %s", std::filesystem::relative(dir, g_data_dir).c_str());
        }
        else
        {
            kz_log(nullptr, "Failed to create directory (%s): %s", std::filesystem::relative(dir, g_data_dir).c_str(), ec.message().c_str());
            return;
        }
    }
}
void kz_rp_uninit()
{
    g_krp_running.store(false);

    g_replay_writer_cv.notify_all();
    g_replay_upload_cv.notify_all();

    g_replay_writer_thread.join();
    g_replay_upload_thread.join();
}
void kz_rp_set_cmd(int id, const usercmd_t* cmd)
{
    krp_frame* frame = reinterpret_cast<krp_frame*>(g_current_frame[id].data);
    frame->cmd.lerp_msec        = cmd->lerp_msec;
    frame->cmd.msec             = cmd->msec;

    frame->cmd.viewangles.x     = cmd->viewangles.x;
    frame->cmd.viewangles.y     = cmd->viewangles.y;
    frame->cmd.viewangles.z     = cmd->viewangles.z;

    frame->cmd.forwardmove      = cmd->forwardmove;
    frame->cmd.sidemove         = cmd->sidemove;
    frame->cmd.upmove           = cmd->upmove;
    frame->cmd.lightlevel       = cmd->lightlevel;
    frame->cmd.buttons          = cmd->buttons;
    frame->cmd.impulse          = cmd->impulse;
    frame->cmd.weaponselect     = cmd->weaponselect;

    frame->cmd.impact_index     = cmd->impact_index;

    frame->cmd.impact_position.x  = cmd->impact_position.x;
    frame->cmd.impact_position.y  = cmd->impact_position.y;
    frame->cmd.impact_position.z  = cmd->impact_position.z;
}
void kz_rp_set_vars(int id, const entvars_t* vars)
{
    krp_frame* frame = reinterpret_cast<krp_frame*>(g_current_frame[id].data);

    frame->vars.origin.x      = vars->origin.x;
    frame->vars.origin.y      = vars->origin.y;
    frame->vars.origin.z      = vars->origin.z;

    frame->vars.velocity.x    = vars->velocity.x;
    frame->vars.velocity.y    = vars->velocity.y;
    frame->vars.velocity.z    = vars->velocity.z;

    frame->vars.v_angle.x     = vars->v_angle.x;
    frame->vars.v_angle.y     = vars->v_angle.y;
    frame->vars.v_angle.z     = vars->v_angle.z;

    frame->vars.fixangle      = vars->fixangle;
    frame->vars.movetype      = vars->movetype;
    frame->vars.flags         = vars->flags;
    frame->vars.button        = vars->button;
    frame->vars.oldbuttons    = vars->oldbuttons;
}
void kz_rp_write_frame(int id)
{
    g_current_frame[id].player_index = id;
    g_current_frame[id].type = KRP_SIGNAL_FRAME;

    krp_frame* frame = reinterpret_cast<krp_frame*>(g_current_frame[id].data);
    frame->glb.frametime = gpGlobals->frametime;

    if (!g_replay_writer_queue.try_push(g_current_frame[id]))
    {
        assert(!"This is not supposed to happend.");
    }
    else
    {
        g_replay_writer_cv.notify_one();
    }
}
void kz_rp_compress_and_upload_async(ws_upload upr)
{
    if (!g_replay_upload_queue.try_push(upr))
    {
        kz_log(nullptr, "[KRP] The queue is full");
    }
    else
    {
        g_replay_upload_cv.notify_one();
    }
}
/***************************************************************************************************************/
/***************************************************************************************************************/
static uint64_t kz_rp_timestamp_from_header(FILE* fp)
{
    if (!fp)
    {
        return 0;
    }

    long current_pos = ftell(fp);
    if (current_pos < 0)
    {
        return 0;
    }

    size_t ts_offset = offsetof(krp_header, timestamp);
    uint64_t ts = 0;

    if (fseek(fp, (long)ts_offset, SEEK_SET) == 0)
    {
        if (fread(&ts, sizeof(uint64_t), 1, fp) != 1)
        {
            ts = 0;
        }
    }
    fseek(fp, current_pos, SEEK_SET);
    return ts;
}
std::string kz_rp_mapname_from_header(FILE* fp)
{
    if (!fp)
    {
        return "";
    }

    long current_pos = ftell(fp);
    if (current_pos < 0)
    {
        return "";
    }

    size_t name_offset = offsetof(krp_header, map.name);
    char buffer[64] = {0};

    if (fseek(fp, (long)name_offset, SEEK_SET) != 0 || fread(buffer, sizeof(char), 63, fp) == 0)
    {
        fseek(fp, current_pos, SEEK_SET);
        return "";
    }
    fseek(fp, current_pos, SEEK_SET);

    return std::string(buffer);
}
static void kz_rp_write_teleports_to_header(FILE *fp, uint32_t checkpoints, uint32_t teleports)
{
    if (!fp)
    {
        return;
    }

    long current_pos = ftell(fp);
    if (current_pos < 0)
    {
        return;
    }

    size_t cp_offset = offsetof(krp_header, run.checkpoints);
    size_t tp_offset = offsetof(krp_header, run.teleports);

    if (fseek(fp, (long)cp_offset, SEEK_SET) == 0)
    {
        fwrite(&checkpoints, sizeof(uint32_t), 1, fp);
    }
    if (fseek(fp, (long)tp_offset, SEEK_SET) == 0)
    {
        fwrite(&teleports, sizeof(uint32_t), 1, fp);
    }

    fseek(fp, current_pos, SEEK_SET);
    return;
}
static void kz_rp_teleports_from_header(FILE *fp, uint32_t *checkpoints, uint32_t *teleports)
{
    if (!fp || !checkpoints || !teleports)
    {
        return;
    }

    long current_pos = ftell(fp);
    if (current_pos < 0)
    {
        return;
    }

    size_t cp_offset = offsetof(krp_header, run.checkpoints);
    size_t tp_offset = offsetof(krp_header, run.teleports);
    bool success = false;

    if (fseek(fp, (long)cp_offset, SEEK_SET) == 0 && fread(checkpoints, sizeof(uint32_t), 1, fp) == 1)
    {
        if (fseek(fp, (long)tp_offset, SEEK_SET) == 0 && fread(teleports, sizeof(uint32_t), 1, fp) == 1)
        {
            success = true;
        }
    }
    if (!success)
    {
        *checkpoints = 0;
        *teleports = 0;
    }

    fseek(fp, current_pos, SEEK_SET);
    return;
}
static void kz_rp_write_frametype(FILE* fp, krp_packet* curr, uint8_t frametype)
{
    fwrite(&frametype, sizeof(frametype), 1, fp);
    
    krp_mask mask = {};
    switch (frametype)
    {
        case BIT_FRAMETYPE_EVENT:
        {
            fwrite(&mask, sizeof(mask), 1, fp);
            break;
        }
        case BIT_FRAMETYPE_DELTA:
        {
            break;
        }
        case BIT_FRAMETYPE_KEYFRAME:
        {
            memset(&mask, 0xFF, sizeof(mask));
            fwrite(&mask, sizeof(mask), 1, fp);
        }
    }
}
static void kz_rp_write_event(FILE* fp, krp_packet* curr)
{
    krp_signal* sig = reinterpret_cast<krp_signal*>(curr->data);
    fwrite(&sig->event, sizeof(sig->event), 1, fp);
}
static void kz_rp_write_keyframe(FILE* fp, krp_packet* curr)
{
    krp_frame* frame = reinterpret_cast<krp_frame*>(curr->data);
    fwrite(&frame->cmd, sizeof(frame->cmd), 1, fp);
    fwrite(&frame->vars, sizeof(frame->vars), 1, fp);
    fwrite(&frame->glb, sizeof(frame->glb), 1, fp);
}
static void kz_rp_write_delta(FILE* fp, krp_packet* curr, krp_packet* last)
{
    krp_mask mask = {};
    const size_t size = sizeof(krp_frame);

    uint8_t buffer[size];
    size_t idx = 0;

    uint8_t* pCurr = reinterpret_cast<uint8_t*>(curr->data); // krp_frame
    uint8_t* pLast = reinterpret_cast<uint8_t*>(last->data); // krp_frame

    uint8_t* pMask  = reinterpret_cast<uint8_t*>(&mask);
    uint8_t diff = 0;
    size_t block = 0;
    size_t bit = 0;

    for (size_t i = 0; i < size; ++i)
    {
        diff = pCurr[i] ^ pLast[i];
        if (diff != 0)
        {
            block = (i / 8);
            bit = (i & 7);

            pMask[block] |= (1ULL << bit);
            buffer[idx++] = diff;
        }
    }
    fwrite(&mask, sizeof(mask), 1, fp);
    if (idx > 0)
    {
        fwrite(buffer, idx, 1, fp);
    }
}
static std::vector<uint8_t> kz_rp_reorganize_data(const std::vector<char>& src)
{
    std::vector<uint8_t> frame_type;
    std::vector<uint8_t> frame_data[sizeof(krp_frame)];

    const size_t num_blocks = sizeof(krp_mask) / sizeof(uint64_t);
    std::vector<uint8_t> frame_flags[num_blocks];

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(src.data()) + sizeof(krp_header);
    const uint8_t* end = reinterpret_cast<const uint8_t*>(src.data()) + src.size();

    while (ptr < end)
    {
        uint8_t type = *ptr++;
        frame_type.push_back(type);

        krp_mask mask;
        memcpy(&mask, ptr, sizeof(krp_mask)); ptr += sizeof(krp_mask);

        uint8_t* m_ptr = reinterpret_cast<uint8_t*>(&mask);
        for (size_t block = 0; block < num_blocks; ++block)
        {
            for (size_t i = 0; i < 8; ++i)
            {
                frame_flags[block].push_back(m_ptr[block * 8 + i]);
            }
        }
        if (type == BIT_FRAMETYPE_DELTA || type == BIT_FRAMETYPE_KEYFRAME)
        {
            size_t byte_idx = 0;
            for (size_t i = 0; i < sizeof(krp_mask) && byte_idx < sizeof(krp_frame); ++i)
            {
                for (size_t bit = 0; bit < 8; ++bit)
                {
                    if (m_ptr[i] & (1 << bit))
                    {
                        if (ptr < end)
                        {
                            frame_data[byte_idx].push_back(*ptr++);
                        }
                    }

                    if (++byte_idx >= sizeof(krp_frame))
                    {
                        break;
                    }
                }
            }
        }
        else if (type == BIT_FRAMETYPE_EVENT)
        {
        }
    }

    krp_header header;
    memcpy(&header, src.data(), sizeof(krp_header));
    header.size_types = static_cast<uint32_t>(frame_type.size());
    header.size_flags = 0;
    header.size_data  = 0;

    for (size_t i = 0; i < num_blocks; ++i)
    {
        header.size_flags += static_cast<uint32_t>(frame_flags[i].size());
    }
    for (size_t i = 0; i < sizeof(krp_frame); ++i)
    {
        header.size_data += static_cast<uint32_t>(frame_data[i].size());
    }

    std::vector<uint8_t> result;
    result.reserve(src.size());

    // Result: [header][frametype_1][frametype_N]...[flags_1][flags_N]...[data_1][data_N]
    uint8_t* h_ptr = reinterpret_cast<uint8_t*>(&header);
    result.insert(result.end(), h_ptr, h_ptr + sizeof(header));
    result.insert(result.end(), frame_type.begin(), frame_type.end());

    for (size_t i = 0; i < num_blocks; ++i)
    {
        result.insert(result.end(), frame_flags[i].begin(), frame_flags[i].end());
    }
    for (size_t i = 0; i < sizeof(krp_frame); ++i)
    {
        if (!frame_data[i].empty())
        {
            result.insert(result.end(), frame_data[i].begin(), frame_data[i].end());
        }
    }
    return result;
}
static FILE* kz_rp_compress_file(const char* file)
{
    FILE* fp = fopen(file, "rb");
    if (!fp) 
    {
        return nullptr;
    }

    fseek(fp, 0, SEEK_END);
    size_t src_size = ftell(fp);
    rewind(fp);

    std::vector<char> src_buffer(src_size);
    size_t read_bytes = fread(src_buffer.data(), 1, src_size, fp);
    fclose(fp);

    if (read_bytes != src_size)
    {
        return nullptr;
    }

    size_t max_dst_size = ZSTD_compressBound(src_size);
    std::vector<char> dst_buffer(max_dst_size);
    std::vector<uint8_t> r_buffer = kz_rp_reorganize_data(src_buffer);

    int c_level = static_cast<int>(kz_api_replays_clevel->value); 
    size_t compressed_size = ZSTD_compress(dst_buffer.data(), max_dst_size, r_buffer.data(), r_buffer.size(), c_level);

    if (ZSTD_isError(compressed_size))
    {
        kz_log(&g_replay_upload_log, "[ZSTD] Compression failed: %s", ZSTD_getErrorName(compressed_size));
        return nullptr;
    }

    char compressed_path[256];
    snprintf(compressed_path, sizeof(compressed_path), "%s", file);
    compressed_path[strlen(file) - 1] = 'z'; // .krpr -> .krpz

    FILE* out_fp = fopen(compressed_path, "wb+");
    if (!out_fp)
    {
        return nullptr;
    }

    fwrite(dst_buffer.data(), 1, compressed_size, out_fp);
    rewind(out_fp);
    return out_fp;

}
static void kz_rp_writer_thread(void)
{
    static FILE* s_fd[33];
    static krp_packet s_last[33];
    static size_t s_counter[33];
    static uint32_t s_checkpoints[33];
    static uint32_t s_gochecks[33];

    static char s_filepath[33][255];

    kz_storage_init();
    while (g_krp_running.load() || !g_replay_writer_queue.empty())
    {
        while (krp_packet* s_curr = g_replay_writer_queue.front())
        {
            int id = s_curr->player_index;
            switch (s_curr->type)
            {
                case KRP_SIGNAL_START:
                {

                    if(s_fd[id])
                    {
                        fclose(s_fd[id]);
                        s_fd[id] = nullptr;

                        kz_log(&g_replay_writer_log, "[KRP] run_start: closing active file descriptor for player (%d)", id);
                    }

                    krp_signal* sig     = reinterpret_cast<krp_signal*>(s_curr->data);
                    krp_header header   = g_header;
                    const char* mapname = g_header.map.name;

                    header.timestamp = sig->ts;

                    std::filesystem::path path = g_data_dir / "kz_global" / "replays" / mapname / sig->steamid_short;
                    snprintf(s_filepath[id], sizeof(s_filepath[0]), "%s.tmp", path.string().c_str());
                    snprintf(header.player.name, sizeof(header.player.name), "%s", sig->nickname);
                    snprintf(header.player.steamid, sizeof(header.player.steamid), "STEAM_%c:%c:%s", sig->steamid_short[0], sig->steamid_short[1], sig->steamid_short + 2);

                    s_fd[id] = fopen(s_filepath[id], "wb+");
                    if (s_fd[id])
                    {
                        fwrite(&header, sizeof(header), 1, s_fd[id]);
                    }
                    else
                    {
                        kz_log(&g_replay_writer_log, "[KRP] ERROR: Could not create %s.tmp (%s)", std::filesystem::relative(path, g_data_dir).c_str(), strerror(errno));
                    }
                    memset(&s_last[id], 0, sizeof(s_last[0]));
                    s_counter[id] = 0;
                    s_checkpoints[id] = 0;
                    s_gochecks[id] = 0;
                    break;
                }
                case KRP_SIGNAL_EVENT:
                {
                    if (s_fd[id])
                    {
                        kz_rp_write_frametype(s_fd[id], s_curr, BIT_FRAMETYPE_EVENT);
                        kz_rp_write_event(s_fd[id], s_curr);
                    }
                    {
                        krp_signal* sig = reinterpret_cast<krp_signal*>(s_curr->data);
                        if (sig->event == KRP_EVENT_TYPE_CHECKPOINT)
                            s_checkpoints[id]++;
                        else if (sig->event == KRP_EVENT_TYPE_GOCHECK)
                            s_gochecks[id]++;
                    }
                    break;
                }
                case KRP_SIGNAL_FRAME:
                {
                    if (s_fd[id])
                    {
                        // TODO: write in chunks, less i/o
                        if (!s_counter[id])
                        {
                            // We write a full frame (no delta) when the run just started or got unpaused (savepos)
                            kz_rp_write_frametype(s_fd[id], s_curr, BIT_FRAMETYPE_KEYFRAME);
                            kz_rp_write_keyframe(s_fd[id], s_curr);
                        }
                        else
                        {
                            kz_rp_write_frametype(s_fd[id], s_curr, BIT_FRAMETYPE_DELTA);
                            kz_rp_write_delta(s_fd[id], s_curr, &s_last[id]);
                        }
                        memcpy(&s_last[id], s_curr, sizeof(s_last[0]));
                        s_counter[id]++;
                    }
                    break;
                }
                case KRP_SIGNAL_PAUSE:
                {
                    if (s_fd[id])
                    {
                        kz_rp_write_teleports_to_header(s_fd[id], s_checkpoints[id], s_gochecks[id]);

                        fclose(s_fd[id]);
                        s_fd[id] = nullptr;
                    }
                    else
                    {
                        kz_log(&g_replay_writer_log, "[KRP] run_pause: no file descriptor for player (%d)", id);
                    }
                    break;
                }
                case KRP_SIGNAL_UNPAUSE:
                {
                    krp_signal* sig     = reinterpret_cast<krp_signal*>(s_curr->data);
                    const char* mapname = g_header.map.name;

                    std::filesystem::path path = g_data_dir / "kz_global" / "replays" / mapname / sig->steamid_short;
                    snprintf(s_filepath[id], sizeof(s_filepath[0]), "%s.tmp", path.string().c_str());

                    if (s_fd[id])
                    {
                        fclose(s_fd[id]);
                        s_fd[id] = nullptr;

                        kz_log(&g_replay_writer_log, "[KRP] run_unpause: closing active file descriptor for player (%d)", id);
                    }
                    if (!s_fd[id])
                    {
                        s_fd[id] = fopen(s_filepath[id], "rb+");
                        s_counter[id] = 0;

                        kz_rp_teleports_from_header(s_fd[id], &s_checkpoints[id], &s_gochecks[id]);
                    }
                    if (!s_fd[id])
                    {
                        kz_log(&g_replay_writer_log, "[KRP] ERROR: Could not open %s.tmp (%s)", std::filesystem::relative(path, g_data_dir).c_str(), strerror(errno));
                    }
                    break;
                }
                case KRP_SIGNAL_REJECT:
                {
                    if (s_fd[id])
                    {
                        fclose(s_fd[id]);
                        s_fd[id] = nullptr;

                        krp_signal* sig = reinterpret_cast<krp_signal*>(s_curr->data);
                        if(sig->delete_file)
                        {
                            unlink(s_filepath[id]);
                        }
                    }
                    else
                    {
                        kz_log(&g_replay_writer_log, "[KRP] run_reject: no file descriptor for player (%d)", id);
                    }
                    s_checkpoints[id] = 0;
                    s_gochecks[id] = 0;
                    break;
                }
                case KRP_SIGNAL_FINISH:
                {
                    if (s_fd[id])
                    {
                        char new_path[255];
                        krp_signal* sig     = reinterpret_cast<krp_signal*>(s_curr->data);
                        std::string mapname = kz_rp_mapname_from_header(s_fd[id]);

                        char uid_str[64];
                        char ts_str[16];
                        to_base36(kz_rp_timestamp_from_header(s_fd[id]), ts_str, sizeof(ts_str));
                        snprintf(uid_str, sizeof(uid_str), "%08u_%s_%s", static_cast<uint32_t>(sig->time * 1000.0f), sig->steamid_short, ts_str);

                        std::filesystem::path npath = g_data_dir / "kz_global" / "replays" / uid_str;
                        npath.replace_extension(".krpr");
                        snprintf(new_path, sizeof(new_path), "%s", npath.string().c_str());

                        fseek(s_fd[id], 0, SEEK_SET);

                        krp_header temp_header;
                        if (fread(&temp_header, sizeof(krp_header), 1, s_fd[id]) == 1)
                        {
                            strncpy(temp_header.player.name, sig->nickname, sizeof(temp_header.player.name) - 1);
                            temp_header.player.name[sizeof(temp_header.player.name) - 1] = '\0';

                            fseek(s_fd[id], 0, SEEK_SET);
                            fwrite(&temp_header, sizeof(krp_header), 1, s_fd[id]);
                            fflush(s_fd[id]);
                        }


                        fclose(s_fd[id]);
                        s_fd[id] = nullptr;

                        if (rename(s_filepath[id], new_path) == 0)
                        {
                            kz_log(&g_replay_writer_log, "[KRP] Saved replay: %s", std::filesystem::relative(npath, g_data_dir).c_str());
                        }
                        else
                        {
                            kz_log(&g_replay_writer_log, "[KRP] ERROR: Rename failed (%s)", strerror(errno));
                            break;
                        }

                        JSON_Value* data_val = json_value_init_object();
                        JSON_Object* data_obj = json_value_get_object(data_val);

                        char steamid[35];
                        snprintf(steamid, sizeof(steamid), "STEAM_%c:%c:%s", sig->steamid_short[0], sig->steamid_short[1], sig->steamid_short + 2);

                        json_object_set_string(data_obj, "steamid",     steamid);
                        json_object_set_string(data_obj, "map_name",    mapname.c_str());
                        json_object_set_number(data_obj, "time_ms",     (double)(int64_t)(sig->time * 1000.0f));
                        json_object_set_number(data_obj, "checkpoints", s_checkpoints[id]);
                        json_object_set_number(data_obj, "gochecks",    s_gochecks[id]);
                        json_object_set_string(data_obj, "local_uid",   uid_str);

                        std::string message;
                        uint64_t msg_id = kz_storage_get_next_id(StorageTable::outgoing_queue);
                        kz_ws_build_msg(WSMsgOut::ADD_RECORD, data_val, message, msg_id, &g_replay_writer_log);

                        auto shared_msg = std::make_shared<std::string>(std::move(message));

                        kz_storage_save(shared_msg, WSMsgOut::ADD_RECORD, msg_id, StorageTable::outgoing_queue);
                        kz_ws_send_msg(*shared_msg, msg_id);
                    }
                    else
                    {
                        kz_log(&g_replay_writer_log, "[KRP] run_finish: no file descriptor for player (%d)", id);
                    }
                    break;
                }
            }
            g_replay_writer_queue.pop();
        }
        std::unique_lock<std::mutex> lock(g_replay_writer_mtx);
        g_replay_writer_cv.wait(lock, []{ return (!g_replay_writer_queue.empty() || !g_krp_running.load()); });
    }
    for (int i = 0; i < 33; i++)
    {
        if (s_fd[i])
        {
            fclose(s_fd[i]);
            s_fd[i] = nullptr;
        }
    }
    kz_storage_uninit();
}
static void kz_rp_upload_thread(void)
{
    while (g_krp_running.load() || !g_replay_upload_queue.empty())
    {
        while (ws_upload* item = g_replay_upload_queue.front())
        {
            std::filesystem::path pathname = item->filepath;
            FILE* fp = nullptr;

            if (pathname.extension() == ".krpz")
            {
                fp = fopen(pathname.string().c_str(), "rb");
            }
            else
            {
                fp = kz_rp_compress_file(pathname.string().c_str());
                if (!fp)
                {
                    kz_log(&g_replay_upload_log, "[UPLOAD] Failed to compress file: %s", std::filesystem::relative(pathname, g_data_dir).c_str());
                    g_replay_upload_queue.pop();
                    continue;
                }
            }
            if (!fp)
            {
                kz_log(&g_replay_upload_log, "[UPLOAD] fopen failure: %s", strerror(errno));
                g_replay_upload_queue.pop();
                continue;
            }

            const size_t max_data_per_chunk = (CHUNK_SIZE - sizeof(ws_uchunk_header));
            fseek(fp, 0, SEEK_END);
            uint32_t total_chunks = (ftell(fp) + (max_data_per_chunk - 1)) / max_data_per_chunk;
            rewind(fp);

            if (kz_api_log_upload->value > 0.0f)
            {
                kz_log(&g_replay_upload_log, "[UPLOAD] Starting: (id: %llu) - (%u chunks)", item->id, total_chunks);
            }

            ws_uchunk_header* header = nullptr;

            char buffer[CHUNK_SIZE];
            char* data_ptr = buffer + sizeof(ws_uchunk_header);
            size_t bytes = 0;

            auto last_log_time = std::chrono::steady_clock::now();
            for (uint32_t i = 0; i < total_chunks; ++i)
            {
                bytes = fread(data_ptr, 1, max_data_per_chunk, fp);
                if (bytes > 0)
                {
                    header = reinterpret_cast<ws_uchunk_header*>(buffer);
                    memset(header, 0, sizeof(*header));

                    header->id             = item->id;
                    header->chunk_index    = i;
                    header->chunk_checksum = UTIL_CRC32(data_ptr, bytes);
                    header->chunk_total    = static_cast<uint64_t>(total_chunks);
                    memcpy(header->local_uid, item->local_uid, sizeof(header->local_uid));

                    g_websocket.sendBinary(std::string(buffer, sizeof(*header) + bytes));
                    // Stay under API replay byte rate limit (~5 MiB/s per server).
                    std::this_thread::sleep_for(std::chrono::milliseconds(13));
                }
                if (kz_api_log_upload->value > 0.0f)
                {
                    auto now     = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time).count();
                    if (elapsed > 1 || i >= (total_chunks - 1) || i == 0)
                    {
                        float p = (static_cast<float>(i + 1) / static_cast<float>(total_chunks)) * 100.0f;
                        kz_log(&g_replay_upload_log, "[UPLOAD] (id: %llu): %u/%u chunks (%0.1f%%) complete.", item->id, (i + 1), total_chunks, p);
                        last_log_time = now;
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock(g_active_uploads_mtx);
                g_active_uploads.erase(item->local_uid);
            }
            g_replay_upload_queue.pop();
            fclose(fp);
        }
        {
            std::unique_lock<std::mutex> lock(g_replay_upload_mtx);
            g_replay_upload_cv.wait(lock, []{ return (!g_replay_upload_queue.empty() || !g_krp_running.load()); });
        }
    }
}
