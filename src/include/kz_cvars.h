#ifndef KZ_CVARS_H
#define KZ_CVARS_H

typedef struct pr_cvar_s // Protected cvar
{
    const char* const name;
    const char* const expected_value;
    mutable cvar_t* ptr;
} pr_cvar_t;

extern cvar_t* kz_api_url;
extern cvar_t* kz_api_token;
extern cvar_t* kz_api_log_send;
extern cvar_t* kz_api_log_recv;
extern cvar_t* kz_api_log_upload;
extern cvar_t* kz_api_replays_clevel;

extern const pr_cvar_t g_server_cvars[];
extern const size_t g_server_cvars_size;

extern const pr_cvar_t g_player_cvars[];
extern const size_t g_player_cvars_size;

extern void kz_run_cvar_checker(void);
extern void kz_qqc_handler(const edict_t* pEdict, int requestId, const char* cvar, const char* value);
#endif
