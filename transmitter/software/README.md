# Transmitter — software

Bike-side firmware (ESP32-C3, ESP-IDF). See
[`docs/firmware.md §1`](../../docs/firmware.md#1-transmitter-firmware-bike-side).

Responsibilities:
- TWAI (CAN 2.0) in **listen-only mode**, filtered to the [bike profile](../../docs/can-profiles.md) IDs.
- Decode `wheel_speed`, `throttle_pct`, `rpm`, `clutch_pulled`, `gear`/`neutral`.
  (The reference bus carries **no brake-switch bit** — braking is inferred from
  wheel-speed-derived acceleration; see [DE-09](../../docs/design/de-09-brake-decel-logic.md).)
- Run the [braking state machine](../../docs/firmware.md#braking-state-machine) (50 Hz).
- Broadcast `chmbl_msg_t` heartbeat over [ESP-NOW](../../docs/protocol.md) at 20–50 Hz.
- Power management: deep-sleep when the bus is idle / bike off.

Keep the state machine and profile decoder **platform-independent** so they can be
host-unit-tested without hardware.

The braking state machine is specified in `state_machine/brake_fsm.sm` and compiled by
the [SMC State Machine Compiler](https://smc.sourceforge.net/) as a CMake pre-build step
(see [`firmware.md §4`](../../docs/firmware.md#4-build--toolchain)). **A JRE is required
on the build host**; `Smc.jar` lives under `tools/smc/`. Generated `brake_fsm_sm.[ch]`
are build artifacts and are not committed.

_Firmware to be added (Phase 1–3). Raw CAN capture logs go under `captures/`._
