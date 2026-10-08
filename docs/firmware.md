# Firmware

Both units run **ESP-IDF** (chosen for TWAI + ESP-NOW + deep sleep control), on
different ESP32 variants: the transmitter and the logger both run on the
[`logger/`](../logger) PCB's **ESP32-S3** (see [`hardware.md §1`](hardware.md#1-transmitter-bike-side)),
while `brake_light` stays on **ESP32-C3**. The transmitter firmware is ported and
targets `esp32s3` only (see [`transmitter/software/README.md`](../transmitter/software/README.md)).
The two codebases share a small protocol/profile library.

```
transmitter/software/   ← bike-side firmware
brake_light/software/   ← rider-side firmware
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
| **CAN RX** | bus-driven | TWAI in **listen-only mode**; filter to the profile's CAN IDs; pull `wheel_speed`, `throttle_pct`, `rpm`, `clutch_pulled`, `gear`/`neutral`. (The reference bus carries **no brake-switch bit**.) |
| **Decode** | per frame | Apply the active [bike profile](can-profiles.md) (ID → bit offset/length/scale) to raw frames. |
| **State machine** | 50 Hz tick | Derive speed/acceleration, run the FSM, apply anti-strobe dwell, emit current `state` and drive the light. |
| **ESP-NOW TX** | 20–50 Hz | Send heartbeat with current state + sequence number. |
| **Power mgmt** | background | Detect bus-idle / ignition-off → deep sleep; wake on activity. |
| **Watchdog** | always | Reset on hang; never get stuck asserting a stale state. |

### Braking state machine

The reference bike (Triumph Speed 400) **does not publish a brake-switch bit on its CAN
bus**, so the light is not driven by a brake signal at all. Instead it is **inferred
from wheel-speed-derived acceleration**, qualified by clutch and gear/neutral context.
The deceleration estimate comes from the bike's own CAN **wheel-speed** signal (not an
on-board accelerometer/gyro), which keeps the design clear of the inertial-detection
patent family (see [ARCHITECTURE §1](../ARCHITECTURE.md#1-why-this-approach)).

States: `OFF`, `BRAKING`, `STOPPED`. Both `BRAKING` and `STOPPED` render as `ST_BRAKE`
(light on); `OFF` renders as `ST_OFF`. The light is effectively **binary on/off** on the
wire; the two on-states exist because their *off* conditions differ (moving vs. stopped).
(`ST_DECEL` is left **reserved** in the [protocol](protocol.md) for a possible future
soft-cue tier.) Full rationale and the transition table: [DE-09](design/de-09-brake-decel-logic.md).

```
                ┌───────────────────────── OFF (light off) ─────────────────────────┐
                │                                                                    │
       decel > DECEL_ON │                                  speed < STOP_SPEED        │
                ▼                                                          ▼
        ┌───────────────┐   speed < STOP_SPEED          ┌───────────────────────────┐
        │   BRAKING      │ ───────────────────────────▶ │          STOPPED           │
        │  (light on)    │                               │        (light on)          │
        └───────┬───────┘                               └─────────────┬──────────────┘
                │ accel > ACCEL_OFF & speed > MIN_SPEED                │ speed > MOVING_SPEED
                │ OR |accel| < STEADY_BAND for STEADY_TIMEOUT          │ OR (clutch released in gear & rolling)
                ▼                                                      │ OR stopped > STOP_TIMEOUT
               OFF ◀──────────────────────────────────────────────────┘
