---
name: chmbl-debugging-playbook
description: Symptom-to-triage playbook for open-chmbl failures. Load when something is BROKEN and you need to diagnose it — firmware build errors (esp_driver_twai / -Werror=format / stale sdkconfig), frozen or wrong derived acceleration, ESP-NOW pairing/link problems, logger SD frame drops / watchdog crashes / dead LCD, golden_check failures, or trc_viz not starting. Each entry has a first-check command, the likely cause with its incident history (commit hashes), and a discriminating experiment for look-alike causes. Do NOT load for how-to-run instructions (chmbl-run-and-operate), environment setup from scratch (chmbl-build-and-env), tool output interpretation when nothing is failing (chmbl-diagnostics-and-tooling), or the full history of settled investigations (chmbl-failure-archaeology).
---

# open-chmbl debugging playbook

Symptom → triage for this project's real, historically observed failure modes.
Every claim below was verified against the repo source and `git show` of the
cited commits as of 2026-07-07. Run first-check commands from the repo root.

Jargon used here, defined once:

- **CAN** — Controller Area Network, the motorcycle's shared message bus
  (500 kbit/s here). **TWAI** is Espressif's name for its CAN peripheral.
- **DBC** — a text database describing how signals are packed into CAN frames.
  This repo's ground truth is `profiles/triumph_tr.dbc`.
- **Profile** — the generated C table (`transmitter/software/main/bike_profile_triumph_tr.c`)
  produced from the DBC by `tools/gen_profile.py`. Never hand-edited.
- **ESP-NOW** — Espressif's connectionless Wi-Fi-radio protocol; the encrypted
  link between the bike-side transmitter and the helmet-side brake_light.
- **.trc** — PCAN-format text CAN capture; the committed reference ride is
  `logger/40mph_drive_cycle.trc` (~220 s, ~315k frames).
- **NVS** — ESP32 non-volatile storage (where the paired peer MAC persists).
- **Kconfig / sdkconfig** — ESP-IDF's build-time configuration system.
- **FSM** — finite state machine; DE-09 is the braking FSM design.
- **SMC** — State Machine Compiler, a Java tool that DE-09's design says will
  generate the FSM code from a `.sm` model at build time.

General triage order: (1) is it a build problem, (2) is the data wrong, or
(3) is the link/IO wrong? Sections below follow that order.

---

## (a) Build failures

ESP-IDF is required for target builds (`idf.py set-target <t> && idf.py build`);
the host golden harness needs only cmake + gcc. If `idf.py` itself is missing,
that's environment setup — see `chmbl-build-and-env`.

