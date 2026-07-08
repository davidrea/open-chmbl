---
name: chmbl-research-methodology
description: Load when doing investigative work in open-chmbl — identifying/attributing a CAN signal, forming a hypothesis about a bug or behavior, designing a replay experiment, tuning an FSM parameter, deciding whether a hunch is now an "accepted result", or retiring an idea. Covers the evidence bar (one mechanism explains ALL observations including negatives), predict-numbers-before-running, the hunch→replay→write-up→promote/retire lifecycle, and assigned adversarial refutation. Do NOT load for what evidence tier a claim sits at or acceptance thresholds (chmbl-validation-and-qa), for how to run/read the tools (chmbl-diagnostics-and-tooling), for the change-gating process (chmbl-change-control), for doc structure/house style (chmbl-docs-and-writing), or for the record of already-settled investigations (chmbl-failure-archaeology).
---

# Research methodology: how a hunch becomes an accepted result in open-chmbl

open-chmbl infers motorcycle braking from reverse-engineered CAN-bus data (CAN =
Controller Area Network, the shared broadcast wire vehicle ECUs talk on). Nothing on
that bus is documented by the manufacturer; every decoded signal and every FSM (finite
state machine) parameter in this repo is the product of an *investigation*. This skill
is the discipline that turns "I think byte 7 of 0x481 is the side stand" into a row in
the decode table — and turns "maybe an LCD status display" into a documented dead end
instead of a zombie idea.

Jargon used below: a `.trc` file is a PCAN-format text log of timestamped raw CAN
frames; a DBC file is a machine-readable spec of how signals pack into frames; "DE" =
design element (`docs/design/de-*.md`, the project's unit of work); "replay" = running
a committed `.trc` capture through offline tooling (`tools/trc_viz.py`,
`tools/golden_check.py`) instead of riding the bike again.

## 1. The evidence bar: one mechanism, ALL observations, including the negatives

A hypothesis is accepted only when **a single mechanism explains every observation you
have — including the observations where nothing happened — and has survived deliberate
attempts to break it** (§5). Explaining the positive cases is table stakes; the
negatives are what kill look-alike hypotheses.

Both canonical exemplars live in `docs/can-profiles.md` §"Decode notes (Speed 400
reference capture)" — read them before doing any signal-attribution work.

### Exemplar A — the side-stand identification (negatives kill the impostor)

Two candidate decodes both "explained" the ride capture:

| Candidate | Ride observation | Bench negative control | Verdict |
|---|---|---|---|
| `0x121`/`0x113` lamp-cluster bits | Fire shortly after the ride starts | **FAIL** — they also fire on the bench the first time speed exceeds ~10 km/h. Mechanism is the warning-lamp **self-check clearing**, not the stand. | Rejected |
| `0x481` B7 bit 0 | Flips 0→1 shortly after the ride begins, stays up | **PASS** — reads 0 (stand down) in every bench capture, consistent with the bike sitting on its stand | Accepted: `side_stand` |

The lamp-cluster candidate was *correlated* with ride onset; only the bench negative
(bike on its stand, signal must stay 0) exposed that the correlation had a different
mechanism. Lesson: **always run the capture where your signal should do nothing.**

### Exemplar B — the two-rpm disambiguation (correlation ≠ identity)

`0x146` B2–B3 correlates **0.98** with `0x140` B6 and decodes to textbook rpm values
at ×0.25 (idle 1410, max 4559). A correlation-only method would accept it as engine
speed. The discriminating observations:

- `0x146` **freezes at the idle setpoint when stationary** and **lingers ~1300 after
  the kill switch** — impossible for a crankshaft that is coasting to a halt. So it is
  the ECU's *filtered/target* rpm, not crank speed.
- `0x140` B6 shows cranking at start, its ratio to wheel speed is constant within each
  gear (clean gearbox steps ≈ 0.27 / 0.19 / 0.14 / 0.11), and it **reads 0 whenever
  the engine is off** — the honest on/off signal.

Lesson: when two signals correlate 0.98, the 2% where they disagree is where the truth
is. Find the operating regime that separates the mechanisms (here: stationary idle and
post-kill decay) and let it decide.

