#include "can_tap.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include "bike_profiles.h"
#include "can_decode.h"
#include "ui_log.h"

static can_decode_t      s_dec;
static SemaphoreHandle_t s_lock;
static uint32_t          s_frames;
static uint32_t          s_decoded;

void can_tap_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);

    const bike_profile_t *p = BIKE_PROFILE_DEFAULT;
    can_decode_init(&s_dec, p);

    /* The wheel-speed low-pass ahead of the accel slope is part of the DE-09
     * tuning set, so it comes from the same Kconfig menu as the thresholds.
     * can_decode's own default is the same 80 ms; setting it explicitly keeps
     * the menu authoritative. */
    can_decode_set_speed_smooth_ms(&s_dec,
                                   (float)CONFIG_BRAKE_FSM_SPEED_SMOOTH_MS);

    ui_log_line("CAN decode tap: profile \"%s\" @ %u bit/s", p->name,
                (unsigned)p->bitrate);
    ui_log_line("  wheel-speed LPF %d ms, accel window %u ms, ring %u samples",
                CONFIG_BRAKE_FSM_SPEED_SMOOTH_MS,
                (unsigned)CAN_DECODE_ACCEL_WINDOW_MS,
                (unsigned)CAN_DECODE_SPEED_HIST);
}

void can_tap_feed(uint32_t can_id, const uint8_t *data, uint8_t dlc,
                  uint32_t now_ms)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_frames++;
    if (can_decode_feed(&s_dec, can_id, data, dlc, now_ms)) {
        s_decoded++;
    }
    xSemaphoreGive(s_lock);
}

void can_tap_snapshot(can_tap_snap_t *out, uint32_t now_ms)
{
    memset(out, 0, sizeof(*out));

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    const can_signals_t *s = &s_dec.sig;

    out->speed_mph   = s->wheel_speed.value;
    out->speed_valid = can_sig_valid(&s->wheel_speed, now_ms);
    out->accel_mphps = s->accel.value;
    out->accel_valid = can_sig_valid(&s->accel, now_ms);
    out->clutch_pulled = (s->clutch_pulled.value != 0.0f);
    out->gear        = s->gear.value;
    out->in_gear     = (s->gear.value != 0.0f);

    /* The gate wants the raw age rather than a bool, because it applies two
     * different timeouts to it (see logger_main.c): CAN_DECODE_STALE_MS to
     * trust a fresh reading, and the longer bus-silence timeout to declare the
     * bike off. */
    out->cutoff_seen   = s->engine_cutoff.seen;
    out->cutoff        = (s->engine_cutoff.value != 0.0f);
    out->cutoff_age_ms = now_ms - s->engine_cutoff.last_ms;

    out->frames  = s_frames;
    out->decoded = s_decoded;

    xSemaphoreGive(s_lock);
}
