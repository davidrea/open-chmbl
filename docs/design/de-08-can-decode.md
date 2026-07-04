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
ESP32-C3 TWAI controller + SN65HVD230 transceiver — see
[`hardware.md §1`](../hardware.md#1-transmitter-bike-side).

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

## 4. I/O assignments & configuration
- TWAI TX/RX pins, **listen-only mode**, bit rate (from DE-07), acceptance filter to
  profile IDs.
- Profile bit/scale/offset extraction; per-signal staleness timeouts.

## 5. Firmware module/task decomposition
- `can_rx.c` — TWAI listen-only bring-up (bitrate + single-filter acceptance mask
  derived from the profile's IDs), RX task, source-aware (`can`/`fake`) signal
  snapshot consumed by `sig show` and (later) DE-09.
- `can_decode.c`/`.h` — **pure, host-testable** profile decoder: generic bit
  extractor (Intel/Motorola, signed/unsigned), `value = raw*scale + offset`,
  per-signal staleness → validity, and the derived smoothed
  `accel = d(wheel_speed)/dt` (mph/s). No ESP-IDF includes.
- `bike_profile.h` / `bike_profiles.h` / `bike_profile_triumph_tr.c` — the
  profile descriptor and the generated (committed) Triumph TR-series table
  (§3a).
- `cmd_can.c` / `cmd_sig.c` — CLI (§6).
- Host golden test: `transmitter/software/test_host/trc_replay.c` links
  `can_decode.c` + the generated profile outside ESP-IDF and replays
  `logger/40mph_drive_cycle.trc`; `tools/golden_check.py` diffs its output
  against `cantools` decoding the same capture through `profiles/triumph_tr.dbc`.
  Wired into CI as the `can-decode-golden` job.

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
