# Logger PCB — bring-up plan

Board bring-up for the custom **ESP32-S3-WROOM-1-N4** CAN logger
([`logger.kicad_sch`](logger.kicad_sch) / [`logger.kicad_pcb`](logger.kicad_pcb)).
Target reader: experienced embedded engineer. Each section is a self-contained gate —
finish and sign it off before moving to the next. Work USB-powered on the bench until
§8; keep the bike 12 V path out of it until the low-voltage side is proven.

> **Status column:** every step table carries one. `✅ <date>` = verified on hardware ·
> `◐ <date>` = partially verified, see the notes under that table · `⚠→✅` / `⚠→◐` =
> failed first, passed after a rework — **read the note under that table before trusting
> the result** · `—` = not yet run · `n/a` = optional/skipped. Dates are the board this
> plan tracks (MAC `ac:27:6e:c2:c9:38`); re-run from scratch for a new board.

> **⚠ This board carries a rework: U3 (USB ESD protection) is removed.** See §2. It is
> not as-designed, and §10 cannot be signed off until U3 is replaced and §2 re-run.

> **Firmware approach:** the logger app in [`../software/`](../software) **is now
> ported** to this board — target `esp32s3`, the §0 pin map, `CONFIG_SPIRAM` dropped,
> 4 MB flash, native-USB console, and a three-LED indicator (external IO18 + D5/D6)
> replacing the WROVER-KIT's single red die. Backwards compatibility with the
> WROVER-KIT was explicitly dropped. It builds clean; **everything below marked `—`
> is still unverified on hardware.**
>
> Two things the port carries that bring-up should lean on: the CAN silent pin (IO35)
> is **read back high-impedance at boot and then driven high**, with a loud console
> warning if the hi-z read is not high (that is §4.2 answered from the chip side — and
> the readback comes first precisely so a missing `R16` is reported rather than masked
> by the drive), and *microSD → SDMMC bus width* is Kconfig-selectable 1-bit/4-bit with
> the negotiated width printed at mount, which is exactly the §6.2 → §6.3 progression.
>
> **The firmware has since changed shape** (see
> [`../software/README.md`](../software/README.md)): it is now the **ride-validation
> build**, which also runs the [DE-09](../../docs/design/de-09-brake-decel-logic.md)
> braking state machine on-board and drives **IO18** from its output. Consequences for
> this plan:
>
> * **§7.2 is now exercised by the shipping firmware** — see the note under §7.
> * **§7.3 (button) is not.** Recording is gated automatically on the decoded engine
>   kill switch; the pushbutton is unused, and the `iot_button` dependency is gone. Use
>   a scratch app for IO6.
> * **§9.1 changed:** there is no button start/stop and no LED status to verify. What
>   §9.1 should now confirm is automatic start on kill-switch RUN, close on STOP and on
>   bus silence, the periodic flush/`fsync` leaving a readable file after a power cut,
>   and the FSM transition log tracking the LED. Treat the step's wording as stale, not
>   the objective.
> * **§4.1's LED pass still stands** — D5/D6 are now unused by default (they are
>   invisible inside the enclosure), which does not invalidate the result.
>
> **Still outstanding:** the Kconfig-gated **self-test mode** described in the original
> plan — a console command or boot-time button hold that walks each peripheral in turn
> — was **not** built. The §4/§5/§6 steps that name it should be read as "exercise this
> with the ported firmware", using the boot log and the LEDs, or with a scratch app in
> [`../software/bringup/`](../software/bringup/).

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

| # | Step | Status |
|---|------|:------:|
| 1 | Inspect U1, U2, U3, U4 under magnification: orientation (pin-1), solder bridges, tombstoning, missing parts vs. BOM. | ✅ 07-26 |
| 2 | Confirm D4 (20CJQ060) and D3 (SM24CANB TVS) orientation; confirm C1–C9 populated. | ✅ 07-26 |
| 3 | DMM continuity, power OFF: check **+3V3 → GND**, **+5VD → GND**, and **VBUS → GND** are each **not** a dead short. | ✅ 07-26 |
| 4 | Confirm GND continuity across J2 shell, J3 pin4, J4 pin1, J5 GND, U1 pad. | ✅ 07-26 |
| 5 | Ohm out `R16` (IO35↔3V3) and `R15` (IO21↔3V3) present; confirm IO45 has **no** stuffed pull-up (validates the silent-pin rework). | ✅ 07-26 |
| 6 | Verify J5 (microSD) socket seating and that DET_A (`R17`) pull-up is present. | ✅ 07-26 |

