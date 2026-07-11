---
name: chmbl-architecture-contract
description: Load this skill BEFORE designing, reviewing, or modifying anything in open-chmbl that touches system structure - the TX/RX split, the CAN listen-only rule, the radio heartbeat model, the DBC-to-C decode pipeline, the braking FSM shape, build targets, or protocol/timing budgets. It states the load-bearing design decisions WITH their rationale, a numbered invariant checklist a reviewer can test a change against, and the known weak points. Do NOT load it for step-by-step build/flash/run instructions (chmbl-build-and-env, chmbl-run-and-operate), for debugging a specific failure (chmbl-debugging-playbook), for CAN/DBC domain fundamentals (chmbl-can-reference), or for the change-approval process itself (chmbl-change-control).
---

# open-chmbl architecture contract

This skill is the "why the system is shaped this way" document. Read it before
proposing any structural change; check the change against the invariant checklist
in §3. Docs are the spec in this project — if a change alters behavior described
here, the corresponding repo doc (`ARCHITECTURE.md`, `docs/firmware.md`,
`docs/protocol.md`, `docs/can-profiles.md`, or a `docs/design/de-*.md`) must be
updated in the same change (see `chmbl-change-control`).

## 0. Jargon (defined once)

| Term | Meaning here |
|------|--------------|
| CAN | Controller Area Network — the motorcycle's internal message bus, exposed on its diagnostic port. |
| TWAI | "Two-Wire Automotive Interface" — Espressif's name for the ESP32's built-in CAN controller. |
| Listen-only / silent mode | TWAI mode in which the controller never transmits and never ACKs frames — electrically passive beyond bus loading. |
| ESP-NOW | Espressif's connectionless 2.4 GHz radio protocol (no Wi-Fi association); used for the bike→helmet link. |
| DBC | An industry-standard text file format describing CAN signals (frame ID, bit position, byte order, scale, offset). |
| FSM | Finite State Machine — here, the braking state machine on the transmitter. |
| SMC | State Machine Compiler (smc.sourceforge.net) — a Java tool that generates C from a `.sm` state-machine model. |
| NVS | Non-Volatile Storage — ESP-IDF's key-value flash store (holds pairing keys/config). |
| Kconfig | ESP-IDF's compile-time configuration system (`idf.py menuconfig`). |
| `.trc` | PCAN trace file format — timestamped CAN frame captures; the repo's ride logs use it. |
| TX / RX | TX = the bike-side `transmitter/` unit; RX = the helmet-side `brake_light/` unit. |

## 1. The system in one paragraph

A bike-side ESP32-C3 (`transmitter/`) plugs into a Triumph Speed 400's Euro-5
diagnostic port, reads the CAN bus in strictly listen-only mode, decodes wheel
speed / clutch / gear / throttle / rpm via a per-bike profile generated from a
DBC file, infers braking from wheel-speed-derived deceleration (the bus has no
brake-switch bit), and broadcasts a small state heartbeat over encrypted ESP-NOW
at 20–50 Hz to a helmet-side battery-powered ESP32-C3 (`brake_light/`) that
renders an LED bar. A third firmware, `logger/` (ESP-WROVER-KIT), records ride
CAN traffic to `.trc` files on microSD for offline reverse-engineering and FSM
calibration.

## 2. Load-bearing decisions and WHY

### 2.1 Smart TX, dumb RX

All interpretation (CAN decode, FSM, braking inference) lives on the transmitter.
The brake_light renders whatever discrete state it is told, plus three purely
local concerns: ambient-light dimming, battery management, and link-loss fault
indication. WHY (ARCHITECTURE.md §2): keeping the logic on one side makes the
wire protocol tiny (one packed 8-byte struct, `chmbl_msg_t` in
`transmitter/software/main/protocol.h`) and makes failure modes easy to reason
about — the RX never has to second-guess a stale or partial picture of the bike;
it either has a fresh state or it declares link-lost. Do not add bike-state
interpretation to the RX, and do not grow the protocol beyond discrete states +
diagnostics.

