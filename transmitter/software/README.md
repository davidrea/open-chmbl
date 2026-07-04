# Transmitter — software

Bike-side firmware (ESP32-C3, ESP-IDF). See
[`docs/firmware.md §1`](../../docs/firmware.md#1-transmitter-firmware-bike-side).

Responsibilities:
- TWAI (CAN 2.0) in **listen-only mode**, filtered to the [bike profile](../../docs/can-profiles.md) IDs.
- Decode `wheel_speed`, `throttle_pct`, `rpm`, `clutch_pulled`, `gear`/`neutral`.
  (The reference bus carries **no brake-switch bit** — braking is inferred from
  wheel-speed-derived acceleration; see [DE-09](../../docs/design/de-09-brake-decel-logic.md).)
- Run the [braking state machine](../../docs/firmware.md#braking-state-machine) (50 Hz).
- Broadcast `chmbl_msg_t` heartbeat over [ESP-NOW](../../docs/protocol.md) at 20–50 Hz.
- Power management: deep-sleep when the bus is idle / bike off.

Keep the state machine and profile decoder **platform-independent** so they can be
host-unit-tested without hardware.

The braking state machine is specified in `state_machine/brake_fsm.sm` and compiled by
the [SMC State Machine Compiler](https://smc.sourceforge.net/) as a CMake pre-build step
(see [`firmware.md §4`](../../docs/firmware.md#4-build--toolchain)). **A JRE is required
on the build host**; `Smc.jar` lives under `tools/smc/`. Generated `brake_fsm_sm.[ch]`
are build artifacts and are not committed.

## Status

Bootstrapped ESP-IDF project — now hosting the first slice of the **developer
CLI** (design element [DE-00](../../docs/design/README.md)). The real bike-side
firmware (TWAI/CAN + state machine + ESP-NOW TX) lands on top of this; the
console exists so each later element can be faked/observed in isolation on the
bench.

```
software/
├── CMakeLists.txt          top-level ESP-IDF project
├── sdkconfig.defaults      committed defaults (common)
├── sdkconfig.defaults.esp32c3   product target — USB Serial/JTAG console
├── sdkconfig.defaults.esp32     interim dev hardware — UART console
└── main/
    ├── CMakeLists.txt
    ├── Kconfig.projbuild    CHMBL_CLI / CHMBL_STATE_GPIO options
    ├── console.h
    ├── main.c              app_main: start the console
    ├── console.c           REPL bootstrap (USB Serial/JTAG, UART fallback)
    ├── cmd_system.c        `id`    — chip unique ID (base MAC) + chip info
    └── cmd_state.c         `state` — set/show the stand-in braking output state
```

`console.c` / `console.h` / `cmd_system.c` are duplicated verbatim with the
`brake_light` firmware for now; per the [roadmap](../../docs/roadmap.md) they get
promoted to a shared component once the shell stabilizes.

### Targets & console transport

The firmware builds for two targets; pick one with `idf.py set-target`. The
console code is identical — only the transport (chosen by the per-target
`sdkconfig.defaults.<target>`) and the default indicator GPIO differ.

| Target | Status | Console transport | Connect |
|--------|--------|-------------------|---------|
| `esp32c3` | product target | built-in **USB Serial/JTAG** (native USB GPIO18/19) | `/dev/ttyACM*`, any baud |
| `esp32` | interim dev hardware | **UART0** (GPIO1 TX / GPIO3 RX) via onboard USB-UART bridge | `/dev/ttyUSB*`, 115200 |

Commands so far (`help` lists them):

| Command | Purpose |
|---------|---------|
| `help` | List commands. |
| `id` | Chip unique ID (base MAC), model/revision, IDF version. |
| `state [off\|brake]` | Set / show the stand-in braking output state (no arg = show). Lights the indicator GPIO on BRAKE. (DECEL is a reserved wire state, not TX-emitted — see [protocol.md](../../docs/protocol.md).) |

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/)
(v5.3+). With the IDF environment sourced:

```bash
cd transmitter/software
idf.py set-target esp32      # or esp32c3 (re-run set-target to switch)
idf.py menuconfig            # optional: set the indicator GPIO for your board
idf.py build
idf.py flash monitor         # on attached hardware (Ctrl-] to exit the monitor)
```

`idf.py monitor` attaches to whichever console the target selected; type `help`
at the `chmbl>` prompt. The indicator pin defaults to the onboard LED of each
target's reference board (**GPIO2** on the classic ESP32-DevKitC, **GPIO8** on
the ESP32-C3-DevKitM/C). Change it under *Transmitter configuration → Stand-in
state-indicator GPIO number*, or override `CONFIG_CHMBL_STATE_GPIO`.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`)
on both `esp32` and `esp32c3` on every push/PR that touches the firmware,
confirming it compiles.

_Raw CAN capture logs go under `captures/`._
