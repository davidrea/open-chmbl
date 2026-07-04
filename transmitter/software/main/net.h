/*
 * ESP-NOW heartbeat transmit (DE-01 / TX-NET) — public entry points.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the ESP-NOW send callback and starts the heartbeat task.
 * pairing_init() must have run first. */
void net_init(void);

bool net_is_running(void);
void net_start(void);
void net_stop(void);

uint8_t net_get_rate(void);
void net_set_rate(uint8_t hz); /* clamped to 1-50 Hz; out-of-range is ignored */

/* Sends one heartbeat immediately, independent of net_start()/net_stop().
 * Returns false if unpaired or the radio rejects the send. */
bool net_send_now(void);

void net_get_stats(uint16_t *seq, uint32_t *sent_ok, uint32_t *sent_fail);

#ifdef __cplusplus
}
#endif
