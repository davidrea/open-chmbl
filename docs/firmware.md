# Firmware

Both units run on ESP32-C3 (Arduino-ESP32 or ESP-IDF; ESP-IDF preferred for TWAI +
ESP-NOW + deep sleep control). The two codebases share a small protocol/profile
library.

```
transmitter/software/   ← bike-side firmware
brake_light/software/   ← helmet-side firmware
(shared protocol + profile structs are duplicated or symlinked between them;
 see roadmap for the mono-repo decision)
```

---

## 1. Transmitter firmware (bike-side)

Responsibilities: read CAN, decode the bike profile, run the braking state machine,
broadcast state over ESP-NOW, manage sleep.

### Tasks / loop

| Task | Rate | Job |
|------|------|-----|
| **CAN RX** | bus-driven | TWAI in **listen-only mode**; filter to the profile's CAN IDs; pull `brake_switch`, `throttle_pct`, `rpm`, `clutch_pulled`. |
| **Decode** | per frame | Apply the active [bike profile](can-profiles.md) (ID → bit offset/length/scale) to raw frames. |
| **State machine** | 50 Hz tick | Debounce inputs, apply hysteresis, emit current `state`. |
| **ESP-NOW TX** | 20–50 Hz | Send heartbeat with current state + sequence number. |
| **Power mgmt** | background | Detect bus-idle / ignition-off → deep sleep; wake on activity. |
| **Watchdog** | always | Reset on hang; never get stuck asserting a stale state. |

### Braking state machine

States: `OFF`, `BRAKE`, `DECEL`, plus a `SHIFT` **suppression** condition.

Priority (highest first): `BRAKE` > `SHIFT`-suppress > `DECEL` > `OFF`.

```
                 brake_switch?
                    │ yes
        ┌───────────▼───────────┐
        │        BRAKE           │  (always wins)
        └───────────┬───────────┘
                    │ no brake_switch
            clutch_pulled?
              │ yes        │ no
   ┌──────────▼───┐   throttle ≈ 0  AND  rpm falling > Δ ?
   │  hold last   │        │ yes              │ no
   │  non-brake   │   ┌────▼────┐        ┌────▼────┐
   │  (SHIFT)     │   │  DECEL  │        │   OFF   │
   └──────────────┘   └─────────┘        └─────────┘
```

**Tunables (start here, calibrate on the bench):**

| Parameter | Start value | Purpose |
|-----------|-------------|---------|
| `BRAKE_DEBOUNCE_MS` | 20 ms | Reject switch chatter. |
| `THROTTLE_CLOSED_PCT` | ≤ 3 % | "Throttle closed" threshold for `DECEL`. |
| `RPM_FALL_THRESH` | ~300 rpm over 200 ms | Engine-braking detection. Gear/load dependent — coarse by design. |
| `DECEL_MIN_DWELL_MS` | 300 ms | Minimum on-time; prevents flicker. |
| `SHIFT_HOLD_MS` | 400 ms | How long to freeze state after clutch-in. |
| `STATE_MIN_DWELL_MS` | 150 ms | Global anti-strobe floor on all transitions. |

`DECEL` is **off by default** via config (legal reasons — see safety doc); `BRAKE`
is always enabled.

### Config

A compile-time (later: runtime) config struct selects the active bike profile and
feature flags:

```c
typedef struct {
    bike_profile_t profile;     // CAN IDs + bit layouts for the reference bike
    bool   enable_decel;        // default false (legal)
    bool   has_clutch_signal;   // false → DECEL disabled / conservative
    uint8_t tx_rate_hz;         // 20..50
} tx_config_t;
```

---

## 2. Brake_light firmware (helmet-side)

Responsibilities: receive state, render LEDs, dim for ambient light, monitor
battery, manage pairing and link-loss.

### Tasks / loop

| Task | Rate | Job |
|------|------|-----|
| **ESP-NOW RX** | event | Validate seq/encryption; update `last_state` + `last_rx_time`. |
| **State render** | 60 Hz | Map state → LED pattern/brightness via the pattern engine. |
| **Ambient dim** | 5 Hz | Read light sensor → scale brightness (night vs. day). |
| **Link watchdog** | 10 Hz | If `now - last_rx_time > LINK_TIMEOUT_MS` → link-lost indication. |
| **Battery** | 1 Hz | Fuel gauge → low-battery pattern + cutoff. |
| **UI / pairing** | event | Button: power, enter pairing, cycle brightness cap. |

### Pattern engine (suggested mapping)

| State | Pattern |
|-------|---------|
| `OFF` | Off, or a dim steady running light (config). |
| `DECEL` | Medium-brightness steady red. **No flashing** (default). |
| `BRAKE` | Full-brightness steady red. |
| **link-lost** | Steady running light **+ slow fault blink** (distinct from braking). |
| low-battery | Brief periodic amber/dim blink of the status LED, not the main bar. |

All transitions are rate-limited so the main bar can't strobe.
`LINK_TIMEOUT_MS` target: **≤ 300 ms**.

### Failsafe philosophy

- Lost link ⇒ **honest "I don't know" indication**, never a silent dark light and
  never a latched fake `BRAKE`.
- Stale packet (old sequence number) is dropped.
- On boot before first packet: running light + waiting indication.

---

## 3. Shared library

`shared` (see [roadmap](roadmap.md) for whether it's a real shared dir or duplicated):

- `protocol.h` — the ESP-NOW message struct + enums (see [protocol.md](protocol.md)).
- `bike_profile.h` — the per-bike CAN decode profile struct (see
  [can-profiles.md](can-profiles.md)).
- `states.h` — the `brake_state_t` enum shared by both sides.

---

## 4. Build & toolchain

- **ESP-IDF** (recommended) per unit, or PlatformIO with the `espidf` framework.
- Separate build per board: `transmitter/software/` and `brake_light/software/`.
- CI later: build both firmwares; unit-test the state machine and the profile
  decoder on host (they're pure functions — keep them platform-independent so they
  can be tested without hardware).
