# KRP replay format

This document describes the **KRP** replay format used by the KZ Global API module to capture, store, upload, and play back Kreedz runs. It is the prose companion to `src/include/krp_format.h`, the single canonical definition of the on-disk layout; the format is produced/consumed by `src/kz_replay.cpp` (writer/compressor) and `src/kz_replay_pb.cpp` (parser/playback).

The format has two physical representations:

| Extension | Stage | Layout | Compression |
|-----------|-------|--------|-------------|
| `.krpr` | Raw capture written live during a run | **Row-major** — a sequential stream of frame records | None |
| `.krpz` | Final artifact for storage/upload/playback | **Column-major** — streams regrouped by field | zstd |

A `.tmp` file is used while a run is in progress; on finish it is renamed to `.krpr`. The `.krpr` is later reorganized and zstd-compressed into `.krpz`, which is the only form the playback parser reads.

All multi-byte fields are little-endian (native x86). Every on-disk struct is `#pragma pack(push, 1)` — **no padding**.

---

## 1. File header (`krp_header`)

Both `.krpr` and `.krpz` begin with a fixed **256-byte** header (189 bytes of fields + a 67-byte reserved pad). It is written once at run start and patched in place afterwards (nickname, checkpoint/teleport counts).

| Offset | Field | Type | Size | Notes |
|-------:|-------|------|-----:|-------|
| 0 | `magic` | `uint64` | 8 | `0x4B52502146494C45` = ASCII `"KRP!FILE"` |
| 8 | `version` | `uint64` | 8 | `KRP_CURRENT_VERSION`, currently `1` |
| 16 | `player.name` | `char[32]` | 32 | Nickname (patched on finish) |
| 48 | `player.steamid` | `char[35]` | 35 | `STEAM_X:Y:Z` |
| 83 | `map.name` | `char[64]` | 64 | Map name |
| 147 | `map.checksum` | `uint32` | 4 | CRC32 of the map (BSP) |
| 151 | `run.checkpoints` | `uint32` | 4 | Checkpoint count (patched into the header on pause and finish) |
| 155 | `run.teleports` | `uint32` | 4 | Gocheck/teleport count |
| 159 | `timestamp` | `uint64` | 8 | Run start, ms since Unix epoch |
| 167 | `server_ip` | `uint32` | 4 | `inet_addr` form |
| 171 | `server_port` | `uint16` | 2 | |
| 173 | `size_types` | `uint32` | 4 | **`.krpz` only** — length of the frame-types stream |
| 177 | `size_flags` | `uint32` | 4 | **`.krpz` only** — total length of the mask/flags streams |
| 181 | `size_data` | `uint32` | 4 | **`.krpz` only** — total length of the column data streams |
| 185 | `size_events` | `uint32` | 4 | **`.krpz` only** — length of the events stream |
| 189 | `reserved` | `uint8[67]` | 67 | Zero-filled; pads the header to 256 bytes (body begins at `0x100`) |

The four `size_*` fields are `0` in a raw `.krpr`; they are filled in during reorganization and describe the section lengths of the columnar `.krpz` body.

**Versioning.** `magic` and `version` sit at fixed offsets 0 and 8 in every version. On read, the parser validates `magic` and dispatches on `version`; unknown versions are rejected. Version `1` is the current 256-byte layout. Version `0` was an older 189-byte header with **no** `reserved` block — it is no longer produced and the current parser rejects it (older replays are re-captured, not migrated). The `reserved` block exists so future additive fields can be carved out of it without shifting existing offsets or changing the header size.

---

## 2. Frame payload (`krp_frame`)

Each captured tick is a **112-byte** `krp_frame`, composed of three sub-structs. This is the unit that delta-encoding operates on, byte by byte.

### 2.1 `krp_usercmd` (48 bytes) — player input

