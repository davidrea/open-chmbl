/*
 * Ride-logging gate — when to record, from the engine cutoff (kill) switch.
 *
 * Pure C, no ESP-IDF headers, so it is unit-tested on the host
 * (test_host/log_gate_test.c). trc_log.c feeds it one observation of the
 * decoded `engine_cutoff` signal per evaluation and acts on the event it
 * returns; every recording decision lives here, the plumbing lives there.
 *
 * Policy:
 *   - A capture SESSION starts whenever the switch is seen in RUN while no
 *     session is active. That covers both cases the rider cares about: the
 *     bike switched on with the switch already in RUN (first RUN seen after
 *     boot, or after the bus comes back), and a CUTOFF -> RUN transition.
 *     Each session start asks for a new file.
 *   - A session ends the moment the switch is seen in CUTOFF, or when the
 *     signal has gone unseen for longer than close_ms — no 0x121 frames,
 *     which is what key-off looks like.
 *   - Between stale_ms and close_ms an active session is held: a short bus
 *     dropout keeps writing to the same file rather than splitting the ride.
 *     A session never STARTS on a stale observation.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t stale_ms; /* an observation older than this is not trusted
                          (CAN_DECODE_STALE_MS) */
    uint32_t close_ms; /* an active session ends after this long with no
                          observation; must exceed stale_ms */
} log_gate_cfg_t;

/* One look at the decoded engine_cutoff signal. */
typedef struct {
    bool     seen;   /* received at least once since boot */
    bool     cutoff; /* true = kill asserted (STOP), false = RUN */
    uint32_t age_ms; /* time since the most recent update */
} log_gate_obs_t;

typedef enum {
    LOG_GATE_NONE = 0,
    LOG_GATE_START,        /* session started: open a new file */
    LOG_GATE_STOP_CUTOFF,  /* session ended: switch moved to CUTOFF */
    LOG_GATE_STOP_SILENT,  /* session ended: signal unseen > close_ms */
} log_gate_event_t;

typedef struct {
    bool     active;   /* a RUN session is in progress */
    uint32_t sessions; /* sessions started since init */
} log_gate_t;

void log_gate_init(log_gate_t *g);

/* Evaluate one observation. Returns the transition it caused, if any; at most
 * one per call. */
log_gate_event_t log_gate_update(log_gate_t *g, const log_gate_obs_t *obs,
                                 const log_gate_cfg_t *cfg);

const char *log_gate_event_name(log_gate_event_t ev);

#ifdef __cplusplus
}
#endif
