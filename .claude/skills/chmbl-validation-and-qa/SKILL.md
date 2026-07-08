---
name: chmbl-validation-and-qa
description: Load when deciding whether a change to open-chmbl is "validated", when citing evidence for a claim, when adding/extending tests (golden capture, host unit test, CI matrix row, isolation-acceptance criteria), or when touching acceptance thresholds or accepted baselines. Covers the evidence hierarchy, the golden/capture inventory, exactly what CI gates exist and what they do NOT cover, and the definition-of-done checklist for a design element. Do NOT load for how to run/interpret the tools themselves (chmbl-diagnostics-and-tooling), for the experiment-design method (chmbl-research-methodology), or for the change-review process itself (chmbl-change-control).
---

# Validation and QA: what counts as evidence in open-chmbl

open-chmbl is a helmet brake light driven by a bike-side ESP32 that *listens* to the
motorcycle's CAN bus (Controller Area Network — the shared broadcast wire vehicle ECUs
talk on). There is no brake-switch signal on the bus, so braking is *inferred* from
wheel-speed deceleration by a state machine (FSM). That inference is a safety-adjacent
signal-processing claim, so this project holds validation to a specific standard:
**numbers from replays of committed captures, not impressions from dashboards.**

Jargon used below: a `.trc` file is a PCAN-format text log of raw CAN frames with
timestamps; a DBC file is a machine-readable database describing how signals are packed
into CAN frames; "DE" = design element (the project's unit of work, docs in
`docs/design/`); "FFL" = feature-function list IDs like TX-SM-1.

## 1. The evidence hierarchy

When you claim something works, your evidence sits at one of three levels. Only the top
level supports the word "validated".

| Level | What it is | What it can support |
|---|---|---|
| **1. Replay metrics on a committed capture** | A number computed by replaying `logger/40mph_drive_cycle.trc` through a decoder/FSM (via `tools/golden_check.py`, `tools/trc_viz.py --headless-check`, or the host harness `trc_replay`) | "Validated" / accepted-baseline claims |
| **2. Bench CLI demonstration** | Driving a device (or the host harness) over the serial developer CLI (`docs/cli.md`) with faked inputs and observing outputs | "Demonstrated in isolation" — satisfies a DE's §7 isolation acceptance |
| **3. Reasoning** | Code reading, math, analogy to the docs | "Expected" / "designed" — never "validated" |

Rules:

- A **"validated" claim must cite a capture + a number.** The exemplar is the DE-09
  dry-run evidence in `docs/firmware.md`: the low-speed hysteresis cut FSM transitions
  on the 40 mph ride log from **162 → 48** (sub-0.5 s light blips 65 → 8), and the
  120 ms decel-on debounce further cut transitions **48 → 30** (blips 8 → 3) with
  on-time essentially unchanged. Every clause there names the capture and quantifies
  the effect. Match that pattern.
- **Eyeballing the `trc_viz` dashboard is exploration, not evidence.** The GUI is a
  calibration bench for forming hypotheses; the numbers that back a claim come from
  its headless replay or from `golden_check.py`. (Note, verified in code as of
  2026-07-07: `trc_viz.py --headless-check` prints replay stats — duration, peak
  speed, FSM transition count, brake-on time — plus one peak-speed sanity line
  "43 mph ± 3", and **always exits 0**. It is a stats printer, not a pass/fail gate;
  you must read and cite its numbers yourself.)
- Level 2 (bench CLI) is real evidence for *isolation* acceptance — e.g. DE-01's §7
  ("pair two boards; TX `state force BRAKE` → BL `in show` reflects it") — but it does
  not validate signal-processing behavior on real ride data. A FSM tunable "looks
  right on the bench" is a level-2 claim; the ride-log transition count is level 1.

For *how* to design the experiment (predict numbers before running, refutation), see
`chmbl-research-methodology`. For *how to run and read* each tool, see
`chmbl-diagnostics-and-tooling`.

## 2. The golden / certified inventory

Everything below verified present in-repo as of 2026-07-07.

