---
name: chmbl-proof-and-analysis-toolkit
description: Load this skill when you must PROVE something in open-chmbl rather than assume it — promoting a candidate CAN signal decode to "confirmed", deriving a clean acceleration from quantized wheel speed (and sizing the history ring), choosing between hysteresis and rate-limiting for an oscillating state machine, picking a debounce value from data, validating a decoder against an independent implementation, or doing latency/power budget arithmetic honestly. Each recipe is a first-principles method with a worked example from this repo's history. Do NOT load it for how to run the tools mechanically (chmbl-diagnostics-and-tooling), for what counts as citable evidence and CI gates (chmbl-validation-and-qa), for the experiment-lifecycle process (chmbl-research-methodology), for executing the DE-09 implementation (chmbl-de09-campaign), or for the settled history of past investigations (chmbl-failure-archaeology).
---

# Proof and analysis toolkit: "prove it, don't just install it"

open-chmbl infers motorcycle braking from CAN-bus wheel speed and lights a helmet LED
bar. Every load-bearing number in this project — a signal decode, a filter window, a
threshold, a latency claim — was *proven* against data or arithmetic, not eyeballed.
This skill is the method library: six recipes, each with the worked example from this
repo's own history, so you can run the same kind of proof on the next signal, the next
bike, or the next tunable.

