#include "krp_codec.h"
#include "krp_start_events.h"

#include <cstring>

static int g_failures = 0;

static void expect(bool cond)
{
    if (!cond)
    {
        ++g_failures;
    }
}

static void expect_error(krp::error got, krp::error expected)
{
    if (got != expected)
    {
        ++g_failures;
    }
}

int run_krp_validate_tests(void)
{
    g_failures = 0;

    expect_error(krp::validate_header(nullptr, 0), krp::error::truncated);

    uint8_t truncated[8] = {0};
    expect_error(krp::validate_header(truncated, sizeof(truncated)), krp::error::truncated);

    krp_header bad_magic = {};
    bad_magic.magic   = 0;
    bad_magic.version = KRP_CURRENT_VERSION;
    expect_error(krp::validate_header(reinterpret_cast<const uint8_t*>(&bad_magic), sizeof(bad_magic)),
        krp::error::bad_magic);

    krp_header bad_version = {};
    bad_version.magic   = KRP_MAGIC;
    bad_version.version = 99;
    expect_error(krp::validate_header(reinterpret_cast<const uint8_t*>(&bad_version), sizeof(bad_version)),
        krp::error::bad_version);

    krp_header valid = {};
    valid.magic   = KRP_MAGIC;
    valid.version = KRP_CURRENT_VERSION;
    expect_error(krp::validate_header(reinterpret_cast<const uint8_t*>(&valid), sizeof(valid)), krp::error::ok);

    KrpStartEvents start;
    expect(!start.has_frame);
    expect(start.defer_until_keyframe(KRP_EVENT_GOCHECK));
    start.reset();
    expect(!start.has_frame && start.deferred.empty());
    expect(start.defer_until_keyframe(KRP_EVENT_CHECKPOINT));
    expect(start.defer_until_keyframe(KRP_EVENT_GOCHECK));

    std::vector<uint8_t> written;
    start.after_keyframe([&](uint8_t event) { written.push_back(event); });
    start.after_keyframe([&](uint8_t event) { written.push_back(event); });
    expect(start.has_frame);
    expect(written == std::vector<uint8_t>{KRP_EVENT_CHECKPOINT, KRP_EVENT_GOCHECK});
    expect(!start.defer_until_keyframe(KRP_EVENT_CHECKPOINT));

    return g_failures;
}
