# DE-09 — BRAKE/DECEL logic

**Status:** 🔲 not started · **Device(s):** transmitter · **Depends on:** DE-00, DE-08

The braking state machine: fuse the decoded signals into `OFF` / `BRAKE` / `DECEL`
with `SHIFT` suppression and anti-strobe dwell. Designed as a **pure function** so it
can be developed and unit-tested entirely from faked signals — no CAN, no radio. See
[`firmware.md §braking-state-machine`](../firmware.md#braking-state-machine).

## 1. Scope & isolation boundary
- **In:** debounce, hysteresis, the OFF/BRAKE/DECEL/SHIFT transition logic, dwell/anti-
  strobe timing, the `enable_decel` and clutch-availability gating, BRAKE priority.
- **Out (faked at edges):** input signals come from `sig set` (not live CAN — that's
  DE-08); the output is read via `state show` and need not drive a real radio (DE-01).
- **Isolation test:** transmitter board (or host unit test) driving faked
  brake/throttle/rpm/clutch and asserting the output state + timing.

## 2. FFL traceability
TX-SM-1…6.

## 3. Component selection
None — pure logic. Threshold/timer constants from [`firmware.md`](../firmware.md), to
be calibrated against DE-07 ride logs.

## 4. I/O assignments & configuration
- Tunables: `BRAKE_DEBOUNCE_MS`, `THROTTLE_CLOSED_PCT`, `RPM_FALL_THRESH`,
  `DECEL_MIN_DWELL_MS`, `SHIFT_HOLD_MS`, `STATE_MIN_DWELL_MS` (see firmware doc).

## 5. Firmware module/task decomposition
- State-machine tick (~50 Hz) consuming the signal struct, emitting `brake_state_t`.
- **Pure/host-testable** core — the primary reason this element is isolated: full unit
  tests with synthetic signal/time sequences, no hardware.

## 6. CLI hooks
- `sig set brake|throttle|rpm|clutch|wheel`, `sig source fake`, `state show`,
  `state force` (override for downstream tests).

## 7. Isolation acceptance
- Brake switch → `BRAKE` within budget and over `DECEL`/`SHIFT`;
  throttle-closed + rpm-falling + clutch-out → `DECEL` (when enabled);
  clutch-in → no flicker (`SHIFT` hold); no transition violates the anti-strobe floor;
  `enable_decel=false` or clutch unavailable → `DECEL` suppressed.

## 8. Open items
- Final threshold values (calibrate on DE-07 wheel-speed/RPM logs).
- Whether wheel-speed-derived deceleration feeds `DECEL` live (see roadmap).
