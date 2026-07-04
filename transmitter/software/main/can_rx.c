/* DE-08 — TWAI listen-only RX task + source-aware signal snapshot. */

#include "can_rx.h"

#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "bike_profiles.h"

static const char *TAG = "can_rx";

static const bike_profile_t *s_profile = BIKE_PROFILE_DEFAULT;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static can_decode_t s_decode;      /* live decode state (SIG_SOURCE_CAN) */
static can_signals_t s_fake;       /* bench values (SIG_SOURCE_FAKE) */
static can_decode_t s_fake_accel;  /* accel filter for faked wheel speed */
static bool s_fake_accel_override; /* `sig set accel` beats derivation */
static sig_source_t s_source = SIG_SOURCE_CAN;
static can_rx_stats_t s_stats;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---- signal name table -------------------------------------------------- */

const char *const sig_names[] = {
    "wheel_speed", "accel", "clutch_pulled", "gear", "wheel_speed_rear",
    "throttle_pct", "rpm", "rpm_ecu", "side_stand_up", "engine_cutoff",
    NULL,
};

sig_value_t *sig_by_index(can_signals_t *s, int idx)
{
    sig_value_t *const fields[] = {
        &s->wheel_speed, &s->accel, &s->clutch_pulled, &s->gear,
        &s->wheel_speed_rear, &s->throttle_pct, &s->rpm, &s->rpm_ecu,
        &s->side_stand_up, &s->engine_cutoff,
    };
    if (idx < 0 || idx >= (int)(sizeof(fields) / sizeof(fields[0]))) {
        return NULL;
    }
    return fields[idx];
}

/* ---- source-aware access ------------------------------------------------ */

void sig_set_source(sig_source_t src)
{
    taskENTER_CRITICAL(&s_lock);
    s_source = src;
    taskEXIT_CRITICAL(&s_lock);
}

sig_source_t sig_get_source(void)
{
    return s_source;
}

void sig_snapshot(can_signals_t *out, uint32_t *t)
{
    uint32_t t_now = now_ms();
    taskENTER_CRITICAL(&s_lock);
    if (s_source == SIG_SOURCE_FAKE) {
        /* fake values never go stale — keep them pinned to now */
        for (int i = 0; sig_names[i]; i++) {
            sig_value_t *v = sig_by_index(&s_fake, i);
            if (v->seen) {
                v->last_ms = t_now;
            }
        }
        *out = s_fake;
    } else {
        *out = s_decode.sig;
    }
    taskEXIT_CRITICAL(&s_lock);
    if (t) {
        *t = t_now;
    }
}

