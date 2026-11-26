# AGENTS.md — AI Agent Guide

This file tells AI coding agents how to work in this repository.

## What this project is

`cs16kz` is the **KZ Global API** AMX Mod X native module for **Counter-Strike 1.6** Kreedz (KZ) servers.
It connects game servers to the global backend over a **TLS WebSocket**, submits player runs and replays,
and exposes a Pawn API for KZ gameplay plugins.

Companion repo: [kz-global-api](https://github.com/nquinquenel/kz-global-api) — the Kotlin backend that
accepts WebSocket connections, stores records, and serves replays from Cloudflare R2.

## Tech stack

- **C++17** — module source in `src/`
- **Zig** (≥ 0.16.0, see `minimum_zig_version` in `build.zig.zon`) — build system only; no Zig application code
- **AMX Mod X** + **Metamod** + **ReHLDS** (optional but recommended) — runtime integration
- **IXWebSocket** + **mbedTLS** — WSS client
- **Parson** — JSON parsing/serialization
- **SQLiteCpp** — local queue and persistence
- **zstd** — replay compression (`.krpz`)
- **SPSCQueue** — lock-free queue between WS thread and game thread

If you add or replace a dependency, update this section and `build.zig.zon`.

## How to build and run

Supported targets: **Linux or Windows**, **x86 only** (32-bit, matching CS 1.6 dedicated servers).

```bash
zig build                              # build module + run host unit tests
zig build test -Dtest-only=true        # host unit tests only (no AMXX deps)
./build.sh                             # Linux x86 release build with final AMXX filenames
```

Windows:

```powershell
zig build
.\scripts\run-tests.ps1
```

First build needs network access to fetch dependencies (`zig build --fetch` populates the cache for offline builds).

Output artifacts:
- Linux: `zig-out/lib/kz_global_api_amxx_i386.so` and `zig-out/lib/kz_base_amxx_i386.so`
- Windows: `zig-out/bin/kz_global_api_amxx.dll` and `zig-out/bin/kz_base_amxx.dll`

Deploy under `cstrike/addons/amxmodx/modules/` on the game server. Server setup is documented in `docs/server-integration.md`.

## Code conventions

- C++ sources live in `src/kz_global_api/` and `src/kz_base/`, with headers in each module's `include/` directory
- One responsibility per file — keep files small and focused (e.g. `kz_ws.cpp` for connection lifecycle, `kz_ws_msgs.cpp` for message handlers)
- Use `kz_` prefix for module-specific symbols; KRP replay types use `krp_`
- WebSocket message type constants live in `src/kz_global_api/include/kz_ws_protocol.h` (`WSMsgOut::`, `WSMsgIn::`)
- Pawn natives are registered in `kz_natives.cpp`; public API headers in `addons/amxmodx/scripting/include/`
- CVars are defined in `kz_cvars.cpp` / `kz_cvars.h`
- Use `kz_log(&g_*_log, ...)` for module logging — never `printf` or `std::cout`
- On-disk structs use `#pragma pack(push, 1)` — no implicit padding
- Build with `-Wall -Wextra -Werror`; new code must compile warning-free
- `MODULE_VERSION` and `MODULE_CHECKSUM` are injected at compile time by `build.zig` — do not hardcode version strings

### AMXX / Metamod specifics

- `MF_GetAmxString` returns a `char*` managed by AMXX — **never** pass a local buffer or `sizeof` (CI rejects this pattern)
- Do not allocate with `new`/`malloc` before `OnAmxxAttach` unless using the memory manager (disabled here)
- Metamod hooks are enabled via `#define FN_*` entries in each module's `include/moduleconfig.h` — only uncomment hooks you actually implement
- Engine/game callbacks run on the main server thread; the WebSocket runs on a background thread — use `SPSCQueue` and mutexes to cross thread boundaries

## Security and performance

Every feature or change must consider both:

**Security**
- Validate all path segments before writing under `addons/amxmodx/data/kz_global/` — use `kz_ws_valid_replay_segment()` (see `kz_path_validate.cpp`)
- Never construct filesystem paths from unvalidated user/API input (no `..`, slashes, or spaces)
- Download replays over **HTTPS only** from presigned URLs returned by the API
- Do not log API tokens or player secrets
- KRP header validation must reject unknown magic/version before parsing body data

**Performance**
- WebSocket message handlers must not block the WS thread on slow disk or network I/O — queue work and process on the game thread where appropriate
- Replay capture runs every tick via Metamod hooks — keep `krp_frame` serialization lean; no heap allocations in hot paths
- SQLite writes are for queue/persistence only — batch where possible, keep transactions short
- Upload retries and replay downloads use configurable backoff (`kz_api_retries_*` CVars)

## Testing

**Always write tests** for pure logic that can run on the host (no game server required).

Host unit tests live in `src/kz_global_api/test/` and are compiled separately from the AMXX module:

```bash
bash scripts/run-tests.sh
# or: zig build test -Dtest-only=true
```

Current coverage: path-segment validation (`kz_path_validate.cpp`), KRP header validation and first-frame event ordering, replay UID selection, acknowledgment matching, and run event state.

When adding testable logic, follow the existing pattern in `src/kz_global_api/test/` — simple `expect()` helpers, no external test framework.

WebSocket/API integration and replay playback require a staging game server; they are not covered by host tests.

**CI** (`.github/workflows/ci.yml`) runs on every push/PR:
1. Static checks: `bash scripts/lint-amxx.sh` (rejects `MF_GetAmxString` buffer misuse)
2. Host unit tests: `zig build test -Dtest-only=true`
3. Full Linux and Windows x86 module builds

Run the full CI-equivalent checks locally before committing.

## Documentation

The `docs/` folder contains reference documentation for this project. **Keep it up to date** whenever you make a meaningful change to protocols, replay format, or server setup. **Do not put dependency or toolchain version numbers in `docs/`** (those belong in README / `build.zig.zon`).

| File | What it covers |
|------|----------------|
| `docs/server-integration.md` | Server admin guide: install, config, KZ plugin hooks, troubleshooting |
| `docs/replay-format.md` | KRP replay format (`.krpr` / `.krpz`), frame layout, compression |

Cross-repo protocol reference (authoritative for WS message schemas):
- `kz-global-api/docs/websocket-protocol.md` — every WS message type with JSON schemas
- `kz-global-api/docs/plugin-integration.md` — plugin-facing protocol requirements

When to update:
- **KRP format change** → `docs/replay-format.md` + `src/kz_global_api/include/krp_format.h`
- **New/changed Pawn native** → `addons/amxmodx/scripting/include/kz_global_api.inc`
- **Server config or deployment change** → `docs/server-integration.md`
- **New WS message type** → `src/kz_global_api/include/kz_ws_protocol.h`, handler in `kz_ws_msgs.cpp`, and `kz-global-api/docs/websocket-protocol.md`

## Adding a new WebSocket message type

1. Add outbound/inbound constant to `WSMsgOut::` or `WSMsgIn::` in `src/kz_global_api/include/kz_ws_protocol.h`
2. Implement send logic (if outbound) or a handler function (if inbound) in `kz_ws_msgs.cpp`
3. Register inbound handlers in the dispatch table in `kz_ws.cpp`
4. Coordinate with `kz-global-api` — add the matching message type, handler, and test on the backend
5. Update `kz-global-api/docs/websocket-protocol.md`

## Adding a new Pawn native

1. Implement the native in `kz_natives.cpp` and declare it in `src/kz_global_api/include/kz_natives.h`
2. Register it in the natives table (same file)
3. Add documentation and the function signature to `addons/amxmodx/scripting/include/kz_global_api.inc`
4. If the native triggers a WS message, wire it through the existing queue/send path in `kz_ws.cpp`

## Project layout

```
src/
  kz_global_api/              WebSocket, replay, and Pawn API module
    include/                  Global module headers
    test/                     Host-side unit tests
  kz_base/                    Checkpoints and movement helpers
  shared/                     AMXX and engine integration
addons/amxmodx/
  scripting/include/          Pawn API (.inc files)
  configs/kz_global.cfg       Default server CVars
  data/kz_global/             Runtime data dir (replays, SQLite, CA cert)
docs/                         Reference documentation
build.zig / build.zig.zon     Zig build system and dependencies
```

## What NOT to do

- Do not change KRP on-disk layout without a version bump and migration plan
- Do not store API tokens in source code — servers set them via `kz_global.cfg`
- Do not bypass path validation when writing replay or queue files
- Do not commit a failing test or a build that does not pass `-Werror`
- Do not add Metamod hooks in `moduleconfig.h` without implementing the corresponding function
