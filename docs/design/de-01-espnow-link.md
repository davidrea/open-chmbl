# DE-01 — ESP-NOW link

**Status:** 🔲 not started · **Device(s):** transmitter + brake_light · **Depends on:** DE-00

The pre-paired, encrypted heartbeat link between the two MCUs, built and proven on its
own before any CAN or LED behaviour is attached. See [`protocol.md`](../protocol.md)
for the message/pairing/failsafe definitions this implements.

## 1. Scope & isolation boundary
- **In:** pairing ritual, encrypted peer setup, heartbeat TX at 20–50 Hz, sequence
  numbering, RX validation + stale-drop, last-rx timestamping, link counters.
- **Out (faked at edges):** the *state* value is supplied by `tx state force` / `sig set`
  on the TX; on the BL the received state is just printed (`in show`, `render` is DE-04).
  Link-loss *behaviour* is DE-03; this element only exposes the timestamp/counters.
- **Isolation test:** two dev boards on a bench, no CAN, no LEDs.

## 2. FFL traceability
TX-NET-1…5, BL-NET-1…5.

## 3. Component selection
ESP32-C3 Wi-Fi radio (ESP-NOW); ESP-IDF `esp_now` + `esp_wifi`. No extra hardware.

## 4. I/O assignments & configuration
- Fixed/agreed Wi-Fi channel; STA interface, no AP association.
- PMK + per-peer LMK (encrypted peer); peer MAC + key persisted to NVS.
- `chmbl_msg_t` payload per [`protocol.md §2`](../protocol.md#2-message-format).

## 5. Firmware module/task decomposition
- TX: heartbeat task (timer-driven, 20–50 Hz) → `esp_now_send`.
- BL: `esp_now` RX callback → validate seq → update `{last_state, last_rx_time}`.
- Shared: `protocol.h` struct/enums; pairing helper; NVS peer store.
- Pure/host-testable: sequence-validation and staleness logic.

## 6. CLI hooks
- TX: `pair *`, `net show`, `net rate`, `net send`, `state force`.
- BL: `pair *`, `in show`, `link show`.

## 7. Isolation acceptance
- Pair two boards; TX `state force BRAKE` → BL `in show` reflects it within the latency
  budget; `net show`/`link show` show healthy counters and advancing sequence; an
  unpaired board is ignored.

## 8. Open items
- Channel selection/agreement during pairing vs. fixed channel.
- Whether to enable the optional BL→TX telemetry now or defer.
