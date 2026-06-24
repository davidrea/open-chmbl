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
    const char  *name;          // e.g. "Yamaha MT-09 2021 (Euro5)"
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

## 5. Reference target

> **TODO — fill in once the first contributor's bike is on the bench.**

| Field | Value |
|-------|-------|
| Bike (make/model/year) | _TBD — whatever the first builder owns_ |
| Diagnostic connector | _TBD (pinout verified? Y/N)_ |
| CAN bit rate | _TBD_ |
| `brake_switch` (ID / bits / scale) | _TBD_ |
| `throttle_pct` | _TBD_ |
| `rpm` | _TBD_ |
| `clutch_pulled` | _TBD or "not exposed"_ |
| Raw capture log | _link to `transmitter/software/captures/…`_ |

Recommended first target: a Euro 5 (post-2020) bike the contributor owns, that
exposes CAN on the diagnostic port and has an active reverse-engineering community.
Pick by **physical access**, not popularity — you need to spin the wheel and squeeze
the brake repeatedly.

---

## 6. Generalizing later

Once one bike works end-to-end, supporting more is "just" capturing each bike and
adding a `bike_profile_t`. A future runtime profile selector (button/app) lets one
transmitter serve multiple bikes. Out of scope until the reference bike is proven.
