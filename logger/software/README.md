# logger — software

CAN data-logger firmware for the **custom logger PCB** ([`../hardware/`](../hardware) —
ESP32-S3-WROOM-1-N4, onboard TCAN330 transceiver, native SDMMC microSD, **ESP-IDF**).
Captures all CAN traffic, no filtering, and writes **PCAN `.trc` (v2.1)** files to the
microSD — **and** runs the [DE-09](../../docs/design/de-09-brake-decel-logic.md) braking
state machine on-board, previewing the brake-light decision on the remote LED.

> **Target:** `esp32s3` — the custom logger PCB, and the only supported target.
> Board-specific defaults are in [`sdkconfig.defaults.esp32s3`](sdkconfig.defaults.esp32s3);
> module- and app-level ones in [`sdkconfig.defaults`](sdkconfig.defaults). The retired
> ESP-WROVER-KIT (`esp32`) was dropped when the firmware was ported to this board; for
> that version, see the history.

## What this build is

This is the **ride-validation build**. Two jobs at once:

1. **Log CAN**, automatically and silently, gated on the engine kill switch — no
   button, no status LED.
2. **Run the real DE-09 state machine** against the real decoded signals at 50 Hz, and
   light the **remote LED** (J4 pin 2, IO18) whenever the firmware would be commanding
   the rider-side brake light **ON**.

There is **no ESP-NOW radio in this build**. The logger does not pair with or transmit
to the [`brake_light`](../../brake_light) unit; the LED *stands in* for "this firmware
would now be sending `ST_BRAKE`". The point is that the owner can ride with the logger,
watch the real FSM decision on a panel-mount LED, and afterwards read the `.trc`
together with the console transition log to see exactly **why** it decided that. The
product path is the [`transmitter`](../../transmitter), not this.

Responsibilities:
- **TWAI (CAN 2.0)** in **listen-only** mode (Kconfig-selectable to normal/ACK), all
  IDs accepted — see [`docs/can-profiles.md`](../../docs/can-profiles.md).
