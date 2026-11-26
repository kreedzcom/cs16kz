#pragma once

#include <cstdint>

// Outbound (plugin → API) message types
namespace WSMsgOut {
    constexpr int HELLO          = 1;
    constexpr int MAP_CHANGE     = 2;
    constexpr int PLAYER_JOIN    = 3;
    constexpr int PLAYER_LEAVE   = 4;
    constexpr int WANT_MAP_INFO  = 5;
    constexpr int ADD_RECORD     = 8;
    constexpr int GET_REPLAY     = 9;
}

// Inbound (API → plugin) message types
namespace WSMsgIn {
    constexpr int HELLO_ACK      = 101;
    constexpr int MAP_INFO       = 102;
    constexpr int COURSE_TOP     = 103;
    constexpr int PLAYER_RECORDS = 104;
    constexpr int RECORD_ACK     = 105;
    constexpr int FILE_ACK       = 106;
    constexpr int WR_BROADCAST   = 107;
    constexpr int GET_REPLAY_ACK = 108;
    constexpr int DEL_RECORD_NOTIFY = 109;
    constexpr int ERROR_MSG      = 199;
}

// Parson returns -1 for missing and non-boolean values.
constexpr bool kz_ws_valid_boolean(int value)
{
    return value == 0 || value == 1;
}

constexpr bool kz_ws_matches_ack(int64_t request, int response)
{
    if (response == WSMsgIn::ERROR_MSG)
    {
        return true;
    }
    switch (request)
    {
        case WSMsgOut::WANT_MAP_INFO: return response == WSMsgIn::MAP_INFO;
        case WSMsgOut::PLAYER_JOIN:  return response == WSMsgOut::PLAYER_JOIN;
        case WSMsgOut::ADD_RECORD:   return response == WSMsgIn::RECORD_ACK;
        case WSMsgOut::GET_REPLAY:   return response == WSMsgIn::GET_REPLAY_ACK;
        default: return false;
    }
}

#pragma pack(push, 1)
typedef struct
{
    char        local_uid[64];
    uint64_t    id;
    int32_t     chunk_checksum;
    uint64_t    chunk_index;
    uint64_t    chunk_total;
} ws_uchunk_header;
#pragma pack(pop)
static_assert(sizeof(ws_uchunk_header) == 92);