> **§1 complete — all six steps passed, manual inspection 2026-07-26.** No assembly
> defects, no rail shorts. Step 5 clears the silent-pin rework at the board level
> (`R16`/`R15` present, IO45 pull-up unstuffed), which §3/§4.3 later corroborated from
> the chip side via esptool's 3.3 V flash-voltage report.

---

## 2. Power rails (USB-C bench power)

**Objective:** bring up +5VD and +3V3 from USB-C only, current-limited, before the MCU is trusted.

| # | Step | Status |
|---|------|:------:|
| 1 | Bench supply or USB with current limit ~150 mA. Apply USB-C to J2. Watch inrush; a hard limit trip = short → stop. | ⚠→✅ |
| 2 | Measure **+5VD** ≈ 5 V (one Schottky drop below VBUS is expected only on the 12 V path, not USB VBUS). | ⚠→✅ |
| 3 | Measure **+3V3** = 3.3 V ±3% at U4 output / L1 / a 3V3 test point. Check ripple on scope (< ~30 mVpp). | ⚠→◐ |
| 4 | Thermal check: U4 (TPS62172) and U1 not hot after 1 min at idle. | — |
| 5 | Confirm CC1/CC2 5.1 kΩ pull-downs present (UFP sink) — host should supply 5 V without negotiation. | — |
| 6 | Remove power; raise supply limit to ~500 mA for subsequent sections. | — |

> ### ⚠ 2.1–2.3 FAILED on first attempt — U3 destroyed, and **U3 is now depopulated**
>
> **Timeline:** first power application **2026-07-26** (USB-C, supply limited to 150 mA)
> tripped the limit — a **soft short from VBUS to GND**, present **only under power**;
> an unpowered DMM check was clean, which is why §1.3 passed. Fault bisected to
> **U3 (USBLC6-2P6)**. Board left unpowered until hot air was available; **U3 removed
> 2026-08-03**, which cleared the short, and 2.1–2.3 passed the same day. Applying power
> appears to have damaged U3.
>
> **The board this plan tracks is therefore NOT as-designed: U3 is absent, so J2's
> `D+`/`D−` have no ESD protection.** Handle the USB port accordingly, and do not sign
> off §10 until U3 is replaced and §2 re-run on the reworked board.
>
> *To confirm:* the §3/§4 results are also dated 2026-08-03, the same day as the rework,
> so whether they were taken before or after U3 came off is not recorded. It does not
> change any of those results — the fault was a supply-side leakage that a
> current-limited bench supply caught and a normal USB host would likely have sourced
> through — but note it if the §3/§4 numbers ever need to be attributed precisely.
>
> **Root cause not established.** A "short only when biased" is the signature of a
> damaged junction, and it fits a defective, ESD-damaged, or reflow-damaged part. It is
> **not** explained by overvoltage on the clamp pin: U3 pin 5 (VBUS, 5.25 V max, VBR
> min 6.0 V) sits on `+5VD`, and `+5VD` is **USB VBUS only** — D4 is a common-cathode
> dual Schottky whose shared cathode (pin 2) is the *regulator input* node, so the
> 12 V path ORs in there and the `+5VD`-side diode reverse-biases. 12 V never reaches
> `+5VD` or U3. Worth checking on the replacement: correct part (LCSC `C15999`), pin-1
> orientation, and ESD handling during rework.
>
> **Open gap:** steps **2.4–2.6** (thermals, CC pull-downs, raise the current limit)
> were never reported either way — marked `—` rather than assumed. They will need doing
> on the reworked board regardless, once U3 is replaced.
>
> **2.3 partial:** board VCC measured **3.24 V** on USB power — in spec (3.3 V −1.8%).
> Ripple not yet scoped, and this was a spot DMM reading rather than a measurement at
> U4 output / L1 / a labelled 3V3 test point. Re-take properly to close the step.

---

## 3. USB enumeration, boot & flash

**Objective:** prove native-USB, boot strapping, and the flash/console path.

