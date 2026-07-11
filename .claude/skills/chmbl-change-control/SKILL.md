---
name: chmbl-change-control
description: >
  Load this skill BEFORE making, reviewing, or merging ANY change to the open-chmbl
  repo — code, docs, DBC/profile, tunables, tools, or CI. It defines how changes are
  classified and gated, the safety non-negotiables that act as merge blockers (with
  the rationale and history behind each), the docs-are-spec and trade-study doctrine,
  the generated-file and DBC-ground-truth rules, and the design-element sequencing
  law. Do NOT load it for how-to-build/flash questions (chmbl-build-and-env,
  chmbl-run-and-operate), for debugging a failure (chmbl-debugging-playbook), or for
  what evidence counts as validation (chmbl-validation-and-qa) — this skill only
  covers WHAT must be true before a change may land.
---

# open-chmbl change control

open-chmbl is an open-source helmet-mounted auxiliary brake light: a bike-side ESP32-C3
"transmitter" listens (never transmits) on a motorcycle's CAN bus (Controller Area
Network — the vehicle's internal message bus), infers braking from wheel-speed-derived
deceleration, and radios a brake state over ESP-NOW (Espressif's connectionless 2.4 GHz
protocol) to a helmet-worn LED bar. Because the device rides on a life-saving piece of
equipment and interacts with a safety-critical vehicle, **change control here is a
safety mechanism, not bureaucracy**. This skill is the gatekeeper's manual.

All facts verified against the repo as of 2026-07-07 (branch
`claude/skill-library-continuity-mib7ua`). Re-verification commands are at the end.

---

## 1. Change classes and their gates

CI is defined in `.github/workflows/firmware-build.yml`. It runs on push and PR **only
when the change touches** `brake_light/software/**`, `logger/software/**`,
`transmitter/software/**`, `profiles/**`, `tools/**`, or the workflow file itself.
Two job groups exist:

- **`build` matrix (5 rows):** ESP-IDF `release-v5.3` compiles via
  `espressif/esp-idf-ci-action@v1` for brake_light×{esp32c3, esp32},
  logger×esp32, transmitter×{esp32c3, esp32}. (esp32c3 is the product target;
  classic esp32 is interim dev hardware — both are kept building deliberately.)
- **`can-decode-golden` (host):** Python 3.12 + `pip install -r tools/requirements.txt`,
  then three gates in sequence: (1) regenerate the bike profile from the DBC and
  `diff` it against the committed file (**staleness gate**); (2) cmake-build the host
  decode harness `transmitter/software/test_host/`; (3) `python3 tools/golden_check.py`
  — replays the committed ride capture `logger/40mph_drive_cycle.trc` through both the
  C decoder and python-cantools and asserts agreement (tolerances `REL_TOL=1e-4`,
  `ABS_TOL=1e-3` in `tools/golden_check.py`).

Classify every change into exactly one primary class and satisfy its full row:

