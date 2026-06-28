# Server integration guide

This guide is for **game server admins** who want to connect a Counter-Strike 1.6 Kreedz server to the **Kreedz global API** using the KZ Global API AMXX module.

The module handles the heavy lifting (WebSocket connection, replay capture, uploads, map WR sync, cross-community bans, SR bot playback). **Your KZ gameplay plugin** must tell it when runs start, pause, checkpoint, finish, and so on. Without that integration, the module loads but **no global records are submitted**.

---

## What you get

Once set up correctly, your server can:

- Submit **validated runs** (time + replay + checkpoint/gocheck counts) to the global leaderboard
- Receive **map WR data** and spawn an **SR replay bot** on the current map
- Enforce **cross-community bans** when players join
- Queue and retry uploads if the API is temporarily unavailable

---

## Before you start

### Server stack

| Component | Required? | Notes |
|-----------|-----------|-------|
| CS 1.6 dedicated server (`cstrike`) | Yes | Module only loads on Counter-Strike |
| **AMX Mod X** 1.8.2+ | Yes | Needs `common.games` gamedata |
| **Metamod** | Yes | Module attaches through Metamod |
| **ReHLDS** | Recommended | Best compatibility; module falls back to engine detours if ReHLDS is missing |
| **32-bit (x86)** Linux or Windows | Yes | Match your game server architecture |

### Network and disk

- **Outbound HTTPS/WSS** to the API endpoint (default: `wss://api.kreedz.com/ws/game`)
- **Disk space** under `addons/amxmodx/data/kz_global/` for replays and a small SQLite queue (grows with traffic; plan for several GB on busy servers)

### API access

You need a **server API token** from the Kreedz global programme. Set it in `kz_global.cfg` (see below). Without a valid token the module will not connect.

### Fair-play settings

The module enforces standard KZ server cvars (gravity, airaccel, ticrate, etc.) and pushes expected client cvars on join (fps, side speed, vsync, …). Your server should already use normal KZ physics; plugins that change those values may conflict.

---

## Two common situations

### A) Brand-new server

