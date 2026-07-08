---
name: chmbl-external-positioning
description: >
  Load this skill whenever you are writing or reviewing ANYTHING externally visible
  about open-chmbl — README/docs edits that make claims, release notes, blog posts or
  writeups, GitHub issue/PR text aimed at outside contributors, comparisons to
  commercial products, or any statement about patents, legality, "what works", or
  reproducibility. It defines the two patent families the design routes around, the
  exact positioning claim, the legal/regulatory envelope for public statements, the
  honesty ledger (PROVEN / VALIDATED-OFFLINE-ONLY / DESIGNED-ONLY / ASPIRATIONAL and
  the never-upgrade-a-tier rule), and the release reproducibility standard. Do NOT
  load it for internal engineering work — building, debugging, testing, or tuning —
  those are covered by chmbl-build-and-env, chmbl-debugging-playbook,
  chmbl-validation-and-qa, and chmbl-de09-campaign.
---

# open-chmbl external positioning: patents, legality, honesty tiers, reproducibility

This skill governs **what this project may say about itself in public** and what it
must prove first. The project's "beyond state of the art" target is not a novel
algorithm — it is a **complete, documented, legally-careful, buildable open product**.
Every external claim is judged against that bar.

Glossary for zero-context readers (terms used below):

| Term | Meaning here |
|------|--------------|
| CAN | Controller Area Network — the motorcycle's internal message bus, exposed on the diagnostic port |
| TWAI | Espressif's name for the ESP32's CAN controller; "listen-only mode" = never ACKs or transmits |
| DBC | A standard text file format describing CAN frame/signal layouts (`profiles/triumph_tr.dbc` is this project's ground truth) |
| `.trc` | PCAN trace file — a timestamped raw CAN capture (`logger/40mph_drive_cycle.trc`) |
| ESP-NOW | Espressif's connectionless encrypted 2.4 GHz radio protocol linking bike unit to helmet unit |
| FSM | Finite state machine — here, the OFF/BRAKING/STOPPED braking logic (design element DE-09) |
| IMU | Inertial measurement unit (accelerometer/gyro) — explicitly NOT used, see patents below |
| DOT / ECE | US and European helmet/vehicle-lighting certification regimes |
| DE-xx | A "design element" — one isolated slice of the design, tracked in `docs/design/README.md` §3 |

---

## 1. The two patent families and the positioning claim

Source of record: `ARCHITECTURE.md` §1 and `docs/safety-regulatory.md` §5. Two
existing patent families fence off the two obvious ways to build a helmet brake
light:

1. **Brake-lamp-circuit tap** — galvanically or optically tapping the motorcycle's
   stop-lamp wiring to detect the brake being applied.
2. **Inertial/IMU detection** — an accelerometer/gyro on the helmet or light
   inferring deceleration.

open-chmbl deliberately uses **neither**. The exact positioning claim — use this
wording shape, not looser paraphrases:

> Braking is inferred from **deceleration derived from the bike's own CAN
> wheel-speed data**, read via a **listen-only** (never ACK, never transmit) tap on
> the Euro 5 diagnostic port. No brake-wire tap. No accelerometer/gyro. No
> modification to bike wiring.

Two mandatory qualifiers on every public use of this claim:

- **This is design-around positioning by the project, NOT legal advice.** The repo
  does not identify patent numbers, does not contain a freedom-to-operate analysis,
  and no one on this project is qualified to render one. Any public statement of the
  form "this design does not infringe" or "patent-cleared" **requires patent
  counsel first**. Permitted phrasing: "designed to avoid the approaches used by
  existing patent families"; forbidden phrasing: "patent-free", "non-infringing",
  "legally clear of patents".
- The design-around is load-bearing engineering doctrine, not just marketing: it is
  why the brief and all sibling skills forbid describing IMU-based detection or
  brake-wire taps even as fallbacks. Commit `5c094a5` ("Redesign braking logic
  around wheel-speed deceleration (no brake switch)") is the pivot where this became
  the architecture, after captures confirmed the reference bike publishes **no
  brake-switch bit** on the bus.

Note the corollary in `docs/safety-regulatory.md` §5: a future IMU, if ever added,
would be diagnostics-only (e.g. fallen-helmet detection) — **never** to assert
braking. Do not soften this in public text.

---

## 2. Legal/regulatory envelope for public docs and releases

Source of record: `docs/safety-regulatory.md` §1 (legal), §2 (functional safety),
§3 (helmet). Every public artifact — README, release notes, writeup, video
description — must stay inside this envelope:

| Rule | Required public framing |
|------|------------------------|
| **Auxiliary, never a replacement** | The device never substitutes for the factory brake light, which remains the legally required device. Say so explicitly. |
| **Helmet-attachment legality varies by jurisdiction** | Attaching anything to a certified helmet can **void DOT/ECE certification**; some jurisdictions restrict or ban helmet-mounted lighting outright. Never state or imply it is road-legal anywhere without citing that jurisdiction's rule. |
| **Default framing until confirmed legal** | "Treat this project as **track / off-road / educational** until you have confirmed legality for your use." This sentence (or equivalent) belongs in every release and top-level doc — it is already in `ARCHITECTURE.md`'s lead warning block. |
| **No flashing/strobing** | Flashing stop lamps are illegal in many US states and under ECE rules. Public docs must state the light is steady, binary on/off, gated by a global anti-strobe minimum-dwell floor (`STATE_MIN_DWELL_MS`, 150 ms default); there is no flashing tier (protocol `DECEL` state is reserved, never emitted). |
| **Red to the rear only** | Rear lighting is red; red must not be visible to the front. |
| **Inferred, not measured, braking** | Public text must disclose the light reflects wheel-speed deceleration, not lever state — hard engine-braking lights it; gentle trail-braking may not (`docs/safety-regulatory.md` §1). |
| **Builder assumes liability** | This is a documentation **duty**, not a footnote: a device that influences following traffic carries real liability, this is not a certified product, and every release must state clearly that builders assume responsibility. |
| **Never drill a helmet** | Non-penetrating mounts only (adhesive/strap, breakaway). Any published mounting instructions must repeat this. |

Also in the envelope for anything describing the bike-side unit: CAN is strictly
listen-only (a safety rule, not just a patent posture), and parasitic draw target
< 1 mA when parked.

**Open gap (verified 2026-07-07): the repo has no LICENSE file.** For an "open
product" this is a release blocker — do not describe the project publicly as
"open-source" under a specific license until one is committed. Flag it, don't
paper over it.

---

## 3. The honesty ledger — claim tiers

**The rule: never publish a claim from a lower tier as if it were a higher one.**
"Designed" is not "implemented"; "validated offline on one log" is not "validated";
"target" is not "measured". Each entry below was verified against the repo on
2026-07-07 (branch `claude/skill-library-continuity-mib7ua`); re-check before
publishing (commands in §6).

### Tier 1 — PROVEN (empirically demonstrated, evidence committed)

| Claim | Evidence |
|-------|----------|
| The reference bike (Triumph Speed 400) diagnostic port carries **free-running broadcast** CAN at 500 kbit/s, readable listen-only | `docs/can-profiles.md` §5 (marked ✅ resolved); the committed ride capture itself |
| There is **no brake-switch bit** on the reference bus | Repeated captures while working the brake found none — `docs/can-profiles.md` §5, `ARCHITECTURE.md` §1 |
| The decode table signals (wheel_speed `0x102`, clutch/gear `0x142`, throttle/rpm `0x140`, ECU rpm `0x146`, side stand `0x481`, engine cutoff `0x121`) decode as documented | `docs/can-profiles.md` §5 decode table + decode notes; `logger/40mph_drive_cycle.trc` (~220 s ride) and overlay PNGs in `logger/` |
| The embedded C decoder and python-cantools **agree** when replaying the ride capture | `tools/golden_check.py` (tolerances REL 1e-4 / ABS 1e-3), run in CI (`can-decode-golden` job) |
| The ESP-NOW link (DE-01) is implemented | Status 🟢 in `docs/design/README.md` §3 |

Even Tier 1 carries **stated confidence caveats you must propagate**: the decode
table is *empirically reverse-engineered from a single reference bike, not from
Triumph documentation*; the wheel-speed scale (`raw/16` → km/h) and both rpm scales
are calibrated against the ride's known envelope and are **approximate**
(`docs/can-profiles.md` §5 warning block). Any public statement of the decode must
carry that caveat. Also honest: `docs/can-profiles.md` cites bench captures
`logger/throttle.trc` and `logger/wheel.trc` that are **not committed** — only
`40mph_drive_cycle.trc` is in-repo. Don't cite the bench captures as reproducible
evidence.

### Tier 2 — VALIDATED-OFFLINE-ONLY (works in replay, never on hardware)

| Claim | Evidence and its limits |
|-------|------------------------|
| The DE-09 braking FSM (OFF/BRAKING/STOPPED) behaves sensibly with the documented tunables | Dry-run in `tools/trc_viz.py` against **one ride log** (`40mph_drive_cycle.trc`): hysteresis cut transitions 162→48 and sub-0.5 s blips 65→8; the 120 ms debounce cut 48→30 and blips 8→3 (`docs/firmware.md`). One log, one bike, one rider, offline replay — say exactly that. |

### Tier 3 — DESIGNED-ONLY (documented design; no implementation or measurement)

| Claim | Actual status |
|-------|---------------|
| Braking FSM **in firmware** | Not implemented — DE-09 status 🔲; `transmitter/software/state_machine/brake_fsm.sm` and `tools/smc/` do not exist despite being described in `docs/firmware.md` §4 (a known doc-ahead-of-code gap; docs are the spec here). There is also an open firmware bug in the decode path it depends on (`CAN_DECODE_SPEED_HIST` — see chmbl-de09-campaign). |
| DE-02 auto-brightness, DE-05 battery/charge, DE-06 TX power/sleep, DE-10 status LED | Status 🔲 (not started); DE-03 link-loss and DE-04 LED render are 🟡 (in design). Per `docs/design/README.md` §3. |
| Power budget (< 1 mA parked) | A target in `docs/safety-regulatory.md` §2 — never measured. |
| End-to-end latency ≤ 100 ms (brake event → LED) | A **budget/target** in `ARCHITECTURE.md` §5 — never measured end-to-end, and cannot be until DE-09 exists in firmware. Never write "the light responds within 100 ms". |
| Link-loss failsafe behavior (≤ 300 ms timeout, distinct indication) | Specified in `docs/protocol.md`; DE-03 is 🟡. |

### Tier 4 — ASPIRATIONAL (stated intent, no artifact)

| Claim | Status |
|-------|--------|
| Multi-bike support ("adding a bike is a data change") | Architecture intent (`ARCHITECTURE.md` §3, `docs/can-profiles.md` §6); exactly one profile exists. The Scrambler 400 X is *expected* to share it (same TR-series powertrain) — expected, not tested. |
| Street Triple 765 support | Explicitly a **stretch target** needing its own profile (`docs/roadmap.md`, `docs/can-profiles.md` §5). |

When writing public text, tag internally before you draft: put each claim in a
tier, then draft only phrasing that tier supports. If a reviewer can read your
sentence as one tier higher than the evidence, rewrite it.

---

## 4. Reproducibility standard for an open product

The project's distinctive external asset is not a secret decode — it is that
**anyone can re-derive it**. The chain that makes that true, all committed:

```
raw capture (logger/40mph_drive_cycle.trc)
  → DBC ground truth (profiles/triumph_tr.dbc)
    → deterministic regeneration (tools/gen_profile.py → transmitter/software/main/bike_profile_triumph_tr.c, committed; CI diffs a fresh regeneration against it — staleness gate)
      → golden test (tools/golden_check.py: C decoder vs cantools on the same capture, REL 1e-4 / ABS 1e-3)
```

Never hand-edit the generated profile C file; the DBC is the single source of
truth and CI enforces it.

**What a release or public writeup must include** (the bar for calling a result
reproducible):

1. **The capture** it was derived from or validated against, committed in-repo (or
   the exact committed file named by path + commit hash).
2. **The exact tunables** used — full values, not "defaults" (the FSM tunables table
   lives in `docs/firmware.md`; e.g. `DECEL_ON_MPHPS` 3.0, `DECEL_ON_DEBOUNCE_MS`
   120, `STOP_SPEED_MPH` 1.0, `MOVING_SPEED_MPH` 3.0, `STATE_MIN_DWELL_MS` 150,
   `STOP_TIMEOUT_MS` 60000).
3. **Replay metrics**, not adjectives — transition counts, blip counts, on-time,
   as in the 162→48→30 numbers above. A claim without a replayable number is an
   opinion.
4. **Versioned docs** — the doc of record updated in the same change (project
   doctrine: docs are the spec, code follows), and the commit hash the writeup
   describes.

**Anonymization of captures is required, not optional.** Both `docs/can-profiles.md`
(§3 step 6 and the note under the decode table) and `docs/roadmap.md` Phase 2 say
committed captures must be **anonymized** raw `.trc`/`candump` logs. Before
committing or publishing any capture, strip or verify absence of anything
identifying: VIN or serial-bearing diagnostic responses, GPS/location metadata in
filenames or companion files, and dates/places in commit messages that identify the
rider's movements. (The repo does not ship an anonymization script — checking is
manual as of 2026-07-07.) Known wart to state honestly: `docs/can-profiles.md`
tells contributors to commit captures under `transmitter/software/captures/`, but
the committed captures actually live in `logger/`.

---

## 5. Ecosystem context — handle with care

Repo-verifiable facts you may state: the two patent-fenced approaches (lamp-circuit
tap; inertial detection) are named in `ARCHITECTURE.md` §1 as "the obvious designs",
and this project's differentiator is CAN-inference via a listen-only diagnostic-port
read.

**Background knowledge — NOT verifiable from this repo; label it as such if you use
it at all:** commercially available helmet brake lights are generally IMU-based —
i.e., precisely the approach inside the inertial-detection patent fence — and
"brake-light modulators" sold for motorcycles tap the stop-lamp circuit, the other
fenced approach. If that background is accurate, the CAN-inference approach is what
makes this project distinctive in the market. But the repo contains **no market
survey, no product teardown, no patent citations** — so any public sentence
comparing open-chmbl to commercial products must be sourced independently at
writing time or explicitly framed as "to our knowledge". Never present a market
claim as a repo fact, and never name a competitor product's patent status without
counsel.

The honest one-line external pitch that stays entirely inside verified territory:

> An open, documented, reproducible reverse-engineering of a modern motorcycle's
> CAN bus into an auxiliary helmet brake light — with the captures, decode tables,
> and golden tests committed so anyone can check the work.

---

## 6. Pre-publication checklist

Before any externally visible text ships:

- [ ] Every claim assigned a tier (§3) and phrased at that tier, with Tier-1
      confidence caveats propagated.
- [ ] Patent language is design-around framing only; no clearance claims; counsel
      disclaimer present where patents are discussed.
- [ ] Auxiliary-not-replacement, track/off-road/educational framing, no-flashing,
      red-to-rear, helmet-cert warning, and builder-assumes-liability statements
      present (§2).
- [ ] Nothing describes or suggests transmitting on the bike CAN bus, IMU braking
      detection, brake-wire taps, flashing patterns, or drilling helmets.
- [ ] Reproducibility bundle complete: capture path, exact tunables, replay
      metrics, doc-of-record commit (§4).
- [ ] Any new capture is anonymized (§4).
- [ ] Market/competitor statements labeled as background knowledge or independently
      sourced (§5).
- [ ] LICENSE gap acknowledged if the text calls the project "open-source" (§2).

---

## When NOT to use this skill

- Implementing or debugging the DE-09 FSM, the `CAN_DECODE_SPEED_HIST` bug, or the
  SMC build step → `chmbl-de09-campaign`.
- What counts as internal evidence, acceptance thresholds, adding tests/CI rows →
  `chmbl-validation-and-qa`.
- Running `trc_viz.py` / `golden_check.py` / `gen_profile.py` and interpreting their
  output → `chmbl-diagnostics-and-tooling`.
- Doc-of-record conventions, DE template, FFL IDs, house style for internal docs →
  `chmbl-docs-and-writing`.
- Why the architecture is shaped this way (invariants, weak points) →
  `chmbl-architecture-contract`.
- CAN/DBC/`.trc`/TWAI/ESP-NOW fundamentals → `chmbl-can-reference`.
- Open research problems and first steps toward the full open product →
  `chmbl-research-frontier`.
- How changes are classified/gated (docs-are-spec, trade-study rules) →
  `chmbl-change-control`.

---

## Provenance and maintenance

All facts verified against the repo on **2026-07-07**, branch
`claude/skill-library-continuity-mib7ua`. Re-verify before relying on:

| Fact class | Re-verification command (run from repo root) |
|------------|-----------------------------------------------|
| Patent framing & positioning claim | `sed -n '17,40p' ARCHITECTURE.md` and `grep -n -A4 "non-goals" docs/safety-regulatory.md` |
| Legal envelope wording | `sed -n '10,36p' docs/safety-regulatory.md` |
| DE status tiers (🔲/🟡/🟢) | `grep -n "DE-0\|DE-10" docs/design/README.md` |
| FSM-in-firmware still absent | `ls transmitter/software/state_machine tools/smc 2>&1` (expect "No such file or directory") |
| Committed captures inventory | `ls logger/*.trc logger/*.png` |
| Golden-test tolerances | `grep -n "REL_TOL\|ABS_TOL" tools/golden_check.py` |
| FSM tunables & dry-run numbers | `grep -n "162\|48 → 30\|DECEL_ON\|MIN_DWELL" docs/firmware.md` |
| Decode-table confidence caveats | `sed -n '233,241p' docs/can-profiles.md` |
| Latency budget still a target (not measured) | `grep -n "100 ms" ARCHITECTURE.md docs/*.md` |
| Anonymization requirement | `grep -rn -i "anonym" docs/` |
| LICENSE still missing | `ls LICENSE* 2>&1` |
| Pivot commit exists | `git log --oneline -1 5c094a5` |
