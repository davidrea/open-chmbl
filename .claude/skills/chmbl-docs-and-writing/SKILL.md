---
name: chmbl-docs-and-writing
description: Load this skill BEFORE writing or editing ANY documentation in open-chmbl — a new or updated docs/*.md, a design-element doc (de-*.md), an exploration, a README, the status table, the roadmap, or a commit/PR message. Docs are the SPEC in this repo (code follows docs), so this skill owns the document map (one home per fact + a decision tree for where a new fact lands), the 8-section DE template with exemplars, the exploration→DE promotion path, status-emoji and resolved-question conventions (including known drift to fix), the observed house-style checklist, cross-link/anchor hygiene with a working checker script, the "if you change X, update Y" table, and commit-message conventions. Do NOT load it for what must be true before a change may land (chmbl-change-control), for externally visible claims/patent language (chmbl-external-positioning), for what counts as validation evidence (chmbl-validation-and-qa), or for CAN/DBC technical content itself (chmbl-can-reference).
---

# Docs and writing: maintaining the spec of open-chmbl

In this repo **docs are the spec — code follows.** Any behavior change updates the
relevant design doc / `docs/firmware.md` **in the same change**; silent doc/code
divergence is a merge blocker (process side: `chmbl-change-control`). That makes doc
edits engineering work, not housekeeping. This skill tells you where a fact lives,
what a design doc must contain, which status conventions to keep in sync, and the
house style to imitate.

Jargon used below: "DE" = design element (the project's unit of work, one doc per
element under `docs/design/`); "FFL" = feature-function list, the capability baseline
in `docs/feature-functions.md` with IDs like `TX-SM-1` (transmitter) / `BL-RND-2`
(brake_light); "TX" = the bike-side `transmitter/` unit, "RX" = the helmet-side
`brake_light/` unit (vocabulary defined in `ARCHITECTURE.md` §2); FSM = finite state
machine; DBC = the machine-readable CAN signal-packing database
(`profiles/triumph_tr.dbc`); CAN = the vehicle's broadcast data bus.

## 1. The document map — one home per fact

Each doc of record has a single responsibility. Put a fact in its home and **link** to
it from everywhere else (relative links with anchors — §5); never restate it.

| Doc | Single responsibility |
|---|---|
| `README.md` | Front door: one-paragraph pitch, the safety blockquote, and a "Start here" link list. No technical detail of its own. |
| `ARCHITECTURE.md` | **Index + overview**: why-this-approach (the two patent families avoided), the TX/RX split and who does the thinking, reference-bike-first strategy, FSM sketch, safety summary, repo layout. Every section ends by linking into `docs/` for the detail. |
| `docs/feature-functions.md` | The capability baseline — every FFL ID (`TX-<area>-<n>`, `BL-<area>-<n>`) is minted here and only here. |
| `docs/roadmap.md` | Phases with exit criteria + the **open-questions table**. Declares itself "the single source of truth for *what's next*" (phase level). |
| `docs/firmware.md` | Firmware task decomposition, the **FSM transition table and tunables table** (the behavioral spec DE-09 implements), the SMC pre-build step, shared-library shape. |
| `docs/protocol.md` | ESP-NOW message format, pairing ritual, sequence/failsafe rules. |
| `docs/can-profiles.md` | Listen-only golden rule, sniffing methodology, the `bike_profile_t` struct shape, and the **reference decode table** (§5) with its ⚠️/✅ risk boxes. |
| `docs/cli.md` | The developer-CLI spec: the source-override (`real`/`fake`) model and every command. Command behavior changes land here. |
| `docs/hardware.md` | BOM, power, connectors, enclosures (first-pass sizing). |
| `docs/safety-regulatory.md` | Legal + functional-safety + helmet-safety requirements. Hard rules live here; other docs summarize and link. |
| `docs/led-brightness-benchmark.md` | Cross-cutting sizing study (CHMSL photometry → flux budget → emitter down-select); feeds DE-02 and DE-04. |
| `docs/design/README.md` | **Process + status home**: the FFL→DE→isolation→integration process, the 8-section DE template (§2), and the DE status table (§3) — declared "the single source of truth for *what's the next isolated piece*". |
| `docs/design/de-*.md` | One design element each: rationale, isolation boundary, acceptance criteria, open items. The *why*; the *what* (transition tables, tunables) may live in `firmware.md` with the DE doc holding rationale. |
| `docs/design/explorations/` | Parking lot for **uncommitted** future directions (💭/⏳ status, own README table). No FFL trace, no status-table slot until promoted (§2b). |
| Per-unit READMEs (`transmitter/`, `brake_light/`, `logger/software/`, `*/hardware|software/`) | Parts, pinout, build/flash for that directory only (per `docs/roadmap.md` §Tracking). |

### Decision tree — where does a new fact land?

1. **A new capability the device must have?** → mint an FFL ID in
   `docs/feature-functions.md` (next number in its area), then trace it from a DE doc.
2. **A behavior/timing/threshold the firmware must implement?** → the relevant spec
   doc (`firmware.md` for tasks/FSM/tunables, `protocol.md` for the radio,
   `cli.md` for commands), with rationale in the owning `de-*.md`.
3. **A rationale, trade-off, or decision for one element?** → that element's
   `de-*.md` (architecture decisions get their own subsection — DE-08 §3a is the
   exemplar; dependency/framework choices need a trade study — DE-04 §3.3).
4. **An empirical fact about the reference bike / a CAN signal?** → the decode table
   and decode notes in `docs/can-profiles.md` §5 (and the DBC if it changes packing —
   see §6).
5. **A cross-element design decision or invariant?** → `ARCHITECTURE.md` if it shapes
   the system split; `docs/safety-regulatory.md` if it is a safety rule.
6. **An open question / a resolution of one?** → the roadmap open-questions table row
   (and the DE's §8 — see §3 for the resolved style).
7. **An idea not yet scheduled?** → a new file in `docs/design/explorations/` plus a
   row in `docs/design/explorations/README.md`'s table. NOT a `de-*.md`.
8. **How to build/flash/wire this directory?** → that directory's README.
9. **Project status?** → the DE status table (`docs/design/README.md` §3) and nowhere
   else except each DE's own header line, which must mirror it (§3 below).

If the fact would fit two homes, put it in the more specific one and link from the
general one (that is the observed pattern: `ARCHITECTURE.md` §4 sketches the FSM and
links to `firmware.md#braking-state-machine` for the full table).

