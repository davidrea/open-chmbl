/*
 * ESP-NOW heartbeat receive (DE-01 / BL-NET) — public entry points.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the pairing data callback that receives inbound heartbeats.
 * pairing_init() must have run first. */
void net_init(void);

/* Count of accepted-radio packets dropped because the sender wasn't the
 * paired peer (diagnostics, surfaced by `link show`). */
uint32_t net_get_drop_unpaired(void);

#ifdef __cplusplus
}
#endif