| Class | Examples | CI jobs that must pass | Doc that MUST change in the same commit/PR | Replay/metric evidence required? |
|---|---|---|---|---|
| **Docs-only** | Fix wording in `docs/firmware.md`; add a DE doc; update a status emoji | **None run** (docs paths don't trigger CI) — gate is human review | The doc *is* the change; keep `docs/design/README.md` §3 status table consistent | No — but a doc that changes specified behavior obligates a follow-up code change (see §2a) |
| **Design-element implementation** | Implementing DE-02 auto-brightness; adding a firmware task | Full 5-row `build` matrix (both targets for the touched firmware) | The element's `docs/design/de-*.md` (I/O, tasks, CLI hooks) and the status table in `docs/design/README.md` §3 | Yes — the DE doc's **§7 Isolation acceptance** demonstrated via the serial CLI before the element is called done |
| **Decode / profile change** | Editing `profiles/triumph_tr.dbc`; changing `tools/gen_profile.py`; touching `transmitter/software/main/can_decode.c/.h` | `can-decode-golden` (staleness diff + host harness build + golden_check) **plus** the transmitter build rows | `docs/can-profiles.md` §5 decode table, and `docs/design/de-08-can-decode.md` if the architecture moves | Yes — golden_check over `logger/40mph_drive_cycle.trc` is mandatory and automated; a DBC edit also needs its empirical justification recorded in can-profiles.md |
| **FSM-tunable change** | Changing `DECEL_ON_MPHPS`, `DECEL_ON_DEBOUNCE_MS`, `STATE_MIN_DWELL_MS`, etc. | Transmitter build rows once DE-09 is in firmware (as of 2026-07-07 the FSM is design-only, status 🔲 — tunables live in the `docs/firmware.md` table and in `tools/trc_viz.py`) | The tunables table in `docs/firmware.md` — value AND rationale | Yes — a before/after replay metric on the committed capture (transition count from `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check`; sub-0.5 s blip count from the `fsm_metrics.py` script shipped with `chmbl-diagnostics-and-tooling` — headless-check does not print blips); precedent: hysteresis cut transitions 162→48, debounce cut 48→30 (historical figures; operative gate owned by `chmbl-de09-campaign` Phase 4) |
| **Tooling change** | Editing `tools/gen_profile.py`, `tools/golden_check.py`, `tools/trc_viz.py`, `tools/requirements.txt` | Everything — `tools/**` is in the CI path filter, so the 5-row matrix AND `can-decode-golden` run | `docs/cli.md` / the tool's own doc section if flags or behavior change | Yes if the tool produces evidence others rely on: golden_check must still pass; gen_profile output must still diff-match the committed profile |
| **CI change** | Editing `.github/workflows/firmware-build.yml` | All jobs (the workflow file is in its own trigger paths) | A note in the relevant doc if a gate is added/renamed; **never remove a gate or matrix row without a doc explaining why** | N/A — but a CI change that weakens a gate is itself a change-control violation absent written rationale |

Rules that cut across classes:

- A change touching multiple classes must satisfy **every** applicable row (e.g. a DBC
  edit that also retunes the FSM needs golden_check AND the replay metric AND both docs).
- "Same change" means same commit or same PR — never "I'll update the doc later."
- CI does not run on docs-only changes, so **doc/code consistency is enforced by
  discipline, not automation**. That is exactly why rule §2a exists.

---

## 2. The two unwritten rules (owner doctrine — treat as law)

### 2a. Docs are the spec — code follows

Any behavior change updates the relevant DE doc and/or `docs/firmware.md` **in the same
change**. No silent doc/code divergence, ever, in either direction.

- **Rationale:** the docs of record (`docs/firmware.md`, `docs/design/de-*.md`,
  `docs/can-profiles.md`, `docs/protocol.md`) are what a zero-context builder or a
  future AI session reads first. If they drift from the code, every downstream
  decision — tuning, safety review, a second bike profile — is made against fiction.
  The project's stated bar is "a complete, documented, legally-careful, buildable open
  product"; documentation *is* the product.
- **When code and docs disagree today**, state the gap honestly rather than papering
  over it. Live examples (as of 2026-07-07): `docs/firmware.md` §4 describes an SMC
  (State Machine Compiler — a Java tool that generates C from a `.sm` state-machine
  model) pre-build step and `transmitter/software/state_machine/brake_fsm.sm`, but
  neither the `.sm` file nor `tools/smc/Smc.jar` exists yet — DE-09 is 🔲
  unimplemented. That is *documented forward intent*, which is allowed; **undocumented
  implemented behavior is not.**

### 2b. No new dependencies or frameworks without a written trade study

Adding a library, a hardware part class, a build tool, or a framework requires a
written comparison **before** the change lands. The exemplar to imitate is the
**LM3410 LED-driver trade study in `docs/design/de-04-led-render.md` §3.3**. Copy its
shape:

1. **Requirements ranked for THIS application** — DE-04 lists 8, starting with a hard
   gate (Vin reaches down to ~2.8 V so the whole 1S battery discharge is usable).
2. **A candidates table** with the deciding columns (part, vendor, topology, key specs,
   package, price, sourcing, verdict).
3. **Rejections with reasons** — DE-04 rejects the MAX16833 ("Vin minimum 5 V… it
   physically cannot run from a 1S cell"), and TPS61500/LT3518 for Vin-min brushing the
   1S cutoff.
4. **A conclusion naming the pick AND the trade-offs accepted** — DE-04 designs in the
   LM3410 and explicitly accepts its 24 V output ceiling, non-synchronous efficiency
   loss, and moderate dimming ratio, with the mitigation for each.
5. **Named fallbacks with the trigger condition** — DIO5661 as the budget alternate
   (gated on datasheet confirmation), LT3922-1 as the premium upgrade (gated on
   Phase-4 EMI measurements).

A software analogue already exists: `docs/design/de-08-can-decode.md` §3a weighs three
decode architectures (fully hand-coded vs. `cantools generate_c_source` vs. the chosen
DBC-plus-generated-data-table hybrid) with rationale and a named escape hatch. A PR
that adds `some_new_lib` to `tools/requirements.txt` or a new ESP-IDF component
dependency without this kind of study should be sent back.

---

## 3. Safety non-negotiables as change-control gates

Each of these is a **merge blocker**. Source of record: `docs/safety-regulatory.md`.
For each: what it is, WHY, and what kind of change would violate it. (The same
rules appear as design-review invariants in `chmbl-architecture-contract` §3 —
if you amend a rule here, update the matching invariant there in the same
change, and vice versa.)

| Gate | Rule | Why (rationale) | A change that VIOLATES it |
|---|---|---|---|
| **Listen-only CAN** | The TWAI controller (Two-Wire Automotive Interface — ESP32's CAN peripheral) runs in silent mode: no ACK, no transmit, ever, on the bike bus (`docs/safety-regulatory.md` §2) | Transmitting or even ACKing could perturb the motorcycle's ECUs — a safety-critical vehicle we must never influence. Being a pure listener is also the project's entire legal/ethical posture toward the bike | Any code that puts TWAI in normal mode, sends a diagnostic request/response, "just probes" an ID, or wires the TX line as an output. Even a request/response *option* is out — the reference decode is confirmed free-running broadcast precisely so this never becomes tempting |
| **No strobing / flashing** | All light patterns are steady; a global `STATE_MIN_DWELL_MS` (150 ms) anti-strobe floor rate-limits every state transition; the light is binary on/off (`ST_DECEL` is reserved in the protocol but never emitted) (`docs/safety-regulatory.md` §1, `docs/firmware.md` tunables) | Flashing/strobing stop lamps are illegal in many US states and under ECE rules; a flashing tier would make the whole device unlawful to ride with | Adding a "pulse on hard braking" render mode; lowering/removing `STATE_MIN_DWELL_MS`; emitting `ST_DECEL` as a distinct flashing pattern; any PWM change that moves brightness dimming below flicker-fusion frequency (dimming PWM must stay ~200 Hz–1 kHz — brightness control, never a visible pattern) |
| **Fail honest** | Link loss → a *distinct* indication (steady running light + slow fault blink), never silent-dark and never a latched fake `BRAKE`; stale/invalid `wheel_speed` → the state machine cannot assert braking; internal fault → watchdog reset, not a latched state (`docs/safety-regulatory.md` §2, §4) | Silent-dark means rider and traffic trust a dead device. A latched fake BRAKE "cries wolf" and trains following traffic to ignore the light — worse than no light. The absence-of-heartbeat model makes missing packets the fault signal | Making the light "hold last state" on link loss; asserting BRAKE from a signal whose staleness flag is set; removing per-signal validity/staleness checks from the decoder; disabling the watchdog to "stop annoying resets" |
| **No helmet penetration** | Never drill, screw, or otherwise penetrate the helmet shell; mounts are non-penetrating (adhesive/strap, VHB tape only) and must shear/break away under impact (`docs/safety-regulatory.md` §3) | Penetrating the shell compromises its protective function and voids DOT/ECE certification; a rigid snag point transmits crash load into the head/neck | Any mount design, doc, or BOM item involving fasteners through the shell; a rigid hook/protrusion; a mount whose hold force exceeds the shear-release threshold |
| **Patent avoidance** | No brake-light-wire tap; no IMU/inertial (accelerometer/gyro) brake detection. Deceleration comes only from the bike's own CAN wheel-speed signal (`docs/safety-regulatory.md` §5) | Both approaches are covered by existing patents the project deliberately avoids; the wire tap additionally modifies bike wiring. This constraint is why the whole DE-09 FSM exists — commit `5c094a5` is the pivot that redesigned braking around wheel-speed decel after confirming the bus has no brake-switch bit | Adding an accelerometer to the helmet unit *for braking* (a future IMU is permitted for diagnostics only, e.g. fallen-helmet detection — never to assert braking); any harness/doc describing a tap on the brake-lamp circuit; "fusing" IMU data into the brake decision |

A sixth functional-safety gate worth checking on power-related changes: **parasitic
draw < 1 mA when the bike is off** (`docs/safety-regulatory.md` §2) — the transmitter
must never flatten the motorcycle battery. A change that keeps the radio or CAN
transceiver awake on an always-on diagnostic port violates it.

**No skill, doc, or PR may describe a way to route around these gates.** If a proposed
feature needs one of them relaxed, it is rejected, not gated.

---

## 4. Generated files and ground truth

### 4a. `bike_profile_triumph_tr.c` is generated — never hand-edit

`transmitter/software/main/bike_profile_triumph_tr.c` is **generated and committed**
output of `tools/gen_profile.py` parsing `profiles/triumph_tr.dbc`. The CI
`can-decode-golden` job regenerates it and `diff`s against the committed file; any
hand edit or forgotten regeneration fails CI (the **staleness gate**). To change the
profile:

```bash
# 1. Edit the DBC (with empirical justification recorded in docs/can-profiles.md §5)
# 2. Regenerate exactly as CI does:
python3 tools/gen_profile.py profiles/triumph_tr.dbc \
  --name "Triumph Speed 400 / Scrambler 400X (TR-series)" \
  --bitrate 500000 \
  --symbol bike_profile_triumph_tr \
  --out transmitter/software/main/bike_profile_triumph_tr.c
# 3. Commit DBC + regenerated .c together; 4. golden_check must still pass
```

### 4b. The DBC is the ground truth

`profiles/triumph_tr.dbc` (DBC = the standard CAN database format describing message
IDs, signal bit layouts, scales, offsets) is the **single machine-readable ground
truth** for the bike's CAN layout (`docs/can-profiles.md`, `docs/design/de-08-can-decode.md`
§3a). Both the firmware decoder and the offline python-cantools validation path decode
from the same DBC — that is the whole point of the architecture: it eliminates
hand-transcription drift and silent bit-extraction bugs. Consequences:

- A decode fix goes into the **DBC**, never directly into the generated C or into
  per-signal special cases in `can_decode.c`.
- The prose decode table in `docs/can-profiles.md` §5 must be updated to match
  (docs-are-spec applies), including the evidence for the new interpretation.
- `gen_profile.py` errors on unmapped DBC signals by design — a new signal must be
  added to its `FIELD_MAP`, which is a tooling change (trade-study rule does not apply,
  but the golden gate does).

---

## 5. Design-element process — the change-sequencing law

The project deliberately has no shall-statement requirements. Instead
(`docs/design/README.md` §1): **feature-function lists → design documents → isolated
implementation → integration.** FFL IDs (feature-function list IDs like `TX-SM-1`,
`BL-RND-2` in `docs/feature-functions.md`) are the capability baseline; each design
element (DE-00…DE-10) takes a small set of FFL IDs into a buildable design.

The sequencing rules, as change control:

1. **One element at a time.** Do not open work on a second element while one is
   mid-implementation. The build order in the status table encodes real dependencies
   (CLI first so everything is testable; CAN-dependent elements last, after captures
   exist).
2. **Isolation before integration.** Inputs at the element's boundary are **faked**
   via the serial developer CLI (`docs/cli.md`) and outputs **viewed** via the CLI;
   the element is done only when its DE doc's §7 "Isolation acceptance" criteria are
   demonstrable in isolation. Only independently proven elements get integrated.
3. **The status table in `docs/design/README.md` §3 is the single source of truth for
   what's next** (the doc says this verbatim: "This table is the single source of
   truth for 'what's the next isolated piece.'"). Statuses: 🔲 not started ·
   🟡 in design · 🟢 implemented. A change that implements, starts, or de-scopes an
   element must update this table in the same change. As of 2026-07-07: DE-01 is 🟢;
   DE-00/03/04/07 are 🟡; DE-02/05/06/08/09/10 are 🔲 per the table — **but note a
   live inconsistency**: the table shows DE-08 as 🔲 while `de-08-can-decode.md`'s own
   header says "🟢 implemented" (and the code, host harness, and CI golden job exist).
   This is exactly the drift this rule forbids; whichever change touches DE-08 next
   should reconcile the table.
4. **Explorations are not commitments.** Ideas in `docs/design/explorations/` get
   promoted to a `de-*` element (with FFL traceability and an isolation test) only
   when scheduled — implementing an exploration directly skips the process.
5. Every new `de-*.md` follows the 8-section template in `docs/design/README.md` §2
   (scope/isolation boundary, FFL traceability, component selection, I/O, firmware
   decomposition, CLI hooks, isolation acceptance, open items).

---

## 6. Pre-merge checklist

Run through this for every change; every "no" needs a written justification in the PR.

- [ ] **Classified** the change against the §1 table; all applicable rows satisfied.
- [ ] **CI green** for the jobs the class requires (5-row build matrix and/or
      `can-decode-golden`) — and if the change touched a docs-only path, confirmed no
      code behavior changed without its doc.
- [ ] **Docs updated in the same change** (docs-are-spec): DE doc, `docs/firmware.md`
      tunables table, `docs/can-profiles.md` decode table, status table — whichever
      the class demands.
- [ ] **No new dependency/framework** — or a written trade study in the DE-04 §3.3 /
      DE-08 §3a mold accompanies it (ranked requirements, candidates table, rejections
      with reasons, accepted trade-offs, fallbacks with triggers).
- [ ] **Safety gates untouched:** still listen-only CAN; no strobing (min-dwell floor
      intact, steady patterns only); still fails honest (no silent-dark, no latched
      BRAKE, staleness blocks braking assertion); no helmet penetration; no brake-wire
      tap or IMU-based braking; parked draw target < 1 mA unharmed.
- [ ] **Generated file discipline:** `bike_profile_triumph_tr.c` untouched by hand;
      if the DBC changed, the file was regenerated with the exact CI command and
      committed together, and golden_check passes.
- [ ] **Sequencing respected:** work is on the current element per the
      `docs/design/README.md` §3 table; isolation acceptance shown via CLI before any
      integration claim; status emoji updated.
- [ ] **Evidence attached** where the class requires it: golden_check output, replay
      metrics (e.g. `trc_viz.py --headless-check` transition/blip counts) for tunable
      changes, CLI transcripts for isolation acceptance.
- [ ] **Honest labeling:** anything unproven stays marked open/candidate/planned; no
      claim contradicts `docs/safety-regulatory.md`; known doc-vs-repo gaps stated,
      not papered over.

---

## When NOT to use this skill

- **Actually building/flashing or setting up the environment** → `chmbl-build-and-env`,
  `chmbl-run-and-operate`.
- **A build/CAN/link/tool failure to triage** → `chmbl-debugging-playbook`; the history
  behind settled dead ends → `chmbl-failure-archaeology`.
- **What evidence counts and how to add tests/CI rows** (mechanics, not gates) →
  `chmbl-validation-and-qa`; interpreting tool output → `chmbl-diagnostics-and-tooling`.
- **Why the architecture is shaped this way** → `chmbl-architecture-contract`;
  doc-writing conventions and the DE template's house style → `chmbl-docs-and-writing`.
- **Executing the DE-09 implementation itself** → `chmbl-de09-campaign` (this skill
  only tells you which gates that campaign must clear).
- **Patent/legal positioning detail** → `chmbl-external-positioning` (this skill only
  states the resulting change gates).

---

## Provenance and maintenance

All claims verified 2026-07-07. Re-verify before relying on:

| Fact class | Re-verification command |
|---|---|
| CI jobs, matrix rows, trigger paths, gate steps | `cat .github/workflows/firmware-build.yml` |
| Golden test tolerances | `grep -n "REL_TOL\|ABS_TOL" tools/golden_check.py` |
| gen_profile regeneration command + staleness rule | `sed -n '1,25p' tools/gen_profile.py` and the "not stale" step in the workflow |
| DE status table / build order / "single source of truth" | `sed -n '52,80p' docs/design/README.md` |
| Safety non-negotiables wording | `cat docs/safety-regulatory.md` |
| FSM tunables and defaults (incl. `STATE_MIN_DWELL_MS` 150 ms) | `grep -n "MIN_DWELL\|DECEL_ON\|Tunables" docs/firmware.md` |
| Trade-study exemplars | `docs/design/de-04-led-render.md` §3.3; `docs/design/de-08-can-decode.md` §3a |
| SMC step still unimplemented (DE-09 🔲) | `ls transmitter/software/state_machine tools/smc 2>&1` (expect "No such file") |
| trc_viz headless mode exists | `grep -n "headless-check" tools/trc_viz.py` |
| Braking-pivot commit | `git log --format='%h %s' 5c094a5 -1` |
