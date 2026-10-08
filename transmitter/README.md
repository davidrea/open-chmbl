# Transmitter (bike-side)

Plugs into the motorcycle's diagnostic port, reads the CAN bus **listen-only**,
decodes brake/throttle/RPM/clutch via the active [bike profile](../docs/can-profiles.md),
runs the [braking state machine](../docs/firmware.md#braking-state-machine), and
broadcasts state to the [`brake_light`](../brake_light) over
[ESP-NOW](../docs/protocol.md).

- [`hardware/`](hardware) — reuses the [`logger/`](../logger) PCB (ESP32-S3); see
  [`hardware/README.md`](hardware/README.md) for pinout and BOM notes.
- [`software/`](software) — ESP32-S3 firmware for that board: TWAI listen-only,
  profile decode, the [braking state machine](../docs/design/de-09-brake-decel-logic.md)
  driving the light, and ESP-NOW TX. See
  [`software/README.md`](software/README.md).

See [`ARCHITECTURE.md`](../ARCHITECTURE.md) for the big picture and
[`docs/hardware.md`](../docs/hardware.md) for the parts sketch.
