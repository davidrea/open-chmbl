---
name: chmbl-de09-campaign
description: >
  EXECUTABLE, decision-gated campaign to implement DE-09 — the braking state machine —
  in the transmitter firmware, including the prerequisite CAN_DECODE_SPEED_HIST bug fix,
  the SMC-vs-hand-coded-FSM decision gate, host replay validation against the committed
  40 mph ride capture, firmware integration, and promotion through change control. Load
  this skill when the task is to implement, resume, validate, or review DE-09 work
  (states OFF/BRAKING/STOPPED, brake_fsm, decel debounce, stop hysteresis, SPEED_HIST).
  Do NOT load it for general build/flash setup (chmbl-build-and-env), CAN/DBC theory
  (chmbl-can-reference), the change-gating rules themselves (chmbl-change-control),
  evidence standards in the abstract (chmbl-validation-and-qa), or debugging unrelated
  failures (chmbl-debugging-playbook).
---

# DE-09 campaign: implement the braking state machine in the transmitter firmware

This is a **campaign runbook**, not background reading. Execute the phases in order.
Every gate states the EXPECTED observation and what to do if you see something else.
All commands run from the repo root `/home/user/open-chmbl` unless noted. All numbers
below were measured against the real repo **as of 2026-07-08** (branch
`claude/skill-library-continuity-mib7ua`, HEAD `30f0b15`); re-verification commands are
in "Provenance and maintenance" at the end.

**Jargon used here (defined once):**

| Term | Meaning |
|---|---|
| CAN | Controller Area Network — the motorcycle's internal message bus. The transmitter reads it strictly **listen-only** (never ACKs or transmits). |
| FSM | Finite State Machine — here the DE-09 `OFF`/`BRAKING`/`STOPPED` brake-light logic. |
| SMC | State Machine Compiler (smc.sourceforge.net) — a Java tool that generates C from a `.sm` model. Documented as the DE-09 mechanism but **not yet in the repo** (Phase 2). |
| `.trc` | PCAN trace file — the text CAN-capture format the logger writes. The reference capture is `logger/40mph_drive_cycle.trc` (315,882 lines, ~220 s ride). |
| DBC | CAN database file (`profiles/triumph_tr.dbc`) describing signal bit layouts; ground truth for the generated decode profile. |
| ESP-NOW | Espressif's connectionless 2.4 GHz radio protocol; carries the 8-byte heartbeat (`chmbl_msg_t`, `transmitter/software/main/protocol.h`) to the helmet unit. |
| DE / FFL | Design element / feature-function list — the project's unit of work and its capability IDs (DE-09 realizes TX-SM-1…6). Status table: `docs/design/README.md` §3. |
| Kconfig / NVS | ESP-IDF's build-config menu system / non-volatile storage. Only peripheral here. |

**Docs of record for this element** (read before writing code):
`docs/design/de-09-brake-decel-logic.md` (the spec — transition table §4, module
decomposition §5, CLI hooks §6, isolation acceptance §7),
`docs/firmware.md` (§"Braking state machine" incl. the tunables table and `tx_config_t`;
§4 "State Machine Compiler (SMC) pre-build step"), `docs/cli.md` §3, and the reference
Python implementation `tools/trc_viz.py` (`run_fsm`, `_compute_accel`, `BrakeTunables`).

**Doctrine that binds every phase** (owned in detail by `chmbl-change-control`):
docs are the spec — any behavior/doc divergence is fixed **in the same change**; no new
dependency without a **written trade study** (exemplar: the LM3410 study in
`docs/design/de-04-led-render.md`); never transmit on the bike CAN bus; no
strobing/flashing; no IMU or brake-wire-tap detection (patent + safety fences); never
hand-edit the generated `transmitter/software/main/bike_profile_triumph_tr.c`.

---

## Phase 0 — Preflight: verify the repo still matches this campaign's assumptions

DE-09 may have been started by a previous session. Run these checks first; each has an
expected output and a resume branch.

```bash
cd /home/user/open-chmbl
grep -n "CAN_DECODE_SPEED_HIST" transmitter/software/main/can_decode.h
ls transmitter/software/state_machine tools/smc 2>&1
grep -n "DE-09" docs/design/README.md
grep -rn "brake_fsm" transmitter/software/main/ | head
git log --oneline -5
```

Expected (campaign not started):

1. `#define CAN_DECODE_SPEED_HIST      16u` (line 28) — the live bug is still in place.
2. `ls: cannot access ... No such file or directory` for **both**
   `transmitter/software/state_machine` and `tools/smc` — the SMC route is documented
   but nothing exists.
