#pragma once

#include <cstdint>

enum class KzRunState : uint8_t
{
    Idle,
    Recording,
    Paused,
};

constexpr bool kz_run_accepts_events(KzRunState state)
{
    return state == KzRunState::Recording;
}