### 2.2 Listen-only CAN — inviolable

The TWAI controller runs in `TWAI_MODE_LISTEN_ONLY` (verified in
`transmitter/software/main/can_rx.c` line 223; the logger defaults to it via
`CONFIG_LOGGER_CAN_LISTEN_ONLY`). It never ACKs and never transmits on the bike
bus. WHY (docs/can-profiles.md §1 "Golden rule"): the CAN bus is a
safety-critical network between the motorcycle's own ECUs; interfering with it
is unacceptable. This is also the identity of the product: a **passive
observer** that reads data from the diagnostic port without modifying any bike
wiring — that is the entire legal/safety posture. The whole architecture was
gated on the (since experimentally confirmed) question of whether the port
exposes free-running broadcast traffic a passive listener can read
(docs/can-profiles.md §5) — it does, at 500 kbit/s. There is deliberately no
CAN transmit path in the transmitter firmware for CLI fakes to reach
(docs/cli.md §5). Never add one.

### 2.3 Patent-driven design fences — permanent

Two existing patent families fence off the obvious designs (ARCHITECTURE.md §1):

1. **No brake-wire tap** — no galvanic or optoisolated connection to the bike's
   stop-lamp circuit, ever.
2. **No IMU / inertial detection** — no accelerometer or gyro anywhere in the
   braking inference, ever.

These forbid, forever in this codebase: any hardware or firmware that senses the
brake-light circuit, and any inertial sensor input to the FSM. Deceleration MUST
come from the bike's own CAN wheel-speed signal. This is why "no brake-switch
bit on the bus" (confirmed absent on the reference bike, docs/can-profiles.md
§5) forced inference-from-deceleration rather than a simpler design — the
simpler designs are legally fenced. If a proposed change would add an IMU "just
for calibration" or "just as a cross-check," it violates the fence.

### 2.4 Heartbeat, not events

The TX transmits state at a fixed rate (Kconfig `CHMBL_NET_RATE_HZ`, range
1–50, default 20; `transmitter/software/main/net.c`) every tick, whether or not
state changed. WHY (docs/protocol.md §4): **absence of packets IS the failure
signal.** With an event-driven protocol, a dropped "brake released" packet
leaves the light latched on, and radio silence is indistinguishable from
"nothing happening." With a heartbeat, the RX runs a link watchdog
(`brake_light/software/main/link.c`): no valid packet within `LINK_TIMEOUT_MS`
(target ≤ 300 ms) → a **distinct link-lost indication** (steady running light +
slow fault blink). The failsafe contract (protocol.md §4, firmware.md §2):
never infer "not braking" from silence (never silently dark), never latch a
stale `BRAKE`, drop packets with old sequence numbers, show a waiting
indication before the first packet after boot.

### 2.5 Decode architecture: DBC → generated data table → generic extractor → golden test

The decode pipeline (decision record: docs/design/de-08-can-decode.md §3a):

```
profiles/triumph_tr.dbc            (committed ground truth)
   → tools/gen_profile.py          (parses DBC with cantools)
   → transmitter/software/main/bike_profile_triumph_tr.c   (generated AND committed
                                    data table — never hand-edit; CI diffs a fresh
                                    regeneration against it as a staleness gate)
   → can_decode.c                  (ONE generic hand-written bit extractor,
                                    pure C, no ESP-IDF includes, host-testable)
   → tools/golden_check.py         (replays logger/40mph_drive_cycle.trc through
                                    the host-built C decoder AND python-cantools,
                                    asserts agreement; CI job `can-decode-golden`)
```

WHY hybrid, per the DE-08 §3a decision record, over the two rejected options:

- **Rejected (A) fully hand-coded** — hand-transcribing the decode table and
  hand-writing the extractor risks transcription drift and silent bit-extraction
  bugs with no machine cross-check.
