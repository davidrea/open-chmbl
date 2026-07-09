# Transmitter — hardware

Bike-side electronics. See [`docs/hardware.md §1`](../../docs/hardware.md#1-transmitter-bike-side)
for the block diagram and parts sketch.

**Hardware plan: this board is the [`logger/`](../../logger) PCB**, reused with the
microSD slot (J5) and button/LED breakout (J4) left unpopulated — the transmitter
needs neither. No separate transmitter schematic exists; the logger's is the source
of truth. See [`logger/hardware/README.md`](../../logger/hardware/README.md) for the
schematic, connector pinout (J2 USB-C, J3 bike harness), and BOM notes, and
**[§5 there](../../logger/hardware/README.md#5-known-issue--gpio45s-pin-conflicts-with-a-boot-strap)**
for an open strapping-pin issue to check before building a board off this design.

As reused:
- **ESP32-S3-WROOM-1** + **TCAN330** CAN transceiver (3.3 V), silent-mode pin wired
  to a GPIO for a hardware-level listen-only default.
- Diode-ORed 12 V (bike) / USB-C VBUS power into a TPS62172 buck (12 V → 3.3 V).
  **Not** automotive load-dump-rated in this rev — reverse-polarity protection is a
  Schottky diode, not a FET; revisit before full-time bike use.
- **Listen-only** CAN — never ACK or transmit on the bus.
- J3 (bike-side connector, JST-PH 5-pin): CAN-L, CAN-H, +12 V, GND, spare.

Not yet on this board (deltas from the shared logger design, tracked for Phase 4):
- Deep-sleep / load-shed when the bike is off (< 1 mA parked) — needs an ignition
  sense the logger board doesn't have.
- Sealed, IP65+, vibration- and heat-tolerant enclosure.

Reference bike connector ([Triumph Speed 400 / Scrambler 400 X / Street Triple 765](../../docs/can-profiles.md#5-reference-target--triumph-speed-400-tr-series-platform)):
**red 6-pin OBD2** under the seat, carrying CAN-H/CAN-L + K-line. Use a 6-pin →
16-pin adapter, or wire a mating 6-pin plug directly into J3. **Confirm exact pin
positions and bus bit rate by probing before connecting.**
