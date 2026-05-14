# cs16kz — KZ Global API (AMXX module)

**KZ Global API** is an [AMX Mod X](https://www.amxmodx.org/) native module for **Counter-Strike 1.6** Kreedz (KZ) servers. It bridges your game server to the **Kreedz global API** over a **TLS WebSocket**, so plugins written in Pawn can sync map metadata, run state, and replay-related workflows with the wider Kreedz ecosystem.

---

## Features

- **WebSocket client** — Persistent connection to the Kreedz API (default endpoint: `wss://api.kreedz.com/ws/game`), with retries and structured message handling.
- **Pawn API** — Natives and includes under `addons/amxmodx/scripting/include/` for map lookups, run lifecycle (`started` / `paused` / `unpaused` / `rejected` / `finished`), and related helpers.
- **Local persistence** — SQLite-backed storage for queued/outgoing traffic and replay-related data paths (see `kz_storage` / `kz_replay` in `src/`).
- **ReHLDS integration** — Hooks into ReHLDS APIs where available for server-side behavior that matches modern CS 1.6 hosting stacks.
- **Server configuration** — CVars for API URL, token, logging, retry policy, and replay bot naming.

---

## Requirements

### To build

- **[Zig](https://ziglang.org/)** ≥ **0.15.1** (see `minimum_zig_version` in `build.zig.zon`).
- A **C/C++ toolchain** Zig can drive (Clang/LLVM is typical).
- **Network** for the first `zig build` so dependencies in `build.zig.zon` can be fetched; after that, `zig build --fetch` can populate the cache for offline builds.

---

## Building

From the repository root:

```bash
zig build
```

Supported **OS / CPU** combinations are enforced in `build.zig`: **Linux or Windows**, **x86 only**.

### Linux release example (project script)

The included script cross-compiles to `x86-linux-gnu` and renames the shared object to the expected AMXX module filename:

```bash
./build.sh
```

This runs:

`zig build -Dtarget=x86-linux-gnu` and moves `zig-out/lib/libkz_global_api_amxx_i386.so` to `zig-out/lib/kz_global_api_amxx_i386.so`.
