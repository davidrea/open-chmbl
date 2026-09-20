/*
 * Host parity harness for DE-09: replay a PCAN .trc capture through the real
 * decoder core (can_decode.c) and the real braking state machine
 * (brake_fsm.c), and print what both produced.
 *
 * tools/fsm_check.py feeds the emitted series back through the reference
 * implementation in tools/trc_viz.py — the Python twin of the algorithm in
 * tools/trc_viz.html that the tunables were calibrated on — and asserts the
 * two agree tick for tick. That is what keeps the bench viewer and the
 * firmware the same machine: change one without the other and CI says so.
 *
 * Two row types on stdout:
 *
 *   S,<t_ms>,<wheel_speed_mph>
 *       one per decoded wheel-speed frame, as the firmware saw it (raw, i.e.
 *       before the low-pass inside the accel derivation).
 *
 *   T,<t_ms>,<speed>,<speed_valid>,<accel>,<accel_valid>,<clutch>,<gear>,<state>
 *       one per 50 Hz FSM tick: the inputs that tick saw and the state it
 *       produced. accel is 'nan' until the derivation primes.
 *
 * Time is relative to the first frame carrying a profile signal, matching the
 * reference, and is truncated to whole milliseconds because that is the
 * resolution the firmware's clock actually has.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bike_profiles.h"
#include "brake_fsm.h"
#include "can_decode.h"

/* Is this frame one the profile decodes anything from? Used only to find the
 * time origin (the reference starts its clock at the first such frame). */
static bool profile_has_id(const bike_profile_t *p, uint32_t id)
{
    const can_signal_t *sigs[] = {
        &p->wheel_speed, &p->wheel_speed_rear, &p->clutch_raw, &p->gear,
        &p->throttle_pct, &p->rpm, &p->rpm_ecu, &p->side_stand_up,
        &p->engine_cutoff_flag, &p->cutoff_reason,
    };
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        if (sigs[i]->can_id == id) {
            return true;
        }
    }
    return false;
}

static void emit_tick(uint32_t t_ms, const brake_fsm_in_t *in,
                      brake_fsm_state_t state)
{
    char accel[32];
    if (in->accel_valid) {
        snprintf(accel, sizeof(accel), "%.6f", (double)in->accel_mphps);
    } else {
        snprintf(accel, sizeof(accel), "nan");
    }
    printf("T,%lu,%.6f,%d,%s,%d,%d,%u,%d\n", (unsigned long)t_ms,
           (double)in->speed_mph, in->speed_valid ? 1 : 0, accel,
           in->accel_valid ? 1 : 0, in->clutch_pulled ? 1 : 0, in->gear,
           (int)state);
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

    brake_fsm_t fsm;
    const brake_tunables_t tun = BRAKE_TUNABLES_DEFAULT;
    brake_fsm_init(&fsm, &tun);
    can_decode_set_speed_smoothing(&dec, tun.speed_smooth_ms);

    char line[512];
    bool     have_t0 = false;
    double   t0_ms = 0.0;
    uint32_t grid_ms = 0;
    unsigned frames = 0, ticks = 0, on_ticks = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ';') {
            continue;   /* header/comment */
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

        if (!have_t0) {
            if (!profile_has_id(p, id)) {
                continue;
            }
            have_t0 = true;
            t0_ms = t_ms;
        }
        const double rel = t_ms - t0_ms;
        if (rel < 0.0) {
            continue;   /* out-of-order stamp; the firmware's clock can't go back */
        }
        const uint32_t now_ms = (uint32_t)rel;

        /* Tick the FSM at every 20 ms grid point strictly before this frame,
         * so a tick sees exactly the frames stamped at or before it. */
        while (grid_ms < now_ms) {
            brake_fsm_in_t in = {
                .speed_mph     = dec.sig.wheel_speed.value,
                .speed_valid   = can_sig_valid(&dec.sig.wheel_speed, grid_ms),
                .accel_mphps   = dec.sig.accel.value,
                .accel_valid   = can_sig_valid(&dec.sig.accel, grid_ms),
                .clutch_pulled = can_sig_valid(&dec.sig.clutch_pulled, grid_ms) &&
                                 dec.sig.clutch_pulled.value != 0.0f,
                .gear = can_sig_valid(&dec.sig.gear, grid_ms)
                            ? (uint8_t)(dec.sig.gear.value + 0.5f) : 0u,
            };
            const brake_fsm_state_t st =
                brake_fsm_step(&fsm, &in, BRAKE_FSM_TICK_MS);
            emit_tick(grid_ms, &in, st);
            ticks++;
            if (brake_fsm_light_on(st)) {
                on_ticks++;
            }
            grid_ms += BRAKE_FSM_TICK_MS;
        }

        frames++;

        /* Emit the raw wheel-speed sample the derivation is about to consume.
         * Decoded through the profile the same way can_decode_feed() does it,
         * rather than read back afterwards, so a frame that fails to decode
         * produces no row instead of repeating the previous value. */
        float raw;
        if (id == p->wheel_speed.can_id &&
            can_sig_decode(&p->wheel_speed, data, dlc, &raw)) {
            printf("S,%lu,%.6f\n", (unsigned long)now_ms,
                   (double)(p->wheel_speed_kmh ? raw * KMH_TO_MPH : raw));
        }
        can_decode_feed(&dec, id, data, dlc, now_ms);
    }
    fclose(f);

    fprintf(stderr,
            "frames=%u ticks=%u transitions=%lu light_on=%.1f s (%.1f%% of trace)\n",
            frames, ticks, (unsigned long)fsm.transitions,
            on_ticks * (BRAKE_FSM_TICK_MS / 1000.0),
            ticks ? (100.0 * on_ticks / ticks) : 0.0);
    return 0;
}
