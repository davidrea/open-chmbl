/*
 * Wireless protocol (DE-01) — wire format shared between transmitter and
 * brake_light. Mirrors docs/protocol.md §2 exactly; keep the two copies
 * (this one and brake_light/software/main/protocol.h) in sync until they're
 * promoted to a real shared component (see roadmap.md).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHMBL_PROTOCOL_VERSION 1

typedef enum {
    ST_OFF   = 0,  /* not braking */
    ST_DECEL = 1,  /* RESERVED — not emitted by the current TX FSM */
    ST_BRAKE = 2,  /* braking / stopped */
} brake_state_t;

typedef enum {
    MSG_HEARTBEAT = 0,
    MSG_TELEMETRY = 1,
    MSG_PAIR      = 2,
} chmbl_msg_type_t;

typedef struct __attribute__((packed)) {
    uint8_t  version;   /* CHMBL_PROTOCOL_VERSION */
    uint8_t  msg_type;  /* chmbl_msg_type_t */
    uint16_t seq;       /* monotonic sequence (replay/staleness) */
    uint8_t  state;     /* brake_state_t */
    uint8_t  flags;     /* bit0: decel_enabled, bit1: tx_low_power, ... */
    uint8_t  tx_health; /* CAN-ok, bus-idle, etc. (diagnostics) */
    uint8_t  reserved;
} chmbl_msg_t; /* 8 bytes; ESP-NOW payload <= 250 B, lots of headroom */

#ifdef __cplusplus
}
#endif
