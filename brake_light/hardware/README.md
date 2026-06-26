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

## Enclosure

Mechanical package is being modeled in [`enclosure/`](enclosure): a **curved two-piece
clamshell** that conforms to the helmet/pack, capturing the PCB and a clear
**overmolded silicone lens** between an **inner shell** (helmet side, carries the
**recessed magnets**) and an **outer shell** (lens window). Parametric OpenSCAD; see
[`enclosure/README.md`](enclosure/README.md) for the strategy and the
[anti-shear reconciliation](enclosure/README.md#4-magnet-mounting--anti-shear-reconciliation)
with the [magnetic-mount plan of record](../../docs/design/explorations/mounting-magnetic.md).

⚠️ Mounting + LiPo safety are mandatory reading: [`docs/safety-regulatory.md`](../../docs/safety-regulatory.md).

_Schematics and BOM to be added (Phase 4)._