| Symptom | First check | Likely cause | Fix / status |
|---|---|---|---|
| CMake: `Failed to resolve component 'esp_driver_twai'` | `grep -n PRIV_REQUIRES transmitter/software/main/CMakeLists.txt` | The standalone `esp_driver_twai` component exists only on IDF ≥ 5.5; this project pins **release-v5.3**, where `driver/twai.h` comes from the umbrella `driver` component (incident `dd09fef`) | Depend on `driver`, not `esp_driver_twai`. Already fixed in-tree; recurs if someone "modernizes" the dep or bumps IDF — re-read `dd09fef` before touching it |
| Target build fails with `-Werror=format` on printf of a counter | `grep -rn 'PRIu32' transmitter/software/main/cmd_net.c brake_light/software/main/cmd_link.c` | On this toolchain `uint32_t` is `long unsigned int`, so `printf("%u", some_uint32)` is a format error under `-Werror` (incident `4571558`). CI catches it on all four transmitter/brake_light matrix rows | Use `%" PRIu32 "` (from `<inttypes.h>`) for uint32_t. uint8_t/uint16_t promote to `int` and are fine as `%u` |
| Build succeeds but wrong console (no `/dev/ttyACM*`, or garbage on UART), wrong indicator GPIO, after switching chips | `head -5 transmitter/software/sdkconfig` (check `CONFIG_IDF_TARGET`) | Stale `sdkconfig` from the previous target. Each firmware has per-target defaults (`sdkconfig.defaults.esp32c3` = USB Serial/JTAG console; `sdkconfig.defaults.esp32` = UART0 115200) that only apply when sdkconfig is regenerated | Re-run `idf.py set-target <target>` — it regenerates `sdkconfig` from the defaults files (both firmware READMEs: "re-run set-target to switch"). Deleting `sdkconfig` then `set-target` is the clean-slate version |
| CMake error about `java`, `Smc.jar`, or `state_machine/brake_fsm.sm` missing | `ls transmitter/software/state_machine tools/smc 2>&1` | You are building a branch that has started DE-09. As of 2026-07-07 **neither `brake_fsm.sm` nor `tools/smc/Smc.jar` exists** — the SMC pre-build step is designed (docs/firmware.md §"State Machine Compiler (SMC) pre-build step", `find_program(JAVA_EXECUTABLE java REQUIRED)`) but unimplemented (DE-09 status 🔲) | On the DE-09 campaign branch: install a JRE and ensure `tools/smc/Smc.jar` is checked in. On main today: this error means someone half-landed DE-09 — see `chmbl-de09-campaign` |
| Host harness (`trc_replay`) fails to build | `cmake -S transmitter/software/test_host -B transmitter/software/test_host/build -DCMAKE_BUILD_TYPE=Release && cmake --build transmitter/software/test_host/build` | It compiles `main/can_decode.c` + the generated profile with `-Wall -Wextra -Werror` and no ESP-IDF; any new warning in the decode core breaks it | Fix the warning in the decode core; do not weaken the flags. Verified building clean in this container |

Sibling: recreate-from-scratch environment problems → `chmbl-build-and-env`.
Build-tree slimming history (`COMPONENTS=main`, commits `f4ef5a6`/`6b189e6`) →
`chmbl-failure-archaeology`.

---

## (b) Frozen or wrong derived acceleration

This is the project's flagship trap. The bus has no brake-switch bit, so
braking is inferred from the slope of wheel speed — if the derived
acceleration is wrong, the whole product is wrong.

**The live bug (open as of 2026-07-07, fix reserved for the DE-09 campaign):**
`transmitter/software/main/can_decode.h` sets `CAN_DECODE_SPEED_HIST 16` with
`CAN_DECODE_ACCEL_WINDOW_MS 200`. Wheel speed (CAN ID 0x102) arrives at
~100 Hz on the reference bike, so a 16-deep ring spans only ~150 ms.

**Mechanism** (read `accel_update()` in `can_decode.c`): each new sample is
pushed into the ring, then the code walks the ring from newest to oldest
looking for the first sample **at least 200 ms old**. With the ring spanning
~150 ms, no such sample exists, the function hits
`return; /* window not yet spanned — keep previous accel */`, and the derived
acceleration only ever updates on rare >200 ms frame gaps — i.e. it is
effectively **frozen**, so the FSM would miss most decelerations.

**Sizing law:** `SPEED_HIST ≥ ACCEL_WINDOW_MS × wheel-speed frame rate`
(0.2 s × 100 Hz = 20 minimum; the tool uses **32** for margin, ~320 ms).
Discovered tool-side first (`f44b0ed` frozen accel in trc_viz, after `bca0d14`
stuck gauges; `6930e19` added the pre-slope low-pass) — the trc_viz.py header
documents the firmware bug explicitly.

