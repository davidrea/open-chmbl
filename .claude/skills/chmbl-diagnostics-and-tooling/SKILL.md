---
name: chmbl-diagnostics-and-tooling
description: Load when you need to RUN or INTERPRET any open-chmbl measurement tool — tools/trc_viz.py (dashboard + --headless-check + FSM tunable flags), tools/golden_check.py (C-vs-cantools golden test, incl. building the host harness), tools/gen_profile.py (DBC-to-C generation + staleness check), the trc_replay host harness CSV, the device serial-CLI diagnostic commands (net/can/sig/state/link/pair — what exists vs docs/cli.md spec), the logger status-LED codes, or the shipped capture-analysis scripts in this skill's scripts/ dir. The rule here is MEASURE instead of eyeball. Do NOT load for what tier of evidence a number is (chmbl-validation-and-qa), for triaging a tool that is broken/won't start (chmbl-debugging-playbook), for building the dev environment itself (chmbl-build-and-env), for flashing/console attach mechanics (chmbl-run-and-operate), or for tunable semantics and where config lives (chmbl-config-and-flags).
---

# Diagnostics and tooling: measure instead of eyeball

open-chmbl infers motorcycle braking from CAN wheel-speed deceleration and shows it
on a helmet LED bar. Every behavioral claim in this project is backed by a **number
from a replay of a committed capture**, not an impression from a plot. This skill is
the operator's manual for each measurement tool: how to run it, what every output
line means, and the reference numbers to compare against.

Jargon (once): **CAN** = Controller Area Network, the vehicle's shared broadcast
bus; a **`.trc`** file is a PCAN-format text log of raw CAN frames with millisecond
timestamps; a **DBC** file is a machine-readable spec of how signals are bit-packed
into CAN frames; **FSM** = finite state machine (here the DE-09 OFF/BRAKING/STOPPED
brake-light logic); **ESP-NOW** = the encrypted 2.4 GHz radio link between bike and
helmet; **DE** = design element (unit of work, docs in `docs/design/`); **uv** = the
Python runner that reads a script's inline PEP 723 dependency header and creates the
env automatically.

The one committed reference capture is `logger/40mph_drive_cycle.trc` — a ~220 s
Triumph Speed 400 ride, 315,882 lines (~22 MB), peak ~43 mph. All reference numbers
below come from replaying it.

## 1. The toolbox at a glance

All commands run from the repo root (`/…/open-chmbl`). Runtimes measured here
2026-07-08 on the committed capture (after first-run dependency download).

| Tool | Question it answers | Command | Runtime |
|---|---|---|---|
| `tools/trc_viz.py --headless-check` | What does the DE-09 FSM do on this ride, with these tunables? | `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check` | ~5 s |
| `tools/trc_viz.py` (GUI) | Where/why does the FSM misbehave? (scrub + live sliders) | same, without the flag (needs a display) | interactive |
| `tools/golden_check.py` | Does the firmware's C decoder agree with cantools bit-for-bit? | build harness, then `python3 tools/golden_check.py` | ~2.5 s |
| `tools/gen_profile.py` | Is the committed generated profile stale vs the DBC? | regen + `diff` (§4) | <1 s |
| `test_host/build/trc_replay` | Raw per-frame decoded values from the *real firmware C code* | `./transmitter/software/test_host/build/trc_replay <trc>` | ~1 s |
| `scripts/trc_stats.py` (this skill) | What's IN a capture? duration, per-ID rates, wheel-speed range, gaps | `uv run .claude/skills/chmbl-diagnostics-and-tooling/scripts/trc_stats.py <trc>` | ~1.5 s |
| `scripts/fsm_metrics.py` (this skill) | Blip count + per-episode stats that `--headless-check` doesn't print | `uv run .claude/skills/chmbl-diagnostics-and-tooling/scripts/fsm_metrics.py <trc> [tunable flags]` | ~2 s |
| Device CLI (`chmbl>` serial shell) | Live device state: link, CAN rx, decoded signals | §6 | live |

