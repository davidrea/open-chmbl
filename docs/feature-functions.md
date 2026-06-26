# Feature-function lists

The capability decomposition for each device, in lieu of formal shall-statement
requirements. Each **feature** (a capability area) has an ID and a set of
**functions** (what the device actually does). Design documents
([`docs/design/`](design/)) trace back to these IDs, and the developer
[CLI](cli.md) is built to **fake or view** the I/O behind each one so every feature
can be exercised in isolation.

ID scheme: `TX-<area>-<n>` for the transmitter (bike-side), `BL-<area>-<n>` for the
brake_light (helmet-side).

---

## Transmitter (bike-side)

### TX-PWR — Power & lifecycle
- TX-PWR-1 — Derive regulated 3.3 V from the bike's 12 V diagnostic-port supply.
- TX-PWR-2 — Survive automotive transients (reverse polarity, load dump) without damage.
- TX-PWR-3 — Enter low-power sleep when the bike is off / the bus is idle.
- TX-PWR-4 — Wake on CAN bus activity (or ignition sense).
- TX-PWR-5 — Hold parked draw below the parasitic budget (< 1 mA target).

### TX-CAN — CAN interface
- TX-CAN-1 — Connect to the bus **listen-only / silent** (never ACK or transmit).
- TX-CAN-2 — Operate at the bus bit rate (configured or auto-detected).
- TX-CAN-3 — Hardware-filter to the active profile's CAN IDs.
- TX-CAN-4 — Receive frames with timestamps.
- TX-CAN-5 — Detect bus-idle / bus-off / loss-of-traffic.

### TX-DEC — Signal decode
- TX-DEC-1 — Apply the active [bike profile](can-profiles.md) to raw frames.
- TX-DEC-2 — Decode `wheel_speed` (**required** — the primary braking input) and convert to MPH.
- TX-DEC-3 — Decode `clutch_pulled`.
- TX-DEC-4 — Decode `gear`/`neutral` when present (else mark unavailable).
- TX-DEC-5 — Decode `throttle_pct` (0–100 %) and `rpm` (diagnostics/telemetry).
- TX-DEC-6 — Derive smoothed road **acceleration** (MPH/s) from `wheel_speed`.
- TX-DEC-7 — Flag per-signal validity / staleness (frame not seen within timeout).
- _(No `brake_switch` — confirmed absent from the reference bus.)_