| Artifact | Path | Role |
|---|---|---|
| Reference ride capture | `logger/40mph_drive_cycle.trc` | The **only** committed capture: ~220 s Triumph Speed 400 ride, 315,882 lines (~22 MB). Every level-1 claim in the project replays this file. |
| Decoded overlay PNG | `logger/40mph_drive_cycle_decoded.png` | Derived visualization of the decoded signals (documentation artifact, not evidence). |
| FSM dry-run PNG | `logger/40mph_drive_cycle_fsm.png` | Derived visualization of the DE-09 dry run (documentation artifact, not evidence). |
| DBC ground truth | `profiles/triumph_tr.dbc` | The committed signal-packing spec. The decode table in `docs/can-profiles.md` §"Decode table" is its human-readable form. |
| Generated profile | `transmitter/software/main/bike_profile_triumph_tr.c` | Generated from the DBC by `tools/gen_profile.py` and committed. **Never hand-edit**; CI diffs a regeneration against it. |
| Per-DE acceptance contracts | `docs/design/de-*.md` **§7 "Isolation acceptance"** | The acceptance criteria for each design element. All seven DE docs (01, 02, 03, 04, 08, 09, 10) carry a §7. |

**Known gap — referenced but missing:** `docs/can-profiles.md` cites two single-signal
bench captures, `logger/throttle.trc` and `logger/wheel.trc`, as cross-checks for the
decode table. They are **not committed** (verified absent as of 2026-07-07). Treat
decode-table rows that lean on them (e.g. "full 0–255 sweep in `throttle.trc`") as
documented-but-not-reproducible-from-repo; do not cite those files as evidence you can
replay. Also note `docs/can-profiles.md` tells contributors to commit captures under
`transmitter/software/captures/`, but the actual committed capture lives in `logger/`
— follow the existing `logger/` convention until the doc is reconciled.

## 3. Automated gates in CI — exactly what runs

One workflow: `.github/workflows/firmware-build.yml`, triggered on push/PR touching
`brake_light/software/**`, `logger/software/**`, `transmitter/software/**`,
`profiles/**`, `tools/**`, or the workflow itself.

**Job 1 — `build`: 5-row ESP-IDF build matrix** (espressif/esp-idf-ci-action@v1,
`esp_idf_version: release-v5.3`, `fail-fast: false`):

| # | name | path | target |
|---|---|---|---|
| 1 | brake_light | `brake_light/software` | esp32c3 |
| 2 | brake_light | `brake_light/software` | esp32 |
| 3 | logger | `logger/software` | esp32 |
| 4 | transmitter | `transmitter/software` | esp32c3 |
| 5 | transmitter | `transmitter/software` | esp32 |

(esp32c3 is the product target; classic esp32 is interim dev hardware; the logger only
targets classic esp32, the WROVER-KIT.)

**Job 2 — `can-decode-golden` (host, Python 3.12, `pip install -r
tools/requirements.txt`):**

1. **Staleness diff**: regenerates the profile and diffs it against the committed file —
   ```
   python3 tools/gen_profile.py profiles/triumph_tr.dbc \
     --name "Triumph Speed 400 / Scrambler 400X (TR-series)" \
     --bitrate 500000 --symbol bike_profile_triumph_tr \
     --out /tmp/bike_profile_triumph_tr.c
   diff -u transmitter/software/main/bike_profile_triumph_tr.c /tmp/bike_profile_triumph_tr.c
   ```
2. **Host harness build**: `cmake -S transmitter/software/test_host -B
   transmitter/software/test_host/build -DCMAKE_BUILD_TYPE=Release && cmake --build
   transmitter/software/test_host/build` — builds `trc_replay` from the *real* firmware
   sources `../main/can_decode.c` + `../main/bike_profile_triumph_tr.c` with
   `-Wall -Wextra -Werror`.
3. **Golden comparison**: `python3 tools/golden_check.py` — replays
   `logger/40mph_drive_cycle.trc` through both the C decoder (`trc_replay`) and
   python-cantools decoding `profiles/triumph_tr.dbc`, asserting every decoded signal
   value (plus derived `clutch_pulled` / `engine_cutoff` predicates) agrees within
   **`REL_TOL = 1e-4` / `ABS_TOL = 1e-3`** (constants at the top of
   `tools/golden_check.py`; overridable inputs via `--harness/--dbc/--trc`, whose
   defaults are the committed paths above).

**What is NOT covered by any automated test today (as of 2026-07-07):**

- **The DE-09 braking state machine** — no firmware implementation exists yet
  (`transmitter/software/state_machine/brake_fsm.sm` and `tools/smc/Smc.jar` are
  described in `docs/firmware.md` §4 but absent from the tree; DE-09 status is 🔲).
  The only FSM executable is the Python port inside `tools/trc_viz.py`, and its
  headless mode is not a gate (see §1).
- **All brake_light logic** — LED render, link-loss failsafe, auto-brightness, pairing:
  CI only proves they *compile*.
- **The ESP-NOW link layer** (the encrypted radio protocol between bike and helmet) —
  no automated test; DE-01's acceptance is a two-board bench procedure.
