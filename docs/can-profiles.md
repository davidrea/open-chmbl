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
- Note the bus **bit rate** (commonly 500 kbit/s, sometimes 250 kbit/s).

---

## 3. Sniffing methodology

To build a profile:

1. **Capture** raw frames with the wheel off the ground / on a stand, logging all
   IDs. Tools: a USB-CAN adapter + SavvyCAN, or the ESP32 itself in promiscuous
   listen-only mode dumping to serial.
2. **Correlate** one signal at a time:
   - Squeeze the **brake lever / pedal** → watch which ID + bit toggles → `brake_switch`.
   - Sweep the **throttle** → find the byte that ramps 0→100 % → `throttle_pct`.
   - Rev the engine → find the 16-bit, scaled `rpm` field.
   - Pull the **clutch** → find the `clutch_pulled` bit (if present at all).
3. **Record** each as `(can_id, byte/bit offset, length, scale, offset)`.
4. **Validate** by replaying the capture through the decoder offline and checking the
   recovered signals match what you did.

Keep raw capture logs in the repo (anonymized) so others can re-derive or extend.

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
