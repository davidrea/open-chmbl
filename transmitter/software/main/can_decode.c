/* DE-08 — profile-based CAN decode core. Pure C, host-testable. */

#include "can_decode.h"

#include <string.h>

/* ---- bit-field extraction ---------------------------------------------- */

static bool bit_at(const uint8_t *data, uint8_t dlc, uint16_t pos, uint8_t *out)
{
    uint8_t byte = pos / 8u;
    if (byte >= dlc) {
        return false;
    }
    *out = (data[byte] >> (pos % 8u)) & 1u;
    return true;
}

bool can_sig_extract(const can_signal_t *s, const uint8_t *data, uint8_t dlc,
                     uint32_t *raw_out)
{
    uint32_t raw = 0;
    uint8_t bit;

    if (s->bit_len == 0 || s->bit_len > 32) {
        return false;
    }

    if (s->byte_order == CAN_SIG_LE) {
        /* Intel: bit_start is the LSB position; bits count upward. */
        for (uint8_t i = 0; i < s->bit_len; i++) {
            if (!bit_at(data, dlc, (uint16_t)s->bit_start + i, &bit)) {
                return false;
            }
            raw |= (uint32_t)bit << i;
        }
    } else {
        /* Motorola: bit_start is the MSB position in DBC "sawtooth"
         * numbering — within a byte positions run 7..0, then continue at
         * bit 7 of the next byte. */
        uint16_t pos = s->bit_start;
        for (uint8_t i = 0; i < s->bit_len; i++) {
            if (!bit_at(data, dlc, pos, &bit)) {
                return false;
            }
            raw = (raw << 1) | bit;
            pos = (pos % 8u == 0u) ? pos + 15u : pos - 1u;
        }
    }

    *raw_out = raw;
    return true;
}

bool can_sig_decode(const can_signal_t *s, const uint8_t *data, uint8_t dlc,
                    float *value_out)
{
    uint32_t raw;

    if (s->can_id == 0 || !can_sig_extract(s, data, dlc, &raw)) {
        return false;
    }

    if (s->is_signed && s->bit_len < 32 &&
        (raw & (1u << (s->bit_len - 1u)))) {
        raw |= ~((1u << s->bit_len) - 1u); /* sign-extend */
        *value_out = (float)(int32_t)raw * s->scale + s->offset;
    } else {
        *value_out = (float)raw * s->scale + s->offset;
    }
    return true;
}

static bool bit_set(uint8_t *data, uint8_t dlc, uint16_t pos, uint8_t bit)
{
    uint8_t byte = pos / 8u;
    if (byte >= dlc) {
        return false;
    }
    data[byte] = (uint8_t)((data[byte] & ~(1u << (pos % 8u))) |
                           ((bit & 1u) << (pos % 8u)));
    return true;
}

bool can_sig_pack(const can_signal_t *s, uint8_t *data, uint8_t dlc,
                  uint32_t raw)
{
    if (s->bit_len == 0 || s->bit_len > 32) {
        return false;
    }

    if (s->byte_order == CAN_SIG_LE) {
        for (uint8_t i = 0; i < s->bit_len; i++) {
            if (!bit_set(data, dlc, (uint16_t)s->bit_start + i,
                         (uint8_t)(raw >> i))) {
                return false;
            }
        }
    } else {
        uint16_t pos = s->bit_start;
        for (uint8_t i = 0; i < s->bit_len; i++) {
            if (!bit_set(data, dlc, pos,
                         (uint8_t)(raw >> (s->bit_len - 1u - i)))) {
                return false;
            }
            pos = (pos % 8u == 0u) ? pos + 15u : pos - 1u;
        }
    }
    return true;
}

/* ---- signal bookkeeping ------------------------------------------------ */

static void sig_update(sig_value_t *s, float value, uint32_t now_ms)
{
    s->value = value;
    s->seen = true;
    s->last_ms = now_ms;
}

bool can_sig_valid(const sig_value_t *s, uint32_t now_ms)
{
    return s->seen && (now_ms - s->last_ms) <= CAN_DECODE_STALE_MS;
}

/* ---- derived acceleration ---------------------------------------------- */