3. `| **DE-09** | [Braking state machine](de-09-brake-decel-logic.md) | transmitter | TX-SM-* | DE-00, DE-08 | 🔲 |`
   (line 69) — status "not started". `docs/design/de-09-brake-decel-logic.md` line 3
   also says `**Status:** 🔲 not started`.
4. No `brake_fsm` hits in `transmitter/software/main/` — no FSM sources yet.

**If you see X instead → branch:**

- `SPEED_HIST` is `32u` → Phase 1 is done; confirm its gate (golden_check output below)
  still holds and skip to Phase 2.
- `tools/smc/Smc.jar` exists or a `brake_fsm.sm` exists → the SMC decision (Phase 2)
  was taken as option A. Do not re-litigate it silently; find the trade-study/decision
  record (search `git log --oneline --all -- tools/smc docs/design/de-09*` and the DE-09
  doc §8) and continue from Phase 3 semantics + Phase 4 validation.
- `brake_fsm.[ch]` sources exist in `main/` → Phases 2–3 at least partially done.
  Run the Phase 4 gate to find where the previous session stopped.
- DE-09 row shows 🟡/🟢 → read the doc's §8 open items and `git log` for what was
  claimed; a 🟢 with any gate below failing is a docs-are-spec violation — treat fixing
  that as the first task, through change control.

Also confirm the working tools baseline (this doubles as an environment check):

```bash
cmake -S transmitter/software/test_host -B transmitter/software/test_host/build -DCMAKE_BUILD_TYPE=Release
cmake --build transmitter/software/test_host/build
python3 -c "import cantools, can" || pip install -r tools/requirements.txt
python3 tools/golden_check.py
```

Expected final lines (measured 2026-07-08, `SPEED_HIST=16` baseline):

```
frames=315869 accel_mphps_min=-4.73 accel_mphps_max=4.00 wheel_speed_mph_last=0.00
PASS: 183944 signal values identical between the C decoder and cantools
```

Note that `accel_mphps` range `-4.73..+4.00` — that suspiciously tame range **is the
bug** Phase 1 fixes. If the harness fails to build or golden_check fails here, stop:
your environment or baseline is broken — load `chmbl-build-and-env` /
`chmbl-debugging-playbook` before touching DE-09.

Record the reference-tool baseline (the numbers later gates compare against):

```bash
uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check
```

Expected output, **exactly** (tool-default tunables; measured 2026-07-08):

```
duration        :    220.1 s
grid points     :    11005  @ 50 Hz
peak speed      :     43.2 mph
peak rpm (live) :     4522 rpm
peak throttle   :     29.8 %
gears seen      : [0, 1, 2, 3, 4]
accel range     : -6.8 .. +21.0 mph/s
FSM transitions :       29
brake light on  :     89.0 s (40% of ride)
peak-speed sanity (43 mph ± 3): PASS
```

(dearpygui needs a display; only `--headless-check` works in a headless container.)

---

## Phase 1 — Fix the SPEED_HIST accel-freeze bug (prerequisite, its own change)

**The bug.** `can_decode.c:accel_update()` computes acceleration as the slope of wheel
speed against the **newest ring sample at least `CAN_DECODE_ACCEL_WINDOW_MS` (200 ms)
old**, then EMA-smooths it (`alpha 0.3`). The ring holds `CAN_DECODE_SPEED_HIST = 16`
samples — but the reference bus emits wheel speed (frame 0x102) at **~96.6 Hz, median
gap 10.0 ms** (measured over the committed capture: 21,254 frames / 220.1 s). Sixteen
samples span only ~150 ms < 200 ms, so once the ring wraps the "≥200 ms old" search
almost never succeeds and the derived accel **freezes**, updating only on rare >200 ms
frame gaps. Documented in the `tools/trc_viz.py` header (lines 49–56), which already
uses `SPEED_HIST = 32`.

**Sizing law:** ring depth ≥ `ACCEL_WINDOW_MS × wheel-frame rate` = 200 ms × ~0.1
frames/ms = 20 samples minimum; **32** gives ~310 ms of history — margin for jitter and
it keeps the power-of-two modulo cheap. History of the discovery: commits `f44b0ed`,
`bca0d14`, `6930e19` (see `chmbl-failure-archaeology`).

**The change (one line)** in `transmitter/software/main/can_decode.h` line 28:

```c
#define CAN_DECODE_SPEED_HIST      32u
```

This is firmware behavior change → per docs-are-spec, note it wherever the constant is
described (as of 2026-07-08 the only doc of the bug is the trc_viz header comment —
update that NOTE to past tense in the same change).

**Validation gate (run now, expect these exact numbers):**

```bash
cmake --build transmitter/software/test_host/build
python3 tools/golden_check.py
```

