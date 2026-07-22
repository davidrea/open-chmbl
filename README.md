# open-chmbl

In automotive engineering, the CHMBL is the "Center High-Mounted Brake Light" — this
repository is an **"Open-sourCe Helmet Mounted Brake Light"** for motorcyclists.

A battery-powered ~8″ LED bar worn on the rider's back — magnetically clamped to a
jacket or backpack — lights up when the motorcycle is braking. The reference bike
doesn't publish a brake-switch bit on its **CAN bus**, so braking is **inferred from
wheel-speed deceleration** read off the bus (via the Euro 5 diagnostic port,
**listen-only**) and sent wirelessly to the rider-side unit over **ESP-NOW**. Clutch
and gear/neutral context lets it hold the light through a stop and release it sensibly
when the bike pulls away — deriving deceleration from the bike's own CAN wheel-speed
(not an accelerometer) keeps it clear of the inertial-detection patents.

Two units, both ESP32-based:

- **[`transmitter/`](transmitter)** — bike-side; plugs into the diagnostic port,
  decodes CAN, broadcasts braking state.
- **[`brake_light/`](brake_light)** — rider-side; battery-powered LED bar (fabric
  mount this rev; helmet fitment deferred) that renders the state.

> ⚠️ Auxiliary device. It does **not** replace the bike's factory brake light, and
> helmet-mounted lighting is restricted in some places. Read
> **[`docs/safety-regulatory.md`](docs/safety-regulatory.md)** before building or
> riding.

## Start here

- **[`ARCHITECTURE.md`](ARCHITECTURE.md)** — system overview and design index.
- **[`docs/feature-functions.md`](docs/feature-functions.md)** — per-device capabilities.
- **[`docs/design/README.md`](docs/design/README.md)** — how we build it: design
  elements, implemented one at a time in isolation.
- [`docs/cli.md`](docs/cli.md) · [`docs/hardware.md`](docs/hardware.md) ·
  [`docs/firmware.md`](docs/firmware.md) · [`docs/protocol.md`](docs/protocol.md) ·
  [`docs/can-profiles.md`](docs/can-profiles.md) ·
  [`docs/safety-regulatory.md`](docs/safety-regulatory.md) ·
  [`docs/roadmap.md`](docs/roadmap.md)

Status: **architecture / design phase** (see the roadmap).
