# logger — software

CAN data-logger firmware for the **ESP-WROVER-KIT v4.1** (ESP32-WROVER, **ESP-IDF**).
Captures all CAN traffic, no filtering, and writes **PCAN `.trc` (v2.1)** files to the
on-board microSD.

Responsibilities:
- **TWAI (CAN 2.0)** in **listen-only** mode (Kconfig-selectable to normal/ACK), all
  IDs accepted — see [`docs/can-profiles.md`](../../docs/can-profiles.md).
- Timestamp every frame and write it to microSD in **PCAN `.trc`** format, compatible
  with PCAN-Explorer and `python-can`'s `TRCReader` (the offline path in
  [`can-profiles.md §5`](../../docs/can-profiles.md#3-sniffing-methodology)).
- **One pushbutton** start/stop: each **start** opens a new `N.trc` (N an increasing
  integer), each **stop** closes it. Debounced via the `iot_button` component.
- Show a **running operations log** on the kit's LCD (booted, filesystem mounted,
  files listed, next file number, opened file, button pressed, recording
  started/stopped, file closed, drops).

## Hardware / wiring

The ESP-WROVER-KIT drives its LCD, microSD (SDMMC 4-bit), PSRAM and console over most
of its GPIOs; the free pins carry the CAN signals and the button. An **external CAN
transceiver** (e.g. SN65HVD230, 3.3 V) sits between the ESP and the bus.

| Signal | Default GPIO | Wiring |
|--------|:------------:|--------|
| Start/stop button | **IO33** | Other side to **GND**; internal pull-up, active-low, debounced. |
| CAN TX | **IO26** | → transceiver **TXD** |
| CAN RX | **IO27** | ← transceiver **RXD** |
| — | — | transceiver **CAN-H / CAN-L** → bus; **VCC** 3V3, **GND** common. |

All four (button, TX, RX, bit rate, mode) are configurable under
*CAN logger configuration* in `menuconfig`. Defaults: **500 kbit/s**, **listen-only**.

> These pins are the ones left free by the WROVER-KIT (LCD `5/18/19/21/22/23/25`,
> microSD `2/4/12/13/14/15`, PSRAM `16/17`, console `1/3`). If you re-pin, keep clear
> of those.

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
    ├── idf_component.yml    esp_wrover_kit BSP (microSD + iot_button) + esp_lcd_ili9341
    ├── Kconfig.projbuild    button / CAN pins, bit rate, listen-only, queue depth
    ├── trc_format.[ch]      pure PCAN .trc formatting (host-testable)
    ├── ui_log.[ch]          on-screen rolling operations log (LVGL)
    ├── display_init.[ch]    ILI9341 LCD + LVGL bring-up (see note below)
    └── logger_main.c        app_main: TWAI, microSD, button, RX + writer tasks
```

> **Display driver note:** the WROVER-KIT v4.1 panel is an **ILI9341**, but the
> `esp_wrover_kit` BSP only ships the ST7789 driver (its "ILI9341" menuconfig
> option just flips colour order/mirror — it still calls `esp_lcd_new_panel_st7789`,
> which leaves the panel showing garbage/vertical stripes). So `display_init.c`
> brings the LCD up itself with the real `esp_lcd_ili9341` driver + `esp_lvgl_port`,
> reusing the BSP's pin macros and backlight helpers; the BSP still owns the SD
> card and button.

`trc_format` is deliberately platform-independent (no IDF headers) so it can be
host-unit-tested, per [`docs/firmware.md §4`](../../docs/firmware.md#4-build--toolchain).

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/)
(v5.2+). With the IDF environment sourced:

```bash
cd logger/software
idf.py set-target esp32
idf.py menuconfig      # optional: CAN logger configuration (pins, bit rate, mode)
idf.py build
idf.py flash monitor   # on the attached ESP-WROVER-KIT
```

The `espressif/esp_wrover_kit` BSP (and its LVGL / `iot_button` dependencies) is pulled
automatically by the component manager on first build.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`,
target `esp32`) on every push/PR that touches the firmware, confirming it compiles.
