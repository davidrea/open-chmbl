# Brake_light — hardware

Helmet-side electronics. See [`docs/hardware.md §2`](../../docs/hardware.md#2-brake_light-helmet-side)
for the block diagram and parts sketch.

Key constraints:
- ESP32-C3 + red LED bar with constant-current driver (or addressable).
- 1S LiPo (protected cell) + USB-C charging with **load sharing** (e.g. MCP73871).
- **Ambient-light auto-dimming** (safety requirement — no blinding at night).
- Sized for a full-day runtime target (see hardware doc worked example).
- Lightweight, low-profile, **breakaway non-penetrating** mount — never drill the helmet.
- Sealed, IP65+ enclosure.

⚠️ Mounting + LiPo safety are mandatory reading: [`docs/safety-regulatory.md`](../../docs/safety-regulatory.md).

_Schematics, BOM, enclosure, and mount files to be added (Phase 4)._
