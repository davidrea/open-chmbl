/*
 * DE-09 host replay harness: push a PCAN .trc capture through the REAL firmware
 * decode (components/chmbl_can/can_decode.c) and the REAL firmware state
 * machine (components/brake_fsm/brake_fsm.c) on the 50 Hz grid the firmware
 * ticks, and report what the brake light would have done.
 *
 * tools/fsm_check.py runs this against the tuned Python reference
 * (run_fsm() in tools/trc_viz.py) over the same capture and asserts the two
 * agree. That is the only automated proof that the C port of the FSM still
 * behaves like the implementation the thresholds were calibrated on.
 *
 * Deliberately NOT a re-implementation of either side: the only logic in this
 * file is the .trc parser, the grid clock, and the signals -> brake_fsm_input_t
 * mapping that logger/software/main/fsm_preview.c performs on target.
 *
 * Grid semantics, matched to the reference's zero-order-hold resampling:
 * at grid instant G every frame with timestamp <= G has been fed, and the FSM
 * then reads the decoder's current values. So the loop steps every grid point
 * strictly before a frame's timestamp, feeds the frame, and repeats.
 *
 * Output: `key=value` lines on stdout (parsed by fsm_check.py); the transition
 * log and progress on stderr.
 *
 *     usage: fsm_replay <capture.trc> [--quiet]
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bike_profiles.h"
#include "brake_fsm.h"
#include "can_decode.h"

/* Grid step in ms. Integer so grid instants are exact; the FSM is still handed
 * BRAKE_FSM_TICK_MS as dt, as the firmware does. */
#define GRID_MS 20u

/* Parse one .trc v2.1 data line. Same scanf shape as trc_replay.c. */
static bool trc_parse(const char *line, double *t_ms_out, uint32_t *id_out,
                      uint8_t *data, uint8_t *dlc_out)
{
    unsigned msgnum, bus, dlc_u;
    double t_ms;
    uint32_t id;
    int consumed;

    if (line[0] == ';') {
        return false;   /* header / comment */
    }
    if (sscanf(line, " %u %lf DT %u %x Rx - %u%n",
               &msgnum, &t_ms, &bus, &id, &dlc_u, &consumed) != 5) {
        return false;
    }

    uint8_t dlc = (uint8_t)(dlc_u > 8 ? 8 : dlc_u);
    const char *pos = line + consumed;
    memset(data, 0, 8);
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

    *t_ms_out = t_ms;
    *id_out = id;
    *dlc_out = dlc;
    return true;
}

/* Is this frame one the profile decodes? Used both to pick the time origin and
 * to bound the replay, mirroring the reference's "ids = DBC frame ids" filter. */
static bool profile_carries(const bike_profile_t *p, uint32_t id)
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

/* ---- replay state -------------------------------------------------------- */

typedef struct {
    can_decode_t dec;
    brake_fsm_t  fsm;

    uint32_t grid_i;        /* next grid point to step              */
    uint32_t grid_n;        /* total grid points (set after pass 1)  */
    uint32_t on_ticks;      /* grid points with the light on         */

    /* Two transition counts, because they are not the same number and the
     * comparison against the reference needs the right one:
     *
     *  grid_transitions - changes between CONSECUTIVE SAMPLED grid states.
     *      This is what fsm_stats() in tools/trc_viz.py counts (np.diff over
     *      the output series), so it is the comparable metric. A transition
     *      that happens before the first grid state is recorded — the
     *      OFF -> STOPPED at t=0 every capture starts with, since speed reads
     *      0 before the first wheel-speed frame — is invisible to it.
     *  fsm_transitions - every change brake_fsm_step() actually reports, which
     *      is what the firmware's console log prints. Normally exactly one more
     *      than grid_transitions, for that t=0 entry. */
    uint32_t grid_transitions;
    uint32_t fsm_transitions;

    uint32_t accel_invalid_ticks;
    bool     have_prev_sample;
    brake_fsm_state_t prev_sample;
    bool     quiet;

    float    accel_min, accel_max, speed_max;
} replay_t;

