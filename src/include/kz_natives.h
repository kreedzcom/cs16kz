#ifndef KZ_NATIVES_H
#define KZ_NATIVES_H

typedef struct
{
    char szWR_Pro[128];
    char szWR_Noob[128];
    int map_props[3];
    bool updated;
} map_info_t;

typedef struct { int fwd; std::vector<int> data; } plugin_callback_data;

extern int g_fwd_bot_run_started;
extern int g_fwd_bot_run_finished;
extern map_info_t g_current_map_info;
extern std::map<int64_t, plugin_callback_data> g_plugin_callbacks;

extern void kz_api_add_forwards(void);
extern void kz_api_add_natives(void);
extern void kz_call_map_info_forward(int fwd, const char* map, const char* pro, const char* noob, int* map_props, size_t map_props_size);
#endif 
