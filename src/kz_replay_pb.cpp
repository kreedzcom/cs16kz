#include "amxxmodule.h"
#include "engine_strucs.h"

#include "pdata.h"
#include "kz_player.h"
#include "kz_ws.h"
#include "kz_util.h"
#include "kz_cvars.h"
#include "kz_replay.h"
#include "kz_storage.h"
#include "kz_natives.h"

#include "krp_codec.h"

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif 

#define DELAY_FRAMES 100

static krp_playback  g_current_playback;
krp_playback* g_pb_bot_data;
static edict_t* g_pb_bot_ent;
static string_t g_pb_classname;
static int g_pb_bot_id;

static std::mutex g_pb_parse_mtx;
static std::mutex g_pb_data_mtx;
static std::thread g_pb_parse_thread;
static std::condition_variable g_pb_parse_cv;
static std::atomic<bool> g_pb_running;
static std::atomic<bool> g_pb_pending_data;

static kz::queue<log_entry> g_pb_parse_log(64);
static kz::queue<std::filesystem::path> g_pb_parse_queue(4);
static std::vector<krp_playback> g_pb_data;

static void kz_bot_create(const char* nickname);
static void kz_bot_think(edict_t* pent);
static void kz_bot_footsteps(edict_t* pent);

static void kz_pb_parser_thread(void);
static void parse_playback(krp_playback& out, const std::vector<uint8_t>& src, const std::filesystem::path& file);
static void print_header(const krp_header& header);
static std::string get_timestamp_string(uint64_t ts_ms, bool use_utc);
std::filesystem::path kz_pb_find_fastest(const char* mapname);

extern float g_wait_after_load;

