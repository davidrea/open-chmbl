# Brake_light — hardware

Rider-side electronics. See [`docs/hardware.md §2`](../../docs/hardware.md#2-brake_light-rider-side)
for the block diagram and parts sketch.

Key constraints:
- **Bare ESP32-C3 module** (ESP32-C3-WROOM-02 baseline) + **chip-down** LiIon
  charger, DC-DC, and LED driver. Moved off the integrated dev-board path (previously
  scoped in the removed `§2.1` module survey) to unlock the thin/wide form factor
  below — see [`docs/hardware.md §2.1`](../../docs/hardware.md#21-parts-direction-bare-module--chip-down).
- **~8″ wide red LED bar** (620–630 nm mid-power) driven from a boost constant-current
  driver (TI **LM3410** baseline; upgrade parts noted in de-04). One series string of
  ~8 emitters — split into parallel strings if the emitter count outruns the driver's
  V<sub>out</sub> ceiling. Series-string + boost topology set in
  [`docs/design/de-04`](../../docs/design/de-04-led-render.md).
- **Ambient-light auto-dimming** (safety requirement — no blinding at night).
- **Status-indicator LED** — **discrete WS2812B** (2020 / Mini) chip-down, **separate
  from the bar** — for status/fault by color + blink code
  ([`docs/design/de-10`](../../docs/design/de-10-status-indicator.md)). No longer
  free-riding on a module's onboard RGB (we're on a bare module now).
- **Battery: 1S 18650 Li-ion, protected cell** + USB-C charging with **load sharing**
  (MCP73871 chip-down baseline). 18650 chosen over pouch/LiPo because pouch cells
  need mechanical compression the thin enclosure can't provide; protected cell means
  the load-share IC doesn't have to double as cell protection.
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

_Schematics, BOM, enclosure, and mount files to be added (Phase 4)._
