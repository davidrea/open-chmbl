/*
 * Link watchdog (DE-01 seq/timestamp + DE-03 placeholder failsafe) —
 * public entry points.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LINK_WAITING = 0, /* no packet accepted yet since boot */
    LINK_UP      = 1, /* last accepted packet within the timeout */
    LINK_LOST    = 2, /* no valid packet for longer than the timeout */
} link_status_t;

typedef struct {
    link_status_t status;
    brake_state_t last_state;
    uint16_t      last_seq;
    int32_t       last_rx_age_ms; /* -1 if never received */
    uint32_t      rx_count;
    uint32_t      drop_stale;
    uint16_t      timeout_ms;
} link_info_t;

/* Starts the link watchdog task (~1000/CHMBL_LINK_BLINK_MS Hz): while the
 * link is up it mirrors the received braking state onto the stand-in brake
 * light; otherwise (waiting or lost) it blinks that same LED as a
 * link-loss placeholder, since there's no separate status-indicator LED
 * (DE-10) on the ESP32 DevKitC yet — see
 * docs/design/de-03-link-loss-failsafe.md. */
void link_init(void);

/* Called by net.c for each packet accepted from the paired peer. Drops
 * non-newer sequence numbers itself (replay/reorder); otherwise updates
 * the last-rx timestamp and state. */
void link_on_rx(uint16_t seq, brake_state_t state);

void link_get_info(link_info_t *info);

#ifdef __cplusplus
}
#endif