- **Rejected (B) `cantools generate_c_source`** — generating per-message
  pack/unpack C inverts the data-driven design (per-bike generated *code* plus
  a dispatcher), pushes profile selection toward compile-time, and makes a
  future runtime profile selector expensive. Kept as an escape hatch for a
  future bike too irregular for the generic extractor (e.g. multiplexed
  messages).
- **Chosen (C)** — the FSM's signal set is fixed (~6 signals) regardless of
  bike; only bit layouts differ. So each bike is a small (~150-byte) const
  `bike_profile_t` data table, preserving "a new bike is a data change, not new
  code," while the golden test routes both firmware and offline validation
  through the same DBC so they can never silently drift apart.

Invariant consequence: never hand-edit `bike_profile_triumph_tr.c`; change the
DBC and regenerate.

### 2.6 The braking FSM contract (DE-09 — designed, NOT yet in firmware)

Spec: docs/firmware.md §1 and docs/design/de-09-brake-decel-logic.md. Status as
of 2026-07-07: 🔲 not implemented in firmware (see §4).

- **The light is binary on/off on the wire.** TX-internal states are `OFF`,
  `BRAKING`, `STOPPED`; both `BRAKING` and `STOPPED` emit `ST_BRAKE`. **Two
  on-states exist because their OFF conditions differ**: `BRAKING` releases on
  positive acceleration above a minimum speed or sustained steady speed;
  `STOPPED` releases on real motion (> `MOVING_SPEED_MPH`), a launch (clutch
  released **in gear** while rolling — this is why the gear/neutral signal
  exists in the profile: a neutral stop must not read as a launch), or a
  60 s stop timeout. A stopped bike must read as braking to following traffic.
- **`ST_DECEL` is reserved** in `protocol.h` — never emitted by the current
  FSM; held for a possible future soft-cue tier. Do not repurpose the value.
- **Hysteresis** (enter `STOPPED` below 1.0 mph, exit above 3.0 mph): low-speed
  creep straddling a single threshold made the light strobe. Evidence on the
  40 mph ride log: FSM transitions cut **162 → 48**, sub-0.5 s blips 65 → 8.
  The anti-strobe dwell cannot substitute — the guards genuinely oscillate, so
  the fix is hysteresis, not rate-limiting.
- **Decel-on debounce** (decel > `DECEL_ON_MPHPS` must hold 120 ms): rejects
  momentary throttle-off derivative spikes *without* the latency penalty of
  heavier low-pass smoothing (which suppresses spikes only by delaying genuine
  hard-braking onset — the wrong trade for a brake light). Evidence: blips
  8 → 3, transitions **48 → 30**, on-time essentially unchanged; larger values
  clipped real short brake taps, so 120 ms is the knee.
- **Global min-dwell** (`STATE_MIN_DWELL_MS` = 150): an anti-strobe floor on
  ALL transitions — flashing brake lights are illegal in many jurisdictions
  (docs/safety-regulatory.md).
- **Stale input rule**: if `wheel_speed` is stale/invalid the state machine
  cannot assert braking (docs/safety-regulatory.md; staleness =
  `CAN_DECODE_STALE_MS` 1000 ms in `can_decode.h`).
- The FSM is to be specified in an SMC `.sm` model generated at build time by a
  CMake pre-build step (docs/firmware.md §4) — the `.sm` file is the single
  source of truth, generated C never committed. See §4: neither the `.sm` file
  nor `tools/smc/Smc.jar` exists in the repo yet.

All eleven tunables (table in docs/firmware.md §1) are config values so
behavior retunes without touching the state machine.

### 2.7 Isolation-first build process; the CLI is architecture

Process (docs/design/README.md): feature-function lists → design elements
DE-00…DE-10, built **one at a time in isolation**, inputs faked and outputs
viewed through a serial developer CLI (docs/cli.md). The CLI's
**source-override model** is an architectural feature, not a debug afterthought:
every external I/O signal is read through an indirection with two sources
(`real|fake`); `sig set wheel 30` / `sig ramp wheel -5` / `sig source can|fake`
let you drive the decoder and (future) FSM deterministically on the bench with
no bike present, and `sig show` / `state show` / `link show` view any module's
outputs. This single mechanism is what makes "implement one design element in
isolation" practical. The CLI is compiled behind a build flag and absent in
production; its fakes can never reach the bus because no CAN transmit path
exists. Status of each element lives in the docs/design/README.md §3 table.