Expected (measured 2026-07-08 by building the patched decoder):

```
frames=315869 accel_mphps_min=-10.08 accel_mphps_max=25.83 wheel_speed_mph_last=0.00
PASS: 183944 signal values identical between the C decoder and cantools
```

- `PASS: 183944 ...` must be **unchanged** — SPEED_HIST does not touch bit extraction,
  and golden_check compares per-signal values only. If the PASS count changes or any
  MISMATCH appears → you changed more than the one constant; revert and re-diff.
- The accel range must open up from `-4.73..+4.00` to `-10.08..+25.83` (the derivative
  is now alive). Cross-check against the reference implementation with the wheel-speed
  low-pass disabled (the firmware has no pre-slope low-pass — see Phase 4):
  `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check --speed-smooth-ms 0`
  prints `accel range : -9.8 .. +24.7 mph/s`. Close but not bit-identical is expected:
  the C harness truncates timestamps to integer ms and reports per-frame extremes,
  while trc_viz uses float ms and resamples onto a 20 ms grid.
- **If accel range is still ~±4–5** → the rebuild picked up a stale binary; delete
  `transmitter/software/test_host/build` and rebuild.
- **If accel range is wildly larger (±50+)** → you probably also removed the EMA or
  changed the window; only the ring depth should change.

Land this as its own reviewed change (class: firmware code change → 5 CI build rows +
golden job; see `chmbl-change-control`). Do not bundle it with the FSM.

---

## Phase 2 — DECISION GATE: SMC code generation vs hand-coded FSM

The docs (firmware.md §4, DE-09 §5, `transmitter/software/README.md` lines 19–21)
specify: FSM structure lives in `transmitter/software/state_machine/brake_fsm.sm`,
compiled at build time by `java -jar tools/smc/Smc.jar -c` via a CMake pre-build step
(exact snippet in firmware.md §4). **Neither the `.sm` file nor `tools/smc/Smc.jar`
exists in the repo**, and SMC would be a **new build-time dependency (a JRE) on every
build host and all 5 CI rows** — which triggers the owner's written-trade-study rule.

**You may not silently pick either path.** Deviating from the documented SMC choice
requires a written trade study plus doc updates in the same change; following it
requires discharging the obligations below. Present this menu, with your
recommendation, to the project owner through change control, and record the outcome in
DE-09 §8 (and the trade study itself in the DE doc or a linked doc).

**Option A — follow the documented SMC route.** Obligations:

1. Obtain `Smc.jar` and vendor it under `tools/smc/` (firmware.md says "checked in").
   Verify the license permits redistribution in this repo (UNVERIFIED — check
   smc.sourceforge.net and the jar's bundled license before committing it; this repo is
   public and `chmbl-external-positioning` rules apply).
2. Write `state_machine/brake_fsm.sm` (states, single `Poll` event, guards, entry
   actions) — the `.sm` becomes the single source of truth; generated `brake_fsm_sm.[ch]`
   are **never committed**.
3. Wire the documented CMake snippet (firmware.md §4) into
   `transmitter/software/main/CMakeLists.txt` (register generated source, add include
   dir, `add_dependencies(${COMPONENT_LIB} brake_fsm_gen)`), and the same pre-build into
   `test_host/CMakeLists.txt` so host tests exercise the same generated code.
4. **MUST-VERIFY:** confirm a JRE exists inside the `espressif/esp-idf-ci-action@v1`
   (release-v5.3) container used by all 5 build rows in
   `.github/workflows/firmware-build.yml`, and in the golden job's ubuntu runner.
   UNVERIFIED as of 2026-07-08 — if absent, CI needs an extra install step in every row,
   widening the footprint of the dependency. (A JRE does exist in this dev container:
   OpenJDK 21.0.10.)

**Option B — hand-code the FSM as a pure C switch (mirroring `trc_viz.py:run_fsm`),
and retire the SMC decision in the docs in the same change.** Obligations:

1. Write the trade study (exemplar format: DE-04's LM3410 study) recording why SMC is
   being retired: 3 states, 1 event, 8 guards — the generated-code machinery (Java on
   every build host + CI, un-committed generated sources, a jar of unverified license in
   a public repo) outweighs the benefit for a machine this small; a reference
   implementation of the exact transition structure already exists and is validated
   (`run_fsm` in trc_viz.py); a plain `switch` is reviewable line-by-line against the
   DE-09 §4 table.
2. Update **in the same change**: firmware.md §4 (remove/replace the SMC section),
   DE-09 (§5 decomposition, the "specified in an SMC model" preamble),
   `transmitter/software/README.md` lines 19–21. No silent doc/code divergence.

**Recommendation (to surface, not to act on unilaterally): Option B.** Reasoning: the
FSM is far below the complexity where a generator pays for itself; option A adds a
toolchain dependency to 5 CI rows for zero validated benefit and has an unresolved
license/vendoring question; the project's own validation strategy (replay-metric
equivalence against trc_viz) works identically either way. But the SMC route is written
into three docs — this is exactly the "documented decision needs a written retirement"
case, so route the choice through `chmbl-change-control` and get the owner's call
before Phase 3. If the owner picks A, Phase 3's semantics section still applies — the
guards and timers live in the hand-written `BrakeFsmCtx` either way; only the
transition dispatch is generated.

