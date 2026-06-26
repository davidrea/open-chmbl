# Roadmap & open questions

Phased plan from "empty repo" to "works on a real bike." Each phase has a clear exit
criterion so we know when to move on.

---

## Phase 0 — Architecture (this pass)

- ✅ System architecture, docs, repo skeleton.
- ✅ Per-device [feature-function lists](feature-functions.md), the
  [design-element framework](design/README.md), and the [developer CLI](cli.md) spec.
- **Exit:** agreed design + directory structure (`transmitter/` and `brake_light/`,
  each with `hardware/` + `software/`); FFLs and the isolated build order in place.

## Phase 1 — Bench bring-up (no bike)

Built as isolated [design elements](design/README.md), one at a time, each exercised
through the [developer CLI](cli.md):

- **DE-00** CLI/shell framework on both devices (prerequisite — makes the rest
  testable in isolation).
- **DE-01** ESP-NOW link: pre-pairing, encrypted peer, heartbeat at 20–50 Hz, seq.
- **DE-02** auto-brightness; **DE-03** link-loss failsafe; **DE-04** LED render.
- Host-side unit tests for the pure cores (state machine, profile decoder).
- **Exit:** each element passes its isolation acceptance (see its design doc); waving a
  faked state over the air drives correct render + link-loss behaviour.

## Phase 2 — CAN capture on the reference bike (Triumph Speed 400)

Two rigs (see [can-profiles.md §3](can-profiles.md#3-sniffing-methodology)):
**Rig A** = PCAN-USB + PCAN-Explorer on a stand; **Rig B** = Raspberry Pi / Pi Zero
ride-logger (SocketCAN, listen-only) for in-motion signals.

- **First (Rig A): determine whether the diagnostic port broadcasts free-running CAN
  or only responds to requests** — this gates the whole listen-only approach (see
  [can-profiles.md §5](can-profiles.md#5-reference-target--triumph-speed-400-tr-series-platform)).
- Confirm the red 6-pin connector pinout + bus bit rate by probing.
- Rig A: reverse-engineer `wheel_speed`, `clutch_pulled`, `gear`/`neutral`, `throttle_pct`
  and `rpm`. Build a `.dbc`/`.sym`. **There is no `brake_switch` on the reference bus**
  (confirmed by capture) — braking is inferred from wheel-speed deceleration.
- Rig B: log full rides to capture **wheel speed** in motion; use `d(wheel_speed)/dt` to
  tune the [braking-FSM](can-profiles.md) acceleration thresholds and smoothing window,
  and confirm `clutch`/`gear` behaviour at stops and launches.
- Export the `bike_profile_t` from the `.dbc`; **validate the same profile on the
  Scrambler 400 X** (shared powertrain). Commit anonymized `.trc` / `candump` logs.
- **Exit:** offline replay recovers all available signals + a sane derived acceleration;
  one profile works on both 400s; a labelled ride log exists for threshold tuning.

## Phase 3 — End-to-end on the bench

- Transmitter decodes live CAN (bike on a stand / ride log replay) → state machine →
  ESP-NOW → helmet LED.
- Calibrate the FSM tunables (decel-on / accel-off thresholds, steady/stop timeouts,
  acceleration smoothing window, anti-strobe dwell).
- Verify light-on latency ≤ 100 ms; verify no strobing; verify the stop-hold and
  neutral-aware release behave on real ride data.
- **Exit:** a hard decel → bright steady light within budget; coast to a stop → light
  holds through the standstill; accelerate away / launch → off; no flicker.

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
| Brake signal source | Brake-switch bit on the bus, or inferred? | **Resolved — no brake bit on the reference bus.** Braking is inferred from wheel-speed deceleration (see [firmware.md](firmware.md#braking-state-machine) / [DE-09](design/de-09-brake-decel-logic.md)). |
| CAN access mode | Free-running broadcast vs. request/response on the diag port? | **Unknown — Phase 2 gate.** Determines whether listen-only sniffing works at all. |
| State-machine spec | How is the braking FSM authored? | **SMC `.sm` model**, compiled to C by a CMake pre-build step (requires a JRE on the build host). |
| Street Triple support | Same profile or separate? | Separate profile (different platform), but same connector/TX hardware. |
| Wheel speed as primary input | Use bike wheel-speed for live deceleration? | **Yes — it is now the primary braking input** (no brake bit exists). Stays clear of the inertial-sensing patent — it's CAN data, not an IMU. |
| Reverse-engineering tools | Bench + ride logging stack? | **Rig A:** PCAN-USB + PCAN-Explorer (listen-only). **Rig B:** Pi/Pi Zero + SocketCAN `candump`/`python-can`; analysis with `cantools`. |
| LED array | Addressable (WS2812) vs. discrete high-power red + CC driver? | Discrete red — simpler legal-color/no-flash story, more efficient. |
| Status indicator | Separate status/fault LED (color + blink codes), independent of the bar? | **Yes** — add it (BL-IND / [DE-10](design/de-10-status-indicator.md)); ideally a module's onboard WS2812. |
| ESP32-C3 module | Integrated board with onboard WS2812 **and** LiPo charger, or bare module + discretes? | Nice-to-have; pick an integrated one if clean (LOLIN C3 Pico / confirmed FireBeetle 2 C3), else XIAO C3 + external WS2812, or a bare module + both discretes. See [hardware §2.1](hardware.md#21-integrated-module-candidates-ws2812--lipo-charger). |
| Mount | Adhesive/strap breakaway baseline vs. magnetic shear-release? | Baseline for now; [magnetic mount](design/explorations/mounting-magnetic.md) (helmet-interchangeable VHB steel targets and/or garment/backpack shoulder mount) is a future-state exploration. |
| Shared code | Real `shared/` lib vs. duplicated headers across `transmitter/software` and `brake_light/software`? | Start duplicated/symlinked; promote to a lib (or PlatformIO `lib_deps`) once it stabilizes. |
| Framework | ESP-IDF vs. Arduino-ESP32? | ESP-IDF for TWAI + ESP-NOW + deep-sleep control. |
| Soft `DECEL` tier | Keep a separate softer "coasting" cue below the brake threshold? | Not for now — the light is binary on/off. `ST_DECEL` is **reserved** in the protocol if a second tier is wanted later. |
| Stop-and-go flicker | How to handle repeated creep-and-stop in traffic? | Open — smoothing + anti-strobe dwell bound it; may add a `STOPPED`→`OFF` hold. See [DE-09 §8](design/de-09-brake-decel-logic.md#8-open-items). |
| Charge IC | MCP73871 (load-share) vs. TP4056? | MCP73871 so the light works while charging. |
| Battery monitor | Fuel-gauge IC vs. ADC divider? | TBD on cost/space. |
| Telemetry | RX→TX battery/RSSI reporting? | Optional, diagnostics only; not core. |

---

## Tracking

As code lands, each unit's `hardware/` and `software/` directory gets its own README
documenting parts, pinout, and build/flash instructions. This roadmap is the
single source of truth for "what's next."
