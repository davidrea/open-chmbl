---
name: chmbl-run-and-operate
description: >
  Load this skill when you need to RUN or OPERATE open-chmbl hardware or its data
  pipeline: flashing firmware and attaching a serial console, using the `chmbl>`
  developer CLI (which commands actually exist vs. the docs/cli.md spec), pairing a
  transmitter and brake_light on the bench, exercising link-loss, operating the
  logger in the field (button, LED codes, .trc files), or moving a new CAN capture
  into analysis/validation. Do NOT load it for setting up the toolchain or building
  (chmbl-build-and-env), for interpreting trc_viz/golden_check output in depth
  (chmbl-diagnostics-and-tooling), for debugging failures (chmbl-debugging-playbook),
  or for config-option details (chmbl-config-and-flags).
---

# Running and operating open-chmbl devices and the data pipeline

Everything device-side in this skill is derived from repo source and READMEs as of
2026-07-07 — no hardware or ESP-IDF is available in a typical AI session, so no
`idf.py` output here was executed live. Facts are cited to files; anything not
verifiable from the repo is marked UNVERIFIED.

Jargon used below, once:

- **CAN** — Controller Area Network, the motorcycle's shared message bus.
  **TWAI** is Espressif's name for its on-chip CAN controller. **Listen-only** =
  the controller never transmits or even ACKs frames (the project's golden rule,
  docs/can-profiles.md §1).
- **ESP-NOW** — Espressif's connectionless 2.4 GHz Wi-Fi-frame protocol used for the
  encrypted transmitter→brake_light heartbeat.
- **NVS** — Non-Volatile Storage, the ESP32's key-value flash store (persists the
  pairing peer across boots).
- **Kconfig / menuconfig** — ESP-IDF's compile-time configuration system
  (`idf.py menuconfig`); options are `CONFIG_...` symbols.
- **`.trc`** — PCAN-format ASCII CAN trace file (version 2.1), one line per frame.
- **FSM** — finite state machine (the braking logic, DE-09 — designed, not yet in
  firmware).

## 1. Flash and connect

Three ESP-IDF (release-v5.3) firmware projects, one per device:

| Device | Directory | Targets | Console transport |
|---|---|---|---|
| transmitter (bike side) | `transmitter/software/` | `esp32c3` (product), `esp32` (interim dev) | C3: USB Serial/JTAG; esp32: UART0 |
| brake_light (helmet side) | `brake_light/software/` | `esp32c3` (product), `esp32` (interim dev) | same as transmitter |
| logger (ride-capture rig) | `logger/software/` | `esp32` only (ESP-WROVER-KIT v4.1) | UART0 |

Per-device flash sequence (transmitter shown; substitute the directory):

```bash
cd transmitter/software
idf.py set-target esp32c3      # or esp32; re-run set-target to switch
idf.py build
idf.py flash monitor           # Ctrl-] exits the monitor
```

Console connection facts (from `*/software/README.md` tables and the committed
`sdkconfig.defaults.<target>` files):

| Target | Port on host | Baud | Notes |
|---|---|---|---|
| `esp32c3` | `/dev/ttyACM*` | any | Built-in USB Serial/JTAG on native USB pins GPIO18/19 — virtual COM port, no USB-TTL adapter, JTAG debug on the same cable. |
| classic `esp32` | `/dev/ttyUSB*` | 115200 | UART0 (GPIO1 TX / GPIO3 RX) through the dev board's USB-UART bridge chip. |

The transport is selected per target by `sdkconfig.defaults.esp32c3`
(`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`) vs `sdkconfig.defaults.esp32`
(`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`); application code is identical.

