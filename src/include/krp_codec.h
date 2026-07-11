#ifndef KRP_CODEC_H
#define KRP_CODEC_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

#include "krp_format.h"

namespace krp
{

enum class error
{
    ok = 0,
    bad_magic,
    bad_version,
    truncated,
    corrupt_sections,
    first_frame_not_keyframe,
    zstd_error
};

const char* error_str(error e);

/* Validated view into a decompressed columnar body; aliases the caller's buffer. */
struct sections
{
    const krp_header* header;

    const uint8_t* types;
    const uint8_t* flags;
    const uint8_t* data;
    const uint8_t* events;

    size_t num_frames;
    size_t col_sizes[sizeof(krp_frame)];
};

extern error validate_header(const uint8_t* buf, size_t len);
extern error map_sections(const uint8_t* body, size_t len, sections& out);

extern error compress(const std::vector<uint8_t>& krpr, std::vector<uint8_t>& out, int level);
extern error decompress(const std::vector<uint8_t>& krpz, std::vector<uint8_t>& out);
extern error zstd_decompress(const uint8_t* src, size_t len, std::vector<uint8_t>& out);

/* Reconstructed-frame walk over a map_sections()'d body.
 * on_frame(size_t idx, uint8_t type, const krp_frame&), on_event(size_t idx, uint8_t event). */
template <typename FrameFn, typename EventFn>
error for_each_record(const sections& s, FrameFn&& on_frame, EventFn&& on_event)
{
    const size_t block_stream_size = s.header->size_flags / KRP_MASK_BLOCKS;

    const uint8_t* col[sizeof(krp_frame)];
    {
        const uint8_t* cursor = s.data;
        for (size_t i = 0; i < sizeof(krp_frame); ++i)
        {
            col[i] = cursor;
            cursor += s.col_sizes[i];
        }
    }

    uint8_t frame_bytes[sizeof(krp_frame)] = {0};
    const uint8_t* ev = s.events;
    size_t row = 0;

    for (size_t t = 0; t < s.header->size_types; ++t)
    {
        const uint8_t type = s.types[t];

        if (type == KRP_FRAMETYPE_EVENT)
        {
            on_event(t, *ev++);
            continue;
        }

        for (size_t block = 0; block < KRP_MASK_BLOCKS; ++block)
        {
            uint64_t block_val;
            memcpy(&block_val, s.flags + (block * block_stream_size) + (row * 8), 8);

            for (size_t bit = 0; bit < 64; ++bit)
            {
                if (!(block_val & (1ULL << bit)))
                {
                    continue;
                }
                const size_t idx = block * 64 + bit;
                if (idx >= sizeof(krp_frame))
                {
                    continue;
                }

                if (type == KRP_FRAMETYPE_KEYFRAME)
                {
                    frame_bytes[idx] = *col[idx]++;
                }
                else
                {
                    frame_bytes[idx] ^= *col[idx]++;
                }
            }
        }
        ++row;

        krp_frame frame;
        memcpy(&frame, frame_bytes, sizeof(frame));
        on_frame(t, type, frame);
    }
    return error::ok;
}

} /* namespace krp */

#endif /* KRP_CODEC_H */
