# Transmitter — software

Bike-side firmware (ESP32-C3, ESP-IDF). See
[`docs/firmware.md §1`](../../docs/firmware.md#1-transmitter-firmware-bike-side).

Responsibilities:
- TWAI (CAN 2.0) in **listen-only mode**, filtered to the [bike profile](../../docs/can-profiles.md) IDs.
- Decode `brake_switch`, `throttle_pct`, `rpm`, `clutch_pulled`.
- Run the [braking state machine](../../docs/firmware.md#braking-state-machine) (50 Hz).
- Broadcast `chmbl_msg_t` heartbeat over [ESP-NOW](../../docs/protocol.md) at 20–50 Hz.
- Power management: deep-sleep when the bus is idle / bike off.

Keep the state machine and profile decoder **platform-independent** so they can be
host-unit-tested without hardware.

_Firmware to be added (Phase 1–3). Raw CAN capture logs go under `captures/`._