---

## Phase 3 — Implement the pure core (`BrakeFsmCtx` + tick), exact DE-09 semantics

Create `transmitter/software/main/brake_fsm.c/.h` as a **pure, host-testable module**:
no ESP-IDF includes (follow `can_decode.[ch]`'s pattern — it is compiled by both the
IDF component and `test_host/CMakeLists.txt`). Inputs are plain values + a millisecond
clock; output is the state and the emitted `brake_state_t`.

**States and wire mapping** (protocol.h): `OFF` → `ST_OFF (0)`; `BRAKING` and `STOPPED`
both → `ST_BRAKE (2)`; `ST_DECEL (1)` is **reserved, never emitted**.

**Transition table — port EXACTLY (DE-09 §4; Poll at 50 Hz; first matching guard wins):**

| # | From | Guard | To |
|---|------|-------|----|
| 1 | `OFF` | decel > `decel_on_mphps` held continuously for `decel_on_debounce_ms` | `BRAKING` |
| 2 | `OFF` | `speed_mph` < `stop_speed_mph` | `STOPPED` |
| 3 | `BRAKING` | `speed_mph` < `stop_speed_mph` | `STOPPED` |
| 4 | `BRAKING` | accel > `accel_off_mphps` AND `speed_mph` > `accel_off_min_speed_mph` | `OFF` |
| 5 | `BRAKING` | \|accel\| < `steady_band_mphps` held for `steady_timeout_ms` | `OFF` |
| 6a | `STOPPED` | `speed_mph` > `moving_speed_mph` | `OFF` |
| 6b | `STOPPED` | clutch released AND in gear (gear ≠ 0/neutral) AND `speed_mph` > `stop_speed_mph` | `OFF` |
| 6c | `STOPPED` | stopped for > `stop_timeout_ms` | `OFF` |

**Tunables — implement the doc-table defaults (firmware.md `tx_config_t`), not
trc_viz's:**

| Parameter | Doc default | trc_viz `BrakeTunables` default (DIFFERS) |
|-----------|------------|-------------------------------------------|
| `decel_on_mphps` | **3.0** | 2.0 |
| `decel_on_debounce_ms` | **120** | 120 |
| `stop_speed_mph` | **1.0** | 1.0 |
| `moving_speed_mph` | **3.0** | 3.0 |
| `accel_off_mphps` | **0.5** | 0.5 |
| `accel_off_min_speed_mph` | **5.0** | 5.0 |
| `steady_band_mphps` | **0.5** | 0.75 |
| `steady_timeout_ms` | **2000** | 1500 |
| `stop_timeout_ms` | **60000** | 60000 |
| `state_min_dwell_ms` | **150** | 250 |

(trc_viz marks its divergent four as "open — default" bench values. Docs are the spec:
the firmware defaults come from the firmware.md table. Whenever you compare against
trc_viz, pass the doc values explicitly as CLI flags — every tunable is a flag, e.g.
`--decel-on-mphps 3.0`.)

**Semantics you must reproduce from `run_fsm` (trc_viz.py lines 277–344) — each of
these is a known correctness trap:**

1. **Condition timers accumulate BEFORE guards are evaluated**, every tick, even while
   the dwell floor is blocking transitions: `decel_hold` (+= dt while accel <
   −decel_on, else reset to 0), `steady_hold` (+= dt only while state==BRAKING and
   |accel| < band, else 0), `stopped_hold` (+= dt while state==STOPPED).
2. **Anti-strobe dwell gates dispatch, not logic**: if `since_transition <
   state_min_dwell_ms`, no transition is taken this tick (guards aren't even
   consulted), but the hold timers still accumulate. Initialize `since_transition`
   large so the first transition is allowed immediately.
3. **On any transition: reset `decel_hold`, `steady_hold`, `stopped_hold` to 0 and
   `since_transition` to 0.**
4. **Invalid/NaN accel disables rules 1, 4, 5** (the accel-consuming guards) — it must
   never count toward `decel_hold`. In firmware, "invalid" means
   `!can_sig_valid(&sig.accel, now)`.
