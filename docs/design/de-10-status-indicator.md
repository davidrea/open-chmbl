# DE-10 — Status-indicator LED

**Status:** 🔲 not started · **Device(s):** brake_light · **Depends on:** DE-00

A **small dedicated indicator LED**, separate from the main brake-light array, that
reports device status and faults by **color and/or blink code**. The point is *discrete
diagnostics*: a rider (or bench tech) can read pairing, link, charge, and fault state
without leaning on the main bar — which may be off, dimmed, or itself the thing that's
broken. Maps naturally onto a single addressable RGB LED (WS2812-class), including the
**onboard WS2812 found on several candidate ESP32-C3 modules** (see
[`hardware.md §2`](../hardware.md#2-brake_light-helmet-side)).

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
One **addressable RGB LED** (WS2812/SK6812-class) on a single GPIO, **or** the
**module's onboard WS2812** if the chosen ESP32-C3 board carries one — that's the whole
point of the [integrated-module survey](../hardware.md#21-integrated-module-candidates-ws2812--lipo-charger):
the status indicator is "free" on those boards. A plain mono LED could stand in but
loses the color channel. WS2812B variants are stocked at LCSC (`C2761795`, `C114586`,
small `WS2812B-2020`/`-Mini` for a low-profile indicator).

## 4. I/O assignments & configuration
- One digital GPIO (WS2812 data); shares the same RMT/driver approach as the main bar.
- Config: the **code table** (state → color + blink period/duty), an anti-strobe floor,
  and a night-dim level / disable flag.

## 5. Firmware module/task decomposition
- Indicator task (~10–20 Hz): take the highest-priority active status code → emit the
  color/blink for it; pace it against the anti-strobe floor.
- Pure/host-testable: the **status-code → pattern** mapping and the blink generator
  (fed synthetic codes/time; no hardware).
- Priority resolution (e.g. fault > pairing > link-lost > charging > battery > idle) is
  a small pure function.

## 6. CLI hooks
- `ind show` — current status code, resolved color/blink, source, night-dim state.
- `ind test <code|color|off>` — force an indicator code/color (preview a blink code).
- `ind source status|fake` — switch back to live status aggregation.

## 7. Isolation acceptance
- Each status code renders its **distinct, documented** color/blink and is readable by
  eye; priority resolution shows the right code when several are active; the indicator
  is **independent of the main bar** (drive it with the bar off); anti-strobe floor
  honored; night-dim reduces brightness without losing legibility.

## 8. Open items
- Final **code table** (which states, which colors/blinks) — define alongside DE-03
  (link) and DE-05 (battery) so their faults have assigned codes.
- Whether to reuse the module's onboard WS2812 vs. place a dedicated indicator where
  the rider can actually see it (module LED may be buried in the enclosure).
- Color-blind-friendly coding (lean on **blink patterns**, not color alone, for the
  safety-relevant distinctions).
