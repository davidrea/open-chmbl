---
name: chmbl-research-frontier
description: Load when deciding WHAT to work on next in open-chmbl, when asked "what are the open problems", when planning a new campaign/ride/bench session, or when evaluating whether a proposed effort actually advances the project. Lists the ranked open problems toward a complete, buildable, legally-careful open product — for each, why the current state falls short, the repo asset to build on, the first three concrete steps, and a falsifiable milestone. Do NOT load for executing the DE-09 firmware work itself (chmbl-de09-campaign), for how to run tools or hardware (chmbl-run-and-operate, chmbl-diagnostics-and-tooling), for what counts as evidence (chmbl-validation-and-qa), or for re-litigating settled dead ends (chmbl-failure-archaeology).
---

# Research frontier: open problems toward a buildable open product

open-chmbl is an open-source helmet-mounted brake light: a bike-side ESP32-C3
"transmitter" reads a Triumph Speed 400's CAN bus (Controller Area Network — the
broadcast wire the bike's computers talk on) in strictly listen-only mode, infers
braking from wheel-speed-derived deceleration with a finite state machine (FSM),
and radios a brake/off state over ESP-NOW (Espressif's connectionless Wi-Fi-band
protocol) to a helmet-side battery-powered LED bar.

**"State of the art" for THIS project is not novelty.** The owner's definition:
a **complete, documented, legally-careful, buildable open product** — polish,
reproducibility, and validation over new ideas. So the frontier below is ranked by
**product-completion impact and dependency order**, and each item is tagged:

- **[COMPLETION]** — engineering completion: the design exists, the outcome is
  predictable, the work is building/validating it.
- **[RESEARCH]** — genuinely open: the answer is unknown and an experiment could
  come back negative.

Every problem entry gives: why the current state falls short → the specific asset
this repo already has → the first three concrete steps (real paths/commands) → a
falsifiable "you have a result when …" milestone.

Jargon used below: **DE** = design element (the project's unit of work,
`docs/design/`, status table in `docs/design/README.md` §3: 🔲 not started /
🟡 in design / 🟢 implemented); **`.trc`** = PCAN-format text log of timestamped
raw CAN frames; **DBC** = machine-readable description of how signals pack into
CAN frames; **FSM** = finite state machine; **SMC** = State Machine Compiler
(generates C from a `.sm` model); **CHMSL** = center high-mounted stop lamp (the
"third brake light" class this product targets photometrically); **NVS** =
ESP32 non-volatile storage.

Derivation and status of every item below verified against the repo **as of
2026-07-08** (branch `claude/skill-library-continuity-mib7ua`). Sources: the
phase plan and open-questions table in `docs/roadmap.md`, the 🔲 rows in
`docs/design/README.md` §3, and each DE doc's §8 open items.

---

## The frontier at a glance

| # | Problem | Type | Blocks | Status source |
|---|---------|------|--------|---------------|
| 1 | DE-09 braking FSM in firmware | COMPLETION | everything downstream (Phase 3) | `docs/design/README.md` §3: 🔲 |
| 2 | Multi-ride FSM validation corpus | RESEARCH | trusting the tunables | DE-09 §8: "confirm on more logs" |
| 3 | Scrambler 400 X profile validation | RESEARCH | Phase 2 exit ("one profile works on both 400s") | `docs/roadmap.md` Phase 2 |
| 4 | Hardware DEs: DE-02/04/05/06/10 | COMPLETION | a wearable product (Phase 4) | §3 table: 🔲/🟡 |
| 5 | End-to-end latency measurement | RESEARCH | the ≤100 ms claim | `ARCHITECTURE.md` (never measured) |
| 6 | Recommit missing bench captures | COMPLETION | full decode reproducibility | `docs/can-profiles.md` broken links |
| 7 | Reserved `ST_DECEL` soft-cue tier | RESEARCH | nothing (deliberate deferral) | `docs/protocol.md`, roadmap table |
| 8 | Runtime profile selection / Street Triple 765 | RESEARCH (stretch) | nothing (Phase 5) | `docs/roadmap.md` Phase 5 |

Items 1→5 are roughly sequential by dependency: firmware FSM (1) before latency
can be measured end-to-end (5); corpus (2) and Scrambler ride (3) need only the
existing logger and can run in parallel with anything.

