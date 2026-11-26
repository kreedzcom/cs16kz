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

constexpr bool kz_run_can_pause(KzRunState state, bool first_frame_queued)
{
    return state == KzRunState::Recording && first_frame_queued;
}
