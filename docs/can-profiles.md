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
   others can re-derive or extend. The `.dbc`/`.sym` is the ground truth; the compact
   `bike_profile_t` ([§4](#4-profile-data-structure)) is exported from it.

---

## 4. Profile data structure

A profile is plain data — adding a bike later is a data change, not new code:

```c
typedef struct {
    uint32_t can_id;     // frame ID carrying this signal
    uint8_t  bit_start;  // LSB position within the frame
    uint8_t  bit_len;    // field width in bits
    float    scale;      // raw → engineering units
    float    offset;     // raw → engineering units
} can_signal_t;

typedef struct {
    const char  *name;          // e.g. "Triumph Speed 400 / Scrambler 400X (TR-series)"
    uint32_t     bitrate;       // 250000 / 500000
    can_signal_t wheel_speed;   // REQUIRED — primary braking input (decode to km/h or
                                // mph; the FSM works in mph).
    can_signal_t clutch_pulled; // 1-bit; gates the stop-exit logic.
    can_signal_t gear;          // gear position; neutral encoded as gear 0 (or a
                                // dedicated neutral bit). .can_id = 0 if unavailable.
    can_signal_t throttle_pct;  // 0..100 %; diagnostics/telemetry.
    can_signal_t rpm;           // engine rpm; diagnostics/telemetry.
    // NOTE: no brake_switch — the reference bus does not publish one.
} bike_profile_t;
```

The decoder filters incoming frames to the profile's IDs and applies
`value = raw * scale + offset` per signal. `wheel_speed` is mandatory; if `gear.can_id
== 0` the FSM's neutral-aware stop exit degrades to the stop timeout only (see
[firmware.md](firmware.md#braking-state-machine) and
[DE-09](design/de-09-brake-decel-logic.md)).

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
| Connector | **Red 6-pin** OBD2 (Euro 5), under the seat next to the fuse box | High |
| Adapter | Needs a **6-pin → 16-pin** Triumph/aftermarket adapter for off-the-shelf tools | High |
| Signals present | **CAN-H / CAN-L** (ISO 11898 / J-2284) **plus** a K-line (legacy ISO 9141) | High |
| Bit rate | Likely 500 kbit/s — **verify by probing** | Low |
| Native 6-pin pinout | Maps to the standard 16-pin signals (CAN-H, CAN-L, K-line, 12 V, grounds); **confirm exact pin positions** by service manual / probing before connecting | Low |

Street Triple 765 uses the same red 6-pin connector located in the tail.

> ⚠️ **Top risk to resolve first (Phase 2):** does the diagnostic port expose
> **free-running broadcast** CAN traffic (ECUs continuously chattering, which our
> **listen-only** sniffer can read), or only **request/response** diagnostic data
> (KWP2000/UDS — where live values appear *only* after a tester sends a request)?
> Our passive, listen-only design depends on the former. If wheel speed / throttle /
> clutch are only available on request, that conflicts with the
> [listen-only golden rule](#1-golden-rule-listen-only) and forces a design rethink
> (e.g. tapping an internal vehicle CAN where the ECUs broadcast among themselves,
> rather than the diagnostic request/response channel). **Determine this before
> anything else.**

> ✅ **Resolved on the reference bike: there is no brake-switch signal on the bus.**
> Repeated captures while working the brake found no toggling bit. The architecture
> therefore infers braking from **wheel-speed deceleration** (see
> [firmware.md](firmware.md#braking-state-machine)); `wheel_speed`, `clutch_pulled` and
> `throttle_pct` are confirmed present, and `gear`/`neutral` is expected (it's on the
> cluster). `wheel_speed` and `gear` still need their exact IDs/bit layouts captured.

### Decode table (to fill in during sniffing)

| Signal | CAN ID | Bits (start/len) | Scale / offset | Notes |
|--------|--------|------------------|----------------|-------|
| `wheel_speed` | _TBD_ | _TBD_ | → km/h (FSM uses mph) | **Required — primary braking input.** Shown on the cluster, so on the bus; present in motion. |
| `clutch_pulled` | _TBD_ | _TBD_ | 1-bit | **Confirmed present.** Gates the stop-exit logic. |
| `gear` / `neutral` | _TBD_ | _TBD_ | → gear (0 = N) | Shown on the cluster, so expected on the bus. Enables neutral-aware stop exit. |
| `throttle_pct` | _TBD_ | _TBD_ | → 0–100 % | **Confirmed present.** Diagnostics/telemetry. |
| `rpm` | _TBD_ | _TBD_ | → rpm | Diagnostics/telemetry; not used by the FSM. Expect 16-bit. |
| `brake_switch` | — | — | — | **Not on the bus** — confirmed absent on the reference bike. |
| Bus bit rate | — | — | — | Confirm 250 vs 500 kbit/s. |
| Free-running vs. request/response | — | — | — | **Answer the risk above.** |

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
