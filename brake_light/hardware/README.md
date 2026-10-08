# Brake_light — hardware

Rider-side electronics. See [`docs/hardware.md §2`](../../docs/hardware.md#2-brake_light-rider-side)
for the block diagram and parts sketch.

Key constraints:
- **Bare ESP32-C3 module** (ESP32-C3-WROOM-02 baseline) + **chip-down** LiIon
  charger, DC-DC, and LED driver. Moved off the integrated dev-board path (previously
  scoped in the removed `§2.1` module survey) to unlock the thin/wide form factor
  below — see [`docs/hardware.md §2.1`](../../docs/hardware.md#21-parts-direction-bare-module--chip-down).
- **~8″ wide red LED bar** (620–630 nm mid-power) driven from a boost constant-current
  driver. Series-string + boost topology set in
  [`docs/design/de-04`](../../docs/design/de-04-led-render.md), which down-selected the
  TI **LM3410**; the **first pass fitted two `AP3019AKTR` drivers** on the §3.1
  two-parallel-strings branch — see
  [the as-built summary below](#as-built-block-summary-first-pass-schematic) and
  [DE-04 §3.5](../../docs/design/de-04-led-render.md).
- **Binary on/off bar — brightness deferred.** The firmware drives each driver's
  `CTRL`/`EN` pin as a **static enable**, and the LED current is fixed by the sense
  resistors. No PWM dimming, no ambient auto-dimming for now; the decision is recorded
  in [`docs/design/de-04`](../../docs/design/de-04-led-render.md), the deferred element
  is [`de-02`](../../docs/design/de-02-auto-brightness.md). The ambient-light sensor is
  **fitted but unused**, so this is a firmware-only change later. Auto-dimming remains a
  safety requirement (no blinding at night) — deferred, not dropped.
- **Status-indicator LEDs** — **separate from the bar** for status/fault reporting
  ([`docs/design/de-10`](../../docs/design/de-10-status-indicator.md)). DE-10 specified a
  chip-down WS2812B; the **first pass fitted two discrete mono LEDs (D13 RED / D14 GRN)**
  instead, which loses the full RGB code space — see
  [DE-10 §3](../../docs/design/de-10-status-indicator.md).
- **Battery: 1S 18650 Li-ion, protected cell** + USB-C charging. 18650 chosen over
  pouch/LiPo because pouch cells need mechanical compression the thin enclosure can't
  provide; a protected cell means the charge IC doesn't have to double as cell
  protection. The charger baseline was an MCP73871 (load-share); the **first pass fitted
  a `BQ21040DBV`**, so the load-sharing behaviour needs confirming.
- **Form factor: thin, short-in-vertical (viewed from the rear), wide.** The ~8″ LED
  bar sets the width; the 18650's ~18 mm diameter sets the enclosure-thickness floor.
- **Mount: magnetic to jacket / backpack fabric.** Magnets live in the light
  assembly; a thin steel strip or washers sit inside the garment or pack so the
  fabric is sandwiched between them. This is the [garment / backpack shoulder
  mount](../../docs/design/explorations/mounting-magnetic.md#exploration-a--garment--backpack-shoulder-mount)
  exploration promoted to the current baseline; helmet fitment and cross-form
  interchangeability are **deferred**.
- Sized for a full-day runtime target — with a ~3000 mAh 18650 the runtime budget
  roughly doubles the earlier 1500 mAh LiPo worked example (see hardware doc).
- Sealed, IP65+ enclosure.

⚠️ Mounting + Li-ion safety are mandatory reading: [`docs/safety-regulatory.md`](../../docs/safety-regulatory.md).

---

## Status of the design files

| File | State |
|------|-------|
| [`brake_light.kicad_sch`](brake_light.kicad_sch) | **First pass, committed.** Power, charge, buck, USB-C + ESD, MCU, ambient-light sensor and both LED strings are drawn and wired. Four MCU-side nets are **not yet wired** — see [Not yet assigned](#not-yet-assigned-provisional-in-firmware). |
| [`brake_light.kicad_pcb`](brake_light.kicad_pcb) | **First pass, committed.** Carries the netlist generated from the schematic (used to verify the table below). Not yet fabricated or brought up. |
| BOM | To be exported once the unwired nets are resolved. |
| Enclosure / mount files | Still to be added (Phase 4). |

> Nothing here has been built or measured yet. Treat every value below as
> "as-drawn", not "as-measured".

## As-built block summary (first-pass schematic)

| Block | Part | Notes |
|-------|------|-------|
| MCU | **U1 `ESP32-C3-WROOM-02`** | Bare module; native USB on IO18/IO19 |
| Brake bar, string A | **U3 `AP3019AKTR`** boost CC driver | L1 22 µH, C4 0.22 µF out, **R4 560 mΩ** sense; drives **D3…D12 (10 emitters)** in series |
| Brake bar, string B | **U6 `AP3019AKTR`** boost CC driver | L2 22 µH, C7 0.22 µF out, **R10 560 mΩ** sense; drives **D15…D24 (10 emitters)** in series |
| Charger | **U2 `BQ21040DBV`** | From USB VBUS; `TS` ← NTC **TH1**, `ISET` ← R9, `~CHG` → status LED **D2** |
| 3V3 rail | **U7 `TPS62162DSGR`** buck | VBAT → 3V3; `EN` tied to VBAT (always on); `PG` → R14 1 kΩ → the C3's `EN`, so the MCU is held in reset until 3V3 is good |
| Ambient light | **U5 `ISL29035`** | I²C + interrupt. **Populated but unused in firmware** — see [DE-02](../../docs/design/de-02-auto-brightness.md) |
| USB-C | **J2** + **U4 `USBLC6-2P6`** | CC1/CC2 5.1 kΩ (R1/R2), D+/D− ESD-clamped |
| Status LEDs | **D13 (RED)**, **D14 (GRN)** | Each **cathode → GND, anode → 100 Ω (R21 / R20) → MCU pin**, i.e. **active high** |
| Buttons | **SW1 BOOT/USER**, **SW2 RST** | Both switch to GND (active low) |
| Cell | **BT1** 1S Li-ion (18650) | |

**Fixed LED current setpoint.** The sense resistors set the string current in
hardware — there is no firmware dimming (see
[DE-04](../../docs/design/de-04-led-render.md)). The schematic's own sizing note reads
`R = 0.2 V / I_LED`, so **R = 560 mΩ → I_string ≈ 357 mA**, dissipating ≈ 71 mW in the
sense resistor. Ten 620–630 nm emitters at V<sub>f</sub> ≈ 2.1 V put each string at
≈ 21 V, inside the driver's noted 30 V ceiling. The intensity this *should* be is set by
the [LED brightness benchmark](../../docs/led-brightness-benchmark.md) — which is still
the governing sizing study, and which this first pass has **not** been reconciled against
(see the open items below).

---

## Pin map

Derived from the committed design files: the ESP32-C3 pin numbers and the pin↔net
assignments below were read out of **`brake_light.kicad_sch`** and cross-checked against
the net assigned to each pad in **`brake_light.kicad_pcb`** (which KiCad generates from
the schematic). Where the two are quoted together they agree.

### Verified from the netlist

| Function | ESP32-C3 signal | Module pad | How it is wired |
|----------|-----------------|:----------:|-----------------|
| ISL29035 **SDA** | `IO0` | 18 | → U5.6 `SDA`; 4.7 kΩ pull-up (R6) to +3V3 |
| ISL29035 **SCL** | `IO1` | 17 | → U5.5 `SCL`; 4.7 kΩ pull-up (R7) to +3V3 |
| ISL29035 **INT** | `IO3` | 15 | → U5.4 `~INT`; 4.7 kΩ pull-up (R8) to +3V3 |
| **SW1** BOOT/USER button | `IO9` | 8 | → SW1.1; SW1.2 → GND ⇒ **active low**, needs the internal pull-up |
| USB **D−** | `IO18` | 13 | → U4 `I/O1` → J2 `A7`/`B7` |
| USB **D+** | `IO19` | 14 | → U4 `I/O2` → J2 `A6`/`B6` |
| UART **TXD** test point | `IO21` | 12 | → TP1 |
| UART **RXD** test point | `IO20` | 11 | → TP2 |
| Chip **reset** | `EN` | 2 | SW2 (RST) to GND + R5 10 kΩ pull-up + R14 from U7 `PG` |
| Supply | `3V3` / `GND` | 1 / 9, 19 | U7 buck output |

So the ambient-light sensor, the user button, USB and the console test points **are**
fully determined by the committed design files.

### Not yet assigned (provisional in firmware)

These four MCU-side nets are **not wired in the committed first pass** — the board
netlist reports each pad as its own single-pad `unconnected-(…)` net:

| Function | Pad that is open | Netlist name |
|----------|------------------|--------------|
| Brake bar **string A** driver enable | U3 pin 4 `CTRL` | `unconnected-(U3-CTRL-Pad4)` |
| Brake bar **string B** driver enable | U6 pin 4 `CTRL` | `unconnected-(U6-CTRL-Pad4)` |
| Status LED **RED** feed | R21 pin 1 (→ D13 anode) | `unconnected-(R21-Pad1)` |
| Status LED **GRN** feed | R20 pin 1 (→ D14 anode) | `unconnected-(R20-Pad1)` |

Free module GPIOs available for them: `IO2`, `IO4`, `IO5`, `IO6`, `IO7`, `IO8`, `IO10`.

The firmware therefore keeps all four as Kconfig symbols with **provisional defaults**,
so nothing is hard-coded ahead of a schematic review
([`brake_light/software/README.md`](../software/README.md)):

| Function | Provisional default (esp32c3) | Kconfig symbol |
|----------|-------------------------------|----------------|
| String A enable (U3 `CTRL`) | `IO5` | `CHMBL_BAR_EN_A_GPIO` |
| String B enable (U6 `CTRL`) | `IO6` | `CHMBL_BAR_EN_B_GPIO` |
| Status LED RED (D13 via R21) | `IO7` | `CHMBL_STATUS_LED_RED_GPIO` |
| Status LED GRN (D14 via R20) | `IO10` | `CHMBL_STATUS_LED_GRN_GPIO` |

Why those four, if the schematic review wants a starting point:

- **Avoid `IO2` and `IO8`** — both are ESP32-C3 boot **strapping** pins. A driver-enable
  or LED pin sitting low through reset would change the boot mode.
- **`IO9` is taken** by SW1 (and is itself the download-boot strap).
- **Keep `IO4` free.** With `IO0`/`IO1`/`IO3` on the I²C bus and `IO2` strapping, `IO4` is
  the last ADC1-capable pin left, and a battery-sense divider (DE-05) is the obvious next
  thing that needs one — note there is **no battery-voltage sense net on the board yet
  either**.
- `IO4`–`IO7` double as the C3's JTAG pins, which this board does not need: debug goes
  over the native USB Serial/JTAG on `IO18`/`IO19`.

**Active levels.** The status-LED orientation **is** verified (cathode to GND, anode fed
through the 100 Ω resistor), so those pins are **active high**. The AP3019A `CTRL` pin is
also active high, and the firmware drives it as a **static enable** — high = that string
lit at the sense-resistor setpoint, low = dark. That is a deliberate deferral of PWM
dimming, recorded in [DE-04](../../docs/design/de-04-led-render.md).

---

## Open items from this first pass

1. **Wire the four nets above** (both driver `CTRL` pins and both status-LED feeds), then
   fold the agreed GPIO numbers back into this table and the firmware's Kconfig defaults.
2. **Reconcile the LED operating point with the sizing study.** As drawn the bar is
   **2 strings × 10 emitters at ≈ 357 mA each** (≈ 714 mA of LED current). DE-04 §3.4's
   worked baseline is **one string of 8 at ≈ 80 mA**, sized to the
   [benchmark](../../docs/led-brightness-benchmark.md)'s ≈ 50–80 cd daylight target. The
   first-pass board is far above that, which matters more than usual now that brightness
   is fixed in hardware: with dimming deferred there is no firmware knob to pull it back.
   The sense resistors are the only adjustment.
3. **Driver part differs from the DE-04 down-select.** The board uses
   **`AP3019AKTR` ×2**; DE-04 §3.3's trade study concluded **TI LM3410**. The AP3019A's
   higher output ceiling (sheet note "MAX 30V") is what makes the 10-emitter strings
   possible, so this looks like a reasonable substitution — but it has not been run
   through the §3.3 criteria (V<sub>in</sub>-min against the 1S cutoff, switch current,
   EMI next to the 2.4 GHz radio). Confirm against the datasheet and record the outcome in
   DE-04.
4. **No battery-voltage sense and no MCU connection to the charger's `~CHG`** — battery
   state (DE-05) and the charge/low-battery status codes (DE-10) are not observable in
   firmware on this revision.
5. **ISL29035 is populated but unused.** Deliberate: it costs nothing to leave fitted and
   means no hardware change when [DE-02](../../docs/design/de-02-auto-brightness.md) is
   picked back up.
