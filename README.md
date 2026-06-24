# open-chmbl

In automotive engineering, the CHMBL is the "Center High-Mounted Brake Light" — this
repository is an **"Open-sourCe Helmet Mounted Brake Light"** for motorcyclists.

A battery-powered LED bar mounts on the back of the rider's helmet and lights up when
the motorcycle is braking. Brake state is read from the bike's **CAN bus** (via the
Euro 5 diagnostic port, **listen-only**) and sent wirelessly to the helmet over
**ESP-NOW**. Reading throttle, RPM and clutch alongside the brake switch lets it tell
**friction braking** from **engine braking** and **gear shifts**.

Two units, both ESP32-based:

- **[`transmitter/`](transmitter)** — bike-side; plugs into the diagnostic port,
  decodes CAN, broadcasts braking state.
- **[`brake_light/`](brake_light)** — helmet-side; battery-powered LED bar that
  renders the state.

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
