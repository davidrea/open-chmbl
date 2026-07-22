# Open-CHMBL — System Architecture

**Open-sourCe Helmet-Mounted Brake Light** for motorcyclists.

A battery-powered LED bar worn on the rider's back — magnetically clamped to a
**jacket or backpack** between the shoulder blades — lights up when the motorcycle
is braking. Brake state is derived from the bike's **CAN bus** (via the diagnostic
port) and sent wirelessly to the rider-side unit over **ESP-NOW**.

> The name is aspirational: helmet fitment is deferred for the first build (shell
> curvature + helmet certification make it a harder problem). The rider-side
> hardware currently targets the **fabric attachment** case only — see
> [`docs/hardware.md §2`](docs/hardware.md#2-brake_light-rider-side).

> ⚠️ **Read [`docs/safety-regulatory.md`](docs/safety-regulatory.md) first.**
> This is an **auxiliary** light. It does not replace the motorcycle's own legally
> required brake light, and helmet-mounted lighting is restricted or prohibited in
> some jurisdictions. Treat this project as track/off-road/educational until you
> have confirmed legality for your use.

---

## 1. Why this approach

Two existing patent families constrain the obvious designs:

- **Tapping the brake-light wires** directly (galvanic/optoisolated tap on the
  bike's stop-lamp circuit).
- **Inertial detection** — using an accelerometer/gyro on the helmet or light to
  infer deceleration.

Open-CHMBL deliberately avoids both. Instead it reads vehicle data from the **CAN bus
exposed on the modern Euro 5 diagnostic port** — a data-only, read-only interface that
does not modify any bike wiring.

On the reference bike (Triumph Speed 400) the **brake-switch state is not published on
the CAN bus** — repeated captures found no brake bit. The signals that *are* present and
displayed on the instrument cluster — **wheel speed** and **gear/neutral** — plus the
confirmed **throttle** and **clutch** signals are enough to do something a stop-lamp tap
cannot: infer braking from **deceleration of the bike's own wheel-speed signal**, and
distinguish steady cruise, accelerating away, and a hold at a standstill.

Crucially, the deceleration estimate is derived from **CAN wheel-speed data, not an
on-board accelerometer/gyro**, so it stays clear of the inertial-detection patent family
above. See [§4](#4-braking-state-machine) for the resulting state machine.

---

## 2. System overview

```
        MOTORCYCLE                              RIDER (jacket / pack)
 ┌──────────────────────────┐              ┌───────────────────────────┐
 │  6-pin diagnostic port    │              │  RX unit                  │
 │     │ CAN-H / CAN-L        │              │  ┌─────────────────────┐  │
 │     ▼                      │   ESP-NOW    │  │ ESP32-C3            │  │
 │  ┌───────────────────┐     │  (2.4 GHz,   │  │  • RX callback      │  │
 │  │ TX unit            │    ))) encrypted, │  │  • state interpret  │  │
 │  │  • CAN transceiver │     │   pre-paired)│  │  • LED pattern eng. │  │
 │  │  • ESP32-S3 (TWAI  │ ─ ─ ─ ─ ─ ─ ─ ─►  │  │  • ambient dimming  │  │
 │  │    listen-only)    │     │              │  │  • battery monitor  │  │
 │  │  • bike-profile    │     │              │  └─────────┬───────────┘  │
 │  │    decoder         │     │              │            ▼              │
 │  │  • state machine   │     │              │     ~8″ red LED bar       │
 │  │  • ESP-NOW TX       │     │              │     + LED driver         │
 │  └───────────────────┘     │              │     1S 18650 + USB-C      │
 │  Powered from bike (12 V)   │              │  Magnetic to fabric       │
 └──────────────────────────┘              └───────────────────────────┘
```

Two independent units:

| Unit | Lives on | Power | Job |
|------|----------|-------|-----|
| **`transmitter/`** (bike-side, "TX") | Plugs into the diagnostic port | Bike 12 V (switched) | Sniff CAN, decode brake/RPM/throttle/clutch, run the braking state machine, broadcast state over ESP-NOW |
| **`brake_light/`** (rider-side, "RX") | Magnetically clamped to the jacket / backpack (helmet fitment deferred) | On-board 1S 18650 Li-ion, USB-C charge | Receive state, drive the ~8″ LED bar with the right pattern/brightness, manage battery & link health |

> Throughout these docs "TX" = the `transmitter` unit and "RX" = the `brake_light` unit.

> TX runs an **ESP32-S3** (not the C3 shown for RX above) — the TX hardware plan
> reuses the [`logger/`](logger) PCB, which needed the S3's SDMMC peripheral for its
> microSD slot. RX has no SD card and stays on the ESP32-C3. See
> [`docs/hardware.md §1`](docs/hardware.md#1-transmitter-bike-side).

The TX does all interpretation. The RX is intentionally "dumb": it renders whatever
discrete state the TX tells it to, plus a couple of local concerns (brightness vs.
ambient light, battery, link-loss fault). Keeping the logic on one side makes the
protocol small and the failure modes easy to reason about.

---

## 3. Reference-bike-first strategy

Wheel speed, throttle, RPM, clutch and gear are **not** reliably available as standard
OBD-II PIDs on motorcycles (and **brake-switch state may not be on the bus at all** — on
the reference bike it isn't). The signals that exist live in manufacturer-specific CAN
frames whose IDs and bit layouts must be reverse-engineered per model. Trying to be
universal on day one is the fastest way to ship nothing.

So: **pick one reference motorcycle, reverse-engineer it fully, prove the whole
chain, then generalize.** The decoder is written against a small per-bike "profile"
(CAN ID + bit offsets + scaling) so adding a second bike later is a data change, not
a rewrite — but we only fill in one profile now.

**The reference bike is the Triumph Speed 400.** Its sibling **Scrambler 400 X**
shares the same TR-series 398 cc single powertrain/ECU, so one profile is expected to
cover both. The **Street Triple 765** is a stretch target — a different platform
(triple, separate ECU = its own profile) but the same red 6-pin diagnostic connector,
so the TX hardware carries over. Both expose CAN-H/CAN-L (plus a K-line) on a red
6-pin OBD2 port under the seat. See
[`docs/can-profiles.md`](docs/can-profiles.md) for the sniffing methodology, the
profile data structure, and **[`docs/can-profiles.md#5-reference-target--triumph-speed-400-tr-series-platform`](docs/can-profiles.md#5-reference-target--triumph-speed-400-tr-series-platform)**
for the connector details and decode table.

> ⚠️ The single biggest unknown for these bikes is whether the diagnostic port
> exposes **free-running broadcast** CAN (which our listen-only sniffer can read) or
> only **request/response** diagnostic data. Resolving that is the first job in
> Phase 2 — see the CAN doc.

---

## 4. Braking state machine

Because the reference bus has **no brake-switch bit**, the TX infers the light from
**wheel-speed-derived acceleration**, qualified by clutch and gear/neutral context.
Brightness and pattern for each state are decided on the RX.

**Inputs (from CAN):**
- `wheel_speed` — cluster wheel/road speed; the primary input (converted to MPH).
- `clutch_pulled` — clutch lever switch (confirmed present on the reference bus).
- `gear` / `neutral` — gear position / neutral indicator (shown on the cluster).
- `throttle_pct`, `rpm` — confirmed present; retained for diagnostics/telemetry, not
  required by the state machine.

**Derived:** `accel` — the slope of a smoothed `wheel_speed`, in MPH/s (− = decel). This
is the core trigger.

**Output states** (the light is effectively binary on/off; two on-states exist because
their *off* conditions differ):

| State | Condition (sketch) | Meaning | Render |
|-------|--------------------|---------|--------|
| `OFF` | Accelerating, steady cruise, or armed-but-idle | Not braking | Light off (or dim running light) |
| `BRAKING` | Deceleration exceeds a threshold while moving | Bike is slowing | **Bright, solid red** |
| `STOPPED` | At/near a standstill (held on) | Bike has stopped | **Bright, solid red** |

`BRAKING` and `STOPPED` are both sent to the helmet as `BRAKE` (light on). The protocol
keeps a `DECEL` value **reserved** for a possible future soft-cue tier.

Design rationale:

- **Deceleration is the signal.** With no stop-lamp flag, "the bike is slowing harder
  than `DECEL_ON_MPHPS`" is the braking trigger. It comes from the bike's CAN
  wheel-speed (not an IMU), keeping clear of the inertial-detection patent.
- **Two thresholds, with hysteresis.** A larger deceleration turns the light *on*; a
  positive acceleration (above a minimum speed) or a sustained steady speed turns it
  *off*. Different on/off conditions prevent chatter around any single threshold.
- **Stop handling.** Coasting below `STOP_SPEED_MPH` turns the light on (or keeps it on)
  and *holds* it through a standstill — a stopped bike should read as braking to
  following traffic. It releases when the bike pulls away, when the **clutch is released
  in gear** (launching — this is where the gear/neutral signal earns its keep, since a
  neutral stop must *not* be read as a launch), or after a long-stop timeout.
- **Anti-strobe.** A global minimum-dwell floor gates all transitions so the light can't
  strobe. Flashing brake lights are illegal in many places anyway — see safety doc.

Full transition table, thresholds, and timing live in
[`docs/firmware.md#braking-state-machine`](docs/firmware.md#braking-state-machine); the
rationale and the SMC model are in
[`docs/design/de-09-brake-decel-logic.md`](docs/design/de-09-brake-decel-logic.md).

---

## 5. Wireless link (ESP-NOW)

- **Pre-paired, encrypted, connectionless.** TX and RX exchange MACs once during a
  pairing ritual, then store each other as encrypted ESP-NOW peers (PMK/LMK). No
  Wi-Fi AP, no pairing on the road.
- **Heartbeat model.** The TX transmits state at a fixed rate (target **20–50 Hz**)
  whether or not state changed. The RX treats *absence* of packets as a fault.
- **Link-loss failsafe.** If the RX hasn't heard a valid packet within a timeout
  (target **≤ 300 ms**), it enters a **distinct "link-lost" indication** — a steady
  running light plus a slow fault blink. It must **not** assume "no packet = not
  braking" (silently going dark) and must **not** latch a stale `BRAKE` on forever.
- **End-to-end latency budget:** brake event → LED ≤ **100 ms** (CAN read + state
  machine + ESP-NOW hop + LED update). ESP-NOW itself is single-digit ms.
- **Message integrity:** sequence number for replay/staleness detection; ESP-NOW's
  built-in encryption + a small app-level check.

Message formats, pairing procedure, sequence/failsafe details:
[`docs/protocol.md`](docs/protocol.md).

---

## 6. Critical safety constraints (summary)

These are hard requirements, expanded in [`docs/safety-regulatory.md`](docs/safety-regulatory.md):

1. **CAN is listen-only.** The ESP32 TWAI controller runs in **listen-only / silent
   mode** — it never ACKs and never transmits on the bike bus. We must not risk
   disturbing the motorcycle's own ECUs.
2. **Auxiliary, not a replacement.** Open-CHMBL never substitutes for the bike's
   factory brake light.
3. **Mount safety.** Baseline fabric mount uses **magnets that peel off** under
   shear — the magnetic interface *is* the frangible/breakaway release, satisfying
   the safety doc's snag-and-crash requirements without a separate breakaway pin.
   Keep mass low. The **helmet variant** (deferred) inherits the same hard rule:
   non-penetrating only, never drill the shell.
4. **No false braking, no strobing.** Failsafes bias toward an honest, non-alarming
   state. Flashing patterns are disabled by default for legal reasons.
5. **Parasitic draw.** The TX must sleep / cut load when the bike is off so it can't
   flatten the motorcycle battery.

---

## 7. Repository layout (planned)

Documentation now; code later. Anticipated structure:

```
open-chmbl/
├── ARCHITECTURE.md            ← this file (index + overview)
├── README.md
├── docs/
│   ├── feature-functions.md   ← per-device capability decomposition (FFL IDs)
│   ├── cli.md                 ← developer CLI to fake/view I/O (isolation testing)
│   ├── hardware.md            ← BOM, power, connectors, enclosures
│   ├── firmware.md            ← tasks, state machine, config, build
│   ├── protocol.md            ← ESP-NOW message + pairing + failsafe
│   ├── can-profiles.md        ← CAN sniffing method + per-bike profile format
│   ├── safety-regulatory.md   ← legal, functional safety, helmet safety
│   ├── roadmap.md             ← phased plan, milestones, open questions
│   └── design/                ← per-element design docs (build one at a time)
│       ├── README.md          ← process, template, design-element build order
│       └── de-*.md            ← ESP-NOW, auto-brightness, link-loss, CAN, BRAKE/DECEL
├── transmitter/               ← bike-side unit
│   ├── hardware/              ← schematics, BOM, connector, enclosure
│   └── software/              ← ESP32 firmware (TWAI listen-only + ESP-NOW TX)
└── brake_light/               ← rider-side unit (fabric mount this rev)
    ├── hardware/              ← LED bar, LiPo+charge, mount, enclosure
    └── software/              ← ESP32 firmware (ESP-NOW RX + LED engine)
```

Each unit and each `hardware/`/`software/` subdirectory has its own README. The
shared protocol/profile/state definitions are referenced from both `software/`
directories — see [`docs/roadmap.md`](docs/roadmap.md) for whether they become a
real shared library or duplicated headers.

---

## 8. How we build it

We don't use shall-statement requirements. Instead:
**[feature-function lists](docs/feature-functions.md) → [design elements](docs/design/README.md)
→ isolated implementation → integration.** Each design element is built and proven on
its own, with its inputs **faked** and outputs **viewed** through the per-device
[developer CLI](docs/cli.md). The element build order lives in
[`docs/design/README.md`](docs/design/README.md).

## 9. Where to go next

- The capabilities, per device: [`docs/feature-functions.md`](docs/feature-functions.md)
- The build process & element order: [`docs/design/README.md`](docs/design/README.md)
- The developer CLI (fake/view I/O): [`docs/cli.md`](docs/cli.md)
- Understand the parts and power: [`docs/hardware.md`](docs/hardware.md)
- Understand the code structure & state machine: [`docs/firmware.md`](docs/firmware.md)
- Understand the radio link: [`docs/protocol.md`](docs/protocol.md)
- Reverse-engineer the reference bike: [`docs/can-profiles.md`](docs/can-profiles.md)
- **Before building anything physical:** [`docs/safety-regulatory.md`](docs/safety-regulatory.md)
- The plan and what's undecided: [`docs/roadmap.md`](docs/roadmap.md)
