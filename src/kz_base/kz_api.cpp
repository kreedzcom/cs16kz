#include "amxxmodule.h"
#include "kz_api.h"

bool g_api_initialized = false;

// Function pointers into kz_global_api/kz_replay.cpp (kz_rp_run_*).
int (*kz_rp_run_started)(int)         = nullptr;
int (*kz_rp_run_checkpoint)(int)      = nullptr;
int (*kz_rp_run_gocheck)(int)         = nullptr;
int (*kz_rp_run_paused)(int)          = nullptr;
int (*kz_rp_run_unpaused)(int)        = nullptr;
int (*kz_rp_run_rejected)(int, bool)  = nullptr;
int (*kz_rp_run_finished)(int, float, bool, uint8_t) = nullptr;

void kz_api_init()
{
    kz_rp_run_started    = reinterpret_cast<int (*)(int)>       (MF_RequestFunction("kz_rp_run_started"));
    kz_rp_run_checkpoint = reinterpret_cast<int (*)(int)>       (MF_RequestFunction("kz_rp_run_checkpoint"));
    kz_rp_run_gocheck    = reinterpret_cast<int (*)(int)>       (MF_RequestFunction("kz_rp_run_gocheck"));
    kz_rp_run_paused     = reinterpret_cast<int (*)(int)>       (MF_RequestFunction("kz_rp_run_paused"));
    kz_rp_run_unpaused   = reinterpret_cast<int (*)(int)>       (MF_RequestFunction("kz_rp_run_unpaused"));
    kz_rp_run_rejected   = reinterpret_cast<int (*)(int, bool)> (MF_RequestFunction("kz_rp_run_rejected"));
    kz_rp_run_finished   = reinterpret_cast<int (*)(int, float, bool, uint8_t)>(MF_RequestFunction("kz_rp_run_finished"));

    // Only usable when kz_global_api is present and every function resolved.
    g_api_initialized = kz_rp_run_started
                 && kz_rp_run_checkpoint
                 && kz_rp_run_gocheck
                 && kz_rp_run_paused
                 && kz_rp_run_unpaused
                 && kz_rp_run_rejected
                 && kz_rp_run_finished;
}