Honesty note (as of 2026-07-08): the bench negative-control captures the decode notes
cite (`logger/throttle.trc`, `logger/wheel.trc`) are **not committed** — only
`logger/40mph_drive_cycle.trc` is. The reasoning is documented but not replayable from
the repo; see `chmbl-validation-and-qa` §2 before citing them as evidence.

## 2. Predict the numbers BEFORE running

Before executing a replay or bench run, **write down the metric you will read and the
value/direction you expect**. A number chosen after seeing the output is a
description, not a test (threshold discipline: `chmbl-validation-and-qa` §5).

The exemplar (verified in `docs/firmware.md` and `docs/design/de-09-brake-decel-logic.md`
§4): the 120 ms decel-on debounce for the DE-09 FSM.

1. **Hypothesis with a mechanism:** momentary throttle-off dips spike the wheel-speed
   derivative past the braking threshold but clear within a tick or two; a debounce
   requiring the threshold to hold continuously should reject them *without
   attenuating* real braking.
2. **Prediction, stated first:** short `BRAKING` blips drop substantially; total
   brake-**on time stays essentially unchanged** (because real braking holds the
   threshold and merely fires ≤ 120 ms later). The unchanged-on-time clause is the
   load-bearing part — it is what a low-pass filter would violate.
3. **Run the replay** on `logger/40mph_drive_cycle.trc`. Observed: sub-0.5 s blips
   **8 → 3**, total transitions **48 → 30**, on-time essentially unchanged. Prediction
   met on both axes.
4. **The knee method:** sweep the parameter and find where the trade flips. Larger
   debounce values started **clipping real short brake taps** — so 120 ms is the knee:
   the largest value that removes artifacts before it starts eating signal. Report the
   knee and the failure mode beyond it, not just "120 ms works."

(The same discipline produced the hysteresis result that preceded it: predicted that a
`stop`/`moving` speed gap would stop the STOPPED↔OFF ping-pong; observed transitions
**162 → 48**, blips **65 → 8**.)

A prediction that includes what must *not* change (on-time, decode agreement, a
negative capture staying silent) is worth double one that only predicts the headline
number.

## 3. The idea lifecycle in this repo

Every idea travels this path. No stage may be skipped, and both terminal states leave
a written record.

```
hunch
  → offline replay experiment (trc_viz tunables / analysis over committed captures)
    → written analysis in the DE doc or docs/design/explorations/
      → PROMOTION via change control (docs updated same-change, CI + golden green,
        DE status flip in docs/design/README.md §3)
      OR
      → DOCUMENTED RETIREMENT with evidence (dead-end note in the doc of record)
```

**Stage 1 — hunch.** Comes from the sources in §4. Costs nothing; write it down.

**Stage 2 — offline replay experiment.** The workbench is `tools/trc_viz.py`
(run: `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc`): it ports the firmware's
acceleration derivation and the DE-09 FSM, exposes **every FSM tunable as a live
slider**, and has a `--headless-check` stats mode. You iterate against the committed
capture, not against the bike — a ride costs an afternoon; a replay costs seconds.
Design the experiment per §1–§2; tool interpretation details are in
`chmbl-diagnostics-and-tooling`.

**Stage 3 — written analysis.** Results land in the doc of record (format: §6):
- Signal decodes → `docs/can-profiles.md` decode table + decode notes.
- FSM behavior/tunables → the DE doc (`docs/design/de-09-brake-decel-logic.md`) and
  `docs/firmware.md`.
- Future-state directions not yet committed to → `docs/design/explorations/` — the
  designated **parking lot**. Per its README, an exploration "does not get an
  FFL-traced isolation test or a slot in the status table until it's promoted to a
  real design element." Exemplar: `docs/design/explorations/mounting-magnetic.md`
  (status 💭 exploring).