| # | Step | Status |
|---|------|:------:|
| 1 | Connect J2 to host. Confirm the ESP32-S3 USB-serial-JTAG device enumerates (`lsusb` / dmesg / Device Manager). | ✅ 08-03 |
| 2 | Optionally attach a scope/UART to `TP2` (GPIO43, 115200) and confirm second-stage boot-ROM log on reset (`SW1`). | n/a |
| 3 | Enter download mode: hold `SW2` (BOOT), tap `SW1` (EN), release `SW2`. Run `esptool.py chip_id` / `flash_id`. | ◐ 08-03 |
| 4 | Confirm chip = ESP32-S3, flash = **4 MB**, **PSRAM = none** (matches `-N4`). A flash-read error here points at strapping/IO45 — recheck §1.5. | ✅ 08-03 |
| 5 | Bring up the `esp32s3` firmware skeleton (target set, pin map stubbed, `CONFIG_SPIRAM` dropped); confirm it boots from normal boot (no `SW2` held) and its console log prints over native USB. | ✅ 08-03 |
| 6 | Confirm auto-reset-to-download works from the flasher (DTR/RTS via native USB) so later steps don't need the button dance. | ✅ 08-03 |

> **Build firmware for 4 MB (`CONFIG_ESPTOOLPY_FLASHSIZE_4MB`).** U1 is an
> `ESP32-S3-WROOM-1-N4` per the schematic and production BOM (LCSC `C2913197`):
> **4 MB flash, no PSRAM.** Confirmed on the bench — `esptool.py flash_id` reports
> JEDEC `c8`/`4016` = GigaDevice GD25Q32, 32 Mbit.
>
> Build for 8 MB by mistake and the app boot-loops before `app_main`, with
> `E spi_flash: Detected size(4096k) smaller than the size in the binary image
> header(8192k)` followed by `assert failed: __esp_system_init_fn_init_flash
> startup_funcs.c:114`. **The 2nd-stage bootloader prints `SPI Flash Size : 8MB`
> on the way up in that case** — that echoes the image header, not a probe of the
> part, so it is not evidence about the fitted flash. Only the runtime probe and
> `flash_id` are. Plan partition sizing and any OTA headroom against 4 MB.

**As-built results:** USB-serial-JTAG enumerates as `/dev/cu.usbmodem101`; chip =
ESP32-S3 (QFN56) rev v0.2, no PSRAM; flash = 4 MB (matches `-N4`); esptool reports
*"Flash voltage set by a strapping pin to 3.3V"*, which independently clears the §1.5 /
§4.3 IO45 concern.

> **3.5 pass, via the scratch app — the logger port itself is still outstanding.**
> Verified in [`../software/bringup/blink/`](../software/bringup/blink/):
> `CONFIG_IDF_TARGET="esp32s3"`; **no `CONFIG_SPIRAM`** (the option is absent entirely,
> not merely unset — correct for a module with no PSRAM); pin map stubbed to the §0
> assignments (IO1/IO2/IO8); console on `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` with the
> UART console disabled (`CONFIG_ESP_CONSOLE_UART_NUM=-1`), and logs read back over
> native USB throughout §4; boots from normal boot with `SW2` never touched, 20/20
> across the §4.3 soak.
>
> That satisfies everything this step asks for, but note **what it is**: a scratch app,
> not the logger firmware. The port the firmware-approach note calls for — real app on
> `esp32s3`, two-LED status indicator, self-test mode — is unchanged and still tracked
> in §10. This step proves the *board* runs an `esp32s3` build; it does not mean the
> port is done.

> **3.3 partial:** `chip_id` and `flash_id` both ran clean, but via **auto-reset**, not
> the documented `SW2`/`SW1` button dance — the manual path is still unproven. Step 6
> makes that acceptable in practice (auto-reset works over native USB, no dance needed),
> but do the button sequence once if `SW1`/`SW2` are to be considered tested.
>
> **3.2 skipped:** step is marked optional and the native-USB console served the same
> purpose. `TP1`/`TP2` remain unverified.

---

## 4. GPIO & strapping verification

**Objective:** confirm strapping pins settle correctly and the silent-pin rework is real on this board.