### 2.8 Target portability contract

Three firmwares, two chip targets (verified against `sdkconfig.defaults.*`
files and `.github/workflows/firmware-build.yml`, a 5-row build matrix on
ESP-IDF release-v5.3; the canonical matrix table lives in
`chmbl-build-and-env` — update there first if a row is added):

| Firmware | esp32c3 (product) | esp32 classic (interim dev hw) |
|----------|-------------------|-------------------------------|
| transmitter | yes | yes |
| brake_light | yes | yes |
| logger | no | yes (ESP-WROVER-KIT only) |

Application code is shared across targets; only per-target
`sdkconfig.defaults.<target>` and Kconfig pin numbers diverge. The contract
(brake_light/software/README.md "Portability" note):

1. **Never pin tasks to core 1** — the ESP32-C3 is single-core RISC-V; the
   classic ESP32 is dual-core Xtensa. (All current firmwares use plain
   `xTaskCreate`, no `xTaskCreatePinnedToCore` — verified 2026-07-07.)
2. **Console transport is target-selected**, code identical: C3 uses the
   built-in USB Serial/JTAG controller (native USB GPIO18/19, enumerates as
   `/dev/ttyACM*`, any baud, JTAG on the same cable); classic ESP32 has no USB
   peripheral so the console is UART0 (GPIO1/GPIO3) via the board's USB-UART
   bridge (`/dev/ttyUSB*`, 115200).
3. Deep-sleep wake config differs (Xtensa `ext0/ext1` vs. RISC-V GPIO wake) —
   relevant to future TX power management (DE-06).
4. Pin assignments live in Kconfig, not code, because usable GPIO maps differ.

### 2.9 End-to-end latency budget

Per docs/protocol.md §5 — any change touching the tick rates, smoothing window,
heartbeat rate, or LED render loop must re-check this budget:

| Stage | Budget |
|-------|--------|
| CAN frame → decode → state machine (incl. 50 Hz tick + accel smoothing) | ≤ ~25 ms |
| ESP-NOW hop | ≈ 2–5 ms |
| RX render → LED update (60 Hz) | ≤ ~16 ms |
| **Brake event → LED, total** | **≤ ~100 ms target** |

> ⚠️ **Open, unreconciled tension:** the 120 ms decel-on debounce alone exceeds
> the 100 ms total target if "brake event" is read as "deceleration begins."
> One reading treats the debounce as detection-side delay *before* the brake
> event exists (excluded from this transport/render budget); the other reads
> the budget end-to-end from physical decel onset (blown by design). The docs
> of record do not settle which reading is intended — do not silently pick one.
> See `chmbl-proof-and-analysis-toolkit` Recipe 6a for the arithmetic and
> `chmbl-research-frontier` §5 (latency measurement) for the resolution path;
> settling it is a docs-of-record change that goes through
> `chmbl-change-control`.

## 3. Invariant checklist (test every structural change against this)

(Items 1, 2, 5, 8, 9, 15, 16 mirror the merge-blocker gate table in
`chmbl-change-control` §3 — amend both together.)

1. TWAI is configured `TWAI_MODE_LISTEN_ONLY` on the transmitter; no code path
   transmits or ACKs on the motorcycle CAN bus. (Logger may leave listen-only
   only via its explicit Kconfig, only on a bench bus.)
2. No brake-wire tap; no IMU/accelerometer/gyro input to braking inference.
   Deceleration derives solely from CAN wheel-speed.
3. All bike-state interpretation stays on the TX; the RX renders discrete
   states plus local concerns (ambient, battery, link health) only.
4. TX sends the heartbeat every tick regardless of state change; RX treats
   packet absence as a fault, never as "not braking."