Python env: `uv run` handles deps for `trc_viz.py` and the shipped scripts (inline
PEP 723 headers). `golden_check.py` and `gen_profile.py` have no header — run them
with a python that has `pip install -r tools/requirements.txt` (cantools, python-can,
numpy, dearpygui). Environment setup itself: `chmbl-build-and-env`.

## 2. trc_viz.py — the DE-09 calibration bench

`tools/trc_viz.py` decodes a `.trc` through the profile DBC (same
python-can `TRCReader` + cantools path as `golden_check.py`), re-derives
acceleration with a **faithful Python port of `accel_update()` in
`transmitter/software/main/can_decode.c`**, runs the DE-09 FSM from
`docs/design/de-09-brake-decel-logic.md` on a 50 Hz grid, and either prints stats
(`--headless-check`) or opens a Dear PyGui dashboard.

One deliberate divergence from firmware, documented in its header: it uses
`SPEED_HIST = 32` where the firmware's `can_decode.h` has
`CAN_DECODE_SPEED_HIST = 16` — the 16-sample ring can't span the 200 ms slope
window at the bus's ~100 Hz wheel-speed rate, so the firmware's derived
acceleration freezes. That is a live firmware bug whose fix is reserved for the
DE-09 campaign (`chmbl-de09-campaign`); §5 below shows the bug measurably.

### 2.1 CLI flags (verified against the code, 2026-07-08)

```
uv run tools/trc_viz.py <capture.trc> [flags]
```

| Flag | Default | Effect |
|---|---|---|
| `<trc>` (positional) | required | PCAN .trc capture to play back |
| `--dbc PATH` | `profiles/triumph_tr.dbc` | profile DBC to decode with |
| `--headless-check` | off | decode + run FSM, print stats, no GUI, **always exits 0** |
| `--selftest-frames N` | 0 | GUI mode: render N frames sweeping the whole ride, then exit (CI/exercise) |
| `--snapshot PATH.png` | "" | with `--selftest-frames`: dump a framebuffer PNG mid-ride |
| `--<tunable> FLOAT` | see below | override any FSM tunable — **works with `--headless-check` too** |

**Every `BrakeTunables` field is auto-generated as a flag** (underscores →
hyphens). You do NOT need a wrapper to sweep tunables headlessly. The eleven
flags and their *bench defaults* (canonical doc-vs-bench tunables catalog:
`chmbl-config-and-flags` §3 — this table documents the FLAGS; if values
disagree, that catalog wins):

| Flag | Bench default | docs/firmware.md default |
|---|---|---|
| `--decel-on-mphps` | 2.0 | **3.0** |
| `--decel-on-debounce-ms` | 120 | 120 |
| `--stop-speed-mph` | 1.0 | 1.0 |
| `--moving-speed-mph` | 3.0 | 3.0 |
| `--accel-off-mphps` | 0.5 | 0.5 |
| `--accel-off-min-speed-mph` | 5.0 | 5.0 |
| `--steady-band-mphps` | 0.75 | **0.5** |
| `--steady-timeout-ms` | 1500 | **2000** |
| `--stop-timeout-ms` | 60000 | 60000 |
| `--state-min-dwell-ms` | 250 | **150** |
| `--speed-smooth-ms` | 80 | (bench-only: EMA low-pass tau on wheel speed before the slope calc; not in the docs table) |

The bolded four differ: the design doc left those tunables open and the bench
carries its own defaults. When replaying "as designed", pass the docs values
explicitly. Tunable *semantics* and where they'll live in firmware config:
`chmbl-config-and-flags`.

### 2.2 What --headless-check actually computes

