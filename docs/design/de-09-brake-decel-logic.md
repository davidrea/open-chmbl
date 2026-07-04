# DE-09 — Braking state machine

**Status:** 🔲 not started · **Device(s):** transmitter · **Depends on:** DE-00, DE-08

The braking state machine: fuse the decoded CAN signals into a `BRAKE` / `OFF` light
decision. The reference bike (Triumph Speed 400) **does not publish a brake-switch bit
on its CAN bus**, so braking is **inferred from wheel-speed-derived acceleration**,
qualified by clutch and gear/neutral context. Designed as a **pure function** so it can
be developed and unit-tested entirely from faked signals — no CAN, no radio. See
[`firmware.md §braking-state-machine`](../firmware.md#braking-state-machine).

The state machine itself is **specified in an SMC ([State Machine Compiler]) `.sm`
model** and the generated C is built by a **CMake pre-build step** — see
[§5](#5-firmware-moduletask-decomposition) and
[`firmware.md §4`](../firmware.md#4-build--toolchain). The `.sm` model is the single
source of truth for the transition structure; this document is the rationale.

[State Machine Compiler]: https://smc.sourceforge.net/

## 1. Scope & isolation boundary
- **In:** the speed/acceleration derivation (smoothing, MPH/s estimate), the
  `OFF`/`BRAKING`/`STOPPED` transition logic, the steady/stop/anti-strobe timers, the
  clutch-and-gear gating, and the mapping of state → emitted `brake_state_t`.
- **Out (faked at edges):** input signals come from `sig set` (not live CAN — that's
  DE-08); the output is read via `state show` and need not drive a real radio (DE-01).
- **Isolation test:** transmitter board (or host unit test) driving a **synthetic
  speed/clutch/gear profile** and asserting the output state + transition timing.

## 2. FFL traceability
TX-SM-1…6.

## 3. Inputs & derived signals

The machine consumes decoded CAN signals (DE-08) and one **derived** quantity:

| Signal | Source | Use |
|--------|--------|-----|
| `wheel_speed` → `speed_mph` | CAN (cluster) | Converted to MPH; the primary input. |
| `accel_mphps` | **derived** from `speed_mph` | Signed acceleration (− = decel). The core trigger. |
| `clutch_pulled` | CAN (confirmed present) | Gates the "launching from a stop" exit. |
| `gear` / `neutral` | CAN (cluster) | Distinguishes a parked/neutral stop from a hold-clutch-in-gear stop. |
| `rpm` | CAN | Not required by this FSM; retained for diagnostics/telemetry. |

> **No `brake_switch`.** It is confirmed *absent* from the reference bus, so the FSM
> never sees it. Deceleration is computed from the bike's own CAN **wheel-speed**
> signal, not an on-board accelerometer/gyro — this is what keeps the design clear of
> the inertial-detection patent family (see [ARCHITECTURE §1](../../ARCHITECTURE.md#1-why-this-approach)).

**Acceleration estimate.** `accel_mphps` is the slope of `speed_mph` over a short
window (e.g. linear fit / low-pass over ~100–200 ms), not a raw sample-to-sample diff —
CAN speed is quantized and noisy, and the whole FSM hinges on a clean derivative.
Window length and filter are tunables to be calibrated on DE-07 ride logs.

## 4. State machine

**States:** `OFF`, `BRAKING`, `STOPPED`. Both `BRAKING` and `STOPPED` render as
`ST_BRAKE` (light on); `OFF` renders as `ST_OFF`. (`ST_DECEL` is left reserved in the
protocol — see [protocol.md](../protocol.md).)

```
                ┌─────────────────────────────── OFF ───────────────────────────────┐
                │                              (light off)                            │
                │  decel > DECEL_ON                         speed < STOP_SPEED         │
                ▼                                                          ▼
        ┌───────────────┐   speed < STOP_SPEED          ┌───────────────────────────┐
        │   BRAKING      │ ───────────────────────────▶ │          STOPPED           │
        │  (light on)    │                               │        (light on)          │
        └───────┬───────┘                               └─────────────┬──────────────┘
                │  accel > ACCEL_OFF & speed > 5                       │ speed > MOVING_SPEED
                │  OR steady |accel| < band for STEADY_TIMEOUT         │ OR (clutch released in gear & rolling)
                ▼                                                      │ OR stopped > STOP_TIMEOUT
               OFF ◀──────────────────────────────────────────────────┘
```

**Transitions (Poll fired ~50 Hz; first matching guard wins):**

| From | Guard | To | Rule |
|------|-------|----|------|
| `OFF` | deceleration > `decel_on_mphps` held for `decel_on_debounce_ms` | `BRAKING` | 1 — turn on when slowing hard |
| `OFF` | `speed_mph` < `stop_speed_mph` | `STOPPED` | 2 — turn on when settling to a stop |
| `BRAKING` | `speed_mph` < `stop_speed_mph` | `STOPPED` | 3 — keep on through the stop |
| `BRAKING` | acceleration > `accel_off_mphps` **and** `speed_mph` > `accel_off_min_speed_mph` | `OFF` | 4 — accelerating away |
| `BRAKING` | \|accel\| < `steady_band_mphps` for `steady_timeout_ms` | `OFF` | 5 — steady cruise after braking |
| `STOPPED` | `speed_mph` > `moving_speed_mph` | `OFF` | 6a — moving away from the stop (hysteresis) |
| `STOPPED` | clutch released **and** in gear (not neutral) **and** `speed_mph` > `stop_speed_mph` | `OFF` | 6b — launching |
| `STOPPED` | stopped for > `stop_timeout_ms` | `OFF` | 6c — parked / long stop |

Notes:
- **Rule 6b uses gear/neutral.** "Clutch released" only means *launching* when the bike
  is in gear (you must hold the clutch in to sit in gear). In **neutral** the guard is
  never true, so a neutral stop holds the light until rule 6a or 6c — exactly the
  parked-at-a-light case.
- **Low-speed hysteresis (rules 6a/6b).** `STOPPED` enters at `speed < stop_speed_mph`
  (1.0) but only exits for motion at `speed > moving_speed_mph` (3.0); the launch guard
  (6b) additionally requires the bike to be **rolling** (`speed > stop_speed_mph`) so it
  cannot ping-pong against the entry rule (rule 2) while sitting still in gear with the
  clutch out. This is what makes creep-in-traffic hold a steady light instead of
  strobing. On the [DE-07 40 mph ride log] the gap cut FSM transitions **162 → 48** and
  sub-0.5 s light blips **65 → 8**. The anti-strobe dwell can't substitute: the guards
  genuinely oscillate across a single threshold, so the fix is hysteresis, not
  rate-limiting.
- **Decel-on debounce (rule 1).** The `OFF → BRAKING` trigger requires
  `decel > decel_on_mphps` to hold **continuously** for `decel_on_debounce_ms` (120 ms).
  Momentary throttle-off dips spike the wheel-speed derivative past the threshold and
  clear within a tick or two; the debounce rejects them **without attenuating the
  signal**. This is deliberately a debounce and **not** a heavier low-pass filter: extra
  smoothing would suppress the same spikes only by delaying genuine hard-braking onset,
  which is the wrong trade for a safety light. Sustained braking still fires ≤ 120 ms
  later. On the [DE-07 40 mph ride log] it removed the last momentary `BRAKING` blips
  (8 → 3, transitions 48 → 30) at ~unchanged on-time; larger values began clipping real
  short brake taps, so 120 ms is the knee.
- **Stop-and-go:** creeping *past* `moving_speed_mph` drops `STOPPED → OFF`; the next
  gentle slowdown re-enters via rule 1/2. Sustained slow creep below the hysteresis band
  now holds the light rather than blinking.

[DE-07 40 mph ride log]: ../can-profiles.md#decode-table
- **Anti-strobe** is a global `state_min_dwell_ms` floor: the tick handler will not
  dispatch a state-changing `Poll` until the floor has elapsed since the last
  transition. Keeps the `.sm` model focused on logic, not flicker.

## 5. Firmware module/task decomposition
- **SMC model (`brake_fsm.sm`)** specifies the states, the single `Poll` event, the
  guards, and the entry actions (`setOutput`, timer resets). It is the single source of
  truth for the transition structure.
- **CMake pre-build step** runs SMC (`java -jar Smc.jar -c …`) to generate
  `brake_fsm_sm.[ch]` into the build tree before the firmware compiles; the firmware
  target depends on the generated sources so editing the `.sm` triggers regeneration.
  Optionally also emit a Graphviz `.dot` (`-graph`) so the diagram above stays in sync.
  Mechanics in [`firmware.md §4`](../firmware.md#4-build--toolchain).
- **Host context (`BrakeFsmCtx`)** — pure, host-testable: holds the smoothed
  `speed_mph`/`accel_mphps`, the steady/stop timers, and implements the guard predicates
  (`isDecelExceeded`, `isStopped`, `isAcceleratingAway`, `isSteadyElapsed`,
  `isMovingAwayFromStop`, `isClutchReleasedInGear`, `isStopTimeoutElapsed`). This is the
  primary reason the element is isolated: full unit tests with synthetic speed/time
  sequences, no hardware and no generated code dependency beyond the FSM shell.
- **Tick task (~50 Hz)** updates the context from the latest signals, enforces the
  anti-strobe floor, and fires `Poll` on the generated FSM.

## 6. CLI hooks
- `sig set wheel <mph>`, `sig set clutch <0|1|na>`, `sig set gear <n|N>`,
  `sig set throttle`/`rpm` (for completeness); `sig source fake`; `state show`
  (current state + active timers + derived `accel_mphps`); `state force` (override for
  downstream tests). See [`cli.md`](../cli.md).

## 7. Isolation acceptance
- A synthetic decel ramp steeper than `decel_on_mphps` → `BRAKING` within budget; a
  gentle coast-to-stop → `STOPPED` via rule 2; reaching 0 from `BRAKING` → `STOPPED`
  (stays on).
- From `BRAKING`: a positive accel ramp above threshold (speed > 5 mph) → `OFF` (rule
  4); a held-steady speed → `OFF` after `steady_timeout_ms` (rule 5).
- From `STOPPED`: pulling away (speed > `moving_speed_mph`) → `OFF`; clutch released in
  gear **while rolling** (speed > `stop_speed_mph`) → `OFF`; neutral-with-clutch-out does
  **not** turn off until the `stop_timeout_ms` expires; the 60 s timeout fires.
- **Hysteresis:** a slow creep that stays between `stop_speed_mph` and `moving_speed_mph`
  in **neutral** holds `STOPPED` (no strobe); a standstill in gear with clutch out does
  **not** ping-pong `STOPPED`↔`OFF`.
- No transition violates the anti-strobe floor; the emitted `brake_state_t` matches the
  state map.

## 8. Open items
- Final tunable values and the acceleration smoothing window/filter (calibrate on DE-07
  wheel-speed ride logs).
- Confirm `gear`/`neutral` and `wheel_speed` are actually decodable on the reference
  bus (presumed available because the cluster displays them — verify in DE-07).
- ~~Stop-and-go flicker policy (a `STOPPED`→`OFF` hold/hysteresis vs. the literal
  rules).~~ **Resolved:** added `moving_speed_mph` hysteresis on the `STOPPED` exit +
  a rolling qualifier on the launch guard (see [§4 note](#4-state-machine)); validated on
  the DE-07 ride log (transitions 162 → 48). Final value (3.0 mph) still to confirm on
  more logs.
- Whether to ever use the reserved `ST_DECEL` tier for a softer coasting cue.
