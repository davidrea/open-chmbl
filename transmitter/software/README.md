# Transmitter — software

Bike-side firmware (ESP32-C3, ESP-IDF). See
[`docs/firmware.md §1`](../../docs/firmware.md#1-transmitter-firmware-bike-side).

Responsibilities:
- TWAI (CAN 2.0) in **listen-only mode**, filtered to the [bike profile](../../docs/can-profiles.md) IDs.
- Decode `brake_switch`, `throttle_pct`, `rpm`, `clutch_pulled`.
- Run the [braking state machine](../../docs/firmware.md#braking-state-machine) (50 Hz).
- Broadcast `chmbl_msg_t` heartbeat over [ESP-NOW](../../docs/protocol.md) at 20–50 Hz.
- Power management: deep-sleep when the bus is idle / bike off.

Keep the state machine and profile decoder **platform-independent** so they can be
host-unit-tested without hardware.

## Status

Bootstrapped ESP-IDF project — currently a **bring-up blink** only. It exists to
prove the toolchain, board, and CI build before the real bike-side firmware
(TWAI/CAN + state machine + ESP-NOW TX) lands.

```
software/
├── CMakeLists.txt          top-level ESP-IDF project
├── sdkconfig.defaults      committed defaults (generated sdkconfig is gitignored)
└── main/
    ├── CMakeLists.txt
    ├── Kconfig.projbuild    BLINK_GPIO / BLINK_PERIOD_MS options
    └── blink.c              app_main: toggle one GPIO LED
```

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/)
(v5.3+). With the IDF environment sourced:

```bash
cd transmitter/software
idf.py set-target esp32c3
idf.py menuconfig      # optional: set the LED GPIO for your board
idf.py build
idf.py flash monitor   # on attached hardware
```

The LED pin defaults to **GPIO8** (onboard LED on the ESP32-C3-DevKitM/C).
Change it under *Transmitter blink configuration → Blink LED GPIO number*, or
override `CONFIG_BLINK_GPIO` to match your board.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`,
target `esp32c3`) on every push/PR that touches the firmware, confirming it compiles.

_Raw CAN capture logs go under `captures/`._
