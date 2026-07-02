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
- Emit a **running operations log** to the serial console (`idf.py monitor`) and to
  a scrolling text console on the kit's **LCD** (booted, filesystem mounted, files
  listed, next file number, opened file, button pressed, recording started/stopped,
  file closed, drops).

> **LCD**: the only other onboard indicator is the WROVER-KIT's red/green/blue LED,
> but two of its three legs (IO2/IO4) are shared with the microSD's 4-bit SDMMC bus,
> leaving only the red channel usable — not enough for real status. The kit's LCD
> is a v4.1-panel **ST7789V** (menuconfig-selectable to ILI9341 for older v3 kits);
> the esp-bsp package's "ILI9341 vs ST7789" toggle turns out to only flip a mirror
> flag and always drives ST7789 init commands, which produced a garbled screen in
> an earlier attempt. `lcd_console.c` drives the panel directly with ESP-IDF's
> built-in `esp_lcd` ST7789 driver (no LVGL, no BSP) and renders text with an
> embedded 8x8 bitmap font — see *LCD status console* in `menuconfig` for the
> controller choice, pixel clock, color-invert/BGR/mirror knobs, and the color-bar
> self-test.
>
> **Known conflict: LCD DC shares GPIO21 with the microSD card-detect switch.**
> Confirmed on hardware: with a card inserted the panel never updates (stuck on
> its power-on-reset color, RDDID/RDID4 readback zero); pull the card and the
> console works. The microSD socket's card-detect contact grounds that net
> whenever a card is present, contending with the LCD's DC signal — a PCB-level
> conflict on the WROVER-KIT, not a driver bug, and the DC line is soldered
> directly to GPIO21 (no free alternative pin to move it to). **This means the
> LCD is not usable at the same time as a mounted microSD card** — i.e. not
> during an actual logging run. Until/unless that's resolved in hardware, treat
> the LCD console as a bench/bring-up aid (status visible before a card is
> inserted, or with logging paused) rather than a live in-ride indicator.

## Hardware / wiring

The ESP-WROVER-KIT uses most of its GPIOs for the microSD (SDMMC 4-bit), PSRAM and
console (and the unused LCD); the free pins carry the CAN signals and the button. An
**external CAN transceiver** (e.g. SN65HVD230, 3.3 V) sits between the ESP and the bus.

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
    ├── idf_component.yml    espressif/button (iot_button), esp_lcd_ili9341; rest is ESP-IDF
    ├── Kconfig.projbuild    button / CAN pins, bit rate, listen-only, queue depth, LCD console
    ├── trc_format.[ch]      pure PCAN .trc formatting (host-testable)
    ├── ui_log.[ch]          operations log to the serial console + LCD
    ├── lcd_console.[ch]     ST7789/ILI9341 bring-up + scrolling text console (no LVGL/BSP)
    ├── font8x8.[ch]         embedded public-domain 8x8 bitmap font
    └── logger_main.c        app_main: TWAI, microSD, button, RX + writer tasks
```

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
idf.py flash monitor   # on the attached ESP-WROVER-KIT; monitor shows the op log
```

The `espressif/button` component is pulled automatically by the component manager on
first build.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`,
target `esp32`) on every push/PR that touches the firmware, confirming it compiles.
