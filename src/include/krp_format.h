/* ============================================================================
 * krp_format.h  --  KRP replay format standard (standalone)
 * ----------------------------------------------------------------------------
 * Self-contained definition of the on-disk KRP replay format used by the
 * KZ Global API module. Depends only on <stdint.h>. Include this to read or
 * write .krpr / .krpz files from any tool without pulling in the game module.
 *
 * This is the single canonical definition of the format. The prose companion
 * doc lives at docs/replay-format.md.
 *
 * This header describes the WIRE FORMAT only. Runtime/in-memory helpers from
 * the module (krp_packet, krp_signal, krp_playback, and the writer/parser
 * function declarations) are intentionally omitted -- they are not part of the
 * file format.
 *
 * Conventions
 *   - All fields are little-endian.
 *   - All on-disk structs are byte-packed (#pragma pack(1)); no padding.
 *   - char[] string fields are NUL-terminated and fixed-width (snprintf-style,
 *     truncated to fit).
 *   - krp_header.reserved[] is zero-filled on write; readers must ignore it.
 *
 * Versioning
 *   - magic and version sit at fixed offsets 0 and 8 in every version.
 *   - Readers must validate magic == KRP_MAGIC and reject unknown versions.
 *   - version 1 uses a 256-byte header (this file). version 0 was a 189-byte
 *     header with no reserved block; it is not produced anymore and current
 *     readers reject it.
 *
 * Physical representations
 *   .krpr  Raw capture, row-major. Header, then a flat sequence of records,
 *          each prefixed by a 1-byte frame type:
 *            EVENT    : 1 byte event id
 *            KEYFRAME : 16-byte all-0xFF mask + full 112-byte krp_frame
 *            DELTA    : 16-byte krp_mask + only the changed (XOR-diff) bytes
 *          The four krp_header.size_* fields are 0 in this form.
 *
 *   .krpz  Final artifact, column-major + zstd-compressed. The DECOMPRESSED
 *          body is laid out as four contiguous sections whose lengths are the
 *          header's size_* fields:
 *            [ header ][ frame-types ][ mask/flags ][ column data ][ events ]
 *          - frame-types : one byte per record (size_types bytes)
 *          - mask/flags  : per-frame masks split into KRP_MASK_BLOCKS blocks;
 *                          for each block, every frame's 8 mask-bytes are
 *                          stored contiguously (block 0 stream, then block 1).
 *          - column data : diff/keyframe bytes grouped by byte-position within
 *                          krp_frame (column 0 for all frames, then column 1...).
 *                          Columns that never change occupy zero bytes.
 *          - events      : concatenated event ids (present but not required
 *                          for playback).
 *
 * Delta encoding
 *   Deltas operate on the raw bytes of krp_frame, not on fields. For each byte
 *   i, diff = cur[i] ^ prev[i]; if non-zero, mask bit i is set (block i/8, bit
 *   i%8) and the diff byte is emitted. Decode: keyframe bytes are copied,
 *   delta bytes are re-applied by XOR against the reconstructed prior frame.
 *   The first frame of every run (and the first after an unpause) is a keyframe.
 * ============================================================================ */

#ifndef KRP_FORMAT_H
#define KRP_FORMAT_H

#include <stdint.h>

/* ---- portable compile-time assert ---------------------------------------- */
#if defined(__cplusplus)
    #define KRP_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define KRP_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
    #define KRP_STATIC_ASSERT2(cond, line) \
        typedef char krp_static_assert_##line[(cond) ? 1 : -1]
    #define KRP_STATIC_ASSERT1(cond, line) KRP_STATIC_ASSERT2(cond, line)
    #define KRP_STATIC_ASSERT(cond, msg)   KRP_STATIC_ASSERT1(cond, __LINE__)
#endif

/* ---- constants ----------------------------------------------------------- */

/* Magic: ASCII "KRP!FILE" (big-endian spelling of the 64-bit value). */
#define KRP_MAGIC            0x4B52502146494C45ULL

/* Bump whenever anything that changes on-disk interpretation changes: the
 * header struct, krp_frame / krp_usercmd / krp_entvars / krp_glbvars, the
 * delta/mask scheme, or the columnar section order. */
#define KRP_CURRENT_VERSION  1

/* File extensions. */
#define KRP_EXT_RAW          ".krpr"   /* row-major, uncompressed          */
#define KRP_EXT_COMPRESSED   ".krpz"   /* column-major, zstd-compressed    */

