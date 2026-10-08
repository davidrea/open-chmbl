# Brake_light — software

Rider-side firmware (ESP32-C3, ESP-IDF). See
[`docs/firmware.md §2`](../../docs/firmware.md#2-brake_light-firmware-rider-side).

Responsibilities:
- Receive `chmbl_msg_t` over [ESP-NOW](../../docs/protocol.md) (encrypted, pre-paired peer).
- Validate sequence / drop stale packets.
- **Render state onto the brake bar as a plain binary GPIO output** — high when the
  brake light should be on, low when it should be off — steady, never flashing
  ([DE-04](../../docs/design/de-04-led-render.md)).
- **Link-loss failsafe**: the bar is held **steady off**, and the distinct link-lost /
  waiting indication goes to the **separate status LEDs** — never silently dark (the
  status LED and the console still say so), never a latched fake brake
  ([DE-03](../../docs/design/de-03-link-loss-failsafe.md)).
- Battery monitoring + low-battery warning *(not implemented — and not wired on the
  first-pass PCB)*.
- Button UI: power, pairing *(not implemented)*.

**Deferred, on purpose:**
- **Brightness, PWM dimming and ambient-light auto-dimming.** There is no LEDC/PWM, no
  duty cycle, no commanded brightness and no user brightness cap anywhere in the output
  path, and the board's `ISL29035` ambient-light sensor is **not read**. The per-string
  LED current is fixed in hardware by the driver's sense resistor. The decision and its
  reasoning live in [DE-04](../../docs/design/de-04-led-render.md); the deferred element
  is [DE-02](../../docs/design/de-02-auto-brightness.md) (`BL-BRT-*`), along with the
  `ambient *` and `bright cap` CLI commands.
- **The full DE-10 status-code engine** (fault-class blink codes, battery/charge codes,
  priority resolution, night-dim). What is here is a minimal link-health indication —
  see [DE-10](../../docs/design/de-10-status-indicator.md).

## Status

ESP-IDF project hosting the **developer CLI** ([DE-00](../../docs/design/README.md)), the
**ESP-NOW link** ([DE-01](../../docs/design/de-01-espnow-link.md)), the **link-loss
failsafe** ([DE-03](../../docs/design/de-03-link-loss-failsafe.md)), the **binary
brake-bar render stage** ([DE-04](../../docs/design/de-04-led-render.md)) and a minimal
**link-health status indicator** (a slice of
[DE-10](../../docs/design/de-10-status-indicator.md)).

The render stage replaces the old arrangement where the `light` CLI command poked a
single stand-in GPIO and the link watchdog **blinked that same pin** on link loss. With a
binary bar a blink both contradicts "high when braking, low otherwise" and is a flashing
brake light, which [`docs/safety-regulatory.md §1`](../../docs/safety-regulatory.md)
forbids — so link-loss indication moved to the status LEDs and the bar is now held
steady.

**Not yet run on real hardware.** The first-pass PCB exists but has not been built, and
four of its MCU-side nets (both LED-driver enables, both status-LED feeds) are **not
wired yet** — so those pins are Kconfig symbols with **provisional defaults**. See
[`../hardware/README.md`](../hardware/README.md#pin-map) for which assignments are
verified from the netlist and which are provisional.

```
software/
├── CMakeLists.txt          top-level ESP-IDF project
├── sdkconfig.defaults      committed defaults (common)
├── sdkconfig.defaults.esp32c3   product target — USB Serial/JTAG console
├── sdkconfig.defaults.esp32     interim dev hardware — UART console
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild   CLI / bar + status pins / anti-strobe / link options
│   ├── console.h
│   ├── main.c              app_main: bring up render, status, link; start the console
│   ├── console.c           REPL bootstrap (USB Serial/JTAG, UART fallback)
│   ├── protocol.h          ESP-NOW wire format (hand-synced with the transmitter copy)
│   ├── render_core.[ch]    DE-04 pure core: state→binary output + anti-strobe dwell
│   ├── render.[ch]         DE-04 platform: the two driver-enable GPIOs + render task
│   ├── status.[ch]         DE-10 slice: D13/D14 status LEDs from link health
│   ├── pairing.[ch]        ESP-NOW pairing ritual + NVS peer store
│   ├── net.[ch]            ESP-NOW receive → link watchdog
│   ├── link.[ch]           DE-01 seq/timestamp + DE-03 failsafe; publishes to render
│   ├── cmd_system.c        `id`     — chip unique ID (base MAC) + chip info
│   ├── cmd_light.c         `light`  — bench override of the brake bar
│   ├── cmd_render.c        `render` — view the bar's render output
│   ├── cmd_pair.c          `pair`   — manage the ESP-NOW peer
│   ├── cmd_link.c          `link`   — ESP-NOW link health
│   └── cmd_ind.c           `ind`    — status-indicator LEDs
└── test_host/              host test for render_core (no ESP-IDF, no hardware)
    ├── CMakeLists.txt
    └── render_core_test.c
```

### Output path (DE-04)

The render task is the **single writer** of the brake-bar pins:

```
 ESP-NOW rx ─▶ link.c ──(effective state, 10 Hz)──▶ render.c ─▶ EN_A ─▶ U3 ─▶ string A
                 │                                   ▲            └─▶ EN_B ─▶ U6 ─▶ string B
                 │                                   │
                 │                      `light on|off|toggle|auto` (bench override)
                 └──(link health)──▶ status.c ─▶ D13 (RED) / D14 (GRN)
```

| Effective state | Bar output | Why |
|-----------------|-----------|-----|
| `ST_BRAKE` | **pins high** (lit) | braking / stopped |
| `ST_OFF` | pins low (dark) | not braking |
| `ST_DECEL` | **pins high** (lit) | **Reserved**, never emitted by the TX FSM ([`protocol.md §2`](../../docs/protocol.md)). Mapped to ON rather than left undefined: with brightness deferred there is no middle tier, and the fail-safe reading of "the bike is slowing" is a lit light. |
| any other value | pins low | an off-protocol value must not latch a fake `BRAKE` |
| link `LOST` / `WAITING` | pins low (**steady**) | the honest "I don't know" moves to the status LED; the bar is never blinked |

Both driver enables are driven **together** as one logical output, so the bar cannot
light half-on. **Anti-strobe (BL-RND-2)** is asymmetric: once lit the bar stays lit for
at least `CHMBL_RENDER_MIN_ON_MS` (600 ms), and once dark it stays dark for at least
`CHMBL_RENDER_MIN_OFF_MS` (150 ms) before it may relight. So a brake-on edge from a
settled-dark bar is immediate, and even a state flapping every render tick is bounded to
one on→off→on cycle per 750 ms. The primary anti-oscillation guards are still upstream
(the TX FSM's hysteresis, decel debounce and dwell floor — see
[`docs/firmware.md`](../../docs/firmware.md#braking-state-machine)); this floor is defence
in depth.

### Status indicator (DE-10 slice)

| Link status | Indicator |
|-------------|-----------|
| `UP` | GRN (D14) steady |
| `WAITING` (booted, no packet yet) | GRN slow blink |
| `LOST` | RED (D13) slow blink |

Half-period `CHMBL_STATUS_BLINK_MS` (500 ms), Kconfig-floored at 150 ms so the indicator
itself can't strobe. Fault classes, battery/charge codes, priority resolution and
night-dim are deferred to the full DE-10.

### Targets, console transport & pins

The firmware builds for two targets; pick one with `idf.py set-target`. The code is
identical — only the console transport (chosen by the per-target
`sdkconfig.defaults.<target>`) and the default pin numbers differ.

| Target | Status | Console transport | Connect | JTAG on same cable |
|--------|--------|-------------------|---------|--------------------|
| `esp32c3` | product target | built-in **USB Serial/JTAG** (native USB GPIO18/19) | `/dev/ttyACM*`, any baud | yes |
| `esp32` | interim dev hardware | **UART0** (GPIO1 TX / GPIO3 RX) via onboard USB-UART bridge | `/dev/ttyUSB*`, 115200 | no (needs external probe) |

On the C3 the native USB port means no external USB-TTL adapter and console + debug over
one cable. The classic ESP32 has no USB peripheral, so the shell goes out the TX/RX pins
through the board's bridge chip.

Output pins are all Kconfig, under *Brake_light configuration*:

| Kconfig | `esp32c3` default | `esp32` default | Notes |
|---------|------------------|-----------------|-------|
| `CHMBL_BAR_EN_A_GPIO` | **5** *(provisional)* | 2 | String A driver enable (U3 `CTRL`). Active high. `esp32` default is the DevKitC onboard LED, so the bar's on/off is visible on the bench board. |
| `CHMBL_BAR_EN_B_GPIO` | **6** *(provisional)* | −1 | String B driver enable (U6 `CTRL`). **−1 = string not fitted**, which is how the one-LED dev board runs. |
| `CHMBL_STATUS_LED_RED_GPIO` | **7** *(provisional)* | 4 | D13 via R21. Active high. |
| `CHMBL_STATUS_LED_GRN_GPIO` | **10** *(provisional)* | 5 | D14 via R20. Active high. |

*(provisional)* = the corresponding pad is **unconnected in the committed first-pass
schematic/PCB**, so the default is a firmware-side proposal pending a schematic review,
not a verified assignment. The pins that *are* verified from the netlist (I²C to the
`ISL29035`, the SW1 button, USB, the console test points) are tabulated in
[`../hardware/README.md`](../hardware/README.md#pin-map). On a C3-DevKitM bench board set
`CHMBL_BAR_EN_A_GPIO=8` to drive the onboard LED instead.

Timing knobs: `CHMBL_RENDER_TICK_MS` (16), `CHMBL_RENDER_MIN_ON_MS` (600),
`CHMBL_RENDER_MIN_OFF_MS` (150), `CHMBL_STATUS_BLINK_MS` (500),
`CHMBL_LINK_TIMEOUT_MS` (300), `CHMBL_LINK_TICK_MS` (100).

> **Portability:** ESP-IDF abstracts the drivers, so application code is shared.
> The few genuine target divergences to keep in mind: don't pin tasks to core 1
> (the C3 is single-core); deep-sleep wake config differs (Xtensa `ext0/ext1`
> vs. RISC-V GPIO wake — relevant to TX power mgmt, DE-06); and pin numbers stay
> in Kconfig since the usable GPIO maps differ.

Commands so far (`help` lists them):

| Command | Purpose |
|---------|---------|
| `help` | List commands. |
| `id` | Chip unique ID (base MAC), model/revision, IDF version. |
| `light [on\|off\|toggle\|auto]` | Bench override of the brake bar's binary output; `auto` releases it back to the link-driven state (no arg = show). |
| `render [show]` | Effective state, the binary output on the driver-enable pins, anti-strobe floors/holds, override status. |
| `pair start\|status\|clear` | Manage the ESP-NOW peer. |
| `link [show]` | Link status, last state/seq, last-rx age vs. timeout, rx/drop counters. |
| `ind [show]` | Status-indicator LEDs: status being shown, resulting pattern, pin state. |

`ambient *`, `bright cap`, `batt *`, `in *` and `led test` from
[`docs/cli.md §4`](../../docs/cli.md#4-brake_light-cli) are not implemented yet; the
brightness-related ones are **deferred** with DE-02 rather than pending.

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/)
(v5.3+). With the IDF environment sourced:

```bash
cd brake_light/software
idf.py set-target esp32      # or esp32c3 (re-run set-target to switch)
idf.py menuconfig            # optional: set the bar / status-LED pins for your board
idf.py build
idf.py flash monitor         # on attached hardware (Ctrl-] to exit the monitor)
```

`idf.py monitor` attaches to whichever console the target selected; type `help`
at the `chmbl>` prompt. To exercise the bar with no transmitter present:
`light on`, `light off`, then `render show` to confirm what reached the pins (including
any anti-strobe hold), and `light auto` to hand the bar back to the link.

## Host test

The render stage's pure core — the state→binary-output map and the anti-strobe dwell
floor — builds and runs on the host, no ESP-IDF and no hardware:

```bash
cd brake_light/software/test_host
cmake -B build && cmake --build build
./build/render_core_test
```

It proves `ST_BRAKE`/`ST_DECEL` → on and `ST_OFF`/unknown → off, that a brake-on edge
from a settled-dark bar is immediate, that both dwell floors defer rather than drop a
change, that a transient inside a floor never reaches the pin, and that a state flapping
every tick cannot strobe the bar.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`) for
both `esp32c3` and `esp32` on every push/PR that touches the firmware, and runs the
`test_host` render-core test on the host.