| Symptom | First check | Likely cause | Fix / status |
|---|---|---|---|
| Firmware/host accel barely moves; range implausibly narrow; FSM never triggers | Discriminating experiment below | `SPEED_HIST=16` ring can't span the 200 ms window at 100 Hz | **Open bug.** The fix (deepen the ring per the sizing law) is gated to the DE-09 campaign — see `chmbl-de09-campaign`; do not fix it drive-by |
| Accel updates but is spiky / triggers spuriously | Rerun trc_viz with `--speed-smooth-ms 0` vs default 80 and compare FSM transitions | Missing/insufficient pre-slope low-pass: quantization steps and single-sample glitches inject slope spikes (`6930e19`; 80 ms EMA is the knee, ≥200 ms attenuates genuine hard braking) | Tunable `speed_smooth_ms` (trc_viz slider/flag); firmware equivalent lands with DE-09 |
| Gauges/readouts frozen in trc_viz while needles move | `git show bca0d14 --stat` (tool history) | Dear PyGui draw_text nodes update via `configure_item(text=...)`, not `set_value` | Fixed in `bca0d14`; pattern to remember when extending the dashboard |

**Discriminating experiment — firmware-derived accel vs tool replay of the
same capture.** Both commands consume the identical `.trc`; the only
difference is the ring depth (16 in the C core vs 32 in the tool):

```bash
# 1. Firmware decode core (SPEED_HIST=16), host-built:
cmake -S transmitter/software/test_host -B transmitter/software/test_host/build -DCMAKE_BUILD_TYPE=Release
cmake --build transmitter/software/test_host/build
transmitter/software/test_host/build/trc_replay logger/40mph_drive_cycle.trc > /dev/null
#   stderr summary (verified in this container):
#   frames=315869 accel_mphps_min=-4.73 accel_mphps_max=4.00 ...

# 2. Tool port (SPEED_HIST=32):
uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check
#   verified: accel range -6.8 .. +21.0 mph/s
```

The C core reports an accel envelope of **−4.7 … +4.0 mph/s**; the corrected
derivation reports **−6.8 … +21.0 mph/s** on the same ride. A ride that
reaches 43 mph in ~220 s obviously exceeds +4 mph/s of acceleration — the
compressed envelope is the frozen-ring signature. If instead BOTH paths agree
and both look wrong, suspect the profile/DBC (section e), not the ring.

On live hardware the same comparison is `sig show` (accel row) on the
transmitter console while riding/replaying, vs the tool on a capture of the
same maneuver — see `chmbl-run-and-operate` for console/capture procedure.

---

## (c) ESP-NOW link problems

Three CLI commands discriminate the causes (all on the developer serial
console; syntax verified from `cmd_pair.c`, `cmd_net.c`, `cmd_link.c`):

- `pair status` (both boards) — prints `paired with <mac>` or `not paired`.
- `net show` (transmitter) — peer, running/stopped, rate, seq, sent ok/fail.
- `link show` (brake_light) — status `WAITING (no packet yet)` / `UP` / `LOST`,
  last state, last seq, last-rx age vs the failsafe timeout, and rx counters:
  ok, dropped (stale/replay), dropped (unpaired sender).