Read from `headless_check()` in the code: it (1) re-derives acceleration from the
raw wheel-speed samples with the active `speed_smooth_ms`, (2) runs the FSM over
the 50 Hz grid, (3) prints the stats below, (4) prints one hardcoded sanity line
("peak speed 43 mph ± 3" — specific to the 40 mph reference ride; expect `CHECK`
on any other capture), and (5) **returns 0 unconditionally**. It is a measurement
printer, not a pass/fail gate — you read and cite the numbers yourself
(evidence rules: `chmbl-validation-and-qa`).

Real output, run here 2026-07-08 (defaults; wall time 5.4 s):

```
$ uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check
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

Line-by-line interpretation:

- **duration / grid points** — ride length and the 20 ms FSM poll grid
  (`FSM_DT_MS = 20.0`, per the DE-09 spec's 50 Hz).
- **peak speed / rpm / throttle / gears seen** — decode sanity. On the reference
  ride expect 43.2 mph, ~4522 rpm, ~29.8 %, gears [0..4] (0 = neutral). Wildly
  different numbers on this capture mean a decode problem, not an FSM problem
  (triage: `chmbl-debugging-playbook`).
- **accel range** — min/max of the *derived* acceleration in mph/s. Reference:
  −6.8 .. +21.0 with `speed_smooth_ms=80`. A tiny, near-frozen range (e.g.
  ±0.5) is the SPEED_HIST-too-small signature. The +21 peak is a launch spike
  through the quantized wheel signal; it's why `accel_off` has a min-speed guard.
- **FSM transitions** — total state changes over the ride. Fewer = calmer light.
- **brake light on** — seconds and fraction where state ≠ OFF. Tunings that cut
  transitions must not silently gut on-time (the light still has to come on for
  real braking) — always report both together.

### 2.3 The reference numbers (what "good" looks like)

DE-09 dry-run baselines recorded in `docs/firmware.md` (history: commit
`8192663`), all on `logger/40mph_drive_cycle.trc`. **Historical figures** from an
earlier tool version — today's tool measures 29 transitions / 1 blip at defaults;
the operative DE-09 gate is owned by `chmbl-de09-campaign` Phase 4:

| Mechanism added | FSM transitions | sub-0.5 s blips |
|---|---|---|
| (none — naive thresholds) | 162 | 65 |
| + low-speed hysteresis (`MOVING_SPEED` > `STOP_SPEED`, rolling launch guard) | 48 | 8 |
| + 120 ms decel-on debounce | 30 | 3 |

Honest caveat (verified by running it, 2026-07-08): the exact tunable set that
produced 162/48/30 is not fully recorded in-repo, so you cannot reproduce those
precise numbers from today's defaults. What you CAN reproduce, with
`scripts/fsm_metrics.py` (§7.2), is each mechanism's direction and magnitude:

| Config (measured 2026-07-08) | transitions | blips |
|---|---|---|
| bench defaults | 29 | 1 |
| docs-default tunables, debounce 120 ms | 24 | 1 |
| docs-default tunables, debounce **0** | 489 | 214 |
| no hysteresis, no debounce, no dwell | 3491 | 1710 |

Today's defaults land in-family with the accepted end state (29 vs 30
transitions). If a change pushes transitions far above ~30 or blips above ~3 on
this capture, it has regressed the accepted baseline — changing that baseline is
change-controlled (`chmbl-change-control`).

### 2.4 The GUI dashboard (needs a display)

`uv run tools/trc_viz.py logger/40mph_drive_cycle.trc` opens a 900×780 dark
dashboard: speed + rpm gauges; a bipolar accel/decel bar (red fills left for
decel, green right for accel, an amber marker at −`decel_on_mphps` shows exactly
when braking *should* trigger); a segmented gear bar (N,1–6); throttle bar;
clutch / engine-cutoff readouts; a big OFF/BRAKING/STOPPED state word
(red when non-OFF); a timeline strip chart (speed + brake-on trace) with a
draggable scrub cursor; Play/Pause with 0.5×–4× rates; and a **"DE-09 brake FSM
tunables" panel — one live slider per tunable**. Moving any slider re-derives
acceleration and re-runs the FSM over the whole ride instantly, updating the
brake trace and a `transitions: N  light on: X.Xs (Y%)` stats readout.

Calibration-bench workflow: scrub to a misbehaving moment → adjust sliders while
watching the timeline and the stats line → **then confirm headlessly** with the
same values as `--flags` plus `--headless-check` (and `scripts/fsm_metrics.py`
for blips), and cite those printed numbers. Slider impressions are exploration;
the headless printout is the measurement.

Headless containers: the GUI won't start without a display; `--selftest-frames N
--snapshot out.png` exists for automated UI exercise but still requires a
framebuffer. In this environment only `--headless-check` is usable.

## 3. golden_check.py — C decoder vs cantools

**What it asserts:** replaying the same `.trc` through (a) the host-built C
decoder — the *actual firmware sources* `can_decode.c` +
`bike_profile_triumph_tr.c` — and (b) python-cantools decoding
`profiles/triumph_tr.dbc`, **every decoded signal value on every frame is
identical** within `REL_TOL = 1e-4` / `ABS_TOL = 1e-3` (constants at the top of
`tools/golden_check.py`, compared via `math.isclose`), including two C-side
derived predicates `clutch_pulled` and `engine_cutoff`. This transfers cantools'
bit-unpacking correctness onto the tiny embedded extractor (design:
`docs/design/de-08-can-decode.md`).

**Run it** (host harness must be built first; needs cmake + a C compiler, no
ESP-IDF):

```bash
cmake -S transmitter/software/test_host -B transmitter/software/test_host/build \
      -DCMAKE_BUILD_TYPE=Release