void kz_pb_init(void)
{
    kz_log_addq(&g_pb_parse_log);

    g_pb_running.store(true);
    g_pb_parse_thread = std::thread(kz_pb_parser_thread);
}
void kz_pb_uninit(void)
{
    g_pb_running.store(false);
    g_pb_parse_cv.notify_all();
    g_pb_parse_thread.join();
}
void kz_pb_frame(void)
{
    if (g_wait_after_load > 0.0f && g_wait_after_load < 2.0f)
    {
        g_wait_after_load = (gpGlobals->time + 2.5f);
    }
    if (g_wait_after_load > 1.0f && (gpGlobals->time - g_wait_after_load > 2.5f))
    {
        g_pb_bot_ent = CREATE_NAMED_ENTITY(ALLOC_STRING("info_target"));        
        g_pb_bot_ent->v.classname = g_pb_classname;

        g_wait_after_load = 0.0f;

        std::filesystem::path file = kz_pb_find_fastest(STRING(gpGlobals->mapname));
        if (!file.empty())
        {
            kz_pb_parse_file_async(file);
        }
        else
        {
            g_pb_bot_data = nullptr;
        }
    }
    if (g_pb_bot_ent == nullptr || FNullEnt(g_pb_bot_ent))
    {
        return;
    }
    if (!g_pb_pending_data.load())
    {
      return;
    }
    {

        std::lock_guard<std::mutex> lock(g_pb_data_mtx);
        g_pb_pending_data.store(false);

        if (!g_pb_data.empty())
        {
            const auto& entry = g_pb_data.back();
            if (FStrEq(STRING(gpGlobals->mapname), entry.header.map.name))
            {
                g_current_playback = std::move(g_pb_data.back());
                g_pb_bot_data = &g_current_playback;

                g_pb_bot_data->use_cmd = static_cast<bool>(kz_api_bot_use_cmd->value);
                g_pb_bot_data->double_speed = false;

                g_pb_bot_data->use_count = 0;
                g_pb_bot_data->plr_sound = 0;
                g_pb_bot_data->step_left = 0;
                g_pb_bot_data->team      = static_cast<int>(kz_api_bot_team->value);

                g_pb_bot_data->delay_counter = 0;
                g_pb_bot_data->frame_counter = 0;
                g_pb_bot_data->finish_frame  = g_pb_bot_data->frames.size() - 1;

                char szNickname[32];
                snprintf(szNickname, sizeof(szNickname), "%s[%s] %s", kz_api_bot_prefix->string, g_pb_bot_data->timer_str, g_pb_bot_data->header.player.name);

                if (g_pb_bot_id)
                {
                    g_pb_bot_ent->v.nextthink = gpGlobals->time + 0.01f;
                    ENTITY_SET_KEYVALUE(edictByIndex(g_pb_bot_id), "name", szNickname);
                }
                else
                {
                    kz_bot_create(szNickname);
                }
            }
            g_pb_data.clear();
        }
    }
}
void kz_pb_server_deactivate_post(void)
{
    g_pb_bot_id = 0;
    g_pb_bot_ent = nullptr;

    g_current_playback.frames.clear();
    g_current_playback.frames.shrink_to_fit();

    std::lock_guard<std::mutex> lock(g_pb_data_mtx);
    g_pb_data.clear();
}
void kz_pb_spawn(edict_t* pent)
{
    if (FClassnameIs(pent, "worldspawn"))
    {
        g_pb_classname = ALLOC_STRING("kz_playback");

        PRECACHE_SOUND("player/pl_step1.wav");
        PRECACHE_SOUND("player/pl_step2.wav");
        PRECACHE_SOUND("player/pl_step3.wav");
        PRECACHE_SOUND("player/pl_step4.wav");
    }
}
void kz_pb_think(edict_t* pent)
{
    if (!g_pb_bot_id || pent->v.classname != g_pb_classname)
    {
        return;
    }

    assert(indexOfEdict(pent) == indexOfEdict(g_pb_bot_ent));
    if (!MF_IsPlayerBot(g_pb_bot_id))
    {
        kz_log(nullptr, "[Playback] Entity %d is not a bot", g_pb_bot_id);
        
        g_pb_bot_id = 0;
        return;
    }
    kz_bot_think(edictByIndex(g_pb_bot_id));
    pent->v.nextthink = gpGlobals->time + 0.01f;
}
void kz_pb_addtofullpack(entity_state_s* state, int e, edict_t* ent, edict_t* host, int hostflags, int player, unsigned char* pset)
{
    if (!player || !g_pb_bot_id)
    {
        return;
    }
    if (g_pb_bot_id != static_cast<int>(indexOfEdict(ent)) || g_pb_bot_id == static_cast<int>(indexOfEdict(host)))
    {
        return;
    }
    if (!g_pb_bot_data || g_pb_bot_data->frames.empty())
    {
        return;
    }

    int32_t index = g_pb_bot_data->frame_counter;

    state->origin.x = g_pb_bot_data->frames[index].origin.x;
    state->origin.y = g_pb_bot_data->frames[index].origin.y;
    state->origin.z = g_pb_bot_data->frames[index].origin.z;

    state->angles.x = g_pb_bot_data->frames[index].v_angle.x;
    state->angles.y = g_pb_bot_data->frames[index].v_angle.y;
    state->angles.z = g_pb_bot_data->frames[index].v_angle.z;

}