| Field | Type | Size |
|-------|------|-----:|
| `lerp_msec` | `int16` | 2 |
| `msec` | `uint8` | 1 |
| `viewangles` | `v3f` (3×`float`) | 12 |
| `forwardmove` | `float` | 4 |
| `sidemove` | `float` | 4 |
| `upmove` | `float` | 4 |
| `lightlevel` | `uint8` | 1 |
| `buttons` | `uint16` | 2 |
| `impulse` | `uint8` | 1 |
| `weaponselect` | `uint8` | 1 |
| `impact_index` | `int32` | 4 |
| `impact_position` | `v3f` | 12 |

### 2.2 `krp_entvars` (60 bytes) — entity state

| Field | Type | Size |
|-------|------|-----:|
| `origin` | `v3f` | 12 |
| `velocity` | `v3f` | 12 |
| `v_angle` | `v3f` | 12 |
| `fixangle` | `int32` | 4 |
| `movetype` | `int32` | 4 |
| `flags` | `int32` | 4 |
| `button` | `int32` | 4 |
| `oldbuttons` | `int32` | 4 |
| `fuser2` | `float` | 4 |

### 2.3 `krp_glbvars` (4 bytes) — globals

| Field | Type | Size |
|-------|------|-----:|
| `frametime` | `float` | 4 |

`v3f` is three consecutive `float`s (`x`, `y`, `z`), 12 bytes.

---

## 3. Raw stream (`.krpr`)

After the header, a `.krpr` is a flat sequence of records. Each record starts with a **1-byte frame type**:

| Value | Name | Meaning |
|------:|------|---------|
| 0 | `KRP_FRAMETYPE_EVENT` | A run event (checkpoint / gocheck) |
| 1 | `KRP_FRAMETYPE_DELTA` | Frame stored as a diff against the previous frame |
| 2 | `KRP_FRAMETYPE_KEYFRAME` | Full, self-contained frame |

Record bodies by type:

- **EVENT** — 1 byte: the event id (`0` = checkpoint, `1` = gocheck).
- **KEYFRAME** — a 16-byte all-`0xFF` mask, followed by the complete 112-byte `krp_frame` (`cmd`, then `vars`, then `glb`). The first frame of every run is always a keyframe.
- **DELTA** — a 16-byte `krp_mask` bitmask followed by only the changed bytes (see §4).

The first frame after a run start or an unpause is written as a **keyframe**; all other ticks are **deltas**.

### Run lifecycle signals

Live capture is driven by signals pushed onto the writer queue (`KRP_SIGNAL_*`): `START`, `FRAME`, `EVENT`, `PAUSE`, `UNPAUSE`, `REJECT`, `FINISH`. These control the writer thread but are **not** all serialized verbatim — only frame-type records (event/delta/keyframe) end up in the byte stream. Start opens the file and writes the header; pause flushes checkpoint/teleport counts into the header and closes the file; unpause reopens and appends (next frame is a keyframe); reject closes and optionally deletes; finish patches the nickname and the checkpoint/teleport counts into the header, closes, and renames the file to its final name.

### Final filename

On finish the file is renamed to:

```
<gochecks:06>_<time_ms:08>_<steamid_short>_<timestamp_base36>.krpr
```

where `gochecks` is the teleport count (zero-padded to 6 digits), `time_ms` is the run time in milliseconds (zero-padded to 8 digits), `steamid_short` is the compact SteamID, and `timestamp_base36` is the start timestamp in base-36. Zero-padding gochecks first and time second makes lexicographic sort equal to fewest-gochecks-then-fastest — which is how playback picks the map's SR replay (a 0-gocheck pro run always outranks a faster run with teleports).

---

## 4. Delta encoding

Deltas operate on the raw bytes of `krp_frame`, not on fields. The mask is a `krp_mask` (two `uint64`, **16 bytes = 128 bits**); `krp_frame` is 112 bytes, so one mask bit maps to each byte with room to spare:

```
static_assert(sizeof(krp_mask) * 8 >= sizeof(krp_frame));
```

For each byte `i` of the frame, the writer XORs current vs previous (`diff = cur[i] ^ prev[i]`). If non-zero, it sets mask bit `i` (block `i/8`, bit `i%8`) and appends the **XOR diff byte**. Bytes that did not change contribute neither a set bit nor a data byte.

