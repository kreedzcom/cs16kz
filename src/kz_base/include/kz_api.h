#ifndef KZ_API_H
#define KZ_API_H

#include <cstdint>

// Runtime bridge into the kz_global_api module (records / replays).
//
// The kz_rp_run_* pointers are resolved via MF_RequestFunction() against the functions
// kz_global_api exports; they are non-null only when that module is loaded and has registered them.
// g_initialized is true when every one resolved -- always check it before calling through a pointer.

extern bool g_api_initialized;

extern int (*kz_rp_run_started)(int id);
extern int (*kz_rp_run_checkpoint)(int id);
extern int (*kz_rp_run_gocheck)(int id);
extern int (*kz_rp_run_paused)(int id);
extern int (*kz_rp_run_unpaused)(int id);
extern int (*kz_rp_run_rejected)(int id, bool delete_file);
extern int (*kz_rp_run_finished)(int id, float time, bool submit_run, uint8_t char_prefix);

// Resolve the kz_global_api functions. Call once after all modules load (OnPluginsLoaded).
void kz_api_init();

#endif // KZ_API_H
