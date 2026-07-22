# DE-02 — Auto-brightness

**Status:** 🔲 not started · **Device(s):** brake_light · **Depends on:** DE-00

Ambient-light-driven brightness for the LED bar: bright enough to be seen in daylight,
dim enough not to blind following traffic at night. A standalone safety feature,
testable with no radio link.

## 1. Scope & isolation boundary
- **In:** ambient-light sensing, ambient→brightness mapping, day/night scaling,
  user brightness cap, change smoothing → a single **commanded brightness** output.
- **Out (faked at edges):** ambient lux is injected with `ambient set`; the braking
  *state* is irrelevant here (test against a fixed `in set state`); actual LED driving
  is DE-04 — this element produces the brightness number and we read it via `render show`.
- **Isolation test:** brake_light board only; sweep faked lux, watch commanded brightness.

## 2. FFL traceability
BL-BRT-1…4.

## 3. Component selection
Ambient-light sensor (digital lux sensor over I²C, or analog photodiode + ADC) — see
[`hardware.md §2`](../hardware.md#2-brake_light-rider-side). Selection TBD.

## 4. I/O assignments & configuration
- Sensor bus/pin, sample rate (~5 Hz), lux range.
- Brightness curve (lux → 0–100 %), day/night breakpoints, cap, smoothing time-constant.
- **Physical endpoints** the 0–100 % maps onto come from the
  [LED brightness benchmark §4](../led-brightness-benchmark.md#4-design-target-for-the-helmet-bar):
  daylight peak ≈ **50–80 cd** on-axis (BRAKE) down to a night floor ≈ **5–15 cd**.

## 5. Firmware module/task decomposition
- Ambient task (~5 Hz): read sensor → map → smooth → publish `commanded_brightness`.
- Pure/host-testable: the lux→brightness curve, smoothing, and cap (no hardware).

## 6. CLI hooks
- `ambient show`, `ambient set <lux>`, `ambient source sensor|fake`, `bright cap`,
  `render show` (to read resulting brightness).

## 7. Isolation acceptance
- Faked daylight lux → near-cap brightness; faked darkness → low brightness; cap
  respected; transitions smoothed (no step/flicker) as lux jumps.

## 8. Open items
- Sensor choice and placement (must see ambient, not the bar's own glow).
- Curve/breakpoint values — calibrate against real day/night readings, anchored to the
  [benchmark](../led-brightness-benchmark.md) cd endpoints.
