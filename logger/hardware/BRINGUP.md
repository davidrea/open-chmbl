# Logger PCB — bring-up plan

Board bring-up for the custom **ESP32-S3-WROOM-1-N8** CAN logger
([`logger.kicad_sch`](logger.kicad_sch) / [`logger.kicad_pcb`](logger.kicad_pcb)).
Target reader: experienced embedded engineer. Each section is a self-contained gate —
finish and sign it off before moving to the next. Work USB-powered on the bench until
§8; keep the bike 12 V path out of it until the low-voltage side is proven.

> **Firmware approach:** the logger app in [`../software/`](../software) still targets
> the retired ESP-WROVER-KIT (`esp32`) and is **not yet ported** to this board. Rather
> than throwaway scratch apps, bring-up is done by **porting the logger firmware to
> `esp32s3` and building I/O-exercise routines into it** — a **bring-up/self-test mode**
> (Kconfig-gated, entered via a console command or a boot-time button hold) that
> exercises each peripheral in turn (LEDs, button, CAN silent/loopback, SDMMC, rails
> readout). The port work — `set-target esp32s3`, the §0 pin map, drop `CONFIG_SPIRAM`,
> the two-LED status indicator (IO1/IO2, replacing the WROVER-KIT single red die) — is
> done incrementally as the sections below are cleared, so bring-up validates the real
> firmware and the diagnostic mode ships with it. Sequence the port to match the
> section order: rails/boot/flash first, then LEDs+button, CAN, SDMMC, integration.

---

## 0. As-built pin map (from netlist)

Verified against the exported KiCad netlist of the committed schematic. Use this, not
the older `logger_sch_revA.pdf` and not the README GPIO references.

| Signal | GPIO | U1 pin | Net / notes |
|--------|:----:|:------:|-------------|
| CAN TXD | **IO21** | 23 | → U2 (TCAN330) pin1 TXD; `R15` pull-up to 3V3 |
| CAN RXD | **IO47** | 24 | ← U2 pin4 RXD |
| CAN S (silent) | **IO35** | 28 | → U2 pin8 S; `R16` pull-up to 3V3 → **defaults silent** |
| SD CLK | **IO9** | 17 | J5 pin5 via series `R12` |
| SD CMD | **IO10** | 18 | J5 pin3; `R9` pull-up |
| SD DAT0 | **IO48** | 25 | J5 pin7; `R10` pull-up |
| SD DAT1 | **IO17** | 10 | J5 pin8; `R11` pull-up |
| SD DAT2 | **IO12** | 20 | J5 pin1; `R7` pull-up |
| SD DAT3/CD | **IO11** | 19 | J5 pin2; `R8` pull-up |
| SD card-detect (DET_A) | **IO8** | 12 | J5 pin10; `R17` pull-up, active-low |
| Button (BTN_SIG) | **IO6** | 6 | J4 pin3; `R5` pull-up, **active-low** |
| Ext LED drive | **IO18** | 11 | Q1 (2N7002K) gate via `R3`; `R18` gate pull-down; low-side to J4 pin2 |
| Status LED D5 | **IO2** | 38 | via `R20`, active-high |
| Status LED D6 | **IO1** | 39 | via `R21`, active-high |
| USB D− / D+ | — | 13 / 14 | J2; ESD via U3 (USBLC6-2P6). **Native USB**, no UART bridge |
| UART0 TX / RX (console TPs) | GPIO43 / 44 | 37 / 36 | `TP2` / `TP1` — boot-ROM log at 115200 |
| EN / reset | — | 3 | `SW1`, `R13` pull-up, `C9` |
| BOOT (IO0) | IO0 | 27 | `SW2` to GND (download mode) |

> **Strapping status (this rev is clean):** IO0 = boot button (internal PU),
> IO45 = **unconnected** (internal PD → 3.3 V flash, correct), IO46 = unconnected,
> IO3 = unconnected. The `hardware/README.md §5` "GPIO45 silent-pin" issue **no longer
> applies** — S was moved to the non-strapping IO35. Confirm in §4 anyway.

---

## 1. Pre-power inspection

**Objective:** catch assembly defects and rail shorts before any power is applied.

| # | Step |
|---|------|
| 1 | Inspect U1, U2, U3, U4 under magnification: orientation (pin-1), solder bridges, tombstoning, missing parts vs. BOM. |
| 2 | Confirm D4 (20CJQ060) and D3 (SM24CANB TVS) orientation; confirm C1–C9 populated. |
| 3 | DMM continuity, power OFF: check **+3V3 → GND**, **+5VD → GND**, and **VBUS → GND** are each **not** a dead short. |
| 4 | Confirm GND continuity across J2 shell, J3 pin4, J4 pin1, J5 GND, U1 pad. |
| 5 | Ohm out `R16` (IO35↔3V3) and `R15` (IO21↔3V3) present; confirm IO45 has **no** stuffed pull-up (validates the silent-pin rework). |
| 6 | Verify J5 (microSD) socket seating and that DET_A (`R17`) pull-up is present. |