| Symptom | First check | Likely cause | Fix / status |
|---|---|---|---|
| `pair start` on both boards; one says paired, the other times out | Read `git show d628425` in full | **Asymmetric pairing race**: the board already broadcasting hears the second board's very first announcement almost instantly (bench-proximity latency) — often before its own next 200 ms broadcast — and used to go silent on discovery, so the second board could exhaust its whole window having never heard one packet back | Fixed in `d628425`: `pairing_start()` keeps broadcasting for a grace period after discovery (`PAIR_GRACE_SENDS` = 8 × 200 ms ≈ 1.6 s). If this recurs, verify the grace loop is intact in **both** `pairing.c` files (transmitter and brake_light have parallel copies) |
| brake_light never lights, `link show` says `WAITING (no packet yet)` | `pair status` on both boards | Never paired (or `pair clear` was run): no NVS-persisted peer. Also check `net show` on the transmitter — `state : stopped` means the heartbeat is paused | Run `pair start` on both boards within the same window (`CHMBL_PAIR_TIMEOUT_S`); peer persists to NVS so later boots re-pair silently |
| Was working, `link show` now `LOST`, last-rx age exceeds the timeout | `net show` on the transmitter: are `sent ok` counters advancing? | Link loss: transmitter off/out of range/stopped (`net stop` is the deliberate way to provoke this for DE-03 testing) | Expected fail-honest behavior — the brake_light must show a distinct link-loss indication, never a latched BRAKE. If `sent ok` advances but nothing arrives, check both boards' `CHMBL_NET_CHANNEL` Kconfig match — there is no channel agreement in the protocol |
| `link show` `UP` but `dropped (stale/replay)` climbing | Watch `last seq` vs `net show`'s `seq` | Stale-sequence rejection: `link_on_rx()` drops any packet whose 16-bit seq is ≤ the last accepted one (replay/out-of-order guard). A transmitter reboot resets its seq to a lower value than the brake_light last saw | A handful after a transmitter reboot is normal; continuous climbing with two transmitters alive means a duplicate/rogue peer |
| `dropped (unpaired sender)` climbing | `pair status` on the brake_light vs the transmitter's actual MAC | `net.c` drops heartbeats whose source MAC ≠ the paired peer — a third board, or the boards paired to different partners | Re-run `pair start` on the intended pair; `pair clear` first if a wrong peer is persisted |

Sibling: pairing/console operating procedure → `chmbl-run-and-operate`;
protocol/ESP-NOW background → `chmbl-can-reference`.

---

## (d) Logger problems (ESP-WROVER-KIT + SN65HVD230)

