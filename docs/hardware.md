# Hardware

Two boards: the **transmitter** (bike-side) and the **brake_light** (helmet-side).
Everything here is a first-pass sketch to size the design — not a finalized BOM.

---

## 1. Transmitter (bike-side)

Lives at the diagnostic port. Powered from the bike. Read-only on CAN.

**Hardware plan: reuse the [`logger/`](../logger) PCB.** The logger's board
(ESP32-S3-WROOM-1 + onboard CAN transceiver + protected 12 V/USB-C power, see
[`logger/hardware/README.md`](../logger/hardware/README.md)) was designed to double
as the transmitter with its microSD slot (J5) and button/LED breakout (J4) left
unpopulated — the transmitter needs neither. This supersedes the earlier
ESP32-C3 + SN65HVD230 sketch below the block diagram; the C3 remains the MCU for
`brake_light/` (§2), which has no SD card and so doesn't need the S3's SDMMC
peripheral.

> ⚠️ The logger board has an open strapping-pin issue (a pull-up on the ESP32-S3's
> `GPIO45` VDD_SPI boot strap) — see
> [`logger/hardware/README.md §5`](../logger/hardware/README.md#5-known-issue--gpio45s-pin-conflicts-with-a-boot-strap)
> before building a transmitter board off this design.

### Block diagram

```
 J3 (bike)                     USB-C (J2)
 12 V ──▶┌───────────┐  +5VD  VBUS──▶┌──────────────┐
 GND ───▶│ Schottky   │──────────────▶│  TPS62172     │──▶ 3V3 rail
         │ reverse-pol│                │  (buck)       │       │
 CAN-H ─▶│ + TVS      │                └──────────────┘       │
 CAN-L ─▶└───────────┘                                          │
                                                                  ▼
                              CAN transceiver ──────────▶ ESP32-S3 TWAI
                              (TCAN330, 3.3 V,             (listen-only)
                               silent-mode pin)                  │
                                                                  └─▶ ESP-NOW (2.4 GHz)
```

### Parts (as reused from the logger board)

| Function | Part | Notes |
|----------|---------------|-------|
| MCU + radio | **ESP32-S3-WROOM-1-N8** | Has built-in **TWAI** (CAN 2.0) controller; only a transceiver is needed. Chosen over the C3 on the logger board for its SDMMC peripheral — not needed here, but the transmitter reuses that same board. |
| CAN transceiver | **TCAN330** (3.3 V) | 3.3 V logic, silent-mode pin wired to a GPIO for a hardware-level listen-only default (see the GPIO45 caveat above). |
| Power regulation | **TPS62172** buck, 12 V→3.3 V (via a diode-ORed +5VD rail shared with USB-C) | Not automotive-load-dump-rated in this rev — a Schottky diode handles reverse-polarity, not a FET. Revisit for full-time bike use; fine for bench/prototype. |
| Input protection | Schottky diode (reverse-pol.) + dual TVS on CAN-H/CAN-L | See [`logger/hardware/README.md §2`](../logger/hardware/README.md#2-power). |
| Ignition sense | Tap switched-12 V or detect bus activity | Not yet on the logger board — needed for TX-specific sleep behavior (DE-06); would be a board delta from the shared logger design. |
| Connector | JST-PH 5-pin (J3): CAN-L, CAN-H, +12 V, GND, spare | Board-side connector. Bike-specific diagnostic plug (Euro 5 6-pin is common) still needs its own adapter/harness — **verify pinout per bike**, pin assignment is not universal. |
| Enclosure | Potted/sealed, **IP65+** | Engine bay vibration + weather + heat. Not designed yet. |

### Power & parasitic draw

- The diagnostic port may provide **constant** or **switched** 12 V depending on the
  bike. If constant, the TX **must** sleep (deep sleep, transceiver disabled) when it
  sees no CAN activity, and ideally cut the buck's load, so it cannot drain the
  motorcycle battery while parked. Target parked draw: **< 1 mA**.
- Wake on CAN bus activity (transceiver wake / RXD edge) or on ignition sense.

---

## 2. Brake_light (helmet-side)

Self-contained, battery powered, charges over USB-C. This is the part the rider
sees and touches, so usability (charging, on/off, brightness) matters.

### Block diagram

```
 USB-C ──▶ Charge IC ──▶ LiPo ──▶ Load-share ──▶ Boost/LDO ──▶ 3V3 ──▶ ESP32-C3
 (5 V)    (w/ load     (1S)       path                        │
          sharing)                                            ├─▶ LED driver ──▶ Red LED bar
 Ambient-light sensor ─────────────────────────────────────▶ │   (constant-current
 Fuel-gauge / ADC ─────────────────────────────────────────▶ │    or addressable)
                                                              ├─▶ Mode/pair button
                                                              └─▶ Status-indicator LED
                                                                  (RGB WS2812, separate
                                                                   from the bar)
```

### Parts (sketch)

| Function | Candidate part | Notes |
|----------|---------------|-------|
| MCU + radio | **ESP32-C3** | Same family as TX simplifies firmware/build. |
| Battery | **1S LiPo**, ~1000–2000 mAh | Size for runtime target (below). Protected cell. |
| Charge IC | **MCP73871** (or TP4056 + load-share) | MCP73871 does **load sharing** so the light works while charging. TP4056 is cheaper but no load share. |
| USB-C | USB-C receptacle w/ CC resistors | 5 V sink only; add ESD protection. |
| LED array | **Discrete 620–630 nm red** (mid-power 2835/3030, ~8–12 emitters) in series string(s) + **boost constant-current driver** | Emitter/flux **down-selected** in [`led-brightness-benchmark.md`](led-brightness-benchmark.md) (~50–80 cd daylight, ~60–100 lm installed red); series-string + boost **topology** worked out in [`de-04`](design/de-04-led-render.md). Series count per string is capped by the driver's V<sub>out</sub> ceiling (≤ ~8 mid-power reds ≈ 18 V for LM3410's 24 V); split into parallel strings beyond that. Addressable RGB is too dim per-pixel for the bar → status indicator only. |
| LED driver IC | **TI LM3410(X)** boost CC | 2.7–5.5 V in (uses the full 1S discharge), 2.8 A/**24 V** integrated switch, PWM + analog dim, OVP/thermal; ~$1.5, LCSC. The 24 V ceiling caps a single string at ~8 mid-power reds — tall single strings (10–12) or higher V<sub>out</sub> want the upgrade parts: ADI **LT3922‑1** (synchronous 34 V, spread-spectrum, AEC‑Q100) or LCSC-native **DIO5661** (37 V). **MAX16833 rejected** (5 V Vin-min — can't run on 1S). [`de-04 §3.3`](design/de-04-led-render.md). |
| Brightness control | PWM dimming (driver DIM pin) + analog current trim + **ambient light sensor** | Night brightness must not blind following drivers; day brightness must be visible in sun. Auto-dim is a real safety feature, not a nicety. Day/night intensity endpoints (~50–80 cd → ~5–15 cd) set in [`led-brightness-benchmark.md §4`](led-brightness-benchmark.md#4-design-target-for-the-helmet-bar). Keep dimming PWM > flicker fusion — distinct from the illegal *flashing*. |
| Battery monitor | Fuel gauge IC or ADC divider | Low-battery warning pattern + cutoff. |
| User I/O | One button | Power, pairing, brightness cycle. |
| Status indicator | **Addressable RGB LED** (WS2812-class), **separate from the main bar** | Discrete status/fault by **color + blink code** (pairing, link, charge, fault) — legible even when the bar is off. Can reuse a **module's onboard WS2812** (see §2.1). See [`de-10`](design/de-10-status-indicator.md). |
| Mount (baseline) | Non-penetrating, **breakaway** | Adhesive pad or strap; see safety doc. **Never drill the helmet.** Magnetic shear-release variants are a [future-state exploration](design/explorations/mounting-magnetic.md). |
| Enclosure | **IP65+**, low-profile, lightweight | Mass on a helmet contributes to neck load in a crash — minimize it. |

### Runtime budgeting (worked example)

LED current dominates. Rough sizing:

```
Avg LED draw (mixed off/decel/brake, auto-dimmed)  ≈  120 mA
ESP32-C3 (RX mostly, modem on)                     ≈   30 mA
                                                   ----------
System average                                     ≈  150 mA

1500 mAh battery × ~0.85 usable / 150 mA           ≈  8.5 h
```

That's a comfortable full-day-of-riding target. Brake events are brief and bright;
the *average* is what sets runtime, so auto-dimming and an `OFF`/dim-running idle
state matter a lot. Tune the battery size to the LED bar you choose.

### 2.1 Integrated module candidates (WS2812 + LiPo charger)

To **cut the parts we have to design in**, we want a dev-board-style ESP32-C3 module
that already carries **(a) a LiPo charger** (replaces the discrete charge IC / load-share
design) and **(b) an onboard WS2812 addressable LED** (serves the
[status indicator](design/de-10-status-indicator.md) for free). The main red brake bar
stays external regardless — the onboard WS2812 is a *status* LED, not the bar (the
legal-color/no-flash story keeps the bar as discrete red or its own addressable strip).

**Reality check:** very few compact C3 boards carry **both** at once — most have one or
the other. Survey of candidates (verify against the current datasheet before committing
— vendors revise silently):

| Board | LiPo charger | Onboard WS2812 | Notes |
|-------|:------------:|:--------------:|-------|
| **LOLIN / Wemos C3 Pico** | ✅ | ✅ | Strongest "both onboard" candidate — D1-mini-class form, RGB + LiPo charging per vendor docs. **Confirm charger chip + RGB GPIO on the schematic.** |
| **DFRobot FireBeetle 2 ESP32-C3** | ✅ | ❔ | FireBeetle 2 line pairs onboard charging with a WS2812 on some variants (confirmed on the ESP32-E); **confirm the C3 SKU specifically.** |
| **DFRobot Beetle ESP32-C3** (DFR0868) | ✅ (TP4057) | ❌ | Coin-size, charger onboard, but **no** addressable RGB — would still need an external WS2812. |
| **Seeed XIAO ESP32-C3** | ✅ (ETA4054, ~370 mA, JST-PH) | ❌ | Tiny, ubiquitous, great charger story; **only a red charge LED** — add an external WS2812 for the indicator. |
| **Olimex ESP32-C3-DevKit-Lipo** | ✅ | ❌ (plain status LEDs) | **Open-source hardware** (a plus for this project); stocked at **LCSC** (`C17694452`, ~$6–7). |
| **Waveshare ESP32-C3-Zero** | ❌ | ✅ (GPIO10) | RGB onboard, **no** charger — pair with an external charge IC. |
| **ESP32-C3 SuperMini *Plus*** | ❌ | ✅ (GPIO8, shared w/ blue LED) | Cheap; RGB but no charger. (Plain "SuperMini" has neither RGB nor charger.) |
| **Espressif ESP32-C3-DevKitC/M** | ❌ | ✅ | Reference devkit; WS2812 onboard, no charger. |

**LCSC availability (soft preference):** the most LCSC-clean path is actually a **bare
module** — `ESP32-C3-MINI-1` / `ESP32-C3-WROOM-02` — plus discrete **WS2812B**
(`C2761795` / `C114586`, or small `WS2812B-2020` / `WS2812B-Mini` for a low-profile
indicator) and a charge IC (TP4056 / MCP73871), all stocked at LCSC. A finished
dev-board with everything onboard trades an LCSC-friendly BOM for less hand-design; the
Olimex C3-DevKit-Lipo is the one surveyed board that is **both** integrated **and** on
LCSC, though it lacks the onboard WS2812.

**Lean:** treat "onboard WS2812 + onboard charger" as a *nice-to-have* that picks the
module when a clean one exists (LOLIN C3 Pico / a confirmed FireBeetle 2 C3), but don't
let it gate the design — the fallback (XIAO C3 for charging + one external WS2812, or a
bare module + both discretes) is cheap and fully LCSC-sourceable. Decision tracked in
[`roadmap.md`](roadmap.md).

---

## 3. Open hardware questions

- Exact diagnostic connector + pinout for the **reference bike** (see
  [`can-profiles.md`](can-profiles.md)).
- ~~LED bar: addressable vs. discrete high-power red~~ — **resolved**: **discrete
  620–630 nm red mid-power array (~8–12 emitters) on a boost constant-current driver**,
  sized to a car-CHMSL intensity band (~50–80 cd daylight) in
  [`led-brightness-benchmark.md`](led-brightness-benchmark.md), with the series/boost
  topology + driver (**TI LM3410**) selected in [`de-04`](design/de-04-led-render.md).
  Addressable RGB is too weak per-pixel for the bar and stays the status indicator only.
  Remaining sub-question: final emitter part/bin, optic/diffuser, and exact emitter count
  (which also sets series-vs-parallel string layout against the driver's V<sub>out</sub>).
- ESP32-C3 **module choice** (§2.1): an integrated "WS2812 + LiPo charger" board vs. a
  bare module + discrete charger/indicator. Nice-to-have, not a gate.
- **Mount direction:** keep the adhesive/strap breakaway baseline, or pursue a
  [magnetic shear-release mount](design/explorations/mounting-magnetic.md)
  (helmet-interchangeable VHB steel targets, and/or a garment/backpack shoulder mount)?
  Future-state exploration.
- Whether to add a small inertial sensor purely as a *cross-check / tamper or
  fallen-helmet detector* (NOT for brake detection — that's the patented approach we
  avoid). Deferred.
