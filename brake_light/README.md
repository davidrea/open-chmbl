# Brake_light (rider-side)

Battery-powered ~8″ wide red LED bar worn on the rider's back — magnetically clamped
to a jacket or the top of a backpack between the shoulder blades (a steel strip or
washers inside the garment / pack completes the mount). Receives braking state from
the [`transmitter`](../transmitter) over [ESP-NOW](../docs/protocol.md) and renders
it on the bar, while managing its own 1S 18650 Li-ion battery, USB-C charging, pairing,
and [link-loss failsafe](../docs/protocol.md#4-failsafe--link-health).

The bar is driven as a **plain binary output** — on when braking, off otherwise, steady,
never flashing. **Brightness levels and ambient auto-dimming are deferred** (decision:
[`docs/design/de-04`](../docs/design/de-04-led-render.md); deferred element:
[`de-02`](../docs/design/de-02-auto-brightness.md)); the ambient-light sensor is on the
board but unread, so nothing has to change in hardware when it is picked back up.

Helmet fitment is **deferred** — the current form factor targets a thin, short-in-
the-vertical-axis, wide fabric-mounted bar so shell curvature and helmet-certification
questions don't gate the first build.

- [`hardware/`](hardware) — LED bar, 18650 + charging (chip-down), enclosure,
  magnetic fabric mount. A **first-pass schematic + PCB are committed** (not built yet);
  see the [pin map](hardware/README.md#pin-map).
- [`software/`](software) — ESP32-C3 firmware (ESP-NOW RX + binary brake-bar render).

⚠️ Mounting and battery safety are not optional — see
[`docs/safety-regulatory.md`](../docs/safety-regulatory.md). See
[`ARCHITECTURE.md`](../ARCHITECTURE.md) for the big picture.