- **Anything device-side** — no hardware-in-the-loop, no on-target tests. The five
  matrix rows are compile checks only.
- The known live firmware bug (`CAN_DECODE_SPEED_HIST` = 16 in
  `transmitter/software/main/can_decode.h`, too small for the ~100 Hz wheel-speed rate)
  passes CI because no gate exercises the acceleration derivation. Fixing it is
  reserved for the DE-09 campaign — see `chmbl-de09-campaign`.

## 4. How to add tests

### 4a. A new golden capture

1. Record with the logger (field procedure: `chmbl-run-and-operate`). Name it
   descriptively after the maneuver, matching the existing pattern:
   `<scenario>_<detail>.trc` (exemplar: `40mph_drive_cycle.trc`; docs suggest names
   like `speed400_coastdown`).
2. **Anonymize before committing** — the docs of record require "anonymized raw
   captures" (`docs/can-profiles.md`). A `.trc` holds only CAN frames + relative
   timestamps, but strip/avoid anything identifying in filenames, comments, or the
   `.trc` header lines, and never commit GPS or companion metadata.
3. Commit under `logger/` (the actual convention — see the §2 note about the
   doc-vs-repo path discrepancy).
4. Wire it into the gates:
   - `golden_check.py` accepts `--trc <path>`; to make a second capture a permanent
     gate, add a second `python3 tools/golden_check.py --trc logger/<new>.trc` step to
     the `can-decode-golden` job in `.github/workflows/firmware-build.yml`.
   - `trc_viz.py` takes the capture as its positional argument:
     `uv run tools/trc_viz.py logger/<new>.trc --headless-check`. Note its peak-speed
     sanity line is hardcoded to the 40 mph ride (43 mph ± 3) — expect "CHECK" on a
     different ride; that line is informational, not a failure.
5. If the capture becomes load-bearing for a claim, update the doc making the claim in
   the same change (docs-are-spec rule — `chmbl-change-control`).

### 4b. A new host-side unit test

The host harness is deliberately tiny: `transmitter/software/test_host/CMakeLists.txt`
defines one executable, `trc_replay`, built from `trc_replay.c` + the firmware's
`../main/can_decode.c` + `../main/bike_profile_triumph_tr.c`, with `../main` on the
include path and `-Wall -Wextra -Werror`. To add a test:

1. Add a new source `transmitter/software/test_host/<name>.c` with its own `main()`.
2. Extend the CMakeLists with a second `add_executable(<name> <name>.c
   ../main/<units-under-test>.c ...)` + matching `target_include_directories(<name>
   PRIVATE ../main)`. Keep pulling in the *real* firmware sources — that identity (host
   test compiles the same .c the device runs) is the whole point of the harness.
3. Build and run locally exactly as CI does (the cmake commands in §3 step 2), then add
   a run step for your executable to the `can-decode-golden` job. Your test must exit
   non-zero on failure — CI has no other way to notice.
4. Firmware code with ESP-IDF dependencies (FreeRTOS, TWAI, NVS) can't compile on the
   host; keep testable logic in dependency-free units like `can_decode.c` is. If you
   need to restructure firmware to make logic host-testable, that's a design change —
   route it through `chmbl-change-control`.

### 4c. A new CI matrix row

Copy an existing `include:` entry in the `build` job of
`.github/workflows/firmware-build.yml` and set `name` / `path` / `target`:

```yaml
          - name: <firmware>
            path: <firmware>/software
            target: esp32c3   # or esp32
```

Also add the firmware's path glob to both `on.push.paths` and `on.pull_request.paths`
if it's a new directory, or the workflow won't trigger on changes to it.

### 4d. A new isolation acceptance

Every DE doc carries a **§7 "Isolation acceptance"** section: a bulleted list of
observable, CLI-drivable pass conditions written *before* implementation. Follow the
existing pattern (read `docs/design/de-01-espnow-link.md` §7 for a bench-interaction
style, `docs/design/de-09-brake-decel-logic.md` §7 for a stimulus→state style):

