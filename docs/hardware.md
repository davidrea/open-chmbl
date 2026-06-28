# Hardware

Two boards: the **transmitter** (bike-side) and the **brake_light** (helmet-side).
Everything here is a first-pass sketch to size the design — not a finalized BOM.

---

## 1. Transmitter (bike-side)

Lives at the diagnostic port. Powered from the bike. Read-only on CAN.

### Block diagram

```
 Diagnostic  ┌──────────────┐   3.3 V   ┌─────────────┐
 connector   │ Reverse-pol. │──────────▶│  Buck reg.   │──▶ 3V3 rail
   12 V ─────▶│  + TVS prot. │           │ (12V→3.3V)   │
   GND  ─────▶└──────────────┘           └─────────────┘
   CAN-H ─────────────────────┐
   CAN-L ─────────────────────│──▶ CAN transceiver ──▶ ESP32 TWAI (listen-only)
                              (SN65HVD230, 3.3 V)        │
                                                          └─▶ ESP-NOW (2.4 GHz)
```

### Parts (sketch)

| Function | Candidate part | Notes |
|----------|---------------|-------|
| MCU + radio | **ESP32-C3** (or ESP32-S3/WROOM) | Has built-in **TWAI** (CAN 2.0) controller; only a transceiver is needed. C3 is cheap, RISC-V, Wi-Fi/BLE. |
| CAN transceiver | **SN65HVD230** (3.3 V) | 3.3 V logic, low power, has a silent-mode pin. Avoid 5 V-only transceivers (TJA1050) unless level-shifting. |
| Power regulation | Automotive-grade **buck** (e.g. TPS54xx), 12 V→3.3 V | Must survive load dumps / 12–14.4 V charging system. Wide-Vin. |
| Input protection | Reverse-polarity FET + **TVS diode** + fuse | Automotive transients are brutal. |
| Ignition sense | Tap switched-12 V or detect bus activity | Used to sleep when bike is off (see parasitic draw). |
| Connector | Bike-specific diagnostic plug → **wire-to-board (Molex SL, 4-pin)** | Euro 5 6-pin is common; **verify pinout per bike** — pin assignment is not universal. First prototypes reuse an off-the-shelf **Euro 5 → OBD2 adapter cable** with the 16-pin OBD2 end cut off and a 4-pin Molex SL header fitted (only 12 V / GND / CAN-H / CAN-L are populated on the reference bike). |
| Enclosure | **Single PCB in heat-shrink**, taped under the seat | Under-seat location is benign but vibratory; dual-wall adhesive-lined heat-shrink + VHB tape is lighter, cheaper, and more serviceable than a potted box. See [`transmitter/hardware`](../transmitter/hardware/README.md#packaging--mounting-plan-of-record). Revisit potting only if a bike routes the unit somewhere weather-exposed. |

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
| LED array | High-power **red** LEDs + constant-current driver, **or** WS2812-style addressable | Addressable = easy patterns but more current/complexity. Plain red + CC driver = efficient, simplest legal-color story. |
| Brightness control | PWM dimming + **ambient light sensor** | Night brightness must not blind following drivers; day brightness must be visible in sun. Auto-dim is a real safety feature, not a nicety. |
| Battery monitor | Fuel gauge IC or ADC divider | Low-battery warning pattern + cutoff. |
| User I/O | One button | Power, pairing, brightness cycle. |
| Status indicator | **Addressable RGB LED** (WS2812-class), **separate from the main bar** | Discrete status/fault by **color + blink code** (pairing, link, charge, fault) — legible even when the bar is off. Can reuse a **module's onboard WS2812** (see §2.1). See [`de-10`](design/de-10-status-indicator.md). |
| Mount (baseline) | Non-penetrating, **breakaway** | Adhesive pad or strap; see safety doc. **Never drill the helmet.** Magnetic shear-release variants are a [future-state exploration](design/explorations/mounting-magnetic.md). |
| Enclosure | **IP65+**, low-profile, lightweight | Mass on a helmet contributes to neck load in a crash — minimize it. Being modeled as a two-piece clamshell with a concave helmet-cup inner shell (helmet nests in) and a convex lens cap, flat parting, capturing the PCB and a cast-silicone lens, with recessed outboard magnets — see [`brake_light/hardware/enclosure`](../brake_light/hardware/enclosure/README.md). |

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
- LED bar: addressable vs. discrete high-power red — affects driver, current, and
  the legal-color/no-flash story.
- ESP32-C3 **module choice** (§2.1): an integrated "WS2812 + LiPo charger" board vs. a
  bare module + discrete charger/indicator. Nice-to-have, not a gate.
- **Mount direction:** keep the adhesive/strap breakaway baseline, or pursue a
  [magnetic shear-release mount](design/explorations/mounting-magnetic.md)
  (helmet-interchangeable VHB steel targets, and/or a garment/backpack shoulder mount)?
  Future-state exploration.
- Whether to add a small inertial sensor purely as a *cross-check / tamper or
  fallen-helmet detector* (NOT for brake detection — that's the patented approach we
  avoid). Deferred.