| # | Step | Status |
|---|------|:------:|
| 1 | Via the firmware's self-test mode, toggle status LEDs on **IO2 (D5)** and **IO1 (D6)**; confirm each lights (active-high). Wire these into the ported status-indicator module as you go. | ✅ 08-03 |
| 2 | Read **IO35** at reset (before driving it): scope/logic-probe should show it **high** (R16 pull-up) — this is the CAN-silent default, now on a non-strapping pin. | — |
| 3 | Confirm IO45 floats near 0 V at boot (internal PD) and the board boots repeatably across ~10 EN resets — no intermittent flash errors. | ✅ 08-03 |
| 4 | Read **BTN_SIG (IO6)**: high at idle (R5), goes low when J4 pin3 is shorted to GND. | — |
| 5 | Read **DET_A (IO8)**: high with no card, low with a card seated in J5. | ✅ 08-03 |

> **4.1 pass:** both LEDs light active-high. The test pattern drives red alone, then
> green alone, before alternating — so it also confirms IO1/IO2 are **not swapped**
> relative to the §0 netlist map, which a simultaneous blink could not distinguish.
> Done with the scratch app [`../software/bringup/blink/`](../software/bringup/blink/),
> **not** the self-test mode. The result stands, but note the firmware moved on: D5/D6
> are now **unused by default** (they are invisible inside the sealed enclosure), with
> only an opt-in, default-off fatal-error blink on D6
> (`CONFIG_LOGGER_FAULT_LED_ENABLE`, `fault_led.[ch]`). The IO1/IO2 assignments this
> step confirmed are carried there. The external LED on IO18 is **not** covered by this
> step — it is now the DE-09 brake-light preview and is still §7.2.
>
> **4.5 pass:** IO8 reads **high with J5 empty** and **low with a card seated**, and
> transitions cleanly on insert/remove at runtime. Read with the **internal pull-up
> disabled**, so the high state is `R17` doing the work — an internal pull would have
> read high even with R17 missing and turned a real failure into a false pass. Scratch
> app [`../software/bringup/blink/`](../software/bringup/blink/) prints the level at boot
> and on every change.
>
> **4.3 pass:** two halves, both covered.
>
> *Strapping:* esptool reports *"Flash voltage set by a strapping pin to 3.3V"* — the
> fact the step is after, read from the chip rather than probed at the pin.
>
> *Repeatability:* **20/20 clean boots**, scripted via
> [`../software/bringup/reset_soak.py`](../software/bringup/reset_soak.py) — twice the
> ~10 the step asks for. Every cycle was a genuine reboot (`rst=USB`, uptime back to
> 507 ms), with no error logs, asserts or reboot loops. A chip reset re-runs the boot
> ROM, re-samples strapping and re-reads flash, so intermittent flash or strapping
> faults would have surfaced.
>
> **Caveat — SW1 and the EN RC network are still untested.** The soak commands resets
> over USB rather than pulling the EN pin, so `SW1`/`R13`/`C9` are not exercised. That
> gap is tracked in the §3.3 note; press SW1 by hand a few times to close it.
>
> **Two traps the script documents, both of which silently produce wrong results:**
> `USBJTAGSerialReset` is the *enter-download-mode* sequence (it drives IO0 low and
> parks the board in the ROM bootloader — it does not restart the app), and pyserial
> asserts DTR on open, which stops the USB-JTAG peripheral acting on `HardReset`'s RTS
> pulse. Either one leaves the board simply running on, which is why the script proves
> each reboot from a *fresh uptime* rather than assuming the reset worked.

---

## 5. CAN transceiver (U2 TCAN330)

**Objective:** prove TWAI ↔ TCAN330 path and hardware silent-mode control, listen-only first.

| # | Step | Status |
|---|------|:------:|
| 1 | Drive **IO35 low** from firmware (S = normal mode) only after §5.3; leave default (high/silent) for the first powered check. | — |
| 2 | TWAI self-test / loopback via the firmware's self-test mode (`esp32s3`, IO21 TX / IO47 RX): confirm the controller reaches error-active and frames loop back internally. | — |
| 3 | Connect J3 CAN-H/CAN-L to a known 500 kbit/s bus (PCAN-USB or second node) with proper termination. Keep **listen-only** (S high). | — |
| 4 | Confirm frames are received and timestamped; verify **no ACK / no TX** on the bus with a scope on CAN-H (golden-rule listen-only). | — |
| 5 | Only on an isolated bench bus: drive IO35 low, send a frame, confirm normal-mode TX works and TCAN330 sources a dominant bit. | — |
| 6 | Scope CAN-H/CAN-L differential for clean levels (~2.5 V recessive, ~1 V diff dominant); confirm D3 TVS not clamping under normal signaling. | — |