cmake --build transmitter/software/test_host/build
python3 tools/golden_check.py          # needs cantools/python-can installed
```

Flags: `--harness` (default `transmitter/software/test_host/build/trc_replay`),
`--dbc` (default `profiles/triumph_tr.dbc`), `--trc` (default
`logger/40mph_drive_cycle.trc`).

Real output, run here 2026-07-08 (wall time 2.5 s):

```
frames=315869 accel_mphps_min=-4.73 accel_mphps_max=4.00 wheel_speed_mph_last=0.00
PASS: 183944 signal values identical between the C decoder and cantools
```

The first line is the harness's stderr summary (§5); the PASS line is the
verdict: 183,944 (frame, signal) pairs compared, zero disagreements, exit 0.

**Reading a failure** (exit 1). Three row types, from the code:

- `MISSING  frame <n> <signal> (expected <v>)` — cantools decoded it, the C side
  emitted nothing. Typical cause: profile stale vs DBC (run the §4 staleness
  check first) or a signal missing from the harness's emit list.
- `MISMATCH frame <n> <signal>: C=<a> cantools=<b>` — both decoded, values
  disagree beyond tolerance. Typical cause: bit-order/sign/scale error in the C
  extractor or a hand-edited generated profile. Aborts after 20 mismatches
  (`... aborting after 20 mismatches`).
- `EXTRA    frame <n> <signal> = <v> (C only)` — the C side emitted a row
  cantools didn't (first 20 shown).

Then `FAIL: <n> mismatch(es) over <m> compared values`. First discriminating
question on any failure: did the DBC, the generated profile, or `can_decode.c`
change most recently? (Playbook: `chmbl-debugging-playbook`.)

## 4. gen_profile.py — DBC → C profile, and the staleness check

`tools/gen_profile.py` parses a profile DBC with cantools and emits the const
`bike_profile_t` C table the firmware decoder interprets, so firmware and the
offline python path decode from one source. The output is **generated AND
committed** (`transmitter/software/main/bike_profile_triumph_tr.c`) — never
hand-edit it.

Flags: positional `dbc`; `--name` (human-readable profile name), `--bitrate`
(bus bit/s), `--symbol` (C symbol + file base name) — all three **required**;
`--out PATH` (default stdout).

**The FIELD_MAP contract** (top of the script): a dict maps DBC signal names →
`bike_profile_t` fields (`WHEEL_SPEED_FRONT`→`wheel_speed`, `CLUTCH_RAW`,
`GEAR`, `THROTTLE_PCT`, `RPM_LIVE`, `RPM_ECU`, `SIDE_STAND_UP`,
`ENGINE_CUTOFF_FLAG`, `CUTOFF_REASON`, `WHEEL_SPEED_REAR`). Enforcement, all
hard exits: any DBC signal **not** in FIELD_MAP → error ("add it to FIELD_MAP")
— nothing silently falls out of the profile; duplicate signal names → error;
missing any of the REQUIRED set `{WHEEL_SPEED_FRONT, CLUTCH_RAW, GEAR}` → error.
Signals in FIELD_MAP but absent from the DBC emit `.can_id = 0 /* not on this
bike */`. `wheel_speed_kmh` is set iff `WHEEL_SPEED_FRONT`'s DBC unit string is
`km/h`; the kill-switch `cutoff_reason_value` 0x28 is a constant in the script
(source: `docs/can-profiles.md` §5).

**Staleness check** — exactly what CI's `can-decode-golden` job runs; a clean
diff means the committed profile matches the DBC:

```bash
python3 tools/gen_profile.py profiles/triumph_tr.dbc \
  --name "Triumph Speed 400 / Scrambler 400X (TR-series)" \
  --bitrate 500000 --symbol bike_profile_triumph_tr \
  --out /tmp/bike_profile_triumph_tr.c
