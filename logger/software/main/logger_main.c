/*
 * open-chmbl CAN data logger — ESP-WROVER-KIT v4.1 firmware.
 *
 * Captures ALL CAN traffic (no filtering) in listen-only mode and writes it to
 * the on-board microSD as PCAN .trc (v2.1) ASCII files. A single debounced
 * pushbutton toggles recording: each start opens a new N.trc (N an increasing
 * integer), each stop closes it. The kit's LCD shows a running operations log.
 *
 * This is the DE-07 "ride logger" — a self-contained ESP32 replacement for the
 * Raspberry Pi rig (see docs/can-profiles.md §3). Power-loss / card-removal
 * robustness is intentionally out of scope.
 *
 * Tasks:
 *   CAN-RX  — twai_receive(); timestamps each frame and queues it (only while
 *             recording); counts drops if the queue is full.
 *   Writer  — owns the open file and the recording state; drains the frame queue
 *             and the button-command queue via a queue set, formats each frame
 *             and writes it, and opens/closes files on toggle.
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "driver/twai.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "bsp/esp_wrover_kit.h"
#include "button_gpio.h"
#include "iot_button.h"

#include "trc_format.h"
#include "ui_log.h"

#define TRC_DIR         BSP_SD_MOUNT_POINT
#define RX_QUEUE_LEN    CONFIG_LOGGER_RX_QUEUE_LEN
#define CTRL_QUEUE_LEN  4

/* A CAN frame stamped with its receive time (esp_timer microseconds). */
typedef struct {
    int64_t     t_us;
    trc_frame_t frame;
} ts_frame_t;

typedef enum { CMD_TOGGLE } logger_cmd_t;

static QueueHandle_t    s_frame_q;
static QueueHandle_t    s_ctrl_q;
static QueueSetHandle_t s_queue_set;

static volatile bool     s_recording;   /* read by CAN-RX, written by Writer */
static volatile uint32_t s_dropped;     /* frames dropped on a full queue     */
static bool              s_sd_ok;        /* microSD mounted successfully       */
static unsigned          s_next_num = 1; /* next N.trc file number             */

/* ---- CAN bit-rate selection (Kconfig) ------------------------------------ */

