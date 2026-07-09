# Logger — hardware

Custom PCB (KiCad, [`logger.kicad_pro`](logger.kicad_pro)) that replaced the original
ESP-WROVER-KIT bring-up rig. Self-powered from the bike's 12 V or from USB-C, decodes
nothing on-board — it just timestamps and records **all** CAN traffic to a microSD card.
See [`../README.md`](../README.md) for the device-level description and
[`../software/README.md`](../software/README.md) for firmware status.

This board is also the **base design for the [`transmitter/`](../../transmitter)**
bike-side unit — see [§4](#4-shared-design-with-the-transmitter).

## 1. MCU: ESP32-S3-WROOM-1-N8

**ESP32-S3**, not the ESP32-C3 used elsewhere in this project (`brake_light/`, and
the transmitter's earlier sketch — see [`docs/hardware.md`](../../docs/hardware.md)).
The reason is the **microSD interface**: this design uses the SoC's native **SDMMC
host peripheral** (full 4-bit bus — `CLK`/`CMD`/`DAT0`–`DAT3`, see §3.4) for the
throughput a listen-only, no-filtering CAN capture needs. **The ESP32-C3 doesn't have
an SDMMC host peripheral** — only SPI — so it was not an option for this board. The
C3 stays the right choice for `brake_light/`, which has no SD card.

`-N8` = 8 MB flash, **no PSRAM**. (The retired ESP-WROVER-KIT bring-up target had
4 MB of PSRAM; the firmware's old `CONFIG_SPIRAM=y` dependency was dropped in the
port to this board — see [`../software/README.md`](../software/README.md).)

## 2. Power

```
 J3 pin 3 (+12 V, bike) ──▶ D4 (Schottky, reverse-pol.) ─┐
                                                          ├─▶ +5VD rail ─▶ U4 (TPS62172
 J2 (USB-C VBUS, 5 V)   ──▶ U3 (USBLC6-2P6 ESD) ─────────┘              buck) ─▶ +3V3
```

- **D4** (`20CJQ060`, Schottky) diode-ORs the bike's 12 V input against USB-C VBUS
  onto a shared **+5VD** rail, so the board can be bench-powered/flashed over USB-C
  without the bike connected, and the diode blocks backfeed onto the bike harness.
- **U4** (`TPS62172`, buck) steps +5VD down to **+3V3**, which powers the ESP32-S3,
  the CAN transceiver, and the microSD card.
- **D3** (`SM24CANB-02HTG`, dual TVS) sits right at J3 protecting CAN-H/CAN-L from
  bus transients.

There is no automotive load-dump-rated buck or a dedicated reverse-polarity FET here
(that was the original `docs/hardware.md` sketch) — this rev uses a Schottky diode for
reverse-polarity protection and a standard (not automotive-grade) buck. Revisit before
this board goes on a bike full-time; fine for bench/ride-logging use.

## 3. Connectors

| Ref | Type | Function |
|-----|------|----------|
| **J2** | USB-C receptacle | Power, flash, and console. **Native USB** — D+/D- wired directly to the ESP32-S3 (no USB-UART bridge chip). CC1/CC2 have 5.1 kΩ pull-downs (UFP power-sink detection). ESD-protected by U3 (`USBLC6-2P6`). |
| **J3** | JST-PH, 5-pin | Bike harness. Pin 1 **CAN-L**, pin 2 **CAN-H**, pin 3 **+12 V**, pin 4 **GND**, pin 5 **spare/unconnected**. TVS-protected at the connector (D3). |
| **J4** | JST-PH, 4-pin | Button/LED breakout. Pin 1 **GND**, pin 2 **LED drive** (through Q1, a low-side `2N7002K` switch), pin 3 **button input** (`BTN_SIG`, pulled up to 3V3, active-low), pin 4 **+3V3**. Both signal pins are TVS-protected (D1/D2, `PTVS3V3D1BALYL`) since this connector feeds an external panel-mount switch/LED on flying leads. |
| **J5** | microSD (Molex 104031-0811) | Full SDMMC **4-bit** bus (`CLK`, `CMD`, `DAT0`–`DAT3`) + card-detect (`DET_A`, pulled up) + 3V3/GND, wired straight to the ESP32-S3's SDMMC host peripheral. |

### 3.1 CAN transceiver

**U2 (`TCAN330`)**, 3.3 V logic — TXD/RXD to the ESP32-S3, plus its **silent-mode
(`S`) pin** wired to a GPIO so firmware can force RX-only operation in hardware, not
just in software. See [§5](#5-known-issue--gpio45s-pin-conflicts-with-a-boot-strap)
for a problem with exactly this pin.

### 3.2 microSD (J5)

Sized for a real ride-logging device: full 4-bit SDMMC, not the slower 1-bit/SPI
fallback. This is the peripheral that dictated the ESP32-S3 (§1).

### 3.3 Button/LED (J4)

Optional external start/stop button + status LED on flying leads (the onboard `SW1`/
`SW2` push-button footprints are DNI placeholders for a bench BOOT/EN header, not the
operator control). `Q1` (`2N7002K`) is a low-side switch for driving an LED through
J4 pin 2; `R4` is the current-limit resistor.

## 4. Shared design with the transmitter

The `transmitter/` bike-side unit reuses this exact board with **J4** (button/LED)
and **J5** (microSD) left unpopulated — the transmitter needs neither a card slot nor
a physical button/LED breakout. See
[`transmitter/hardware/README.md`](../../transmitter/hardware/README.md) and
[`docs/hardware.md §1`](../../docs/hardware.md#1-transmitter-bike-side).

## 5. Known issue — GPIO45's pin conflicts with a boot strap

**`R16`** pulls the CAN transceiver's silent-mode pin (`U2` pin 8, ESP32-S3
`GPIO45`) **up to 3V3**. `GPIO45` is one of the ESP32-S3's strapping pins — it
selects the SPI-flash/PSRAM supply voltage at reset (low = internal 3.3 V regulator,
the correct setting for this module's flash; high = expects an externally-supplied
1.8 V rail). Pulling it high at boot is the same class of bug already hit once on
this project's bench boards (GPIO8 vs. the SPI-flash bus — see the blink-LED note)
and is a known cause of ESP32-S3 boards that won't boot / report flash read errors.

The pull-up itself is sensible in isolation — it defaults the transceiver to
**silent mode before firmware runs**, which is the right failsafe for a listen-only
device — but `GPIO45` is the wrong net for it. **Verify board bring-up before relying
on this rev**; the fix is either a rework (replace `R16` with a pull-down to force
`GPIO45` low, moving the transceiver's default-silent behavior to firmware-only) or,
for a respin, moving the CAN `S` pin off `GPIO45` to a non-strapping GPIO.

## 6. Fabrication / BOM

KiCad project only — `bom.csv`, `positions.csv`, `netlist.ipc`, and the JLC
export zip under `production/` are regenerated locally by the KiCad Fabrication
Toolkit plugin and are **gitignored**, not committed. Re-export from
[`logger.kicad_pcb`](logger.kicad_pcb) / [`logger.kicad_sch`](logger.kicad_sch) as
needed.

Key parts: `U1` ESP32-S3-WROOM-1-N8, `U2` TCAN330, `U4` TPS62172, `U3` USBLC6-2P6,
`D3` SM24CANB-02HTG, `D4` 20CJQ060.