static int sig_index(const char *name)
{
    for (int i = 0; sig_names[i]; i++) {
        if (strcmp(name, sig_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

bool sig_fake_set(const char *name, float value)
{
    int i = sig_index(name);
    if (i < 0) {
        return false;
    }
    uint32_t t = now_ms();
    taskENTER_CRITICAL(&s_lock);
    sig_value_t *v = sig_by_index(&s_fake, i);
    v->value = value;
    v->seen = true;
    v->last_ms = t;
    if (strcmp(name, "accel") == 0) {
        s_fake_accel_override = true;
    } else if (strcmp(name, "wheel_speed") == 0 && !s_fake_accel_override) {
        can_decode_accel_feed(&s_fake_accel, value, t);
        if (s_fake_accel.sig.accel.seen) {
            s_fake.accel = s_fake_accel.sig.accel;
        }
    }
    taskEXIT_CRITICAL(&s_lock);
    return true;
}

bool sig_fake_clear(const char *name)
{
    int i = sig_index(name);
    if (i < 0) {
        return false;
    }
    taskENTER_CRITICAL(&s_lock);
    sig_value_t *v = sig_by_index(&s_fake, i);
    v->seen = false;
    v->value = 0.0f;
    if (strcmp(name, "accel") == 0) {
        s_fake_accel_override = false;
    }
    taskEXIT_CRITICAL(&s_lock);
    return true;
}

/* ---- TWAI reception ------------------------------------------------------ */

void can_rx_get_stats(can_rx_stats_t *out)
{
    twai_status_info_t st;
    taskENTER_CRITICAL(&s_lock);
    *out = s_stats;
    taskEXIT_CRITICAL(&s_lock);
    if (out->started && twai_get_status_info(&st) == ESP_OK) {
        out->rx_missed = st.rx_missed_count;
        out->bus_errors = st.bus_error_count;
    }
}

const bike_profile_t *can_rx_profile(void)
{
    return s_profile;
}

/* Single acceptance filter covering all of the profile's 11-bit IDs: bits
 * that differ between any two IDs become don't-care. Over-accepts (the
 * decoder ignores non-profile IDs anyway) but sheds most bus traffic in
 * hardware. */
static void profile_filter(const bike_profile_t *p, twai_filter_config_t *f)
{
    const can_signal_t *sigs[] = {
        &p->wheel_speed, &p->wheel_speed_rear, &p->clutch_raw, &p->gear,
        &p->throttle_pct, &p->rpm, &p->rpm_ecu, &p->side_stand_up,
        &p->engine_cutoff_flag, &p->cutoff_reason,
    };
    uint32_t code = 0, diff = 0;
    bool first = true;
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        if (sigs[i]->can_id == 0) {
            continue;
        }
        if (first) {
            code = sigs[i]->can_id;
            first = false;
        } else {
            diff |= code ^ sigs[i]->can_id;
        }
    }
    /* SJA1000-style single filter: standard ID sits in bits [31:21];
     * mask bit 1 = don't care. */
    f->single_filter = true;
    f->acceptance_code = code << 21;
    f->acceptance_mask = (diff << 21) | 0x001FFFFF;
}

static void can_rx_task(void *arg)
{
    (void)arg;
    twai_message_t msg;

    while (1) {
        if (twai_receive(&msg, portMAX_DELAY) != ESP_OK) {
            continue;
        }
        uint32_t t = now_ms();
        taskENTER_CRITICAL(&s_lock);
        s_stats.frames_rx++;
        s_stats.last_rx_ms = t;
        if (!msg.extd && !msg.rtr &&
            can_decode_feed(&s_decode, msg.identifier, msg.data,
                            msg.data_length_code, t)) {
            s_stats.frames_decoded++;
        }
        taskEXIT_CRITICAL(&s_lock);
    }
}

void can_rx_init(void)
{
    can_decode_init(&s_decode, s_profile);
    can_decode_init(&s_fake_accel, s_profile);
    s_stats.bitrate = s_profile->bitrate;

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CONFIG_CHMBL_CAN_TX_GPIO,
        (gpio_num_t)CONFIG_CHMBL_CAN_RX_GPIO,
        TWAI_MODE_LISTEN_ONLY);
    g.rx_queue_len = 32;

    twai_timing_config_t timing;
    if (s_profile->bitrate == 250000) {
        twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();
        timing = t;
    } else {
        twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
        timing = t;
        if (s_profile->bitrate != 500000) {
            ESP_LOGW(TAG, "unsupported profile bitrate %lu, using 500k",
                     (unsigned long)s_profile->bitrate);
        }
    }

    twai_filter_config_t filter;
    profile_filter(s_profile, &filter);

    esp_err_t err = twai_driver_install(&g, &timing, &filter);
    if (err == ESP_OK) {
        err = twai_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI bring-up failed (%s) — CAN decode inactive",
                 esp_err_to_name(err));
        return;
    }

    s_stats.started = true;
    xTaskCreate(can_rx_task, "can_rx", 3072, NULL, 10, NULL);
    ESP_LOGI(TAG, "listen-only @ %lu bit/s, profile \"%s\" (TX GPIO%d, RX GPIO%d)",
             (unsigned long)s_profile->bitrate, s_profile->name,
             CONFIG_CHMBL_CAN_TX_GPIO, CONFIG_CHMBL_CAN_RX_GPIO);
}