diff -u transmitter/software/main/bike_profile_triumph_tr.c /tmp/bike_profile_triumph_tr.c
```

Changed the DBC? Regenerate with `--out transmitter/software/main/
bike_profile_triumph_tr.c` and commit both files together (rules:
`chmbl-change-control`).

## 5. trc_replay — the host harness and its CSV

`transmitter/software/test_host/build/trc_replay <capture.trc>` (built in §3)
parses the `.trc` text format directly in C and feeds every frame through
`can_decode_feed()` — the real firmware decode path, ~1 s for the full capture.

**stdout — one CSV row per decoded signal per frame:**

```
frame_index,can_id,signal,value
```

`frame_index` is 1-based over ALL frames in the file (including IDs the profile
ignores — that's why signal rows are sparse); `can_id` is hex (`0x102`); `value`
is `%.6f`. Signal rows use DBC names **in DBC units** (wheel speed rows are
km/h, matching cantools), plus two derived predicate rows (`clutch_pulled`,
`engine_cutoff`, 0.0/1.0) emitted from the decode state. Real first rows on the
reference capture:

```
4,0x102,WHEEL_SPEED_FRONT,0.000000
4,0x102,WHEEL_SPEED_REAR,0.000000
6,0x140,THROTTLE_PCT,5.490198
6,0x140,RPM_LIVE,0.000000
8,0x142,CLUTCH_RAW,0.000000
8,0x142,GEAR,0.000000
8,0x142,clutch_pulled,0.000000
```

**stderr — one summary line at EOF:**

```
frames=315869 accel_mphps_min=-4.73 accel_mphps_max=4.00 wheel_speed_mph_last=0.00
```

`frames` = total frames parsed (315,869 data lines of the 315,882-line file —
the rest are `;` header lines). `accel_mphps_min/max` track the *firmware's*
derived acceleration in mph/s; `wheel_speed_mph_last` is the final decoded
speed in mph (the ride ends stopped).

**Interpretation — the SPEED_HIST bug, measured:** the harness compiles the
firmware's `CAN_DECODE_SPEED_HIST = 16`, so its accel range is the frozen
−4.73 .. +4.00, while trc_viz's 32-sample ring on the same capture reports
−6.8 .. +21.0 (§2.2). That side-by-side is the quantitative signature of the
open bug: the 16-deep ring rarely holds a sample ≥200 ms old at ~100 Hz, so the
slope only updates on occasional frame gaps. After the DE-09 campaign fixes the
sizing, expect the two ranges to converge; re-run both to verify.

Exit codes: 0 on success, 2 on usage/file-open error. The accel numbers are
diagnostics only — `golden_check.py` does not compare them.

## 6. Device-side diagnostics (serial CLI + logger LED)

The `chmbl>` shell (ESP-IDF `esp_console` REPL over USB Serial/JTAG on
ESP32-C3, UART0 on classic ESP32; attach mechanics: `chmbl-run-and-operate`).
**`docs/cli.md` is a spec that is only partially implemented.** The canonical
implemented-vs-spec inventory lives in `chmbl-run-and-operate` §2 — the excerpt
below covers only the diagnostic read-out commands this skill interprets
(verified against `*/software/main/cmd_*.c` + `console.c`, 2026-07-08):

**Transmitter — implemented:** `help`, `id`, `state`, `pair`, `net`, `can`, `sig`.
**Brake_light — implemented:** `help`, `id`, `light`, `pair`, `link`.
**Spec-only, NOT in code** (docs/cli.md lists them; don't expect them on a
device): `version`, `reset`, `config show/set/save`, `stats`, `log <level>`,
`power show`; brake_light's entire `in`/`ambient`/`batt`/`render`/`led`/
`bright`/`ind` domains; transmitter `can replay <name>` for stored captures
(only the synthetic `can replay decel` exists); `state force ... auto`.

What each implemented diagnostic reports (from the source):

| Command | Output fields |
|---|---|
| `net show` (TX) | peer MAC (or `unpaired`), `state: running/stopped`, heartbeat `rate` Hz, `seq`, `sent: N ok, M fail`. Also `net rate <1-50>`, `net send` (one heartbeat now), `net start`/`net stop` — stop is the bench way to provoke the brake_light's link-loss behavior. |
| `can show` (TX) | active profile name, bitrate + `(listen-only)`, driver running/NOT RUNNING, `frames: N rx, M decoded`, `dropped` (rx queue) + bus errors, `last rx: N ms ago`, and the profile's CAN IDs. First stop for "is the bike talking to us?" |
| `can replay decel` (TX) | packs a synthetic 40 mph coast-then-brake (−12 mph/s) vector through the real decoder (offline instance, transmits nothing) and prints t/wheel/accel/gear/clutch every 250 ms — a bench self-test of decode + accel derivation. |
| `sig show` (TX) | `source: can/fake`, then every signal with value, unit, and validity `yes` / `STALE` / `no` (STALE = seen but older than `CAN_DECODE_STALE_MS` = 1000 ms). |
| `sig set/ramp/source` (TX) | fake inputs for bench FSM work: `sig set wheel <mph>`, `sig ramp wheel <mph/s> [until <mph>]` (constant `sig set` gives accel ≈ 0 — ramp is how you exercise decel thresholds), `sig set clutch/gear/throttle/rpm …`, `sig source can|fake`. Fakes never touch the CAN bus (there is no transmit path). |
| `state` (TX) | current stand-in output state + indicator GPIO; `state off|brake` forces it (DECEL reserved, never TX-emitted). This is the pre-DE-09 stand-in, not the FSM. |
| `pair status` (both) | `paired with <mac>` or `not paired`; `pair start` / `pair clear`. |
| `link` (BL) | `status: WAITING/UP/LOST`, last state + seq, `last-rx age: N ms (timeout M ms)`, `rx: N ok, M dropped (stale/replay), K dropped (unpaired sender)`. Read this while `net stop`-ing the TX to watch failsafe behavior. |
| `light` (BL) | stand-in brake-light GPIO show/`on`/`off`/`toggle`. |
| `id` (both) | chip model/rev/cores, features, base MAC, 64-bit unique ID, IDF version. |

**Logger status LED** (no CLI; ESP-WROVER-KIT onboard **red** die on GPIO0 — the
only usable leg, the LCD and green/blue legs are hardwired into microSD lines;
per `logger/software/README.md` and timings from `status_led.c`):

| Pattern | Timing (code) | Meaning |
|---|---|---|
| Slow heartbeat | 60 ms on / 1940 ms off (2 s period) | mounted, ready, not recording |
| Solid on | continuous | actively recording |
| Fast blink | 100 ms on / 100 ms off (5 Hz) | fatal error: microSD mount / file-open / CAN-start failure |

GPIO number and active-low polarity are menuconfig options
(`chmbl-config-and-flags`). Field procedure for the logger:
`chmbl-run-and-operate`.

## 7. Shipped scripts (in this skill's scripts/ dir)

Both are PEP 723 scripts (`uv run` just works; or plain `python3` after
`pip install -r tools/requirements.txt`). Both were executed here 2026-07-08
against the committed capture; outputs below are real.

### 7.1 trc_stats.py — capture summarizer

First tool to run on ANY new `.trc` before deeper analysis: is the capture
healthy, complete, and does it contain the signals you need?

```
$ uv run .claude/skills/chmbl-diagnostics-and-tooling/scripts/trc_stats.py \
      logger/40mph_drive_cycle.trc
