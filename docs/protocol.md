# Wireless protocol (ESP-NOW)

The transmitter and brake_light talk over **ESP-NOW**: connectionless, low-latency
2.4 GHz, no Wi-Fi association. The link is **pre-paired** and **encrypted**.

---

## 1. Pairing (one-time)

ESP-NOW peers are identified by MAC. Pairing exchanges MACs and a shared key once,
off the road:

1. Hold the brake_light button to enter **pairing mode** (status LED blinks).
2. Put the transmitter in pairing mode the same way (or it advertises on boot until
   paired).
3. They exchange MAC addresses on a known pairing channel and each store the other
   as an **encrypted peer** (ESP-NOW PMK + per-peer LMK).
4. MACs + key are persisted to NVS. Subsequent boots are silent and automatic.

Re-pairing is an explicit user action so a stranger's transmitter can't drive your
helmet light.

---

## 2. Message format

One small fixed struct, broadcast by the TX as a **heartbeat** at 20–50 Hz (sent
every tick regardless of whether state changed — see failsafe).

```c
typedef enum : uint8_t {
    ST_OFF   = 0,  // not braking
    ST_DECEL = 1,  // RESERVED — not emitted by the current TX FSM (future soft cue)
    ST_BRAKE = 2,  // braking / stopped (TX states BRAKING and STOPPED both map here)
} brake_state_t;

typedef struct __attribute__((packed)) {
    uint8_t       version;     // protocol version
    uint8_t       msg_type;    // HEARTBEAT / TELEMETRY / PAIR
    uint16_t      seq;         // monotonic sequence (replay/staleness)
    brake_state_t state;       // current braking state
    uint8_t       flags;       // bit0: decel_enabled, bit1: tx_low_power, ...
    uint8_t       tx_health;   // CAN-ok, bus-idle, etc. (diagnostics)
    uint8_t       reserved;
} chmbl_msg_t;                 // 8 bytes; ESP-NOW payload ≤ 250 B, lots of headroom
```

Optional **RX→TX telemetry** (low rate, for diagnostics only — e.g. helmet battery
%, RSSI) can reuse the channel with `msg_type = TELEMETRY`. Not required for core
function.

---

## 3. Sequence, staleness & replay

- `seq` increments every TX packet.
- RX **drops** any packet whose `seq` is not newer than the last accepted one
  (handles reordering and basic replay).
- RX timestamps the last accepted packet for the link watchdog.
- App-level integrity rides on top of ESP-NOW's built-in encryption (encrypted
  peer). A short rolling check can be added if threat model warrants.

---

## 4. Failsafe & link health

| Condition | RX behavior |
|-----------|-------------|
| Packets arriving, `state = OFF/DECEL/BRAKE` | Render that state. |
| **No valid packet for > `LINK_TIMEOUT_MS` (≤ 300 ms)** | **Link-lost indication**: steady running light + slow fault blink. Never silently dark; never a latched fake `BRAKE`. |
| Packet with old `seq` | Drop. |
| Pre-first-packet (boot) | Waiting indication. |
| TX `tx_low_power`/`bus_idle` flag set | RX may dim to a parked/idle state. |

Because state is sent every tick, *loss of heartbeat* is the signal that something
is wrong — the RX never has to infer "not braking" from silence.

---

## 5. Timing budget

```
CAN frame → decode → state machine      ≤ ~25 ms (incl. 50 Hz tick + accel smoothing)
ESP-NOW hop                              ≈  2–5 ms
RX render → LED update                   ≤ ~16 ms (60 Hz)
                                         -----------
Brake event → LED                        ≤ ~100 ms target
```

---

## 6. Channel & coexistence notes

- Fix the ESP-NOW channel (or scan/agree during pairing) so TX and RX stay aligned.
- The TX's Wi-Fi radio is used only for ESP-NOW; no AP/STA association.
- 2.4 GHz is shared with the rider's phone/comms — keep payloads tiny and the rate
  bounded; ESP-NOW retransmit is fine for a heartbeat (next packet is ~20–50 ms away
  anyway).
