# Hardware

Two boards: the **transmitter** (bike-side) and the **brake_light** (rider-side —
fabric-mounted, this rev). Everything here is a first-pass sketch to size the design
— not a finalized BOM.

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

## 2. Brake_light (rider-side)

Self-contained, battery powered, charges over USB-C. This is the part the rider
sees and touches, so usability (charging, on/off, brightness) matters.

**Form factor** (this rev): a **thin, short (vertical, viewed from the rear), wide
~8″ LED bar** that clamps magnetically to the outside of a **jacket or backpack**,
between the shoulder blades — a thin steel strip or washers inside the garment /
pack completes the mount. Helmet fitment and cross-form interchangeability are
**deferred** so shell curvature and helmet-certification questions don't gate the
first build. See the [garment / backpack mount exploration](design/explorations/mounting-magnetic.md#exploration-a--garment--backpack-shoulder-mount).

### Block diagram

```
 USB-C ──▶ Charge IC ──▶ 1S 18650 ──▶ Load-share ──▶ Boost/LDO ──▶ 3V3 ──▶ ESP32-C3
 (5 V)    (MCP73871,     (protected     path                       │       (bare
          chip-down)      cell)                                    │        WROOM-02
                                                                    │        module)
 Ambient-light sensor ─────────────────────────────────────────▶ │
 Fuel-gauge / ADC ─────────────────────────────────────────────▶ │
                                                                    ├─▶ LED driver ──▶ ~8″ red LED bar
                                                                    │   (LM3410 boost CC,
                                                                    │    chip-down)
                                                                    ├─▶ Mode/pair button
                                                                    └─▶ Status WS2812
                                                                        (2020/Mini,
                                                                         chip-down,
                                                                         separate from bar)
```

Everything except the ESP32-C3 module itself is **chip-down** on the brake_light PCB
— the earlier "grab a dev board with the charger already on it" path (removed §2.1
survey) is dropped in favor of an integrated design that fits the thin/wide form
factor.

### Parts (sketch)

| Function | Candidate part | Notes |
|----------|---------------|-------|
| MCU + radio | **ESP32-C3-WROOM-02** (bare module) | Same C3 family as before; module rather than dev-board so we can lay the PCB flat and thin. Placed as-is, no dev-board freebies. |
| Battery | **1S 18650 Li-ion, protected cell**, ~3000 mAh typical | Cylindrical Li-ion chosen over pouch/LiPo because pouch cells need compression the thin enclosure can't provide. Protected cell means the load-share IC does **not** need to double as cell protection. **18 mm diameter sets the enclosure-thickness floor.** |
| Cell attach | **Undecided — open question** | Three viable options: **Keystone 1043** (low-profile top-mount holder — user-swappable, adds ~1–2 mm over cell diameter), **Keystone 54** (end clips only — thinnest; cell sits on the PCB but enclosure has to open for swap), or **cell with a factory JST-PH pigtail** (no board footprint; swap requires re-plugging inside the sealed enclosure). Decide during enclosure design; all three preserve the same charger / load-share design. |
| Charge IC | **MCP73871** (chip-down) | Does **load sharing** so the light runs while charging from USB-C. Directly-placed part, not a module. |
| USB-C | USB-C receptacle w/ CC resistors + ESD protection | 5 V sink only. |
| LED array | **Discrete 620–630 nm red** (mid-power 2835/3030, ~8–12 emitters) laid out linearly along the **~8″ bar** in series string(s) + **boost constant-current driver** | Emitter/flux **down-selected** in [`led-brightness-benchmark.md`](led-brightness-benchmark.md) (~50–80 cd daylight, ~60–100 lm installed red); series-string + boost **topology** in [`de-04`](design/de-04-led-render.md). Series count per string capped by the driver's V<sub>out</sub> ceiling (≤ ~8 mid-power reds ≈ 18 V for LM3410's 24 V); split into parallel strings beyond that. Addressable RGB is too dim per-pixel for the bar → status indicator only. |
| LED driver IC | **TI LM3410(X)** boost CC (chip-down) | 2.7–5.5 V in (uses the full 1S Li-ion discharge, 3.0–4.2 V), 2.8 A / **24 V** integrated switch, PWM + analog dim, OVP/thermal; ~$1.5, LCSC. 24 V ceiling caps a single string at ~8 mid-power reds — tall single strings (10–12) or higher V<sub>out</sub> want the upgrade parts: ADI **LT3922‑1** (synchronous 34 V, spread-spectrum, AEC‑Q100) or LCSC-native **DIO5661** (37 V). **MAX16833 rejected** (5 V Vin-min — can't run on 1S). [`de-04 §3.3`](design/de-04-led-render.md). |
| Brightness control | PWM dimming (driver DIM pin) + analog current trim + **ambient light sensor** | Night brightness must not blind following drivers; day brightness must be visible in sun. Auto-dim is a real safety feature, not a nicety. Day/night intensity endpoints (~50–80 cd → ~5–15 cd) set in [`led-brightness-benchmark.md §4`](led-brightness-benchmark.md#4-design-target-for-the-helmet-bar). Keep dimming PWM > flicker fusion — distinct from the illegal *flashing*. |
| Battery monitor | Fuel gauge IC or ADC divider | Low-battery warning pattern + cutoff. |
| User I/O | One button | Power, pairing, brightness cycle. |
| Status indicator | **Discrete WS2812B** (`WS2812B-2020` or `WS2812B-Mini`), chip-down, **separate from the main bar** | Chip-down since we're on a bare module now (no free onboard RGB). Discrete status/fault by **color + blink code** (pairing, link, charge, fault) — legible even when the bar is off. See [`de-10`](design/de-10-status-indicator.md). |
| Mount (baseline) | **Magnetic to jacket / backpack fabric** — magnets in the light assembly, thin steel strip / washers inside the garment or pack | The magnetic interface is its own **shear / peel release**, satisfying the safety doc's breakaway requirement without a separate frangible pin. Sizing/pole geometry TBD. **No helmet target this rev**; helmet fitment stays in the [magnetic mount exploration](design/explorations/mounting-magnetic.md). |
| Enclosure | **IP65+**, thin (governed by the 18650), short vertically, ~8″ wide | Low profile matters for on-body wear comfort and for staying below the rider's line of sight to the mirrors. |

### Runtime budgeting (worked example)

LED current dominates. Rough sizing with a typical protected 18650 (~3000 mAh
usable capacity, take ~85 % to end-of-life):

```
Avg LED draw (mixed off/decel/brake, auto-dimmed)  ≈  120 mA
ESP32-C3 (RX mostly, modem on)                     ≈   30 mA
                                                   ----------
System average                                     ≈  150 mA

3000 mAh × ~0.85 usable / 150 mA                   ≈  17 h
```

That comfortably covers multi-day riding between charges (versus ~8.5 h with the
earlier 1500 mAh LiPo). Brake events are brief and bright; the *average* is what
sets runtime, so auto-dimming and an `OFF`/dim-running idle state matter a lot.
The extra headroom lets us hold the LED currents at the daylight-visibility target
without shrinking the runtime story.

### 2.1 Parts direction: bare module + chip-down

**Decision:** the brake_light uses a **bare `ESP32-C3-WROOM-02` module** and places
the charger, DC-DC, LED driver, status WS2812, and USB-C front-end **chip-down** on
the brake_light PCB. The earlier dev-board-with-freebies survey (LOLIN C3 Pico,
FireBeetle 2, XIAO C3, Olimex Lipo, etc.) is superseded and removed.

Why the pivot:

- **Form factor.** The rider-side unit is now a **thin, wide (~8″), short-in-vertical
  LED bar** that clamps magnetically to a jacket or backpack. That geometry wants a
  flat, custom-shaped PCB with the LED bar and 18650 laid out on it — a piggybacked
  dev board (with its own USB connector, headers, and vertical parts) doesn't fit.
- **Chip-down parts are already in the family.** The load-share charger
  (`MCP73871`), boost CC driver (`LM3410`), 3V3 buck, and WS2812B-2020 are all
  LCSC-stocked and small enough to place in the bar's own outline. Skipping the
  dev-board wrapper saves height and mm of board.
- **Status indicator is no longer "free."** The de-10 status LED becomes a
  **discrete WS2812B (2020 / Mini)** chip-down part — cheap, one GPIO, positioned
  wherever the enclosure can actually show it (dev-board WS2812s were often buried).
  DE-10 already flagged this trade — see its open items.

The bare-module + chip-down BOM is fully LCSC-sourceable (`ESP32-C3-WROOM-02`,
`MCP73871`, `LM3410`, `WS2812B-2020` / `-Mini`), which is the same "clean supply
chain" argument the earlier survey landed on for a bare module anyway.

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
- ~~ESP32-C3 module choice~~ — **resolved**: bare `ESP32-C3-WROOM-02` + chip-down
  charger / DC-DC / LED driver / status WS2812 (§2.1). The integrated-module survey
  is no longer relevant to this form factor.
- **18650 cell attachment**: pick between **Keystone 1043** (low-profile top-mount
  holder), **Keystone 54** (end clips only — thinnest), or a **cell with a JST-PH
  pigtail** (no board footprint). All three preserve the same charger / load-share
  design; the decision belongs with the enclosure pass because it drives the
  serviceability story (open-the-case-to-swap vs. slide-in). Assume a protected cell
  regardless.
- **Mount direction (fabric case):** magnet size / pole geometry / retention force
  vs. clean-release for the jacket/backpack magnetic mount — see the
  [garment / backpack exploration](design/explorations/mounting-magnetic.md#exploration-a--garment--backpack-shoulder-mount).
  Helmet fitment is deferred (own exploration B in that doc).
- Whether to add a small inertial sensor purely as a *cross-check / tamper or
  fallen-helmet detector* (NOT for brake detection — that's the patented approach we
  avoid). Deferred.
