#include "krp_codec.h"

#include "zstd.h"
#include "zstd_errors.h"

namespace krp
{

const char* error_str(error e)
{
    switch (e)
    {
        case error::ok:                       return "ok";
        case error::bad_magic:                return "bad_magic";
        case error::bad_version:              return "bad_version";
        case error::truncated:                return "truncated";
        case error::corrupt_sections:         return "corrupt_sections";
        case error::first_frame_not_keyframe: return "first_frame_not_keyframe";
        case error::zstd_error:               return "zstd_error";
    }
    return "unknown";
}

error validate_header(const uint8_t* buf, size_t len)
{
    if (!buf || len < sizeof(krp_header))
    {
        return error::truncated;
    }

    krp_header header;
    memcpy(&header, buf, sizeof(header));

    if (header.magic != KRP_MAGIC)
    {
        return error::bad_magic;
    }
    if (header.version != KRP_CURRENT_VERSION)
    {
        return error::bad_version;
    }
    return error::ok;
}

error map_sections(const uint8_t* body, size_t len, sections& out)
{
    memset(&out, 0, sizeof(out));

    const error herr = validate_header(body, len);
    if (herr != error::ok)
    {
        return herr;
    }

    const krp_header* header = reinterpret_cast<const krp_header*>(body);
    const uint64_t total = static_cast<uint64_t>(sizeof(krp_header)) + header->size_types + header->size_flags + header->size_data  + header->size_events;

    if (total > len)
    {
        return error::truncated;
    }

    if ((header->size_flags % (KRP_MASK_BLOCKS * sizeof(uint64_t))) != 0)
    {
        return error::corrupt_sections;
    }

    out.header = header;
    out.types  = body + sizeof(krp_header);
    out.flags  = out.types + header->size_types;
    out.data   = out.flags + header->size_flags;
    out.events = out.data  + header->size_data;

    const size_t block_stream_size = header->size_flags / KRP_MASK_BLOCKS;
    out.num_frames                 = block_stream_size / sizeof(uint64_t);

    if (header->size_types == 0 || out.types[0] != KRP_FRAMETYPE_KEYFRAME)
    {
        return error::first_frame_not_keyframe;
    }

    size_t rows   = 0;
    size_t events = 0;

    for (size_t t = 0; t < header->size_types; ++t)
    {
        const uint8_t type = out.types[t];

        if (type == KRP_FRAMETYPE_EVENT)
        {
            ++events;
            continue;
        }
        if (type != KRP_FRAMETYPE_DELTA && type != KRP_FRAMETYPE_KEYFRAME)
        {
            return error::corrupt_sections;
        }
        if (rows >= out.num_frames)
        {
            return error::corrupt_sections;
        }

        for (size_t block = 0; block < KRP_MASK_BLOCKS; ++block)
        {
            uint64_t block_val;
            memcpy(&block_val, out.flags + (block * block_stream_size) + (rows * 8), 8);

            for (size_t bit = 0; bit < 64; ++bit)
            {
                const size_t idx = block * 64 + bit;
                if (idx < sizeof(krp_frame) && (block_val & (1ULL << bit)))
                {
                    ++out.col_sizes[idx];
                }
            }
        }
        ++rows;
    }

    if (rows != out.num_frames)
    {
        return error::corrupt_sections;
    }
    if (events != header->size_events)
    {
        return error::corrupt_sections;
    }

    uint64_t total_data = 0;
    for (size_t i = 0; i < sizeof(krp_frame); ++i)
    {
        total_data += out.col_sizes[i];
    }
    if (total_data != header->size_data)
    {
        return error::corrupt_sections;
    }

    return error::ok;
}

static error build_krpz_body(const std::vector<uint8_t>& krpr, std::vector<uint8_t>& out)
{
    std::vector<uint8_t> frame_type;
    std::vector<uint8_t> frame_data[sizeof(krp_frame)];
    std::vector<uint8_t> frame_events;
    std::vector<uint8_t> frame_flags[KRP_MASK_BLOCKS];

    const uint8_t* ptr = krpr.data() + sizeof(krp_header);
    const uint8_t* end = krpr.data() + krpr.size();

    while (ptr < end)
    {
        const uint8_t type = *ptr++;
        frame_type.push_back(type);

        if (type == KRP_FRAMETYPE_DELTA || type == KRP_FRAMETYPE_KEYFRAME)
        {
            if (ptr + sizeof(krp_mask) > end)
            {
                return error::truncated;
            }

            krp_mask mask;
            memcpy(&mask, ptr, sizeof(krp_mask));
            ptr += sizeof(krp_mask);

            const uint8_t* m_ptr = reinterpret_cast<const uint8_t*>(&mask);
            for (size_t block = 0; block < KRP_MASK_BLOCKS; ++block)
            {
                for (size_t i = 0; i < 8; ++i)
                {
                    frame_flags[block].push_back(m_ptr[block * 8 + i]);
                }
            }

            size_t byte_idx = 0;
            for (size_t i = 0; i < sizeof(krp_mask) && byte_idx < sizeof(krp_frame); ++i)
            {
                for (size_t bit = 0; bit < 8; ++bit)
                {
                    if (m_ptr[i] & (1u << bit))
                    {
                        if (ptr >= end)
                        {
                            return error::truncated;
                        }
                        frame_data[byte_idx].push_back(*ptr++);
                    }

                    if (++byte_idx >= sizeof(krp_frame))
                    {
                        break;
                    }
                }
            }
        }
        else if (type == KRP_FRAMETYPE_EVENT)
        {
            if (ptr >= end)
            {
                return error::truncated;
            }
            frame_events.push_back(*ptr++);
        }
        else
        {
            return error::corrupt_sections;
        }
    }

    krp_header header;
    memcpy(&header, krpr.data(), sizeof(krp_header));
    header.size_types  = static_cast<uint32_t>(frame_type.size());
    header.size_flags  = 0;
    header.size_data   = 0;
    header.size_events = static_cast<uint32_t>(frame_events.size());

    for (size_t i = 0; i < KRP_MASK_BLOCKS; ++i)
    {
        header.size_flags += static_cast<uint32_t>(frame_flags[i].size());
    }
    for (size_t i = 0; i < sizeof(krp_frame); ++i)
    {
        header.size_data += static_cast<uint32_t>(frame_data[i].size());
    }

    out.clear();
    out.reserve(krpr.size());

    const uint8_t* h_ptr = reinterpret_cast<const uint8_t*>(&header);
    out.insert(out.end(), h_ptr, h_ptr + sizeof(header));
    out.insert(out.end(), frame_type.begin(), frame_type.end());

    for (size_t i = 0; i < KRP_MASK_BLOCKS; ++i)
    {
        out.insert(out.end(), frame_flags[i].begin(), frame_flags[i].end());
    }
    for (size_t i = 0; i < sizeof(krp_frame); ++i)
    {
        if (!frame_data[i].empty())
        {
            out.insert(out.end(), frame_data[i].begin(), frame_data[i].end());
        }
    }
    out.insert(out.end(), frame_events.begin(), frame_events.end());

    return error::ok;
}
static error build_krpr(const sections& s, std::vector<uint8_t>& out)
{
    const krp_header& header       = *s.header;
    const size_t block_stream_size = header.size_flags / KRP_MASK_BLOCKS;

    const uint8_t* col[sizeof(krp_frame)];
    {
        const uint8_t* cursor = s.data;
        for (size_t i = 0; i < sizeof(krp_frame); ++i)
        {
            col[i] = cursor;
            cursor += s.col_sizes[i];
        }
    }

    krp_header out_header = header;
    out_header.size_types  = 0;
    out_header.size_flags  = 0;
    out_header.size_data   = 0;
    out_header.size_events = 0;

    out.clear();
    out.reserve(sizeof(krp_header)
              + header.size_types * (1 + sizeof(krp_mask))
              + header.size_data + header.size_events);

    const uint8_t* h_ptr = reinterpret_cast<const uint8_t*>(&out_header);
    out.insert(out.end(), h_ptr, h_ptr + sizeof(out_header));

    const uint8_t* ev = s.events;
    size_t row = 0;

    for (size_t t = 0; t < header.size_types; ++t)
    {
        const uint8_t type = s.types[t];
        out.push_back(type);

        if (type == KRP_FRAMETYPE_EVENT)
        {
            out.push_back(*ev++);
            continue;
        }

        uint8_t mask[sizeof(krp_mask)];
        for (size_t block = 0; block < KRP_MASK_BLOCKS; ++block)
        {
            memcpy(&mask[block * 8], s.flags + (block * block_stream_size) + (row * 8), 8);
        }
        out.insert(out.end(), mask, mask + sizeof(mask));

        for (size_t i = 0; i < sizeof(krp_frame); ++i)
        {
            if (mask[i / 8] & (1u << (i & 7)))
            {
                out.push_back(*col[i]++);
            }
        }
        ++row;
    }

    return error::ok;
}

error zstd_decompress(const uint8_t* src, size_t len, std::vector<uint8_t>& out)
{
    if (!src || len == 0)
    {
        return error::truncated;
    }

    const unsigned long long content_size = ZSTD_getFrameContentSize(src, len);
    if (content_size == ZSTD_CONTENTSIZE_ERROR || content_size == ZSTD_CONTENTSIZE_UNKNOWN)
    {
        return error::zstd_error;
    }

    out.resize(static_cast<size_t>(content_size));

    const size_t d_size = ZSTD_decompress(out.data(), out.size(), src, len);
    if (ZSTD_isError(d_size))
    {
        out.clear();
        return error::zstd_error;
    }
    out.resize(d_size);

    return error::ok;
}

error compress(const std::vector<uint8_t>& krpr, std::vector<uint8_t>& out, int level)
{
    const error herr = validate_header(krpr.data(), krpr.size());
    if (herr != error::ok)
    {
        return herr;
    }

    std::vector<uint8_t> columnar;
    const error rerr = build_krpz_body(krpr, columnar);
    if (rerr != error::ok)
    {
        return rerr;
    }

    const size_t max_dst_size = ZSTD_compressBound(columnar.size());
    out.resize(max_dst_size);

    const size_t compressed_size = ZSTD_compress(out.data(), max_dst_size, columnar.data(), columnar.size(), level);

    if (ZSTD_isError(compressed_size))
    {
        out.clear();
        return error::zstd_error;
    }
    out.resize(compressed_size);

    return error::ok;
}

error decompress(const std::vector<uint8_t>& krpz, std::vector<uint8_t>& out)
{
    std::vector<uint8_t> body;
    const error zerr = zstd_decompress(krpz.data(), krpz.size(), body);
    if (zerr != error::ok)
    {
        return zerr;
    }

    sections s;
    const error merr = map_sections(body.data(), body.size(), s);
    if (merr != error::ok)
    {
        return merr;
    }

    return build_krpr(s, out);
}

} /* namespace krp */