- Each bullet is a concrete stimulus → observable outcome ("a synthetic decel ramp
  steeper than `decel_on_mphps` → `BRAKING` within budget"), exercisable via the
  developer CLI with faked inputs (`sig set ...`, `state show`, etc. — `docs/cli.md`).
- Name tunables/thresholds symbolically (they resolve to the tunables table in
  `docs/firmware.md`), and include the safety invariants that apply ("no transition
  violates the anti-strobe floor").
- Acceptance criteria are part of the spec: adding or changing them is a doc-of-record
  change (same-change rule, `chmbl-change-control`; template/style details in
  `chmbl-docs-and-writing`).

## 5. Acceptance-threshold discipline

- **State thresholds BEFORE the run.** Write down the pass/fail number (or the
  predicted metric) before executing the replay or bench test. A number chosen after
  seeing the output is a description, not a test. Full method in
  `chmbl-research-methodology`.
- **Never adjust a threshold post-hoc to make a run pass.** If the run misses the
  stated threshold, the result is a miss; either the implementation changes or the
  threshold is *re-derived* — with written rationale, as its own reviewed change —
  before a re-run.
- **Changes to accepted baselines go through change control.** The golden tolerances
  (REL 1e-4 / ABS 1e-3 in `tools/golden_check.py`), the DE-09 accepted dry-run numbers
  (162→48, 48→30), the FSM tunable defaults in `docs/firmware.md`, and each DE's §7
  criteria are accepted baselines. Loosening any of them requires the trade-off to be
  written into the corresponding doc of record **in the same change** as the
  code/config edit (docs-are-spec). No skill or shortcut routes around this — see
  `chmbl-change-control`.
- Tightening a threshold is also a change (it can flip green history to red), but the
  bar for rationale is lower than for loosening.

## 6. Definition of done for a design element

A DE is not done until every box checks. (Flipping the status emoji is the *last* step,
not the first.)

- [ ] **Build green on both targets**: the firmware builds for `esp32c3` and `esp32`
      in the CI matrix (all rows touching your firmware pass). Locally without CI:
      `idf.py set-target <t> && idf.py build` per target (requires ESP-IDF
      release-v5.3 — see `chmbl-build-and-env`).
- [ ] **Isolation acceptance demonstrated**: every bullet of the DE doc's §7 exercised
      via the developer CLI (or host harness) and observed passing; results recorded
      with numbers where the criteria are quantitative (level-1/level-2 evidence per
      §1, cited capture + number for any "validated" wording).
- [ ] **No regression in existing gates**: staleness diff clean, `golden_check.py`
      passes, all 5 build rows green.
- [ ] **Docs updated in the same change**: the DE doc (§7 results, §8 open items,
      implementation-notes section if deviations occurred — DE-01 §9 is the exemplar),
      plus `docs/firmware.md`/`docs/cli.md`/`docs/can-profiles.md` wherever behavior
      they describe changed. No silent doc/code divergence.
- [ ] **Status table flipped** in `docs/design/README.md` §3
      (🔲 not started → 🟡 in design → 🟢 implemented), same change.
- [ ] Any new dependency/framework introduced has a written trade study in the DE doc
      (exemplar: the LM3410 driver study in `docs/design/de-04-led-render.md`).

## When NOT to use this skill

- Running/interpreting `trc_viz`, `golden_check`, `gen_profile` in detail →
  `chmbl-diagnostics-and-tooling`.
- Designing the experiment itself (prediction-first, refutation, idea lifecycle) →
  `chmbl-research-methodology`.
- The change classification/review process and the non-negotiables' rationale →
  `chmbl-change-control`.
- DE doc structure, FFL IDs, and house style for writing the docs this skill says to
  update → `chmbl-docs-and-writing`.
- Actually executing the DE-09 implementation campaign → `chmbl-de09-campaign`.

## Provenance and maintenance

All facts verified against the repo on branch `claude/skill-library-continuity-mib7ua`,
2026-07-07. Re-verify before trusting:

| Fact class | Re-verification command |
|---|---|
| CI matrix rows + golden job steps | `cat .github/workflows/firmware-build.yml` |
| Golden tolerances + default paths | `grep -n "TOL\|default" tools/golden_check.py` |
| Committed capture inventory | `ls logger/*.trc logger/*.png` |
| Missing bench captures still missing | `ls logger/throttle.trc logger/wheel.trc` (expect "No such file") |
| Capture size/duration | `wc -l logger/40mph_drive_cycle.trc` (315,882 lines) |
| DE §7 sections present | `grep -n "## 7. Isolation acceptance" docs/design/de-*.md` |
| DE-09 evidence numbers (162→48, 48→30) | `grep -n "162" docs/firmware.md` |
| DE status table | `sed -n '55,75p' docs/design/README.md` |
| Host harness sources/flags | `cat transmitter/software/test_host/CMakeLists.txt` |
| DE-09 still unimplemented | `ls transmitter/software/state_machine tools/smc` (expect "No such file") |
| headless-check behavior (stats-only, exit 0) | read `headless_check()` in `tools/trc_viz.py` |
