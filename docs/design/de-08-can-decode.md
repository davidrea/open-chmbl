# DE-08 — Embedded CAN decode

**Status:** 🟢 implemented · **Device(s):** transmitter · **Depends on:** DE-00, DE-07

The on-device CAN reception and profile-based decode that turns raw frames into the
engineering-unit signals the state machine consumes. **Scheduled only after captures
exist** (DE-07) so the [bike profile](../can-profiles.md) is known. Strictly
**listen-only**.

## 1. Scope & isolation boundary
- **In:** TWAI listen-only setup, bit-rate config, ID filtering, frame RX, applying the
  [`bike_profile_t`](../can-profiles.md#4-profile-data-structure) (`wheel_speed`,
  `clutch_pulled`, `gear`/`neutral`, `throttle_pct`, `rpm`), the derived **acceleration**
  (smoothed `d(wheel_speed)/dt` in MPH/s), and per-signal validity/staleness.
- **Out (faked at edges):** upstream is a real bike or a **replayed capture**
  (`can replay`); downstream the state machine (DE-09) is *not* required — we read
  decoded values via `sig show`. The state machine can be tested separately by faking
  `sig set` / `sig ramp`.
- **Isolation test:** feed a recorded capture (or bench bus) → verify `sig show`
  (including derived `accel`) matches the logged actions.

## 2. FFL traceability
TX-CAN-1…5, TX-DEC-1…7.

