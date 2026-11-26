#include "krp_codec.h"
#include "krp_start_events.h"
#include "kz_ws_protocol.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <zlib.h>

static void write_fixture(const std::filesystem::path& directory, const char* name, const char* uid, bool nub)
{
    krp_header header{};
    header.magic = KRP_MAGIC;
    header.version = KRP_CURRENT_VERSION;
    std::strcpy(header.player.steamid, "STEAM_0:0:88888");
    std::strcpy(header.map.name, "kz_contract");
    header.run.checkpoints = nub ? 1 : 0;
    header.run.teleports = nub ? 1 : 0;

    std::vector<uint8_t> raw(sizeof(header));
    std::memcpy(raw.data(), &header, sizeof(header));
    KrpStartEvents start;
    if (nub)
    {
        start.defer_until_keyframe(KRP_EVENT_CHECKPOINT);
        start.defer_until_keyframe(KRP_EVENT_GOCHECK);
    }
    raw.push_back(KRP_FRAMETYPE_KEYFRAME);
    raw.insert(raw.end(), sizeof(krp_mask), 0xff);
    raw.insert(raw.end(), sizeof(krp_frame), 0);
    start.after_keyframe([&](uint8_t event) {
        raw.push_back(KRP_FRAMETYPE_EVENT);
        raw.push_back(event);
    });
    raw.push_back(KRP_FRAMETYPE_DELTA);
    raw.push_back(1);
    raw.insert(raw.end(), sizeof(krp_mask) - 1, 0);
    raw.push_back(1);

    std::vector<uint8_t> compressed;
    std::vector<uint8_t> restored;
    if (krp::compress(raw, compressed, 3) != krp::error::ok ||
        krp::decompress(compressed, restored) != krp::error::ok || restored != raw)
    {
        throw std::runtime_error("native replay round trip failed");
    }

    std::ofstream replay(directory / (std::string(name) + ".krpz"), std::ios::binary);
    replay.exceptions(std::ios::failbit | std::ios::badbit);
    replay.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    replay.close();

    constexpr size_t fixture_chunk_bytes = 32;
    const size_t count = (compressed.size() + fixture_chunk_bytes - 1) / fixture_chunk_bytes;
    for (size_t index = 0; index < count; ++index)
    {
        const auto* data = compressed.data() + index * fixture_chunk_bytes;
        const size_t size = std::min(fixture_chunk_bytes, compressed.size() - index * fixture_chunk_bytes);
        ws_uchunk_header chunk{};
        std::strncpy(chunk.local_uid, uid, sizeof(chunk.local_uid) - 1);
        chunk.chunk_checksum = static_cast<int32_t>(crc32(0, data, size));
        chunk.chunk_index = index;
        chunk.chunk_total = count;
        std::ofstream file(directory / (std::string(name) + "." + std::to_string(index) + ".chunk"), std::ios::binary);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.write(reinterpret_cast<const char*>(&chunk), sizeof(chunk));
        file.write(reinterpret_cast<const char*>(data), size);
        file.close();
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 1;
    }
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    write_fixture(directory, "pro", "0_00020000_0088888_contract", false);
    write_fixture(directory, "nub", "1_00025000_0088888_contract", true);
}
