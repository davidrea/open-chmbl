/*
 * Ride logging — CAN to microSD, gated on the engine cutoff (kill) switch.
 *
 * Ride-validation companion to the brake light: while the switch is in RUN,
 * every frame on the bus is written to the microSD (J5) as a PCAN .trc file,
 * the same dialect the logger firmware writes and the offline tools read
 * (tools/trc_viz.html, tools/trc_viz.py). One ride then yields both the
 * light's behaviour on IO18 and the capture that explains it.
 *
 * Recording is automatic and silent. The decision of when lives in the pure
 * gate (log_gate.h); this module is the plumbing around it:
 *
 *   CAN-RX tap   trc_log_frame(), called by can_rx.c for EVERY received
 *                frame; queues it while a file is open, counts drops if full.
 *   Writer task  owns the card, the open file and the gate. Evaluates the
 *                gate at 20 Hz from the decoded engine_cutoff signal, opens
 *                a new N.trc on each session start, closes it on CUTOFF or
 *                bus silence, and fflush+fsyncs it periodically so a power cut
 *                leaves a readable file.
 *   Card-detect  polls DET_A and posts insert/remove events to the writer.
 *
 * There is no recording indicator. A card that is present but cannot be
 * mounted, opened or written lights the fault lamp (status_led_log_fault()).
 * A missing card is not a fault: J5 is unpopulated on a production
 * transmitter.
 *
 * Built only with CONFIG_CHMBL_TRC_LOG; otherwise every entry point here is a
 * no-op, so callers need no #if of their own.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/twai.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     enabled;      /* built with CONFIG_CHMBL_TRC_LOG */
    bool     card_present; /* DET_A, debounced */
    bool     mounted;      /* FAT filesystem up */
    bool     gate_active;  /* a kill-switch RUN session is in progress */
    bool     recording;    /* a file is open */
    bool     fault;        /* card/file fault currently raised */
    unsigned file_num;     /* N of the open file, else the last one; 0 = none */
    uint32_t file_frames;  /* frames written to that file */
    uint32_t dropped;      /* frames dropped (queue full) for that file */
    uint32_t sessions;     /* RUN sessions since boot */
    uint32_t files;        /* files opened since boot */
} trc_log_status_t;

/* Create the queues and start the writer and card-detect tasks. Call before
 * can_rx_init(), so the frame tap exists before the first frame does. The
 * card is mounted from the writer task, so this does not delay boot. */
void trc_log_init(void);

/* CAN-RX hot path: hand over one received frame. Cheap no-op when no file is
 * open. Must not be called from inside a critical section. */
void trc_log_frame(const twai_message_t *msg);

/* Display snapshot for the `rec` command. Fields are read individually, so a
 * snapshot taken mid-transition may mix old and new values. */
void trc_log_get_status(trc_log_status_t *out);

#ifdef __cplusplus
}
#endif
