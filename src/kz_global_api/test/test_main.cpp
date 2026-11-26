#include <cstdio>
#include "kz_ws_protocol.h"
#include "kz_run_state.h"
#include "kz_upload_attempts.h"

static_assert(!kz_run_accepts_events(KzRunState::Idle));
static_assert(kz_run_accepts_events(KzRunState::Recording));
static_assert(!kz_run_accepts_events(KzRunState::Paused));

static_assert(!kz_ws_valid_boolean(-1));
static_assert(kz_ws_valid_boolean(0));
static_assert(kz_ws_valid_boolean(1));
static_assert(kz_ws_replay_ack_is_playable(false, -1));
static_assert(kz_ws_replay_ack_is_playable(true, 1));
static_assert(!kz_ws_replay_ack_is_playable(true, 0));
static_assert(kz_ws_matches_ack(WSMsgOut::ADD_RECORD, WSMsgIn::RECORD_ACK));
static_assert(!kz_ws_matches_ack(WSMsgOut::ADD_RECORD, WSMsgIn::FILE_ACK));
static_assert(kz_ws_matches_ack(WSMsgOut::WANT_MAP_INFO, WSMsgIn::MAP_INFO));
static_assert(!kz_ws_matches_ack(WSMsgOut::MAP_CHANGE, WSMsgIn::MAP_INFO));
static_assert(!kz_ws_matches_ack(WSMsgOut::WANT_MAP_INFO, WSMsgIn::GET_REPLAY_ACK));

extern int run_path_validate_tests(void);
extern int run_krp_validate_tests(void);
extern int run_replay_uid_tests(void);

static int test_upload_attempts()
{
    KzUploadAttempts attempts;
    const auto now = std::chrono::steady_clock::time_point{};
    const auto first = attempts.start("run", now);
    if (!first || attempts.start("run", now))
    {
        return 1;
    }
    attempts.sent("run", first, now + std::chrono::seconds(30));
    if (attempts.start("run", now + std::chrono::seconds(29)))
    {
        return 1;
    }
    const auto retry = attempts.start("run", now + std::chrono::seconds(30));
    attempts.release("run", first);
    if (!retry || retry == first || !attempts.is_current("run", retry))
    {
        return 1;
    }
    attempts.release("run");
    if (attempts.is_current("run", retry))
    {
        return 1;
    }
    const auto reconnect = attempts.start("run", now);
    attempts.clear();
    return !reconnect || attempts.is_current("run", reconnect);
}

int main(void)
{
    int failures = 0;
    failures += run_path_validate_tests();
    failures += run_krp_validate_tests();
    failures += run_replay_uid_tests();
    failures += test_upload_attempts();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return 1;
    }

    std::fprintf(stdout, "All tests passed\n");
    return 0;
}