capture         : logger/40mph_drive_cycle.trc
duration        :    220.1 s
frames          :   315869  (1435 fps overall)
distinct IDs    :       31

    ID   frames  rate Hz  meaning
 0x149    21272     96.7
 0x141    21272     96.7
 0x140    21271     96.7  engine live: throttle, rpm
 0x142    21271     96.7  transmission: gear, clutch
 0x144    21267     96.6
 0x145    21263     96.6
 0x146    21261     96.6  engine ECU: rpm (x0.25)
 0x102    21254     96.6  wheel speeds (front/rear)
 0x147    21253     96.6
 0x148    21250     96.6
 0x113    10628     48.3
 0x112    10625     48.3
 0x259    10619     48.3
 0x25A    10619     48.3
 0x25B    10617     48.2
   ...    50127           (16 more IDs)

wheel speed     : 0.0 .. 43.2 mph (21254 frames @ 96.6 Hz on 0x102)
wheel gaps >200ms:     3   (t_rel_s, gap_ms): (13.7, 270), (49.6, 270), (170.2, 210)
```

(1.4 s wall time.) Flags: `--dbc` (default `profiles/triumph_tr.dbc`),
`--gap-ms` (default 200 — the firmware's `CAN_DECODE_ACCEL_WINDOW_MS`), `--top N`
(IDs listed by rate, default 15, 0 = all). Exit 1 if the file has no readable
frames.

Interpretation: named IDs are the decode table's (`docs/can-profiles.md` §5;
domain background: `chmbl-can-reference`). Healthy reference-bike numbers:
~1435 fps overall, ~31 distinct IDs, the 0x14x/0x102 group at ~97 Hz, wheel
0.0–43.2 mph. **Wheel gaps > 200 ms** are windows where wheel-speed frames
stopped arriving longer than the accel slope window — during each, the derived
acceleration cannot update and the FSM flies blind; the committed ride has
exactly 3 (a stopped bus goes quiet briefly). A new capture with many gaps means
logger frame drops (history: commit `687d400`; triage:
`chmbl-debugging-playbook`). Note the frame count (315,869) matches
`trc_replay`'s — cross-tool agreement you can check cheaply.

### 7.2 fsm_metrics.py — blips and episode stats

`--headless-check` prints transitions and on-time but **not the blip count**,
which is half of every accepted baseline (162→48→30 *and* 65→8→3). This script
fills exactly that gap — it imports `load_log`/`derive_accel`/`run_fsm` from
`tools/trc_viz.py` itself (never a reimplementation), accepts every tunable
under the same flag names, and prints per-episode stats. A "blip" = a light-on
episode shorter than `--blip-s` (default 0.5 s, matching `docs/firmware.md`).

```
$ uv run .claude/skills/chmbl-diagnostics-and-tooling/scripts/fsm_metrics.py \
      logger/40mph_drive_cycle.trc