---

## 6. microSD (SDMMC 4-bit)

**Objective:** prove the full 4-bit SDMMC bus that drove the S3 choice.

| # | Step | Status |
|---|------|:------:|
| 1 | Insert a known-good card. Confirm DET_A (IO8) reads inserted (§4.5). | ◐ 08-03 |
| 2 | Mount 1-bit SDMMC first (CLK IO9, CMD IO10, DAT0 IO48); confirm init, CID/CSD read, capacity correct. | — |
| 3 | Switch to **4-bit** (add DAT1 IO17, DAT2 IO12, DAT3 IO11); confirm mount and error-free init. | — |
| 4 | Sequential write/read a multi-MB file, CRC-verify. Push clock to the intended rate; watch for CRC/timeout errors indicating SI issues on DAT lines. | — |
| 5 | Sustained-write throughput test at target bitrate-equivalent load; confirm no overruns (this is the capture-path bottleneck). | — |
| 6 | Card insert/remove cycling: confirm DET_A transitions and no bus lock-up. | ◐ 08-03 |

> **6.1 / 6.6 partial — the DET_A half only.** §4.5 already showed IO8 reading inserted
> and transitioning cleanly on insert/remove, so the detect side of both steps is done.
> What is **not** done: 6.1's card is not yet proven *known-good* (nothing has mounted
> it), and 6.6's "no bus lock-up" is unassessable until the SDMMC bus is actually
> running. Re-run both once §6.2 mounts a card — do not treat these as cleared.

---

## 7. Button & LEDs (J4)

**Objective:** validate the operator control breakout.

| # | Step | Status |
|---|------|:------:|
| 1 | On J4: pin1 GND, pin2 LED drive, pin3 BTN_SIG, pin4 +3V3. Confirm 3V3 present on pin4. | — |
| 2 | Wire an external LED (pin4 → LED → pin2). Drive **IO18 high**; confirm Q1 sinks and LED lights; low = off. Check R4 sets sane current. | — |
| 3 | Wire a button (pin3 → button → pin1/GND). Confirm debounced active-low reads on IO6. | — |
| 4 | Confirm D1/D2 TVS on the J4 signal lines don't distort BTN_SIG logic levels. | — |

> Note: §7 covers the **external** LED on J4 via Q1 (IO18). The **onboard** status LEDs
> D5/D6 are §4.1 and are already passing — the two are independent circuits.
>
> ### 7.2 is now exercised by real firmware, not a scratch app
>
> The logger app drives **IO18** as the [DE-09](../../docs/design/de-09-brake-decel-logic.md)
> brake-light preview: active-high, a steady level, on whenever the on-board state
> machine is in `BRAKING` or `STOPPED`. So 7.2 can be run against the shipping firmware
> rather than a blink sketch — and it gives a stronger result than a bare toggle,
> because the console prints every state transition with its rule number alongside the
> LED change, so "Q1 sinks and the LED lights" and "the LED tracks the FSM" are
> confirmed together.
>
> **How to run it:** no bus needed. With the board on USB, the FSM holds the light OFF
> while wheel speed is invalid, which is itself the "low = off" half of the step. For
> the "high = lit" half, feed the board a bus (§5.3) or set
> `CONFIG_BRAKE_FSM_STOP_SPEED_MMPH` high enough that rule 2 latches `STOPPED` on a
> zeroed speed — then restore it.
>
> **Still `—`. Nothing here has been run on hardware; this note records that the
> firmware to run it with now exists, not a result.** `R4`'s current setting (the rest
> of 7.2) and §7.1/§7.3/§7.4 are untouched by this. Note that **§7.3 — the button — is
> no longer exercised by the logger firmware at all**: recording is gated automatically
> on the decoded kill switch and the pushbutton is unused, so 7.3 needs a scratch app in
> [`../software/bringup/`](../software/bringup/) or a meter on IO6.

---

## 8. Bike 12 V power path

