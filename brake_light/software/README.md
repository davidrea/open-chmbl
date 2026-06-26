# Brake_light — software

Helmet-side firmware (ESP32-C3, ESP-IDF). See
[`docs/firmware.md §2`](../../docs/firmware.md#2-brake_light-firmware-helmet-side).

Responsibilities:
- Receive `chmbl_msg_t` over [ESP-NOW](../../docs/protocol.md) (encrypted, pre-paired peer).
- Validate sequence / drop stale packets.
- Render state via the [LED pattern engine](../../docs/firmware.md#pattern-engine-suggested-mapping) (60 Hz), steady / no strobing.
- **Ambient-light dimming**, battery monitoring + low-battery warning.
- **Link-loss failsafe**: distinct indication, never silently dark, never a latched fake brake.
- Button UI: power, pairing, brightness cap.

## Status

Bootstrapped ESP-IDF project — currently a **bring-up blink** only. It exists to
prove the toolchain, board, and CI build before the real firmware lands.

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
cd brake_light/software
idf.py set-target esp32c3
idf.py menuconfig      # optional: set the LED GPIO for your board
idf.py build
idf.py flash monitor   # on attached hardware
```

The LED pin defaults to **GPIO8** (onboard LED on the ESP32-C3-DevKitM/C).
Change it under *Brake_light blink configuration → Blink LED GPIO number*, or
override `CONFIG_BLINK_GPIO` to match your board.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`,
target `esp32c3`) on every push/PR that touches the firmware, confirming it compiles.
