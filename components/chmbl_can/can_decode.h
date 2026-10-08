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

/* Acceleration derivation: low-pass the wheel speed, take the slope over at
 * least ACCEL_WINDOW_MS, then exponentially smooth the slope (ALPHA = new-sample
 * weight). */
#define CAN_DECODE_ACCEL_WINDOW_MS 200u
#define CAN_DECODE_ACCEL_ALPHA     0.3f

/* Depth of the wheel-speed history ring the slope is taken across.
 *
 * SIZING RULE — do not shrink this without redoing the arithmetic:
 *
 *     CAN_DECODE_SPEED_HIST  >  CAN_DECODE_ACCEL_WINDOW_MS x frame_rate_hz
 *
 * accel_update() looks for the newest ring entry at least ACCEL_WINDOW_MS old.
 * If the ring cannot hold that much history the search fails on every sample and
 * the derived accel FREEZES at its last value, only ever updating across a rare
 * frame gap — a silent failure, since the signal still looks plausible.
 *
 * The reference bus emits wheel speed (0x102) at ~100 Hz, so a 200 ms window
 * needs > 20 samples. 32 spans ~320 ms at 100 Hz, which leaves 60% margin for a
 * faster bus or a burstier scheduler. This was 16 (~150 ms) and was exactly the
 * bug above — see docs/design/de-08-can-decode.md and the notes at the top of
 * tools/trc_viz.py. */
#define CAN_DECODE_SPEED_HIST      32u

/* Single-pole low-pass applied to wheel speed BEFORE the slope is taken, as a
 * time constant in ms (<= 0 disables it). Wheel speed is quantized to
 * 0.0625 km/h (~0.039 mph) and the odd single-sample glitch gets through; both
 * inject decel spikes into a 200 ms slope that the FSM would read as braking.
 *
 * This matches _smooth_speed() in tools/trc_viz.py, the tuned reference the
 * DE-09 thresholds were calibrated against. It affects ONLY the derived accel
 * path: sig.wheel_speed still carries the raw decoded sample, so the DE-08
 * golden test against cantools is unaffected.
 *
 * Per-instance (can_decode_t.speed_smooth_ms) so an app can retune it from
 * Kconfig — see CONFIG_BRAKE_FSM_SPEED_SMOOTH_MS. */
#define CAN_DECODE_SPEED_SMOOTH_MS 80.0f

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

    /* wheel-speed history ring for the accel slope. Holds the LOW-PASSED
     * samples (see CAN_DECODE_SPEED_SMOOTH_MS), not the raw ones. */
    float    spd_v[CAN_DECODE_SPEED_HIST];
    uint32_t spd_t[CAN_DECODE_SPEED_HIST];
    uint8_t  spd_head;
    uint8_t  spd_count;
    bool     accel_primed;

    /* wheel-speed low-pass state (accel path only) */
    float    speed_smooth_ms; /* time constant; <= 0 disables the filter */
    float    spd_lpf;         /* filter output                          */
    uint32_t spd_lpf_ms;      /* timestamp of the last filtered sample  */
    bool     spd_lpf_primed;
} can_decode_t;

void can_decode_init(can_decode_t *d, const bike_profile_t *profile);

/* Retune the wheel-speed low-pass time constant (ms; <= 0 disables it). Call
 * right after can_decode_init(), before feeding frames — changing it mid-stream
 * steps the filter output and therefore the derived accel. */
void can_decode_set_speed_smooth_ms(can_decode_t *d, float tau_ms);

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