### TX-SM — Braking state machine
- TX-SM-1 — Consume smoothed acceleration, speed, clutch, and gear/neutral.
- TX-SM-2 — Compute `OFF` / `BRAKING` / `STOPPED` per the [state machine](firmware.md#braking-state-machine), specified in an [SMC](https://smc.sourceforge.net/) `.sm` model.
- TX-SM-3 — Hold the light through a standstill; release on move-away, launch (clutch released in gear), or the long-stop timeout.
- TX-SM-4 — Enforce anti-strobe minimum dwell / hysteresis on all transitions.
- TX-SM-5 — Use clutch + gear/neutral to gate the stop-exit logic (neutral-aware).
- TX-SM-6 — Emit the result as `ST_BRAKE` (light on) / `ST_OFF` over the protocol.

### TX-NET — Wireless transmit (ESP-NOW)
- TX-NET-1 — Pair once with a brake_light (store peer MAC + key in NVS).
- TX-NET-2 — Register the peer as an **encrypted** ESP-NOW peer.
- TX-NET-3 — Broadcast a heartbeat at 20–50 Hz with a monotonic sequence number.
- TX-NET-4 — Carry current state, feature flags, and health in each message.
- TX-NET-5 — Optionally receive low-rate telemetry (battery/RSSI) from the brake_light.

### TX-DIAG — Diagnostics & health
- TX-DIAG-1 — Track and expose CAN health (ok / idle / bus-off).
- TX-DIAG-2 — Watchdog reset on hang; never latch a stale asserted state.
- TX-DIAG-3 — Drive a status indicator (link / CAN / pairing states).

### TX-CFG — Configuration & persistence
- TX-CFG-1 — Select the active bike profile.
- TX-CFG-2 — Hold the braking-FSM tunables and feature flags (TX rate, `has_gear_signal`, etc.).
- TX-CFG-3 — Persist config + peer/keys across power cycles (NVS).

### TX-CLI — Developer CLI
- TX-CLI-1 — Fake each decoded input signal (wheel/clutch/gear/throttle/rpm) — see [cli.md](cli.md).
- TX-CLI-2 — View live decoded signals and their validity.
- TX-CLI-3 — View / force the current state-machine output.
- TX-CLI-4 — View CAN and ESP-NOW link statistics.
- TX-CLI-5 — Trigger pairing; get/set config.

---

## Brake_light (helmet-side)

### BL-PWR — Power & battery
- BL-PWR-1 — Run from a 1S LiPo.
- BL-PWR-2 — Charge over USB-C with **load sharing** (operate while charging).
- BL-PWR-3 — Measure state-of-charge (fuel gauge / ADC).
- BL-PWR-4 — Warn on low battery and cut off at the protection threshold.
- BL-PWR-5 — Power on/off via the user button.

### BL-NET — Wireless receive (ESP-NOW)
- BL-NET-1 — Pair once with a transmitter (store peer MAC + key in NVS).
- BL-NET-2 — Register the peer as an **encrypted** ESP-NOW peer.
- BL-NET-3 — Receive heartbeats; validate sequence and **drop stale/old** packets.
- BL-NET-4 — Timestamp the last accepted packet (feeds the failsafe).
- BL-NET-5 — Optionally send low-rate telemetry to the transmitter.

### BL-RND — State rendering
- BL-RND-1 — Map `OFF` / `DECEL` / `BRAKE` to LED patterns.
- BL-RND-2 — Render **steady** patterns only; enforce an anti-strobe floor.
- BL-RND-3 — Optional dim running light in `OFF`.

### BL-BRT — Auto brightness
- BL-BRT-1 — Sense ambient light.
- BL-BRT-2 — Scale brightness for day vs. night (no blinding at night, visible by day).
- BL-BRT-3 — Apply a user-selectable brightness cap.
- BL-BRT-4 — Smooth brightness changes (no flicker on light transients).

### BL-FS — Link health & failsafe
- BL-FS-1 — Declare link-loss when no valid packet arrives within the timeout (≤ 300 ms).
- BL-FS-2 — Show a **distinct** link-lost indication (running light + slow fault blink).
- BL-FS-3 — Never go silently dark and never latch a fake `BRAKE` on link-loss.
- BL-FS-4 — Show a pre-first-packet "waiting" indication at boot.

### BL-LED — LED driver
- BL-LED-1 — Drive the LED bar at commanded brightness (PWM/constant-current).
- BL-LED-2 — Respect thermal / current limits.

### BL-IND — Status indicator LED
A **small dedicated indicator**, separate from the main brake-light array, for
discrete status and fault reporting. Maps cleanly onto a single addressable RGB LED
(WS2812-class) — see [`hardware.md §2`](hardware.md#2-brake_light-helmet-side).
- BL-IND-1 — Drive a status-indicator LED that is **independent of the main LED bar**
  (a fault is legible even when the bar is off, dim, or itself the problem).
- BL-IND-2 — Convey discrete states by **color and/or blink code** (e.g. pairing,
  link health, charging, low battery, fault classes).
- BL-IND-3 — Report **error / fault codes** as distinguishable blink patterns (or
  colors) so a specific fault can be read off without a console.
- BL-IND-4 — Keep the indicator **steady or slow** (anti-strobe); it must never be
  mistaken for, or interfere with, the braking signal on the main bar.
- BL-IND-5 — Optionally dim/disable the indicator at night so it doesn't distract the
  rider, without losing fault legibility on demand.

### BL-UI — User interface
- BL-UI-1 — Button: power, enter pairing, cycle brightness cap.
- BL-UI-2 — Surface link / pairing / battery / fault state to the rider via the
  [status-indicator LED](#bl-ind--status-indicator-led) (BL-IND).

### BL-CFG — Configuration & persistence
- BL-CFG-1 — Persist peer/keys, brightness cap, and running-light option (NVS).

### BL-CLI — Developer CLI
- BL-CLI-1 — Fake the incoming braking state (in lieu of a live link) — see [cli.md](cli.md).
- BL-CLI-2 — Fake the ambient-light reading.
- BL-CLI-3 — Fake battery state-of-charge.
- BL-CLI-4 — View render output (state + commanded brightness + pattern).
- BL-CLI-5 — View link / battery / failsafe status; trigger pairing; get/set config.
- BL-CLI-6 — Drive/preview the status-indicator LED (force a code/color) and view its current state.

---

## Traceability

Every design document lists the feature-function IDs it realizes. The intent is that
each ID is satisfied by exactly one design element and exercisable through the CLI, so
"is this feature done?" has a concrete, testable answer. See
[`docs/design/README.md`](design/README.md).