On transmitter and brake_light, `idf.py monitor` lands you at a **`chmbl>`**
prompt (an `esp_console` REPL, prompt set in each device's `main/console.c`).
Type `help` to list registered commands. The CLI is compiled in only when
`CONFIG_CHMBL_CLI=y` (default y; disabled for production builds). The logger has
**no CLI** — its console is a one-way operations log (`ui_log.c`).

Build/toolchain setup, targets, and CI build matrix are owned by
`chmbl-build-and-env`; Kconfig option details by `chmbl-config-and-flags`.

## 2. Developer CLI — implemented vs. specified

docs/cli.md is the **spec** (target state, source-override model, full grammar).
The firmware implements a subset. Per project doctrine, docs are the spec and code
follows — the gap below is expected staging, not divergence to "fix" silently
(see `chmbl-change-control`).

### 2.1 Commands that exist in firmware (as of 2026-07-07)

Verified by reading every `cmd_*.c` under `transmitter/software/main/` and
`brake_light/software/main/`, plus the `cmd_*_register()` calls in each
`console.c`.

**Transmitter** (`help`, `id`, `state`, `pair`, `net`, `can`, `sig`):

| Command | Real syntax | Behavior (source file) |
|---|---|---|
| `help` | `help` | esp_console built-in list. |
| `id` | `id` | Chip model/rev/cores, base MAC (= unique ID), IDF version (`cmd_system.c`). |
| `state` | `state` \| `state off` \| `state brake` | Show/force the stand-in emitted braking state; drives indicator GPIO (`CONFIG_CHMBL_STATE_GPIO`, default GPIO8 on C3 / GPIO2 on esp32). DECEL is a reserved wire state, never emitted. `net.c` broadcasts this value in every heartbeat (`cmd_state.c`). |
| `pair` | `pair start` \| `pair status` \| `pair clear` | Manage the ESP-NOW peer (see §3) (`cmd_pair.c`). |
| `net` | `net show` \| `net rate <1-50>` \| `net send` \| `net start` \| `net stop` | Heartbeat TX control: peer, running state, rate (boot default `CONFIG_CHMBL_NET_RATE_HZ` = 20 Hz), seq, ok/fail counters. Heartbeat runs by default at boot (`s_running = true` in `net.c`) (`cmd_net.c`). |
| `can` | `can` \| `can show` \| `can replay decel` | RX diagnostics + bench replay (see §6) (`cmd_can.c`). |
| `sig` | `sig` \| `sig show` \| `sig set <name> <value\|na>` \| `sig ramp wheel <mph/s> [until <mph>]` \| `sig source can\|fake` | Show/fake decoded CAN signals (`cmd_sig.c`). |

`sig` details (from `cmd_sig.c` / `can_rx.c`):

- Signal names: `wheel_speed`, `accel`, `clutch_pulled`, `gear`,
  `wheel_speed_rear`, `throttle_pct`, `rpm`, `rpm_ecu`, `side_stand_up`,
  `engine_cutoff`. Aliases for `set`/`ramp`: `wheel`→wheel_speed,
  `clutch`→clutch_pulled, `throttle`→throttle_pct.
- `sig set gear N` means neutral (gear 0). `sig set <name> na` marks the fake
  unavailable.
- `sig set` while source is `can` stores the fake but warns — run
  `sig source fake` to use it. `sig ramp` switches source to fake itself and
  updates every 50 ms; default `until` is 0 mph for negative rates, 100 for
  positive. Ramping is the way to exercise derived `accel` (a constant
  `sig set wheel` gives accel ≈ 0). `sig source can` also stops any ramp.
- `sig show` prints value/unit and validity `yes` / `STALE` / `no`.
- Fakes affect only the decoded-signal view; nothing is ever transmitted on CAN.

**Brake_light** (`help`, `id`, `light`, `pair`, `link`):

| Command | Real syntax | Behavior (source file) |
|---|---|---|
| `help` | `help` | Built-in. |
| `id` | `id` | Identical to transmitter (`cmd_system.c` is duplicated verbatim). |
| `light` | `light` \| `light on` \| `light off` \| `light toggle` | Drive/read the stand-in brake-light GPIO (`CONFIG_CHMBL_LIGHT_GPIO`, default GPIO8 on C3 / GPIO2 on esp32). Note the link watchdog also drives this pin (§3.2) and will overwrite manual settings within one blink period while the link task runs (`cmd_light.c`, `link.c`). |
| `pair` | `pair start` \| `pair status` \| `pair clear` | Same code as transmitter (`cmd_pair.c` identical). |
| `link` | `link` (any args ignored — `link show` also works) | Link status WAITING/UP/LOST, last state/seq, last-rx age vs timeout (`CONFIG_CHMBL_LINK_TIMEOUT_MS`, default 300 ms), rx ok / dropped-stale / dropped-unpaired counters (`cmd_link.c`). |

### 2.2 docs/cli.md spec — commands that are SPEC-ONLY (not in any cmd_*.c)

Common grammar: `version`, `reset`, `config show|set|save`, `stats`,
`log <level>`.
Transmitter: `state show` / `state force OFF|BRAKE|auto` (implemented form is
`state [off|brake]` with no `auto`), `can replay <name>` for arbitrary stored
captures (only the built-in `decel` vector exists — §6), `power show`.
Brake_light: the entire `in`, `ambient`, `batt`, `render`, `led test`,
`bright cap`, and `ind` domains — these await DE-02/03/04/10 hardware and the
generic source-override registry (docs/cli.md §5 status note).

If you add a command, register it in the device's `console.c` and update
docs/cli.md in the same change (docs-are-spec rule).

## 3. Pairing runbook (bench)

Pairing (DE-01) is CLI-triggered because no dev board has the production pairing
button wired. Both devices share an identical `pairing.c` (verified by diff).
Mechanism: each side broadcasts an unencrypted `MSG_PAIR` announcement every
200 ms and listens; the first foreign MAC heard becomes the peer, registered as an
**encrypted** ESP-NOW peer (compiled-in dev PMK/LMK — a placeholder; per-pair key
exchange is an open DE-01 item) and persisted to NVS so later boots re-pair
silently.

Steps:

1. Flash both boards and open a monitor to each (two terminals, two ports).
2. On either board: `pair start`. It prints
   `pairing: broadcasting, waiting up to 15s for a peer ...` and dots.
   The window is `CONFIG_CHMBL_PAIR_TIMEOUT_S` (default **15 s**, range 3–60).
3. Within that window, on the other board: `pair start`.
4. Both sides should print `pairing: paired with <mac>`. Verify with
   `pair status` on each — the MACs must be each other's.
5. Persistence check (optional): power-cycle either board; boot log shows
   `restored peer <mac>` and `pair status` still reports paired.
6. To forget: `pair clear` (deletes the peer and erases it from NVS).

Why success is symmetric now: commit `d628425` fixed an asymmetric race — the
first board often heard the second board's very first announcement within
milliseconds and went silent immediately, so the second board could time out
having never heard a packet, even though the first "paired". The fix: after
discovering a peer, `pairing_start()` keeps broadcasting for a grace period
(`PAIR_GRACE_SENDS` = 8 sends × 200 ms ≈ 1.6 s, printing
`pairing: found a peer, exchanging a few more announcements...`) before
adopting the peer. If you ever see one-sided pairing again, suspect a
regression here (see `chmbl-failure-archaeology`).

Both boards must share `CONFIG_CHMBL_NET_CHANNEL` (default 6) — there is no
channel-agreement handshake yet.

### 3.1 Verify the live link

With both paired, the transmitter heartbeats automatically (20 Hz default):

- Transmitter: `net show` → `state : running`, sent-ok counter climbing.
- Brake_light: `link` → `status : UP`, last-rx age well under the 300 ms timeout.
- Transmitter: `state brake` → the brake_light's stand-in LED goes solid on
  (link watchdog mirrors received state onto the light). `state off` turns it off.

### 3.2 Exercise link-loss (DE-03 placeholder)

`net stop` / `net start` on the transmitter is the documented, intended way to
exercise the brake_light's link-loss behavior from the bench (docs/cli.md §5,
docs/design/de-03-link-loss-failsafe.md — verified both).

