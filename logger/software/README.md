# logger — software

CAN data-logger firmware for the **custom logger PCB** ([`../hardware/`](../hardware) —
ESP32-S3-WROOM-1-N4, onboard TCAN330 transceiver, native SDMMC microSD, **ESP-IDF**).
Captures all CAN traffic, no filtering, and writes **PCAN `.trc` (v2.1)** files to the
microSD.

> **Target:** `esp32s3`. The retired ESP-WROVER-KIT (`esp32`) is **no longer
> supported** — the pin map, the native-USB console and the three-LED indicator are
> all specific to this board. For the WROVER-KIT version, see the history before the
> port.

Responsibilities:
- **TWAI (CAN 2.0)** in **listen-only** mode (Kconfig-selectable to normal/ACK), all
  IDs accepted — see [`docs/can-profiles.md`](../../docs/can-profiles.md).
- Timestamp every frame and write it to microSD in **PCAN `.trc`** format, compatible
  with PCAN-Explorer and `python-can`'s `TRCReader` (the offline path in
  [`can-profiles.md §5`](../../docs/can-profiles.md#3-sniffing-methodology)).
- **One pushbutton** start/stop on **IO6**: each **start** opens a new `N.trc` (N an
  increasing integer), each **stop** closes it. Debounced in software via the
  `iot_button` component.
- Gate all microSD work on **card presence (DET_A, IO8)** — no mount without a card,
  no recording without a mount, and a card pulled mid-capture aborts cleanly instead
  of wedging the writer.
- Emit a **running operations log** over the native-USB console — view with
  `idf.py monitor`. At boot that includes the CAN silent-pin readback, whether a card
  is **detected**, the **mount result**, and a **listing of every file on the card**
  with sizes.
- Drive the **three status LEDs** (see below).

## Silent mode is enforced in hardware

`R16` (10K) pulls the TCAN330's **S** pin up to 3V3, and **S high = silent**: the
transceiver receives but its driver is disabled, so it cannot put a dominant bit —
not even an ACK — onto the bus.

That pin is **GPIO35**, which is **U1 pin 28**. (Easy to transpose; the netlist table
in [`../hardware/BRINGUP.md §0`](../hardware/BRINGUP.md) is authoritative.)

The firmware configures GPIO35 as a **high-impedance input with both internal pulls
disabled**, and never drives it. So silence is the power-on default and the
failure-safe state: it holds through reset, through boot before `app_main` runs, and
through a firmware crash. The internal pull-up is left off deliberately — it would
hold S high even if `R16` were missing, hiding exactly the board fault that would let
the logger transmit onto a live bus. At boot the firmware reads the pin back and
shouts if it is anything but high.

The TWAI controller is **also** held in listen-only mode. Both halves of the golden
rule — never disturb a live vehicle bus — are in force, and neither depends on the
other.

## Status LEDs

"Heartbeat" is a double thump (lub-dub) on a 2 s period; **inverted heartbeat** is
that same waveform negated — mostly lit, two brief dropouts. The pair reads at a
glance and neither can be confused with the error blink.

| LED | GPIO | Shows | Patterns |
|-----|:----:|-------|----------|
| External indicator (operator's pod, J4, alongside the button) | **IO18** | logger state | idle → heartbeat · recording → **inverted** heartbeat · error → **2 Hz, 50% duty** |
| **D5** green (onboard) | **IO2** | bus liveness | no CAN traffic → heartbeat · CAN traffic present → **inverted** heartbeat |
| **D6** red (onboard) | **IO1** | card activity | brief flash per microSD write · error → **2 Hz, 50% duty** |

Notes:
- D5 is driven by the TWAI controller, which **runs continuously** — not only while
  recording. Bus liveness has to be answerable *before* you press record; walking back
  to the bike to discover the harness was unplugged is the failure this prevents.
  Frames received while idle are counted for the indicator and discarded.
- D6's flash tracks *actual card writes*, not frames. The `.trc` stream is fully
  buffered (32 KB), so frames do not each hit the card; the firmware pulses D6 once
  per buffer's worth. Flashing per frame would report the wrong thing and, at
  ~1500 frames/s, would peg D6 solid — indistinguishable from a fault.
- D5 stays independent of the error state, so "no card" and "no CAN" remain
  distinguishable without reaching for the console.

## Hardware / wiring

As-built pin map, from the exported KiCad netlist — the same table as
[`../hardware/BRINGUP.md §0`](../hardware/BRINGUP.md). Do **not** take pin numbers
from the older `logger_sch_revA.pdf` or from `hardware/README.md`; both are stale.

| Signal | GPIO | Notes |
|--------|:----:|-------|
| Start/stop button (BTN_SIG) | **IO6** | J4 pin3; `R5` pull-up, **active-low**, debounced in software |
| CAN TXD | **IO21** | → U2 (TCAN330) pin1; `R15` pull-up |
| CAN RXD | **IO47** | ← U2 pin4 |
| CAN S (silent) | **IO35** | U1 **pin 28**; `R16` pull-up → **hi-z, defaults silent** |
| SD CLK / CMD | **IO9** / **IO10** | J5 pin5 (via `R12`) / pin3 |
| SD DAT0–3 | **IO48 / IO17 / IO12 / IO11** | J5 pin7 / 8 / 1 / 2 |
| SD card-detect (DET_A) | **IO8** | J5 pin10; `R17` pull-up, **active-low** |
| External indicator LED | **IO18** | Q1 gate via `R3`, low-side to J4 pin2 |
| Status LED D5 (green) | **IO2** | via `R20`, active-high |
| Status LED D6 (red) | **IO1** | via `R21`, active-high |

All of these, plus bit rate, listen-only and the SDMMC bus width, are configurable in
`menuconfig` (*CAN logger configuration*, *microSD (SDMMC)*, *Status indicators*).
Defaults: **500 kbit/s**, **listen-only**, **4-bit SDMMC**.

> **Internal pull-ups are deliberately left disabled** on BTN_SIG, DET_A and the CAN
> silent pin, and `SDMMC_SLOT_FLAG_INTERNAL_PULLUP` is not set on the SD bus. The
> board carries proper external pull-ups (`R5`, `R17`, `R16`, `R7`–`R11`); an internal
> pull would read correct even with one of those unstuffed, turning a real board fault
> into a false pass. Same reasoning as [`BRINGUP.md §4.5`](../hardware/BRINGUP.md).

> **Bring-up aid:** *microSD → SDMMC bus width* selects 1-bit or 4-bit. `BRINGUP.md`
> §6.2 mounts 1-bit first, then §6.3 moves to 4-bit; the boot log reports the width
> actually negotiated, so the two are distinguishable.

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
├── bringup/                standalone bring-up scratch apps + reset soak script
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml    espressif/button (iot_button); rest is ESP-IDF
    ├── Kconfig.projbuild    pin map, bit rate, listen-only, queue depth, SD width, LEDs
    ├── trc_format.[ch]      pure PCAN .trc formatting (host-testable)
    ├── ui_log.[ch]          operations log to the console
    ├── status_led.[ch]      three-LED indicator (state / bus / card)
    └── logger_main.c        app_main: TWAI, microSD, button, RX + writer + card-detect tasks
```

`trc_format` is deliberately platform-independent (no IDF headers) so it can be
host-unit-tested, per [`docs/firmware.md §4`](../../docs/firmware.md#4-build--toolchain).

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
(v5.2+). With the IDF environment sourced:

```bash
cd logger/software
idf.py set-target esp32s3
idf.py menuconfig      # optional: pins, bit rate, mode, SD bus width
idf.py build
idf.py flash monitor   # console rides native USB — no UART bridge on this board
```

The `espressif/button` component is pulled automatically by the component manager on
first build.

> **Build for 4 MB.** U1 is an `ESP32-S3-WROOM-1-N4`: 4 MB flash, **no PSRAM**
> (`CONFIG_SPIRAM` must be *absent*, not merely `=n`). Both are set in
> `sdkconfig.defaults`. Build at 8 MB by mistake and the app boot-loops before
> `app_main` — see [`BRINGUP.md §3`](../hardware/BRINGUP.md) for the exact signature
> and why the bootloader's `SPI Flash Size` line is not evidence.

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`,
target `esp32s3`) on every push/PR that touches the firmware, confirming it compiles.
