/*
 * DE-08 — profile-based CAN decode (pure, host-testable core).
 *
 * Turns raw CAN frames into the engineering-unit signals the braking state
 * machine (DE-09) consumes, driven entirely by a bike_profile_t data table.
 * No ESP-IDF dependencies: the TWAI RX task (can_rx.c) feeds frames in on
 * target; the host golden test feeds the same frames from a .trc capture.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bike_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A signal is invalid until first seen, and goes stale (invalid again) when
 * no frame carrying it arrives for this long. */
#define CAN_DECODE_STALE_MS 1000u

/* Acceleration derivation: slope of wheel speed over at least this window,
 * then exponentially smoothed (alpha = new-sample weight). */
#define CAN_DECODE_ACCEL_WINDOW_MS 200u
#define CAN_DECODE_ACCEL_ALPHA     0.3f
#define CAN_DECODE_SPEED_HIST      16u

#define KMH_TO_MPH 0.621371f

typedef struct {
    float    value;
    bool     seen;    /* received at least once since init */
    uint32_t last_ms; /* timestamp of the most recent update */
} sig_value_t;

typedef struct {
    sig_value_t wheel_speed;      /* mph — REQUIRED, primary braking input */
    sig_value_t accel;            /* mph/s, derived + smoothed; follows
                                     wheel_speed validity */
    sig_value_t clutch_pulled;    /* 1.0 pulled / 0.0 released */
    sig_value_t gear;             /* 0 = neutral */
    sig_value_t wheel_speed_rear; /* mph */
    sig_value_t throttle_pct;     /* 0..100 */
    sig_value_t rpm;              /* live tach; 0 = engine off */
    sig_value_t rpm_ecu;          /* ECU filtered/target rpm */
    sig_value_t side_stand_up;    /* 1 = stand up */
    sig_value_t engine_cutoff;    /* 1 = kill asserted */
} can_signals_t;

typedef struct {
    const bike_profile_t *profile;
    can_signals_t sig;

    /* wheel-speed history ring for the accel slope */
    float    spd_v[CAN_DECODE_SPEED_HIST];
    uint32_t spd_t[CAN_DECODE_SPEED_HIST];
    uint8_t  spd_head;
    uint8_t  spd_count;
    bool     accel_primed;
} can_decode_t;

void can_decode_init(can_decode_t *d, const bike_profile_t *profile);

/* Feed one received frame. Returns true if any profile signal was updated.
 * now_ms is a monotonic millisecond clock (wraparound-safe). */
bool can_decode_feed(can_decode_t *d, uint32_t can_id, const uint8_t *data,
                     uint8_t dlc, uint32_t now_ms);

/* Signal validity: seen and not stale as of now_ms. */
bool can_sig_valid(const sig_value_t *s, uint32_t now_ms);

/* Raw bit-field extraction per the profile descriptor (exposed for the host
 * golden test). Returns false if the field does not fit within dlc bytes. */
bool can_sig_extract(const can_signal_t *s, const uint8_t *data, uint8_t dlc,
                     uint32_t *raw_out);

/* Extracted + scaled engineering value (raw * scale + offset, after optional
 * sign extension). Returns false if absent (.can_id == 0) or out of range. */
bool can_sig_decode(const can_signal_t *s, const uint8_t *data, uint8_t dlc,
                    float *value_out);

/* Inverse of can_sig_extract: write a raw value into a frame buffer per the
 * descriptor (used by `can replay` to synthesize test frames). */
bool can_sig_pack(const can_signal_t *s, uint8_t *data, uint8_t dlc,
                  uint32_t raw);

/* Feed a wheel-speed sample (mph) into the derived-accel filter only —
 * lets `sig set wheel`/`sig ramp` exercise the same smoothing the live
 * decode uses. Updates d->sig.accel. */
void can_decode_accel_feed(can_decode_t *d, float speed_mph, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