capture          : logger/40mph_drive_cycle.trc
tunable overrides: (none — trc_viz defaults)
FSM transitions  :     29
light-on episodes:     14
blips (<0.5 s)    :      1
on-time          :     89.0 s (40% of ride)
episode duration : min 0.28 s, median 6.41 s, max 16.24 s
blip times (s)   : 190.9
```

(2.1 s wall time.) Transitions/on-time agree with `--headless-check` (29 /
89.0 s) — same FSM, by construction. Extra flags: `--dbc`, `--trc-viz` (path to
`tools/trc_viz.py`, defaults to the repo-relative location), `--blip-s`. Run it
from the repo root. Always exits 0 (measurement, not gate). The §2.3
mechanism-ablation table was produced with it, e.g.:

```
uv run .claude/skills/chmbl-diagnostics-and-tooling/scripts/fsm_metrics.py \
    logger/40mph_drive_cycle.trc \
    --moving-speed-mph 1.0 --decel-on-debounce-ms 0 --state-min-dwell-ms 0
# → transitions 3491, blips 1710  (hysteresis + debounce + dwell all disabled)
```

Blip times are where to scrub to in the GUI: measure → locate → understand →
retune → re-measure.

## When NOT to use this skill

- Deciding what a number *proves* (evidence tiers, acceptance thresholds,
  what CI gates) → `chmbl-validation-and-qa`.
- A tool won't run / build fails / output looks impossible →
  `chmbl-debugging-playbook`.
- Installing ESP-IDF, cmake, uv, python deps from scratch → `chmbl-build-and-env`.
- Flashing, attaching the serial console, pairing procedure, logger field ops →
  `chmbl-run-and-operate`.
- What a tunable *means* / where config lives / adding a config axis →
  `chmbl-config-and-flags`.
- CAN/DBC/.trc/ESP-NOW theory → `chmbl-can-reference`.
- Designing the experiment around a measurement → `chmbl-research-methodology`.
- Actually implementing DE-09 in firmware → `chmbl-de09-campaign`.

## Provenance and maintenance

All commands, outputs, and numbers above were executed/verified in this repo on
branch `claude/skill-library-continuity-mib7ua`, 2026-07-08 (Python 3.11 host;
runtimes are ballpark for a ~2020s dev container). Re-verify before trusting:

| Fact class | Re-verification command |
|---|---|
| trc_viz flags + tunable defaults | `uv run tools/trc_viz.py --help` (or read `BrakeTunables` + `main()` in `tools/trc_viz.py`) |
| headless-check output + exit-0 behavior | `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check; echo $?` |
| Reference FSM numbers (162→48→30, 65→8→3) | `grep -n "162\|48\|30" docs/firmware.md` (§ around "cut FSM transitions") |
| Docs tunables table | `sed -n '104,118p' docs/firmware.md` |
| golden tolerances + defaults | `grep -n "TOL\|default=" tools/golden_check.py` |
| golden PASS count (183,944) | build harness (§3) then `python3 tools/golden_check.py` |
| gen_profile FIELD_MAP / REQUIRED | `sed -n '29,45p' tools/gen_profile.py` |
| Staleness-check command matches CI | `grep -n -A6 "gen_profile" .github/workflows/firmware-build.yml` |
| trc_replay CSV + stderr format | read `transmitter/software/test_host/trc_replay.c` (`emit()`, final `fprintf`) |
| Firmware SPEED_HIST still 16 (bug open) | `grep -n SPEED_HIST transmitter/software/main/can_decode.h` |
| Implemented CLI commands | `grep -rn "\.command" transmitter/software/main/cmd_*.c brake_light/software/main/cmd_*.c` |
| Logger LED timings | read `status_led_task()` in `logger/software/main/status_led.c` |
| Shipped-script outputs | re-run the two commands in §7 against `logger/40mph_drive_cycle.trc` |