---

## 2. Power rails (USB-C bench power)

**Objective:** bring up +5VD and +3V3 from USB-C only, current-limited, before the MCU is trusted.

| # | Step |
|---|------|
| 1 | Bench supply or USB with current limit ~150 mA. Apply USB-C to J2. Watch inrush; a hard limit trip = short → stop. |
| 2 | Measure **+5VD** ≈ 5 V (one Schottky drop below VBUS is expected only on the 12 V path, not USB VBUS). |
| 3 | Measure **+3V3** = 3.3 V ±3% at U4 output / L1 / a 3V3 test point. Check ripple on scope (< ~30 mVpp). |
| 4 | Thermal check: U4 (TPS62172) and U1 not hot after 1 min at idle. |
| 5 | Confirm CC1/CC2 5.1 kΩ pull-downs present (UFP sink) — host should supply 5 V without negotiation. |
| 6 | Remove power; raise supply limit to ~500 mA for subsequent sections. |

---

## 3. USB enumeration, boot & flash

**Objective:** prove native-USB, boot strapping, and the flash/console path.

| # | Step |
|---|------|
| 1 | Connect J2 to host. Confirm the ESP32-S3 USB-serial-JTAG device enumerates (`lsusb` / dmesg / Device Manager). |
| 2 | Optionally attach a scope/UART to `TP2` (GPIO43, 115200) and confirm second-stage boot-ROM log on reset (`SW1`). |
| 3 | Enter download mode: hold `SW2` (BOOT), tap `SW1` (EN), release `SW2`. Run `esptool.py chip_id` / `flash_id`. |
| 4 | Confirm chip = ESP32-S3, flash = 8 MB, **PSRAM = none** (matches `-N8`). A flash-read error here points at strapping/IO45 — recheck §1.5. |
| 5 | Bring up the `esp32s3` firmware skeleton (target set, pin map stubbed, `CONFIG_SPIRAM` dropped); confirm it boots from normal boot (no `SW2` held) and its console log prints over native USB. |
| 6 | Confirm auto-reset-to-download works from the flasher (DTR/RTS via native USB) so later steps don't need the button dance. |

---

## 4. GPIO & strapping verification

**Objective:** confirm strapping pins settle correctly and the silent-pin rework is real on this board.

| # | Step |
|---|------|
| 1 | Via the firmware's self-test mode, toggle status LEDs on **IO2 (D5)** and **IO1 (D6)**; confirm each lights (active-high). Wire these into the ported status-indicator module as you go. |
| 2 | Read **IO35** at reset (before driving it): scope/logic-probe should show it **high** (R16 pull-up) — this is the CAN-silent default, now on a non-strapping pin. |
| 3 | Confirm IO45 floats near 0 V at boot (internal PD) and the board boots repeatably across ~10 EN resets — no intermittent flash errors. |
| 4 | Read **BTN_SIG (IO6)**: high at idle (R5), goes low when J4 pin3 is shorted to GND. |
| 5 | Read **DET_A (IO8)**: high with no card, low with a card seated in J5. |

---

## 5. CAN transceiver (U2 TCAN330)

**Objective:** prove TWAI ↔ TCAN330 path and hardware silent-mode control, listen-only first.

| # | Step |
|---|------|
| 1 | Drive **IO35 low** from firmware (S = normal mode) only after §5.3; leave default (high/silent) for the first powered check. |
| 2 | TWAI self-test / loopback via the firmware's self-test mode (`esp32s3`, IO21 TX / IO47 RX): confirm the controller reaches error-active and frames loop back internally. |
| 3 | Connect J3 CAN-H/CAN-L to a known 500 kbit/s bus (PCAN-USB or second node) with proper termination. Keep **listen-only** (S high). |
| 4 | Confirm frames are received and timestamped; verify **no ACK / no TX** on the bus with a scope on CAN-H (golden-rule listen-only). |
| 5 | Only on an isolated bench bus: drive IO35 low, send a frame, confirm normal-mode TX works and TCAN330 sources a dominant bit. |
| 6 | Scope CAN-H/CAN-L differential for clean levels (~2.5 V recessive, ~1 V diff dominant); confirm D3 TVS not clamping under normal signaling. |

