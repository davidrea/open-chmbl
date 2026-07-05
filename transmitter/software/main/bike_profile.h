/*
 * Bike CAN profile — plain data describing where each signal lives on a
 * specific bike's bus (see docs/can-profiles.md §4).
 *
 * A profile is const data, not code: adding a bike means authoring its DBC
 * under profiles/ and regenerating with tools/gen_profile.py. The generic
 * extractor in can_decode.c interprets these descriptors at runtime.
 *
 * Pure C, no ESP-IDF dependencies — compiles on host for the golden tests.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAN_SIG_LE = 0, /* Intel: bit_start is the LSB position          */
    CAN_SIG_BE = 1, /* Motorola: bit_start is the MSB position, DBC
                       "sawtooth" numbering (bit 7 of byte 0 == 7)   */
} can_sig_byte_order_t;

typedef struct {
    uint32_t can_id;     /* frame ID carrying this signal; 0 = signal absent */
    uint8_t  bit_start;  /* DBC start bit (LSB for LE, MSB for BE)           */
    uint8_t  bit_len;    /* field width in bits (1..32)                      */
    uint8_t  byte_order; /* can_sig_byte_order_t                             */
    uint8_t  is_signed;  /* two's-complement sign extension when set         */
    float    scale;      /* raw -> engineering units                         */
    float    offset;     /* raw -> engineering units                         */
} can_signal_t;

typedef struct {
    const char  *name;    /* e.g. "Triumph Speed 400 / Scrambler 400X (TR-series)" */
    uint32_t     bitrate; /* bus bit rate, e.g. 500000 */

    /* Signals consumed by the braking FSM (DE-09). wheel_speed is REQUIRED. */
    can_signal_t wheel_speed;      /* front wheel; primary braking input */
    uint8_t      wheel_speed_kmh;  /* 1 if the DBC decodes to km/h (converted
                                      to mph by the decoder; the FSM works
                                      in mph) */
    can_signal_t clutch_raw;       /* clutch_pulled = (raw != 0) */
    can_signal_t gear;             /* gear position; 0 = neutral */

    /* Diagnostics / telemetry. .can_id = 0 if unavailable on this bike. */
    can_signal_t wheel_speed_rear;
    can_signal_t throttle_pct;
    can_signal_t rpm;              /* live tach; 0 = engine off */
    can_signal_t rpm_ecu;          /* ECU filtered/target rpm */
    can_signal_t side_stand_up;    /* 1 = stand up */
    can_signal_t engine_cutoff_flag;
    can_signal_t cutoff_reason;    /* engine_cutoff = flag && (reason ==
                                      cutoff_reason_value) */
    uint8_t      cutoff_reason_value;
} bike_profile_t;

#ifdef __cplusplus
}
#endif