static void accel_update(can_decode_t *d, float speed_mph, uint32_t now_ms)
{
    /* push into the history ring */
    d->spd_v[d->spd_head] = speed_mph;
    d->spd_t[d->spd_head] = now_ms;
    d->spd_head = (uint8_t)((d->spd_head + 1u) % CAN_DECODE_SPEED_HIST);
    if (d->spd_count < CAN_DECODE_SPEED_HIST) {
        d->spd_count++;
    }

    /* slope against the oldest sample at least ACCEL_WINDOW_MS back */
    uint8_t best = CAN_DECODE_SPEED_HIST; /* invalid */
    for (uint8_t i = 0; i < d->spd_count; i++) {
        uint8_t idx = (uint8_t)((d->spd_head + CAN_DECODE_SPEED_HIST - 1u - i) %
                                CAN_DECODE_SPEED_HIST);
        if (now_ms - d->spd_t[idx] >= CAN_DECODE_ACCEL_WINDOW_MS) {
            best = idx;
            break;
        }
    }
    if (best == CAN_DECODE_SPEED_HIST) {
        return; /* window not yet spanned — keep previous accel */
    }

    float dt_s = (float)(now_ms - d->spd_t[best]) / 1000.0f;
    if (dt_s <= 0.0f) {
        return;
    }
    float slope = (speed_mph - d->spd_v[best]) / dt_s;

    if (!d->accel_primed) {
        d->sig.accel.value = slope;
        d->accel_primed = true;
    } else {
        d->sig.accel.value += CAN_DECODE_ACCEL_ALPHA *
                              (slope - d->sig.accel.value);
    }
    d->sig.accel.seen = true;
    d->sig.accel.last_ms = now_ms;
}

void can_decode_accel_feed(can_decode_t *d, float speed_mph, uint32_t now_ms)
{
    accel_update(d, speed_mph, now_ms);
}

/* ---- public API -------------------------------------------------------- */

void can_decode_init(can_decode_t *d, const bike_profile_t *profile)
{
    memset(d, 0, sizeof(*d));
    d->profile = profile;
}

bool can_decode_feed(can_decode_t *d, uint32_t can_id, const uint8_t *data,
                     uint8_t dlc, uint32_t now_ms)
{
    const bike_profile_t *p = d->profile;
    float v;
    bool updated = false;

    if (p->wheel_speed.can_id == can_id &&
        can_sig_decode(&p->wheel_speed, data, dlc, &v)) {
        float mph = p->wheel_speed_kmh ? v * KMH_TO_MPH : v;
        sig_update(&d->sig.wheel_speed, mph, now_ms);
        accel_update(d, mph, now_ms);
        updated = true;
    }
    if (p->wheel_speed_rear.can_id == can_id &&
        can_sig_decode(&p->wheel_speed_rear, data, dlc, &v)) {
        sig_update(&d->sig.wheel_speed_rear,
                   p->wheel_speed_kmh ? v * KMH_TO_MPH : v, now_ms);
        updated = true;
    }
    if (p->clutch_raw.can_id == can_id &&
        can_sig_decode(&p->clutch_raw, data, dlc, &v)) {
        sig_update(&d->sig.clutch_pulled, (v != 0.0f) ? 1.0f : 0.0f, now_ms);
        updated = true;
    }
    if (p->gear.can_id == can_id &&
        can_sig_decode(&p->gear, data, dlc, &v)) {
        sig_update(&d->sig.gear, v, now_ms);
        updated = true;
    }
    if (p->throttle_pct.can_id == can_id &&
        can_sig_decode(&p->throttle_pct, data, dlc, &v)) {
        sig_update(&d->sig.throttle_pct, v, now_ms);
        updated = true;
    }
    if (p->rpm.can_id == can_id &&
        can_sig_decode(&p->rpm, data, dlc, &v)) {
        sig_update(&d->sig.rpm, v, now_ms);
        updated = true;
    }
    if (p->rpm_ecu.can_id == can_id &&
        can_sig_decode(&p->rpm_ecu, data, dlc, &v)) {
        sig_update(&d->sig.rpm_ecu, v, now_ms);
        updated = true;
    }
    if (p->side_stand_up.can_id == can_id &&
        can_sig_decode(&p->side_stand_up, data, dlc, &v)) {
        sig_update(&d->sig.side_stand_up, v, now_ms);
        updated = true;
    }
    if (p->engine_cutoff_flag.can_id == can_id &&
        can_sig_decode(&p->engine_cutoff_flag, data, dlc, &v)) {
        float reason;
        bool cutoff = (v != 0.0f);
        if (cutoff && p->cutoff_reason.can_id != 0) {
            cutoff = can_sig_decode(&p->cutoff_reason, data, dlc, &reason) &&
                     (uint8_t)reason == p->cutoff_reason_value;
        }
        sig_update(&d->sig.engine_cutoff, cutoff ? 1.0f : 0.0f, now_ms);
        updated = true;
    }

    return updated;
}