5. RX link-loss (> `LINK_TIMEOUT_MS`, ≤ 300 ms target) produces a distinct
   link-lost indication — never silently dark, never a latched `BRAKE`.
6. RX drops packets whose `seq` is not newer than the last accepted.
7. Stale/invalid `wheel_speed` ⇒ the FSM cannot assert braking.
8. `ST_DECEL` (= 1) stays reserved; `BRAKING` and `STOPPED` both emit
   `ST_BRAKE`; the light is binary on the wire.
9. No flashing/strobing brake patterns; every state transition respects the
   global min-dwell floor.
10. `bike_profile_triumph_tr.c` is never hand-edited; decode changes go through
    `profiles/triumph_tr.dbc` + `tools/gen_profile.py`, and the golden test
    (`tools/golden_check.py` over `logger/40mph_drive_cycle.trc`) plus the CI
    staleness diff must pass.
11. `can_decode.c` and the FSM numeric context stay pure C with no ESP-IDF
    includes (host-testable).
12. No task pinned to core 1; new code builds on both `esp32c3` and `esp32`
    (extend the CI matrix if a new firmware/target combination lands).
13. FSM behavior changes go through the tunables table, not hard-coded logic,
    and update docs/firmware.md + DE-09 in the same change (docs are the spec).
14. The 100 ms brake-event→LED budget (§2.9) still holds after any rate/window
    change (subject to the open debounce-vs-budget reading noted in §2.9).