5. **Fail honest (safety non-negotiable):** stale/invalid `wheel_speed` must make the
   machine unable to *assert* braking — do not enter `BRAKING`/`STOPPED` on stale
   speed, and define (and document) the behavior when speed goes stale mid-state.
   trc_viz does not model staleness; this is firmware-only logic — state it in the DE
   doc when you add it.
6. Guard order within a state is the table order (first match wins) — e.g. from
   `BRAKING`, stop-entry (rule 3) beats accel-off (rule 4).
7. The 6b "rolling" qualifier (`speed > stop_speed_mph`) is what prevents ping-pong
   against rule 2 while sitting in gear with clutch out — do not "simplify" it away.

Expose: `brake_fsm_init(ctx, const tunables*)`, a 50 Hz
`brake_fsm_poll(ctx, const inputs*, uint32_t now_ms)`, getters for state /
`brake_state_t` / active timers (the `state show` CLI needs them), and the guard
predicates named per DE-09 §5 (`isDecelExceeded`, `isStopped`, `isAcceleratingAway`,
`isSteadyElapsed`, `isMovingAwayFromStop`, `isClutchReleasedInGear`,
`isStopTimeoutElapsed`) so the code reviews 1:1 against the doc.

Gate for this phase: it compiles warning-clean under the host harness's
`-Wall -Wextra -Werror` (add the source to `test_host/CMakeLists.txt`), plus Phase 4.

---

## Phase 4 — Host validation gate: replay the ride capture through decode + FSM

Extend `transmitter/software/test_host/` with a second executable (e.g.
`fsm_replay.c`): parse the `.trc` exactly as `trc_replay.c` does, feed frames through
`can_decode_feed()` (now with `SPEED_HIST 32`), maintain a **20 ms poll grid** (fire
`brake_fsm_poll` for every 20 ms boundary crossed by the capture clock), and print at
minimum: transition count, light-on seconds, light-on fraction, accel min/max, and the
count of sub-0.5 s on-episodes ("blips").

**Reference numbers — and an honest reconciliation.** The DE-09/firmware.md dry-run
history says the tuned FSM reached "**30 transitions / 3 sub-0.5 s blips**" on this
capture (from 162/65 raw → 48/8 with hysteresis → 30/3 with debounce; commit
`8192663`). Those are **historical doc figures**. What the reference tool ACTUALLY
prints today (measured 2026-07-08) — note `--headless-check` prints **no blip metric at
all** (only duration, grid points, peaks, gears, accel range, FSM transitions, brake
light on):

| trc_viz run | transitions | light on |
|---|---|---|
| tool defaults | 29 | 89.0 s (40%) |
| doc-table defaults (flags: `--decel-on-mphps 3.0 --steady-band-mphps 0.5 --steady-timeout-ms 2000 --state-min-dwell-ms 150`) | 24 | 78.7 s (36%) |
| doc-table defaults + `--speed-smooth-ms 0` | **27** | **79.0 s** |
| tool defaults + `--speed-smooth-ms 0` | 37 | 89.7 s |

Counting blips via the tool's own `run_fsm`/`fsm_stats` functions with a small script
(sub-0.5 s on-episodes) gives **1** at both tunable sets today — the historical "3"
predates later tool changes (notably the pre-slope low-pass, commit `6930e19`). Treat
the doc's 30/3 as provenance, not as the gate. **The gate is agreement with the
reference implementation run under matching conditions, not with the historical
prose.**

**The matching condition that matters:** trc_viz applies a causal low-pass
(`speed_smooth_ms`, default 80 ms, commit `6930e19`) to wheel speed BEFORE the slope;
the firmware's `can_decode.c` has **no pre-slope low-pass** (only ring-slope + EMA). So
compare your C harness against `--speed-smooth-ms 0`:

**GATE:** `./fsm_replay logger/40mph_drive_cycle.trc` with doc-default tunables ≈
trc_viz `--headless-check --decel-on-mphps 3.0 --steady-band-mphps 0.5
--steady-timeout-ms 2000 --state-min-dwell-ms 150 --speed-smooth-ms 0`
→ expected **27 transitions, ~79.0 s on, accel range ≈ −9.8..+24.7** (the C side
measured −10.08..+25.83 per-frame; ±~1 mph/s at the extremes and ±1–2 transitions are
explainable by integer-ms truncation and grid-resampling differences — anything larger
is a porting bug). Judged by these **metrics only — never by eyeballing a dashboard.**

**If the numbers differ → branch, in this order:**

1. **Transitions ≈ 0, light never on, accel range ~±4** → `SPEED_HIST` still 16 in
   what you built (stale build dir, or Phase 1 not merged into your branch).
