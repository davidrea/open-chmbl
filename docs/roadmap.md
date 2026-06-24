# Roadmap & open questions

Phased plan from "empty repo" to "works on a real bike." Each phase has a clear exit
criterion so we know when to move on.

---

## Phase 0 — Architecture (this pass)

- ✅ System architecture, docs, repo skeleton.
- **Exit:** agreed design + directory structure (`transmitter/` and `brake_light/`,
  each with `hardware/` + `software/`).

## Phase 1 — Bench bring-up (no bike)

- ESP-NOW link between two dev boards: pre-pairing, encrypted peer, heartbeat at
  20–50 Hz, link-loss failsafe on the RX.
- RX drives a placeholder LED bar from a faked `brake_state_t` stream.
- Host-side unit tests for the **state machine** and **profile decoder** (pure
  functions, no hardware).
- **Exit:** wave a fake state over the air → correct LED behavior + correct link-loss
  indication.

## Phase 2 — CAN capture on the reference bike (Triumph Speed 400)

Two rigs (see [can-profiles.md §3](can-profiles.md#3-sniffing-methodology)):
**Rig A** = PCAN-USB + PCAN-Explorer on a stand; **Rig B** = Raspberry Pi / Pi Zero
ride-logger (SocketCAN, listen-only) for in-motion signals.

- **First (Rig A): determine whether the diagnostic port broadcasts free-running CAN
  or only responds to requests** — this gates the whole listen-only approach (see
  [can-profiles.md §5](can-profiles.md#5-reference-target--triumph-speed-400-tr-series-platform)).
- Confirm the red 6-pin connector pinout + bus bit rate by probing.
- Rig A: reverse-engineer `brake_switch`, `throttle_pct`, `rpm`, and `clutch_pulled`
  (the 400 single may not expose a clutch switch). Build a `.dbc`/`.sym`.
- Rig B: log full rides to capture **wheel speed**; correlate speed ↔ RPM ↔ throttle
  ↔ clutch to derive gears and **true road deceleration** for `DECEL` calibration.
- Export the `bike_profile_t` from the `.dbc`; **validate the same profile on the
  Scrambler 400 X** (shared powertrain). Commit anonymized `.trc` / `candump` logs.
- **Exit:** offline replay recovers all available signals; one profile works on both
  400s; a labelled ride log exists for threshold tuning.

## Phase 3 — End-to-end on the bench

- Transmitter decodes live CAN (bike on a stand) → state machine → ESP-NOW → helmet
  LED.
- Calibrate the state-machine tunables (debounce, RPM-fall threshold, dwell times).
- Verify `BRAKE` latency ≤ 100 ms; verify no strobing; verify `DECEL` gating by
  clutch.
- **Exit:** squeeze the brake → bright steady light within budget; roll off throttle
  → `DECEL` (when enabled); shift → no flicker.

## Phase 4 — Hardware integration

- Real boards: transmitter (protected 12 V power, listen-only CAN, sleep) and
  brake_light (LiPo, USB-C charge, auto-dim, breakaway mount, IP65 enclosure).
- Parasitic-draw and runtime measurements vs. targets.
- **Exit:** a wearable unit + a plug-in unit that run a full ride on the bench/stand.

## Phase 5 — Field, polish, generalize

- Controlled real-world testing (where legal).
- Add more bike profiles; consider runtime profile selection.
- Documentation for builders, BOM finalization, enclosure/mount files.

---

## Open questions / decisions to revisit

| Topic | Question | Current lean |
|-------|----------|--------------|
| Reference bike | Which exact make/model/year? | **Triumph Speed 400** (+ Scrambler 400 X, shared powertrain); Street Triple 765 as a stretch. |
| CAN access mode | Free-running broadcast vs. request/response on the diag port? | **Unknown — Phase 2 gate.** Determines whether listen-only sniffing works at all. |
| Street Triple support | Same profile or separate? | Separate profile (different platform), but same connector/TX hardware. |
| Wheel speed in DECEL | Use bike wheel-speed for live deceleration, or calibration only? | Calibration ground truth first; revisit feeding it live once captured (stays clear of the inertial-sensing patent — it's CAN data, not an IMU). |
| Reverse-engineering tools | Bench + ride logging stack? | **Rig A:** PCAN-USB + PCAN-Explorer (listen-only). **Rig B:** Pi/Pi Zero + SocketCAN `candump`/`python-can`; analysis with `cantools`. |
| LED array | Addressable (WS2812) vs. discrete high-power red + CC driver? | Discrete red — simpler legal-color/no-flash story, more efficient. |
| Shared code | Real `shared/` lib vs. duplicated headers across `transmitter/software` and `brake_light/software`? | Start duplicated/symlinked; promote to a lib (or PlatformIO `lib_deps`) once it stabilizes. |
| Framework | ESP-IDF vs. Arduino-ESP32? | ESP-IDF for TWAI + ESP-NOW + deep-sleep control. |
| `DECEL` feature | Ship the engine-braking courtesy cue at all? | Implement, **off by default** for legal reasons. |
| Charge IC | MCP73871 (load-share) vs. TP4056? | MCP73871 so the light works while charging. |
| Battery monitor | Fuel-gauge IC vs. ADC divider? | TBD on cost/space. |
| Telemetry | RX→TX battery/RSSI reporting? | Optional, diagnostics only; not core. |

---

## Tracking

As code lands, each unit's `hardware/` and `software/` directory gets its own README
documenting parts, pinout, and build/flash instructions. This roadmap is the
single source of truth for "what's next."
