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

Bootstrapped ESP-IDF project — now hosting the first slice of the **developer
CLI** (design element [DE-00](../../docs/design/README.md)). The real helmet-side
firmware (ESP-NOW RX + LED pattern engine) lands on top of this; the console
exists so each later element can be faked/observed in isolation on the bench.

```
software/
├── CMakeLists.txt          top-level ESP-IDF project
├── sdkconfig.defaults      committed defaults (console → USB Serial/JTAG)
└── main/
    ├── CMakeLists.txt
    ├── Kconfig.projbuild    CHMBL_CLI / CHMBL_LIGHT_GPIO options
    ├── console.h
    ├── main.c              app_main: start the console
    ├── console.c           REPL bootstrap (USB Serial/JTAG, UART fallback)
    ├── cmd_system.c        `id`    — chip unique ID (base MAC) + chip info
    └── cmd_light.c         `light` — drive the stand-in brake-light GPIO
```

### Console transport

The console runs over the ESP32-C3's built-in **USB Serial/JTAG** controller: it
enumerates as a virtual COM port over the native USB pins (GPIO18/GPIO19), so no
external USB-TTL adapter is needed, and it carries the **JTAG debug interface on
the same cable simultaneously**. Connect to the enumerated port (e.g.
`/dev/ttyACM0`) at any baud — the rate is ignored for USB CDC.

Commands so far (`help` lists them):

| Command | Purpose |
|---------|---------|
| `help` | List commands. |
| `id` | Chip unique ID (base MAC), model/revision, IDF version. |
| `light [on\|off\|toggle]` | Drive / read the stand-in brake-light GPIO (no arg = show state). |

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/)
(v5.3+). With the IDF environment sourced:

```bash
cd brake_light/software
idf.py set-target esp32c3
idf.py menuconfig      # optional: set the stand-in light GPIO for your board
idf.py build
idf.py flash monitor   # on attached hardware (Ctrl-] to exit the monitor)
```

`idf.py monitor` attaches to the USB Serial/JTAG console; type `help` at the
`chmbl>` prompt. The stand-in light pin defaults to **GPIO8** (onboard LED on
the ESP32-C3-DevKitM/C). Change it under *Brake_light configuration → Stand-in
brake-light GPIO number*, or override `CONFIG_CHMBL_LIGHT_GPIO` to match your
board.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`,
target `esp32c3`) on every push/PR that touches the firmware, confirming it compiles.