| Symptom | First check | Likely cause | Fix / status |
|---|---|---|---|
| Capture misses frames; a 100 Hz ID (e.g. wheel speed 0x102) shows up at ~22 Hz | `grep 'dropped-frames' <capture>.trc` — every capture since the fix carries a `;dropped-frames: N` footer | Historic: default ~128 B stdio buffer flushed an SD block write every few frames, capping the writer near ~400 frames/s against a ~1490 frames/s bus → ~70% drops (incident `687d400`, PR #8) | Fixed in `687d400`: `setvbuf` 32 KB fully-buffered stream + TWAI RX queue 32→128 (~85 ms slack). Field-verified 0 drops at full bus load. A capture with a nonzero footer is self-documenting — do not calibrate from it |
| Logger crashes ~5 s after boot, task-watchdog message, before any recording | `idf.py monitor` — look for the task WDT report naming `can_rx` / starved `IDLE0` | Historic: TWAI started at boot; with no transceiver or floating RX the controller received continuous garbage, `twai_receive` never blocked, and the priority-6 RX task starved IDLE0 (incident `b860f06`) | Fixed in `b860f06`: TWAI is gated to recording (`writer_start_recording()` starts it, stop stops it; RX task sleeps 20 ms idle, 100 ms receive timeout while recording). If it recurs, someone reintroduced `twai_start()` at init |
| "Why doesn't the LCD work?" | `sed -n '/LCD/,/dead end/p' logger/software/README.md` | **Permanently dead — do not retry.** The WROVER-KIT hardwires the LCD's DC line to GPIO21, which is also the microSD socket's card-detect switch; confirmed on hardware across `4f17d58`→`ffb8cbf`→`5462acb`→`85587d7` (full ST7789/ILI9341 bring-up worked with the card removed, never updated with a card inserted). SD is mission-critical; DC has no alternate pin | Fenced off. Status is conveyed on the onboard **red LED die, GPIO0** instead (`51991ae`): slow blink idle, solid recording, fast blink fatal error. Green (GPIO2)/blue (GPIO4) are also unusable — they're SD D0/D1 |
| Fast-blinking red LED at boot | `idf.py monitor` for the ops log | Fatal error state: microSD mount, file-open, or CAN-start failure | Check card seating/format; the serial log names the failing step |

Sibling: logger field operation (button, file numbering, retrieving captures)
→ `chmbl-run-and-operate`; the LCD retirement as a methodology exemplar →
`chmbl-research-methodology`.

---

## (e) Decode disagreements (golden_check failures)

`tools/golden_check.py` replays `logger/40mph_drive_cycle.trc` through the
host-built C decoder (`trc_replay`) AND python-cantools from the same DBC, and
asserts every decoded value matches (rel 1e-4 / abs 1e-3). A failure has
exactly three look-alike causes. Discriminate in this order:

```bash
# 0. Reproduce (all verified working in this container):
pip install -r tools/requirements.txt
cmake -S transmitter/software/test_host -B transmitter/software/test_host/build -DCMAKE_BUILD_TYPE=Release
cmake --build transmitter/software/test_host/build
python3 tools/golden_check.py
# PASS looks like: "PASS: 183944 signal values identical ..."

# 1. STALE GENERATED PROFILE? Regenerate and diff (exact CI command):
python3 tools/gen_profile.py profiles/triumph_tr.dbc \
  --name "Triumph Speed 400 / Scrambler 400X (TR-series)" \
  --bitrate 500000 \
  --symbol bike_profile_triumph_tr \
  --out /tmp/bike_profile_triumph_tr.c
diff -u transmitter/software/main/bike_profile_triumph_tr.c /tmp/bike_profile_triumph_tr.c
```

| diff result | golden_check result | Diagnosis | Action |
|---|---|---|---|
| Non-empty diff | (any) | **Stale profile**: the DBC changed but the generated C table wasn't regenerated (or someone hand-edited the generated file — forbidden) | Regenerate with the command above targeting the real path, commit DBC + generated file together. CI's staleness gate fails on exactly this diff |
| Empty diff | `MISMATCH frame N <sig>: C=x cantools=y` | **Decoder bug**: the C bit-extraction disagrees with cantools' reference unpacking — usually Motorola "sawtooth" bit-numbering in `can_sig_extract()` (`can_decode.c`), sign extension, or scale/offset | Fix `can_decode.c`; the golden test exists precisely to transfer cantools' correctness onto the C extractor |
| Empty diff | `MISSING` / `EXTRA` rows | **DBC edit changed the signal set** (renamed/added/removed a signal, or changed a frame ID) so the two decoders no longer describe the same signals | Intentional edit → regenerate + commit + update docs/can-profiles.md in the same change (docs-are-spec, see `chmbl-change-control`). Unintentional → revert the DBC |

Notes: the derived predicates `clutch_pulled` / `engine_cutoff`
(cutoff-reason byte must equal 0x28) are recomputed the reference way inside
golden_check itself — a mismatch there implicates the C predicate logic in
`can_decode_feed()`, not the DBC. The check aborts after 20 mismatches; the
FIRST mismatch is the one to debug. If golden_check crashes rather than
failing (e.g. `FileNotFoundError` on the harness), you skipped step 0's build.

Sibling: interpreting a PASSING run's numbers and gen_profile flags →
`chmbl-diagnostics-and-tooling`; adding tolerances/tests → `chmbl-validation-and-qa`.

---

## (f) trc_viz issues

`tools/trc_viz.py` is a uv/PEP 723 script (inline deps: cantools, python-can,
numpy, dearpygui) — run it as `uv run tools/trc_viz.py …`, no venv needed.

| Symptom | First check | Likely cause | Fix / status |
|---|---|---|---|
| GUI fails to start / display errors on a headless machine or container | `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check` | Dear PyGui needs a real display; **only `--headless-check` works without one** — it decodes the capture, derives accel, runs the DE-09 FSM, prints stats (duration, peak speed/rpm/throttle, gears, accel range, transitions, brake-on time, a 43 mph ± 3 peak-speed sanity check), and exits without any GUI | Working as designed. Verified in this container: 220.1 s, 43.2 mph peak, 29 transitions, 89.0 s on, sanity PASS |
| `ModuleNotFoundError: dearpygui` (or cantools/numpy) | `uv --version` | Ran via bare `python3` instead of `uv run`, so the PEP 723 inline dependency block was ignored | Use `uv run tools/trc_viz.py …`; uv resolves the inline deps automatically (verified: 11 packages installed on first run) |
| Numbers differ from the firmware's on the same capture | Section (b)'s discriminating experiment | Intentional: trc_viz uses `SPEED_HIST = 32` where the firmware core still has the buggy 16 (documented in the script's header NOTE) | Not a tool bug — it's the reference-correct derivation until the firmware fix lands via `chmbl-de09-campaign` |
| FSM stats shift when tunables change unexpectedly | `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check --decel-on 3.0` (every `BrakeTunables` field is a `--kebab-case` flag) | You (or a slider) changed a tunable; accel + FSM are recomputed live whenever one changes, including `speed_smooth_ms` | Baseline numbers and tunable semantics live in `chmbl-diagnostics-and-tooling` / docs/firmware.md |

---

## Cross-cutting traps

- **Captures live in `logger/`, not `transmitter/software/captures/`.**
  docs/can-profiles.md tells contributors to commit captures under the latter,
  but the only committed capture is `logger/40mph_drive_cycle.trc`, and the
  referenced bench captures `logger/throttle.trc` / `logger/wheel.trc` are NOT
  in-repo. If a runbook step can't find a capture, this doc-vs-repo gap is why.
- **Never "fix" a decode issue by editing `bike_profile_triumph_tr.c`** — it's
  generated; CI's staleness diff will fail. Edit the DBC and regenerate.
- **Never diagnose CAN problems by transmitting on the bike bus.** All bike-side
  CAN is listen-only by doctrine; use the committed capture, the host harness,
  or the transmitter's `sig source fake` / `sig set` / `sig ramp` /
  `can replay decel` CLI to inject test inputs instead.

## When NOT to use this skill

- Setting up the toolchain / recreating the dev environment → `chmbl-build-and-env`.
- Normal operation (flashing, consoles, pairing ritual, logger field use) → `chmbl-run-and-operate`.
- Interpreting healthy tool output, thresholds, metrics → `chmbl-diagnostics-and-tooling`.
- Full narrative history of settled investigations and reverts → `chmbl-failure-archaeology`.
- Executing the DE-09 implementation (including the SPEED_HIST fix and the SMC step) → `chmbl-de09-campaign`.
- Config axes and every Kconfig knob → `chmbl-config-and-flags`.

## Provenance and maintenance

All facts verified 2026-07-07 on branch `claude/skill-library-continuity-mib7ua`.
Re-verify before trusting, in one line each:

- Incident commits/messages: `git show --stat dd09fef 4571558 d628425 687d400 b860f06 51991ae f44b0ed bca0d14 6930e19`
- SPEED_HIST still 16 (bug still open): `grep -n SPEED_HIST transmitter/software/main/can_decode.h`
- Accel-freeze mechanism unchanged: `grep -n 'window not yet spanned' transmitter/software/main/can_decode.c`
- CLI verbs: `grep -n 'usage:' transmitter/software/main/cmd_{net,pair,sig,can}.c brake_light/software/main/cmd_{link,pair}.c`
- DE-09 / SMC still unimplemented: `ls transmitter/software/state_machine tools/smc 2>&1` (should fail)
- Golden pipeline + staleness-gate commands: `sed -n '65,100p' .github/workflows/firmware-build.yml`
- trc_replay accel envelope (−4.73/+4.00) and golden PASS count (183944): rerun section (e) step 0
- trc_viz headless numbers (43.2 mph, 29 transitions, 89.0 s): `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check`
- LCD dead-end wording: `grep -n 'dead end' logger/software/README.md`
- IDF version pin (driver-vs-esp_driver_twai hinges on it): `grep -n esp_idf_version .github/workflows/firmware-build.yml`
