#include <cstdio>
#include "kz_ws_protocol.h"

static_assert(kz_ws_matches_ack(WSMsgOut::ADD_RECORD, WSMsgIn::RECORD_ACK));
static_assert(!kz_ws_matches_ack(WSMsgOut::ADD_RECORD, WSMsgIn::FILE_ACK));
static_assert(kz_ws_matches_ack(WSMsgOut::WANT_MAP_INFO, WSMsgIn::MAP_INFO));
static_assert(!kz_ws_matches_ack(WSMsgOut::WANT_MAP_INFO, WSMsgIn::GET_REPLAY_ACK));

extern int run_path_validate_tests(void);
extern int run_krp_validate_tests(void);
extern int run_replay_uid_tests(void);

int main(void)
{
    int failures = 0;
    failures += run_path_validate_tests();
    failures += run_krp_validate_tests();
    failures += run_replay_uid_tests();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return 1;
    }

    std::fprintf(stdout, "All tests passed\n");
    return 0;
}
