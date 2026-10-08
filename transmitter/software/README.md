# Transmitter — software

Bike-side firmware for the **logger PCB** ([`logger/hardware/`](../../logger/hardware) —
ESP32-S3-WROOM-1-N4, onboard TCAN330 transceiver, **ESP-IDF**). There is no separate
transmitter board: the transmitter *is* the logger board with the microSD slot (J5)
and, in the final build, the remote pod connector (J4) left unpopulated — see
[`docs/hardware.md §1`](../../docs/hardware.md#1-transmitter-bike-side) and
[`docs/firmware.md §1`](../../docs/firmware.md#1-transmitter-firmware-bike-side).

> **Target:** `esp32s3`. The earlier `esp32c3` / classic-`esp32` dev boards are **no
> longer supported** — the pin map, the native-USB console and the three-LED indicator
> are all specific to this board, exactly as for the logger firmware. For the dev-board
> version, see the history before the port.

Responsibilities:
- **TWAI (CAN 2.0)** in **listen-only** mode, filtered to the
  [bike profile](../../docs/can-profiles.md) IDs, with the transceiver held silent in
  hardware (below).
- Decode `wheel_speed`, `throttle_pct`, `rpm`, `clutch_pulled`, `gear`/`neutral`
  ([DE-08](../../docs/design/de-08-can-decode.md)) and derive acceleration from wheel
  speed. The reference bus carries **no brake-switch bit**.
- Run the [braking state machine](../../docs/design/de-09-brake-decel-logic.md) at
  50 Hz and drive the **brake-light output**.
- Broadcast the resulting state over [ESP-NOW](../../docs/protocol.md) at 20–50 Hz.

## The brake light, right now

The state machine's output drives **IO18** — on a logger that is the operator's pod LED
on J4; here it is the brake light. With J4 populated on the bench board, **that pod LED
is the brake light under test**: it lights exactly when the FSM says BRAKING or STOPPED,
with no pattern of its own, so what you see on the bench is the decision the radio will
eventually carry. The same state already goes out over ESP-NOW (`net show`), so pointing
it at a real rider-side light later changes nothing in this firmware.

## Silent mode is enforced in hardware

`R16` (10K) pulls the TCAN330's **S** pin up to 3V3, and **S high = silent**: the
transceiver receives but its driver is disabled, so it cannot put a dominant bit — not
even an ACK — onto the bus.

That pin is **GPIO35**, which is **U1 pin 28**. (Easy to transpose; the netlist table in
[`logger/hardware/BRINGUP.md §0`](../../logger/hardware/BRINGUP.md) is authoritative.)

The firmware configures GPIO35 as a **high-impedance input with both internal pulls
disabled**, and never drives it, so silence is the power-on default and the failure-safe
state: it holds through reset, through boot before `app_main` runs, and through a
firmware crash. The internal pull-up is left off deliberately — it would hold S high even
if `R16` were missing, hiding exactly the board fault that would let the transmitter talk
on a live bus. At boot the pin is read back; a low reading is logged loudly and lights
the fault lamp. The TWAI controller is **also** held in listen-only mode. Both halves of
the golden rule — never disturb a live vehicle bus — are in force, and neither depends
on the other. Identical treatment to the logger firmware on this board.

## Indicators

| LED | GPIO | Shows |
|-----|:----:|-------|
| External indicator (J4 pin2, the pod LED) | **IO18** | **the brake light** — on = BRAKING or STOPPED |
| **D5** green (onboard) | **IO2** | bus liveness: heartbeat → no CAN traffic · **inverted** heartbeat → traffic present |
| **D6** red (onboard) | **IO1** | fault lamp: **2 Hz, 50% duty** if the silent pin reads low or TWAI fails to start; dark otherwise |

"Heartbeat" is a double thump (lub-dub) on a 2 s period; **inverted heartbeat** is that
waveform negated — mostly lit, two brief dropouts. Timings match the logger's indicator,
so the same board means the same thing whichever image is flashed.

## Hardware / wiring

As-built pin map, from the exported KiCad netlist — the same table as
[`logger/hardware/BRINGUP.md §0`](../../logger/hardware/BRINGUP.md). Do **not** take pin
numbers from the older `logger_sch_revA.pdf` or from `logger/hardware/README.md`; both
are stale.

| Signal | GPIO | Notes |
|--------|:----:|-------|
| CAN TXD | **IO21** | → U2 (TCAN330) pin1; `R15` pull-up |
| CAN RXD | **IO47** | ← U2 pin4 |
| CAN S (silent) | **IO35** | U1 **pin 28**; `R16` pull-up → **hi-z, defaults silent** |
| Brake-light output | **IO18** | Q1 gate via `R3`, low-side to J4 pin2 |
| Status LED D5 (green) | **IO2** | via `R20`, active-high |
| Status LED D6 (red) | **IO1** | via `R21`, active-high |

All of these are configurable in `menuconfig` (*Transmitter configuration*). The logger's
microSD pins (IO8–IO12, IO17, IO48) and its start/stop button (IO6) are **untouched** by
this firmware — the transmitter populates neither.

> The DE-09 tunables are deliberately **not** Kconfig options: they are floats that get
> swept, not build-time choices. They default to the calibrated values in
> `brake_fsm.h` and are adjustable live with `state tune` (below), which is the same
> loop [`tools/trc_viz.html`](../../tools/trc_viz.html) offers offline.

## Structure

```
software/
├── CMakeLists.txt          top-level ESP-IDF project
├── sdkconfig.defaults      committed defaults (generated sdkconfig is gitignored)
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild    pin map, CLI toggle, ESP-NOW channel/rate
│   ├── main.c               app_main: indicators, link, CAN, FSM, console
│   ├── bike_profile*.[ch]   generated CAN profile data (tools/gen_profile.py)
│   ├── can_decode.[ch]      pure decode + derived acceleration (host-testable)
│   ├── can_rx.[ch]          TWAI listen-only RX task, silent pin, signal snapshot
│   ├── brake_fsm.[ch]       DE-09 state machine — pure, host-testable
│   ├── brake_ctl.[ch]       50 Hz tick, brake-light output, force override, tunables
│   ├── status_led.[ch]      the three LEDs
│   ├── pairing.[ch], net.[ch]   ESP-NOW peer + heartbeat (DE-01)
│   ├── console.c            REPL bootstrap (native USB Serial/JTAG)
│   └── cmd_*.c              one file per console command
└── test_host/              host harnesses for the pure cores (no ESP-IDF)
```

`can_decode.c`, `brake_fsm.c` and the profile data are deliberately
platform-independent (no IDF headers) so they can be host-tested, per
[`docs/firmware.md §4`](../../docs/firmware.md#4-build--toolchain).

## Console

Over the S3's native USB Serial/JTAG (`idf.py monitor`) — this board has no UART bridge,
so that is the only console it has. Type `help` at the `chmbl>` prompt.

| Command | Purpose |
|---------|---------|
| `help` | List commands. |
| `id` | Chip unique ID (base MAC), model/revision, IDF version. |
| `state [show]` | FSM state, the inputs it saw, its timers, and the emitted output state. |
| `state force off\|brake\|auto` | Override the emitted state, or hand it back to the FSM. |
| `state tune [<name> <value>]` | List or set a DE-09 tunable, live. |
| `sig show \| set \| ramp \| source` | Inspect or fake decoded signals — `sig source fake` then `sig ramp wheel -12` drives a synthetic stop straight through the real FSM. |
| `can show \| can replay decel` | CAN RX diagnostics; replay a synthetic vector through an offline decoder. |
| `pair …`, `net …` | ESP-NOW peer and heartbeat (DE-01). |

Bench recipe for the light, no bus required:

```
sig source fake
sig set gear 3
sig set clutch 0
sig ramp wheel -12 until 0     # a firm stop: light comes on, stays on through the stop
state show
```

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
(v5.3+). With the IDF environment sourced:

```bash
cd transmitter/software
idf.py set-target esp32s3
idf.py menuconfig      # optional: pins, ESP-NOW channel/rate
idf.py build
idf.py flash monitor   # console rides native USB — no UART bridge on this board
```

> **Build for 4 MB.** U1 is an `ESP32-S3-WROOM-1-N4`: 4 MB flash, **no PSRAM**
> (`CONFIG_SPIRAM` must be *absent*, not merely `=n`). Both are set in
> `sdkconfig.defaults`. Build at 8 MB by mistake and the app boot-loops before
> `app_main` — see [`BRINGUP.md §3`](../../logger/hardware/BRINGUP.md) for the exact
> signature and why the bootloader's `SPI Flash Size` line is not evidence.

## Host tests

`test_host/` builds two harnesses against the same sources the firmware compiles, with
no ESP-IDF involved:

```bash
cmake -S transmitter/software/test_host -B transmitter/software/test_host/build
cmake --build transmitter/software/test_host/build
pip install -r tools/requirements.txt

python3 tools/golden_check.py    # DE-08: C decoder vs python-cantools
python3 tools/fsm_check.py       # DE-09: C state machine vs the reference
```

`fsm_check.py` replays a ride capture through the firmware's FSM and through
[`tools/trc_viz.py`](../../tools/trc_viz.py) — the Python twin of the in-browser bench
the tunables were calibrated on — and asserts they produce the **same state at every
tick**. Retune the bench without porting the change (or "clean up" the firmware and alter
its behaviour) and this fails. Both run in CI on every push.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`,
target `esp32s3`) on every push/PR that touches the firmware, and runs both host tests
above.

_Raw CAN capture logs go under `captures/`._