**Objective:** validate the automotive input, diode-OR, and reverse-polarity protection — last, and deliberately.

> Standard (non-automotive) buck and a Schottky for reverse protection — **bench/ride
> use only**, not a permanent bike install (per README §2). No load-dump clamp here.

> **Rail topology (read before probing).** `+5VD` is **not** a regulated 5 V rail and is
> **not** the 12 V path: it is J2's USB VBUS net (`J2` A4/A9/B4/B9), and it also carries
> U3 pin 5. **D4 (20CJQ060) is a common-cathode dual Schottky** — anodes on `+12P`
> (pin 1) and `+5VD` (pin 3), **shared cathode on pin 2**, which is the U4 (TPS62172)
> input node. So the two supplies OR together *at D4 pin 2*, not on `+5VD`; on 12 V-only
> power `+5VD` sits near 0 V and its diode blocks backfeed toward USB. That reverse-bias
> is what §8.3 is testing.

| # | Step | Status |
|---|------|:------:|
| 1 | Bench supply to J3 pin3 (+12 V) / pin4 (GND), current-limited. Confirm the **U4 input node** (D4 pin 2, common cathode) ≈ 12 V − Vf(D4), and that **`+5VD` stays at ~0 V** with USB unplugged — the `+5VD`-side diode must reverse-bias. Verify +3V3 = 3.3 V. | — |
| 2 | **Reverse-polarity test:** swap J3 +12/GND; confirm D4 blocks, no current flows, rails stay at 0, no damage. | — |
| 3 | Diode-OR test: power both USB-C and 12 V; confirm no backfeed from +5VD onto J3 pin3 and no USB VBUS onto the 12 V harness. | — |
| 4 | Sweep input 9–15 V; confirm +3V3 stays in regulation and U4 thermals are acceptable. | — |
| 5 | CAN TVS (D3) sanity: confirm normal CAN signaling on the 12 V-powered board is unaffected. | — |

---

## 9. Integration & soak

**Objective:** confirm the whole capture chain end-to-end under realistic load.

| # | Step | Status |
|---|------|:------:|
| 1 | Run the logger firmware: listen-only CAN capture → timestamp → `.trc` write to microSD, **automatic** start on kill-switch RUN / close on STOP or bus silence, periodic flush survives a power cut, and the DE-09 transition log tracking the remote LED. (Was "button start/stop, LED status" — see the firmware-approach note.) | — |
| 2 | Capture a live 500 kbit/s bus for ≥30 min; confirm zero dropped frames and file integrity (`python-can` TRCReader / `tools/trc_viz.html`). | — |
| 3 | Power-source hot-swap USB↔12 V mid-capture; confirm the board survives the diode-OR handoff (data loss on cut is acceptable — out of scope). | — |
| 4 | Thermal soak at 12 V for ≥1 hr; log rail voltages and U1/U2/U4 temps. | — |
| 5 | EN-reset and power-cycle stress (×20): confirm deterministic boot every time. | — |

---

## 10. Sign-off checklist

- [ ] No rail shorts; +5VD and +3V3 in spec on both USB and 12 V
- [x] Native USB enumerates; flash 4 MB / no PSRAM confirmed (matches `-N4`); repeatable boot
- [ ] Strapping clean; **IO35 silent-pin rework confirmed** (IO45 unstuffed/floating)
- [ ] CAN RX in listen-only verified; no bus ACK/TX; normal-mode TX works on isolated bus
- [ ] microSD 4-bit mount + sustained write, CRC-clean
- [ ] Remote LED (IO18/Q1) functional and tracking the DE-09 FSM; onboard D5/D6 pass
      (§4.1 ✅, now unused by default); button (IO6) functional — needs a scratch app,
      the logger firmware no longer uses it
- [ ] Reverse-polarity and diode-OR protection verified
- [ ] ≥30 min live capture, zero drops; thermal soak passed
- [x] `esp32s3` firmware port complete (pin map, three-LED status, no PSRAM, 4 MB, native-USB console) — builds clean; hardware verification is the unchecked boxes above
- [ ] Self-test mode merged and Kconfig-gated off for production builds — **not built**; see the firmware-approach note at the top

---

*Pin map derived from the committed KiCad schematic netlist. Re-verify against the
schematic if the board is respun.*
