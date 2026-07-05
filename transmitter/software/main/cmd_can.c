/*
 * `can` command — CAN reception diagnostics and bench replay (DE-08).
 *
 * Mirrors docs/cli.md §3 (TX-CLI-1/4):
 *
 *     can show           bit rate, frame counters, bus health, profile
 *     can replay decel   feed a synthetic coast-then-brake vector through
 *                        the real decoder (offline instance — does not
 *                        disturb the live decode, transmits nothing)
 *
 * The replay vector is synthesized by packing engineering values through
 * the active profile (can_sig_pack), so it exercises the same bit layouts
 * the live bus would.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_console.h"
#include "esp_timer.h"

#include "bike_profiles.h"
#include "can_rx.h"
#include "console.h"

static int can_do_show(void)
{
    can_rx_stats_t st;
    can_rx_get_stats(&st);
    const bike_profile_t *p = can_rx_profile();

    printf("profile:  %s\n", p->name);
    printf("bitrate:  %lu bit/s (listen-only)\n", (unsigned long)st.bitrate);
    printf("driver:   %s\n", st.started ? "running" : "NOT RUNNING");
    printf("frames:   %lu rx, %lu decoded\n",
           (unsigned long)st.frames_rx, (unsigned long)st.frames_decoded);
    printf("dropped:  %lu (rx queue), bus errors: %lu\n",
           (unsigned long)st.rx_missed, (unsigned long)st.bus_errors);
    if (st.frames_rx == 0) {
        printf("last rx:  never\n");
    } else {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        printf("last rx:  %lu ms ago\n", (unsigned long)(now - st.last_rx_ms));
    }
    printf("profile IDs: wheel 0x%03lX, trans 0x%03lX, engine 0x%03lX/0x%03lX,"
           " body 0x%03lX, chassis 0x%03lX\n",
           (unsigned long)p->wheel_speed.can_id,
           (unsigned long)p->gear.can_id,
           (unsigned long)p->throttle_pct.can_id,
           (unsigned long)p->rpm_ecu.can_id,
           (unsigned long)p->engine_cutoff_flag.can_id,
           (unsigned long)p->side_stand_up.can_id);
    return 0;
}

/* Synthetic "decel" vector: hold 40 mph for 1 s, brake at -12 mph/s to a
 * stop, clutch in for the last moments — roughly the profile of a firm stop
 * from the reference ride. 20 ms frame spacing (~0x102's real cadence). */
static int can_do_replay_decel(void)
{
    const bike_profile_t *p = can_rx_profile();
    can_decode_t dec;
    can_decode_init(&dec, p);

    printf("%8s %12s %12s %8s %8s\n",
           "t[ms]", "wheel[mph]", "accel[mph/s]", "gear", "clutch");

    float mph = 40.0f;
    for (uint32_t t = 0; mph > 0.0f || t <= 1000; t += 20) {
        if (t > 1000) {
            mph -= 12.0f * 0.020f;
            if (mph < 0.0f) {
                mph = 0.0f;
            }
        }

        /* pack wheel speed (profile decodes km/h if flagged) */
        uint8_t frame[8] = {0};
        float eng = p->wheel_speed_kmh ? mph / KMH_TO_MPH : mph;
        uint32_t raw = (uint32_t)((eng - p->wheel_speed.offset) /
                                  p->wheel_speed.scale + 0.5f);
        can_sig_pack(&p->wheel_speed, frame, 8, raw);
        can_sig_pack(&p->wheel_speed_rear, frame, 8, raw);
        can_decode_feed(&dec, p->wheel_speed.can_id, frame, 8, t);

        /* gear/clutch frame: 3rd gear, clutch pulled below 8 mph */
        if (p->gear.can_id != 0) {
            uint8_t tf[8] = {0};
            can_sig_pack(&p->gear, tf, 8, 3);
            can_sig_pack(&p->clutch_raw, tf, 8, mph < 8.0f ? 0xDu : 0x0u);
            can_decode_feed(&dec, p->gear.can_id, tf, 8, t);
        }

        if (t % 250 == 0 || mph == 0.0f) {
            printf("%8lu %12.2f %12.2f %8.0f %8.0f\n", (unsigned long)t,
                   (double)dec.sig.wheel_speed.value,
                   (double)dec.sig.accel.value,
                   (double)dec.sig.gear.value,
                   (double)dec.sig.clutch_pulled.value);
            if (mph == 0.0f) {
                break;
            }
        }
    }
    printf("can: replay complete (offline decoder instance; live decode "
           "untouched)\n");
    return 0;
}

static int cmd_can(int argc, char **argv)
{
    if (argc < 2 || strcasecmp(argv[1], "show") == 0) {
        return can_do_show();
    }
    if (strcasecmp(argv[1], "replay") == 0 && argc == 3) {
        if (strcasecmp(argv[2], "decel") == 0) {
            return can_do_replay_decel();
        }
        printf("can: unknown vector '%s' (available: decel)\n", argv[2]);
        return 1;
    }
    printf("usage: can [show] | can replay decel\n");
    return 1;
}

void cmd_can_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "can",
        .help = "CAN RX diagnostics: can show | can replay decel",
        .hint = "[show|replay]",
        .func = &cmd_can,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