2. **Transitions way high (≥ ~45)** → dwell floor or debounce or hysteresis not
   working: check that `decel_hold` resets on transition, that dwell blocks dispatch,
   and that STOPPED exits at `moving_speed_mph` (3.0) not `stop_speed_mph`. (The
   162→48→30 history tells you which mechanism removes which excess.)
3. **Off by a handful with correct shape** → check the FSM poll grid is exactly 20 ms
   and driven by capture time (not per-frame), and that you compared against
   `--speed-smooth-ms 0`. If you compared against smoothing 80 ms, expect 24 not 27.
4. **Accel range matches but transitions don't** → guard order / timer-accumulation
   semantics (Phase 3 traps 1–3, 6); diff your tick against `run_fsm` line by line.
5. **Accel range wrong** → accel-port fidelity: window search must be "newest sample
   ≥ 200 ms old", EMA alpha 0.3, prime-then-EMA.

Record your harness's exact output — it becomes the accepted baseline the CI/host test
asserts (see `chmbl-validation-and-qa` §4b for how to wire it as a gate; get threshold
changes approved, never quietly re-baseline).

---

## Phase 5 — Firmware integration (tick task, CLI hooks, heartbeat)

Note: ESP-IDF is not installed in typical worker containers — you cannot run `idf.py`
locally; target-build claims are verified by CI (5 rows). Label them as such until CI
runs.