```

**Transitions (Poll fired at the 50 Hz tick; first matching guard wins):**

| From | Guard | To |
|------|-------|----|
| `OFF` | deceleration > `DECEL_ON_MPHPS` held for `DECEL_ON_DEBOUNCE_MS` | `BRAKING` |
| `OFF` | `speed` < `STOP_SPEED_MPH` | `STOPPED` |
| `BRAKING` | `speed` < `STOP_SPEED_MPH` | `STOPPED` |
| `BRAKING` | acceleration > `ACCEL_OFF_MPHPS` **and** `speed` > `ACCEL_OFF_MIN_SPEED_MPH` | `OFF` |
| `BRAKING` | \|accel\| < `STEADY_BAND_MPHPS` held for `STEADY_TIMEOUT_MS` | `OFF` |
| `STOPPED` | `speed` > `MOVING_SPEED_MPH` | `OFF` |
| `STOPPED` | clutch released **and** in gear (not neutral) **and** `speed` > `STOP_SPEED_MPH` | `OFF` |
| `STOPPED` | stopped for > `STOP_TIMEOUT_MS` | `OFF` |

`accel` is the slope of a **smoothed** `speed` over a short window (≈100–200 ms), not a
raw sample diff — CAN speed is quantized and noisy and the FSM hinges on a clean
derivative. The "clutch released in gear" guard reads the **gear/neutral** signal so a
neutral stop holds the light (no spurious launch detection); a long stop is bounded by
`STOP_TIMEOUT_MS`.

**Low-speed hysteresis.** The `STOPPED` state has a **wider exit than entry**: you enter
at `speed` < `STOP_SPEED_MPH` (1.0) but only leave for motion once `speed` >
`MOVING_SPEED_MPH` (3.0). The launch guard ("clutch released in gear") likewise requires
the bike to actually be rolling (`speed` > `STOP_SPEED_MPH`) so it cannot ping-pong
against the stop-entry rule while sitting still in gear. Without this gap, low-speed
creep in stop-and-go traffic straddles a single threshold and the light strobes — on the
[DE-07 40 mph ride log](can-profiles.md#decode-table) it cut FSM transitions from **162
to 48** (and sub-0.5 s light blips from 65 to 8), turning the two creep zones into solid
holds. The anti-strobe dwell alone can't fix this: the underlying guards genuinely
oscillate, so the fix is hysteresis, not just rate-limiting.

**Decel-on debounce (momentary-dip rejection).** The `OFF → BRAKING` trigger requires
`deceleration > DECEL_ON_MPHPS` to hold **continuously** for `DECEL_ON_DEBOUNCE_MS`
(120 ms) before the light comes on. Momentary throttle-off dips briefly spike the
wheel-speed derivative past the threshold and clear within a tick or two; the debounce
rejects those without attenuating the signal (unlike heavier low-pass smoothing, which
would suppress the spikes only by **delaying genuine hard-braking onset** — the wrong
trade for a brake light). A sustained decel still fires, just ≤ 120 ms later. On the
[DE-07 ride log](can-profiles.md#decode-table) this removed the remaining sub-0.5 s
`BRAKING` blips (8 → 3, and total transitions 48 → 30) with on-time essentially
unchanged; larger debounce values started clipping real short brake taps, so 120 ms is
the knee. This is a **debounce, not a low-pass filter** — chosen precisely to keep
braking-onset latency bounded and predictable.

**Tunables (defaults — speeds in MPH, accelerations in MPH/s; calibrate on DE-07 logs):**

| Parameter | Default | Purpose |
|-----------|---------|---------|
| `decel_on_mphps` | 2.0 | Deceleration that turns the light **on**. |
| `decel_on_debounce_ms` | 120 | Decel must exceed `decel_on_mphps` this long before `BRAKING` (rejects momentary dips). |
| `stop_speed_mph` | 1.0 | At/under = "stopped" (enter `STOPPED`). |
| `moving_speed_mph` | 3.0 | Must be exceeded to leave `STOPPED` for motion (hysteresis; > `stop_speed_mph`). |
| `accel_off_mphps` | 0.5 | Acceleration that turns the light **off** while moving. |
| `accel_off_min_speed_mph` | 5.0 | Only honor the accel-off rule above this speed. |
| `steady_band_mphps` | 0.75 | \|accel\| under this counts as "steady". |
| `steady_timeout_ms` | 1500 | Steady-after-decel hold before turning off. |
| `stop_timeout_ms` | 60000 | Max on-time while stopped. |
| `state_min_dwell_ms` | 250 | Global anti-strobe floor on all transitions. |
| `speed_smooth_ms` | 80 | Wheel-speed low-pass ahead of the slope estimate. |

These are the values calibrated in the bench viewer and shipped in
`brake_fsm.h`; they are runtime values, not build-time ones, so the behaviour can be
swept with `state tune` without reflashing. **Anti-strobe:** no guard is evaluated until
`state_min_dwell_ms` has elapsed since the last transition.

> **Plain C, tested against a reference.** The machine is ~40 lines in
> `transmitter/software/main/brake_fsm.c` (pure, no ESP-IDF), ticked by `brake_ctl.c`.
> An earlier plan to specify it in an SMC model was dropped — see
> [DE-09](design/de-09-brake-decel-logic.md) for why. What keeps the definition honest
> is [`tools/fsm_check.py`](../tools/fsm_check.py), which asserts the shipping C and the
> calibration bench agree tick for tick over a real ride capture, in CI.

### Config

A compile-time (later: runtime) config struct selects the active bike profile and the
state-machine tunables:

```c
typedef struct {
    bike_profile_t profile;          // CAN IDs + bit layouts for the reference bike
    bool   has_gear_signal;          // false → neutral-aware exits degrade to timeout only
    uint8_t tx_rate_hz;              // 20..50
    // Braking FSM tunables (see table above)
    float    decel_on_mphps;         // 2.0
    uint16_t decel_on_debounce_ms;   // 120  (reject momentary decel dips)
    float    stop_speed_mph;         // 1.0  (enter STOPPED)
    float    moving_speed_mph;       // 3.0  (exit STOPPED; hysteresis)
    float    accel_off_mphps;        // 0.5
    float    accel_off_min_speed_mph;// 5.0
    float    steady_band_mphps;      // 0.75
    uint16_t steady_timeout_ms;      // 1500
    uint32_t stop_timeout_ms;        // 60000
    uint16_t state_min_dwell_ms;     // 250
    uint16_t speed_smooth_ms;        // 80
} tx_config_t;
```

---

## 2. Brake_light firmware (rider-side)

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
| **Status indicator** | 10–20 Hz | Aggregate device status → drive the separate indicator LED (color/blink code); independent of the main bar. |

### Pattern engine (suggested mapping)

| State | Pattern |
|-------|---------|
| `OFF` | Off, or a dim steady running light (config). |
| `DECEL` | **Reserved** (not emitted by the current TX FSM). If ever used: medium-brightness steady red, **no flashing**. |
| `BRAKE` | Full-brightness steady red. Emitted whenever the TX is in `BRAKING` or `STOPPED`. |
| **link-lost** | Steady running light **+ slow fault blink** (distinct from braking). |
| low-battery | Brief periodic amber/dim blink of the **status-indicator LED**, not the main bar. |

All transitions are rate-limited so the main bar can't strobe.
`LINK_TIMEOUT_MS` target: **≤ 300 ms**.

### Status-indicator LED (separate from the bar)

A small **addressable RGB LED**, independent of the main array, carries discrete
status and fault reporting by **color and/or blink code** — readable even when the bar
is off, dimmed, or itself faulted. It can be the chosen module's **onboard WS2812** (see
[`hardware.md §2.1`](hardware.md#21-integrated-module-candidates-ws2812--lipo-charger)).
Design element [DE-10](design/de-10-status-indicator.md); capabilities BL-IND-*.

A starting code table (resolve by priority, highest first):

| Status | Suggested indicator |
|--------|---------------------|
| fault / error | Red — **blink code** encodes the fault class (count the blinks). |
| pairing | Blue, slow pulse. |
| link-lost | Amber, slow blink (mirrors the bar's fault blink). |
| charging | Steady amber; **green** when full. |
| low battery | Red, brief periodic blink. |
| OK / idle | Off, or a dim "armed" tick (night-dimmed). |

Lean on **blink patterns**, not color alone, for the safety-relevant distinctions
(color-blind legibility). The indicator is **anti-strobe** and must never be confused
with the braking signal.

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
- CI builds every firmware with the real toolchain and runs the host tests below;
  keep the pure cores (state machine, profile decoder) platform-independent so they
  stay testable without hardware.

### Host tests for the pure cores

Both of the transmitter's decision-making cores are written without ESP-IDF headers so
they compile on the host: `can_decode.c` (DE-08) and `brake_fsm.c` (DE-09).
`transmitter/software/test_host/` builds two harnesses against **the same sources the
firmware compiles** — not copies — and two scripts check them against independent
references:

| Harness | Script | Asserts |
|---------|--------|---------|
| `trc_replay` | [`tools/golden_check.py`](../tools/golden_check.py) | every decoded signal matches **python-cantools** over a ride capture |
| `fsm_replay` | [`tools/fsm_check.py`](../tools/fsm_check.py) | the FSM's state matches the **calibration bench** ([`tools/trc_viz.py`](../tools/trc_viz.py)) at every 50 Hz tick, and the derived acceleration agrees to 1e-3 mph/s |

```bash
cmake -S transmitter/software/test_host -B transmitter/software/test_host/build
cmake --build transmitter/software/test_host/build
pip install -r tools/requirements.txt
python3 tools/golden_check.py && python3 tools/fsm_check.py
```

Both run in CI on every push that touches the firmware, the profiles or the tools. The
FSM check is what makes the duplication between the firmware and the bench viewer safe:
they are allowed to be two implementations precisely because divergence fails the build.

> An earlier revision of this document specified the braking FSM in an
> [SMC](https://smc.sourceforge.net/) `.sm` model generated by a CMake pre-build step,
> which would have required a JRE on every build host and in CI. That was dropped when
> DE-09 was implemented; the rationale is in
> [DE-09 §5](design/de-09-brake-decel-logic.md#5-firmware-moduletask-decomposition).