## 2. The design-document template

`docs/design/README.md` §2 fixes the structure: every `de-*.md` starts with a title
`# DE-NN — <Element name>`, then a header line

```
**Status:** 🔲 not started · **Device(s):** transmitter · **Depends on:** DE-00, DE-08
```

(status emoji + optional free-text qualifier like "implemented (bench)" or
"placeholder landed", middot-separated fields), a short intro paragraph linking to the
spec docs it implements, then these eight numbered sections:

| § | Section | What it must contain | Exemplar to imitate |
|---|---|---|---|
| 1 | **Scope & isolation boundary** | What's in, what's out, and *what is faked at the edges* to test it alone — an explicit "In:", "Out (faked at edges):", "Isolation test:" bullet trio. | `de-09-brake-decel-logic.md` §1: inputs come from `sig set`, not live CAN (that's DE-08); output read via `state show`, no radio needed. |
| 2 | **FFL traceability** | Just the FFL IDs realized, as ranges. | `de-08-can-decode.md` §2: "TX-CAN-1…5, TX-DEC-1…7." One line is correct. |
| 3 | **Component selection** | Hardware parts / software libraries with rationale; defer shared parts to `hardware.md`. New dependencies/frameworks require a written trade study. | `de-04-led-render.md` §3.3 — the LM3410 driver trade study: requirements *ranked for this application*, a comparison table, an explicit winner. Architecture decisions get a lettered subsection: `de-08` §3a weighs options (A)/(B)/(C, chosen) with rationale and the escape hatch. |
| 4 | **I/O assignments & configuration** | Pins, peripherals, bit rates, timings, message layouts. | `de-01-espnow-link.md` §4: channel, PMK/LMK, NVS persistence, payload struct by link. |
| 5 | **Firmware module/task decomposition** | Tasks, queues, shared state, rates, ownership — and explicitly which parts are **pure (host-testable)** vs. platform-bound. | `de-08` §5: `can_decode.c` called out as "pure, host-testable ... No ESP-IDF includes", CLI and host-golden-test files listed. |
| 6 | **CLI hooks** | The `docs/cli.md` commands used to fake inputs / view outputs. | `de-09` §6: `sig set wheel <mph>`, `sig source fake`, `state show`, `state force`. |
| 7 | **Isolation acceptance** | Concrete stimulus → observable outcome bullets, written *before* implementation, thresholds named symbolically, safety invariants included. | `de-09` §7 (stimulus→state style: "a synthetic decel ramp steeper than `decel_on_mphps` → `BRAKING` within budget"); `de-01` §7 (two-board bench style). Evidence rules: `chmbl-validation-and-qa`. |
| 8 | **Open items** | Bulleted unknowns; resolutions use strike-through, never deletion (§3). | `de-09` §8. |

**Observed, sanctioned deviations** (the template bends where the element demands):
`de-09` replaces §3 with "Inputs & derived signals" (a signals table — it selects no
components); `de-01` appends a **§9 "Implementation notes"** recording deviations from
the design after landing code — add one whenever implementation diverged, listing each
deviation and whether it is to-revisit. Keep the section *numbers* stable so `§N`
cross-references from other docs survive.

### 2b. Exploration → DE promotion path

Per `docs/design/README.md` §4 and `docs/design/explorations/README.md`: an
exploration is a sketch "on record but not committed" — it captures rationale,
trade-offs, and open questions, and explicitly does **not** get FFL traceability, an
isolation test, or a status-table slot. Promotion happens only when the work is
scheduled, and means, in one change:

1. Write the `de-NN-<slug>.md` using the §2 template (the exploration's content seeds
   §3/§8; you now must supply §1, §2, §6, §7).
2. Mint/assign the FFL IDs it realizes in `docs/feature-functions.md`.
3. Add its row to the status table in `docs/design/README.md` §3 (status 🔲 or 🟡)
   with the Depends-on column filled in.
4. Update the exploration's row in `explorations/README.md` (💭 exploring → promoted;
   ⏳ marks "ready to promote") and link it to the new DE rather than deleting the
   exploration file — the rationale trail stays.

