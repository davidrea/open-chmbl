/*
 * DE-08 — TWAI (CAN) listen-only reception + decoded-signal access.
 *
 * can_rx.c owns the TWAI driver (strictly listen-only, per the golden rule
 * in docs/can-profiles.md §1), the RX task that feeds frames into the pure
 * decoder core (can_decode.c), and the source-aware signal snapshot the
 * braking state machine (DE-09) and the `sig` CLI read.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "can_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     started;        /* TWAI driver installed and started */
    uint32_t bitrate;        /* configured bus bit rate */
    uint32_t frames_rx;      /* frames received off the bus */
    uint32_t frames_decoded; /* frames that updated at least one signal */
    uint32_t last_rx_ms;     /* timestamp of the most recent frame */
    uint32_t rx_missed;      /* frames dropped by the driver (queue full) */
    uint32_t bus_errors;     /* bus error counter from the controller */
} can_rx_stats_t;

/* Install + start the TWAI driver in listen-only mode at the profile's bit
 * rate and spawn the RX/decode task. Called unconditionally from app_main();
 * logs and degrades gracefully if the transceiver/bus is absent. */
void can_rx_init(void);

void can_rx_get_stats(can_rx_stats_t *out);
const bike_profile_t *can_rx_profile(void);

/* ---- decoded-signal access (source-aware) ------------------------------ */

typedef enum {
    SIG_SOURCE_CAN = 0, /* live decode from the bus */
    SIG_SOURCE_FAKE,    /* bench values injected via `sig set` */
} sig_source_t;

void         sig_set_source(sig_source_t src);
sig_source_t sig_get_source(void);

/* Copy the current signal set (per the active source) and the millisecond
 * clock used for validity checks (can_sig_valid()). */
void sig_snapshot(can_signals_t *out, uint32_t *now_ms);

/* Set a fake signal by name (see sig_names[] in can_rx.c); returns false if
 * the name is unknown. Fake signals are always valid once set. Setting
 * wheel_speed also drives the derived accel through the real smoothing
 * filter (unless accel has been explicitly overridden). */
bool sig_fake_set(const char *name, float value);

/* Mark a fake signal unavailable again (`sig set <name> na`). */
bool sig_fake_clear(const char *name);

/* NULL-terminated list of signal names, in can_signals_t order. */
extern const char *const sig_names[];

/* Per-name access into a can_signals_t (same order as sig_names). */
sig_value_t *sig_by_index(can_signals_t *sigs, int idx);

#ifdef __cplusplus
}
#endif
