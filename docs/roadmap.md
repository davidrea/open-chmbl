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
- **DE-03** link-loss failsafe; **DE-04** LED render — now a **binary on/off bar**, with
  **DE-02 auto-brightness deferred** (see the open-questions table below and the
  [DE-04 decision](design/de-04-led-render.md)). A minimal slice of **DE-10** came
  forward with them, since link-loss indication had to move off the brake bar.
- Host-side unit tests for the pure cores (render map + anti-strobe, state machine,
  profile decoder).
- **Exit:** each element passes its isolation acceptance (see its design doc); waving a
  faked state over the air drives correct render + link-loss behaviour. *(Auto-brightness
  is excluded from this exit criterion while it is deferred.)*

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
- **Exit:** a hard decel → steady light on within budget; coast to a stop → light
  holds through the standstill; accelerate away / launch → off; no flicker.

## Phase 4 — Hardware integration

- Real boards: transmitter (protected 12 V power, listen-only CAN, sleep) and
  brake_light (Li-ion, USB-C charge, breakaway mount, IP65 enclosure; auto-dim deferred).
- **Started early, out of order:** the brake_light has a
  [first-pass PCB](../brake_light/hardware/README.md) (ESP32-C3-WROOM-02, two AP3019A LED
  drivers, BQ21040 charger, TPS62162 buck, ISL29035 ambient sensor, discrete status LEDs).
  Not built yet, and four MCU-side nets — both LED-driver enables and both status-LED
  feeds — are **still unwired**, so the firmware keeps those pins as Kconfig symbols with
  provisional defaults. Reconciling the as-drawn LED current (≈ 357 mA × 2 strings of 10)
  with the [brightness benchmark](led-brightness-benchmark.md) is the other open item,
  and it matters more now that brightness is fixed in hardware.
- Parasitic-draw and runtime measurements vs. targets.
- **Started early, out of order:** the [`logger/`](../logger) got a real
  ESP32-S3 PCB ([`logger/hardware/`](../logger/hardware/README.md)) ahead of
  DE-07/Phase 3, and it's designed to double as the transmitter board (same PCB,
  microSD + button/LED connectors unpopulated) — see
  [`hardware.md §1`](hardware.md#1-transmitter-bike-side). Firmware ports (logger →
  ESP32-S3, transmitter → ESP32-S3) and the transmitter-specific deltas (ignition
  sense/sleep, automotive-grade power protection, enclosure) are still open.
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
| Transmitter MCU/board | ESP32-C3 (as originally sketched), or reuse the logger's board? | **Resolved — reuse the [`logger/`](../logger) PCB** (ESP32-S3-WROOM-1 + TCAN330), with the microSD (J5) and button/LED (J4) connectors unpopulated. The S3 was forced by the logger's need for the SDMMC peripheral (the C3 doesn't have one); the transmitter carries it over for board reuse. `brake_light` has no SD card and stays on the C3. Firmware ports for both boards are still pending — see [`hardware.md §1`](hardware.md#1-transmitter-bike-side). |
| LED array | Addressable (WS2812) vs. discrete red + CC driver? | **Discrete 620–630 nm red**, mid-power 2835/3030 array on a **boost CC driver**, sized to the CHMSL intensity band (~50–80 cd). Photometry/emitter in the [brightness benchmark](led-brightness-benchmark.md); series/boost topology + driver trade study in [DE-04](design/de-04-led-render.md). Addressable RGB too dim per-pixel → status indicator only. The first-pass PCB substituted **`AP3019A` ×2 driving 2 strings of 10** for the LM3410 down-select — confirm against DE-04 §3.3's criteria ([DE-04 §3.5](design/de-04-led-render.md)). |
| **Bar brightness / auto-dimming** | Drive the bar at a commanded brightness (PWM + ambient dimming), or just on/off? | **Deferred to on/off for now.** The bar is a **plain binary GPIO** — high = on, low = off — with the LED current fixed in hardware by the driver's sense resistor and the driver `EN` pin used as a **static enable**, not a PWM dimming input. Get a working light first; `BL-BRT-*` / [DE-02](design/de-02-auto-brightness.md) are **deferred, not dropped**. Decision + what it costs: [DE-04](design/de-04-led-render.md). The `ISL29035` sensor is fitted and unread, so this is a firmware-only change later. **Still a safety item** — a single undimmed setpoint can't serve both day visibility and the night no-blinding rule. |
| Status indicator | Separate status/fault LED (color + blink codes), independent of the bar? | **Yes** — BL-IND / [DE-10](design/de-10-status-indicator.md). The first-pass PCB has **two discrete mono LEDs (D13 RED / D14 GRN)** rather than an addressable RGB, so the code table has to lean on blink patterns over colour. A **minimal link-health slice is implemented**, because link-loss indication had to move off the brake bar (blinking a stop lamp is forbidden) — see [DE-03 §4.1](design/de-03-link-loss-failsafe.md). The full code engine is still to come. |
| ESP32-C3 module | Integrated board with onboard WS2812 **and** LiPo charger, or bare module + discretes? | **Resolved — bare module + discretes.** The first-pass PCB uses a bare `ESP32-C3-WROOM-02` with a chip-down charger, buck, LED drivers and status LEDs. See [hardware §2.1](hardware.md#21-parts-direction-bare-module--chip-down) and [`brake_light/hardware/`](../brake_light/hardware/README.md). |
| Mount | Adhesive/strap breakaway baseline vs. magnetic shear-release? | Baseline for now; [magnetic mount](design/explorations/mounting-magnetic.md) (helmet-interchangeable VHB steel targets and/or garment/backpack shoulder mount) is a future-state exploration. |
| Shared code | Real `shared/` lib vs. duplicated headers across `transmitter/software` and `brake_light/software`? | Start duplicated/symlinked; promote to a lib (or PlatformIO `lib_deps`) once it stabilizes. |
| Framework | Which framework? | **ESP-IDF** for TWAI + ESP-NOW + deep-sleep control. |
| Soft `DECEL` tier | Keep a separate softer "coasting" cue below the brake threshold? | Not for now — the light is binary on/off. `ST_DECEL` is **reserved** in the protocol if a second tier is wanted later. |
| Stop-and-go flicker | How to handle repeated creep-and-stop in traffic? | Open — smoothing + anti-strobe dwell bound it; may add a `STOPPED`→`OFF` hold. See [DE-09 §8](design/de-09-brake-decel-logic.md#8-open-items). |
| Charge IC | MCP73871 (load-share) vs. TP4056? | Was MCP73871 (load-share) so the light works while charging; the **first-pass PCB fitted a `BQ21040DBV`** instead (with an NTC on `TS`). Revisit: BQ21040 is a plain linear charger, so **confirm the load-sharing behaviour** — and note its `~CHG` output only drives a local LED, it is **not** routed to the MCU, so charge state isn't readable in firmware. |
| Battery monitor | Fuel-gauge IC vs. ADC divider? | Still TBD on cost/space — and **neither is on the first-pass PCB**: there is no battery-voltage sense net at all, so `BL-PWR-3/4` (DE-05) and the battery status codes in DE-10 are not implementable on this revision. |
| Telemetry | RX→TX battery/RSSI reporting? | Optional, diagnostics only; not core. |

---

## Tracking

As code lands, each unit's `hardware/` and `software/` directory gets its own README
documenting parts, pinout, and build/flash instructions. This roadmap is the
single source of truth for "what's next."
