# Brake_light — software

Helmet-side firmware (ESP32-C3, ESP-IDF). See
[`docs/firmware.md §2`](../../docs/firmware.md#2-brake_light-firmware-helmet-side).

Responsibilities:
- Receive `chmbl_msg_t` over [ESP-NOW](../../docs/protocol.md) (encrypted, pre-paired peer).
- Validate sequence / drop stale packets.
- Render state via the [LED pattern engine](../../docs/firmware.md#pattern-engine-suggested-mapping) (60 Hz), steady / no strobing.
- **Ambient-light dimming**, battery monitoring + low-battery warning.
- **Link-loss failsafe**: distinct indication, never silently dark, never a latched fake brake.
- Button UI: power, pairing, brightness cap.

_Firmware to be added (Phase 1)._
