---
name: chmbl-config-and-flags
description: >
  Load this skill whenever you need to find, change, or add ANY configuration knob in
  the open-chmbl repo: firmware Kconfig options (CONFIG_CHMBL_*, CONFIG_LOGGER_*),
  sdkconfig.defaults per target (esp32 vs esp32c3, console transport), the DE-09
  braking-FSM tunables (designed tx_config_t vs the trc_viz BrakeTunables bench),
  compile-time decode constants (CAN_DECODE_*), python tool CLI flags (gen_profile.py,
  golden_check.py, trc_viz.py), CI matrix knobs, and NVS-backed runtime state
  (pairing persistence; `config show/set/save` is specified but NOT implemented).
  Also load it before ADDING a new config axis (checklist included). Do NOT load it
  for build-environment setup (chmbl-build-and-env), flashing/console/CLI operation
  (chmbl-run-and-operate), why a design decision exists (chmbl-architecture-contract),
  or the DE-09 implementation campaign itself (chmbl-de09-campaign).
---

# open-chmbl: every configuration axis, verified

Catalog of every place behavior is configurable in this repo, verified against the
tree as of **2026-07-07** (branch `claude/skill-library-continuity-mib7ua`). Each
section ends with the file(s) that own it; §10 gives one re-verification command per
section — run it before trusting any number here.

Jargon used below, defined once:

- **Kconfig** — ESP-IDF's menu-driven build configuration system. Each firmware
  declares project options in `main/Kconfig.projbuild`; a symbol `CHMBL_CLI` becomes
  the C macro `CONFIG_CHMBL_CLI` via the generated `sdkconfig.h`.
- **sdkconfig** — the generated, git-ignored file holding every resolved Kconfig
  value for the current build. **`sdkconfig.defaults`** (committed) seeds it.
- **NVS** — Non-Volatile Storage, an ESP-IDF key-value store in flash that survives
  reboot and reflash (unless the NVS partition is erased).
- **TWAI** — Two-Wire Automotive Interface, Espressif's name for its CAN controller.
- **CAN** — Controller Area Network, the motorcycle's internal message bus. This
  project only ever *listens* to it.
- **ESP-NOW** — Espressif's connectionless 2.4 GHz radio protocol linking the
  bike-side transmitter to the helmet-side brake_light.
- **FSM** — finite state machine; DE-09 is the braking FSM (OFF/BRAKING/STOPPED).
- **DBC** — a CAN database file describing message/signal bit layouts
  (`profiles/triumph_tr.dbc`).
- **`.trc`** — PCAN-View trace format; the committed ride capture is
  `logger/40mph_drive_cycle.trc`.

Three firmwares, each an independent ESP-IDF project with its own config namespace:

| Firmware | Path | Targets | Kconfig prefix |
|---|---|---|---|
| transmitter (bike-side) | `transmitter/software/` | esp32c3 (product), esp32 (interim dev) | `CHMBL_` |
| brake_light (helmet-side) | `brake_light/software/` | esp32c3 (product), esp32 (interim dev) | `CHMBL_` |
| logger (ride recorder) | `logger/software/` | esp32 only (ESP-WROVER-KIT) | `LOGGER_` |

---

## 1. Firmware Kconfig options (`main/Kconfig.projbuild`)

All are **production-relevant build-time options** unless noted. Change them with
`idf.py menuconfig` (per-checkout) or by editing `sdkconfig.defaults*` (committed —
that is a repo change, see §9 and `chmbl-change-control`).

### 1.1 Transmitter — `transmitter/software/main/Kconfig.projbuild`, menu "Transmitter configuration"