---

## 6. microSD (SDMMC 4-bit)

**Objective:** prove the full 4-bit SDMMC bus that drove the S3 choice.

| # | Step |
|---|------|
| 1 | Insert a known-good card. Confirm DET_A (IO8) reads inserted (§4.5). |
| 2 | Mount 1-bit SDMMC first (CLK IO9, CMD IO10, DAT0 IO48); confirm init, CID/CSD read, capacity correct. |
| 3 | Switch to **4-bit** (add DAT1 IO17, DAT2 IO12, DAT3 IO11); confirm mount and error-free init. |
| 4 | Sequential write/read a multi-MB file, CRC-verify. Push clock to the intended rate; watch for CRC/timeout errors indicating SI issues on DAT lines. |
| 5 | Sustained-write throughput test at target bitrate-equivalent load; confirm no overruns (this is the capture-path bottleneck). |
| 6 | Card insert/remove cycling: confirm DET_A transitions and no bus lock-up. |

---

## 7. Button & LEDs (J4)

**Objective:** validate the operator control breakout.

| # | Step |
|---|------|
| 1 | On J4: pin1 GND, pin2 LED drive, pin3 BTN_SIG, pin4 +3V3. Confirm 3V3 present on pin4. |
| 2 | Wire an external LED (pin4 → LED → pin2). Drive **IO18 high**; confirm Q1 sinks and LED lights; low = off. Check R4 sets sane current. |
| 3 | Wire a button (pin3 → button → pin1/GND). Confirm debounced active-low reads on IO6. |
| 4 | Confirm D1/D2 TVS on the J4 signal lines don't distort BTN_SIG logic levels. |

---

## 8. Bike 12 V power path

**Objective:** validate the automotive input, diode-OR, and reverse-polarity protection — last, and deliberately.

> Standard (non-automotive) buck and a Schottky for reverse protection — **bench/ride
> use only**, not a permanent bike install (per README §2). No load-dump clamp here.

| # | Step |
|---|------|
| 1 | Bench supply to J3 pin3 (+12 V) / pin4 (GND), current-limited. Confirm **+5VD** ≈ 12 V − Vf(D4) is blocked correctly — D4 should drop to +5VD only via the rail's regulation path; verify +3V3 = 3.3 V. |
| 2 | **Reverse-polarity test:** swap J3 +12/GND; confirm D4 blocks, no current flows, rails stay at 0, no damage. |
| 3 | Diode-OR test: power both USB-C and 12 V; confirm no backfeed from +5VD onto J3 pin3 and no USB VBUS onto the 12 V harness. |
| 4 | Sweep input 9–15 V; confirm +3V3 stays in regulation and U4 thermals are acceptable. |
| 5 | CAN TVS (D3) sanity: confirm normal CAN signaling on the 12 V-powered board is unaffected. |

---

## 9. Integration & soak

**Objective:** confirm the whole capture chain end-to-end under realistic load.

| # | Step |
|---|------|
| 1 | Run the fully-ported logger firmware (self-test mode off): listen-only CAN capture → timestamp → `.trc` write to microSD, button start/stop, LED status. |
| 2 | Capture a live 500 kbit/s bus for ≥30 min; confirm zero dropped frames and file integrity (`python-can` TRCReader / `tools/trc_viz.html`). |
| 3 | Power-source hot-swap USB↔12 V mid-capture; confirm the board survives the diode-OR handoff (data loss on cut is acceptable — out of scope). |
| 4 | Thermal soak at 12 V for ≥1 hr; log rail voltages and U1/U2/U4 temps. |
| 5 | EN-reset and power-cycle stress (×20): confirm deterministic boot every time. |

---

## 10. Sign-off checklist

- [ ] No rail shorts; +5VD and +3V3 in spec on both USB and 12 V
- [ ] Native USB enumerates; flash 8 MB / no PSRAM confirmed; repeatable boot
- [ ] Strapping clean; **IO35 silent-pin rework confirmed** (IO45 unstuffed/floating)
- [ ] CAN RX in listen-only verified; no bus ACK/TX; normal-mode TX works on isolated bus
- [ ] microSD 4-bit mount + sustained write, CRC-clean
- [ ] Button + external/onboard LEDs functional
- [ ] Reverse-polarity and diode-OR protection verified
- [ ] ≥30 min live capture, zero drops; thermal soak passed
- [ ] `esp32s3` firmware port complete (pin map, two-LED status, no PSRAM); self-test mode merged and Kconfig-gated off for production builds

---

*Pin map derived from the committed KiCad schematic netlist. Re-verify against the
schematic if the board is respun.*