static twai_timing_config_t logger_timing(void)
{
#if defined(CONFIG_LOGGER_CAN_BITRATE_125K)
    return (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
#elif defined(CONFIG_LOGGER_CAN_BITRATE_250K)
    return (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
#elif defined(CONFIG_LOGGER_CAN_BITRATE_1M)
    return (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
#else
    return (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
#endif
}

static const char *logger_bitrate_str(void)
{
#if defined(CONFIG_LOGGER_CAN_BITRATE_125K)
    return "125k";
#elif defined(CONFIG_LOGGER_CAN_BITRATE_250K)
    return "250k";
#elif defined(CONFIG_LOGGER_CAN_BITRATE_1M)
    return "1M";
#else
    return "500k";
#endif
}

static const char *logger_mode_str(void)
{
#if CONFIG_LOGGER_CAN_LISTEN_ONLY
    return "listen-only";
#else
    return "normal/ACK";
#endif
}

/* ---- microSD file bookkeeping -------------------------------------------- */

/* Scan the card for existing N.trc files and set s_next_num = max(N) + 1.
 * Returns the count of matching files found. */
static int scan_existing_files(unsigned *highest)
{
    unsigned max_n = 0;
    int count = 0;

    DIR *dir = opendir(TRC_DIR);
    if (dir == NULL) {
        *highest = 0;
        return 0;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        unsigned n = 0;
        int consumed = 0;
        /* Match "<digits>.trc" exactly (nothing trailing). */
        if (sscanf(ent->d_name, "%u.trc%n", &n, &consumed) == 1 &&
            consumed == (int)strlen(ent->d_name)) {
            count++;
            if (n > max_n) {
                max_n = n;
            }
        }
    }
    closedir(dir);

    *highest = max_n;
    s_next_num = max_n + 1;
    return count;
}

/* ---- Writer task: owns the file + recording state ------------------------ */

static FILE   *s_file;
static uint32_t s_msgnr;
static int64_t  s_t0_us;      /* timestamp of the first frame in this file */
static uint32_t s_file_frames;

static void writer_start_recording(void)
{
    if (!s_sd_ok) {
        ui_log_line("cannot record: no microSD");
        return;
    }

    /* Discard any straggler frame the RX task may have queued right at the
     * previous stop, so it can't bleed into the new file. */
    ts_frame_t stale;
    while (xQueueReceive(s_frame_q, &stale, 0) == pdTRUE) {
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/%u.trc", TRC_DIR, s_next_num);

    s_file = fopen(path, "w");
    if (s_file == NULL) {
        ui_log_line("open FAILED: %u.trc", s_next_num);
        return;
    }

    char header[512];
    int hn = trc_format_header(header, sizeof(header));
    if (hn > 0) {
        fwrite(header, 1, (size_t)hn, s_file);
    }

    ui_log_line("opened file %u.trc", s_next_num);
    s_msgnr = 0;
    s_file_frames = 0;
    s_t0_us = INT64_MIN;
    s_dropped = 0;
    s_recording = true;
    s_next_num++;
    ui_log_line("recording started");
}

/* Write one queued frame to the open file. */
static void writer_write_frame(const ts_frame_t *tf)
{
    if (s_file == NULL) {
        return;
    }
    if (s_t0_us == INT64_MIN) {
        s_t0_us = tf->t_us;
    }
    double time_ms = (double)(tf->t_us - s_t0_us) / 1000.0;

    char line[128];
    int n = trc_format_line(line, sizeof(line), ++s_msgnr, time_ms, &tf->frame);
    if (n > 0) {
        fwrite(line, 1, (size_t)n, s_file);
        fputc('\n', s_file);
        s_file_frames++;
    }
}

static void writer_stop_recording(void)
{
    /* Stop accepting new frames, then flush any already queued for this file. */
    s_recording = false;

    ts_frame_t tf;
    while (xQueueReceive(s_frame_q, &tf, 0) == pdTRUE) {
        writer_write_frame(&tf);
    }

    if (s_file != NULL) {
        fflush(s_file);
        fclose(s_file);
        s_file = NULL;
    }
    ui_log_line("recording stopped");
    ui_log_line("file closed: %u frames%s", (unsigned)s_file_frames,
                s_dropped ? " (drops!)" : "");
    if (s_dropped) {
        ui_log_line("  dropped %u frames", (unsigned)s_dropped);
    }
}

static void writer_handle_toggle(void)
{
    if (s_recording) {
        writer_stop_recording();
    } else {
        writer_start_recording();
    }
}

static void writer_task(void *arg)
{
    (void)arg;
    for (;;) {
        QueueSetMemberHandle_t member = xQueueSelectFromSet(s_queue_set, portMAX_DELAY);
        if (member == s_ctrl_q) {
            logger_cmd_t cmd;
            if (xQueueReceive(s_ctrl_q, &cmd, 0) == pdTRUE && cmd == CMD_TOGGLE) {
                writer_handle_toggle();
            }
        } else if (member == s_frame_q) {
            ts_frame_t tf;
            if (xQueueReceive(s_frame_q, &tf, 0) == pdTRUE) {
                writer_write_frame(&tf);
            }
        }
    }
}

/* ---- CAN-RX task --------------------------------------------------------- */

static void can_rx_task(void *arg)
{
    (void)arg;
    for (;;) {
        twai_message_t msg;
        if (twai_receive(&msg, portMAX_DELAY) != ESP_OK) {
            continue;
        }
        if (!s_recording) {
            continue;   /* discard traffic while stopped */
        }

        ts_frame_t tf = {
            .t_us = esp_timer_get_time(),
            .frame = {
                .id       = msg.identifier,
                .extended = msg.extd,
                .rtr      = msg.rtr,
                .dlc      = msg.data_length_code,
            },
        };
        memcpy(tf.frame.data, msg.data, sizeof(tf.frame.data));

        if (xQueueSend(s_frame_q, &tf, 0) != pdTRUE) {
            s_dropped++;
        }
    }
}

/* ---- Button ------------------------------------------------------------- */

static void on_button_click(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    ui_log_line("button pressed");
    logger_cmd_t cmd = CMD_TOGGLE;
    xQueueSend(s_ctrl_q, &cmd, 0);
}

static void button_init(void)
{
    const button_config_t btn_cfg = { 0 };
    const button_gpio_config_t gpio_cfg = {
        .gpio_num = CONFIG_LOGGER_BUTTON_GPIO,
        .active_level = 0,       /* button pulls the pin to GND when pressed */
        .enable_power_save = false,
        .disable_pull = false,   /* use the internal pull-up */
    };
    button_handle_t btn = NULL;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn));
    ESP_ERROR_CHECK(iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL,
                                           on_button_click, NULL));
}

/* ---- CAN init ------------------------------------------------------------ */

static void can_init(void)
{
    /* Select the mode into a variable first: preprocessor directives inside a
     * function-like macro's argument list are undefined behavior. */
#if CONFIG_LOGGER_CAN_LISTEN_ONLY
    const twai_mode_t mode = TWAI_MODE_LISTEN_ONLY;
#else
    const twai_mode_t mode = TWAI_MODE_NORMAL;
#endif
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        CONFIG_LOGGER_CAN_TX_GPIO, CONFIG_LOGGER_CAN_RX_GPIO, mode);
    g.rx_queue_len = 32;   /* smooth ISR -> task hand-off at high bus load */

    twai_timing_config_t t = logger_timing();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();   /* no filtering */

    ESP_ERROR_CHECK(twai_driver_install(&g, &t, &f));
    ESP_ERROR_CHECK(twai_start());
}

/* ---- app_main ------------------------------------------------------------ */

void app_main(void)
{
    bsp_display_start();      /* LCD + LVGL up (backlight on) */
    ui_log_init();
    ui_log_line("booted");
    ui_log_line("CAN %s %s", logger_bitrate_str(), logger_mode_str());

    s_sd_ok = (bsp_sdcard_mount() == ESP_OK);
    if (s_sd_ok) {
        unsigned highest = 0;
        int found = scan_existing_files(&highest);
        ui_log_line("filesystem mounted");
        ui_log_line("listed %d .trc file(s), max #%u", found, highest);
        ui_log_line("next file number = %u", s_next_num);
    } else {
        ui_log_line("microSD mount FAILED");
    }

    s_frame_q = xQueueCreate(RX_QUEUE_LEN, sizeof(ts_frame_t));
    s_ctrl_q  = xQueueCreate(CTRL_QUEUE_LEN, sizeof(logger_cmd_t));
    s_queue_set = xQueueCreateSet(RX_QUEUE_LEN + CTRL_QUEUE_LEN);
    ESP_ERROR_CHECK(xQueueAddToSet(s_frame_q, s_queue_set) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xQueueAddToSet(s_ctrl_q, s_queue_set) == pdPASS ? ESP_OK : ESP_FAIL);

    xTaskCreate(writer_task, "trc_writer", 4096, NULL, 5, NULL);
    xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 6, NULL);

    can_init();
    button_init();

    ui_log_line("ready: press to start/stop");
}