Jargon (defined once): **CAN** = Controller Area Network, the shared broadcast bus
vehicle ECUs talk on; a **frame** is one broadcast message (ID + up to 8 data bytes).
**DBC** = a text database describing how signals are bit-packed into frames. **.trc** =
PCAN-format text log of timestamped raw CAN frames (this repo's capture format).
**FSM** = finite state machine. **EWMA/EMA** = exponentially-weighted moving average
(`y += alpha * (x - y)`). **LSB** = least-significant bit; the **quantization step** is
the physical value of one LSB. **ESP-NOW** = the low-latency 2.4 GHz radio link between
bike and helmet.

The single committed data corpus everything below replays is
`logger/40mph_drive_cycle.trc` (~220 s Triumph Speed 400 ride, 315,882 lines). The
bench captures `logger/throttle.trc` and `logger/wheel.trc` are cited by
`docs/can-profiles.md` but are **not committed** (verified absent as of 2026-07-08) —
their findings are documented history you can cite, not evidence you can re-run.

Every recipe ends the same way: **write the result up per `chmbl-research-methodology`**
(prediction stated before the run, capture + number cited, refutation attempted), and if
it changes a doc-of-record number, land the doc edit in the same change
(`chmbl-change-control`).

---

## Recipe 1 — CAN signal identification proof

**When to reach for it:** you have a *candidate* decode — "I think CAN ID X, bytes Y,
scale Z is signal S" — from staring at a capture, and you need to promote it to
**confirmed** in the decode table (`docs/can-profiles.md` §5). Also when porting the
profile to a new bike.

A candidate becomes confirmed only when it survives **all four** proof classes below.
One correlation is a hypothesis; the decode table is built from signals that passed
every class that applies.

### Steps

1. **Single-stimulus bench isolation.** Capture with exactly ONE physical input
   changing (bike on a stand, key on) and everything else held still. The candidate
   field must be *the only thing that moves*, and it must move *when the stimulus
   moves*. A field that co-moves with three others in a ride capture proves nothing —
   isolation is what breaks the correlation ambiguity.
2. **Physical-consistency cross-checks.** Decode the candidate with its proposed scale
   and check it against physics/known ground truth it has no reason to satisfy by
   accident:
   - *Envelope calibration*: does the decoded magnitude match what you independently
     know happened (speeds you actually rode, idle/redline rpm)?
   - *Ratio invariants*: quantities linked by mechanism must show the mechanism
     (rpm/wheel-speed constant within a gear; the steps between gears are the gearbox).
3. **Negative evidence.** The signal must be *absent/inactive in captures where the
   event did not happen*. A "kill switch" bit that also appears in no-kill rides is
   decoding something else.
4. **Counterexample hunt (adversarial step — do not skip).** Actively try to kill your
   own hypothesis: scan the *whole* corpus for moments where the candidate behaves in a
   way the hypothesis forbids. One clean counterexample outranks any amount of
   confirmation. Record killed candidates in the decode notes — they save the next
   person the same dead end.

### Worked example (the Speed 400 decode, `docs/can-profiles.md` §5 decode notes)

| Proof class | What was done |
|---|---|
| Isolation | `wheel_speed` (0x102 B1–B2, big-endian 16-bit): the **only field that moves in `wheel.trc`** (bench capture, front wheel spun on the stand). |
| Envelope | At `raw/16` km/h the ride sustains **~30 mph** early and peaks at **43 mph at ~74 % of the powered window** — matching the known ride. |
| Ratio invariant | rpm candidate 0x140 B6: its ratio to wheel speed is **constant within each gear**, with clean gearbox steps **≈ 0.27 / 0.19 / 0.14 / 0.11** — that's a gearbox, so this is engine speed and 0x102 is road speed, mutually confirming. It also shows cranking at start and reads 0 with the engine off. |
| Negative evidence | Engine-cutoff (0x121 B3 bit 6 + reason B6 = 0x28) asserts ~30 ms before live rpm decays to 0 — and **never appears in a capture without a kill**. |
| Counterexample kills | **0x145 B5 "throttle"**: swept 0–255 on the bench exactly like 0x140 B0 — but in the ride it dithers wildly 0–100 % (a fast control/dither channel). Bench isolation alone would have confirmed the wrong byte; the ride corpus killed it. **0x121/0x113 "side stand"**: a lamp-cluster candidate that instead fires the first time the bike exceeds ~10 km/h — the warning-lamp self-check clearing, not the stand. The real side stand is 0x481 B7 bit 0 (0 in every bench capture, bike on its stand — negative evidence agreeing). |

### Write-up

Add the signal as a decode-table row `(can_id, bytes/bits, scale/offset, notes)` in
`docs/can-profiles.md`, with a decode-notes bullet naming *which proof classes* it
passed and *which candidates were killed*. Then make it machine truth: add it to
`profiles/triumph_tr.dbc`, regenerate the profile with `tools/gen_profile.py`, and let
Recipe 5 (differential validation) confirm the C decode. Scales calibrated only by
envelope stay flagged "approximate" (the decode table does this — copy its caution
block style).

---

## Recipe 2 — Derivative of a quantized signal

**When to reach for it:** you need `d(signal)/dt` from a CAN signal (the braking FSM
runs entirely on `d(wheel_speed)/dt`), or you're changing anything in the acceleration
path (`accel_update` in `transmitter/software/main/can_decode.c`, or its Python port in
`tools/trc_viz.py`).

### Why raw sample-to-sample diff is unusable — do this arithmetic first

Wheel speed is quantized: `raw/16` km/h → one LSB = 0.0625 km/h ≈ **0.039 mph**.
Frames arrive ~every 10 ms (measured: mean 0x102 spacing **10.35 ms** over 21,254
frames in the committed capture — re-verify with the awk one-liner in Provenance).
A raw adjacent-sample diff turns a *single LSB step* into

```
0.039 mph / 0.010 s ≈ 3.9 mph/s
```

— larger than the entire `DECEL_ON_MPHPS = 3.0` trigger threshold. At constant true
speed the decoded value ticks up and down by LSBs, so the raw derivative is threshold-
crossing noise by construction. **Always compute the quantization-step-over-sample-
interval number before designing the estimator**; it tells you the minimum smoothing
you need.

### The estimator actually used (read `accel_update` in `can_decode.c`)

1. Each wheel-speed sample (mph) is pushed into a ring buffer of `(value, t_ms)` pairs.
2. **Slope over a window**: scan from newest backwards for the first (i.e. youngest)
   sample at least `CAN_DECODE_ACCEL_WINDOW_MS = 200` ms old; slope =
   `(newest − that) / dt`. Over 200 ms, ±1 LSB of quantization error contributes only
   ±0.039/0.2 ≈ **±0.2 mph/s** — comfortably under the 3.0 mph/s threshold.
3. **EWMA on the slope**: first value primes, then
   `accel += CAN_DECODE_ACCEL_ALPHA (0.3) * (slope − accel)`.
4. If no sample in the ring is ≥ 200 ms old, the function **returns without updating**
   — accel silently holds its previous value. This is the failure mode that bites (see
   the bug below).

The Python port (`_compute_accel` in `tools/trc_viz.py`) is line-faithful, and adds one
more stage in front: `_smooth_speed`, a dt-aware causal EMA low-pass on the raw speed
(`speed_smooth_ms = 80` ms tau, a live tunable) applied *before* the slope, added in
commit `6930e19` specifically so quantization steps and single-sample glitches don't
inject decel spikes.

### The ring-buffer sizing law — and the live bug as the worked failure

**Law: `HIST_samples ≥ window_ms × frame_rate_Hz / 1000`, plus margin** (the window
lookup needs a sample *older* than the window, so spanning it exactly is not enough).

The firmware violates it, as of 2026-07-08 (fix reserved for the DE-09 campaign —
`chmbl-de09-campaign`):

```
CAN_DECODE_SPEED_HIST = 16 samples          (can_decode.h)
wheel-speed frame spacing ≈ 10.35 ms        (~97 Hz, measured from the capture)
ring span = 15 intervals × 10.35 ms ≈ 155 ms  <  200 ms = CAN_DECODE_ACCEL_WINDOW_MS
```

The "youngest sample ≥ 200 ms old" search almost never succeeds, so the derived
acceleration **freezes** — it only updates on rare >200 ms frame gaps. Required:
`0.2 s × 97 Hz ≈ 20` samples minimum; `trc_viz.py` uses `SPEED_HIST = 32`
(~320 ms span, comfortable margin) and documents the bug in its header comment. The
discovery trail is commits `f44b0ed` (frozen accel in the tool) → `bca0d14` →
`6930e19`; see `chmbl-failure-archaeology` for the narrative.

### Latency cost of each smoothing stage — account for all of them

| Stage | Parameter | Latency it adds to a braking onset |
|---|---|---|
| Slope over window | 200 ms window | ≈ window/2 ≈ **100 ms** effective group delay (the slope reports the *average* decel over the window, so a step change reads half-strength at half a window) |
| EWMA on slope | alpha 0.3 @ ~10 ms/sample | time constant ≈ dt·(1−α)/α ≈ **~25 ms** |
| Speed pre-LPF (tool only) | tau 80 ms | **~80 ms** first-order lag on the speed feeding the slope |

These are derived arithmetic, not repo-stated numbers — but they are exactly what
Recipe 6's budget audit must include. Smoothing is never free; every dB of noise
rejection is bought with milliseconds of brake-light delay. When tuning, state the
latency price of the change alongside the noise benefit.

### Write-up

Report: quantization step, frame rate, window, resulting worst-case quantization noise
in the derivative, ring size vs the sizing law, and total added latency. Validate on
the committed capture via `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc
--headless-check` (prints the accel range; it must span realistic braking, roughly
−10..+10 mph/s, not sit frozen near 0).

---

## Recipe 3 — Hysteresis vs rate-limiting

**When to reach for it:** a state machine chatters — rapid transitions back and forth
— and you must decide between (a) **hysteresis** (a two-threshold gap: enter below X,
exit only above Y > X) and (b) **rate-limiting** (a minimum dwell time between
transitions).

### The discriminating question

*Is the underlying guard condition genuinely oscillating across a single threshold?*

- If YES → **hysteresis is the correct fix.** A dwell floor cannot fix it: the guard
  re-fires the instant the dwell expires, so rate-limiting merely slows the strobe to
  the dwell period — the oscillation is still there, just clocked. You must separate
  the enter and exit conditions so the noisy quantity has to travel a real distance to
  re-trigger.
- If NO — the guard fires once per real event but *spuriously* on short transients →
  that's Recipe 4 (debounce), or a dwell floor if the goal is only to bound the minimum
  output pulse width (anti-strobe).

Both mechanisms can coexist with different jobs: in DE-09, `MOVING_SPEED_MPH` is
hysteresis (correctness) while `STATE_MIN_DWELL_MS = 150` is the global anti-strobe
floor (a safety/legal output constraint — flashing patterns are fenced off), not a
chatter fix.

### Steps

1. Reproduce the chatter on a replay and **count transitions** (and sub-0.5 s output
   blips) — `run_fsm` + `fsm_stats` in `tools/trc_viz.py` do exactly this.
2. Inspect the guard quantity around the chatter: is it dwelling *at* the threshold
   (oscillation → hysteresis) or spiking *through* it (transient → debounce)?
3. Size the hysteresis gap wider than the observed oscillation band of the quantity,
   narrower than the smallest real event you must still catch.
4. Re-run the replay; report transitions/blips before → after, and confirm on-time is
   ~unchanged (you removed chatter, not real events).

### Worked example (DE-09's stop/creep strobe — `docs/design/de-09-brake-decel-logic.md`, `docs/firmware.md`)

`STOPPED` originally entered *and* exited at one speed threshold. Low-speed creep in
stop-and-go traffic straddles that single threshold, so the guards **genuinely
oscillate** and the light strobes. Fix: enter `STOPPED` at `speed < STOP_SPEED_MPH`
(1.0) but exit for motion only above `MOVING_SPEED_MPH` (3.0), and require the launch
guard (clutch released, in gear) to also see `speed > STOP_SPEED_MPH` so it can't
ping-pong against stop-entry while stationary. On the 40 mph ride log the gap cut FSM
transitions **162 → 48** and sub-0.5 s light blips **65 → 8**, turning the two creep
zones into solid holds. The docs state the argument explicitly: "the underlying guards
genuinely oscillate, so the fix is hysteresis, not just rate-limiting" — the dwell
alone could never have produced that result.

### Write-up

The evidence sentence must contain the mechanism diagnosis ("guards oscillate at the
threshold"), the gap chosen and why that width, and capture + counts before → after
(the 162→48 sentence in `docs/firmware.md` is the house exemplar — see
`chmbl-validation-and-qa` §1).

---

## Recipe 4 — Debounce knee-finding

**When to reach for it:** a trigger fires spuriously on *short transients* (not
sustained oscillation — that's Recipe 3), and you must pick the debounce duration —
"condition must hold continuously for T ms" — from data rather than vibes.

### Steps

1. **Define the two competing counts** on a replay of a real capture:
   - *blips removed*: spurious short activations eliminated (good),
   - *real events clipped*: genuine events that no longer fire, or fire late enough to
     matter (bad). Also track total on-time — it should barely move if you're removing
     only junk.
2. **Sweep T** across a sensible range (the DE-09 bench exposes this directly:
   `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check
   --decel-on-debounce-ms <T>` prints transitions and on-time per run; every
   `BrakeTunables` field is a CLI flag).
3. **Find the knee**: the T where blips-removed has flattened (diminishing returns)
   and real-event clipping is about to begin. Below the knee you're leaving free noise
   rejection on the table; above it you're paying real events (and pure latency — a
   debounce delays *every* genuine trigger by up to T) for nothing.
4. Sanity-check the latency price: T adds directly to worst-case trigger latency
   (Recipe 6).

### Worked example (the 120 ms decel-on debounce — `docs/firmware.md`, DE-09 §"Decel-on debounce")

After the hysteresis fix (Recipe 3), the remaining chatter was momentary throttle-off
dips spiking the wheel-speed derivative past `DECEL_ON_MPHPS` for a tick or two. On
the 40 mph ride log, requiring the decel to hold **120 ms** removed the remaining
sub-0.5 s `BRAKING` blips (**8 → 3**, total transitions **48 → 30**) with on-time
essentially unchanged; **larger debounce values started clipping real short brake
taps** — so 120 ms is the knee. The docs are explicit that this is "a debounce, not a
low-pass filter": a heavier LPF would suppress the same spikes only by delaying
*genuine hard-braking onset*, an unbounded and signal-dependent cost, whereas a
debounce's cost is bounded and predictable (≤ 120 ms, always).

### Write-up

Report the sweep as a small table (T vs blips vs transitions vs on-time), name the
knee, state the two failure directions you balanced, and state the latency cost
sentence ("sustained decel still fires, ≤ T later"). Threshold discipline (predict
before sweeping, never move the bar post-hoc) is in `chmbl-validation-and-qa` §5.

---

## Recipe 5 — Differential decoder validation (the golden-test method)

**When to reach for it:** you have two independent implementations of one spec and a
real corpus — in this repo: the hand-written C bit extractor
(`transmitter/software/main/can_decode.c`) and python-cantools, both decoding
`profiles/triumph_tr.dbc` over `logger/40mph_drive_cycle.trc`. Use it whenever you
touch the C extractor, the DBC, the generated profile, or add a signal/bike.

### Why this catches what unit tests miss

Unit tests check the cases you thought of — and the person who wrote the buggy
Motorola "sawtooth" bit-walk writes the test with the same mental model, so the test
agrees with the bug. Differential testing breaks that: cantools' unpacking was written
by strangers against the same DBC spec, and a 315k-frame real corpus exercises byte
alignments, sign bits, DLC edge cases, and value ranges nobody enumerates by hand.
Bit-order (Intel vs Motorola start-bit numbering), sign extension, and scale/offset
transcription errors all produce *systematic* disagreement over a corpus — impossible
to miss, trivial to miss in three hand-picked unit vectors. This is the DE-08
architecture decision (rationale: `docs/design/de-08-can-decode.md` §3a): keep a tiny
data-driven C decoder while *inheriting cantools' bit-unpacking correctness*.

### Steps (the method, generalizable)

1. **One spec, two implementations** that share no code: here, the DBC is the spec;
   `trc_replay` (host-built from the *real firmware sources*) is impl A, cantools is
   impl B.
2. **Replay one real corpus through both**, emitting `(frame#, signal, value)` rows.
3. **Assert row-for-row agreement** within stated float tolerances — this repo uses
   `REL_TOL = 1e-4` / `ABS_TOL = 1e-3` (top of `tools/golden_check.py`) — and also
   flag MISSING rows (A silent where B decoded) and EXTRA rows (A decoded where B
   didn't): silence bugs are bugs.
4. **Compare derived predicates too**, computed independently on each side
   (`clutch_pulled`, `engine_cutoff` in `golden_check.py`) — the boolean derivations
   can be wrong even when the raw fields agree.
5. Any disagreement: the *spec* (DBC) arbitrates which side is wrong; fix that side,
   never widen tolerance to make it pass (baseline changes go through
   `chmbl-change-control`).

### Worked example (run it — this is CI's `can-decode-golden` job)

```bash
cmake -S transmitter/software/test_host -B transmitter/software/test_host/build -DCMAKE_BUILD_TYPE=Release
cmake --build transmitter/software/test_host/build
pip install -r tools/requirements.txt
python3 tools/golden_check.py     # PASS: <N> signal values identical ... / exit 1 on any mismatch
```

`golden_check.py` prints `MISMATCH frame <n> <sig>: C=<a> cantools=<b>` per
disagreement (aborts after 20) and exits non-zero — it *is* a gate, unlike
`trc_viz --headless-check`. Extending it to a new capture or signal:
`--trc/--dbc/--harness` flags, plus the CI-wiring steps in `chmbl-validation-and-qa`
§4a. Tool-output interpretation detail: `chmbl-diagnostics-and-tooling`.

### Write-up

"Validated" wording requires this run's PASS line plus the capture name — see the
evidence hierarchy in `chmbl-validation-and-qa` §1.

---

## Recipe 6 — Budget arithmetic (latency and power)

**When to reach for it:** any claim of the form "fits in the budget" — end-to-end
brake-to-light latency, parked current draw — and any change that adds a delay or a
load. The method: decompose the budget into named stages, bound each stage worst-case,
add honestly, and **flag the tension you find instead of rounding it away**.

### 6a. The ≤ 100 ms latency budget vs the 120 ms debounce

`docs/protocol.md` §5 states the budget:

```
CAN frame → decode → state machine   ≤ ~25 ms (incl. 50 Hz tick + accel smoothing)
ESP-NOW hop                          ≈ 2–5 ms
RX render → LED update               ≤ ~16 ms (60 Hz)
Brake event → LED                    ≤ ~100 ms target
```

Now do the honest worst-case for *braking onset → light on* with the DE-09 design
(each row verified against `docs/firmware.md` / `can_decode.h` / `docs/protocol.md`;
the per-stage latency models are derived arithmetic as in Recipe 2, not repo-stated):

| Stage | Worst-case | Source |
|---|---|---|
| Accel slope group delay | ~100 ms | 200 ms window (`CAN_DECODE_ACCEL_WINDOW_MS`), step reads ~half-strength at half a window |
| EWMA on slope | ~25 ms | alpha 0.3 at ~10 ms/sample |
| Decel-on debounce | **120 ms** | `DECEL_ON_DEBOUNCE_MS` — decel must hold continuously this long |
| FSM tick | up to 20 ms | 50 Hz poll |
| Anti-strobe dwell | 0–150 ms | only if a transition occurred < 150 ms ago; usually 0 for OFF→BRAKING |
| Next heartbeat | up to 50 ms | TX broadcasts at 20–50 Hz; state rides the next tick |
| ESP-NOW hop | 2–5 ms | protocol.md §5 |
| LED render | ≤ 16 ms | protocol.md §5 |

Sum (excluding the dwell case): **~330 ms** typical worst-case from brake-onset to
photons; the 120 ms debounce *alone* exceeds the ≤ 100 ms line. **This is a real,
unreconciled tension in the docs of record** (as of 2026-07-08): protocol.md §5's
"≤ ~25 ms incl. accel smoothing" cannot contain a 200 ms slope window plus a 120 ms
hold. The two readings that reconcile it: (a) the 100 ms budget covers only the
*pipeline after the FSM decides* (decode→radio→render, which sums to ~45–90 ms and
does fit), while detection latency is governed separately by the debounce trade
(firmware.md's "sustained decel still fires, just ≤ 120 ms later" prices only the
debounce, not the estimator); or (b) the budget predates the DE-09 debounce design and
needs updating. Do not silently pick one — catching exactly this kind of
budget-vs-mechanism drift is what this recipe is for. If your work touches either
number, surface the conflict and route the doc reconciliation through
`chmbl-change-control` (docs-are-spec).

The method in general: *include every hold, debounce, dwell, filter group delay, tick
quantization, and transmission period in the chain — the mechanisms designed to reject
noise are latency, and they are the ones budget tables forget.*

### 6b. The < 1 mA parked-draw budget

Repo facts: the transmitter may sit on constant 12 V at the diagnostic port; target
parked draw **< 1 mA** so it cannot drain the motorcycle battery
(`docs/hardware.md` §"Power & parasitic draw", `docs/safety-regulatory.md`, FFL
TX-PWR-5). The design response: deep sleep with the CAN transceiver disabled, wake on
bus activity or ignition sense, ideally cutting the buck converter's load.

Shape of the decomposition (per-line figures below are **background knowledge about
typical parts, not repo-measured facts** — measure your actual board):

| Load while parked | Typical order of magnitude |
|---|---|
| ESP32-C3 deep sleep | ~5 µA class |
| Buck regulator quiescent (always across the battery) | tens of µA to >1 mA — **this line item usually decides pass/fail**; a "wide-Vin automotive buck" chosen for run-mode efficiency can blow the whole budget at no load |
| CAN transceiver standby (SN65HVD230 silent/standby) | tens to hundreds of µA |
| Input protection (TVS leakage, divider for ignition sense) | µA — but a resistive 12 V sense divider is a *permanent* load; size it in |

Method: list every element electrically connected to battery 12 V while parked, bound
each at its worst-case quiescent current, sum, compare to 1000 µA, and identify the
dominant term — then attack that term, not the µA-level ones. Sanity anchor: 1 mA
continuous ≈ 0.72 Ah/month against a motorcycle battery of a few Ah — the budget is
not paranoia. No parked-draw measurement exists in-repo yet (as of 2026-07-08); a
claim of meeting TX-PWR-5 will require a bench measurement, not this arithmetic.

### Write-up

A budget audit is a table: stage, worst-case bound, source of the bound (doc, header
constant, datasheet, derived), sum, margin — and an explicit "tensions found" section,
even (especially) when the tension is with a doc of record.

---

## When NOT to use this skill

- Mechanics of running/interpreting `trc_viz`, `golden_check`, `gen_profile` →
  `chmbl-diagnostics-and-tooling`.
- What counts as citable evidence, CI gates, adding tests/captures →
  `chmbl-validation-and-qa`.
- The experiment lifecycle itself (predict-first, refutation protocol, retiring ideas)
  → `chmbl-research-methodology`; the recipes here are the *analysis methods* that
  produce the numbers that process consumes.
- Actually implementing DE-09 / fixing `CAN_DECODE_SPEED_HIST` → `chmbl-de09-campaign`.
- History of settled investigations and dead ends → `chmbl-failure-archaeology`.
- CAN/DBC/.trc fundamentals themselves → `chmbl-can-reference`.

## Provenance and maintenance

All facts verified against the repo (branch `claude/skill-library-continuity-mib7ua`)
on 2026-07-08. Re-verify before trusting:

| Fact class | Re-verification command |
|---|---|
| Decode table + decode notes (envelope 30/43 mph @74 %, gear ratios 0.27/0.19/0.14/0.11, 0x145 / 0x121-0x113 kills, kill-code negative evidence) | `sed -n '205,275p' docs/can-profiles.md` |
| Bench captures still missing | `ls logger/throttle.trc logger/wheel.trc` (expect "No such file") |
| SPEED_HIST=16, window 200 ms, alpha 0.3 | `grep -n "CAN_DECODE_" transmitter/software/main/can_decode.h` |
| accel_update algorithm (ring, ≥window lookup, prime-then-EMA, silent hold) | read `accel_update` in `transmitter/software/main/can_decode.c` |
| Wheel-speed frame spacing ~10.35 ms | `awk '$5=="102"{if(p!=""){s+=$2-p;n++};p=$2}END{print s/n}' logger/40mph_drive_cycle.trc` |
| trc_viz SPEED_HIST=32, speed_smooth 80 ms, tunable CLI flags, headless output | `sed -n '45,95p' tools/trc_viz.py` and `_smooth_speed`/`headless_check` |
| Hysteresis/debounce evidence numbers (162→48, 65→8, 48→30, 8→3, 120 ms knee) | `grep -n "162\|8 → 3\|8 → 3\|48 → 30" docs/firmware.md docs/design/de-09-brake-decel-logic.md` |
| FSM tunable defaults (3.0 / 120 / 1.0 / 3.0 / 0.5 / 5.0 / 0.5 / 2000 / 60000 / 150) | `sed -n '105,120p' docs/firmware.md` |
| Golden tolerances + fail behavior | `grep -n "TOL\|FAIL\|PASS" tools/golden_check.py` |
| Latency budget lines | `sed -n '80,92p' docs/protocol.md` |
| < 1 mA parked target | `grep -rn "1 mA" docs/hardware.md docs/safety-regulatory.md docs/feature-functions.md` |
| Cited commits exist (6930e19, f44b0ed, bca0d14) | `git log --oneline --all \| grep -E "6930e19\|f44b0ed\|bca0d14"` |