1. Install CS 1.6 + Metamod + AMX Mod X + ReHLDS (recommended).
2. Install the **KZ Global API module** and config files (see [Installation](#installation)).
3. Set your **API token** in `kz_global.cfg`.
4. Install or develop a **KZ plugin that calls the API natives** (see [KZ plugin integration](#kz-plugin-integration)). The bundled test plugin is for debugging only, not production gameplay.
5. Restart the server and verify the WebSocket connection in logs.

### B) Server that already runs KZ

You do **not** need to replace your existing KZ core (timer, checkpoints, ranks, etc.).

1. Install the module and config files alongside your current plugins.
2. **Add API calls** to your existing KZ plugin (or ship a small bridge plugin) at the same points where you already start/stop/pause runs.
3. Match the **include version** to the module you deploy (`addons/amxmodx/scripting/include/kz_global_api*.inc`).
4. Test on a staging server first — wrong lifecycle calls produce rejected records, not just missing leaderboard entries.

**Important:** Dropping in only the module `.so` / `.dll` is not enough. Records only flow when your gameplay plugin calls the natives at the right moments.

---

## Installation

Copy these from the release (or build output) into your game server:

| Source | Destination |
|--------|-------------|
| `kz_global_api_amxx_i386.so` (Linux) or `kz_global_api_amxx.dll` (Windows) | `cstrike/addons/amxmodx/modules/` |
| `addons/amxmodx/configs/kz_global.cfg` | same path under `cstrike/` |
| `addons/amxmodx/data/kz_global/cacert.pem` | same path under `cstrike/` |
| `addons/amxmodx/scripting/include/kz_global_api*.inc` | same path (needed to **compile** your integration plugin) |

Register the module in `cstrike/addons/amxmodx/configs/modules.ini`:

```ini
; Linux
kz_global_api_amxx_i386

; Windows
; kz_global_api_amxx
```

On map load, the module automatically runs `exec addons/amxmodx/configs/kz_global.cfg` (via the AMXX configs dir).

---

## Configuration

Edit `cstrike/addons/amxmodx/configs/kz_global.cfg`:

```cfg
// Required — get this from the Kreedz global programme
kz_api_token    "your-secret-token-here"

// Usually leave as default
kz_api_url      "wss://api.kreedz.com/ws/game"
```

Other useful settings (all documented in the cfg file):

| Cvar | Purpose |
|------|---------|
| `kz_api_log_send` / `recv` / `upload` / `parse` | Verbose logging for debugging |
| `kz_api_retries_max` / `kz_api_retries_delay` | Retry failed API messages |
| `kz_api_replays_clevel` | Replay compression level (1–22) |
| `kz_api_bot_prefix` / `bot_team` / `bot_use_cmd` | SR replay bot appearance and behaviour |

Changing `kz_api_url` or `kz_api_token` triggers an automatic reconnect.

---

## KZ plugin integration

The module exposes **Pawn natives** (see `addons/amxmodx/scripting/include/kz_global_api.inc`). Your KZ plugin must `#include <kz_global_api>` and call them when gameplay events happen.

### Run lifecycle (all modes)

| Game event | Native |
|------------|--------|
| Player starts a timed run | `kz_api_run_started(id)` |
| Player pauses (menu / +pause) | `kz_api_run_paused(id)` |
| Player unpauses | `kz_api_run_unpaused(id)` |
| Run invalid (death, disconnect, restart, style violation) | `kz_api_run_rejected(id, true)` |
| Player finishes with a valid time | `kz_api_run_finished(id, Float:time)` |

**Timer:** The module does **not** compute finish time. Your plugin must track elapsed time and **exclude pause time** (see `addons/amxmodx/scripting/kz_global_api_tests.sma` for a minimal example).

**Pause:** Call `run_paused` / `run_unpaused` in sync with your in-game pause. If you pause gameplay but forget the native, the replay keeps recording through the pause and the global API will reject the submission.

**Cleanup:** Call `kz_api_run_rejected(id, true)` when a player disconnects mid-run, dies (if the run ends), or resets — otherwise leftover replay files may accumulate.

**Alive players:** Natives require the player to be **in-game and alive**. Call `run_finished` / `run_rejected` **before** the player dies or goes to spec if your core finishes runs on death.

### TP runs (checkpoints / gochecks)

For checkpoint-style (TP) runs, also call:

| Game event | Native |
|------------|--------|
| Player sets a checkpoint | `kz_api_run_checkpoint(id)` |
| Player teleports to last checkpoint | `kz_api_run_gocheck(id)` |

These counts are sent with the record and checked against the replay. Missing or extra calls relative to actual gameplay will cause **API rejection**.

Always call `kz_api_run_started` **before** checkpoint/gocheck natives during a run.

### Optional: map WR lookup

```pawn
kz_api_get_map_details(mapname[], "your_callback");
```

Callback signature: `public your_callback(mapname[], wr_pro[], wr_noob[], map_props[3])`.

### Version check

Plugins that `#include <kz_global_api>` must match the loaded module version. A mismatch fails plugin load with a clear error in the AMXX log — update the include files when you upgrade the module.

### Reference test plugin

`addons/amxmodx/scripting/kz_global_api_tests.sma` registers console commands (`run_start`, `run_pause`, …) to exercise the API. Compile and load it only for **testing**, not as a replacement for a real KZ plugin.

---

## Checklist before going live

- [ ] Module appears in AMXX log on startup (no hook / gamedata errors)
- [ ] `kz_api_token` is set and not empty
- [ ] Server log shows WebSocket activity (enable `kz_api_log_recv` temporarily)
- [ ] KZ plugin calls `run_started` → … → `run_finished` on a test finish
- [ ] TP server: checkpoint and gocheck natives fire when players CP / gocheck
- [ ] Pause/unpause natives match your timer behaviour
- [ ] Disconnect / death / reset calls `run_rejected`
- [ ] Finish time on server matches what you expect on the global site
- [ ] SR bot spawns after a valid WR replay is available (may take one accepted record on that map)

---

## Troubleshooting

| Symptom | Likely cause |
|---------|----------------|
| No WebSocket traffic | Empty or wrong `kz_api_token`; firewall blocking outbound WSS |
| `[WS] Connection closed (1008)` | Invalid token, unapproved module build, or policy rejection — check with Kreedz ops |
| HTTP 401 / 403 on connect | Bad or revoked token |
| `Failed to install one or more hooks` | Missing ReHLDS/detour support; PLAYER_LEAVE or cvar enforcement may be broken |
| `common.games gamedata could not be read` | AMX Mod X too old or misinstalled |
| Plugin `set_fail_state` on load | Update `kz_global_api*.inc` to match module version |
| Records never appear globally | Gameplay plugin not calling natives, or API rejecting mismatched time/replay/checkpoints |
| `[KRP] run_finish: no file descriptor` | `run_finished` without a prior `run_started`, or run already rejected |
| `[AC] Illegal …` in logs | Client or server cvars outside expected KZ values (informational; review cheats / configs) |
| Player kicked: cross-community banned | Working as intended — API flagged the Steam ID |

Replay and upload details are logged when `kz_api_log_upload` is enabled. Pending messages are stored in SQLite and retried automatically.

---

## What the module does *not* do

- Replace your KZ timer, checkpoint zones, rank system, or map logic
- Decide whether a run is “valid” by KZ rules — that stays in your plugin; the **global API** validates replay vs submitted metadata
- Submit records for **bots** (natives reject bot players)
- Work without a **KZ plugin integration** — the module alone is infrastructure, not a gamemode

---

## Need help?

- **Module / connection / uploads:** check server logs with logging cvars enabled; verify token and file paths above.
- **Plugin integration:** share your KZ core’s run start/finish/pause/checkpoint hooks with whoever maintains your plugins, and map them to the natives in this doc.
- **API access / token / approvals:** contact the Kreedz global programme operators.

For developers building the integration, the native definitions live in `addons/amxmodx/scripting/include/` and the test plugin shows minimal call patterns.
