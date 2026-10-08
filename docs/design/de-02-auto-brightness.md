# DE-02 — Auto-brightness

**Status:** ⏸ **deferred** (not dropped) · **Device(s):** brake_light · **Depends on:** DE-00

Ambient-light-driven brightness for the LED bar: bright enough to be seen in daylight,
dim enough not to blind following traffic at night. A standalone safety feature,
testable with no radio link.

> ## Deferred — see the DE-04 decision
>
> The owner's call was to **defer the whole brightness/auto-dimming axis** and get a
> working on/off light first. The brake bar is now driven as a **plain binary GPIO
> output** (high = on, low = off), with the LED current fixed in hardware by the driver's
> sense resistor. The decision, its reasoning and what it costs are recorded in
> **[DE-04 — the decision box](de-04-led-render.md)**.
>
> So: no PWM/LEDC, no duty cycle, no commanded brightness, no user brightness cap
> anywhere in the firmware's output path, and `BL-BRT-1…4` are **deferred, not deleted**.
> The `ambient *`, `bright cap` and brightness columns of `render show` in
> [`cli.md §4`](../cli.md#4-brake_light-cli) are deferred with them.
>
> **Nothing about the hardware has to change when this is picked back up.** The
> first-pass brake_light PCB already carries an **`ISL29035`** ambient-light sensor
> (U5) — **populated but unused**: deliberately left fitted so DE-02 is a firmware-only
> job later. Its bus is wired and **verified from the netlist**:
>
> | Signal | ESP32-C3 pin | Notes |
> |--------|--------------|-------|
> | `SDA` | `IO0` | 4.7 kΩ pull-up (R6) |
> | `SCL` | `IO1` | 4.7 kΩ pull-up (R7) |
> | `~INT` | `IO3` | 4.7 kΩ pull-up (R8) |
>
> (Full map: [`brake_light/hardware/README.md`](../../brake_light/hardware/README.md#pin-map).)
> The driver `EN`/`CTRL` pins the binary output uses are the *same* pins PWM dimming
> wants, so picking this up is swapping [DE-04 §4.1 for §4.2](de-04-led-render.md#4-io-assignments--configuration) —
> no rewiring.
>
> **This stays a safety requirement.** Deferring the *mechanism* does not retire the
> [no-blinding rule](../safety-regulatory.md#3-helmet--rider-safety): a single undimmed
> setpoint cannot serve both the daylight target and the night floor below, so until this
> element lands the fixed setpoint has to be chosen knowing which end is compromised.
> That is tracked as an open item in [DE-04 §8](de-04-led-render.md#8-open-items).

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
[`hardware.md §2`](../hardware.md#2-brake_light-rider-side). **Resolved by the first-pass
PCB: `ISL29035`**, a digital I²C ambient-light/IR sensor, already fitted (U5). Not read by
firmware yet — see the deferral note above.

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
- ~~Sensor choice~~ — **resolved: `ISL29035`** (fitted on the first-pass PCB, §3).
  **Placement** is still open (must see ambient, not the bar's own glow).
- Curve/breakpoint values — calibrate against real day/night readings, anchored to the
  [benchmark](../led-brightness-benchmark.md) cd endpoints.
- **When this is un-deferred:** add the `ISL29035` I²C driver, restore the PWM path
  ([DE-04 §4.2](de-04-led-render.md#42-deferred--pwmanalog-dimming-for-de-02)), and
  re-add the `ambient *` / `bright cap` commands and the brightness fields of
  `render show`. Firmware only — the sensor and the driver `EN` pins are already there.