| Symbol | Type | Default | Purpose / guard |
|---|---|---|---|
| `CHMBL_CLI` | bool | `y` | Build the developer CLI shell (DE-00). **Dev feature; help text says disable for production builds** — the shell is then absent from the image. Guards `console_start()` via `#if CONFIG_CHMBL_CLI` in `main.c`. |
| `CHMBL_STATE_GPIO` | int, range 0–33 | 8 on esp32c3, 2 on esp32 | Stand-in state-indicator GPIO lit by the `state` command when the stand-in braking state is DECEL/BRAKE. Defaults = onboard LED of each reference dev board. **Interim** until real CAN decode / FSM / ESP-NOW TX land. On classic ESP32 avoid GPIO6–11 (SPI flash). |
| `CHMBL_CAN_TX_GPIO` | int, range 0–33 | 4 on esp32c3, 21 otherwise | GPIO to the SN65HVD230 CAN transceiver's TXD (D pin). The TWAI peripheral requires a TX pin even though the controller is **listen-only and never drives the bus**. |
| `CHMBL_CAN_RX_GPIO` | int, range 0–39 | 5 on esp32c3, 22 otherwise | GPIO from the transceiver's RXD (R pin). |
| `CHMBL_NET_CHANNEL` | int, range 1–13 | 6 | Fixed 2.4 GHz ESP-NOW channel. **Must match the brake_light's `CHMBL_NET_CHANNEL`** — no channel-agreement handshake exists yet (open item, `docs/design/de-01-espnow-link.md`). |
| `CHMBL_NET_RATE_HZ` | int, range 1–50 | 20 | Boot-time ESP-NOW heartbeat rate; runtime override via `net rate <hz>` (RAM-only, not persisted — see §7.3). |
| `CHMBL_PAIR_TIMEOUT_S` | int, range 3–60 | 15 | How long `pair start` broadcasts/listens before giving up. |

### 1.2 Brake_light — `brake_light/software/main/Kconfig.projbuild`, menu "Brake_light configuration"

| Symbol | Type | Default | Purpose / guard |
|---|---|---|---|
| `CHMBL_CLI` | bool | `y` | Same as transmitter's (independent symbol in this project's sdkconfig). |
| `CHMBL_LIGHT_GPIO` | int, range 0–33 | 8 on esp32c3, 2 on esp32 | Stand-in brake-light GPIO driven by the `light` command, **until the real LED render engine (DE-04) lands**. |
| `CHMBL_NET_CHANNEL` | int, range 1–13 | 6 | Must match the transmitter's value (same caveat as above). |
| `CHMBL_PAIR_TIMEOUT_S` | int, range 3–60 | 15 | Pairing window, as on the transmitter. |
| `CHMBL_LINK_TIMEOUT_MS` | int, range 50–2000 | 300 | Link watchdog (DE-03 placeholder): no valid heartbeat for this long → link lost. Protocol target ≤ 300 ms (`docs/protocol.md` §4). |
| `CHMBL_LINK_BLINK_MS` | int, range 50–1000 | 250 | **Placeholder** link-lost/waiting blink half-period on the single stand-in LED. The real distinct indication (running light + fault blink + status LED) arrives with DE-03/DE-10. Note: blinking a *dev-board* LED for link-loss is the fail-honest indication, not a light pattern on the product bar — the no-strobe rule for the brake bar stands. |

### 1.3 Logger — `logger/software/main/Kconfig.projbuild`, menus "CAN logger configuration" and "Status LED"