Until promotion, the baseline in `hardware.md` and the rules in
`safety-regulatory.md` govern anything actually built (the explorations README says
so in a blockquote — keep that framing).

## 3. Status conventions — and keeping them in sync

Four distinct status idioms exist; use the right one and, above all, **keep the two
copies of DE status identical**.

1. **DE status table** (`docs/design/README.md` §3): 🔲 not started · 🟡 in design ·
   🟢 implemented. The table is *declared in that file* to be the single source of
   truth. Flipping a status is the **last** step of a DE, not the first
   (definition-of-done: `chmbl-validation-and-qa` §6).
2. **DE header line**: same emoji plus an honest qualifier ("🟢 implemented (bench)",
   "🟡 placeholder landed"). It must mirror the table row.
   > **KNOWN DRIFT (verified 2026-07-08):** `de-08-can-decode.md`'s header says
   > "🟢 implemented" while its `docs/design/README.md` §3 table row says 🔲. Since
   > the table is the declared source of truth, a reader of the table wrongly
   > concludes DE-08 is not started. When you touch either file, reconcile them in
   > the same change (DE-08 *is* implemented — `transmitter/software/main/can_decode.c`
   > exists and the golden CI job exercises it — so the table row is the stale one).
   > This is exactly the failure mode the same-change rule exists to prevent: update
   > header and table together, always.
3. **Roadmap open-questions table** (`docs/roadmap.md`): resolution style is bolded
   prose *in place* in the "Current lean" column — "**Resolved — no brake bit on the
   reference bus.** Braking is inferred from ..." with a link to where the answer now
   lives. The row is never deleted; the question column keeps the original question.
   Phase bullets use ✅ for done items.
   > **More verified drift of the same kind (2026-07-08):** the roadmap's "CAN access
   > mode" row still says "**Unknown — Phase 2 gate**" and its "Stop-and-go flicker"
   > row still says "Open", but both are resolved — `docs/can-profiles.md` §5 has the
   > ✅ free-running-broadcast box, and DE-09 §8 struck through the flicker item as
   > resolved (hysteresis, 162 → 48 transitions). `ARCHITECTURE.md` §3's ⚠️ box about
   > the broadcast question is likewise stale. Fix these when editing those files;
   > "roadmap row updated?" belongs on your review checklist for any resolution.