## 3. Component selection
ESP32-S3 TWAI controller + TCAN330 transceiver (hardware plan reuses the
[`logger/`](../../logger) PCB) — see
[`hardware.md §1`](../hardware.md#1-transmitter-bike-side). Firmware currently still
targets ESP32-C3 pending the hardware port — see
[`transmitter/software/README.md`](../../transmitter/software/README.md).

## 3a. Architecture decision — DBC + generated data table, hand-written extractor

Weighed two options before implementing:

- **(A) Fully hand-coded** — hand-transcribe the [decode table](../can-profiles.md#5-reference-target--triumph-speed-400-tr-series-platform)
  into `bike_profile_t` and hand-write the bit extractor, with no machine
  cross-check against the offline `cantools` validation path already planned
  in [can-profiles.md §3](../can-profiles.md#3-sniffing-methodology).
- **(B) `cantools generate_c_source`** — generate per-message pack/unpack C
  code straight from the DBC.
- **(C, chosen) Hybrid** — the DBC is the committed ground truth
  (`profiles/triumph_tr.dbc`); [`tools/gen_profile.py`](../../tools/gen_profile.py)
  parses it with `cantools` and emits a **data table**
  (`bike_profile_triumph_tr.c`), not decoder code; one generic, hand-written
  extractor (`can_decode.c`) interprets any profile's bit layout at runtime;
  a host-side golden test ([`tools/golden_check.py`](../../tools/golden_check.py))
  replays the reference capture through both the C extractor and `cantools`
  and asserts they agree bit-for-bit.

Rationale: the signal set the FSM (DE-09) consumes is fixed (~6 signals)
regardless of bike — only the bit layout changes across makes/models. Option
C keeps `bike_profile_t` as a small (~150 byte) const struct, preserving "a
new bike is a data change, not new code" ([§6](../can-profiles.md#6-generalizing-later))
and keeping a future runtime profile selector cheap. It eliminates Option A's
real risks — hand-transcription drift and silent bit-extraction bugs — by
routing both the firmware and the offline validation path through the same
DBC. Option B would invert the data-driven design (per-bike generated code +
glue + a message dispatcher) and push profile selection toward compile-time;
it remains an escape hatch for a future bike whose decode is too irregular
for the generic extractor (e.g. multiplexed messages), without disturbing
already-shipped profiles.

Extended the `can_signal_t` descriptor from `docs/can-profiles.md §4` with
`byte_order` (Intel/Motorola) and `is_signed`, since real DBCs need both;
`can-profiles.md §4` has been updated to match.

## 3b. Derived acceleration — two fixes that the FSM depends on

The decoded *signals* were correct from the start (the golden test proves it against
`cantools`). The **derived** `accel` was not, and because DE-09's rule 1 is driven
entirely by it, the firmware FSM could not reproduce the behaviour the DE-09 thresholds
were tuned to. Both faults were recorded in the comments at the top of
[`tools/trc_viz.py`](../../tools/trc_viz.py), which is why the Python reference
deliberately diverged from the firmware constants. Both are now fixed in
`can_decode.[ch]`.

**1. The wheel-speed history ring was too small to span the slope window.**
`CAN_DECODE_SPEED_HIST` was **16**. Wheel speed (`0x102`) arrives at ~100 Hz, so 16
samples span only ~150 ms — *less* than `CAN_DECODE_ACCEL_WINDOW_MS` (200 ms). The
"oldest sample at least 200 ms back" search therefore failed on almost every sample and
the derived acceleration **froze at its last value**, updating only across a rare frame
gap. A silent failure: the signal still looked plausible.

Now **32** (~320 ms at 100 Hz, ~60% margin). The header states the sizing rule
explicitly —

> `CAN_DECODE_SPEED_HIST > CAN_DECODE_ACCEL_WINDOW_MS × frame_rate_hz`

— next to the constant, so widening the window or moving to a faster bus cannot silently
regress it again.

**2. There was no low-pass on wheel speed before the slope.** The tuned reference
smooths speed with a single-pole, dt-aware filter (`speed_smooth_ms`, τ = **80 ms** —
`_smooth_speed()` in `trc_viz.py`) *before* taking the slope; the firmware fed raw
samples straight into `accel_update()`. Wheel speed is quantized to 0.0625 km/h
(~0.039 mph) and the odd single-sample glitch gets through; one quantum inside a 200 ms
window is ~0.19 mph/s, so raw samples inject decel spikes that rule 1 reads as braking.

The same filter is now in `can_decode.c` (`speed_lpf()`, the identical recurrence
`alpha = 1 − exp(−dt/τ)`), per-instance via `can_decode_set_speed_smooth_ms()` so an app
can retune it from Kconfig (`CONFIG_BRAKE_FSM_SPEED_SMOOTH_MS`).

**It affects only the derived accel path.** `sig.wheel_speed` still carries the raw
decoded sample; the ring, the slope and `sig.accel` are the only things that see the
filtered series. The DE-08 golden test compares *signal* values against `cantools` and
is untouched by this — verified, still 183,944/183,944 exact.

The existing exponential smoothing of the *slope* (`CAN_DECODE_ACCEL_ALPHA` = 0.3) is
unchanged.

**Why this mattered**, measured by the DE-09 replay test on the 40 mph ride log
(reference: 29 transitions, 89.0 s of light-on):

| Decode state | FSM transitions | Light on |
|---|---:|---:|
| ring 16, no LPF (as-was) | 12 | 53.3 s |
| ring 32, no LPF | 37 | 89.7 s |
| ring 32, LPF 80 ms (**as shipped**) | 29 | 90.6 s |

Both fixes are needed: the ring size alone recovers the magnitude but leaves the signal
twitchy enough for eight extra short `BRAKING` blips.

## 3c. Promoted to a shared component

The decode core moved out of `transmitter/software/main/` into a real shared ESP-IDF
component, **[`components/chmbl_can/`](../../components/chmbl_can)**, because the
[`logger/`](../../logger) firmware now needs it too (it runs the DE-09 preview on-board
and gates recording on the decoded kill switch).

| Was | Is |
|-----|-----|
| `transmitter/software/main/bike_profile.h` | `components/chmbl_can/bike_profile.h` |
| `transmitter/software/main/bike_profiles.h` | `components/chmbl_can/bike_profiles.h` |
| `transmitter/software/main/can_decode.[ch]` | `components/chmbl_can/can_decode.[ch]` |
| `transmitter/software/main/bike_profile_triumph_tr.c` | `components/chmbl_can/bike_profile_triumph_tr.c` |

Both apps add the repo-root `components/` directory to `EXTRA_COMPONENT_DIRS` in their
top-level `CMakeLists.txt` and name `chmbl_can` in `PRIV_REQUIRES`, so the sources are
compiled **once** rather than duplicated or reached at with `../../` source paths. The
`#include "can_decode.h"` lines did not change. The component declares no ESP-IDF
dependencies, which is what keeps the host harnesses compiling the same files with plain
gcc. See [`components/README.md`](../../components/README.md).

Downstream references updated with the move: the transmitter's `main/CMakeLists.txt`,
the host harness CMake, the CI "generated profile is not stale" diff, and the `--out`
path in `tools/gen_profile.py`'s usage.

## 4. I/O assignments & configuration
- TWAI TX/RX pins, **listen-only mode**, bit rate (from DE-07), acceptance filter to
  profile IDs.
- Profile bit/scale/offset extraction; per-signal staleness timeouts.

## 5. Firmware module/task decomposition
- `can_rx.c` — TWAI listen-only bring-up (bitrate + single-filter acceptance mask
  derived from the profile's IDs), RX task, source-aware (`can`/`fake`) signal
  snapshot consumed by `sig show` and (later) DE-09.
- `components/chmbl_can/can_decode.c`/`.h` — **pure, host-testable** profile decoder:
  generic bit extractor (Intel/Motorola, signed/unsigned), `value = raw*scale + offset`,
  per-signal staleness → validity, and the derived `accel = d(wheel_speed)/dt` (mph/s):
  80 ms low-pass → ≥200 ms slope → α 0.3 smoothing (§3b). No ESP-IDF includes.
- `components/chmbl_can/bike_profile.h` / `bike_profiles.h` /
  `bike_profile_triumph_tr.c` — the profile descriptor and the generated (committed)
  Triumph TR-series table (§3a). Shared component since §3c.
- `cmd_can.c` / `cmd_sig.c` — CLI (§6), transmitter-side.
- `logger/software/main/can_tap.[ch]` — the logger's instance of the decoder: fed every
  received frame (recording or not) and snapshotted under a mutex for the DE-09 preview
  and the automatic recording gate.
- Host golden test: `transmitter/software/test_host/trc_replay.c` links
  `can_decode.c` + the generated profile outside ESP-IDF and replays
  `logger/40mph_drive_cycle.trc`; `tools/golden_check.py` diffs its output
  against `cantools` decoding the same capture through `profiles/triumph_tr.dbc`.
  Wired into CI as the `can-decode-golden` job.
- Host derived-accel test: the DE-09 replay harness (`fsm_replay.c` /
  `tools/fsm_check.py`, CI job `brake-fsm-replay`) is also the regression test for the
  §3b accel derivation — the FSM is so sensitive to it that the two decode bugs show up
  as a 59% error in brake-light on-time.

## 6. CLI hooks
- `can show` — bit rate, driver state, frame/decode counters, dropped frames,
  bus errors, profile IDs.
- `can replay decel` — synthesizes a coast-to-stop CAN vector (packed through
  the real profile via `can_sig_pack`) and feeds it through an **offline**
  decoder instance — bench-testable without a capture file on the device,
  never touches the live decode or the bus.
- `sig show` — all decoded signals, units, validity, active source.
- `sig set <name> <value|na>` / `sig ramp wheel <mph/s> [until <mph>]` — fake
  a signal (ramp drives the same accel smoothing filter as live decode).
- `sig source can|fake` — switch the signal source.

## 7. Isolation acceptance
- A replayed coast-down/braking capture reproduces the correct decoded `sig` values
  (`wheel_speed`, `clutch_pulled`, `gear`/`neutral`, `throttle_pct`, `rpm`), a sane
  derived `accel`, and the validity flags; listen-only confirmed (no frames emitted).
  Verified by the golden test (§5) over the full reference ride
  (183k+ signal values, exact agreement with `cantools`) plus `can replay decel`
  for the bench-synthesized stop scenario.

## 8. Open items
- Free-running broadcast vs. request/response (the [DE-07 gate](../can-profiles.md#5-reference-target--triumph-speed-400-tr-series-platform))
  — **resolved**, free-running (see can-profiles.md §5).
- Compile-time vs. runtime profile selection — **resolved for now**: compile-time
  (`BIKE_PROFILE_DEFAULT` in `bike_profiles.h`), since only one profile exists.
  The data-table architecture (§3a) keeps a future runtime selector (roadmap
  Phase 5) a matter of choosing among registered `bike_profile_t`s rather than
  a firmware rewrite.
- The two `0x102` wheel-speed fields' front/rear assignment is still a
  suspected (not confirmed) mapping — see the decode notes in can-profiles.md
  §5; doesn't affect the FSM (front is the one wired up and used).
- ~~Derived acceleration matches the offline reference.~~ **Resolved** — see §3b: the
  ring was too small to span the slope window and the wheel-speed low-pass was missing.
  Both fixed, and the DE-09 replay test now guards the derivation.
- The accel derivation has **no automated test of its own** against `cantools` the way
  the signals do (`cantools` does not derive acceleration). It is covered indirectly,
  and sensitively, by the DE-09 replay test. If the accel path grows past a slope plus
  two filters, give it a direct host test with synthetic speed series.
