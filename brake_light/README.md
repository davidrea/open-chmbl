# Brake_light (rider-side)

Battery-powered ~8″ wide red LED bar worn on the rider's back — magnetically clamped
to a jacket or the top of a backpack between the shoulder blades (a steel strip or
washers inside the garment / pack completes the mount). Receives braking state from
the [`transmitter`](../transmitter) over [ESP-NOW](../docs/protocol.md) and renders
it on the bar with ambient-aware brightness, while managing its own 1S 18650 Li-ion
battery, USB-C charging, pairing, and [link-loss failsafe](../docs/protocol.md#4-failsafe--link-health).

Helmet fitment is **deferred** — the current form factor targets a thin, short-in-
the-vertical-axis, wide fabric-mounted bar so shell curvature and helmet-certification
questions don't gate the first build.

- [`hardware/`](hardware) — LED bar, 18650 + charging (chip-down), enclosure,
  magnetic fabric mount.
- [`software/`](software) — ESP32-C3 firmware (ESP-NOW RX + LED pattern engine).

⚠️ Mounting and battery safety are not optional — see
[`docs/safety-regulatory.md`](../docs/safety-regulatory.md). See
[`ARCHITECTURE.md`](../ARCHITECTURE.md) for the big picture.