int kz_pb_check_visibility(const edict_t* pEntity, unsigned char* pset)
{
    if (static_cast<int>(indexOfEdict(pEntity)) == g_pb_bot_id)
    {
        RETURN_META_VALUE(MRES_SUPERCEDE, 1);
    }
    return -1;
}
std::filesystem::path kz_pb_find_fastest(const char* mapname)
{
    std::filesystem::path map_dir = g_data_dir / "kz_global" / "replays" / mapname;

    if (!std::filesystem::exists(map_dir) || !std::filesystem::is_directory(map_dir))
    {
        return {};
    }

    std::filesystem::path best_file;
    std::string best_filename = "";

    for (const auto& entry : std::filesystem::directory_iterator(map_dir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (entry.path().extension() == ".krpz")
        {
            std::string current_filename = entry.path().filename().string();
            if (best_filename.empty() || current_filename < best_filename)
            {
                best_filename = current_filename;
                best_file = entry.path();
            }
        }
    }
    return best_file;
}
/***************************************************************************************************************/
/***************************************************************************************************************/
static void kz_bot_create(const char* nickname)
{
    edict_t* pEntity = g_engfuncs.pfnCreateFakeClient(nickname);
    if (FNullEnt(pEntity))
    {
        kz_log(nullptr, "[Playback] Failed to create fake client.");
        return;
    }

    ENTITY_SET_KEYVALUE(pEntity, "model", (g_pb_bot_data->team == 1 ? "guerilla" : "urban"));
    ENTITY_SET_KEYVALUE(pEntity, "rate", "3500");
    ENTITY_SET_KEYVALUE(pEntity, "cl_updaterate", "30");
    ENTITY_SET_KEYVALUE(pEntity, "cl_lw", "0");
    ENTITY_SET_KEYVALUE(pEntity, "cl_lc", "0");
    ENTITY_SET_KEYVALUE(pEntity, "tracker", "0");
    ENTITY_SET_KEYVALUE(pEntity, "cl_dlmax", "128");
    ENTITY_SET_KEYVALUE(pEntity, "lefthand", "1");
    ENTITY_SET_KEYVALUE(pEntity, "friends", "0");
    ENTITY_SET_KEYVALUE(pEntity, "dm", "0");
    ENTITY_SET_KEYVALUE(pEntity, "ah", "1");

    ENTITY_SET_KEYVALUE(pEntity, "*bot", "1");
    ENTITY_SET_KEYVALUE(pEntity, "_cl_autowepswitch", "1");
    ENTITY_SET_KEYVALUE(pEntity, "_vgui_menu", "0");
    ENTITY_SET_KEYVALUE(pEntity, "_vgui_menus", "0");

    char szReject[128];
    if (!MDLL_ClientConnect(pEntity, STRING(ALLOC_STRING(nickname)), STRING(ALLOC_STRING("127.0.0.1")), szReject))
    {
        kz_log(nullptr, "[Playback] Connection refused. Reason: %s", szReject);
        REMOVE_ENTITY(pEntity);
        return;
    }

    MDLL_ClientPutInServer(pEntity);
    if (!MF_IsPlayerIngame(indexOfEdict(pEntity)))
    {
        kz_log(nullptr, "[Playback] MDLL_ClientPutInServer() failed.");
        return;
    }

    pEntity->v.flags = (pEntity->v.flags | FL_FAKECLIENT);
    pEntity->v.spawnflags = (pEntity->v.spawnflags | FL_FAKECLIENT);

    fm_set_user_team(pEntity, g_pb_bot_data->team);
    fm_cs_user_spawn(pEntity);
    fm_give_item(pEntity, "weapon_knife");
    fm_give_item(pEntity, "weapon_usp");

    pEntity->v.takedamage = DAMAGE_NO;
    pEntity->v.framerate = 1.0f;

    g_pb_bot_id = indexOfEdict(pEntity);
    g_pb_bot_data->frame_counter = 0;
    g_pb_bot_data->delay_counter = 0;

    g_pb_bot_ent->v.nextthink = gpGlobals->time + 0.01f;
}
static void kz_bot_think(edict_t* pent)
{
    if (!g_pb_bot_data || g_pb_bot_data->frames.size() < 2)
    {
        return;
    }

    int32_t index       = g_pb_bot_data->frame_counter;
    int32_t next_index  = (index + 1 < static_cast<int32_t>(g_pb_bot_data->frames.size())) ? (index + 1) : index;

    int32_t flags       = g_pb_bot_data->frames[index].flags;
    int32_t button      = g_pb_bot_data->frames[index].button;
    int32_t oldbuttons  = g_pb_bot_data->frames[index].oldbuttons;
    krp_v3f     origin  = g_pb_bot_data->frames[index].origin;
    krp_v3f     v_angle = g_pb_bot_data->frames[index].v_angle;
            
    bool bGround  = (flags & FL_ONGROUND);
    bool bJump    = (button & IN_JUMP);
    bool bDuck    = (button & IN_DUCK);
    bool bDucking = bDuck;

    float old_x = (origin.x);
    float old_y = (origin.y);

    origin  = g_pb_bot_data->frames[next_index].origin;
    v_angle = g_pb_bot_data->frames[next_index].v_angle;
    float sqr_speed = (origin.x - old_x) * (origin.x - old_x) + (origin.y - old_y) * (origin.y - old_y);

    pent->v.velocity.x = (origin.x - old_x) * 100.0f;
    pent->v.velocity.y = (origin.y - old_y) * 100.0f;
    pent->v.velocity.z = 0.0f;

    pent->v.origin.x = origin.x;
    pent->v.origin.y = origin.y;
    pent->v.origin.z = origin.z;

    pent->v.v_angle.x = v_angle.x;
    pent->v.v_angle.y = v_angle.y;
    pent->v.v_angle.z = v_angle.z;

    pent->v.angles.x = (v_angle.x / -3.0f);
    pent->v.angles.y = v_angle.y;
    pent->v.angles.z = v_angle.z;
    pent->v.fixangle = 1;

    pent->v.movetype = MOVETYPE_NONE; // prevent lj stats
    pent->v.solid    = SOLID_NOT;

    //MF_PrintSrvConsole("(%d/%d) -> origin(%.1f, %.1f, %.1f) | v_angle(%.1f, %.1f, %.1f) | button(%d) || oldbuttons(%d) || flags(%d)\n", index, g_pb_bot_data->finish_frame, origin.x, origin.y, origin.z, v_angle.x, v_angle.y, v_angle.z, button, oldbuttons, flags);

    if (g_pb_bot_data->frame_counter == 0 && g_pb_bot_data->delay_counter == (DELAY_FRAMES - 1))
    {
        if (g_fwd_bot_run_started > 0)
        {
            MF_ExecuteForward(g_fwd_bot_run_started, static_cast<int32_t>(indexOfEdict(pent)));
        }
    }
    if (g_pb_bot_data->frame_counter == (g_pb_bot_data->finish_frame - 1) && g_pb_bot_data->delay_counter == 0)
    {
        if (g_fwd_bot_run_finished > 0)
        {
            MF_ExecuteForward(g_fwd_bot_run_finished, static_cast<int32_t>(indexOfEdict(pent)));
        }
    }
    if (g_pb_bot_data->use_cmd) // executes the +use command if enabled
    {
        if ((g_pb_bot_data->frame_counter == 0 && g_pb_bot_data->delay_counter == (DELAY_FRAMES - 1)) || (g_pb_bot_data->frame_counter == (g_pb_bot_data->finish_frame - 1) && g_pb_bot_data->delay_counter == 0))
        {
            g_pb_bot_data->use_count = 2;
        }
        if (g_pb_bot_data->use_count)
        {
            // floatround() amxx
            REAL fA = (REAL)(gpGlobals->frametime * 1000.0f);
            fA = (REAL)(floor((double)fA + .5));

            byte msec = static_cast<byte>(fA);
            g_engfuncs.pfnRunPlayerMove(pent, pent->v.v_angle, 0.0f, 0.0f, 0.0f, button, 0, msec);
            g_pb_bot_data->use_count--;
        }
        else
        {
            pent->v.button = button;
        }
    }
    else
    {
        pent->v.button = (button | ~IN_USE);
    }

    static TraceResult Tr;
    Vector dest = Vector(origin.x, origin.y, origin.z - 18.0f);
    g_engfuncs.pfnTraceHull((float*)(&origin), dest, 0, 3 /*HULL_HEAD*/, pent, &Tr);


    // 1 = idle        || 2 = duck || 3 = walk  || 4 = run
    // 5 = duck + walk || 6 = jump || 7 = 6?    || 8 = swim

    pent->v.sequence = 19;
    if (Tr.flFraction < (Tr.vecPlaneNormal.z - 0.01f))
    {
        bDucking = true;
    }
    if (bGround)
    {
        if (bJump)
        {
            pent->v.gaitsequence = 6;
        }
        else if (bDucking)
        {
            pent->v.gaitsequence = (sqr_speed > 0.0f ? 5 : 2);
        }
        else if (sqr_speed > (1.35f * 1.35f))
        {
            pent->v.gaitsequence = 4;
        }
        else
        {
            pent->v.gaitsequence = (sqr_speed > 0.0f ? 3 : 1);
        }
    }
    else
    {
        pent->v.gaitsequence = (bDuck ? 2 : 6);
    }

    if (bGround && sqr_speed > (1.5f * 1.5f))
    {
        if (bJump && (oldbuttons & IN_JUMP))
        {
            g_pb_bot_data->plr_sound = 0;
        }
        kz_bot_footsteps(pent);
    }

    g_pb_bot_data->plr_sound -= 10;

    if (g_pb_bot_data->delay_counter == 0 && (g_pb_bot_data->frame_counter == 0 || g_pb_bot_data->frame_counter == (g_pb_bot_data->finish_frame - 1)))
    {
        g_pb_bot_data->delay_counter++;
    }
    if (g_pb_bot_data->delay_counter)
    {
        g_pb_bot_data->delay_counter++;
        if (g_pb_bot_data->delay_counter >= DELAY_FRAMES)
        {
            g_pb_bot_data->delay_counter = 0;
        }
    }
    
    g_pb_bot_data->frame_counter += (g_pb_bot_data->double_speed ? 2 : 1);
    if (g_pb_bot_data->frame_counter >= g_pb_bot_data->finish_frame)
    {
        g_pb_bot_data->frame_counter = 0;
    }
}
static void kz_bot_footsteps(edict_t* pent)
{
    if (g_pb_bot_data->plr_sound <= 0)
    {
        g_pb_bot_data->step_left = !g_pb_bot_data->step_left;
        int32_t r = RANDOM_LONG(0, 1) + (g_pb_bot_data->step_left * 2);

        g_pb_bot_data->plr_sound = 300;
        switch(r)
        {
            case 0:
            {
                g_engfuncs.pfnEmitSound(pent, CHAN_BODY, "player/pl_step1.wav", VOL_NORM, ATTN_NORM, 0, PITCH_NORM);
                break;
            }
            case 1:
            {
                g_engfuncs.pfnEmitSound(pent, CHAN_BODY, "player/pl_step3.wav", VOL_NORM, ATTN_NORM, 0, PITCH_NORM);
                break;
            }
            case 2:
            {
                g_engfuncs.pfnEmitSound(pent, CHAN_BODY, "player/pl_step2.wav", VOL_NORM, ATTN_NORM, 0, PITCH_NORM);
                break;
            }
            case 3:
            {
                g_engfuncs.pfnEmitSound(pent, CHAN_BODY, "player/pl_step4.wav", VOL_NORM, ATTN_NORM, 0, PITCH_NORM);
                break;
            }
        }
    }
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void kz_pb_parse_file_async(std::filesystem::path file)
{
    if (!g_pb_parse_queue.try_push(std::move(file)))
    {
        kz_log(nullptr, "[PARSE] The queue is full"); // impossible, but wtv
    }
    else
    {
        g_pb_parse_cv.notify_one();
    }
}
void kz_pb_parser_thread(void)
{
    while (g_pb_running.load() || !g_pb_parse_queue.empty())
    {
        std::filesystem::path* file = nullptr;
        while ((file = g_pb_parse_queue.front()) != nullptr)
        {
            krp_playback pb_data = {};
            size_t rsize = 0;

            FILE* fp = fopen(file->string().c_str(), "rb");
            if (fp)
            {
                fseek(fp, 0, SEEK_END);
                long size = ftell(fp);
                if (size)
                {
                    std::vector<uint8_t> buffer(static_cast<size_t>(size));
                    std::string str_ext = file->extension().string();
                    const char* ext = str_ext.c_str();

                    rewind(fp);
                    fread(buffer.data(), 1, size, fp);
                    
                    if (strcmp(ext, ".krpz") == 0)
                    {
                        parse_playback(pb_data, buffer, *file);

                        pb_data.filepath = std::filesystem::path(*file);
                        std::string filename = file->filename().string();
                        if (filename.size() < 8)
                        {
                            kz_log(&g_pb_parse_log, "[PARSE] Filename too short to extract time: %s", filename.c_str());
                            g_pb_parse_queue.pop();
                            continue;
                        }
                        std::string ms_str = filename.substr(7, 8);
                        uint32_t ms_total = 0;
                        try
                        {
                            ms_total = static_cast<uint32_t>(std::stoul(ms_str));
                        }
                        catch (const std::exception& e)
                        {
                            kz_log(&g_pb_parse_log, "[PARSE] Failed to parse time from filename '%s': %s", filename.c_str(), e.what());
                            g_pb_parse_queue.pop();
                            continue;
                        }

                        int minutes = (ms_total / 60000);
                        int seconds = (ms_total % 60000) / 1000;
                        int centiseconds = (ms_total % 1000) / 10;

                        rsize = pb_data.frames.size();
                        pb_data.time_s = static_cast<float>(ms_total) / 1000.0f;
                        snprintf(pb_data.timer_str, sizeof(pb_data.timer_str), "%02d:%02d.%02d", minutes, seconds, centiseconds);
                    }
                    else if (kz_api_log_parse->value > 0.0f)
                    {
                        kz_log(&g_pb_parse_log, "[PARSE] Unknown input ext: %s", std::filesystem::relative(*file, g_data_dir).c_str());
                    }
                }
                fclose(fp);
            }
            if (rsize > 1)
            {
                std::lock_guard<std::mutex> lock(g_pb_data_mtx);
                kz_log(&g_pb_parse_log, "[PARSE] Loaded %zu frames from replay: %s/%s", pb_data.frames.size(), file->parent_path().filename().c_str(), file->filename().c_str());

                g_pb_data.push_back(std::move(pb_data));
                g_pb_data.shrink_to_fit();

                g_pb_pending_data.store(true);
            }
            else
            {
                kz_log(&g_pb_parse_log, "[PARSE] Failed to parse replay: %s", std::filesystem::relative(*file, g_data_dir).c_str());
            }
            g_pb_parse_queue.pop();
        }
        {
            std::unique_lock<std::mutex> lock(g_pb_parse_mtx);
            g_pb_parse_cv.wait(lock, []{ return (!g_pb_parse_queue.empty() || !g_pb_running.load()); });
        }
    }
}
/***************************************************************************************************************/
/***************************************************************************************************************/
static void parse_playback(krp_playback& out, const std::vector<uint8_t>& src, const std::filesystem::path& file)
{
    std::vector<uint8_t> d_buffer;
    krp::error err = krp::zstd_decompress(src.data(), src.size(), d_buffer);
    if (err != krp::error::ok)
    {
        if (kz_api_log_parse->value > 0.0f)
        {
            kz_log(&g_pb_parse_log, "[PARSE] Decompression failed (%s): %s", krp::error_str(err), std::filesystem::relative(file, g_data_dir).c_str());
        }
        return;
    }

    if (kz_api_log_parse->value > 0.0f)
    {
        std::string c_bytes = format_bytes(static_cast<uint64_t>(src.size()));
        std::string d_bytes = format_bytes(static_cast<uint64_t>(d_buffer.size()));

        kz_log(&g_pb_parse_log, "---------------------------------------------------------");
        kz_log(&g_pb_parse_log, "[PARSE] File: %s", std::filesystem::relative(file, g_data_dir).c_str());
        kz_log(&g_pb_parse_log, "[PARSE] File size: (compressed: %s, decompressed: %s)", c_bytes.c_str(), d_bytes.c_str());
        kz_log(&g_pb_parse_log, "---------------------------------------------------------");
    }

    krp::sections s;
    err = krp::map_sections(d_buffer.data(), d_buffer.size(), s);
    if (err != krp::error::ok)
    {
        if (kz_api_log_parse->value > 0.0f)
        {

            krp_header header = {};
            if (d_buffer.size() >= sizeof(krp_header))
            {
                memcpy(&header, d_buffer.data(), sizeof(header));
            }
            switch (err)
            {
                case krp::error::bad_magic:
                {
                    kz_log(&g_pb_parse_log, "[PARSE] Bad magic -> expected (0x%llX), got (0x%llX)", static_cast<unsigned long long>(KRP_MAGIC), static_cast<unsigned long long>(header.magic));
                    break;
                }
                case krp::error::bad_version:
                {
                    kz_log(&g_pb_parse_log, "[PARSE] Unknown replay format version: v%llu", static_cast<unsigned long long>(header.version));
                    break;
                }
                default: kz_log(&g_pb_parse_log, "[PARSE] Critical: corrupt replay (%s): %s", krp::error_str(err), std::filesystem::relative(file, g_data_dir).c_str());
            }
        }
        return;
    }
    if (kz_api_log_parse->value > 0.0f)
    {
        print_header(*s.header);
    }

    out.header = *s.header;
    out.frames.clear();
    out.frames.reserve(s.num_frames);

    krp::for_each_record(s,
        [&out](size_t, uint8_t, const krp_frame& f)
        {
            out.frames.push_back({
                    f.vars.origin,
                    f.vars.v_angle,
                    f.vars.flags,
                    f.vars.button,
                    f.vars.oldbuttons,
                    });
        },
        [](size_t, uint8_t) {});
}
static void print_header(const krp_header& header)
{
    struct in_addr ip_addr; 
    ip_addr.s_addr = header.server_ip;

    char ip_str[INET_ADDRSTRLEN]; 
    inet_ntop(AF_INET, &ip_addr, ip_str, INET_ADDRSTRLEN);

    kz_log(&g_pb_parse_log, "[PARSE] Header -> magic:             0x%llX",      static_cast<unsigned long long>(header.magic));
    kz_log(&g_pb_parse_log, "[PARSE] Header -> version:           %llu",        static_cast<unsigned long long>(header.version));
    kz_log(&g_pb_parse_log, "---------------------------------------------------------");
    kz_log(&g_pb_parse_log, "[PARSE] Header -> player -> name:    %s",          header.player.name);
    kz_log(&g_pb_parse_log, "[PARSE] Header -> player -> steamid: %s",          header.player.steamid);
    kz_log(&g_pb_parse_log, "---------------------------------------------------------");
    kz_log(&g_pb_parse_log, "[PARSE] Header -> map -> name:       %s",          header.map.name);
    kz_log(&g_pb_parse_log, "[PARSE] Header -> map -> checksum:   %u",          header.map.checksum);
    kz_log(&g_pb_parse_log, "---------------------------------------------------------");
    kz_log(&g_pb_parse_log, "[PARSE] Header -> timestamp:         %s",          get_timestamp_string(header.timestamp, false).c_str());
    kz_log(&g_pb_parse_log, "[PARSE] Header -> server_ip:         %s",          ip_str);
    kz_log(&g_pb_parse_log, "[PARSE] Header -> server_port:       %u",          header.server_port);
    kz_log(&g_pb_parse_log, "---------------------------------------------------------");
    kz_log(&g_pb_parse_log, "[PARSE] Header -> size_types:        %u",          header.size_types);
    kz_log(&g_pb_parse_log, "[PARSE] Header -> size_flags:        %u",          header.size_flags);
    kz_log(&g_pb_parse_log, "[PARSE] Header -> size_data:         %u",          header.size_data);
    kz_log(&g_pb_parse_log, "[PARSE] Header -> size_events:       %u",          header.size_events);
    kz_log(&g_pb_parse_log, "---------------------------------------------------------");
}
static std::string get_timestamp_string(uint64_t ts_ms, bool use_utc)
{
    std::chrono::milliseconds ms_since_epoch(ts_ms);
    std::chrono::time_point<std::chrono::system_clock> tp(ms_since_epoch);

    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;

    std::tm tm;
    if (use_utc)
    {
        tm = *std::gmtime(&t);
    }
    else
    {
        tm = *std::localtime(&t);
    }

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}