4. **Strike-through-resolved in DE §8** (`de-09` §8 is the exemplar):
   `~~old open item text~~` followed by "**Resolved:** <what was decided>, <link to
   the section holding the answer>, <evidence with numbers>". Never delete the item —
   the strikethrough is the record that it *was* open.
5. **Risk boxes in `can-profiles.md`** (and `ARCHITECTURE.md`): blockquotes leading
   with ⚠️ for an open risk ("> ⚠️ These IDs/scales are **empirically
   reverse-engineered** ...") and flipped to ✅ when answered ("> ✅ **Resolved on the
   reference bike:** ..." — the original question text is kept inside the resolved
   box). Flip, don't delete.
6. **Explorations table**: 💭 exploring · ⏳ ready to promote (legend in
   `explorations/README.md`).

## 4. House style checklist (derived from the actual docs)

Before submitting any doc change, check your text against the observed style:

- [ ] **Bold the load-bearing words**, especially safety qualifiers: "**listen-only**",
      "**never** ACKs", "**confirmed present**", "**not** rider throttle". Bold is
      used for emphasis-that-changes-behavior, not decoration.
- [ ] **Tables for anything enumerable** — tunables, transitions, decode rows, tasks
      with rates, status, trade-study criteria. Prose only for rationale.
- [ ] **Numbered `## N. Title` sections** separated by `---` horizontal rules;
      subsections `### N.M` or lettered (`§3a`); cross-reference with `§N` in prose.
- [ ] **Relative links with anchors** for every cross-doc claim
      (`[firmware.md §4](../firmware.md#4-build--toolchain)`); reference-style link
      definitions (`[DE-07 40 mph ride log]: ../can-profiles.md#decode-table`) when
      the same link repeats. Never absolute URLs into the repo itself.
- [ ] **Blockquote callouts** for warnings/resolutions, led by ⚠️ / ✅ (or a bolded
      opening phrase). Emoji appear *only* as status/callout glyphs.
- [ ] **Signal, state, and code names in backticks**: `wheel_speed`, `clutch_pulled`,
      `OFF`/`BRAKING`/`STOPPED`, `ST_BRAKE`, `tx_config_t`, `sig set wheel <mph>`.
      Tunables named symbolically in prose (`DECEL_ON_MPHPS`), resolving to the
      tunables table in `docs/firmware.md`.
- [ ] **TX/RX vocabulary**: "TX" = transmitter (bike-side), "RX" = brake_light
      (helmet-side), defined once in `ARCHITECTURE.md` §2. Use `transmitter/` /
      `brake_light/` for the directories, TX/RX for the roles.
- [ ] **Units discipline**: the FSM and all tunables work in **MPH / MPH/s**
      ("speeds in MPH, accelerations in MPH/s" — `firmware.md` tunables table);
      the bus decodes in **km/h** (`raw / 16` → km/h) and the decoder converts
      (`wheel_speed_kmh` flag in `bike_profile_t`). Never mix them silently; state
      the unit at each number.
- [ ] **Numbers carry their evidence**: "cut FSM transitions **162 → 48**" style —
      capture named, effect quantified (evidence bar: `chmbl-validation-and-qa`).
- [ ] **ASCII-art diagrams** in fenced code blocks for system/FSM topology (see
      `ARCHITECTURE.md` §2, `firmware.md` FSM); keep the diagram and its transition
      table in the same doc consistent when editing either.
- [ ] **Middot (`·`) separators** in status/header lines; em-dashes (—) in prose.
- [ ] **Honest labeling**: unbuilt things say "planned"/"reserved"/"first-pass
      sketch"; estimates state their assumptions (`led-brightness-benchmark.md`'s
      "two kinds of numbers" blockquote is the exemplar). Never upgrade a claim —
      external-facing wording rules: `chmbl-external-positioning`.

