# Transmitter — hardware

Bike-side electronics. See [`docs/hardware.md §1`](../../docs/hardware.md#1-transmitter-bike-side)
for the block diagram and parts sketch.

Key constraints:
- ESP32-C3 + **SN65HVD230** CAN transceiver (3.3 V).
- Automotive-grade buck (12 V → 3.3 V), reverse-polarity + TVS + fuse protection.
- **Listen-only** CAN — never ACK or transmit on the bus.
- Deep-sleep / load-shed when the bike is off (< 1 mA parked).
- Sealed, IP65+, vibration- and heat-tolerant enclosure.

Reference bike connector ([Triumph Speed 400 / Scrambler 400 X / Street Triple 765](../../docs/can-profiles.md#5-reference-target--triumph-speed-400-tr-series-platform)):
**red 6-pin OBD2** under the seat, carrying CAN-H/CAN-L + K-line. Use a 6-pin →
16-pin adapter, or wire a mating 6-pin plug directly. **Confirm exact pin positions
and bus bit rate by probing before connecting.**

## Packaging & mounting (plan of record)

The transmitter is **not** a milled/clamshell enclosure like the brake light. It is a
**single PCB** sealed in **heat-shrink tubing** (adhesive-lined, dual-wall) and
**retained beneath the seat with adhesive tape** (VHB). There is plenty of room and a
benign-but-vibratory environment under the seat, so a low-profile shrink-wrapped board
taped to a flat pan is the lightest, cheapest, and most serviceable option — no
custom tooling, and the board can be inspected/replaced by peeling the tape.

- **Sealing:** dual-wall adhesive-lined heat-shrink over the populated board gives a
  splash-resistant, strain-relieved package. Bring the harness out one end and shrink
  over the wire entry for strain relief. (This is the practical equivalent of the
  IP65+ goal in [`docs/hardware.md §1`](../../docs/hardware.md#1-transmitter-bike-side)
  for an under-seat, not weather-exposed, location — revisit potting if a given bike
  routes the unit somewhere wetter.)
- **Retention:** VHB tape to a clean, flat under-seat surface. No brackets, no drilling.

### First-prototype harness

First prototypes avoid building a custom bike-connector harness from scratch. Instead:

1. Start from an **off-the-shelf Euro 5 → OBD2 adapter cable** (the diagnostic
   adapters sold for Euro-5 motorcycles; mates the bike's proprietary diagnostic plug
   and breaks it out to a standard 16-pin OBD2 connector).
2. **Cut off the large 16-pin OBD2 connector** — we don't need it.
3. **Fit a wire-to-board connector** to the cut end for a clean, keyed connection to
   the transmitter PCB. Plan of record: a **Molex SL-type header, 4-pin**. Four
   positions cover everything **populated on the reference motorcycle** (the Triumph
   diagnostic plug carries **12 V, GND, CAN-H, CAN-L**; K-line is unused by the
   listen-only transmitter), so a 4-pin SL is sufficient and keeps the BOM small.

> Only the four populated conductors are used. Verify continuity and pinout from the
> adapter cable to the board header **before first power-up** — adapter-cable wiring
> is not guaranteed consistent between vendors. Confirm which pin is switched vs.
> constant 12 V (drives the sleep strategy in [`docs/hardware.md §1`](../../docs/hardware.md#1-transmitter-bike-side)).

_Schematics, BOM, and connector pinout to be added (Phase 4)._
