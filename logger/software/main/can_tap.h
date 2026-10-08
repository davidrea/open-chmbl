/*
 * CAN decode tap — the logger's live view of the bike's signals.
 *
 * Wraps one components/chmbl_can decoder instance (DE-08). Every frame the TWAI
 * RX task receives is fed here, ALWAYS — not only while recording. Two
 * consumers depend on that:
 *
 *   * fsm_preview.c, which needs speed/accel/clutch/gear to run the DE-09 state
 *     machine and light the remote LED; and
 *   * the automatic recording gate in logger_main.c, which watches the engine
 *     kill switch. If the tap only ran while recording, the gate could never
 *     turn recording on in the first place.
 *
 * Thread-safety: can_tap_feed() is called from the CAN-RX task (~1500 frames/s
 * on the reference bus) and can_tap_snapshot() from the 50 Hz FSM task and the
 * 10 Hz gate task. A mutex keeps the decoder single-owner; FreeRTOS mutexes
 * carry priority inheritance, so the low-rate readers cannot block the RX task
 * for long. Snapshots are taken under the lock and copied out, so a consumer
 * never reads a half-updated signal set.
 */
#ifndef CAN_TAP_H
#define CAN_TAP_H

#include <stdbool.h>
#include <stdint.h>

/* A consistent copy of everything the app reads from the decoder. */
typedef struct {
    /* --- DE-09 inputs ------------------------------------------------- */
    float speed_mph;
    bool  speed_valid;     /* seen and fresh (CAN_DECODE_STALE_MS)        */
    float accel_mphps;     /* derived, smoothed slope of wheel speed      */
    bool  accel_valid;
    bool  clutch_pulled;
    bool  in_gear;         /* gear != 0; neutral is NOT in gear           */
    float gear;            /* raw value, for the transition log           */

    /* --- recording gate: the engine cutoff (kill) switch -------------- */
    bool     cutoff_seen;    /* a 0x121 carrying the flag has been decoded */
    bool     cutoff;         /* true = kill ASSERTED (switch at STOP)      */
    uint32_t cutoff_age_ms;  /* ms since that signal last updated; only
                                meaningful when cutoff_seen                */

    /* --- counters (console diagnostics) ------------------------------- */
    uint32_t frames;       /* frames fed in                               */
    uint32_t decoded;      /* frames that updated at least one signal     */
} can_tap_snap_t;

/* Create the lock and the decoder. Call once, before the RX task starts. */
void can_tap_init(void);

/* Feed one received frame. Safe from the CAN-RX hot path. */
void can_tap_feed(uint32_t can_id, const uint8_t *data, uint8_t dlc,
                  uint32_t now_ms);

/* Copy out a consistent view. now_ms is used for the staleness tests. */
void can_tap_snapshot(can_tap_snap_t *out, uint32_t now_ms);

#endif /* CAN_TAP_H */
