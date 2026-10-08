# DE-09 — Braking state machine

**Status:** 🟡 implemented as a shared C component, validated on ride logs, not yet
wired into the transmitter · **Device(s):** transmitter (product), `logger/` (on-board
preview) · **Depends on:** DE-00, DE-08

The braking state machine: fuse the decoded CAN signals into a `BRAKE` / `OFF` light
decision. The reference bike (Triumph Speed 400) **does not publish a brake-switch bit
on its CAN bus**, so braking is **inferred from wheel-speed-derived acceleration**,
qualified by clutch and gear/neutral context. Designed as a **pure function** so it can
be developed and unit-tested entirely from faked signals — no CAN, no radio. See
[`firmware.md §braking-state-machine`](../firmware.md#braking-state-machine).

**What exists now:** the machine is implemented as a shared, ESP-IDF-free component,
[`components/brake_fsm/`](../../components/brake_fsm), **hand-written in C and ported
from the tuned Python/JS reference** rather than generated from an SMC `.sm` model.
[§5](#5-firmware-moduletask-decomposition) explains that departure from the original
plan. It runs on-device today in the [`logger/`](../../logger) ride-validation build,
which drives a panel-mount LED from the FSM output, and is cross-checked against the
reference on every committed ride capture in CI
([§7](#7-isolation-acceptance)). It is **not yet wired into the transmitter**, which is
what would make this element 🟢.

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
window, not a raw sample-to-sample diff — CAN speed is quantized and noisy, and the
whole FSM hinges on a clean derivative.

**Resolved, as shipped** (the derivation lives in DE-08, `can_decode.c`):

1. wheel speed through a **single-pole low-pass**, `speed_smooth_ms` = **80 ms**;
2. **slope** of the filtered series over at least `CAN_DECODE_ACCEL_WINDOW_MS` =
   **200 ms**;
3. the slope **exponentially smoothed**, `CAN_DECODE_ACCEL_ALPHA` = **0.3**.

Both the low-pass and the correctly-sized history ring were missing from the firmware's
first implementation and are recorded as fixed in
[`de-08-can-decode.md §3b`](de-08-can-decode.md#3b-derived-acceleration--two-fixes-that-the-fsm-depends-on).
They are not cosmetic: on the 40 mph ride log the FSM's on-time goes from **53.3 s**
(both missing) to **90.6 s** (both present) against a reference of **89.0 s**, with the
ring fix alone over-shooting to 37 transitions where the reference has 29. The
thresholds below were tuned *with* this derivation and mean nothing without it.

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

### 5a. Architecture decision — hand-written C, ported from the tuned reference

**This section previously specified an SMC ([State Machine Compiler]) `.sm` model
compiled to C by a CMake pre-build step, with the `.sm` as the single source of truth
for the transition structure. That is not what shipped, and this is the reasoning
rather than a quiet contradiction.**

[State Machine Compiler]: https://smc.sourceforge.net/

What shipped: [`components/brake_fsm/brake_fsm.[ch]`](../../components/brake_fsm) —
~150 lines of hand-written, dependency-free C, a **port of the already-tuned**
`run_fsm()` in [`tools/trc_viz.py`](../../tools/trc_viz.py) (and the identical logic in
`tools/trc_viz.html`).

Three reasons, in order of weight:

1. **The reference is the source of truth, and it is not a `.sm` file.** By the time
   this element was built, the transition structure *and every threshold* had already
   been calibrated against real ride logs in the Python/JS visualizer — including the
   rule-6a hysteresis and the 120 ms rule-1 debounce whose rationale is recorded in
   [§4](#4-state-machine). Introducing an `.sm` model would create a **second**
   authority for the same logic, with the tuned one outside it. Porting the validated
   implementation and then asserting equivalence in CI
   ([§7](#7-isolation-acceptance)) keeps exactly one authority.
2. **SMC would put a JRE in the build and in CI.** `Smc.jar` is a Java tool; the
   pre-build step needs `java` on every build host and in the GitHub Actions
   containers, including the ESP-IDF CI image. Nothing else in this repo needs a JVM.
   That is a real, permanent toolchain dependency to carry for a three-state machine.
3. **The structure is small enough that generation buys little.** Three states, one
   `Poll` event, eight guards. The hand-written version is a single `switch` whose arms
   read in the same order as the [§4](#4-state-machine) table, and it is fully covered
   by the replay test. The generated-code benefits SMC offers — exhaustive transition
   dispatch, a state pattern — do not pay for themselves at this size.

What is **kept** from the original plan, because it was the valuable part:

- The machine is still a **pure function of (signals, dt)** with no platform
  dependencies — no ESP-IDF headers, no FreeRTOS, no clock of its own. That is what
  makes it host-testable and shareable between the logger and the transmitter.
- The **guard predicates** are still explicit and first-match-wins, in the §4 order.
- The **anti-strobe floor** is still global and outside the per-rule logic.
- The Graphviz diagram SMC would have emitted is replaced by the ASCII diagram in
  [§4](#4-state-machine) plus the transition table, which is what readers actually use.

What is **lost**: no machine-checked guarantee that the C `switch` matches a declarative
model. The replay test ([§7](#7-isolation-acceptance)) covers this differently and, for
this machine, more usefully — it checks the behaviour against the implementation the
thresholds were tuned on, over 29 minutes of real riding. If the transition structure
ever grows past what one `switch` reads cleanly, revisit this.

> ⚠️ [`firmware.md §4`](../firmware.md#4-build--toolchain) still carries the SMC
> pre-build recipe. It is superseded by this section for DE-09.

### 5b. Modules as built

- **`components/brake_fsm/brake_fsm.[ch]`** — pure, host-testable. Holds the state, the
  three condition hold timers (`decel_hold`, `steady_hold`, `stopped_hold`), the
  anti-strobe `since_trans`, and a `brake_fsm_tunables_t` of the §4 thresholds.
  `brake_fsm_step(f, in, dt_ms)` advances one tick and reports whether the state
  changed and which rule fired. Three details are load-bearing and are commented as
  such in the header, because the reference's behaviour depends on them:
  1. the hold timers accumulate **before** the guards are evaluated, so a hold that
     reaches its threshold on a tick fires on that tick;
  2. on any transition **all three** hold timers reset, not just the one that fired;
  3. `since_trans` starts large, so the first tick is never blocked by the anti-strobe
     floor.
- **`components/brake_fsm/Kconfig`** — the tunables, as integer milli-units (Kconfig has
  no float type), converted to float once at init by the app glue. Shared by the logger
  and, later, the transmitter.
- **Validity is the caller's business.** `brake_fsm` has no notion of staleness,
  matching the reference exactly. Firmware must not tick it with signals it does not
  trust; the logger holds the light off and resets the machine when wheel speed is
  invalid (`logger/software/main/fsm_preview.c`), which is the one place the firmware is
  deliberately more conservative than the reference.
- **Tick task (50 Hz)** — `fsm_preview.c` in the logger. Snapshots the decoder, steps
  the machine with the **nominal** 20 ms as `dt` (a measured `dt` would inject
  scheduler jitter into the debounce windows), drives the output, and logs every
  transition with its rule number and the decisive signal values.
- **Output mapping** — `BRAKING` and `STOPPED` both render as light-on (`ST_BRAKE`);
  `OFF` renders off. A steady level, never a blink.

## 6. CLI hooks
- `sig set wheel <mph>`, `sig set clutch <0|1|na>`, `sig set gear <n|N>`,
  `sig set throttle`/`rpm` (for completeness); `sig source fake`; `state show`
  (current state + active timers + derived `accel_mphps`); `state force` (override for
  downstream tests). See [`cli.md`](../cli.md).

## 6a. Shipped default tunables

All of these are **Kconfig-settable** (`components/brake_fsm/Kconfig`, menu *Braking
state machine (DE-09)*) so they can be retuned without touching code. Kconfig has no
float type, so thresholds are stored in **milli-units** and converted once at init.
The defaults are the values the Python/JS reference was tuned to on the DE-07 ride logs
(`BrakeTunables` in `tools/trc_viz.py`).

| Tunable | Default | Rule | Kconfig symbol |
|---------|--------:|------|----------------|
| `decel_on_mphps` | 2.0 mph/s | 1 | `BRAKE_FSM_DECEL_ON_MMPHPS` = 2000 |
| `decel_on_debounce_ms` | 120 ms | 1 | `BRAKE_FSM_DECEL_ON_DEBOUNCE_MS` |
| `stop_speed_mph` | 1.0 mph | 2, 3, 6b | `BRAKE_FSM_STOP_SPEED_MMPH` = 1000 |
| `moving_speed_mph` | 3.0 mph | 6a | `BRAKE_FSM_MOVING_SPEED_MMPH` = 3000 |
| `accel_off_mphps` | 0.5 mph/s | 4 | `BRAKE_FSM_ACCEL_OFF_MMPHPS` = 500 |
| `accel_off_min_speed_mph` | 5.0 mph | 4 | `BRAKE_FSM_ACCEL_OFF_MIN_SPEED_MMPH` = 5000 |
| `steady_band_mphps` | 0.75 mph/s | 5 | `BRAKE_FSM_STEADY_BAND_MMPHPS` = 750 |
| `steady_timeout_ms` | 1500 ms | 5 | `BRAKE_FSM_STEADY_TIMEOUT_MS` |
| `stop_timeout_ms` | 60000 ms | 6c | `BRAKE_FSM_STOP_TIMEOUT_MS` |
| `state_min_dwell_ms` | 250 ms | anti-strobe | `BRAKE_FSM_STATE_MIN_DWELL_MS` |
| `speed_smooth_ms` | 80 ms | (DE-08 accel path) | `BRAKE_FSM_SPEED_SMOOTH_MS` |

`speed_smooth_ms` is strictly a decode-path value — the wheel-speed low-pass ahead of
the slope ([§3](#3-inputs--derived-signals)) — but it was calibrated together with the
thresholds and belongs to the same tuning set, so it shares the menu and is applied to
the decoder by the app glue.

---

## 7. Isolation acceptance

### 7a. Host replay against the tuned reference (automated, in CI)

[`tools/fsm_check.py`](../../tools/fsm_check.py) replays each committed ride capture in
[`logger/`](../../logger) through the **real** C decode (`components/chmbl_can`) and the
**real** C FSM (`components/brake_fsm`), stepped on the same 50 Hz grid the firmware
ticks, and compares the result against the Python reference. The harness is
`transmitter/software/test_host/fsm_replay.c`; the CI job is `brake-fsm-replay`.

Measured agreement (C vs. reference, shipped defaults):

| Capture | Duration | Transitions (C / ref) | Light on (C / ref) | Δ on-time |
|---------|---------:|----------------------:|-------------------:|----------:|
| `40mph_drive_cycle.trc` | 220 s | 29 / 29 | 90.6 s / 89.0 s | +1.8% |
| `20260709-1.trc` | 449 s | 27 / 27 | 112.7 s / 113.7 s | −0.9% |
| `20260709-2.trc` | 637 s | 53 / 53 | 191.7 s / 191.7 s | −0.0% |
| `20260709-3.trc` | 483 s | 49 / 47 | 169.3 s / 168.4 s | +0.5% |

**Tolerance: transitions within 2, on-time within max(1.0 s, 3%).** An exact match is
not achievable and the reasons are properties of the firmware, not defects: the decoder
runs on a `uint32_t` millisecond clock where the reference reads microsecond `.trc`
timestamps, the firmware derives acceleration in `float` where numpy uses `float64`, and
wheel speed is quantized to ~0.039 mph — one quantum inside a 200 ms window is
~0.19 mph/s, a tenth of the rule-1 threshold. Near a guard boundary the two can land on
opposite sides.

The only material divergence is on `20260709-3.trc` at t≈395 s, and it was investigated
rather than tolerated: a marginal decel event holds past 2.0 mph/s for **five** 20 ms
ticks in the reference (100 ms, rejected by the 120 ms debounce) and **six** in the C
path (120 ms, accepted), producing one extra 0.9 s `BRAKING` episode. Same event,
opposite sides of one tick. Every other transition in all four captures matches 1:1 in
order, rule and — within 0–40 ms — time.

The tolerance has teeth: with the two DE-08 decode bugs this element uncovered still
present, the same harness reports **12** transitions and **53.3 s** of on-time against
29 and 89.0 s — 20x the tolerance. See the `fsm_check.py` docstring.

### 7b. On-board preview on a real ride (manual)

The [`logger/`](../../logger) ride-validation build runs this FSM on the bike and drives
the remote LED (J4 pin 2) from its output, logging every transition to the console with
its rule number and signal values. That makes the whole element observable in the field
without the radio or the rider-side unit existing: ride, watch the LED, then review the
`.trc` and the console log together.

**Not yet performed** — the board has had no hardware bring-up past
[`BRINGUP.md §4`](../../logger/hardware/BRINGUP.md).

### 7c. Synthetic isolation cases (CLI, on the transmitter — not yet built)

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
- **Wire the FSM into the transmitter.** The component is shared and ready
  (`EXTRA_COMPONENT_DIRS` already points both apps at `components/`); the transmitter
  needs the 50 Hz tick task, the `state show` / `state force` CLI hooks
  ([§6](#6-cli-hooks)) and the `ST_BRAKE` emission on the ESP-NOW link. That is what
  takes this element to 🟢.
- **Ride-validate the preview on hardware.** [§7b](#7b-on-board-preview-on-a-real-ride-manual)
  is built but unperformed — the logger PCB has no bring-up past
  [`BRINGUP.md §4`](../../logger/hardware/BRINGUP.md).
- ~~Final tunable values and the acceleration smoothing window/filter (calibrate on
  DE-07 wheel-speed ride logs).~~ **Resolved for now:** the shipped defaults are in
  [§6a](#6a-shipped-default-tunables), tuned on the DE-07 logs in the Python/JS
  reference and reproduced by the C implementation within tolerance
  ([§7a](#7a-host-replay-against-the-tuned-reference-automated-in-ci)). The derivation
  (80 ms LPF → 200 ms slope → α 0.3) is settled in
  [§3](#3-inputs--derived-signals). All Kconfig-settable, so more logs can move them
  without a code change.
- ~~Confirm `gear`/`neutral` and `wheel_speed` are actually decodable on the reference
  bus (presumed available because the cluster displays them — verify in DE-07).~~
  **Resolved:** all four FSM inputs decode on the reference bus and are exercised by
  the replay test over 29 minutes of real riding (gears 0–6 seen, clutch and the
  rule-6b launch guard both firing). See
  [`can-profiles.md §5`](../can-profiles.md#5-reference-target--triumph-speed-400-tr-series-platform).
- ~~Stop-and-go flicker policy (a `STOPPED`→`OFF` hold/hysteresis vs. the literal
  rules).~~ **Resolved:** added `moving_speed_mph` hysteresis on the `STOPPED` exit +
  a rolling qualifier on the launch guard (see [§4 note](#4-state-machine)); validated on
  the DE-07 ride log (transitions 162 → 48). 3.0 mph now holds across all four committed
  captures without creep-strobing.
- Whether to ever use the reserved `ST_DECEL` tier for a softer coasting cue.
