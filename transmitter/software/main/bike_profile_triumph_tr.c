/*
 * GENERATED FILE — do not edit by hand.
 * Source of truth: profiles/triumph_tr.dbc
 * Regenerate: python3 tools/gen_profile.py profiles/triumph_tr.dbc \
 *     --name "Triumph Speed 400 / Scrambler 400X (TR-series)" --bitrate 500000 \
 *     --symbol bike_profile_triumph_tr --out <this file>
 */
#include "bike_profile.h"

const bike_profile_t bike_profile_triumph_tr = {
    .name = "Triumph Speed 400 / Scrambler 400X (TR-series)",
    .bitrate = 500000,
    .wheel_speed_kmh = 1,
    .cutoff_reason_value = 0x28,
    .wheel_speed = { .can_id = 0x102, .bit_start = 15, .bit_len = 16, .byte_order = CAN_SIG_BE, .is_signed = 0, .scale = 0.0625f, .offset = 0.0f },
    .wheel_speed_rear = { .can_id = 0x102, .bit_start = 31, .bit_len = 16, .byte_order = CAN_SIG_BE, .is_signed = 0, .scale = 0.0625f, .offset = 0.0f },
    .clutch_raw = { .can_id = 0x142, .bit_start = 40, .bit_len = 4, .byte_order = CAN_SIG_LE, .is_signed = 0, .scale = 1.0f, .offset = 0.0f },
    .gear = { .can_id = 0x142, .bit_start = 24, .bit_len = 4, .byte_order = CAN_SIG_LE, .is_signed = 0, .scale = 1.0f, .offset = 0.0f },
    .throttle_pct = { .can_id = 0x140, .bit_start = 0, .bit_len = 8, .byte_order = CAN_SIG_LE, .is_signed = 0, .scale = 0.392157f, .offset = 0.0f },
    .rpm = { .can_id = 0x140, .bit_start = 48, .bit_len = 8, .byte_order = CAN_SIG_LE, .is_signed = 0, .scale = 31.4f, .offset = 0.0f },
    .rpm_ecu = { .can_id = 0x146, .bit_start = 23, .bit_len = 16, .byte_order = CAN_SIG_BE, .is_signed = 0, .scale = 0.25f, .offset = 0.0f },
    .side_stand_up = { .can_id = 0x481, .bit_start = 56, .bit_len = 1, .byte_order = CAN_SIG_LE, .is_signed = 0, .scale = 1.0f, .offset = 0.0f },
    .engine_cutoff_flag = { .can_id = 0x121, .bit_start = 30, .bit_len = 1, .byte_order = CAN_SIG_LE, .is_signed = 0, .scale = 1.0f, .offset = 0.0f },
    .cutoff_reason = { .can_id = 0x121, .bit_start = 48, .bit_len = 8, .byte_order = CAN_SIG_LE, .is_signed = 0, .scale = 1.0f, .offset = 0.0f },
};
