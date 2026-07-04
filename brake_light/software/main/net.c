/*
 * ESP-NOW heartbeat receive (DE-01 / BL-NET).
 *
 * Validates that inbound heartbeats come from the paired peer and hands
 * accepted packets to the link watchdog (link.c), which owns sequence
 * validation, last-rx timestamping, and driving the stand-in brake light.
 */
#include <string.h>

#include "protocol.h"
#include "pairing.h"
#include "link.h"
#include "net.h"

static uint32_t s_drop_unpaired;

static void on_data_recv(const uint8_t *mac, const chmbl_msg_t *msg)
{
    uint8_t peer[6];
    if (!pairing_get_peer(peer) || memcmp(mac, peer, 6) != 0) {
        s_drop_unpaired++;
        return;
    }
    link_on_rx(msg->seq, (brake_state_t)msg->state);
}

void net_init(void)
{
    pairing_set_data_cb(on_data_recv);
}

uint32_t net_get_drop_unpaired(void)
{
    return s_drop_unpaired;
}
