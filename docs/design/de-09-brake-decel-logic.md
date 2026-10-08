# DE-09 — Braking state machine

**Status:** 🟢 implemented · **Device(s):** transmitter · **Depends on:** DE-00, DE-08

The braking state machine: fuse the decoded CAN signals into a `BRAKE` / `OFF` light
decision. The reference bike (Triumph Speed 400) **does not publish a brake-switch bit
on its CAN bus**, so braking is **inferred from wheel-speed-derived acceleration**,
qualified by clutch and gear/neutral context. Designed as a **pure function** so it can
be developed and unit-tested entirely from faked signals — no CAN, no radio. See
[`firmware.md §braking-state-machine`](../firmware.md#braking-state-machine).

The state machine lives in `transmitter/software/main/brake_fsm.c` as a plain,
dependency-free C function, ticked at 50 Hz by `brake_ctl.c`. This document is the
rationale; `brake_fsm.c` and the reference implementation it is tested against
([§5](#5-firmware-moduletask-decomposition)) are the code.

> **Not SMC.** Earlier drafts specified this machine in an [SMC (State Machine
> Compiler)] `.sm` model compiled by a CMake pre-build step. That was dropped when the
> element was implemented: the calibrated algorithm is three states and one event, and
> an SMC model would have added a **JRE on every build host and in CI** to generate
> ~40 lines of switch. What the tooling was meant to buy — one authoritative definition
> of the transition structure, mechanically checked — is instead bought by
> [`tools/fsm_check.py`](../../tools/fsm_check.py), which asserts the shipping C and the
> reference implementation agree tick for tick over a real ride capture. Revisit if the
> machine grows states (a soft `DECEL` tier would be the trigger).

[SMC (State Machine Compiler)]: https://smc.sourceforge.net/

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

**Acceleration estimate.** `accel_mphps` is *not* a raw sample-to-sample diff — CAN
speed is quantized (~0.039 mph) and noisy, and the whole FSM hinges on a clean
derivative. As implemented in `can_decode.c` (`accel_update`), each wheel-speed frame:

1. goes through a **dt-aware EMA low-pass** with time constant `speed_smooth_ms`
   (80 ms), so quantization steps and single-sample glitches don't become decel spikes;
2. is pushed into a **history ring**, and the slope is taken against the newest sample
   at least `CAN_DECODE_ACCEL_WINDOW_MS` (200 ms) old;
3. has that slope **exponentially smoothed** (`alpha` 0.3) into the reported value.

> **Size the ring against the frame rate.** The reference bus emits `0x102` at ~100 Hz,
> so 200 ms of history is ~20 samples; the ring holds 32. Undersize it and the "newest
> sample ≥ 200 ms old" search never succeeds, so the derived acceleration silently stops
> updating and the light never comes on — the signal stays *valid*, it just stops
> moving. The firmware shipped a 16-deep ring for a while and did exactly this; it is
> the reason `tools/fsm_check.py` exists.

**Signal loss.** `wheel_speed` is the one required input. When it is absent or stale
(`CAN_DECODE_STALE_MS`, 1 s) the machine is forced to `OFF` and rearmed, so a dropout
behaves like a fresh boot. A brake light that latches on because the bus went quiet is
worse than no light at all.

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
- **Anti-strobe** is a global `state_min_dwell_ms` floor: no guard is evaluated until
  the floor has elapsed since the last transition. It is deliberately outside the
  guards, so the transition table stays about logic and not about flicker.

### Tunables

Defaults as shipped in `brake_fsm.h` (`BRAKE_TUNABLES_DEFAULT`), calibrated on the DE-07
ride logs through [`tools/trc_viz.html`](../../tools/trc_viz.html). Speeds in MPH,
accelerations in MPH/s. All are settable live with `state tune <name> <value>`, over the
same ranges as the bench viewer's sliders — except `decel_on_debounce_ms`, whose minimum
is one tick rather than zero, because a zero debounce degenerates into "on every tick".

| Tunable | Default | Purpose |
|---------|--------:|---------|
| `decel_on_mphps` | 2.0 | Deceleration that turns the light **on** (rule 1). |
| `decel_on_debounce_ms` | 120 | ...held this long first. |
| `stop_speed_mph` | 1.0 | At/under = "stopped" (rules 2/3, rolling qualifier in 6b). |
| `moving_speed_mph` | 3.0 | Must be exceeded to leave `STOPPED` for motion (rule 6a). |
| `accel_off_mphps` | 0.5 | Acceleration that turns the light **off** (rule 4). |
| `accel_off_min_speed_mph` | 5.0 | ...only above this speed. |
| `steady_band_mphps` | 0.75 | \|accel\| under this counts as "steady" (rule 5). |
| `steady_timeout_ms` | 1500 | Steady-after-braking hold before turning off. |
| `stop_timeout_ms` | 60000 | Max on-time while stopped (rule 6c). |
| `state_min_dwell_ms` | 250 | Global anti-strobe floor. |
| `speed_smooth_ms` | 80 | Wheel-speed low-pass ahead of the slope (see §3). |

## 5. Firmware module/task decomposition
- **`brake_fsm.[ch]`** — the machine itself: the tunables struct, the three states, the
  hold timers, and one `brake_fsm_step(fsm, inputs, dt_ms)` call that advances it. Pure
  C, no ESP-IDF headers, no allocation, no clock of its own — it is handed `dt_ms`. That
  is what lets the identical code run on target at 50 Hz and on the host over a capture.
- **`brake_ctl.[ch]`** — the 50 Hz tick task on target: samples the decoded signals
  (`sig_snapshot`), maps them to FSM inputs (including the validity rules in §3), drives
  the brake-light output, and holds the `state force` override and the live tunables.
  All decision-shaped logic stays in `brake_fsm.c`; this is plumbing.
- **`can_decode.[ch]`** (DE-08) — supplies `speed_mph` and derives `accel_mphps`, per §3.
- **Reference implementation** — [`tools/trc_viz.py`](../../tools/trc_viz.py) and its
  in-browser twin [`tools/trc_viz.html`](../../tools/trc_viz.html): the same machine in
  Python/JS with every tunable on a slider, used to calibrate against ride captures.
- **Parity test** — [`tools/fsm_check.py`](../../tools/fsm_check.py) replays a capture
  through `test_host/fsm_replay` (the real `can_decode.c` + `brake_fsm.c`) and through
  the reference, and asserts **identical state at every tick**, plus agreement on the
  derived acceleration to 1e-3 mph/s. Runs in CI. This is what keeps the calibration
  bench and the shipping firmware the same machine; it is also what caught the
  ring-sizing defect in §3.

## 6. CLI hooks
- `sig set wheel <mph>`, `sig set clutch <0|1|na>`, `sig set gear <n|N>`,
  `sig set throttle`/`rpm` (for completeness); `sig source fake`; `sig ramp wheel
  <mph/s>` to drive a synthetic stop through the real derivation.
- `state show` — state, the inputs the last tick saw, the active timers, the emitted
  `brake_state_t`; `state force off|brake|auto`; `state tune [<name> <value>]` to sweep
  a threshold live. See [`cli.md`](../cli.md).

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
- **Signal loss:** `sig set wheel na` (or unplugging the bus) drops the light within a
  stale interval and leaves it off; restoring the signal behaves like a fresh start.
- **Against real rides:** `tools/fsm_check.py` over the DE-07 captures — tick-for-tick
  agreement with the reference, including the two captures with mid-ride wheel-speed
  dropouts, where the firmware holds the light off for the dark ticks and matches
  everywhere else.

## 8. Open items
- ~~Final tunable values and the acceleration smoothing window/filter.~~ **Resolved for
  now:** the table in [§4](#tunables) is what shipped, calibrated on the DE-07 logs in
  the bench viewer. Treat it as a starting point — it has not yet been ridden behind
  the real light.
- ~~Confirm `gear`/`neutral` and `wheel_speed` are actually decodable on the reference
  bus.~~ **Resolved in DE-07:** all present and decoded (see the golden test).
- **Not yet validated on the bike.** Every number above comes from replaying captures.
  The first on-road run with the light live is the real test, and the thing most likely
  to move is `decel_on_mphps`.
- ~~Stop-and-go flicker policy (a `STOPPED`→`OFF` hold/hysteresis vs. the literal
  rules).~~ **Resolved:** added `moving_speed_mph` hysteresis on the `STOPPED` exit +
  a rolling qualifier on the launch guard (see [§4 note](#4-state-machine)); validated on
  the DE-07 ride log (transitions 162 → 48). Final value (3.0 mph) still to confirm on
  more logs.
- Whether to ever use the reserved `ST_DECEL` tier for a softer coasting cue.