/* Frame types. Stored on disk as a single leading byte per record. */
enum
{
    KRP_FRAMETYPE_EVENT    = 0,  /* body: 1-byte event id                    */
    KRP_FRAMETYPE_DELTA    = 1,  /* body: mask + changed bytes               */
    KRP_FRAMETYPE_KEYFRAME = 2   /* body: all-ones mask + full krp_frame     */
};

/* Event ids (payload of an EVENT record). */
enum
{
    KRP_EVENT_CHECKPOINT = 0,
    KRP_EVENT_GOCHECK    = 1
};

/* ---- packed on-disk structures ------------------------------------------- */
#pragma pack(push, 1)

/* 3D float vector (x, y, z). */
typedef struct
{
    float x;
    float y;
    float z;
} krp_v3f;

/* Delta bitmask: one bit per byte of krp_frame. */
typedef struct
{
    uint64_t m1;
    uint64_t m2;
} krp_mask;

#define KRP_MASK_BLOCKS  (sizeof(krp_mask) / sizeof(uint64_t))  /* = 2 */

/* Player input for one tick (48 bytes). */
typedef struct
{
    int16_t  lerp_msec;
    uint8_t  msec;
    krp_v3f  viewangles;

    float    forwardmove;
    float    sidemove;
    float    upmove;
    uint8_t  lightlevel;
    uint16_t buttons;
    uint8_t  impulse;
    uint8_t  weaponselect;

    int32_t  impact_index;
    krp_v3f  impact_position;
} krp_usercmd;

/* Entity state for one tick (60 bytes). */
typedef struct
{
    krp_v3f origin;
    krp_v3f velocity;
    krp_v3f v_angle;
    int32_t fixangle;
    int32_t movetype;
    int32_t flags;
    int32_t button;
    int32_t oldbuttons;
    float   fuser2;
} krp_entvars;

/* Global state for one tick (4 bytes). */
typedef struct
{
    float frametime;
} krp_glbvars;

/* One captured tick (112 bytes). Unit of delta-encoding. */
typedef struct
{
    krp_usercmd cmd;   /* 48 */
    krp_entvars vars;  /* 60 */
    krp_glbvars glb;   /*  4 */
} krp_frame;

/* Fixed 256-byte file header (189 bytes of fields + 67 reserved). Present at
 * the start of both .krpr and .krpz.
 *
 * NOTE: keep field offsets byte-stable. Reordering or resizing an existing
 * field invalidates every replay; grow only by carving named fields out of
 * reserved[] (which keeps the 256-byte size and defaults them to 0). Any change
 * that alters interpretation must bump KRP_CURRENT_VERSION. */
typedef struct
{
    uint64_t magic;      /* KRP_MAGIC                                        */
    uint64_t version;    /* KRP_CURRENT_VERSION                             */

    struct { char name[32]; char steamid[35]; }         player;  /* STEAM_X:Y:Z */
    struct { char name[64]; uint32_t checksum; }        map;     /* CRC32 of BSP */
    struct { uint32_t checkpoints; uint32_t teleports; } run;

    uint64_t timestamp;   /* run start, ms since Unix epoch                  */
    uint32_t server_ip;   /* inet_addr form                                  */
    uint16_t server_port;

    /* Columnar section lengths -- meaningful in .krpz only; 0 in raw .krpr. */
    uint32_t size_types;
    uint32_t size_flags;
    uint32_t size_data;
    uint32_t size_events;

    uint8_t  reserved[67];  /* zero-filled; pads header to 256 bytes.
                             * carve future additive fields from here. */
} krp_header;

#pragma pack(pop)

/* ---- size contract (the machine-checkable part of the spec) -------------- */
KRP_STATIC_ASSERT(sizeof(krp_v3f)     == 12,  "krp_v3f must be 12 bytes");
KRP_STATIC_ASSERT(sizeof(krp_mask)    == 16,  "krp_mask must be 16 bytes");
KRP_STATIC_ASSERT(sizeof(krp_usercmd) == 48,  "krp_usercmd must be 48 bytes");
KRP_STATIC_ASSERT(sizeof(krp_entvars) == 60,  "krp_entvars must be 60 bytes");
KRP_STATIC_ASSERT(sizeof(krp_glbvars) == 4,   "krp_glbvars must be 4 bytes");
KRP_STATIC_ASSERT(sizeof(krp_frame)   == 112, "krp_frame must be 112 bytes");
KRP_STATIC_ASSERT(sizeof(krp_header)  == 256, "krp_header must be 256 bytes");

/* The delta mask must have at least one bit per byte of krp_frame. */
KRP_STATIC_ASSERT(sizeof(krp_mask) * 8 >= sizeof(krp_frame),
                  "krp_mask must cover every byte of krp_frame");

#endif /* KRP_FORMAT_H */