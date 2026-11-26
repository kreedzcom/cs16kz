# cs16kz — KZ Global API (AMXX module)

**KZ Global API** is an [AMX Mod X](https://www.amxmodx.org/) native module for **Counter-Strike 1.6** Kreedz (KZ) servers. It bridges your game server to the **Kreedz global API** over a **TLS WebSocket**, so plugins written in Pawn can sync map metadata, run state, and replay-related workflows with the wider Kreedz ecosystem.

---

## Features

- **WebSocket client** — Persistent connection to the Kreedz API (default endpoint: `wss://api.kreedz.com/ws/game`), with retries and structured message handling.
- **Security-aligned protocol** — `hello` version/checksum, `add_record` with `gochecks`/`checkpoints`, cross-community ban kicks on join, replay upload with `FILE_ACK` retry on failure, and cleanup when the API rejects a record.
- **Pawn API** — Natives and includes under `addons/amxmodx/scripting/include/` for map lookups, run lifecycle (`started` / `paused` / `unpaused` / `rejected` / `finished`), and related helpers.
- **Local persistence** — SQLite-backed storage for queued/outgoing traffic and replay-related data paths (see `kz_storage` / `kz_replay` in `src/`).
- **ReHLDS integration** — Hooks into ReHLDS APIs where available for server-side behavior that matches modern CS 1.6 hosting stacks.
- **Server configuration** — CVars for API URL, token, logging, retry policy, and replay bot naming.

---

## Server integration

If you run a KZ server and want to join the global API (records, replays, SR bot, bans), see **[docs/server-integration.md](docs/server-integration.md)** — setup for new and existing servers, config, KZ plugin hooks, and troubleshooting.

---

## Requirements

### To build

- **[Zig](https://ziglang.org/)** ≥ **0.16.0** (see `minimum_zig_version` in `build.zig.zon`).
- A **C/C++ toolchain** Zig can drive (Clang/LLVM is typical).
- **Network** for the first `zig build` so dependencies in `build.zig.zon` can be fetched; after that, `zig build --fetch` can populate the cache for offline builds.
- **Linux x86 release build** (`./build.sh`): 32-bit system libraries — on Debian/Ubuntu, enable `i386` and install `libsqlite3-dev:i386`, `libssl-dev:i386`, and `zlib1g-dev:i386` (CI does this automatically).

---

## Building

From the repository root:

```bash
zig build
```

Supported **OS / CPU** combinations are enforced in `build.zig`: **Linux or Windows**, **x86 only**.

### Linux release example (project script)

The included script builds the Linux x86 release modules:

```bash
./build.sh
```

Zig installs `kz_global_api_amxx_i386.so` and `kz_base_amxx_i386.so` directly under `zig-out/lib/`. Release archives include both modules and publish the HELLO version and source fingerprints in `kz-hello.txt`.

---

## Testing

On Linux with Zig and the zlib and zstd development headers installed, run `bash scripts/verify-replay-contract.sh /path/to/kz-global-api`. This checks concurrent SQLite persistence and native replay bytes against the companion backend at its checked-out commit. The CI job pins the backend commit, which must be available on the companion remote before publishing this branch.

**CI** runs on every push/PR: host unit tests, native API lint, and Linux and Windows x86 module builds (see [`.github/workflows/ci.yml`](.github/workflows/ci.yml)). A `v*` tag publishes both platforms as a GitHub Release (see [`.github/workflows/release.yml`](.github/workflows/release.yml)).

Local host tests (no full AMXX dependency fetch required for the shell script):

```bash
bash scripts/run-tests.sh
# or: zig build test -Dtest-only=true
```

Windows:

```powershell
.\scripts\run-tests.ps1
```

These cover path-segment validation and KRP header checks. WebSocket/API integration still needs a staging game server.