static void grid_step(replay_t *r)
{
    const uint32_t now_ms = r->grid_i * GRID_MS;
    const can_signals_t *s = &r->dec.sig;

    /* The same mapping fsm_preview.c does on target. Values only: validity is
     * the caller's business (brake_fsm.h), and this harness deliberately does
     * NOT apply the firmware's wheel-speed-validity hold so that it reproduces
     * the reference exactly — see fsm_check.py. */
    const brake_fsm_input_t in = {
        .speed_mph     = s->wheel_speed.value,
        .accel_mphps   = s->accel.value,
        .accel_valid   = can_sig_valid(&s->accel, now_ms),
        .clutch_pulled = (s->clutch_pulled.value != 0.0f),
        .in_gear       = (s->gear.value != 0.0f),
    };

    if (in.accel_valid) {
        if (in.accel_mphps < r->accel_min) r->accel_min = in.accel_mphps;
        if (in.accel_mphps > r->accel_max) r->accel_max = in.accel_mphps;
    } else {
        r->accel_invalid_ticks++;
    }
    if (in.speed_mph > r->speed_max) r->speed_max = in.speed_mph;

    const brake_fsm_state_t prev = r->fsm.state;
    if (brake_fsm_step(&r->fsm, &in, BRAKE_FSM_TICK_MS)) {
        r->fsm_transitions++;
        if (!r->quiet) {
            char accel_str[16];
            if (in.accel_valid) {
                snprintf(accel_str, sizeof(accel_str), "%+6.2f", (double)in.accel_mphps);
            } else {
                snprintf(accel_str, sizeof(accel_str), "%6s", "n/a");
            }
            fprintf(stderr,
                    "%9.3fs  %-7s -> %-7s  rule %-2s  "
                    "speed=%6.2f mph  accel=%s mph/s  clutch=%u  gear=%u\n",
                    (double)now_ms / 1000.0,
                    brake_fsm_state_name(prev),
                    brake_fsm_state_name(r->fsm.state),
                    brake_fsm_rule_name(r->fsm.last_rule),
                    (double)in.speed_mph, accel_str,
                    in.clutch_pulled ? 1u : 0u,
                    (unsigned)r->dec.sig.gear.value);
        }
    }

    if (r->have_prev_sample && r->fsm.state != r->prev_sample) {
        r->grid_transitions++;
    }
    r->prev_sample = r->fsm.state;
    r->have_prev_sample = true;

    if (brake_fsm_light_on(&r->fsm)) {
        r->on_ticks++;
    }
    r->grid_i++;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    bool quiet = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "usage: %s <capture.trc> [--quiet]\n", argv[0]);
            return 2;
        }
    }
    if (path == NULL) {
        fprintf(stderr, "usage: %s <capture.trc> [--quiet]\n", argv[0]);
        return 2;
    }

    const bike_profile_t *p = BIKE_PROFILE_DEFAULT;

    /* ---- pass 1: time origin and duration ------------------------------
     * The reference sets t0 from the first frame the DBC knows and takes the
     * capture duration from the last such frame, so the grid length matches.
     * Two passes rather than buffering 60 MB of frames. */
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        perror(path);
        return 2;
    }

    char line[512];
    double t_first = 0.0, t_last = 0.0;
    bool have_first = false;
    unsigned long frames = 0;

    while (fgets(line, sizeof(line), f)) {
        double t_ms;
        uint32_t id;
        uint8_t data[8], dlc;
        if (!trc_parse(line, &t_ms, &id, data, &dlc)) {
            continue;
        }
        if (!profile_carries(p, id)) {
            continue;
        }
        if (!have_first) {
            t_first = t_ms;
            have_first = true;
        }
        t_last = t_ms;
        frames++;
    }
    if (!have_first) {
        fprintf(stderr, "no decodable frames found in %s\n", path);
        return 1;
    }

    const double duration_ms = t_last - t_first;

    replay_t r;
    memset(&r, 0, sizeof(r));
    r.quiet = quiet;
    r.accel_min = r.accel_max = 0.0f;
    /* grid_n: one point at 0 plus one per whole GRID_MS of capture, which is
     * np.arange(0, duration + dt, dt) to within a single 20 ms point. */
    r.grid_n = (uint32_t)(duration_ms / (double)GRID_MS) + 1u;

    can_decode_init(&r.dec, p);
    brake_fsm_init(&r.fsm, NULL);   /* shipped defaults */

    /* ---- pass 2: feed frames, stepping the grid as time advances -------- */
    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        double t_ms;
        uint32_t id;
        uint8_t data[8], dlc;
        if (!trc_parse(line, &t_ms, &id, data, &dlc)) {
            continue;
        }
        if (!profile_carries(p, id)) {
            continue;
        }

        /* Rounded, not truncated: the firmware's clock is integer ms, so this
         * quantization is real, but rounding keeps it to +/-0.5 ms instead of
         * a systematic -0.5 ms bias against the reference's float times. */
        const double rel = t_ms - t_first;
        const uint32_t now_ms = (uint32_t)(rel < 0.0 ? 0.0 : rel + 0.5);

        while (r.grid_i < r.grid_n && (r.grid_i * GRID_MS) < now_ms) {
            grid_step(&r);
        }
        can_decode_feed(&r.dec, id, data, dlc, now_ms);
    }
    fclose(f);

    while (r.grid_i < r.grid_n) {
        grid_step(&r);
    }

    const double dt_s = (double)GRID_MS / 1000.0;
    printf("capture=%s\n", path);
    printf("frames=%lu\n", frames);
    printf("duration_s=%.3f\n", duration_ms / 1000.0);
    printf("grid_points=%u\n", (unsigned)r.grid_n);
    printf("peak_speed_mph=%.3f\n", (double)r.speed_max);
    printf("accel_min_mphps=%.3f\n", (double)r.accel_min);
    printf("accel_max_mphps=%.3f\n", (double)r.accel_max);
    printf("accel_invalid_ticks=%u\n", (unsigned)r.accel_invalid_ticks);
    printf("transitions=%u\n", (unsigned)r.grid_transitions);
    printf("fsm_transitions=%u\n", (unsigned)r.fsm_transitions);
    printf("on_time_s=%.3f\n", (double)r.on_ticks * dt_s);
    printf("on_frac=%.6f\n",
           r.grid_n ? (double)r.on_ticks / (double)r.grid_n : 0.0);
    return 0;
}