1. **Tick task (50 Hz):** create the FSM task (or extend `can_rx.c`'s ownership):
   every 20 ms, `sig_snapshot()` (source-aware — works for live CAN *and* `sig set`
   fakes, which is what makes the DE-09 isolation test possible), build the FSM inputs
   incl. validity via `can_sig_valid()`, call `brake_fsm_poll()`, and publish the
   mapped `brake_state_t`.
2. **Output publishing:** `net.c` **already** broadcasts `state_get()` in every
   heartbeat (`net.c` line 52) — the integration point exists. Today `state_get()`
   returns the stand-in forced by the `state` CLI command (`cmd_state.c`,
   `s_state`, GPIO indicator on `CONFIG_CHMBL_STATE_GPIO`: 8 on esp32c3, 2 on esp32).
   Rework `cmd_state.c` so the FSM drives the published state in `auto` mode and the
   CLI can still force it.
3. **CLI hooks — what exists vs must be added** (verified against `cmd_sig.c` /
   `cmd_state.c` 2026-07-08):

   | docs/cli.md §3 command | Status |
   |---|---|
   | `sig show` / `sig set <name\|alias> <value\|na>` / `sig set gear N` | EXISTS (`cmd_sig.c`) |
   | `sig ramp wheel <mph/s> [until <mph>]` (50 ms update period, auto-switches source to fake) | EXISTS |
   | `sig source can\|fake` | EXISTS |
   | `state` (bare = show) | EXISTS but shows only forced state + GPIO |
   | `state show` with state + emitted `brake_state_t` + derived `accel` + **active timers** | **ADD** (TX-CLI-3; DE-09 §6 requires timers/accel) |
   | `state force OFF\|BRAKE\|auto` | **ADD** — current `state off\|brake` has **no `auto`** (nothing to release to yet); keep `off\|brake` as force, add `auto` to hand control back to the FSM |
   | `can replay <name>` | Partial: only `can replay decel` exists (`cmd_can.c`) |
   | `config show/set/save` (runtime tunables) | NOT IMPLEMENTED anywhere — do not invent it; compile-time `tx_config_t` defaults are in scope, runtime config is a separate element (see `chmbl-config-and-flags`) |

4. **Build wiring:** add `brake_fsm.c` (and the tick task file) to
   `transmitter/software/main/CMakeLists.txt` `SRCS`. Mind the known traps: IDF 5.3
   provides `driver/twai.h` via the umbrella `driver` component (already in
   `PRIV_REQUIRES`); `-Werror=format` bites `printf` of floats/sizes on target
   (history: `4571558`) — cast to `double`/use correct specifiers as the existing
   `cmd_*.c` files do.
5. **Gate:** both transmitter targets (esp32c3, esp32) build in CI, all 5 matrix rows
   plus the `can-decode-golden` job green. If a target row fails on format warnings or
   missing component deps → `chmbl-debugging-playbook`.

---

## Phase 6 — Isolation acceptance (bench runbook) and promotion

Run DE-09 §7 acceptance on a bench transmitter (flash/console mechanics:
`chmbl-run-and-operate`). All input via `sig`, all observation via `state show` — no
bike, no radio needed (though the heartbeat will carry the state if a brake_light is
paired). Sequence (each line: command → EXPECTED):

```
sig source fake
sig set wheel 40                      → speed 40 mph, accel settles ≈ 0, state OFF
sig ramp wheel -5 until 0             → BRAKING within ~120 ms of decel>3 sustained (rule 1)
                                        → STOPPED when speed < 1.0 (rule 3); light stays on
sig set clutch 1 ; sig set gear 1     → still STOPPED (clutch held in gear)
sig set clutch 0                      → still STOPPED while speed ≤ 1.0 (6b needs rolling)
sig ramp wheel 2 until 2              → still STOPPED at 2 mph in-gear-clutch-out? NO —
                                        6b fires (rolling >1.0, clutch out, in gear) → OFF
state show                            → OFF, timers idle
sig ramp wheel 5 until 40             → stays OFF (accelerating)
sig ramp wheel -4 until 20            → BRAKING (rule 1)
sig ramp wheel 1 until 25             → OFF via rule 4 (accel>0.5 at speed>5)
sig ramp wheel -4 until 20 ; sig set wheel 20
                                      → BRAKING, then OFF after ~2000 ms steady (rule 5)
sig set gear N ; sig ramp wheel -5 until 0
                                      → STOPPED; creep test: sig ramp wheel 2 until 2 →
                                        holds STOPPED in neutral between 1.0 and 3.0 (hysteresis)
sig ramp wheel 4 until 4              → OFF at >3.0 (rule 6a)
sig set wheel 0 ; (wait 60 s)         → STOPPED → OFF at stop_timeout (rule 6c)
```

Also verify: no observed transition violates the 150 ms dwell; `state show`'s emitted
`brake_state_t` maps BRAKING/STOPPED→`ST_BRAKE`, OFF→`ST_OFF`; a constant `sig set
wheel` gives accel ≈ 0 (that's why `sig ramp` exists). Timing asserts at ~120 ms/2 s/
60 s are judged from `state show` timers and console timestamps — record them.

**Promotion (same change or same reviewed PR series):**

- `docs/design/README.md` §3: DE-09 row 🔲 → 🟢 (only when EVERY gate above passed).
- `docs/design/de-09-brake-decel-logic.md`: header status; §8 open items updated
  (tunable calibration status, SMC decision outcome); any semantics added during
  implementation (e.g. stale-speed behavior) written into §4/§5.
- `docs/firmware.md`: SMC section per the Phase 2 outcome; tunables table if any value
  changed (threshold changes need replay evidence — `chmbl-validation-and-qa` §5).
- `docs/roadmap.md` line 25 ("Host-side unit tests for the pure cores (state machine,
  …)") — now true for the FSM; update the near-term list.
- `transmitter/software/README.md` SMC paragraph per Phase 2 outcome.
- CI: all 5 build rows + `can-decode-golden` green; plus the new host FSM replay
  check if you wired it into the golden job.

---

## Fenced-off wrong paths (do not re-fight these — each has recorded WHY)

- **Replacing the decel-on debounce with a heavier low-pass filter.** Doc-recorded
  wrong trade (firmware.md, DE-09 §4 note): extra smoothing suppresses momentary
  throttle-dip spikes only by **delaying genuine hard-braking onset** — unacceptable
  for a safety light. The debounce rejects dips with a bounded ≤120 ms penalty; 120 ms
  is the measured knee (larger values clipped real brake taps).
- **Dropping the STOPPED hysteresis in favor of the anti-strobe dwell alone.** The
  guards **genuinely oscillate** across a single threshold in low-speed creep;
  rate-limiting just slows the strobe. Evidence: hysteresis cut transitions 162→48 on
  the ride log. Keep both mechanisms; they solve different problems.
- **Tuning thresholds by eyeballing the trc_viz dashboard.** Metrics only: transition
  count, blip count, on-time from replay runs, with the tunables stated. The GUI is for
  forming hypotheses, never for accepting them (`chmbl-research-methodology`).
- **Transmitting on the motorcycle CAN bus "for testing".** Never. Listen-only is an
  architecture invariant and a safety/legal line (`chmbl-architecture-contract`). Fake
  inputs via `sig set`/`sig ramp`/`can replay` — that is exactly what they are for, and
  none of them touch the bus (there is no TX path).
- **Hand-editing `transmitter/software/main/bike_profile_triumph_tr.c`.** Generated
  from the DBC by `tools/gen_profile.py`; CI's staleness gate will fail the diff.
  Change the DBC, regenerate.
- **Silently skipping (or silently taking) the SMC decision.** Both directions violate
  doctrine: implementing SMC without the license/CI verification, or hand-coding
  without the trade study + doc retirement, is a docs-are-spec breach. Phase 2 is a
  mandatory gate.
- **IMU/inertial detection or brake-wire tap as an "easier" braking source.** Patent-
  fenced and settled (`chmbl-failure-archaeology`); deceleration comes from the bike's
  own CAN wheel speed, full stop.
- **Flashing/strobing output patterns.** Illegal in many jurisdictions; the min-dwell
  floor exists to prevent them. Nothing in the FSM may emit an intentionally blinking
  brake indication.

---

## Validation-and-promotion protocol — "you are done when"

Route every landing through `chmbl-change-control` (change classes, same-change doc
rule, review gates) with evidence per `chmbl-validation-and-qa`. Recommended change
sequence: (1) SPEED_HIST fix, (2) SMC decision + trade study/doc update, (3) FSM core +
host replay harness, (4) firmware integration + CLI, (5) promotion/doc flip — each
independently CI-green.

**You are DONE when all of the following are true and recorded:**

1. `grep CAN_DECODE_SPEED_HIST transmitter/software/main/can_decode.h` → `32u`, and
   `python3 tools/golden_check.py` prints `PASS: 183944 ...` with harness stderr
   `accel_mphps_min=-10.08 accel_mphps_max=25.83` on the committed capture.
2. The SMC decision has a written owner-approved record; docs and repo agree (either
   `tools/smc/Smc.jar` + `.sm` + CMake step exist and CI proves the JRE, or the SMC
   sections are retired with a trade study).
3. A host FSM replay of `logger/40mph_drive_cycle.trc` at doc-default tunables
   reproduces the reference implementation's numbers under matching conditions
   (expected ~27 transitions / ~79 s on vs trc_viz `--speed-smooth-ms 0` at doc
   tunables; exact accepted baseline recorded in the test).
4. All 5 CI build rows + the golden job are green on the final branch.
5. The Phase 6 bench runbook passed on hardware with timings recorded (≤120 ms
   debounce behavior, 2 s steady, 60 s stop-timeout, hysteresis hold, dwell floor).
6. DE table row = 🟢 and every doc named in Phase 6 promotion was updated in the same
   change series; no doc/code divergence remains.

If any item is unmet, DE-09 stays 🟡 at best — never claim 🟢 (no-oversell rule).

---

## When NOT to use this skill

- Environment/toolchain setup or a build that won't configure → `chmbl-build-and-env`.
- Flashing, consoles, pairing, capture handling → `chmbl-run-and-operate`.
- CAN/DBC/.trc/ESP-NOW fundamentals → `chmbl-can-reference`.
- What gates a change / merge blockers / trade-study format → `chmbl-change-control`.
- Evidence tiers, adding tests/CI rows in general → `chmbl-validation-and-qa`.
- Diagnosing failures unrelated to DE-09 → `chmbl-debugging-playbook`; settled history
  → `chmbl-failure-archaeology`.
- Kconfig/tunable axes beyond this campaign → `chmbl-config-and-flags`.
- Architecture rationale (why listen-only, why no IMU) → `chmbl-architecture-contract`.

## Provenance and maintenance

All facts verified 2026-07-08 on branch `claude/skill-library-continuity-mib7ua`
(HEAD `30f0b15`). Volatile facts and their re-verification commands:

- SPEED_HIST value: `grep -n CAN_DECODE_SPEED_HIST transmitter/software/main/can_decode.h`
- SMC absence: `ls tools/smc transmitter/software/state_machine`
- DE-09 status: `grep -n "DE-09" docs/design/README.md docs/design/de-09-brake-decel-logic.md | head -3`
- Baseline golden numbers: `cmake --build transmitter/software/test_host/build && python3 tools/golden_check.py` (expect `PASS: 183944`; stderr accel range −4.73..+4.00 pre-fix, −10.08..+25.83 post-fix)
- Reference FSM numbers: `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check` (29 transitions / 89.0 s at tool defaults) and the doc-tunable/no-smooth variants tabulated in Phase 4
- Wheel-speed rate (~96.6 Hz, 10 ms median gap): recount 0x102 timestamps in `logger/40mph_drive_cycle.trc`
- Doc-vs-trc_viz tunable defaults: `grep -A12 "class BrakeTunables" tools/trc_viz.py` vs the firmware.md tunables table
- CLI implemented-vs-spec: read `transmitter/software/main/cmd_sig.c` / `cmd_state.c` / `cmd_can.c` against `docs/cli.md` §3
- CI shape (5 rows + golden job): `.github/workflows/firmware-build.yml`
- Heartbeat integration point: `grep -n state_get transmitter/software/main/net.c`
- JRE in this container: `java -version` (OpenJDK 21.0.10); JRE in the CI action container: UNVERIFIED — check before choosing Phase 2 option A.
