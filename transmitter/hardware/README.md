# Transmitter — hardware

Bike-side electronics. See [`docs/hardware.md §1`](../../docs/hardware.md#1-transmitter-bike-side)
for the block diagram and parts sketch.

Key constraints:
- ESP32-C3 + **SN65HVD230** CAN transceiver (3.3 V).
- Automotive-grade buck (12 V → 3.3 V), reverse-polarity + TVS + fuse protection.
- **Listen-only** CAN — never ACK or transmit on the bus.
- Deep-sleep / load-shed when the bike is off (< 1 mA parked).
- Sealed, IP65+, vibration- and heat-tolerant enclosure.

_Schematics, BOM, and connector pinout to be added (Phase 4)._