---

## 1. DE-09: the braking FSM does not exist in firmware yet [COMPLETION]

The single highest-impact gap. The FSM is fully **designed**
(`docs/design/de-09-brake-decel-logic.md`, tunables table in `docs/firmware.md`)
and dry-run tuned offline in `tools/trc_viz.py`, but the transmitter firmware has
no state machine: `transmitter/software/state_machine/brake_fsm.sm` and the SMC
pre-build step described in `docs/firmware.md` §4 do not exist, and the firmware
carries a live bug (`transmitter/software/main/can_decode.h` sets
`CAN_DECODE_SPEED_HIST` to 16, which at ~100 Hz wheel-speed frames spans ~150 ms
— less than the 200 ms `CAN_DECODE_ACCEL_WINDOW_MS`, so the derived acceleration
freezes; `tools/trc_viz.py` uses 32 and documents the sizing law). Until DE-09
lands, the product's core claim — "infers braking" — is validated-offline-only.
**Do not plan this work from here:** the executable, decision-gated campaign with
expected numbers at every gate is the dedicated sibling skill
**`chmbl-de09-campaign`**. Load that.

**You have a result when:** the firmware FSM, fed the replayed 40 mph ride,
reproduces the offline transition/blip counts (30 transitions, 3 sub-0.5 s blips
at the documented tunables) — the campaign skill owns the exact gates.

---

## 2. Multi-ride FSM validation corpus [RESEARCH]

**Why the current state falls short.** Every tunable number the project trusts —
hysteresis cutting transitions 162 → 48, the 120 ms debounce knee cutting 48 → 30
and blips 8 → 3 — was tuned and validated on **one** capture:
`logger/40mph_drive_cycle.trc`, a single ~220 s ride (315,882 lines, and note its
own trailer records `dropped-frames: 11194` from the pre-fix logger firmware).
One ride cannot distinguish "correct tunables" from "tunables overfit to this
ride". DE-09 §8 says it explicitly: the 3.0 mph hysteresis value is "still to
confirm on more logs". This is real research: a stop-and-go ride could
legitimately falsify the current values.

**This repo's asset.** A complete capture-to-metric pipeline already works
end-to-end: the `logger/` firmware (ESP-WROVER-KIT + SN65HVD230, one-button
start/stop, each start opens a new `N.trc` — see `logger/software/README.md` and
`chmbl-run-and-operate`), the replay bench `tools/trc_viz.py` (every FSM tunable
is a CLI flag and a live slider; `--headless-check` prints duration, peak speed,
FSM transition count, and brake-on time), and the decoder golden test
`tools/golden_check.py`. Adding a ride to the corpus is: record, copy the `.trc`,
run two commands.

**First three steps (in this repo):**
1. Pre-declare the acceptance bounds *before* collecting data (the
   `chmbl-research-methodology` evidence bar): add a "ride corpus" table to
   `docs/design/de-09-brake-decel-logic.md` §8 (or a new section in
   `docs/can-profiles.md` §6) declaring, per ride class (stop-and-go / highway
   cruise / spirited), the expected transitions-per-minute range, max sub-0.5 s
   blip count, and "zero missed sustained-braking events" — with the 40 mph
   baseline row filled in from
   `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check`
   (note: it always exits 0 — you must read and record its numbers; it is a
   stats printer, not a gate).
2. Record ≥3 rides spanning the three classes with the existing logger
   (field procedure in `chmbl-run-and-operate`), copy `N.trc` files off the
   microSD, and commit them under `logger/` (the committed-capture home —
   `docs/can-profiles.md` line ~275 says `transmitter/software/captures/`, but
   that directory does not exist; committed captures actually live in `logger/`.
   Fix the doc or the location in the same change, per `chmbl-change-control`).
3. Replay each new ride at the frozen documented tunables — e.g.
   `uv run tools/trc_viz.py logger/<ride>.trc --headless-check --decel-on-mphps 3.0`
   (flag names mirror the `BrakeTunables` fields in `tools/trc_viz.py`; run
   `uv run tools/trc_viz.py --help` for the generated list) — and record the
   numbers against the pre-declared bounds. Decode sanity per ride:
   `python3 tools/golden_check.py --trc logger/<ride>.trc`.