1. Transmitter: `net stop` → prints `net: stopped (heartbeat paused)`.
2. After `CONFIG_CHMBL_LINK_TIMEOUT_MS` (300 ms) the brake_light's `link` shows
   `status : LOST`, and its stand-in LED **blinks** at
   `CONFIG_CHMBL_LINK_BLINK_MS` half-period (250 ms) instead of tracking state —
   the placeholder "distinct indication, never silently dark" failsafe
   (`link.c`). The same blink appears in `WAITING` (no packet since boot).
3. Transmitter: `net start` → within one heartbeat the brake_light returns to
   `UP` and resumes mirroring the received state. A latched fake BRAKE must
   never survive a loss — that is the DE-03 invariant.

## 4. Logger field operation

The logger (`logger/`) is the DE-07 ride-capture rig: an ESP-WROVER-KIT v4.1 +
external 3.3 V CAN transceiver (SN65HVD230) that records ALL bus traffic
(no filtering), listen-only, to PCAN `.trc` v2.1 files on its microSD.

Wiring (defaults from `logger/software/main/Kconfig.projbuild` and
`logger/software/README.md`; all changeable under menuconfig → *CAN logger
configuration*):

| Signal | Default GPIO | Wiring |
|---|---|---|
| Start/stop pushbutton | **IO33** | Momentary switch, other side to GND; internal pull-up, active-low, debounced by the `iot_button` component. |
| CAN TX | **IO26** | → SN65HVD230 **TXD** (TWAI claims the pin even in listen-only; it never drives the bus dominant). |
| CAN RX | **IO27** | ← SN65HVD230 **RXD** |
| Transceiver | — | CAN-H/CAN-L → bus (the bike's diagnostic port), VCC 3V3, GND common. |
| Status LED | **GPIO0** | Onboard red LED die (the only RGB leg not stolen by microSD/LCD). Polarity flag `LOGGER_STATUS_LED_ACTIVE_LOW` (default n). |

Free-pin constraint if you re-pin: keep clear of LCD `5/18/19/21/22/23/25`,
microSD `2/4/12/13/14/15`, PSRAM `16/17`, console `1/3`.

Bit rate: menuconfig choice `LOGGER_CAN_BITRATE` — 125k/250k/500k/1M, default
**500 kbit/s** (matches the reference Triumph Speed 400 bus). Mode:
`LOGGER_CAN_LISTEN_ONLY` default y. RX-to-writer queue depth
`LOGGER_RX_QUEUE_LEN` default 1024 frames.

### 4.1 Pushbutton semantics (from `logger_main.c`)

One button toggles recording:

- **Start** (press while idle): flushes stragglers, opens the next `N.trc`
  (`/sdcard/N.trc`; N = highest existing number + 1, scanned at boot), writes the
  header, allocates a 32 KiB write buffer (batched SD writes — the `687d400`
  frame-drop fix), **then** starts the TWAI controller and begins queuing frames.
  The controller runs only while recording (the `b860f06` idle-watchdog fix).
- **Stop** (press while recording): stops accepting frames, stops TWAI, drains
  the queue into the file, appends a self-documenting footer
  `;dropped-frames: N (RX-to-writer queue overflow)` (0 on a clean capture;
  absence of the line means an older/truncated file), closes the file.

Everything is narrated on the serial console (`idf.py monitor`, 115200,
`/dev/ttyUSB*`): booted, CAN mode/rate, filesystem mounted, N files listed,
next file number, button pressed, opened file / recording started, recording
stopped / file closed with frame count and drops.

### 4.2 Status LED codes (from `status_led.c`)

| Pattern | Meaning | Timing |
|---|---|---|
| Slow heartbeat blink | Idle/ready, not recording | 60 ms on / 1940 ms off |
| Solid on | Recording | — |
| Fast blink | Fatal error (SD mount fail, file-open fail, CAN start fail, "cannot record: no microSD") | 100 ms on / 100 ms off |

### 4.3 On-bike safety (docs/can-profiles.md §3, binding)

- Listen-only, always, on a real bike. Never switch `LOGGER_CAN_LISTEN_ONLY` off
  except on a two-node bench where the single peer needs an ACK.
- The logger is read-only and powered independently. Mount it securely, **start
  logging before riding, never operate it while moving**. Prefer a closed course
  or a helper for deliberate brake-and-coast runs.
- Power-loss / card-removal robustness is intentionally out of scope — stop
  recording before pulling power or the card.
- `$STARTTIME` in the `.trc` header is a fixed placeholder (no RTC); only
  relative frame time offsets are meaningful, which is all offline
  decode/replay uses.

## 5. Data and artifact conventions

Where things live (verified in-tree as of 2026-07-07):

| Artifact | Location | Status |
|---|---|---|
| Committed ride capture | `logger/40mph_drive_cycle.trc` (~220 s Speed 400 ride) | The only capture in the repo. |
| Bench captures `throttle.trc`, `wheel.trc` | referenced by docs/can-profiles.md | **NOT committed** — decode-table citations only. |
| Decoded-signal overlay | `logger/40mph_drive_cycle_decoded.png` | Committed (`f8ba271`, first profiling ride); linked from docs/can-profiles.md decode table. |
| FSM dry-run plot | `logger/40mph_drive_cycle_fsm.png` | Committed (`8192663`, FSM dry-run doc update); not linked from any doc. Generation tool UNVERIFIED — presumed trc_viz screenshot. |

**Known discrepancy (do not paper over):** docs/can-profiles.md §6 and the tail of
`transmitter/software/README.md` say raw captures go under
`transmitter/software/captures/` — that directory does not exist, and the
committed capture actually lives in `logger/`. Follow the existing practice
(`logger/`) or fix the docs via change control; don't silently invent a third
location.

Flow for a NEW capture, `<name>.trc`, copied off the logger's microSD:

1. **Anonymize and commit** the raw `.trc` (project rule: captures stay in the
   repo so others can re-derive; docs/can-profiles.md §6).
2. **Visualize / calibrate:**
   `uv run tools/trc_viz.py logger/<name>.trc` (interactive gauges + FSM
   sliders; needs a display) or
   `uv run tools/trc_viz.py logger/<name>.trc --headless-check` (decode + stats,
   no GUI). Interpretation of the output is owned by
   `chmbl-diagnostics-and-tooling`.
3. **Validate the decoder** against it:
   `python3 tools/golden_check.py --trc logger/<name>.trc`
   (defaults: harness `transmitter/software/test_host/build/trc_replay`, DBC
   `profiles/triumph_tr.dbc` — build the host harness first; see
   `chmbl-build-and-env`). Evidence standards live in `chmbl-validation-and-qa`.

## 6. Bench replay: `can replay` — docs vs. implementation

docs/cli.md specifies `can replay <name>` = "feed a stored capture through the
decoder". The implementation (`transmitter/software/main/cmd_can.c`, verified) is
narrower: **only `can replay decel` exists** — a built-in synthetic vector, not a
stored-capture player. It holds 40 mph for 1 s, then brakes at −12 mph/s to a
stop with clutch pulled below 8 mph, frames every 20 ms, packing engineering
values through the active profile's real bit layouts (`can_sig_pack`) into an
**offline decoder instance** — the live decode is untouched and nothing is
transmitted. It prints t / wheel / accel / gear / clutch every 250 ms and ends
with `can: replay complete`. Any other vector name → `can: unknown vector`.
Replaying real `.trc` files on-device is spec-only; the host-side equivalent is
`trc_replay` + `golden_check.py` (§5).

`can show` prints the active profile name, bit rate (listen-only), driver
running state, rx/decoded/dropped/bus-error counters, last-rx age, and the
profile's CAN IDs — the first thing to check when wired to a live bus.

## When NOT to use this skill

- Installing ESP-IDF, building firmware or the host harness, CI matrix →
  `chmbl-build-and-env`.
- Meaning of Kconfig options / tunables beyond the operational defaults quoted
  here → `chmbl-config-and-flags`.
- Something doesn't work (build errors, no frames, link flapping, SD drops) →
  `chmbl-debugging-playbook`; the history behind past failures →
  `chmbl-failure-archaeology`.
- Deep interpretation of trc_viz / golden_check / gen_profile output →
  `chmbl-diagnostics-and-tooling`; what counts as passing evidence →
  `chmbl-validation-and-qa`.
- CAN/DBC/.trc/ESP-NOW theory → `chmbl-can-reference`.
- Implementing the DE-09 braking FSM → `chmbl-de09-campaign`.

## Provenance and maintenance

All facts verified against the repo on 2026-07-07 (branch
`claude/skill-library-continuity-mib7ua`); device behavior derived from source,
not executed. Re-verify before trusting:

- Implemented CLI commands: `grep -l cmd_ transmitter/software/main/*.c brake_light/software/main/*.c` and read the `cmd_*_register` calls in each `main/console.c`.
- CLI spec / spec-only commands: `sed -n '45,130p' docs/cli.md` (§2–§5).
- Console transports & prompt: `cat */software/sdkconfig.defaults.*`; `grep -n prompt */software/main/console.c`.
- Pairing window, channel, rate, link timeouts: `grep -n "default" transmitter/software/main/Kconfig.projbuild brake_light/software/main/Kconfig.projbuild`.
- Pairing behavior incl. grace period: `sed -n '200,265p' transmitter/software/main/pairing.c`; race-fix story: `git show d628425`.
- Devices' pairing code still identical: `diff transmitter/software/main/pairing.c brake_light/software/main/pairing.c`.
- Logger pins/bit-rate/LED: `cat logger/software/main/Kconfig.projbuild`; LED timings: `sed -n '42,76p' logger/software/main/status_led.c`; button/file semantics: `sed -n '166,300p' logger/software/main/logger_main.c`.
- `can replay` implementation: `sed -n '55,125p' transmitter/software/main/cmd_can.c`.
- Capture inventory & the captures/ discrepancy: `ls logger/ transmitter/software/captures 2>&1; grep -n captures docs/can-profiles.md transmitter/software/README.md`.
