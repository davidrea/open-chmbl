# DE-10 — Status-indicator LED

**Status:** 🟡 **minimal slice landed** (link health only; code engine deferred) · **Device(s):** brake_light · **Depends on:** DE-00

A **small dedicated indicator LED**, separate from the main brake-light array, that
reports device status and faults by **color and/or blink code**. The point is *discrete
diagnostics*: a rider (or bench tech) can read pairing, link, charge, and fault state
without leaning on the main bar — which may be off, dimmed, or itself the thing that's
broken. See [`hardware.md §2`](../hardware.md#2-brake_light-rider-side).

> ## Pulled forward: link-loss indication now lives here
>
> [DE-03](de-03-link-loss-failsafe.md) used to indicate link loss by **blinking the brake
> bar**, because the bench dev board had only one LED. That is no longer acceptable — the
> bar is now a binary GPIO ([DE-04](de-04-led-render.md)) and blinking a stop lamp is
> [forbidden](../safety-regulatory.md#1-legal--regulatory-varies-by-jurisdiction). The
> first-pass PCB gives us somewhere proper to put it, so a **minimal slice of this
> element landed early**, out of build order, to carry DE-03's indication:
>
> | Link status | Indicator |
> |-------------|-----------|
> | `UP` | GRN (D14) steady |
> | `WAITING` (booted, no packet yet) | GRN slow blink |
> | `LOST` | RED (D13) slow blink |
>
> Half-period `CHMBL_STATUS_BLINK_MS` (500 ms), Kconfig-floored at 150 ms so the
> indicator cannot strobe (BL-IND-4). It is driven by its own task and is fully
> independent of the bar (BL-IND-1) — `ind show` reads it back.
>
> **Deliberately NOT implemented** (this is a slice, not the element): the status-code
> enum beyond link health, **priority resolution**, **fault-class blink codes**
> (BL-IND-3), battery / charging / low-battery codes, night-dim and the disable flag
> (BL-IND-5), and the `ind test` / `ind source` CLI verbs. Those land when DE-10 is
> scheduled properly, alongside DE-05.
>
> Two things also block the fuller element on this board revision: the charger's `~CHG`
> output is **not** connected to the MCU and there is **no battery-voltage sense net**, so
> charge and battery codes are not observable in firmware yet
> ([hardware README](../../brake_light/hardware/README.md#open-items-from-this-first-pass)).

## 1. Scope & isolation boundary
- **In:** the status-code model (enum of states → color/blink pattern), the indicator
  driver, anti-strobe pacing, and the night-dim/disable behaviour → a single
  **indicator output** distinct from the main render path.
- **Out (faked at edges):** the *sources* of status (link health from DE-03, battery
  from DE-05, pairing from the radio) are injected via the CLI; the main-bar render
  (DE-04) is untouched. This element only owns "given a status code, drive the
  indicator."
- **Isolation test:** brake_light board only; force each status code over the CLI and
  watch the indicator's color/blink — no radio link or real fault required.

## 2. FFL traceability
BL-IND-1…5 (and BL-CLI-6 for the hook). Realizes the indicator behind BL-UI-2.

## 3. Component selection

**As designed:** one **addressable RGB LED** (WS2812/SK6812-class) on a single GPIO,
placed **chip-down** on the brake_light PCB — baseline `WS2812B-2020` or `WS2812B-Mini`
(LCSC `C2761795`, `C114586`). The earlier "reuse the module's onboard WS2812 for free"
option was retired when the brake_light moved to a bare `ESP32-C3-WROOM-02` module +
chip-down BOM
([`hardware.md §2.1`](../hardware.md#21-parts-direction-bare-module--chip-down)).

**As built (first-pass PCB) — two discrete mono LEDs, not a WS2812:**

| | Designed | First-pass PCB |
|---|---|---|
| Part | 1 × addressable RGB (WS2812B-2020) | **D13 (RED) + D14 (GRN)**, two discrete mono LEDs |
| Drive | 1 GPIO, RMT serial protocol | **2 GPIOs**, each anode-fed through 100 Ω (R21 / R20), cathode on GND ⇒ **active high** |
| Colour space | full RGB | red / green / both (amber-ish) / off |

This is a real reduction: amber and blue from the §7 code table are not directly
available, so a fuller code table on this board has to lean even harder on **blink
patterns** rather than colour — which §8 wanted anyway for colour-blind legibility. Two
discretes are simpler to drive and to diffuse, so this is a reasonable trade for a first
board; whether to go back to a WS2812 on the next revision is an open item.

## 4. I/O assignments & configuration

**As implemented:** two plain output GPIOs, active high, Kconfig-selected
(`CHMBL_STATUS_LED_RED_GPIO`, `CHMBL_STATUS_LED_GRN_GPIO`; `-1` = not fitted), plus
`CHMBL_STATUS_BLINK_MS` (500 ms half-period, Kconfig-floored at 150 ms).

> ⚠️ **The GPIO numbers are provisional.** Both status-LED feeds (R20 pin 1, R21 pin 1)
> are **unconnected in the committed first-pass netlist**, so the firmware defaults
> (`IO7` red / `IO10` green on the ESP32-C3) are a proposal pending a schematic review,
> not a verified assignment. The *active-high orientation* **is** verified. See the
> [hardware README pin map](../../brake_light/hardware/README.md#pin-map).

**As designed (still, for the full element):** the **code table** (state → colour + blink
period/duty), an anti-strobe floor, and a night-dim level / disable flag — none of which
exist yet beyond the link-health rows above.

## 5. Firmware module/task decomposition
- Indicator task (~10–20 Hz): take the highest-priority active status code → emit the
  color/blink for it; pace it against the anti-strobe floor.
- Pure/host-testable: the **status-code → pattern** mapping and the blink generator
  (fed synthetic codes/time; no hardware).
- Priority resolution (e.g. fault > pairing > link-lost > charging > battery > idle) is
  a small pure function.

## 6. CLI hooks
- `ind show` — current status code, resolved color/blink, source, night-dim state.
  **Implemented** (minus night-dim, which doesn't exist yet).
- `ind test <code|color|off>` — force an indicator code/color (preview a blink code).
  *Deferred with the code engine.*
- `ind source status|fake` — switch back to live status aggregation.
  *Deferred; the only source today is link health.*

## 7. Isolation acceptance
- Each status code renders its **distinct, documented** color/blink and is readable by
  eye; priority resolution shows the right code when several are active; the indicator
  is **independent of the main bar** (drive it with the bar off); anti-strobe floor
  honored; night-dim reduces brightness without losing legibility.

## 8. Open items
- Final **code table** (which states, which colors/blinks) — define alongside DE-03
  (link) and DE-05 (battery) so their faults have assigned codes. The three link-health
  rows in the box at the top are the only ones fixed so far, and they are provisional
  within the eventual table.
- **Priority resolution, fault-class blink codes, battery/charge codes and night-dim**
  are all still to build — see the "deliberately NOT implemented" list at the top.
- **Wire the two status-LED feeds** and settle their GPIO numbers (§4), replacing the
  provisional firmware defaults.
- **Discrete red+green vs. a WS2812 on the next board revision** (§3). Two mono LEDs lose
  amber/blue from the §7 table; decide whether the code table shrinks to fit or the part
  goes back to addressable RGB.
- **Charge and battery codes are not observable on this board revision** — the charger's
  `~CHG` is not routed to the MCU and there is no battery-voltage sense net. Either add
  them on the next revision or drop those codes.
- Placement of the chip-down WS2812 in the ~8″-wide enclosure so the rider (or a
  bench tech) can actually see it — off-axis from the main bar, on a face the wearer
  can glance at, with a small light pipe / diffuser if needed.
- Color-blind-friendly coding (lean on **blink patterns**, not color alone, for the
  safety-relevant distinctions).