| Symbol | Type | Default | Purpose / guard |
|---|---|---|---|
| `LOGGER_BUTTON_GPIO` | int, range 0–39 | 33 | Start/stop pushbutton GPIO (active-low, internal pull-up, debounced by the `iot_button` component). GPIO33 is free on the WROVER-KIT v4.1; GPIO32 is the stated alternative. |
| `LOGGER_CAN_TX_GPIO` | int, range 0–33 | 26 | TWAI TX → transceiver TXD. Claimed by the driver even in listen-only mode. |
| `LOGGER_CAN_RX_GPIO` | int, range 0–39 | 27 | TWAI RX ← transceiver RXD. |
| `LOGGER_CAN_BITRATE` | choice | `LOGGER_CAN_BITRATE_500K` | Bit rate of the logged bus. Options: `_125K`, `_250K`, `_500K`, `_1M`. Reference Triumph bus is 500 kbit/s (`docs/can-profiles.md`). |
| `LOGGER_CAN_LISTEN_ONLY` | bool | `y` | **Safety-critical.** Listen-only (silent) TWAI mode: never ACKs or transmits. Help text: disable only for a two-node bench where the single peer needs an ACK. **Never disable on a vehicle** — this is the project's golden rule. |
| `LOGGER_RX_QUEUE_LEN` | int, range 64–8192 | 1024 | Frame queue between the CAN RX task and the microSD writer. Deeper tolerates longer SD stalls at the cost of RAM; overflows are counted and logged. |
| `LOGGER_STATUS_LED_GPIO` | int, range 0–39 | 0 | WROVER-KIT onboard red LED die — the only RGB leg not shared with microSD (green=GPIO2/D0, blue=GPIO4/D1) or the removed LCD. GPIO0 is the boot-strap pin but its strap is sampled at reset before code runs, so driving it afterward is safe (matches Espressif's own BSP). |
| `LOGGER_STATUS_LED_ACTIVE_LOW` | bool | `n` | GPIO high = LED on by default (matches the esp_wrover_kit BSP). Toggle if the LED behaves backwards on your board. |

---

## 2. `sdkconfig.defaults*` per firmware, and how ESP-IDF layers them

### 2.1 What each file sets

**transmitter and brake_light are identical in content** (verify with `diff`):

| File | Sets | Why |
|---|---|---|
| `<fw>/software/sdkconfig.defaults` | `CONFIG_LOG_DEFAULT_LEVEL_INFO=y` | Quieter default log level for shipping firmware. Applies to all targets. |
| `<fw>/software/sdkconfig.defaults.esp32c3` | `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` | Product target: console (REPL + logs) on the built-in USB Serial/JTAG controller — enumerated virtual COM port on native USB pins GPIO18/19, JTAG on the same cable, no USB-TTL adapter. Host sees `/dev/ttyACM*`. |
| `<fw>/software/sdkconfig.defaults.esp32` | `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` | Interim dev hardware: classic ESP32 has no USB peripheral, so console is UART0 (GPIO1 TX / GPIO3 RX) via the board's USB-UART bridge (`/dev/ttyUSB*`, 115200). This is already the IDF default for esp32; it is set explicitly to document the divergence from the C3. |

**logger** has only `logger/software/sdkconfig.defaults` (single target, esp32):

| Line | Why |
|---|---|
| `CONFIG_SPIRAM=y` | WROVER module carries 4 MB PSRAM; leaves internal RAM headroom for the CAN frame queue / write buffering. |
| `CONFIG_FATFS_LFN_STACK=y` | FAT long filenames so the microSD's existing files list cleanly (our own logs are 8.3 `N.trc` names). |
| `CONFIG_LOG_DEFAULT_LEVEL_INFO=y` | Same rationale as the others. |

### 2.2 ESP-IDF layering mechanics (what actually happens)

- On a fresh configure, IDF builds `sdkconfig` from `sdkconfig.defaults` first, then
  applies `sdkconfig.defaults.<IDF_TARGET>` **on top** — so the target file wins on
  any overlap. The resulting `sdkconfig` is **generated and git-ignored**; the
  `.defaults` files are the committed source of truth.
- `idf.py set-target esp32c3` (or `esp32`) selects the target and **regenerates
  `sdkconfig` from scratch** (backing up the old one), discarding any local
  `menuconfig` edits. Run it once per checkout per target switch, before `idf.py
  build`.
- Editing a `.defaults` file does **not** touch an already-generated `sdkconfig`.
  To pick up changed defaults: delete `sdkconfig` and reconfigure, re-run
  `set-target`, or `idf.py fullclean` (which wipes the build dir; then reconfigure).
  When in doubt after changing defaults, target, or IDF version:
  `idf.py fullclean && idf.py set-target <t> && idf.py build`.
- `idf.py menuconfig` edits only the local generated `sdkconfig` — fine for board
  bring-up, but any value that should hold for everyone belongs in
  `Kconfig.projbuild` defaults or `sdkconfig.defaults*` (committed).

**UNVERIFIED at runtime** — ESP-IDF is not installed in this authoring environment,
so the layering description above is from IDF v5.3 documented behavior plus the
comments in the committed defaults files; the file contents themselves are verified.
CI (§6) exercises the real `set-target`-equivalent path on every push.

---

## 3. DE-09 braking-FSM tunables — DESIGNED, NOT YET IN FIRMWARE

**Status as of 2026-07-07: DE-09 is unimplemented (🔲).** No `tx_config_t` exists in
C source; `transmitter/software/state_machine/brake_fsm.sm` does not exist yet. The
tunables live in two places: the design doc (the spec) and the calibration bench
tool (executable). Implementing them in firmware is the `chmbl-de09-campaign` skill's
subject — do not "fix" this gap casually.

> **Canonical home.** §3.1–3.2 are the library's single source of truth for the
> FSM tunables (doc defaults AND trc_viz bench defaults). Sibling skills carry
> excerpts; update here first when either side changes.

### 3.1 The spec: `tx_config_t` in `docs/firmware.md` §1 "Config"

Speeds in MPH, accelerations in MPH/s. Design-doc defaults:

| Field | Default | Purpose |
|---|---|---|
| `decel_on_mphps` | 3.0 | Deceleration that turns the light on. |
| `decel_on_debounce_ms` | 120 | Decel must exceed threshold this long before BRAKING (rejects momentary dips; doc calls 120 ms "the knee"). |
| `stop_speed_mph` | 1.0 | At/under = stopped (enter STOPPED). |
| `moving_speed_mph` | 3.0 | Must exceed to leave STOPPED (hysteresis; > stop_speed). |
| `accel_off_mphps` | 0.5 | Acceleration that turns the light off while moving. |
| `accel_off_min_speed_mph` | 5.0 | Only honor accel-off above this speed. |
| `steady_band_mphps` | 0.5 | \|accel\| under this counts as "steady". |
| `steady_timeout_ms` | 2000 | Steady-after-decel hold before turning off. |
| `stop_timeout_ms` | 60000 | Max on-time while stopped. |
| `state_min_dwell_ms` | 150 | Global anti-strobe floor on all transitions. |

The struct also carries `profile` (bike CAN layout), `has_gear_signal`, and
`tx_rate_hz` (20–50). Doc says: compile-time config first, runtime later; "all
tunables are config values, so the behaviour can be retuned without touching the
state machine."

### 3.2 The bench: `BrakeTunables` dataclass in `tools/trc_viz.py` (~line 77)

Its docstring is the authority on which values the design doc **fixes** vs **leaves
open**: *"Values stated in the design doc are fixed here; the four it leaves open
(decel_on, accel_off, steady_band, steady_timeout) and the anti-strobe dwell get
sensible defaults, all overridable via the UI/CLI."*

| Field | trc_viz default | Doc-fixed or open? | vs firmware.md table |
|---|---|---|---|
| `decel_on_mphps` | 2.0 | open | differs (doc table shows 3.0) |
| `decel_on_debounce_ms` | 120.0 | fixed | matches |
| `stop_speed_mph` | 1.0 | fixed | matches |
| `moving_speed_mph` | 3.0 | fixed | matches |
| `accel_off_mphps` | 0.5 | open | matches numerically |
| `accel_off_min_speed_mph` | 5.0 | fixed | matches |
| `steady_band_mphps` | 0.75 | open | differs (doc table 0.5) |
| `steady_timeout_ms` | 1500.0 | open | differs (doc table 2000) |
| `stop_timeout_ms` | 60000.0 | fixed | matches |
| `state_min_dwell_ms` | 250.0 | open (anti-strobe dwell) | differs (doc table 150) |
| `speed_smooth_ms` | 80.0 | tool-only (wheel-speed low-pass tau before slope calc) | not in the doc table |

The "open" fields' doc-table numbers are the doc's *suggested defaults*; trc_viz's
divergent values are the bench's working calibration. When DE-09 lands in firmware,
the chosen values must be written back into `docs/firmware.md` in the same change
(docs-are-spec rule).

### 3.3 Related compile-time decode constants (IMPLEMENTED, in firmware today)

`transmitter/software/main/can_decode.h` — plain `#define`s, not Kconfig:

| Constant | Value | Meaning |
|---|---|---|
| `CAN_DECODE_STALE_MS` | 1000 | Signal staleness cutoff. |
| `CAN_DECODE_ACCEL_WINDOW_MS` | 200 | Regression window for deriving acceleration from wheel speed. |
| `CAN_DECODE_ACCEL_ALPHA` | 0.3 | Accel smoothing factor. |
| `CAN_DECODE_SPEED_HIST` | 16 | **KNOWN LIVE BUG**: wheel speed arrives ~100 Hz, so 16 samples span ~150 ms < the 200 ms window — derived accel freezes. trc_viz uses 32 (see its header note, ~line 49). Sizing law: `hist ≥ window_ms × frame_rate` (canonical statement: `chmbl-de09-campaign` Phase 1). **The fix is reserved for the DE-09 campaign**; don't patch it in an unrelated change. |
| `KMH_TO_MPH` | 0.621371 | Unit conversion. |

---

## 4. Python tool flags

All run from the repo root; defaults are relative paths, so **run from the repo root**
or pass absolute paths.

### 4.1 `tools/gen_profile.py` — DBC → C profile table generator

| Flag | Required | Default | Meaning |
|---|---|---|---|
| `dbc` (positional) | yes | — | Input DBC path. |
| `--name` | yes | — | Human-readable profile name. |
| `--bitrate` | yes (int) | — | Bus bit rate baked into the profile. |
| `--symbol` | yes | — | C symbol / file base name. |
| `--out` | no | stdout | Output `.c` path. |

Canonical invocation (this exact command is what CI diffs against — see §6):

```bash
python3 tools/gen_profile.py profiles/triumph_tr.dbc \
  --name "Triumph Speed 400 / Scrambler 400X (TR-series)" \
  --bitrate 500000 \
  --symbol bike_profile_triumph_tr \
  --out transmitter/software/main/bike_profile_triumph_tr.c
```

Never hand-edit the generated file; regenerate and commit whenever the DBC changes.

### 4.2 `tools/golden_check.py` — C decoder vs cantools agreement

| Flag | Default | Meaning |
|---|---|---|
| `--harness` | `transmitter/software/test_host/build/trc_replay` | Host-built C decoder binary (build it first with cmake, see CI steps in §6). |
| `--dbc` | `profiles/triumph_tr.dbc` | Reference DBC decoded by python-cantools. |
| `--trc` | `logger/40mph_drive_cycle.trc` | Capture replayed through both paths. |

Agreement tolerances are constants in the file, not flags: `REL_TOL = 1e-4`,
`ABS_TOL = 1e-3` (lines 29–30).

### 4.3 `tools/trc_viz.py` — playback dashboard + DE-09 calibration bench

Runs via `uv run tools/trc_viz.py …` (PEP 723 inline deps: cantools, python-can,
numpy, dearpygui — same pins as `tools/requirements.txt`).

| Flag | Default | Meaning |
|---|---|---|
| `trc` (positional) | — | PCAN `.trc` capture to play back. |
| `--dbc` | `profiles/triumph_tr.dbc` | DBC used for decode. |
| `--headless-check` | off | Decode + derive accel + run the FSM, print stats, **no GUI** (the only mode usable without a display). Prints duration, grid points, peak speed/rpm/throttle, gears seen, accel range, FSM transition count, brake-on time, and a peak-speed sanity line (PASS if 40–46 mph on the reference ride). **Always exits 0** — the sanity line is informational, not a gate (verified in `headless_check()`, ~line 358). |
| `--selftest-frames N` | 0 | Render N GUI frames sweeping the ride, then exit (testing). |
| `--snapshot PATH` | "" | Write a frame-buffer PNG during `--selftest-frames`. |
| `--<tunable-name>` | None | **One float flag is auto-generated per `BrakeTunables` field**, underscores → hyphens (e.g. `--decel-on-mphps 2.5`, `--speed-smooth-ms 0`). Unset flags keep the dataclass defaults from §3.2. |

Verified working here: `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check`.

---

## 5. Host test harness build type

The golden harness is a plain CMake project (`transmitter/software/test_host/`), no
Kconfig. Its only knob is the standard `-DCMAKE_BUILD_TYPE=Release` used by CI.

---

## 6. CI knobs — `.github/workflows/firmware-build.yml`

| Knob | Value | Notes |
|---|---|---|
| `esp_idf_version` | `release-v5.3` | Input to `espressif/esp-idf-ci-action@v1`, one place, applied to every matrix row. Bumping IDF = change here + local reinstall (see `chmbl-build-and-env`). |
| Build matrix | 5 rows: brake_light×{esp32c3, esp32}, logger×esp32, transmitter×{esp32c3, esp32} | `fail-fast: false`. Both CHMBL firmwares are kept building on both targets deliberately so the tree stays portable (console transport chosen by target defaults, §2). |
| `python-version` | `"3.12"` | `can-decode-golden` host job only. |
| Python deps | `pip install -r tools/requirements.txt` | cantools≥39, python-can≥4.3, numpy≥1.26, dearpygui≥1.11. |
| Triggers | push (all branches) / PR, path-filtered to `brake_light/software/**`, `logger/software/**`, `transmitter/software/**`, `profiles/**`, `tools/**`, the workflow file itself; plus `workflow_dispatch` | A docs-only change does not run firmware CI. |
| Golden job steps | gen_profile staleness diff (exact command from §4.1, diffed against the committed file) → cmake host harness (Release) → `python3 tools/golden_check.py` with all defaults | The staleness gate means a DBC change without a regenerated committed profile **fails CI**. |

---

## 7. Runtime configuration and NVS-backed state

### 7.1 `config show` / `config set` / `config save` — SPECIFIED ONLY, NOT IMPLEMENTED

`docs/cli.md` §2 lists `config show` (dump NVS-backed config), `config set <key>
<value>`, and `config save` in the common command grammar. **None of these exist in
code as of 2026-07-07.** Verified: the only registered console commands are —

- transmitter (`console.c` registration list): `help`, `id`, `state`, `pair`, `net`,
  `can`, `sig`
- brake_light: `help`, `id`, `light`, `link`, `pair`

Also specified-but-unimplemented from the same table: `version`, `reset`, `stats`,
`log <level>`, and the generic source-override registry (`... source real|fake`).
The docs' own status note (cli.md §3, "Status" block) says the full registry is
still to come. Treat `config *` as a planned axis; don't document or script against
it as if it works.

### 7.2 Pairing persistence — IMPLEMENTED, the only NVS use today

Both transmitter and brake_light (`main/pairing.c`, identical scheme):

- NVS namespace `"chmbl"` (`NVS_NAMESPACE`), key `"peer_mac"` (`NVS_KEY_PEER`),
  value = 6-byte peer MAC blob.
- Saved on successful `pair start`; loaded at boot (`nvs_get_blob`, must be exactly
  6 bytes); erased by `pair clear` (`nvs_erase_key` + commit).
- Survives reflash; `idf.py erase-flash` wipes it (UNVERIFIED here — no device; it
  erases all flash including the NVS partition by definition).

The logger uses no NVS (verified: no `nvs` references in `logger/software/main/`).

### 7.3 Runtime-but-volatile knobs

- `net rate <hz>` (transmitter) sets the ESP-NOW heartbeat rate 1–50 Hz at runtime;
  it writes a RAM variable seeded from `CONFIG_CHMBL_NET_RATE_HZ` (`net.c` line 27)
  and is **not persisted** — reboot restores the Kconfig default.
- `net start` / `net stop` toggle the heartbeat (the intended bench method for
  exercising brake_light link-loss).

---

## 8. Config-axis status summary (one line each)

| Axis | Status |
|---|---|
| Kconfig `CHMBL_*` / `LOGGER_*` (§1) | Implemented, build-time. `CHMBL_CLI` is the dev/production gate; `*_STATE_GPIO`/`*_LIGHT_GPIO`/`CHMBL_LINK_BLINK_MS` are stand-ins pending DE-04/DE-08/DE-09/DE-10. |
| `sdkconfig.defaults*` (§2) | Implemented, committed; per-target console transport + log level (+ logger PSRAM/FATFS). |
| `tx_config_t` FSM tunables (§3.1) | **Designed only** — no firmware struct exists. |
| `BrakeTunables` (§3.2) | Implemented in the python bench only. |
| `CAN_DECODE_*` constants (§3.3) | Implemented `#define`s; `SPEED_HIST=16` is a known live bug reserved for the DE-09 campaign. |
| Tool flags (§4) | Implemented. |
| CI knobs (§6) | Implemented. |
| `config show/set/save`, `version`, `reset`, `stats`, `log` (§7.1) | **Specified in docs/cli.md only.** |
| Pairing NVS (§7.2) | Implemented on both CHMBL firmwares. |
| `net rate` (§7.3) | Implemented, volatile. |

---

## 9. How to add a config axis (checklist)

Follow `chmbl-change-control` for classification; this is the mechanical sequence
for a new firmware option:

1. **Pick the layer.** Per-board hardware wiring or feature gate → Kconfig. A value
   every build must share → `sdkconfig.defaults` (all targets) or
   `sdkconfig.defaults.<target>` (target-specific, e.g. console transport). A DE-09
   behavior tunable → the (future) `tx_config_t` path per `docs/firmware.md`, not
   Kconfig. Runtime-changeable → needs the (not yet built) config registry; don't
   invent a one-off NVS scheme without a design note.
2. **Add the Kconfig entry** in the right firmware's `main/Kconfig.projbuild`,
   inside the existing menu. Match house style: `CHMBL_`/`LOGGER_` prefix, explicit
   `range`, per-target `default X if IDF_TARGET_ESP32C3` lines where the boards
   differ, and a help text that states the default's rationale and any pairing/guard
   constraint (see `CHMBL_NET_CHANNEL` as the exemplar).
3. **Reference it as `CONFIG_<SYMBOL>`** in C. If it gates code, use
   `#if CONFIG_<SYMBOL>` and keep the disabled path building (the `CHMBL_CLI`
   pattern in `main.c`).
4. **Mirror cross-device constraints.** If the option must match on both radios
   (like `CHMBL_NET_CHANNEL`), add it to both Kconfigs with identical defaults and
   say so in both help texts.
5. **Update the doc of record in the same change** — `docs/firmware.md`,
   `docs/cli.md`, or the relevant DE doc. Docs are the spec; no silent divergence.
6. **Verify both targets still build.** Locally: `idf.py fullclean && idf.py
   set-target esp32c3 && idf.py build`, then the same for `esp32` (transmitter and
   brake_light build for both; logger only esp32). No local IDF? Push and let the
   5-row CI matrix be the gate — that is exactly what it exists for.
7. **If the axis is a new file/firmware/target**, extend the CI matrix and the
   workflow path filters in `.github/workflows/firmware-build.yml` in the same
   change.
8. **Never** add an option that can put any CAN controller in a transmitting mode
   on a vehicle, or that enables a flashing/strobing brake-bar pattern — merge
   blockers per project doctrine (see `chmbl-change-control`).

---

## When NOT to use this skill

- Installing ESP-IDF / uv / the host toolchain, or fixing build breakage →
  `chmbl-build-and-env`.
- Flashing, opening consoles, using the CLI, pairing procedure, logger field ops →
  `chmbl-run-and-operate`.
- *Why* a default is what it is (architecture rationale, invariants) →
  `chmbl-architecture-contract`; the history behind a value → `chmbl-failure-archaeology`.
- Actually implementing DE-09 / fixing `CAN_DECODE_SPEED_HIST` → `chmbl-de09-campaign`.
- Interpreting trc_viz/golden_check output → `chmbl-diagnostics-and-tooling`.
- Change classification, review gates, docs-are-spec doctrine → `chmbl-change-control`.

---

## Provenance and maintenance

All facts verified against the working tree on **2026-07-07** (branch
`claude/skill-library-continuity-mib7ua`, HEAD `9b4ff74`). Re-verify per section
from the repo root:

| Section | Re-verification command |
|---|---|
| §1 Kconfig options | `grep -n "config \|choice \|default \|range " transmitter/software/main/Kconfig.projbuild brake_light/software/main/Kconfig.projbuild logger/software/main/Kconfig.projbuild` |
| §2 defaults files | `for f in transmitter brake_light logger; do ls $f/software/sdkconfig.defaults*; done && cat */software/sdkconfig.defaults*` |
| §3.1 spec tunables | `grep -n -A20 "tx_config_t" docs/firmware.md` |
| §3.2 bench tunables | `grep -n -A20 "class BrakeTunables" tools/trc_viz.py` |
| §3.3 decode constants | `grep -n "#define" transmitter/software/main/can_decode.h` |
| §4 tool flags | `grep -n "add_argument" tools/gen_profile.py tools/golden_check.py tools/trc_viz.py` |
| §4.2 tolerances | `grep -n "_TOL" tools/golden_check.py` |
| §6 CI knobs | `grep -n "esp_idf_version\|target:\|python-version" .github/workflows/firmware-build.yml` |
| §7.1 implemented commands | `grep -rn "\.command =" transmitter/software/main brake_light/software/main` |
| §7.2 pairing NVS | `grep -n "NVS_NAMESPACE\|NVS_KEY_PEER" transmitter/software/main/pairing.c brake_light/software/main/pairing.c` |
| §7.3 net rate | `grep -n "CONFIG_CHMBL_NET_RATE_HZ" transmitter/software/main/net.c` |
| DE-09 still unimplemented? | `ls transmitter/software/state_machine 2>/dev/null \|\| echo "still absent"` and the status table in `docs/design/README.md` §3 |