**You have a result when:** N ≥ 3 committed rides spanning stop-and-go, highway,
and spirited riding all land inside the **pre-declared** transitions-per-minute
and blip-count bounds at the frozen tunables, with **zero missed
sustained-braking events** (every human-annotated hard stop shows a `BRAKING`
entry) — or when a ride class demonstrably violates a bound, which is an equally
valid (negative) result that re-opens the tunable in DE-09 §8.

---

## 3. Scrambler 400 X profile validation [RESEARCH]

**Why the current state falls short.** The roadmap's Phase 2 exit criterion
includes "one profile works on both 400s", and `docs/can-profiles.md` §5 asserts
the Scrambler 400 X shares the Speed 400's TR-series powertrain (the generated
profile is even named "Triumph Speed 400 / Scrambler 400X (TR-series)" in
`transmitter/software/main/bike_profile_triumph_tr.c`). But **no Scrambler
capture exists in the repo** — the one-profile-two-bikes claim is currently an
untested hypothesis baked into a product name. Per the honesty ledger
(`chmbl-external-positioning`), it must not be stated as validated.

**This repo's asset.** The hypothesis is falsifiable with a **single ride log**:
the logger is bike-agnostic (same red 6-pin diagnostic connector), and the whole
offline pipeline — `profiles/triumph_tr.dbc` → `tools/golden_check.py` →
`tools/trc_viz.py` — runs on any `.trc` unchanged.

**First three steps (in this repo):**
1. Record one Scrambler 400 X ride (ignition on → a few stops and launches →
   ignition off) with the existing logger; commit it as
   `logger/scrambler_400x_<desc>.trc`.
2. Run the golden decode against the *unchanged* Speed 400 profile:
   `python3 tools/golden_check.py --trc logger/scrambler_400x_<desc>.trc`
   (defaults already point at `profiles/triumph_tr.dbc` and the host harness —
   build it first per `chmbl-build-and-env`). Disagreement or dead signals here
   falsifies the shared-profile hypothesis at the decode layer.
3. Replay through the FSM bench and compare stats to the Speed 400 baseline:
   `uv run tools/trc_viz.py logger/scrambler_400x_<desc>.trc --headless-check`
   — plausible peak speed, gears seen, accel range, and FSM behavior. Then
   update `docs/can-profiles.md` §5 and the roadmap open-questions row with the
   outcome in the same change.

**You have a result when:** one committed Scrambler ride log decodes every
required signal (wheel_speed, clutch, gear, throttle, rpm) through the unchanged
`triumph_tr` profile with golden_check agreement, and the FSM replay yields
plausible stats — or any required signal fails to decode, which falsifies
one-profile-two-bikes and forks a second profile (a legitimate result; the
`bike_profile_t` machinery in `transmitter/software/main/bike_profiles.h` was
built for multiple profiles).

---

## 4. The remaining hardware DEs toward a wearable product [COMPLETION]

Phase 4's exit is "a wearable unit + a plug-in unit that run a full ride". Five
DEs stand between here and there. All are engineering completion — designs or
firm leans exist; the work is build, measure, document. Per project doctrine,
any new part beyond the ones already selected needs a written trade study (the
DE-04 driver matrix in `docs/design/de-04-led-render.md` §3 is the exemplar),
and each DE is built **one at a time in isolation** per `docs/design/README.md`
§1 (sequencing is change-control law — see `chmbl-change-control`).

| DE | Status | What exists | What's missing |
|----|--------|-------------|----------------|
| DE-02 auto-brightness | 🔲 | `de-02-auto-brightness.md` design; day/night cd endpoints anchored in `docs/led-brightness-benchmark.md` (night floor ≈5–15 cd) | sensor choice/placement, curve calibration (its §8) |
| DE-04 LED bar | 🟡 | Full trade study; **TI LM3410 selected** as primary boost constant-current driver (`de-04-led-render.md` §3) | build + **photometric verification**: bench a bar and measure on-axis intensity against the **≈50–80 cd daylight BRAKE band** (mid-upper ECE R7 S3/S4 CHMSL band, 25–110 cd) set in `docs/led-brightness-benchmark.md`; final LED count/current/optic (its §8) |
| DE-05 battery/charge | 🔲 | Roadmap lean only: **MCP73871** (load sharing — light works while charging; `docs/roadmap.md` open-questions table, `docs/hardware.md` §BOM) | no DE doc yet — write `docs/design/de-05-battery-charge.md` first (per §3: "DE-05…DE-06 don't have stub docs yet; they get one when scheduled") |
| DE-06 TX power/sleep | 🔲 | `docs/hardware.md` targets: parked draw **< 1 mA**, wake on CAN bus activity (transceiver wake / RXD edge) or ignition sense | no DE doc; no sleep code in `transmitter/software/main/` |
| DE-10 status indicator | 🔲 | `de-10-status-indicator.md` design (WS2812 status LED, distinct from the bar) | final blink/color code table — define alongside DE-03 and DE-05 so faults have codes (its §8); color-blind-safe = patterns, not color alone |