## 5. Cross-link hygiene

**How GitHub forms anchors from headings**: strip markdown formatting, lowercase,
delete every character that is not a word character (letters/digits/**underscore** —
underscores survive), space, or hyphen, then turn spaces into hyphens. Duplicate slugs
get `-1`, `-2` suffixes. Consequences seen in this repo:

- `## 2. Brake_light (helmet-side)` → `#2-brake_light-helmet-side` (the `.` and
  parens vanish, `_` stays).
- `## 4. Build & toolchain` → `#4-build--toolchain` — the `&` drops out leaving two
  spaces → a **double hyphen**. Same for any `—`/`&`/`/` in a heading.
- The anchor covers the **whole** heading: `## 3. Helmet & rider safety (the helmet
  side)` → `#3-helmet--rider-safety-the-helmet-side`, not `#3-helmet--rider-safety`.
- Renaming a heading silently breaks every inbound anchor — grep for the old slug
  across `**/*.md` before renaming, or run the checker below.

**Checker** (shipped with this skill, tested against this repo 2026-07-08; checks
inline relative links for missing files *and* bad anchors, skips fenced code blocks;
limitation: it does not verify reference-style definition lines):

```bash
python3 .claude/skills/chmbl-docs-and-writing/scripts/check_links.py   # from repo root
```

Exit 0 = clean; exit 1 prints `file:line: missing file|bad anchor -> target`.

**Known broken links it found (as of 2026-07-08)** — pre-existing issues, listed so
you don't re-diagnose them; fix them when touching the containing file:

| Location | Problem |
|---|---|
| `docs/can-profiles.md:212` and `:213` | Link to `../logger/throttle.trc` and `../logger/wheel.trc` — the bench captures are **not committed** (a known repo gap; see `chmbl-validation-and-qa` §2 before "fixing" by deletion — the honest fix is labeling them as not committed or committing them). |
| `docs/design/explorations/mounting-magnetic.md:11` and `:318` | Anchor `../../safety-regulatory.md#3-helmet--rider-safety` is truncated; the real slug is `#3-helmet--rider-safety-the-helmet-side`. |

## 6. Update triggers — "if you change X, update Y in the same change"

The same-change rule made concrete. Rows verified against the repo 2026-07-08.

| If you change... | You must also update, same change |
|---|---|
| A CAN signal's packing / a new signal | `profiles/triumph_tr.dbc` (ground truth) **and** regenerate the committed profile: `python3 tools/gen_profile.py profiles/triumph_tr.dbc --name "Triumph Speed 400 / Scrambler 400X (TR-series)" --bitrate 500000 --symbol bike_profile_triumph_tr --out transmitter/software/main/bike_profile_triumph_tr.c` (CI diffs a regeneration — never hand-edit the generated file) **and** the human-readable decode table + decode notes in `docs/can-profiles.md` §5. |
| FSM behavior, a guard, or a tunable default | `docs/firmware.md` (transition table + tunables table + `tx_config_t` comment values) **and** `docs/design/de-09-brake-decel-logic.md` (rationale + §4 diagram/table) **and** the Python FSM port in `tools/trc_viz.py` (it mirrors the design; divergence breaks the calibration bench). |
| Adding a new design element | `docs/design/README.md` §3 table row (ID, deps, status) **and** FFL IDs in `docs/feature-functions.md` **and** the new `de-*.md` itself (§2 template). |
| Resolving anything that was open | The roadmap open-questions row (bold **Resolved —** style) **and** the DE §8 item (strike-through) **and** any ⚠️ risk box that posed it (flip to ✅) **and** `ARCHITECTURE.md` if it restates the question (§3's stale box shows what happens otherwise). |
| DE status | Both the `docs/design/README.md` §3 table row and the DE doc's header line (the de-08 drift in §3 above is the cautionary tale). |
| A CLI command (add/rename/behavior) | `docs/cli.md` **and** the CLI-hooks §6 of any DE doc that names the command. |
| The ESP-NOW message struct / pairing / timing | `docs/protocol.md` **and** `de-01-espnow-link.md` (plus its §9 implementation notes if reality diverges). |
| A committed capture (new/renamed) | `docs/can-profiles.md` references **and** the golden-test wiring if load-bearing (`chmbl-validation-and-qa` §4a owns that procedure). |
| A heading in any doc of record | Every inbound anchor — run the §5 checker. |
| Anything a per-directory README documents (pins, build steps, parts) | That README. |

## 7. Commit and PR message conventions

Observed via `git log --oneline -30` (verified 2026-07-08):

- **Optional lowercase scope prefix** = the directory/area touched, colon, then a
  lowercase imperative subject: `logger: fix frame drops by batching SD writes with
  setvbuf`, `tools: low-pass the wheel speed before the decel slope calc`,
  `ci: restore brake_light esp32 build row dropped in merge`, `transmitter: add
  developer console (DE-00), mirroring brake_light`, `chore: ignore Python bytecode
  caches`, `skills: add can-reference domain pack`. Observed scopes: `logger:`,
  `tools:`, `ci:`, `chore:`, `transmitter:`, `skills:` — use `docs:` for pure doc
  changes by extension of the pattern.
- **Unprefixed subjects** are Capitalized imperative: "Fix transmitter build: use
  \`driver\` for TWAI on IDF 5.3", "Implement DE-08 embedded CAN decode via DBC +
  generated profile data", "Docs update based on braking/decel FSM dry run from
  captured CAN data".
- **Name the DE ID in the subject** when the change advances an element
  (`Implement DE-01 ESP-NOW link and DE-03 link-loss placeholder`, `Add Python CAN
  decode visualization with .trc playback (DE-08/DE-09)`).
- Imperative mood throughout (`Fix`, `Add`, `Implement`, `Remove`, `Ignore`); no
  trailing period; subjects state the *what* and often the *why/mechanism* after a
  colon or "by ...".
- Work lands via **PR merge commits** from `claude/<topic>-<suffix>` or
  `<area>/<topic>` branches (e.g. `logger/fix-frame-drops-setvbuf`); a doc-of-record
  change rides in the **same PR** as the code it specifies — never a follow-up
  "update docs" commit.

## When NOT to use this skill

- What must be true before a change may land, change classification, merge blockers,
  the trade-study doctrine itself → `chmbl-change-control`.
- Whether a claim is "validated", evidence tiers, adding tests/CI rows, DE
  definition-of-done → `chmbl-validation-and-qa`.
- Externally visible claims, patents, legality wording, the honesty ledger →
  `chmbl-external-positioning`.
- The CAN/DBC/.trc technical content you might be documenting → `chmbl-can-reference`.
- Design decisions and invariants themselves (as opposed to where to write them
  down) → `chmbl-architecture-contract`.

## Provenance and maintenance

All facts verified against the repo working tree on 2026-07-08 (branch
`claude/skill-library-continuity-mib7ua` lineage). Re-verify before trusting:

| Fact class | Re-verification command |
|---|---|
| Doc inventory / map | `ls docs docs/design docs/design/explorations` |
| DE template (8 sections) | `sed -n '31,50p' docs/design/README.md` |
| DE status table vs. DE headers (drift) | `grep -n "^\*\*Status" docs/design/de-*.md && sed -n '58,71p' docs/design/README.md` |
| Roadmap resolved-row style + stale rows | `grep -n "Resolved\|Unknown\|Open —" docs/roadmap.md` |
| Strike-through pattern | `grep -n "~~" docs/design/de-09-brake-decel-logic.md` |
| ⚠️/✅ risk boxes | `grep -n "⚠️\|✅" docs/can-profiles.md ARCHITECTURE.md` |
| Exploration promotion rules | `cat docs/design/explorations/README.md` |
| Broken links (current set) | `python3 .claude/skills/chmbl-docs-and-writing/scripts/check_links.py` |
| FFL ID scheme | `sed -n '1,15p' docs/feature-functions.md` |
| Tunables units (MPH/MPH-per-s) | `grep -n "MPH" docs/firmware.md` |
| Commit conventions | `git log --oneline -30` |
| gen_profile regeneration args (CI's exact invocation) | `grep -n "gen_profile" .github/workflows/firmware-build.yml` |