15. The paired peer MAC persists in NVS; re-pairing is an explicit user action
    (a stranger's TX must not drive your helmet light). Note: as of 2026-07-08
    only the 6-byte peer MAC is NVS-persisted — the ESP-NOW PMK/LMK are
    compiled-in development placeholders, not production-secure secrets (see
    `chmbl-config-and-flags` §7.2 and `chmbl-can-reference` §6).
16. The helmet mount is non-penetrating; nothing in firmware or docs may
    assume drilling a helmet.
17. Parasitic draw < 1 mA parked (TX sleeps when the bike is off) — planned
    (DE-06, not yet implemented); don't add always-on loads that preclude it.

## 4. Known weak points (as of 2026-07-07 — stated plainly)

- **Live firmware bug — `CAN_DECODE_SPEED_HIST`.**
  `transmitter/software/main/can_decode.h` sets `CAN_DECODE_SPEED_HIST` to 16,
  but wheel-speed frames arrive at ~100 Hz, so 16 samples span ~150 ms — less
  than the 200 ms `CAN_DECODE_ACCEL_WINDOW_MS`. The ring buffer never spans the
  slope window and the derived acceleration can freeze. Documented in the
  `tools/trc_viz.py` header, which uses 32. Sizing law: `hist ≥ window_ms ×
  frame_rate` (canonical full statement + measured rates: `chmbl-de09-campaign`
  Phase 1). The fix is reserved for the DE-09 campaign — see
  `chmbl-de09-campaign` before touching it.
- **DE-09 is designed but NOT implemented.** `transmitter/software/state_machine/
  brake_fsm.sm` and `tools/smc/Smc.jar` are described in docs/firmware.md §4 and
  the transmitter README but do not exist in the repo. The SMC toolchain has
  never been exercised here; the CMake snippet in firmware.md is a design, not
  a tested build.
- **Tunables calibrated on ONE ride.** Every FSM default (and the 162→48→30
  evidence trail) comes from dry-runs against a single ~220 s capture,
  `logger/40mph_drive_cycle.trc`. No rain, no passenger, no other bike, no
  second rider. Treat the defaults as a starting point, not validated values.
- **Decode table is single-bike empirical with approximate scales.** Owned in
  full — table and confidence caveats — by docs/can-profiles.md §5 (skill home:
  `chmbl-can-reference` §5). Headline caveats: scales calibrated against one
  ride's envelope; front/rear `0x102` assignment suspected, not confirmed.
- **Missing bench captures.** docs/can-profiles.md cites `logger/throttle.trc`
  and `logger/wheel.trc` as cross-checks — they are not committed. Only
  `40mph_drive_cycle.trc` is in-repo, so parts of the decode derivation are not
  independently reproducible from the repo alone. (Related path drift: the doc
  tells contributors to commit captures under `transmitter/software/captures/`,
  but committed captures actually live in `logger/`.)
- **Shared code is duplicated, not shared — drift risk.** Verified by `diff`
  on 2026-07-07 between `transmitter/software/main/` and
  `brake_light/software/main/`: byte-identical duplicates are `protocol.h`,
  `pairing.c`, `pairing.h`, `cmd_system.c`, `cmd_pair.c`; `console.c`/
  `console.h` are near-duplicates differing only in per-device command
  registration. `transmitter/software/main/console.h` carries the NOTE that
  these are duplicated verbatim pending promotion to a shared component
  (roadmap decision). Any edit to one copy must be mirrored — nothing enforces
  this today.
- **Design-status drift.** docs/design/de-08-can-decode.md declares itself
  🟢 implemented (and the decoder, golden test, and CI job exist), but the
  docs/design/README.md §3 status table — nominally the single source of truth —
  still shows DE-08 as 🔲. One of the two is stale.

## 5. When NOT to use this skill

- Setting up the toolchain, building, or recreating the environment →
  `chmbl-build-and-env`.
- Flashing, consoles, CLI sessions, pairing, logger field ops →
  `chmbl-run-and-operate`.
- Diagnosing a specific failure symptom → `chmbl-debugging-playbook`; history
  of settled dead ends → `chmbl-failure-archaeology`.
- CAN/DBC/`.trc`/TWAI/ESP-NOW fundamentals as a domain reference →
  `chmbl-can-reference`.
- Enumerating config axes and tunables and how to add one →
  `chmbl-config-and-flags`.
- The change-approval rules themselves (classification, gates, trade studies) →
  `chmbl-change-control`.
- Actually implementing DE-09 (including the SPEED_HIST fix and SMC step) →
  `chmbl-de09-campaign`.
- Patent/legal positioning detail → `chmbl-external-positioning`.

## 6. Provenance and maintenance

Facts verified 2026-07-07 on branch `claude/skill-library-continuity-mib7ua`.
Re-verify before relying on:

| Fact class | Re-verification command |
|------------|-------------------------|
| Listen-only TWAI mode | `grep -n TWAI_MODE_LISTEN_ONLY transmitter/software/main/can_rx.c logger/software/main/logger_main.c` |
| SPEED_HIST bug values (16 vs 200 ms window; viz uses 32) | `grep -n "SPEED_HIST\|ACCEL_WINDOW" transmitter/software/main/can_decode.h tools/trc_viz.py` |
| Heartbeat rate default/range | `grep -n -A4 CHMBL_NET_RATE_HZ transmitter/software/main/Kconfig.projbuild` |
| Protocol struct / ST_DECEL reserved | `sed -n '1,45p' transmitter/software/main/protocol.h` |
| Duplicated-file set | `for f in protocol.h pairing.c pairing.h cmd_system.c cmd_pair.c console.c console.h; do diff -q transmitter/software/main/$f brake_light/software/main/$f; done` |
| No core-1 pinning | `grep -rn PinnedToCore transmitter brake_light logger` (expect no hits) |
| CI matrix / IDF version | `grep -n "target\|esp_idf_version" .github/workflows/firmware-build.yml` |
| DE-09 / SMC still missing | `ls transmitter/software/state_machine tools/smc 2>&1` (expect "No such file") |
| Committed captures | `ls logger/*.trc` |
| FSM tunables + 162→48→30 evidence | docs/firmware.md §1 tables; `grep -n "162\|48 → 30\|8 → 3" docs/firmware.md docs/design/de-09-brake-decel-logic.md` |
| Latency budget | docs/protocol.md §5 |
| DE status table (incl. the DE-08 drift) | docs/design/README.md §3 vs. docs/design/de-08-can-decode.md header |
