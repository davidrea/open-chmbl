# Brake_light — hardware

Helmet-side electronics. See [`docs/hardware.md §2`](../../docs/hardware.md#2-brake_light-helmet-side)
for the block diagram and parts sketch.

Key constraints:
- ESP32-C3 + red LED bar with constant-current driver (or addressable). Favor an
  ESP32-C3 **module that carries an onboard WS2812 + LiPo charger** to cut designed-in
  parts — see [`docs/hardware.md §2.1`](../../docs/hardware.md#21-integrated-module-candidates-ws2812--lipo-charger).
- 1S LiPo (protected cell) + USB-C charging with **load sharing** (e.g. MCP73871).
- **Ambient-light auto-dimming** (safety requirement — no blinding at night).
- **Status-indicator LED** (RGB WS2812, **separate from the bar**) for discrete
  status/fault by color + blink code — [`docs/design/de-10`](../../docs/design/de-10-status-indicator.md).
- Sized for a full-day runtime target (see hardware doc worked example).
- Lightweight, low-profile, **breakaway non-penetrating** mount — never drill the
  helmet. A [magnetic shear-release mount](../../docs/design/explorations/mounting-magnetic.md)
  is a future-state exploration.
- Sealed, IP65+ enclosure.

⚠️ Mounting + LiPo safety are mandatory reading: [`docs/safety-regulatory.md`](../../docs/safety-regulatory.md).

_Schematics, BOM, enclosure, and mount files to be added (Phase 4)._
