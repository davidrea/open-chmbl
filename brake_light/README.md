# Brake_light (helmet-side)

Battery-powered LED bar mounted on the back of the helmet. Receives braking state
from the [`transmitter`](../transmitter) over [ESP-NOW](../docs/protocol.md) and
renders it on the LED bar with ambient-aware brightness, while managing its own
LiPo battery, USB-C charging, pairing, and [link-loss failsafe](../docs/protocol.md#4-failsafe--link-health).

- [`hardware/`](hardware) — LED bar, LiPo + charging, enclosure, breakaway mount.
- [`software/`](software) — ESP32-C3 firmware (ESP-NOW RX + LED pattern engine).

⚠️ Mounting and battery safety are not optional — see
[`docs/safety-regulatory.md`](../docs/safety-regulatory.md). See
[`ARCHITECTURE.md`](../ARCHITECTURE.md) for the big picture.
