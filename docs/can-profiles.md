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
   - Squeeze the **brake lever / pedal** → which ID + bit toggles → `brake_switch`
     (check front and rear separately).
   - Sweep the **throttle** (ride-by-wire, key on) → the byte that ramps 0→100 % →
     `throttle_pct`.
   - Blip the **engine** → the 16-bit, scaled `rpm` field.
   - Pull the **clutch** → the `clutch_pulled` bit (may be absent on the 400 single).
4. **Record** each as `(can_id, bit offset, length, scale, offset)` and build it up as
   a PCAN-Explorer `.sym`/CANdb so the trace becomes human-readable.

### Rig B — ride logger (Raspberry Pi / Pi Zero, listen-only)

Used for **wheel speed** and anything that only exists in motion. A Pi with a CAN
interface (CAN HAT via SocketCAN, or the same PCAN-USB) runs `candump`/`python-can`
to log **timestamped** frames during real riding.

- **Configure the SocketCAN interface listen-only** (`ip link set canX type can
  bitrate <rate> listen-only on`) — the logger must never transmit on the bus.
- Capture full rides: roll-ons, coast-downs, engine-braking, and gear changes.
- **Wheel speed** is the prize here. With wheel speed + RPM you can:
  - cluster `wheel_speed / rpm` into discrete **gears**,
  - compute **true road deceleration** (`d(wheel_speed)/dt`) to calibrate the
    `DECEL` thresholds against reality (RPM derivative alone is gear-dependent), and
  - confirm `clutch_pulled` behaviour (RPM and wheel speed decouple when the clutch
    is in).
- **Safety:** the logger is read-only and powered independently; mount it securely,
  start logging before riding, and never operate it while moving. Prefer a closed
  course / helper for the deliberate brake-and-coast runs.

> Wheel-speed-derived deceleration comes from the **bike's own CAN data**, not an
> on-board accelerometer/gyro, so it stays clear of the inertial-detection patent we
> avoid. Treat it primarily as **calibration ground truth**; whether to also feed it
> into the live `DECEL` logic is an open question (see [roadmap](roadmap.md)).

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
    can_signal_t brake_switch;  // expect 1-bit
    can_signal_t throttle_pct;  // 0..100 %
    can_signal_t rpm;           // engine rpm
    can_signal_t clutch_pulled; // 1-bit; .can_id = 0 if bike doesn't expose it
    can_signal_t wheel_speed;   // optional; .can_id = 0 if unused. Calibration
                                // ground truth; candidate input to a future DECEL.
} bike_profile_t;
```

The decoder filters incoming frames to the profile's IDs and applies
`value = raw * scale + offset` per signal. If `clutch_pulled.can_id == 0`, the state
machine disables/contracts `DECEL` (see [firmware.md](firmware.md)).

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
> Our passive, listen-only design depends on the former. If brake/throttle/RPM are
> only available on request, that conflicts with the
> [listen-only golden rule](#1-golden-rule-listen-only) and forces a design rethink
> (e.g. tapping an internal vehicle CAN where the ECUs broadcast among themselves,
> rather than the diagnostic request/response channel). **Determine this before
> anything else.**

### Decode table (to fill in during sniffing)

| Signal | CAN ID | Bits (start/len) | Scale / offset | Notes |
|--------|--------|------------------|----------------|-------|
| `brake_switch` | _TBD_ | _TBD_ | 1-bit | Front and/or rear stop switch — find both if separate. |
| `throttle_pct` | _TBD_ | _TBD_ | → 0–100 % | Ride-by-wire, so a throttle/APS position should be on the bus. |
| `rpm` | _TBD_ | _TBD_ | → rpm | Expect 16-bit. |
| `clutch_pulled` | _TBD_ | _TBD_ | 1-bit | The 400 single may **not** expose a clutch switch — if absent, `DECEL` is disabled/conservative (see [firmware.md](firmware.md)). |
| `wheel_speed` | _TBD_ | _TBD_ | → km/h | **Ride-logger only** (Rig B) — only present in motion. Calibration ground truth + gear inference; optional live input. |
| Bus bit rate | — | — | — | Confirm 250 vs 500 kbit/s. |
| Free-running vs. request/response | — | — | — | **Answer the risk above.** |

Commit anonymized raw captures under `transmitter/software/captures/` (e.g.
`speed400_brake_sweep.log`) so the decode can be re-derived and the Scrambler 400 X /
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
