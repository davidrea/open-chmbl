# DE-08 — Embedded CAN decode

**Status:** 🔲 not started · **Device(s):** transmitter · **Depends on:** DE-00, DE-07

The on-device CAN reception and profile-based decode that turns raw frames into the
engineering-unit signals the state machine consumes. **Scheduled only after captures
exist** (DE-07) so the [bike profile](../can-profiles.md) is known. Strictly
**listen-only**.

## 1. Scope & isolation boundary
- **In:** TWAI listen-only setup, bit-rate config, ID filtering, frame RX, applying the
  [`bike_profile_t`](../can-profiles.md#4-profile-data-structure) (`wheel_speed`,
  `clutch_pulled`, `gear`/`neutral`, `throttle_pct`, `rpm`), the derived **acceleration**
  (smoothed `d(wheel_speed)/dt` in MPH/s), and per-signal validity/staleness.
- **Out (faked at edges):** upstream is a real bike or a **replayed capture**
  (`can replay`); downstream the state machine (DE-09) is *not* required — we read
  decoded values via `sig show`. The state machine can be tested separately by faking
  `sig set` / `sig ramp`.
- **Isolation test:** feed a recorded capture (or bench bus) → verify `sig show`
  (including derived `accel`) matches the logged actions.

## 2. FFL traceability
TX-CAN-1…5, TX-DEC-1…7.

## 3. Component selection
ESP32-C3 TWAI controller + SN65HVD230 transceiver — see
[`hardware.md §1`](../hardware.md#1-transmitter-bike-side).

## 4. I/O assignments & configuration
- TWAI TX/RX pins, **listen-only mode**, bit rate (from DE-07), acceptance filter to
  profile IDs.
- Profile bit/scale/offset extraction; per-signal staleness timeouts.

## 5. Firmware module/task decomposition
- CAN RX task (bus-driven) → decode → publish signal struct.
- Pure/host-testable: the **profile decoder** (bytes → signals) and staleness logic,
  validated by replaying capture logs off-target (`python-can`/`cantools` for the
  reference data).

## 6. CLI hooks
- `can show` (bit rate, frame rate, IDs, health), `can replay <name>`, `sig show`,
  `sig source can|fake`.

## 7. Isolation acceptance
- A replayed coast-down/braking capture reproduces the correct decoded `sig` values
  (`wheel_speed`, `clutch_pulled`, `gear`/`neutral`, `throttle_pct`, `rpm`), a sane
  derived `accel`, and the validity flags; listen-only confirmed (no frames emitted).

## 8. Open items
- Free-running broadcast vs. request/response (the [DE-07 gate](../can-profiles.md#5-reference-target--triumph-speed-400-tr-series-platform)).
- Compile-time vs. runtime profile selection.