**First three steps (in this repo):**
1. DE-04 first (it's already 🟡 and gates DE-02's calibration): resolve the §8
   opens in `docs/design/de-04-led-render.md` (LED count/current/optic from the
   benchmark's flux math), build the LM3410 bar on a carrier, and bench-measure
   on-axis cd at day/night settings per the verification note in
   `docs/led-brightness-benchmark.md` §"Bench-verify".
2. Write the missing stub docs `docs/design/de-05-battery-charge.md` and
   `docs/design/de-06-tx-power-sleep.md` using the eight-section template in
   `docs/design/README.md` §2 (docs are the spec — the doc lands before code),
   and add their rows' details to the §3 table in the same change.
3. Implement each in dependency order with CLI-faked inputs per its §6
   (`docs/cli.md`), and prove its §7 isolation acceptance before integrating —
   e.g. DE-06's acceptance must include a measured parked-draw number vs. the
   < 1 mA gate and a demonstrated wake-on-CAN.

**You have a result when:** each DE flips 🔲/🟡 → 🟢 in `docs/design/README.md`
§3 with its isolation acceptance demonstrated and, where the target is numeric,
a measurement on record: DE-04 bar inside 50–80 cd daylight on-axis (and the
5–15 cd night floor via DE-02), DE-06 parked draw measured < 1 mA with
wake-on-CAN shown, DE-05 powering the bar at full brake current while charging.

---

## 5. End-to-end latency has never been measured [RESEARCH]

**Why the current state falls short.** `ARCHITECTURE.md` budgets brake event →
LED at **≤ 100 ms** and `docs/roadmap.md` Phase 3 requires verifying it, but no
measurement exists anywhere in the repo — the number is a design target, not a
result. There is also a **known tension to state honestly**: the DE-09 decel-on
debounce alone is **120 ms** (`DECEL_ON_DEBOUNCE_MS` in `docs/firmware.md`,
deliberately chosen as the blip-rejection knee), so a wheel-speed-inferred
braking onset **cannot** beat 100 ms end-to-end by construction. The open
research question is therefore two-part: (a) what is the actual
signal-edge→photon latency of the pipeline *excluding* the debounce (the part
the 100 ms budget can meaningfully bound), and (b) how the docs should
reconcile the budget with the debounce — measure first, then update
`ARCHITECTURE.md`/`docs/roadmap.md` in the same change (docs are the spec).

**This repo's asset.** The isolation architecture makes this benchable without
a bike: the developer CLI (`docs/cli.md`) can fake a state edge on the
transmitter (`state force BRAKE`), the DE-01 ESP-NOW link is 🟢 implemented,
and the brake_light renders it — so a bench rig of two dev boards + a logic
analyzer captures the whole radio+render chain today, before DE-09 even lands.

**First three steps (in this repo):**
1. **Define the measurement protocol first** (this is the actual first
   deliverable — write it before touching hardware): a doc section (new
   `docs/design/explorations/latency-measurement.md`, or a Phase-3 section in
   `docs/roadmap.md`) specifying the bench: channel 1 = a GPIO the transmitter
   toggles at the fake signal edge, channel 2 = a photodiode (or the LED
   drive pin) on the brake_light, timestamped by a logic analyzer; N ≥ 100
   edges; report median + p99; declare which segments are in scope (decode →
   FSM → radio → render) and that the 120 ms debounce is accounted separately.
2. Instrument the edge: add a debug GPIO toggle at the state-change point in
   the transmitter (today that's the CLI `state force` path in
   `transmitter/software/main/cmd_state.c` / `net.c`; post-DE-09, the FSM
   transition) — as a Kconfig-gated debug option per `chmbl-config-and-flags`.
3. Run the bench on the two-board pairing setup (`chmbl-run-and-operate`),
   record the distribution, and land the numbers + any budget reconciliation
   into `ARCHITECTURE.md` and the protocol doc's latency line
   (`docs/protocol.md` "Brake event → LED ≤ ~100 ms target") in one change.

**You have a result when:** a committed measurement report gives median and p99
fake-edge→light latency over ≥100 trials on the bench rig, and the docs state
the measured pipeline latency and the debounce contribution as separate,
reconciled numbers — pass (pipeline ≤ 100 ms excluding debounce) or fail, either
is a result; "we believe it's fast" is not.

---

## 6. Recapture and commit the missing bench captures [COMPLETION]

**Why the current state falls short.** `docs/can-profiles.md` §5 cites two bench
captures as evidence for the decode table — `logger/throttle.trc` (full 0–255
throttle sweep proving `0x140` B0 = `raw/2.55`) and `logger/wheel.trc` (wheel
spin proving `0x102` B1–B2 = `raw/16` km/h, "only field active in `wheel.trc`")
— but **neither file is committed**; only `logger/40mph_drive_cycle.trc` exists.
The decode claims themselves are not in doubt (the ride log exercises them and
`golden_check` passes), but the *cited* evidence is unreproducible: broken links
in the doc of record, which fails the project's own reproducibility standard for
an open product (`chmbl-external-positioning`). Verify the gap yourself:
`ls /home/user/open-chmbl/logger/*.trc` → one file.

**This repo's asset.** The logger and the bike still exist; each capture is a
minutes-long stationary bench session (bike on a stand, ignition on), and the
verification is a one-command golden replay.

**First three steps (in this repo):**
1. Recapture on the stand with the logger: one recording sweeping the throttle
   0→100→0 (engine off, ignition on — ride-by-wire publishes it), one recording
   spinning the front wheel by hand. Field procedure: `chmbl-run-and-operate`.
2. Commit them at the exact cited paths — `logger/throttle.trc` and
   `logger/wheel.trc` — so the existing links in `docs/can-profiles.md` §5
   resolve; note in the doc that these are recaptures (date-stamp them).
3. Verify each replays cleanly through the golden pipeline:
   `python3 tools/golden_check.py --trc logger/throttle.trc` (and `wheel.trc`),
   and eyeball the sweep in
   `uv run tools/trc_viz.py logger/throttle.trc --headless-check`
   (peak throttle ≈ 100 %). While in there, fix the stale
   `transmitter/software/captures/` instruction (can-profiles.md ~line 275) —
   the directory has never existed; committed captures live in `logger/`.

**You have a result when:** both files exist at the cited paths, both pass
`golden_check.py`, the throttle capture shows the full 0–100 % sweep and the
wheel capture shows `0x102` as the (dominant) active signal — making every row
of the decode table backed by a committed, replayable capture.

---

## 7. The reserved `ST_DECEL` soft-cue tier [RESEARCH]

**Why the current state falls short — and why this one is genuinely novel.**
The wire protocol reserves a second tier: `docs/protocol.md` defines
`ST_DECEL = 1 // RESERVED — not emitted by the current TX FSM (future soft cue)`,
and the roadmap open-questions table records the deliberate deferral ("Not for
now — the light is binary on/off"). The novel idea: because braking is inferred
from deceleration anyway, the system can signal **engine-braking / coasting
deceleration** that produces *no* brake-lamp indication on any stock motorcycle
— a real safety gap (following drivers get zero cue when a rider slows on
engine braking). No commercial CHMSL-class product keys off CAN wheel-speed
this way. This is open research on three axes: whether a decel band between
"coasting" and `DECEL_ON` is separable on real ride data without flicker; what
a *legal* rendering is; and whether it helps or just trains followers to ignore
the light.

**Hard legal constraint (non-negotiable):** any `ST_DECEL` rendering must be
**steady illumination only — never flashing/strobing/pulsing**.
`docs/safety-regulatory.md` is explicit ("No flashing brake lights … All
patterns are steady"), and it is a merge blocker per `chmbl-change-control`.
The design space is intensity/extent (e.g. a dimmer or narrower steady bar),
not modulation.

**This repo's asset.** The tier costs zero protocol work (already reserved),
and `tools/trc_viz.py` is a ready experiment bench: the FSM port + tunables +
replay stats mean a candidate `DECEL` band can be evaluated offline against the
ride corpus (item 2 — which is why this ranks *after* it: a one-ride corpus
cannot support a new tier's flicker analysis).

**First three steps (in this repo):**
1. Write the exploration doc first (`docs/design/explorations/st-decel-soft-cue.md`,
   per the explorations convention in `docs/design/README.md` §4): hypothesis,
   the steady-only constraint, and pre-declared metrics — added
   transitions-per-minute budget, max `DECEL`↔`OFF` chatter, and the required
   dwell/hysteresis around the new band.
2. Prototype offline: extend the FSM port in `tools/trc_viz.py` (tool-side
   only — no firmware change) with a `DECEL` state between `steady_band` and
   `decel_on`, reusing the existing debounce/hysteresis machinery; expose its
   thresholds as `BrakeTunables` fields so they get CLI flags for free.
3. Replay against the multi-ride corpus (item 2) with `--headless-check` and
   compare chatter against the pre-declared budget; publish the numbers in the
   exploration doc. Only if the offline result clears the bar does this get
   promoted to a DE with FFL traceability (§4's promotion rule).

**You have a result when:** the offline replay shows a `DECEL` band that fires
on real engine-braking segments with chatter inside the pre-declared budget and
no erosion of `BRAKING` behavior — or shows the band is inseparable from noise
at acceptable dwell, which retires the idea with numbers (document the
retirement per `chmbl-research-methodology`; do not leave it half-open).

---

## 8. Runtime profile selection / Street Triple 765 [RESEARCH — stretch]

**Why the current state falls short.** The transmitter compiles in exactly one
bike profile (`transmitter/software/main/bike_profile_triumph_tr.c`, generated
from `profiles/triumph_tr.dbc` by `tools/gen_profile.py` — never hand-edit it;
CI diffs a regeneration against it). An open product for "motorcycles" rather
than "one Triumph" eventually needs a second profile and a way to select one at
runtime — `docs/roadmap.md` Phase 5 ("Add more bike profiles; consider runtime
profile selection") and the open-questions row: Street Triple 765 = **different
platform, own profile, same red 6-pin connector / same TX hardware**. This is a
stretch item: it ranks last because nothing upstream depends on it and the
reference-bike product must be complete first (that's the SOTA definition).

**This repo's asset.** The plumbing anticipates it: `bike_profiles.h` /
`bike_profile.h` define a data-driven `bike_profile_t` (profiles are tables,
not code), `gen_profile.py` takes any DBC, and NVS-backed persistence already
exists for pairing (the `config` CLI verbs are specified in `docs/cli.md` but
NOT implemented — see `chmbl-config-and-flags`). The logger works unchanged on
the 765's connector, so step one is pure capture + reverse engineering.

**First three steps (in this repo):**
1. Capture: record a 765 stand session + one ride with the existing logger;
   commit under `logger/`. Reverse-engineer the signal map per the methodology
   in `docs/can-profiles.md` §3–4 (and `chmbl-proof-and-analysis-toolkit` for
   the identification proofs) — expect different IDs/scalings; nothing from the
   TR table may be assumed.
2. Author `profiles/street_triple_765.dbc` as ground truth and generate:
   `python3 tools/gen_profile.py profiles/street_triple_765.dbc` (see the
   regeneration usage embedded in the tool's docstring and in the generated
   file header); extend the CI staleness diff in
   `.github/workflows/firmware-build.yml` to cover the second profile.
3. Design runtime selection as a real DE (doc first): a profile registry keyed
   in `bike_profiles.h` plus an NVS-persisted selection via the specified-but-
   unimplemented `config set/save` CLI (`docs/cli.md`) — which makes
   implementing the `config` verbs a prerequisite sub-task.

**You have a result when:** a committed 765 capture decodes wheel_speed (and
whatever subset of clutch/gear/throttle/rpm the 765 publishes) through a second
committed DBC+generated profile with a `golden_check.py --dbc
profiles/street_triple_765.dbc --trc logger/<765 ride>.trc` pass, and one
firmware binary can switch profiles via persisted config on the bench. Partial
falsification is possible and reportable: e.g. the 765 bus may lack a signal
the FSM wants, forcing a documented degraded mode.

---

## What is NOT on this frontier (settled — do not reopen)

These look like open problems to a newcomer but are closed; evidence lives in
`chmbl-failure-archaeology`:

- **Brake-wire tap, IMU/inertial detection** — fenced off by existing patents;
  deceleration must come from the bike's own CAN wheel-speed. Never propose.
- **Transmitting/requesting on the bike CAN bus** — listen-only is an
  architectural invariant, not a TODO.
- **Flashing/strobing patterns of any kind** — illegal in target jurisdictions;
  steady only, global min-dwell.
- **Addressable-LED (WS2812) bar** — rejected by the flux math in
  `docs/led-brightness-benchmark.md`; WS2812 survives only as the DE-10 status
  indicator.
- **Logger LCD** — permanent dead end (GPIO21 contention with microSD
  card-detect; SD is mission-critical).
- **Whether the bus broadcasts free-running / has a brake bit** — resolved by
  capture: free-running at 500 kbit/s, confirmed **no** brake-switch bit.

Note the roadmap's Phase-2 "CAN access mode — Unknown" row is stale relative to
`docs/can-profiles.md` §5 (broadcast confirmed); trust the capture evidence.

## When NOT to use this skill

- **Executing DE-09** — `chmbl-de09-campaign` is the step-by-step campaign; this
  skill only ranks it.
- **How to run the logger, flash boards, pair, or move captures** —
  `chmbl-run-and-operate`.
- **How to build the firmware/tools environment** — `chmbl-build-and-env`.
- **What counts as evidence / how to add tests and CI rows** —
  `chmbl-validation-and-qa`.
- **Designing the experiment itself (predict-then-measure, retirement)** —
  `chmbl-research-methodology`.
- **Whether a past idea was already tried and killed** —
  `chmbl-failure-archaeology`.
- **What you may publicly claim about any of these items** —
  `chmbl-external-positioning` (never upgrade an honesty tier).
- **Change gating for landing any of this work** — `chmbl-change-control`.

## Provenance and maintenance

All statuses and numbers verified against the repo on **2026-07-08**. Re-verify
before trusting; each fact class drifts independently:

| Fact class | Re-verification command |
|---|---|
| DE statuses (🔲/🟡/🟢) | `grep -A2 'DE-0' /home/user/open-chmbl/docs/design/README.md \| grep -E '🔲\|🟡\|🟢'` |
| DE-09 still unimplemented | `ls /home/user/open-chmbl/transmitter/software/state_machine/ 2>&1` (should error until done) |
| SPEED_HIST bug still live | `grep CAN_DECODE_SPEED_HIST /home/user/open-chmbl/transmitter/software/main/can_decode.h` (bug = 16) |
| Capture inventory (items 2/3/6) | `ls /home/user/open-chmbl/logger/*.trc` |
| Missing-capture doc links | `grep -n 'throttle.trc\|wheel.trc' /home/user/open-chmbl/docs/can-profiles.md` |
| Tunable defaults / CLI flags | `grep -A14 'class BrakeTunables' /home/user/open-chmbl/tools/trc_viz.py` |
| Latency target unmeasured | `grep -rn '100 ms' /home/user/open-chmbl/ARCHITECTURE.md /home/user/open-chmbl/docs/protocol.md` (still a "target"?) |
| `ST_DECEL` still reserved | `grep -n ST_DECEL /home/user/open-chmbl/docs/protocol.md` |
| cd band / LM3410 selection | `grep -n '50–80 cd' /home/user/open-chmbl/docs/led-brightness-benchmark.md; grep -n 'LM3410' /home/user/open-chmbl/docs/design/de-04-led-render.md` |
| Parked-draw / wake targets | `grep -n '1 mA\|Wake on CAN' /home/user/open-chmbl/docs/hardware.md` |
| Roadmap open-questions leans | `sed -n '76,98p' /home/user/open-chmbl/docs/roadmap.md` |
| Second-profile CI coverage (item 8) | `grep -n 'gen_profile\|dbc' /home/user/open-chmbl/.github/workflows/firmware-build.yml` |