Decoding reverses this: keyframe bytes are copied directly (`dst[i] = data`), delta bytes are re-applied by XOR (`dst[i] ^= data`) against the reconstructed previous frame. A keyframe's mask is all-ones, so every byte is present.

---

## 5. Columnar reorganization + compression (`.krpz`)

Before upload, `kz_rp_reorganize_data` transposes the row-major `.krpr` body into field-parallel streams, then the whole thing is zstd-compressed (`ZSTD_compress`, level from `kz_api_replays_clevel`). The decompressed `.krpz` body layout is:

```
[ header ][ frame-types ][ mask/flags ][ column data ][ events ]
```

Section lengths come from the header's `size_types`, `size_flags`, `size_data`, and `size_events`.

- **frame-types** — one byte per record, in order (`size_types` bytes total; also equals the frame count including events).
- **mask/flags** — the per-frame masks, split into `num_blocks = sizeof(krp_mask)/8 = 2` blocks. For each block, all frames' 8 mask-bytes for that block are stored contiguously (block 0 stream, then block 1 stream). Only DELTA/KEYFRAME rows contribute mask bytes.
- **column data** — the diff/keyframe bytes grouped **by byte position** within `krp_frame` (column 0 for all frames, then column 1, …). Empty columns (a byte that never changed across the whole run) occupy zero space.
- **events** — the event ids, concatenated. Present in the layout but **ignored by playback**.

This column-major grouping clusters similar values (e.g. a given byte of an angle across thousands of ticks), which zstd compresses far better than the interleaved row form. The transform is lossless — decoding rebuilds the identical frame sequence.

Compression runs on the upload thread; if a queued file is already `.krpz` it is uploaded as-is, otherwise the `.krpr` is reorganized and compressed first. Upload is chunked (64 KiB chunks, each with a CRC32 and index) and rate-limited.

---

## 6. Playback decoding

`parse_playback` (in `kz_replay_pb.cpp`) reconstructs frames from a `.krpz`:

1. `ZSTD_getFrameContentSize` + `ZSTD_decompress` to recover the columnar body.
2. Read the header, validate `magic`, and dispatch on `version` (only `1` is accepted; anything else is rejected). Slice the four sections using `size_types` / `size_flags` / `size_data`, with the body starting right after the 256-byte header.
3. Walk the frame-types stream. The first type **must** be `KEYFRAME` or parsing aborts. For each DELTA/KEYFRAME row, read that row's two mask blocks, and for every set bit consume one byte from the matching column stream — copying it for keyframes, XOR-applying it for deltas — to rebuild the full `krp_frame`.
4. EVENT rows are skipped (events are not needed for playback).

The parser keeps only a reduced per-frame struct for the SR bot, `krp_playback_frame`: `origin` (`v3f`), `v_angle` (`v3f`), `flags`, `button`, `oldbuttons`. Everything else in `krp_frame` is decoded but discarded. The bot interpolates position between consecutive frames, derives velocity, and picks animation gaitsequences from the flags/buttons.

The run time and the `MM:SS.cc` timer string are parsed from the **filename** (the 8-digit millisecond field after the 6-digit gochecks prefix, i.e. bytes 7–14), not from the frame data.

---

## 7. Quick reference

- Magic: `0x4B52502146494C45` (`"KRP!FILE"`), current version `1`.
- Header: 256 bytes (189 fields + 67 reserved), packed, little-endian; body begins at `0x100`.
- Frame: 112 bytes = `usercmd` (48) + `entvars` (60) + `glbvars` (4).
- Delta mask: `krp_mask` = 2×`uint64` (16 bytes / 128 bits), one bit per frame byte, XOR diffs.
- Frame types: `0` EVENT, `1` DELTA, `2` KEYFRAME. First frame is always a keyframe.
- Events: `0` checkpoint, `1` gocheck.
- `.krpr` = raw row-major, uncompressed. `.krpz` = columnar `[header][types][flags][data][events]`, zstd-compressed.
- Playback reads `.krpz` only and uses `origin`, `v_angle`, `flags`, `button`, `oldbuttons`.