- Timestamp every frame and write it to microSD in **PCAN `.trc`** format, compatible
  with PCAN-Explorer and `python-can`'s `TRCReader` (the offline path in
  [`can-profiles.md §3`](../../docs/can-profiles.md#3-sniffing-methodology)).
- **Automatic recording** gated on the decoded engine cutoff (kill) switch, with
  periodic flush/`fsync` so a yanked-power ride still yields a readable trace.
- **Decode** every frame through the shared DE-08 decoder
  ([`components/chmbl_can`](../../components/chmbl_can)) and run the shared DE-09 FSM
  ([`components/brake_fsm`](../../components/brake_fsm)) at 50 Hz.
- Gate all microSD work on **card presence (DET_A, IO8)** — no mount without a card,
  no recording without a mount, and a card pulled mid-capture aborts cleanly instead
  of wedging the writer.
- Emit a **running operations log** over the native-USB console — view with
  `idf.py monitor`. At boot that includes the CAN silent-pin readback, whether a card
  is **detected**, the **mount result**, a **listing of every file on the card** with
  sizes, and the FSM tunables actually compiled in.

## Automatic, silent recording

The pushbutton is gone from the recording path, and so is LED status indication. The
device **logs whenever the engine cutoff (kill) switch is in RUN**.

The gate is the profile's `engine_cutoff` signal: **`0x121` bit 30 set AND `0x121`
byte 6 == `0x28`**. `1.0` means **kill asserted (STOP)**; `0.0` means **RUN**.

| Event | Action |
|-------|--------|
| gate → **RUN** | open a new `N.trc` (same increasing-integer scan as before) |
| gate → **STOP** (kill asserted) | close the file, with a `;closed:` footer |
| **bus silence** — no `0x121` for `CONFIG_LOGGER_BUS_SILENCE_MS` (default **3000 ms**) | close the file |

"Gate → RUN" covers both cases: the bike started with the switch **already** at RUN
(the first valid RUN observation after boot, or after the bus comes back), and a
cutoff→RUN transition mid-session.

The gate posts only on a **transition**, so the writer also catches up when a card
appears: insert a card while the kill switch already reads RUN (booting with the slot
empty, or swapping cards at a stop) and the capture starts as soon as the mount
succeeds.

**Two timeouts, deliberately different.** A reading is only trusted while it is fresh
within the decoder's own staleness window (`CAN_DECODE_STALE_MS`, 1000 ms). Silence is
declared at 3000 ms. Between the two the gate **holds** its last decision — hysteresis,
so a brief run of missed frames cannot chop one ride into two files.

**Durability.** Nothing presses a button to end a file cleanly any more, and the board
loses 12 V the moment the ignition goes off. The writer therefore `fflush()` +
`fsync()`es the open file on an interval **and** a byte budget —
`CONFIG_LOGGER_FLUSH_INTERVAL_MS` (default **2000 ms**) and `CONFIG_LOGGER_FLUSH_KIB`
(default **64 KiB**), whichever comes first — plus once immediately after the header, so
even a power cut seconds in leaves a well-formed short file rather than a zero-length
one. `fsync` is the part that matters: it is what makes FATFS commit the dirty sectors
*and* the directory entry.

This **bounds** the loss window; it does not replace the 32 KiB stdio write buffer,
which is what keeps throughput ahead of a ~1500 frames/s bus. A per-frame `fsync` would
collapse that throughput, which is the problem the buffer exists to solve.

**Capture is unchanged otherwise:** all frames, unfiltered, listen-only. Frames reach
the RX→writer queue only while recording, but the **decode tap sees every frame
always** — otherwise the gate could never turn recording on in the first place.

**The pushbutton** (`CONFIG_LOGGER_BUTTON_GPIO`, IO6) is **unused**. The Kconfig symbol
is retained and documented as such rather than deleted — the pin is still routed and
tracked in [`BRINGUP.md §0/§7`](../hardware/BRINGUP.md) — and the `espressif/button`
(`iot_button`) dependency is gone, since nothing else needed debounced button input.

## The on-board DE-09 brake-light preview

The remote LED on **J4 pin 2**, driven from **IO18** through `Q1` (2N7002K, active-high),
shows the state machine's output:

| FSM state | Protocol | Remote LED |
|-----------|----------|-----------|
| `BRAKING` | `ST_BRAKE` | **on** |
| `STOPPED` | `ST_BRAKE` | **on** |
| `OFF` | `ST_OFF` | off |

A **steady level** — no blinking, no brightness control. DE-09 and
[`docs/safety-regulatory.md`](../../docs/safety-regulatory.md) both forbid strobing.

The machine is ticked on a fixed 50 Hz grid (20 ms) and handed the *nominal* period as
`dt`, which is what the tuned reference does; a measured `dt` would inject scheduler
jitter into the debounce windows.

**Without a trustworthy wheel speed the light is held off** and the machine is reset.
A decoder that has seen nothing reads `0.00 mph`, which rule 2 would happily turn into
a lit `STOPPED` at boot. This is the one place the firmware is deliberately more
conservative than the reference implementation (which has no notion of validity).

Every transition is logged to the console with a timestamp, both state names, the
triggering **rule number** and the decisive signal values:

```
I (48213) logger: FSM 62.500s OFF -> BRAKING (rule 1) light=ON
I (48213) logger:     speed 29.52 mph  accel -2.42 mph/s  clutch out  gear 4
```

On a ride that log *is* the record that explains the LED, and it is what makes the
`.trc` + console pair reviewable afterwards.

### Tunables

Defaults are the values the Python/JS reference was tuned to on real ride logs
(`BrakeTunables` in [`tools/trc_viz.py`](../../tools/trc_viz.py)). Every one is a
Kconfig symbol under *Component config → Braking state machine (DE-09)* — defined by the
shared component, [`components/brake_fsm/Kconfig`](../../components/brake_fsm/Kconfig) —
so retuning needs no code change. Kconfig has no float type, so thresholds are stored in
**milli-units** and converted to float **once**, at init, in `fsm_preview.c`.

| Tunable | Default | Kconfig symbol |
|---------|--------:|----------------|
| `decel_on_mphps` | 2.0 mph/s | `BRAKE_FSM_DECEL_ON_MMPHPS` (2000) |
| `decel_on_debounce_ms` | 120 ms | `BRAKE_FSM_DECEL_ON_DEBOUNCE_MS` |
| `stop_speed_mph` | 1.0 mph | `BRAKE_FSM_STOP_SPEED_MMPH` (1000) |
| `moving_speed_mph` | 3.0 mph | `BRAKE_FSM_MOVING_SPEED_MMPH` (3000) |
| `accel_off_mphps` | 0.5 mph/s | `BRAKE_FSM_ACCEL_OFF_MMPHPS` (500) |
| `accel_off_min_speed_mph` | 5.0 mph | `BRAKE_FSM_ACCEL_OFF_MIN_SPEED_MMPH` (5000) |
| `steady_band_mphps` | 0.75 mph/s | `BRAKE_FSM_STEADY_BAND_MMPHPS` (750) |
| `steady_timeout_ms` | 1500 ms | `BRAKE_FSM_STEADY_TIMEOUT_MS` |
| `stop_timeout_ms` | 60000 ms | `BRAKE_FSM_STOP_TIMEOUT_MS` |
| `state_min_dwell_ms` | 250 ms | `BRAKE_FSM_STATE_MIN_DWELL_MS` |
| `speed_smooth_ms` | 80 ms | `BRAKE_FSM_SPEED_SMOOTH_MS` |

`speed_smooth_ms` is strictly a DE-08 decode-path value (the wheel-speed low-pass ahead
of the acceleration slope) but is calibrated with the thresholds, so it shares the menu.

## Silent mode: belt and braces

`R16` (10K) pulls the TCAN330's **S** pin up to 3V3, and **S high = silent**: the
transceiver's **receiver stays active** while its **driver is disabled**, so it cannot
put a dominant bit — not even an ACK — onto the bus. That is exactly the repo's
listen-only [golden rule](../../docs/can-profiles.md#1-golden-rule-listen-only).

That pin is **GPIO35**, which is **U1 pin 28**. (Easy to transpose; the netlist table
in [`../hardware/BRINGUP.md §0`](../hardware/BRINGUP.md) is authoritative.)

At boot the firmware does two things, in this order:

1. Configures GPIO35 as a **high-impedance input with both internal pulls disabled**
   and **reads it back**. High means `R16` is doing its job. The internal pull-up is
   left off deliberately for this read — it would hold S high even if `R16` were
   missing, hiding exactly the board fault that would let the logger transmit onto a
   live bus. A low reading latches a loud console warning.
2. **Then drives GPIO35 high** as an output and leaves it there, so silence does not
   depend on a single 10K resistor for a whole ride.

`R16` still covers the windows firmware cannot: power-on, reset, and boot before
`app_main` runs. A GPIO output latch holds its level through a firmware crash, so
driving the pin does not weaken that.

The TWAI controller is **also** held in listen-only mode. Both halves of the golden rule
are in force, and neither depends on the other.

## Indicators

There is **no LED status indication for logging** in this build.

| LED | GPIO | Role |
|-----|:----:|------|
| Remote (J4 pin 2, via `Q1`) | **IO18** | DE-09 brake-light preview, and nothing else |
| **D5** green (onboard) | IO2 | **unused** |
| **D6** red (onboard) | IO1 | **unused** by default; opt-in fatal-error blink |

- The remote LED is the only LED the rider can see. Sharing it with logger status would
  make the FSM decision unreadable, which is the whole point of the build.
- D5/D6 are on the PCB inside the sealed enclosure, so on a ride they tell nobody
  anything. With `CONFIG_LOGGER_FAULT_LED_ENABLE` (**default off**, bench use) a latched
  fatal error — no card, mount failure, CAN start failure, transceiver not silent —
  blinks `CONFIG_LOGGER_FAULT_LED_GPIO` (default D6/IO1) at 2 Hz. Nothing more. With it
  off the firmware touches neither onboard LED and both pins stay free.
- **"Silent" means no LED, not no logging.** The full operations log still goes to the
  console, and it is now the only diagnostic surface — do not raise the log level above
  `INFO` for a ride.

## Hardware / wiring

As-built pin map, from the exported KiCad netlist — the same table as
[`../hardware/BRINGUP.md §0`](../hardware/BRINGUP.md). Do **not** take pin numbers
from the older `logger_sch_revA.pdf` or from `hardware/README.md`; both are stale.

| Signal | GPIO | Notes |
|--------|:----:|-------|
| Pushbutton (BTN_SIG) | **IO6** | J4 pin3; `R5` pull-up, active-low — **unused in this build** |
| CAN TXD | **IO21** | → U2 (TCAN330) pin1; `R15` pull-up |
| CAN RXD | **IO47** | ← U2 pin4 |
| CAN S (silent) | **IO35** | U1 **pin 28**; `R16` pull-up → defaults silent, then driven high |
| SD CLK / CMD | **IO9** / **IO10** | J5 pin5 (via `R12`) / pin3 |
| SD DAT0–3 | **IO48 / IO17 / IO12 / IO11** | J5 pin7 / 8 / 1 / 2 |
| SD card-detect (DET_A) | **IO8** | J5 pin10; `R17` pull-up, **active-low** |
| **Remote LED drive** | **IO18** | `Q1` gate via `R3`, low-side to J4 pin2; **active-high** |
| Status LED D5 (green) | **IO2** | via `R20`, active-high — unused |
| Status LED D6 (red) | **IO1** | via `R21`, active-high — unused unless the fault LED is enabled |

All of these, plus bit rate, listen-only, the SDMMC bus width, the recording timeouts
and the FSM tunables, are configurable in `menuconfig` — the app's own menus
(*CAN logger configuration*, *Automatic recording policy*, *microSD (SDMMC)*,
*Indicators*) plus *Component config → Braking state machine (DE-09)* for the tunables.
Defaults: **500 kbit/s**, **listen-only**, **4-bit SDMMC**.

The ESP32-S3 has a **real SDMMC host** whose slot pins route through the GPIO matrix, so
all six are set explicitly from the Kconfig values rather than inherited from a fixed
IO-MUX slot assignment.

> **Internal pull-ups are deliberately left disabled** on DET_A and on the CAN silent
> pin's boot-time readback, and `SDMMC_SLOT_FLAG_INTERNAL_PULLUP` is not set on the SD
> bus. The board carries proper external pull-ups (`R17`, `R16`, `R7`–`R11`); an internal
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

A cleanly closed file ends with two comment lines — `;dropped-frames: N` and
`;closed: <reason>`. **Their absence means the file was cut short** by a power loss or a
card removal rather than closed, which is now a normal thing to check for.

> **No on-board RTC** → `$STARTTIME` is a fixed placeholder; only the **relative time
> offsets** between frames are meaningful. That is exactly what offline decode/replay
> uses, so captures analyse correctly.

## Structure

```
software/
├── CMakeLists.txt                 top-level ESP-IDF project; EXTRA_COMPONENT_DIRS -> ../../components
├── sdkconfig.defaults             module/app defaults (generated sdkconfig is gitignored)
├── sdkconfig.defaults.esp32s3     target-specific: native-USB console
├── bringup/                       standalone bring-up scratch apps + reset soak script
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml          no managed components (iot_button dropped)
    ├── Kconfig.projbuild          pins, bit rate, listen-only, queue depth, SD width,
    │                              recording policy, indicators
    ├── trc_format.[ch]            pure PCAN .trc formatting (host-testable)
    ├── ui_log.[ch]                operations log to the console
    ├── logger_time.h              the one monotonic ms clock everything shares
    ├── can_tap.[ch]               the live decoder instance: fed every frame, snapshot
    │                              under a mutex for the FSM and the recording gate
    ├── fsm_preview.[ch]           50 Hz DE-09 tick + remote LED + transition log
    ├── fault_led.[ch]             opt-in fatal-error blink (default off)
    └── logger_main.c              app_main: TWAI, microSD, recording gate, RX + writer
                                   + card-detect tasks
```

Shared, ESP-IDF-free cores live outside this directory so the transmitter compiles the
same copies — see [`components/README.md`](../../components/README.md):

```
components/
├── chmbl_can/     DE-08: bike_profile.h, the generated Triumph TR table, can_decode.[ch]
└── brake_fsm/     DE-09: brake_fsm.[ch] + the tunables Kconfig
```

`trc_format`, `chmbl_can` and `brake_fsm` are all deliberately platform-independent (no
IDF headers) so they can be host-tested, per
[`docs/firmware.md §4`](../../docs/firmware.md#4-build--toolchain).

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
(v5.2+; CI uses v5.3). With the IDF environment sourced:

```bash
cd logger/software
idf.py set-target esp32s3
idf.py menuconfig      # optional: pins, bit rate, mode, SD width, FSM tunables
idf.py build
idf.py flash monitor   # console rides native USB — no UART bridge on this board
```

No component-manager download step: there are no managed dependencies, and the shared
`components/` directory is picked up through `EXTRA_COMPONENT_DIRS` in the top-level
`CMakeLists.txt`.

> **Build for 4 MB.** U1 is an `ESP32-S3-WROOM-1-N4`: 4 MB flash, **no PSRAM**
> (`CONFIG_SPIRAM` must be *absent*, not merely `=n`). Both are handled in
> `sdkconfig.defaults`. Build at 8 MB by mistake and the app boot-loops before
> `app_main` — see [`BRINGUP.md §3`](../hardware/BRINGUP.md) for the exact signature
> and why the bootloader's `SPI Flash Size` line is not evidence.

## Host tests

The two pure cores this app shares with the transmitter are tested off-target, with no
ESP-IDF involved:

```bash
cmake -S ../../transmitter/software/test_host -B /tmp/th -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/th
python3 ../../tools/golden_check.py --harness /tmp/th/trc_replay   # DE-08 vs cantools
python3 ../../tools/fsm_check.py    --harness /tmp/th/fsm_replay   # DE-09 vs the reference
```

`fsm_check.py` replays each committed capture in [`../`](..) through the **real** decode
and the **real** FSM at the 50 Hz grid and asserts the result matches the tuned Python
reference. Both are CI jobs
([`firmware-build.yml`](../../.github/workflows/firmware-build.yml)).

## CI

[`.github/workflows/firmware-build.yml`](../../.github/workflows/firmware-build.yml)
builds this project with the real ESP-IDF toolchain (`espressif/esp-idf-ci-action`,
target `esp32s3`) on every push/PR that touches the firmware or `components/`,
confirming it compiles, and runs the two host jobs above.
