# Transmitter (bike-side)

Plugs into the motorcycle's diagnostic port, reads the CAN bus **listen-only**,
decodes brake/throttle/RPM/clutch via the active [bike profile](../docs/can-profiles.md),
runs the [braking state machine](../docs/firmware.md#braking-state-machine), and
broadcasts state to the [`brake_light`](../brake_light) over
[ESP-NOW](../docs/protocol.md).

- [`hardware/`](hardware) — schematics, BOM, enclosure, connector pinout.
- [`software/`](software) — ESP32-C3 firmware (TWAI listen-only + ESP-NOW TX).

See [`ARCHITECTURE.md`](../ARCHITECTURE.md) for the big picture and
[`docs/hardware.md`](../docs/hardware.md) for the parts sketch.