**Stage 4a — promotion.** Goes through `chmbl-change-control`: docs updated in the
same change as code (docs-are-spec), CI build matrix + golden check green, and the
status emoji flipped in the `docs/design/README.md` §3 table (🔲 → 🟡 → 🟢) as the
*last* step. Open-question rows in `docs/roadmap.md` §"Open questions" are resolved
**in place** — the row stays, its "Current lean" cell records the answer with a link
(exemplar: the "Brake signal source" row now reads "**Resolved — no brake bit on the
reference bus**"). Never delete the question; the resolved row is the record that it
was ever open.

**Stage 4b — documented retirement.** A dead idea gets a tombstone in the doc of
record stating the evidence, so nobody re-fights the battle. The exemplar is the
WROVER-KIT LCD dead end, permanently recorded in `logger/software/README.md`: the
LCD's DC (data/command) line is hardwired to GPIO21, which the board also routes to
the microSD card-detect; the pins contend whenever a card is present; SD is
mission-critical and neither line has an alternate pin, "the LCD is a dead end here."
Four commits of real work (`4f17d58`→`ffb8cbf`→`5462acb`→`85587d7`) preceded that
verdict, and the write-up is what makes them a purchase instead of a waste — the
status LED on GPIO0 (`51991ae`) replaced it. The full settled-investigation chronicle
is `chmbl-failure-archaeology`; this skill's rule is only: **retirement is a
write-up, not a silent abandonment.**

## 4. Where good ideas historically came from here

Verified against git history and docs (as of 2026-07-08). When you need a hunch, these
are the four proven generators, in rough order of yield:

1. **Staring at real captures.** Every row of the decode table came from a human (or
   model) scrolling decoded overlays of `logger/40mph_drive_cycle.trc` and asking "what
   moved when X happened?" (`f8ba271` — first profiling ride and decoding — added the
   capture, the decoded PNG, and the decode-table rewrite in one commit). No signal was
   ever identified from documentation, because there is none.
2. **Dry-running designed logic against real data BEFORE implementing it** — the
   single most instructive arc in the repo. The DE-09 FSM was designed on paper
   (`5c094a5`), ported to Python inside `trc_viz.py` (`2434c17`), and dry-run against
   the ride capture *before any firmware FSM existed* (it still doesn't —
   `transmitter/software/state_machine/` is absent, DE-09 status 🔲). That dry run
   (`8192663`, "Docs update based on braking/decel FSM dry run from captured CAN
   data") produced the hysteresis fix (162→48) and the debounce knee (48→30) as *design
   inputs*, not post-hoc patches. Cost of iteration: a slider drag, not a
   flash-and-ride cycle.
3. **Tool-first development.** Building the measurement tool finds bugs the product
   can't see in itself: writing `trc_viz.py`'s faithful port of `accel_update()`
   exposed the live firmware `CAN_DECODE_SPEED_HIST` bug (ring of 16 samples spans
   ~150 ms at the ~100 Hz wheel-speed rate — less than the 200 ms
   `CAN_DECODE_ACCEL_WINDOW_MS`, so the derived acceleration freezes) **before the
   firmware FSM that would consume that acceleration even existed**. The discovery
   trail is the tool-side commits `f44b0ed`/`bca0d14`/`6930e19` and the NOTE block in
   the `tools/trc_viz.py` header, which also states the sizing law
   (hist ≥ window_ms × frame_rate). The fix is reserved for the DE-09 campaign
   (`chmbl-de09-campaign`).
4. **Constraint-driven design.** The project's best idea was forced by a fence:
   existing patents rule out a brake-wire tap and IMU/inertial detection, and the bus
   has no brake-switch bit — so braking *had* to come from wheel-speed-derived
   deceleration (`5c094a5`, "Redesign braking logic around wheel-speed deceleration").
   When you hit a hard constraint, treat it as a generator: ask "what does the
   constraint leave available?" before asking "how do we relax it?" (Constraint
   details: `chmbl-external-positioning`; never propose the fenced approaches.)

## 5. Assigned adversarial refutation

Before a mechanism is accepted, **one session (or reviewer) takes the explicit job of
breaking it.** Not "does anyone see a problem?" — an assigned role whose success
condition is refutation. The proposer's own confirmation runs do not count; the
side-stand impostor (§1) survived every confirmation the ride capture could offer and
died only under the bench attack.

Standard attack list — run every applicable one and record the outcome:

- [ ] **Could another signal explain the data?** Enumerate the look-alikes (the
      0.98-correlated `0x146` vs `0x140` B6 case) and find the regime that separates
      them.
- [ ] **Is it an artifact of the single capture?** The repo has ONE committed ride.
      Does the mechanism depend on something incidental to that ride (its speed
      envelope, its one kill event, its gear sequence)?
