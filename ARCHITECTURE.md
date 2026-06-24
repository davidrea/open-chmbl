# Open-CHMBL — System Architecture

**Open-sourCe Helmet-Mounted Brake Light** for motorcyclists.

A battery-powered LED bar mounts on the back of the rider's helmet and lights up
when the motorcycle is braking. Brake state is derived from the bike's **CAN bus**
(via the diagnostic port) and sent wirelessly to the helmet over **ESP-NOW**.

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

Open-CHMBL deliberately avoids both. Instead it reads brake status (and related
engine signals) from the **CAN bus exposed on the modern Euro 5 diagnostic port**.
This is a data-only, read-only interface that does not modify any bike wiring.

Reading more than just the stop-lamp flag also lets us do something a simple tap
cannot: distinguish **friction braking** (rider pulls the lever / presses the
pedal) from **engine braking / deceleration** (throttle closed, bike slowing) and
from **gear shifts** (clutch pulled). See [§4](#4-braking-state-machine).

---

## 2. System overview

```
        MOTORCYCLE                                  HELMET
 ┌──────────────────────────┐              ┌───────────────────────────┐
 │  6-pin diagnostic port    │              │  RX unit                  │
 │     │ CAN-H / CAN-L        │              │  ┌─────────────────────┐  │
 │     ▼                      │   ESP-NOW    │  │ ESP32-C3            │  │
 │  ┌───────────────────┐     │  (2.4 GHz,   │  │  • RX callback      │  │
 │  │ TX unit            │    ))) encrypted, │  │  • state interpret  │  │
 │  │  • CAN transceiver │     │   pre-paired)│  │  • LED pattern eng. │  │
 │  │  • ESP32-C3 (TWAI  │ ─ ─ ─ ─ ─ ─ ─ ─►  │  │  • ambient dimming  │  │
 │  │    listen-only)    │     │              │  │  • battery monitor  │  │
 │  │  • bike-profile    │     │              │  └─────────┬───────────┘  │
 │  │    decoder         │     │              │            ▼              │
 │  │  • state machine   │     │              │     Red LED bar +         │
 │  │  • ESP-NOW TX       │     │              │     LED driver           │
 │  └───────────────────┘     │              │     LiPo + USB-C charge   │
 │  Powered from bike (12 V)   │              │  Self-powered (battery)   │
 └──────────────────────────┘              └───────────────────────────┘
```

Two independent units:

| Unit | Lives on | Power | Job |
|------|----------|-------|-----|
| **`transmitter/`** (bike-side, "TX") | Plugs into the diagnostic port | Bike 12 V (switched) | Sniff CAN, decode brake/RPM/throttle/clutch, run the braking state machine, broadcast state over ESP-NOW |
| **`brake_light/`** (helmet-side, "RX") | Mounted on the helmet shell | On-board LiPo, USB-C charge | Receive state, drive the LED bar with the right pattern/brightness, manage battery & link health |

> Throughout these docs "TX" = the `transmitter` unit and "RX" = the `brake_light` unit.

The TX does all interpretation. The RX is intentionally "dumb": it renders whatever
discrete state the TX tells it to, plus a couple of local concerns (brightness vs.
ambient light, battery, link-loss fault). Keeping the logic on one side makes the
protocol small and the failure modes easy to reason about.

---

## 3. Reference-bike-first strategy

Brake status, throttle, RPM and clutch are **not** reliably available as standard
OBD-II PIDs on motorcycles. They live in manufacturer-specific CAN frames whose IDs
and bit layouts must be reverse-engineered per model. Trying to be universal on day
one is the fastest way to ship nothing.

So: **pick one reference motorcycle, reverse-engineer it fully, prove the whole
chain, then generalize.** The decoder is written against a small per-bike "profile"
(CAN ID + bit offsets + scaling) so adding a second bike later is a data change, not
a rewrite — but we only fill in one profile now.

The reference bike should be **whatever the first contributor actually owns and can
put on a bench with the wheel spinning.** Recommended characteristics: Euro 5
(post-2020), exposes CAN-H/CAN-L on the diagnostic connector, and ideally has an
active hobbyist reverse-engineering community. See
[`docs/can-profiles.md`](docs/can-profiles.md) for the sniffing methodology and the
profile data structure, and **[`docs/can-profiles.md#reference-target`](docs/can-profiles.md#reference-target)**
for where to record the chosen bike and its decoded frames.

---

## 4. Braking state machine

The TX fuses four input signals into one of a few discrete output states. Brightness
and pattern for each state are decided on the RX.

**Inputs (from CAN):**
- `brake_switch` — front and/or rear stop-lamp switch (authoritative).
- `throttle_pct` — throttle/APS position, 0–100 %.
- `rpm` — engine speed.
- `clutch_pulled` — clutch switch (disengaged), if the bike exposes it.

**Output states:**

| State | Condition (sketch) | Meaning | Suggested render |
|-------|--------------------|---------|------------------|
| `OFF` | Throttle > ~3 % or steady cruise | Accelerating/holding | Light off (or dim running light) |
| `BRAKE` | `brake_switch` active | Rider is braking | **Bright, solid red** (legally meaningful signal) |
| `DECEL` | No brake switch **and** throttle ≈ 0 **and** `clutch_pulled` false **and** RPM falling faster than a threshold | Engine braking / coasting deceleration | Medium red (a "slowing" courtesy cue) |
| `SHIFT` (suppress) | `clutch_pulled` true | Gear change — RPM/throttle are noisy | Hold previous non-brake state; do **not** flash |

Design rationale:

- **The brake switch is authoritative and primary.** It is the one signal with a
  clear, legally-recognized meaning. `BRAKE` always wins over `DECEL`/`SHIFT`.
- **`DECEL` is a soft, secondary cue.** RPM derivative is *not* a real road-deceleration
  measurement (it's gear- and load-dependent), so we never try to estimate g-force
  from it. We only use "throttle closed + RPM dropping + clutch out" as a coarse
  "the bike is slowing under engine braking" hint. Whether `DECEL` is even legal to
  display depends on jurisdiction — it is **off by default** (see config).
- **Clutch gates `DECEL`.** During clutch-in (shifting, or pulling up to a stop) RPM
  and throttle swing wildly; without gating, the light would flicker. If the bike
  doesn't expose a clutch signal, `DECEL` should be disabled or made much more
  conservative.
- All inputs are **debounced** and the state machine uses **hysteresis / minimum
  dwell times** so the light can't strobe. Flashing brake lights are illegal in many
  places anyway — see safety doc.

Full transition table, thresholds, and timing live in
[`docs/firmware.md#braking-state-machine`](docs/firmware.md#braking-state-machine).

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
3. **Helmet integrity.** Non-penetrating mount (adhesive pad / strap); never drill a
   helmet. Keep mass low and mount **frangible/breakaway** to limit rotational
   injury and snag risk.
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
│   ├── hardware.md            ← BOM, power, connectors, enclosures
│   ├── firmware.md            ← tasks, state machine, config, build
│   ├── protocol.md            ← ESP-NOW message + pairing + failsafe
│   ├── can-profiles.md        ← CAN sniffing method + per-bike profile format
│   ├── safety-regulatory.md   ← legal, functional safety, helmet safety
│   └── roadmap.md             ← phased plan, milestones, open questions
├── transmitter/               ← bike-side unit
│   ├── hardware/              ← schematics, BOM, connector, enclosure
│   └── software/              ← ESP32 firmware (TWAI listen-only + ESP-NOW TX)
└── brake_light/               ← helmet-side unit
    ├── hardware/              ← LED bar, LiPo+charge, mount, enclosure
    └── software/              ← ESP32 firmware (ESP-NOW RX + LED engine)
```

Each unit and each `hardware/`/`software/` subdirectory has its own README. The
shared protocol/profile/state definitions are referenced from both `software/`
directories — see [`docs/roadmap.md`](docs/roadmap.md) for whether they become a
real shared library or duplicated headers.

---

## 8. Where to go next

- Understand the parts and power: [`docs/hardware.md`](docs/hardware.md)
- Understand the code structure & state machine: [`docs/firmware.md`](docs/firmware.md)
- Understand the radio link: [`docs/protocol.md`](docs/protocol.md)
- Reverse-engineer the reference bike: [`docs/can-profiles.md`](docs/can-profiles.md)
- **Before building anything physical:** [`docs/safety-regulatory.md`](docs/safety-regulatory.md)
- The plan and what's undecided: [`docs/roadmap.md`](docs/roadmap.md)
