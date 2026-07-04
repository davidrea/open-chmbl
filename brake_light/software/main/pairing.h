/*
 * ESP-NOW pairing (DE-01) — public entry points.
 *
 * NOTE: pairing.c / pairing.h are duplicated verbatim with the brake_light
 * firmware for now, same as console.c (see console.h); promote to a shared
 * component once the wire format/pairing scheme stabilizes.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up NVS + Wi-Fi (STA, no AP association) + ESP-NOW on the fixed
 * CHMBL_NET_CHANNEL, set the shared PMK, register the process-wide ESP-NOW
 * recv callback, and silently restore any peer persisted from a previous
 * pairing. Call once, before net_init(). */
void pairing_init(void);

/* True once a peer MAC is known (restored or paired this session) and
 * registered as an encrypted ESP-NOW peer. */
bool pairing_has_peer(void);

/* Copies the current peer MAC (6 bytes) into `out`; returns false if
 * unpaired. */
bool pairing_get_peer(uint8_t out[6]);

/* Enter pairing mode: broadcast MSG_PAIR announcements and listen for the
 * other side's. Blocks the calling task for up to CHMBL_PAIR_TIMEOUT_S
 * seconds. On success, registers the discovered MAC as an encrypted peer
 * and persists it to NVS. Returns true iff a peer was paired. */
bool pairing_start(void);

/* Forget the paired peer (removes the ESP-NOW peer entry and the NVS
 * record). */
void pairing_clear(void);

/* Register the callback that receives non-pairing (HEARTBEAT/TELEMETRY)
 * messages. ESP-NOW allows only one process-wide recv callback; pairing.c
 * owns it and demuxes MSG_PAIR locally, forwarding everything else here. */
typedef void (*pairing_data_cb_t)(const uint8_t *mac, const chmbl_msg_t *msg);
void pairing_set_data_cb(pairing_data_cb_t cb);

#ifdef __cplusplus
}
#endif