- [ ] **Does the negative-control capture stay silent?** Run the capture where the
      effect must be absent (bench/stand for ride-only signals; a no-kill drive for
      the cutoff code — "in a drive capture with no kill this code never appears").
- [ ] **Does the fix survive a different ride segment?** Re-run on a different slice
      of the capture than the one it was tuned on (scrub in `trc_viz`); when more
      captures exist, on a different ride entirely.
- [ ] **Does the mechanism predict something checkable it wasn't fitted to?** The
      accepted decodes all did: engine-cutoff asserting ~30 ms *before* the rpm decay
      begins; gear changes landing inside clutch pulses; gearbox-ratio steps being
      constant per gear. A mechanism that only reproduces its training observations is
      a curve fit.

An attack that lands sends the idea back to §3 stage 2 (or to retirement). Attacks
that were run and failed to land go in the write-up (§6) — they are the evidence.

## 6. The write-up standard

Every accepted result and every retirement is filed in this shape, in the doc of
record:

1. **Symptom / question** — what was observed or asked.
2. **Mechanism** — the single explanation, stated so it could be wrong.
3. **Predicted numbers** — what the mechanism said you'd see, written before running.
4. **Observed numbers** — capture named, metric quantified (the house exemplar:
   "on the DE-07 40 mph ride log it cut FSM transitions 162 → 48 and sub-0.5 s blips
   65 → 8").
5. **Negatives checked** — which §5 attacks were run and what they showed.
6. **Decision** — accepted (table row / tunable / status flip) or retired (tombstone),
   with the knee/trade-off if a parameter was chosen.

Where it gets filed and in what house style is owned by `chmbl-docs-and-writing`
(decode notes vs DE doc vs explorations vs roadmap row); what evidence tier the
numbers constitute — and the rule that "validated" requires a committed capture plus a
number — is owned by `chmbl-validation-and-qa`. This skill owns only the shape and the
discipline that produced the numbers.

## When NOT to use this skill

- Evidence tiers, acceptance thresholds, golden/capture inventory, definition of done
  → `chmbl-validation-and-qa`.
- Running and interpreting `trc_viz` / `golden_check` / `gen_profile` →
  `chmbl-diagnostics-and-tooling`.
- Worked math recipes (derivative of a quantized signal, hysteresis vs rate-limit,
  debounce-knee mechanics) → `chmbl-proof-and-analysis-toolkit`.
- The gating/review process a promotion must pass → `chmbl-change-control`.
- Looking up whether an investigation is already settled → `chmbl-failure-archaeology`.
- Executing the DE-09 implementation itself → `chmbl-de09-campaign`.
- Choosing which open problem to research next → `chmbl-research-frontier`.

## Provenance and maintenance

All facts verified against the repo (branch `claude/skill-library-continuity-mib7ua`)
on 2026-07-08. Re-verify before trusting:

| Fact class | Re-verification command |
|---|---|
| Side-stand + two-rpm decode notes | `grep -n "0x481\|self-check\|setpoint" docs/can-profiles.md` |
| Debounce/hysteresis numbers + knee wording | `grep -n "162\|48 → 30\|knee" docs/firmware.md docs/design/de-09-brake-decel-logic.md` |
| LCD dead-end tombstone | `grep -n "dead end\|GPIO21" logger/software/README.md` |
| Explorations parking-lot rule | `cat docs/design/explorations/README.md` |
| Roadmap resolved-in-place rows | `grep -n "Resolved" docs/roadmap.md` |
| SPEED_HIST bug + sizing law | `sed -n '45,60p' tools/trc_viz.py; grep -n SPEED_HIST transmitter/software/main/can_decode.h` |
| DE-09 still unimplemented (dry-run-only) | `ls transmitter/software/state_machine` (expect "No such file"); DE-09 row in `docs/design/README.md` §3 |
| Key commits (`f8ba271`, `8192663`, `5c094a5`, `2434c17`, LCD arc) | `git show --stat <hash>` |
| Bench captures still missing | `ls logger/throttle.trc logger/wheel.trc` (expect "No such file") |
| Committed capture | `ls logger/*.trc` |
