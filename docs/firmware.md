# Firmware

Both units run on ESP32-C3 using ESP-IDF (chosen for TWAI + ESP-NOW + deep sleep
control). The two codebases share a small protocol/profile library.

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
| **CAN RX** | bus-driven | TWAI in **listen-only mode**; filter to the profile's CAN IDs; pull `wheel_speed`, `throttle_pct`, `rpm`, `clutch_pulled`, `gear`/`neutral`. (The reference bus carries **no brake-switch bit**.) |
| **Decode** | per frame | Apply the active [bike profile](can-profiles.md) (ID → bit offset/length/scale) to raw frames. |
| **State machine** | 50 Hz tick | Derive speed/acceleration, run the FSM, apply anti-strobe dwell, emit current `state`. |
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
| `DECEL_ON_MPHPS` | 3.0 | Deceleration that turns the light **on**. |
| `DECEL_ON_DEBOUNCE_MS` | 120 | Decel must exceed `DECEL_ON_MPHPS` this long before `BRAKING` (rejects momentary dips). |
| `STOP_SPEED_MPH` | 1.0 | At/under = "stopped" (enter `STOPPED`). |
| `MOVING_SPEED_MPH` | 3.0 | Must be exceeded to leave `STOPPED` for motion (hysteresis; > `STOP_SPEED_MPH`). |
| `ACCEL_OFF_MPHPS` | 0.5 | Acceleration that turns the light **off** while moving. |
| `ACCEL_OFF_MIN_SPEED_MPH` | 5.0 | Only honor the accel-off rule above this speed. |
| `STEADY_BAND_MPHPS` | 0.5 | \|accel\| under this counts as "steady". |
| `STEADY_TIMEOUT_MS` | 2000 | Steady-after-decel hold before turning off. |
| `STOP_TIMEOUT_MS` | 60000 | Max on-time while stopped. |
| `STATE_MIN_DWELL_MS` | 150 | Global anti-strobe floor on all transitions. |

All tunables are config values, so the behaviour can be retuned without touching the
state machine. **Anti-strobe:** the tick handler will not dispatch a state-changing
`Poll` until `STATE_MIN_DWELL_MS` has elapsed since the last transition.

> **Specified with SMC.** This state machine is defined in an [SMC (State Machine
> Compiler)](https://smc.sourceforge.net/) `.sm` model — the single source of truth for
> the states, the `Poll` event, and the guards — and the generated C is built by a
> **CMake pre-build step** (see [§4](#4-build--toolchain)). The numeric work (smoothed
> speed/acceleration, the steady/stop/dwell timers) lives in a pure, host-testable
> context that backs the guard predicates.

### Config

A compile-time (later: runtime) config struct selects the active bike profile and the
state-machine tunables:

```c
typedef struct {
    bike_profile_t profile;          // CAN IDs + bit layouts for the reference bike
    bool   has_gear_signal;          // false → neutral-aware exits degrade to timeout only
    uint8_t tx_rate_hz;              // 20..50
    // Braking FSM tunables (see table above)
    float    decel_on_mphps;         // 3.0
    uint16_t decel_on_debounce_ms;   // 120  (reject momentary decel dips)
    float    stop_speed_mph;         // 1.0  (enter STOPPED)
    float    moving_speed_mph;       // 3.0  (exit STOPPED; hysteresis)
    float    accel_off_mphps;        // 0.5
    float    accel_off_min_speed_mph;// 5.0
    float    steady_band_mphps;      // 0.5
    uint16_t steady_timeout_ms;      // 2000
    uint16_t stop_timeout_ms;        // 60000
    uint16_t state_min_dwell_ms;     // 150
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
- CI later: build both firmwares; unit-test the state machine and the profile
  decoder on host (they're pure functions — keep them platform-independent so they
  can be tested without hardware).

### State Machine Compiler (SMC) pre-build step

The transmitter's [braking state machine](#braking-state-machine) is **specified in an
SMC `.sm` model** (`transmitter/software/state_machine/brake_fsm.sm`), and the
generated C (`brake_fsm_sm.[ch]`) is produced **at build time** by the
[SMC State Machine Compiler](https://smc.sourceforge.net/) — a Java tool (`Smc.jar`)
checked in under `tools/smc/`. The generated files are **never committed**; the `.sm`
model is the single source of truth.

Wire it in as a **CMake pre-build step** so editing the model regenerates the code and
the firmware target rebuilds:

```cmake
find_program(JAVA_EXECUTABLE java REQUIRED)
set(SMC_JAR  "${CMAKE_SOURCE_DIR}/tools/smc/Smc.jar")
set(FSM_SM   "${CMAKE_CURRENT_SOURCE_DIR}/state_machine/brake_fsm.sm")
set(FSM_GEN  "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(
    OUTPUT  ${FSM_GEN}/brake_fsm_sm.c ${FSM_GEN}/brake_fsm_sm.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FSM_GEN}
    COMMAND ${JAVA_EXECUTABLE} -jar ${SMC_JAR} -c -d ${FSM_GEN} ${FSM_SM}
    # optional: also emit a Graphviz diagram to keep the docs in sync
    COMMAND ${JAVA_EXECUTABLE} -jar ${SMC_JAR} -graph -glevel 1 -d ${FSM_GEN} ${FSM_SM}
    DEPENDS ${FSM_SM} ${SMC_JAR}
    COMMENT "SMC: compiling brake_fsm.sm → brake_fsm_sm.[ch]"
    VERBATIM)

add_custom_target(brake_fsm_gen DEPENDS ${FSM_GEN}/brake_fsm_sm.c)
# then: list ${FSM_GEN}/brake_fsm_sm.c in the component sources, add ${FSM_GEN} to the
# include dirs, and add_dependencies(<component-lib> brake_fsm_gen).
```

Under ESP-IDF this lives in the relevant component's `CMakeLists.txt` (register the
generated source, add the include dir, and `add_dependencies(${COMPONENT_LIB}
brake_fsm_gen)`). The **host unit-test build** uses the same pre-build step, so the
state machine is exercised off-target with synthetic signal/time sequences. Builders
need a JRE on the build host; document it in the transmitter software README.
