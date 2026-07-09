# logger — software

CAN data-logger firmware for the **custom logger board** ([`../hardware/`](../hardware) —
**ESP32-S3-WROOM-1-N8**, onboard TCAN330 CAN transceiver, native SDMMC microSD),
built with **ESP-IDF**. Captures all CAN traffic, no filtering, and writes
**PCAN `.trc` (v2.1)** files to the microSD.

> **Status:** ported to the custom ESP32-S3 board. Target is `esp32s3`, the pin
> map (below) comes from the schematic in [`../hardware/`](../hardware), and the
> `CONFIG_SPIRAM=y` dependency is gone — the ESP32-S3-WROOM-1-N8 has **no PSRAM**.
> The earlier bring-up target was the ESP-WROVER-KIT v4.1 (ESP32-WROVER); its
> GPIOs are retained only as the non-default fallbacks noted in `menuconfig`.

Responsibilities:
- **TWAI (CAN 2.0)** in **listen-only** mode (Kconfig-selectable to normal/ACK), all
  IDs accepted — see [`docs/can-profiles.md`](../../docs/can-profiles.md).
- Timestamp every frame and write it to microSD in **PCAN `.trc`** format, compatible
  with PCAN-Explorer and `python-can`'s `TRCReader` (the offline path in
  [`can-profiles.md §5`](../../docs/can-profiles.md#3-sniffing-methodology)).
- **One pushbutton** start/stop: each **start** opens a new `N.trc` (N an increasing
  integer), each **stop** closes it. Debounced via the `iot_button` component.
- Emit a **running operations log** to the serial console — view with `idf.py monitor`
  (booted, filesystem mounted, files listed, next file number, opened file, button
  pressed, recording started/stopped, file closed, drops).
- Show at-a-glance status on the **status LED** (GPIO18, driving an external panel
  LED through Q1, a low-side N-FET on the J4 breakout): slow heartbeat blink when
  idle/ready, solid on while recording, fast blink on a fatal error (microSD
  mount/file-open/CAN-start failure). See `status_led.[ch]`.

## Hardware / wiring

The custom board is built around the **ESP32-S3-WROOM-1-N8** with an **onboard
TCAN330** CAN transceiver and a microSD wired to the SoC's **native SDMMC host**
(full 4-bit bus). Pin assignments come straight from the schematic
([`../hardware/`](../hardware)); unlike the classic ESP32, the ESP32-S3 routes
SDMMC through the GPIO matrix, so every bus pin is assigned in firmware.

| Signal | GPIO | Wiring |
|--------|:----:|--------|
| Start/stop button (`BTN_SIG`) | **IO6** | J4 breakout; internal pull-up, active-low, debounced. |
| Status LED | **IO18** | → Q1 gate → external panel LED (J4). Active-high. |
| CAN TX | **IO21** | → TCAN330 (U2) **TXD** |
| CAN RX | **IO47** | ← TCAN330 (U2) **RXD** |
| CAN silent-mode (`S`) | **IO45** | → TCAN330 (U2) pin 8. High = RX-only in HW; driven per listen-only mode. |
| microSD CLK / CMD | **IO9 / IO10** | J5 SDMMC bus |
| microSD DAT0–DAT3 | **IO48 / IO3 / IO12 / IO11** | J5 SDMMC 4-bit data |
| microSD card-detect (`DET_A`) | **IO8** | J5; not used by firmware (no hot-plug path) |

The CAN pins, button, silent-mode pin, SD bus pins, bit rate and mode are all
configurable under *CAN logger configuration* / *microSD (SDMMC) configuration* /
*Status LED* in `menuconfig`. Defaults: **500 kbit/s**, **listen-only**.

> **CAN silent-mode (`S`) pin:** the TCAN330's silent-mode pin is wired to a GPIO
> so the transceiver can be forced RX-only in hardware, not just via TWAI
> listen-only. Firmware drives it high in listen-only mode and low otherwise. Its
> default GPIO45 is an ESP32-S3 strapping pin — sampled only at reset, so driving
> it afterward is safe, but see [`../hardware/README.md` §5](../hardware/README.md)
> for the board-level pull-up caveat. Set the pin to `-1` in `menuconfig` on the
> old external-transceiver bring-up rig, which has no `S` pin wired.
>
> **Retired bring-up rig (ESP-WROVER-KIT v4.1):** the earlier target left the CAN
> signals on IO26/IO27, the button on IO33 and the status LED on GPIO0 (the only
> free leg of its onboard RGB LED — green/blue were the microSD D0/D1 lines). Its
> LCD was unusable (DC on GPIO21 contended with the SD card-detect). Those GPIOs
> survive only as the alternate values documented in each `menuconfig` option.

## PCAN `.trc` format

Files are `N.trc` (`1.trc`, `2.trc`, …) at the card root, written as PCAN trace
**version 2.1**:

```
;$FILEVERSION=2.1
;$STARTTIME=43831.0000000
;$COLUMNS=N,O,T,B,I,d,R,L,D
...
      1        0.000 DT  1      123 Rx  -  8    DE AD BE EF 00 11 22 33
```

Columns: message number, time offset (ms from the first frame), type (`DT`/`RR`), bus,
ID (hex; extended IDs are 8 digits), direction (always `Rx`), reserved, DLC, data.

> **No on-board RTC** → `$STARTTIME` is a fixed placeholder; only the **relative time
> offsets** between frames are meaningful. That is exactly what offline decode/replay
> uses, so captures analyse correctly.

## Structure

```
software/
├── CMakeLists.txt          top-level ESP-IDF project
├── sdkconfig.defaults      committed defaults (generated sdkconfig is gitignored)
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml    espressif/button (iot_button); rest is ESP-IDF
    ├── Kconfig.projbuild    button / CAN pins, bit rate, listen-only, queue depth, status LED
    ├── trc_format.[ch]      pure PCAN .trc formatting (host-testable)
    ├── ui_log.[ch]          operations log to the serial console
    ├── status_led.[ch]      idle/recording/error indicator on the status LED (GPIO18)
    └── logger_main.c        app_main: TWAI, microSD, button, RX + writer tasks
```

`trc_format` is deliberately platform-independent (no IDF headers) so it can be
host-unit-tested, per [`docs/firmware.md §4`](../../docs/firmware.md#4-build--toolchain).

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/)
(v5.2+). With the IDF environment sourced:

```bash
cd logger/software
idf.py set-target esp32s3
idf.py menuconfig      # optional: CAN logger configuration (pins, bit rate, mode)
idf.py build
idf.py flash monitor   # over the board's native USB-C (J2); monitor shows the op log
```

The `espressif/button` component is pulled automatically by the component manager on
first build.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`,
target `esp32s3`) on every push/PR that touches the firmware, confirming it compiles.
