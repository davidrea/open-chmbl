# CAN bus & bike profiles

The transmitter derives everything from the bike's CAN bus. Brake/throttle/RPM/clutch
are **manufacturer-specific** frames — almost never standard OBD-II PIDs — so each
supported bike needs a **profile**: the CAN IDs and bit layouts to decode. We
reverse-engineer **one reference bike first** (see
[ARCHITECTURE §3](../ARCHITECTURE.md#3-reference-bike-first-strategy)).

---

## 1. Golden rule: listen-only

The ESP32 **TWAI controller runs in listen-only / silent mode.** It must never ACK
frames or transmit on the bus. We are a passive observer on a safety-critical
network; interfering with the bike's ECUs is unacceptable. Verify silent mode is
actually configured before connecting to a real motorcycle.

---

## 2. Connector & wiring

- Euro 5 bikes commonly expose a diagnostic connector carrying **CAN-H / CAN-L**
  (plus 12 V and ground, sometimes K-line). A **6-pin** layout is common but the
  **pinout is not standardized across manufacturers** — confirm yours from a service
  manual or by careful probing.
- Identify CAN-H/CAN-L (≈ 2.5 V idle, differential), 12 V, and ground before
  plugging in the transmitter.
- Note the bus **bit rate** (commonly 500 kbit/s, sometimes 250 kbit/s) — let
  PCAN-Explorer auto-detect, or sweep candidate rates.

---

## 3. Sniffing methodology

Reverse-engineering happens in **two rigs**, because the signals split into "available
while parked" and "only available while moving":

### Rig A — stationary bench (PCAN-USB + PCAN-Explorer)

Used for the signals you can exercise on a stand, and to answer the broadcast-mode
question.

1. **Configure listen-only.** Set the PCAN channel to **listen-only / silent mode**
   in PCAN-Explorer so it never ACKs or transmits while passively observing. Let
   PCAN-Explorer auto-detect (or sweep) the **bit rate** if unknown.
2. **Answer the gating question first (see the risk box below):** with the ignition
   on, is there a continuous stream of frames (free-running broadcast) or near-silence
   until polled? If silent, switch PCAN-Explorer out of listen-only to *actively* send
   OBD/UDS requests and see whether the values only appear on request — that result
   decides the whole architecture.
3. **Correlate one signal at a time** (use PCAN-Explorer's trace + a symbol/`.sym`
   file as you identify fields):
   - Squeeze the **brake lever / pedal** → look for a toggling bit. **On the reference
     bike (Speed 400) repeated captures found none — there is no brake-switch signal on
     the bus.** Confirm this for any new bike, but do not block on it: braking is
     inferred from wheel-speed deceleration (see [firmware.md](firmware.md#braking-state-machine)).
   - Sweep the **throttle** (ride-by-wire, key on) → the byte that ramps 0→100 % →
     `throttle_pct` (**confirmed present**).
   - Pull the **clutch** → the `clutch_pulled` bit (**confirmed present**).
   - Select **gears / neutral** (key on, on a stand) → the gear-position / neutral field
     → `gear`/`neutral` (shown on the cluster, so expected on the bus).
   - Blip the **engine** → the 16-bit, scaled `rpm` field (diagnostics only — the state
     machine no longer needs it).
4. **Record** each as `(can_id, bit offset, length, scale, offset)` and build it up as
   a PCAN-Explorer `.sym`/CANdb so the trace becomes human-readable.

### Rig B — ride logger (ESP-WROVER-KIT, listen-only)

Used for **wheel speed** and anything that only exists in motion. A self-contained
**ESP-WROVER-KIT logger** ([`logger/`](../logger/)) with an external CAN transceiver
records **timestamped** frames to its on-board microSD during real riding — no Linux to
babysit. It captures **all** traffic (no filtering) and writes **PCAN `.trc`** files
that replay through `python-can` exactly like the bench captures. (This replaces the
Raspberry Pi / SocketCAN + `candump` rig originally sketched here.)

- **Listen-only by default** — the TWAI controller never ACKs or transmits (Kconfig
  can switch it to normal/ACK for a two-node bench). It must never transmit on the bus.
- **Operation:** one pushbutton is start/stop — each start opens a new `N.trc`, each
  stop closes it; a running operations log goes to the serial console (`idf.py
  monitor`). Set the bus **bit rate** in `menuconfig` (default 500 kbit/s).
- Capture full rides: roll-ons, coast-downs, hard and gentle braking, stops, and gear
  changes.
- **Wheel speed is the headline signal** — the braking state machine is built on its
  derivative. With wheel speed (plus gear/neutral) you can:
  - compute **road deceleration** (`d(wheel_speed)/dt`, in MPH/s) — the input the
    [FSM](firmware.md#braking-state-machine) thresholds against,
  - tune the on/off acceleration thresholds and the smoothing window against real rides,
    and
  - confirm `clutch_pulled` / `gear` behaviour at stops and launches (the FSM's
    stop-exit logic depends on them).
- **Safety:** the logger is read-only and powered independently; mount it securely,
  start logging before riding, and never operate it while moving. Prefer a closed
  course / helper for the deliberate brake-and-coast runs.

> Wheel-speed-derived deceleration comes from the **bike's own CAN data**, not an
> on-board accelerometer/gyro, so it stays clear of the inertial-detection patent we
> avoid — important now that it is the **primary** braking input, not just calibration
> ground truth.

### Both rigs

5. **Validate** by replaying captures through the decoder offline (`python-can` +
   `cantools` against the `.dbc`/`.sym`) and checking recovered signals match the
   logged actions.
6. Keep **anonymized raw captures** in the repo (PCAN `.trc` and `candump` `.log`) so
   others can re-derive or extend. The **DBC is the committed ground truth**
   (`profiles/<bike>.dbc`); the compact `bike_profile_t` ([§4](#4-profile-data-structure))
   is **generated** from it by [`tools/gen_profile.py`](../tools/gen_profile.py) and
   committed alongside. A host-side golden test
   ([`tools/golden_check.py`](../tools/golden_check.py)) replays the ride capture
   through both the embedded C decoder and `cantools` and asserts they agree, so the
   firmware and the offline validation path can never silently drift apart — see
   [DE-08 §3a](design/de-08-can-decode.md#3a-architecture-decision--dbc--generated-data-table-hand-written-extractor)
   for the full rationale.

---

## 4. Profile data structure

A profile is plain data — adding a bike later is a data change, not new code. It is
**generated** from the bike's DBC by `tools/gen_profile.py` (see §3 step 6 and
[DE-08 §3a](design/de-08-can-decode.md#3a-architecture-decision--dbc--generated-data-table-hand-written-extractor));
the struct below is the generator's output shape
(`transmitter/software/main/bike_profile.h`):

```c
typedef enum {
    CAN_SIG_LE = 0,  // Intel: bit_start is the LSB position
    CAN_SIG_BE = 1,  // Motorola: bit_start is the MSB position (DBC sawtooth numbering)
} can_sig_byte_order_t;

typedef struct {
    uint32_t can_id;     // frame ID carrying this signal; 0 = signal absent
    uint8_t  bit_start;  // DBC start bit (LSB for LE, MSB for BE)
    uint8_t  bit_len;    // field width in bits
    uint8_t  byte_order; // can_sig_byte_order_t
    uint8_t  is_signed;  // two's-complement sign extension when set
    float    scale;      // raw → engineering units
    float    offset;     // raw → engineering units
} can_signal_t;

typedef struct {
    const char  *name;          // e.g. "Triumph Speed 400 / Scrambler 400X (TR-series)"
    uint32_t     bitrate;       // 250000 / 500000
    can_signal_t wheel_speed;   // REQUIRED — primary braking input (decode to km/h or
                                // mph; the FSM works in mph).
    uint8_t      wheel_speed_kmh; // 1 if wheel_speed decodes to km/h (converted to mph
                                // by the decoder).
    can_signal_t clutch_raw;    // clutch_pulled = (raw != 0); gates the stop-exit logic.
    can_signal_t gear;          // gear position; neutral encoded as gear 0.
    // Diagnostics/telemetry — .can_id = 0 if unavailable on this bike:
    can_signal_t wheel_speed_rear, throttle_pct, rpm, rpm_ecu, side_stand_up,
                 engine_cutoff_flag, cutoff_reason;
    uint8_t      cutoff_reason_value; // engine_cutoff = flag && (reason == this)
    // NOTE: no brake_switch — the reference bus does not publish one.
} bike_profile_t;
```

The decoder (`can_decode.c`) filters incoming frames to the profile's IDs and applies
`value = raw * scale + offset` per signal, with sign extension when `is_signed`.
`wheel_speed` is mandatory; if `gear.can_id == 0` the FSM's neutral-aware stop exit
degrades to the stop timeout only (see [firmware.md](firmware.md#braking-state-machine)
and [DE-09](design/de-09-brake-decel-logic.md)).

---

## 5. Reference target — Triumph Speed 400 (TR-series platform)

**Primary target: Triumph Speed 400.** Its sibling, the **Scrambler 400 X**, shares
the same **TR-series 398 cc single** powertrain and ECU, so a single profile is
expected to cover both — capture one, validate on the other.

**Bonus/stretch target: Triumph Street Triple 765.** This is a *different platform*
(765 cc inline-triple, Moto2-derived, higher-spec electronics) with its own ECU, so
it needs its **own profile** — but it uses the same family of diagnostic connector,
so the TX *hardware* should carry straight over.

### Diagnostic connector (verified from owner sources)

| Property | Value | Confidence |
|----------|-------|-----------|
| Connector | **Red 6-pin** OBD2 (Euro 5), under the seat next to the fuse box | Confirmed |
| Adapter | Needs a **6-pin → 16-pin** Triumph/aftermarket adapter for off-the-shelf tools | Confirmed |
| Signals present | **CAN-H / CAN-L** (ISO 11898 / J-2284) **plus** a K-line (legacy ISO 9141) | Confirmed |
| Bit rate | 500 kbit/s | Confirmed |
| Native 6-pin pinout | Maps to the standard 16-pin signals (CAN-H, CAN-L, K-line, 12 V, ground) | Confirmed |

Street Triple 765 uses the same red 6-pin connector located in the tail.

> ✅ **Resolved on the reference bike:** does the diagnostic port expose
> **free-running broadcast** CAN traffic (ECUs continuously chattering, which our
> **listen-only** sniffer can read)? _Yes, we have confirmed this experimentally._
> Our passive, listen-only design depends on the former. If wheel speed / throttle /
> clutch are only available on request, that conflicts with the
> [listen-only golden rule](#1-golden-rule-listen-only) and forces a design rethink
> (e.g. tapping an internal vehicle CAN where the ECUs broadcast among themselves,
> rather than the diagnostic request/response channel).

> ✅ **Resolved on the reference bike: there is no brake-switch signal on the bus.**
> Repeated captures while working the brake found no toggling bit. The architecture
> therefore infers braking from **wheel-speed deceleration** (see
> [firmware.md](firmware.md#braking-state-machine)); `wheel_speed`, `clutch_pulled`,
> `throttle_pct`, `gear`/`neutral` and `rpm` have now all been captured and decoded from
> a real ride — see the [decode table](#decode-table) below.

### Decode table

Byte offsets below are **0-based within the frame payload**; multi-byte fields are
**big-endian** (B*n* is the high byte). Derived from the ride logger capture
[`logger/40mph_drive_cycle.trc`](../logger/40mph_drive_cycle.trc) (a ~220 s Speed 400
ride) cross-checked against the single-signal bench captures
[`logger/throttle.trc`](../logger/throttle.trc) and
[`logger/wheel.trc`](../logger/wheel.trc). See
[Decode notes](#decode-notes-speed-400-reference-capture) below for how each was
confirmed; the decoded overlay is
[`logger/40mph_drive_cycle_decoded.png`](../logger/40mph_drive_cycle_decoded.png).

| Signal | CAN ID | Bytes / bits | Scale / offset | Notes |
|--------|--------|--------------|----------------|-------|
| `wheel_speed` (front) | `0x102` | B1–B2, BE (16-bit) | `raw / 16` → km/h | **Required — primary braking input.** Only field active in `wheel.trc`. |
| `wheel_speed` (rear) | `0x102` | B3–B4, BE (16-bit) | `raw / 16` → km/h | Second near-identical field; tracks front. |
| `clutch_pulled` | `0x142` | B5 (low nibble ≠ 0) | `0x8D` = pulled, `0x80` = released | **Confirmed present.** Pulses around every gear change. |
| `gear` / `neutral` | `0x142` | B3, low nibble | `0` = N, `1`–`4` = gear | Neutral at engine start/stop, as expected. Mirrored as `gear × 2` in `0x25D` B0. |
| `throttle_pct` | `0x140` | B0 | `raw / 2.55` → 0–100 % | **Confirmed present.** Full 0–255 sweep in `throttle.trc`; ~2.5 % at idle (ride-by-wire idle air). |
| `rpm` (live, coarse) | `0x140` | B6 | `raw × ~31.4` → rpm | Tracks true engine speed incl. cranking; **`0` = engine off**. Best on/off indicator. |
| `rpm` (ECU filtered) | `0x146` | B2–B3, BE (16-bit) | `raw × 0.25` → rpm | Smooth; idle 1410, max 4559. Freezes at setpoint when stationary and holds ~1300 after kill — ECU target, not raw crank speed. |
| `side_stand` | `0x481` | B7, bit 0 | `1` = stand up, `0` = down | Flips up shortly after ride starts; `0` in all bench captures (bike on stand). |
| `engine_cutoff` (kill switch) | `0x121` | B3 bit 6 **and** B6 = `0x28` | asserted = kill | Asserts ~30 ms before live rpm decays to 0; never appears in a capture without a kill. |
| `brake_switch` | — | — | — | **Not on the bus** — confirmed absent on the reference bike. |
| Bus bit rate | 500 kbit/s | — | — | Logger default; frames decode cleanly. Confirm by probing on new bikes. |
| Free-running vs. request/response | **free-running broadcast** | — | — | Listen-only logger captured continuous traffic — the risk above is resolved for the reference bike. |

> ⚠️ These IDs/scales are **empirically reverse-engineered** from a single reference
> bike, not from Triumph documentation. `wheel_speed` scale (`/16` → km/h) and the two
> `rpm` scales are calibrated against the ride's known speed/rpm envelope and are
> approximate; re-validate against a speedometer/tach reference before relying on them
> for anything beyond the braking FSM (which only needs the *shape* of `wheel_speed`).
> The `0x140` B0 throttle byte and `0x145` B5 both swept on the bench — B0 is the smooth
> rider-demand signal; `0x145` B5 dithers wildly during the ride and is **not** rider
> throttle.

#### Decode notes (Speed 400 reference capture)

- **Wheel speed** — `0x102` carries two near-identical 16-bit fields (we suspect front
  B1–B2, rear B3–B4). At `raw/16` km/h the ride sustains **~30 mph** early and peaks at
  **43 mph at ~74 % of the powered window**, matching the known ride. It is the only
  field that moves in `wheel.trc`.
- **RPM** — `0x140` B6 is a coarse live tach: its ratio to wheel speed is constant
  within each gear (clean gearbox steps ≈ 0.27 / 0.19 / 0.14 / 0.11), it shows cranking
  at engine start, and it reads **0 whenever the engine is off** — making it the most
  reliable on/off signal. `0x146` B2–B3 correlates 0.98 with it and decodes to textbook
  values at `×0.25` (idle 1410, redline-ish 4559), but it holds the idle setpoint when
  stationary and lingers ~1300 after the kill, so it is the ECU's filtered/target rpm.
- **Throttle** — `0x140` B0 sweeps the full 0–255 in `throttle.trc`, sits ~2.5 % at
  idle, and is smooth. `0x145` B5 also swept on the bench but oscillates 0–100 % during
  the ride (a fast control/dither channel), so it is **not** the rider-throttle signal.
- **Gear** — `0x142` B3 low nibble steps N→1→2→3→4 and back; every change lands inside
  a clutch pulse, and it reads Neutral at both engine start and engine stop, as
  specified for this capture.
- **Clutch** — `0x142` B5 toggles `0x80`→`0x8D` for ~0.2–2 s bracketing each of the
  gear changes.
- **Side stand** — `0x481` B7 bit 0 flips 0→1 shortly after the ride begins and stays
  up; it is 0 (down) in every bench capture, consistent with the bike being on its
  stand. (An earlier `0x121`/`0x113` lamp-cluster candidate instead fires the first time
  the bike exceeds ~10 km/h — that is the warning-lamp self-check clearing, not the
  stand.)
- **Engine cutoff** — `0x121` B3 bit 6 asserts with reason code B6 = `0x28`, and the
  live rpm (`0x140` B6) begins decaying ~30 ms later, reaching 0 within a second. It is
  the only state change that *precedes* the engine dying; in a drive capture with no
  kill this code never appears.
- **Data quirks** — the initial N→1 engagement in this capture has no clutch pulse, and
  gear 1 is selected just before the side stand retracts; these are the only points
  where the trace deviates from expected interlock behaviour.

Commit anonymized raw captures under `transmitter/software/captures/` (e.g.
`speed400_coastdown.log`) so the decode can be re-derived and the Scrambler 400 X /
Street Triple can be compared against it.

### Why this is a good reference choice

- **Two bikes, one profile** (Speed 400 + Scrambler 400 X share the TR-series
  powertrain) → immediate generalization with no extra reverse-engineering.
- **Ride-by-wire** throttle means throttle position is electronic and on the bus.
- Large, active 400 owner community already poking at the diagnostic port.
- The Street Triple shares the connector, so proving the 400 also de-risks the TX
  hardware for the triple.

---

## 6. Generalizing later

Once one bike works end-to-end, supporting more is "just" capturing each bike and
adding a `bike_profile_t`. A future runtime profile selector (button/app) lets one
transmitter serve multiple bikes. Out of scope until the reference bike is proven.
