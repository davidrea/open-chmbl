# Brake_light — software

Rider-side firmware (ESP32-C3, ESP-IDF). See
[`docs/firmware.md §2`](../../docs/firmware.md#2-brake_light-firmware-rider-side).

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
├── sdkconfig.defaults      committed defaults (common)
├── sdkconfig.defaults.esp32c3   product target — USB Serial/JTAG console
├── sdkconfig.defaults.esp32     interim dev hardware — UART console
└── main/
    ├── CMakeLists.txt
    ├── Kconfig.projbuild    CHMBL_CLI / CHMBL_LIGHT_GPIO options
    ├── console.h
    ├── main.c              app_main: start the console
    ├── console.c           REPL bootstrap (USB Serial/JTAG, UART fallback)
    ├── cmd_system.c        `id`    — chip unique ID (base MAC) + chip info
    └── cmd_light.c         `light` — drive the stand-in brake-light GPIO
```

### Targets & console transport

The firmware builds for two targets; pick one with `idf.py set-target`. The
console code is identical — only the transport (chosen by the per-target
`sdkconfig.defaults.<target>`) and the default light GPIO differ.

| Target | Status | Console transport | Connect | JTAG on same cable |
|--------|--------|-------------------|---------|--------------------|
| `esp32c3` | product target | built-in **USB Serial/JTAG** (native USB GPIO18/19) | `/dev/ttyACM*`, any baud | yes |
| `esp32` | interim dev hardware | **UART0** (GPIO1 TX / GPIO3 RX) via onboard USB-UART bridge | `/dev/ttyUSB*`, 115200 | no (needs external probe) |

On the C3 the native USB port means no external USB-TTL adapter and console +
debug over one cable. The classic ESP32 has no USB peripheral, so the shell goes
out the TX/RX pins through the board's bridge chip.

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
| `light [on\|off\|toggle]` | Drive / read the stand-in brake-light GPIO (no arg = show state). |

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/)
(v5.3+). With the IDF environment sourced:

```bash
cd brake_light/software
idf.py set-target esp32      # or esp32c3 (re-run set-target to switch)
idf.py menuconfig            # optional: set the stand-in light GPIO for your board
idf.py build
idf.py flash monitor         # on attached hardware (Ctrl-] to exit the monitor)
```

`idf.py monitor` attaches to whichever console the target selected; type `help`
at the `chmbl>` prompt. The stand-in light pin defaults to the onboard LED of
each target's reference board (**GPIO2** on the classic ESP32-DevKitC, **GPIO8**
on the ESP32-C3-DevKitM/C). Change it under *Brake_light configuration →
Stand-in brake-light GPIO number*, or override `CONFIG_CHMBL_LIGHT_GPIO`.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`,
target `esp32c3`) on every push/PR that touches the firmware, confirming it compiles.
