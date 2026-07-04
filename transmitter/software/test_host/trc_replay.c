/*
 * Host golden-test harness: replay a PCAN .trc capture through the DE-08
 * decoder core and print one CSV row per decoded signal:
 *
 *     frame_index,can_id,signal,value
 *
 * tools/golden_check.py decodes the same capture with python-cantools
 * against the profile DBC and asserts the outputs agree — transferring
 * cantools' bit-unpacking correctness onto the C extractor.
 *
 * Signal rows use the DBC signal names; two extra derived rows
 * (clutch_pulled, engine_cutoff) expose the C-side predicates.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bike_profiles.h"
#include "can_decode.h"

static void emit(unsigned frame, uint32_t id, const char *name, float value)
{
    printf("%u,0x%03X,%s,%.6f\n", frame, (unsigned)id, name, (double)value);
}

static void emit_sig(unsigned frame, uint32_t id, const char *name,
                     const can_signal_t *s, const uint8_t *data, uint8_t dlc)
{
    float v;
    if (s->can_id == id && can_sig_decode(s, data, dlc, &v)) {
        emit(frame, id, name, v);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <capture.trc>\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        perror(argv[1]);
        return 2;
    }

    const bike_profile_t *p = BIKE_PROFILE_DEFAULT;
    can_decode_t dec;
    can_decode_init(&dec, p);

    char line[512];
    unsigned frame = 0;
    float accel_min = 0.0f, accel_max = 0.0f;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ';') {
            continue; /* header/comment */
        }
        unsigned msgnum, bus, dlc_u;
        double t_ms;
        uint32_t id;
        int consumed;
        if (sscanf(line, " %u %lf DT %u %x Rx - %u%n",
                   &msgnum, &t_ms, &bus, &id, &dlc_u, &consumed) != 5) {
            continue;
        }
        uint8_t data[8] = {0};
        uint8_t dlc = (uint8_t)(dlc_u > 8 ? 8 : dlc_u);
        const char *pos = line + consumed;
        for (uint8_t i = 0; i < dlc; i++) {
            unsigned byte;
            int n;
            if (sscanf(pos, " %2x%n", &byte, &n) != 1) {
                dlc = i;
                break;
            }
            data[i] = (uint8_t)byte;
            pos += n;
        }

        frame++;
        uint32_t now_ms = (uint32_t)t_ms;
        bool updated = can_decode_feed(&dec, id, data, dlc, now_ms);

        /* per-signal engineering values, straight from the extractor,
         * in DBC units (wheel speed in km/h, matching cantools) */
        emit_sig(frame, id, "WHEEL_SPEED_FRONT", &p->wheel_speed, data, dlc);
        emit_sig(frame, id, "WHEEL_SPEED_REAR", &p->wheel_speed_rear, data, dlc);
        emit_sig(frame, id, "CLUTCH_RAW", &p->clutch_raw, data, dlc);
        emit_sig(frame, id, "GEAR", &p->gear, data, dlc);
        emit_sig(frame, id, "THROTTLE_PCT", &p->throttle_pct, data, dlc);
        emit_sig(frame, id, "RPM_LIVE", &p->rpm, data, dlc);
        emit_sig(frame, id, "RPM_ECU", &p->rpm_ecu, data, dlc);
        emit_sig(frame, id, "SIDE_STAND_UP", &p->side_stand_up, data, dlc);
        emit_sig(frame, id, "ENGINE_CUTOFF_FLAG", &p->engine_cutoff_flag, data, dlc);
        emit_sig(frame, id, "CUTOFF_REASON", &p->cutoff_reason, data, dlc);

        /* derived predicates from the decode state */
        if (updated && id == p->clutch_raw.can_id) {
            emit(frame, id, "clutch_pulled", dec.sig.clutch_pulled.value);
        }
        if (updated && id == p->engine_cutoff_flag.can_id) {
            emit(frame, id, "engine_cutoff", dec.sig.engine_cutoff.value);
        }
        if (dec.sig.accel.seen) {
            if (dec.sig.accel.value < accel_min) accel_min = dec.sig.accel.value;
            if (dec.sig.accel.value > accel_max) accel_max = dec.sig.accel.value;
        }
    }
    fclose(f);

    fprintf(stderr, "frames=%u accel_mphps_min=%.2f accel_mphps_max=%.2f "
            "wheel_speed_mph_last=%.2f\n",
            frame, (double)accel_min, (double)accel_max,
            (double)dec.sig.wheel_speed.value);
    return 0;
}
