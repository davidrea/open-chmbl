/*
 * Ride logging — CAN to microSD, gated on the engine cutoff switch.
 *
 * The card handling, writer and card-detect task are ported from the logger
 * firmware (logger/software/main/logger_main.c) on this same board, keeping
 * its hard-won details: full buffering for SD throughput, a debounced
 * DET_A poll, drop accounting, a clean abort on card removal, and two plain
 * queues rather than a queue set. What changes is the trigger — the button is
 * replaced by the kill-switch gate (log_gate.h) — and the durability: there
 * is no button press to end a capture cleanly any more, so the file is synced
 * periodically.
 */
#include "trc_log.h"

#include <string.h>

#include "sdkconfig.h"

#if CONFIG_CHMBL_TRC_LOG

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "can_decode.h"
#include "can_rx.h"
#include "log_gate.h"
#include "status_led.h"
#include "trc_format.h"

static const char *TAG = "trc_log";

#define TRC_DIR        "/sdcard"
#define CTRL_QUEUE_LEN 8

/* Full-buffering size for the .trc stream. The default newlib buffer (~128 B)
 * flushes an SD block write every few frames; at ~1500 frames/s those tiny
 * writes cap the writer near ~400 frames/s and the queue overflows. A large
 * buffer batches writes into full SD clusters. (Same figure, same reason, as
 * the logger firmware.) */
#define TRC_IO_BUF_SIZE (32 * 1024)

/* DET_A polling: 50 ms with a 3-sample agreement filter debounces the
 * mechanical detect switch in ~150 ms. */
#define CARD_POLL_MS      50
#define CARD_STABLE_POLLS 3

/* Gate evaluation period. engine_cutoff arrives far faster than this; 20 Hz
 * puts a new file within ~50 ms of the switch moving, which is plenty. */
#define GATE_PERIOD_MS 50

typedef struct {
    int64_t     t_us;   /* esp_timer receive time */
    trc_frame_t frame;
} ts_frame_t;

typedef enum {
    CMD_CARD_INSERTED,
    CMD_CARD_REMOVED,
} log_cmd_t;

static QueueHandle_t s_frame_q;
static QueueHandle_t s_ctrl_q;

/* Shared with the CAN-RX task: it reads s_recording and bumps s_dropped. */
static volatile bool     s_recording;
static volatile uint32_t s_dropped;

/* Writer-owned; the rest are read only by trc_log_get_status(). */
static volatile bool     s_card_present;
static volatile bool     s_sd_ok;
static volatile bool     s_fault;
static volatile unsigned s_file_num;
static volatile uint32_t s_file_frames;
static volatile uint32_t s_files;

static sdmmc_card_t *s_card;
static unsigned      s_next_num = 1;

static log_gate_t s_gate;
static const log_gate_cfg_t s_gate_cfg = {
    .stale_ms = CAN_DECODE_STALE_MS,
    .close_ms = CONFIG_CHMBL_TRC_LOG_CLOSE_MS,
};
/* Set when a session starts (or a card arrives mid-session) and cleared by
 * the open attempt, successful or not — so a failure is reported once, not
 * retried 20 times a second. */
static bool s_want_file;

static FILE    *s_file;
static char    *s_io_buf;
static uint32_t s_msgnr;
static int64_t  s_t0_us;
static uint32_t s_last_sync_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void set_fault(bool on)
{
    s_fault = on;
    status_led_log_fault(on);
}

/* ---- microSD ------------------------------------------------------------ */

/* DET_A is active-low: R17 pulls it up, a seated card pulls J5 pin10 to GND. */
static bool sd_card_present(void)
{
    return gpio_get_level(CONFIG_CHMBL_SD_DET_GPIO) == 0;
}

static void sd_det_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_CHMBL_SD_DET_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,   /* R17 does this — see Kconfig */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

/* Mount the card as FAT at TRC_DIR over the S3's SDMMC host on the logger
 * PCB's pins (BRINGUP.md §0). Card-detect is gated by us, not the driver, so
 * a missing card gets our own log line rather than an opaque mount error. */
static bool sd_mount(void)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = CONFIG_CHMBL_SD_CLK_GPIO;
    slot.cmd = CONFIG_CHMBL_SD_CMD_GPIO;
    slot.d0  = CONFIG_CHMBL_SD_D0_GPIO;
#if CONFIG_CHMBL_SD_BUS_WIDTH_4
    slot.d1  = CONFIG_CHMBL_SD_D1_GPIO;
    slot.d2  = CONFIG_CHMBL_SD_D2_GPIO;
    slot.d3  = CONFIG_CHMBL_SD_D3_GPIO;
    slot.width = 4;
#else
    slot.width = 1;
#endif
    /* No SDMMC_SLOT_FLAG_INTERNAL_PULLUP: the board has external pull-ups
     * (R7–R11), and the internal ones would only mask an unstuffed one. */

    const esp_err_t err = esp_vfs_fat_sdmmc_mount(TRC_DIR, &host, &slot,
                                                  &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "microSD mount failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static void sd_unmount(void)
{
    esp_vfs_fat_sdcard_unmount(TRC_DIR, s_card);
    s_card = NULL;
}

/* Set s_next_num = max(N in N.trc) + 1, so a capture never overwrites one.
 * A summary only: after a season of rides the root can hold hundreds of
 * files, and listing them all on every key-on helps nobody. */
static void sd_scan(void)
{
    DIR *dir = opendir(TRC_DIR);
    if (dir == NULL) {
        ESP_LOGE(TAG, "cannot list %s", TRC_DIR);
        s_next_num = 1;
        return;
    }

    unsigned max_n = 0;
    int trc = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        /* Match "<digits>.trc" exactly (nothing trailing). */
        unsigned n = 0;
        int consumed = 0;
        if (sscanf(ent->d_name, "%u.trc%n", &n, &consumed) == 1 &&
            consumed == (int)strlen(ent->d_name)) {
            trc++;
            if (n > max_n) {
                max_n = n;
            }
        }
    }
    closedir(dir);

    s_next_num = max_n + 1;
    ESP_LOGI(TAG, "card holds %d capture(s), highest #%u; next is %u.trc",
             trc, max_n, s_next_num);
}

/* Mount and inventory the card. Shared by boot and runtime card insertion. */
static void sd_bring_up(void)
{
    s_card_present = sd_card_present();
    if (!s_card_present) {
        ESP_LOGI(TAG, "no microSD card — ride logging idle until one is inserted");
        s_sd_ok = false;
        return;
    }

    s_sd_ok = sd_mount();
    if (!s_sd_ok) {
        /* A card is seated but unusable: that is a fault the rider needs to
         * see BEFORE riding, or the ride goes unrecorded. */
        set_fault(true);
        return;
    }

    /* The width actually negotiated (log_bus_width is log2 of capability). */
    const int bus_width = s_card->is_mmc ? (1 << s_card->log_bus_width)
                                         : (s_card->ssr.cur_bus_width ? 4 : 1);
    ESP_LOGI(TAG, "microSD mounted: %s, %llu MB, %d-bit bus", s_card->cid.name,
             (unsigned long long)(((uint64_t)s_card->csd.capacity *
                                   s_card->csd.sector_size) / (1024 * 1024)),
             bus_width);
    sd_scan();
    set_fault(false);
}

/* ---- Writer: the open file ---------------------------------------------- */

/* Release the file after an I/O failure. Whatever reached the card before
 * the last successful sync is a readable capture; the session stays active
 * but no new file is attempted until the next session or a card change. */
static void writer_fail(const char *what)
{
    s_recording = false;
    if (s_file != NULL) {
        fclose(s_file);
        s_file = NULL;
    }
    free(s_io_buf);
    s_io_buf = NULL;
    ESP_LOGE(TAG, "%u.trc: %s failed — capture ended after %u frames",
             s_file_num, what, (unsigned)s_file_frames);
    set_fault(true);
}

static void writer_start(void)
{
    if (!sd_card_present()) {
        ESP_LOGW(TAG, "kill switch in RUN but no microSD card — not recording");
        return;
    }
    if (!s_sd_ok) {
        ESP_LOGE(TAG, "kill switch in RUN but microSD not mounted — not recording");
        set_fault(true);
        return;
    }

    /* Discard any straggler queued at the previous close. */
    ts_frame_t stale;
    while (xQueueReceive(s_frame_q, &stale, 0) == pdTRUE) {
    }

    char path[32];
    snprintf(path, sizeof(path), "%s/%u.trc", TRC_DIR, s_next_num);
    s_file = fopen(path, "w");
    if (s_file == NULL) {
        ESP_LOGE(TAG, "cannot create %u.trc", s_next_num);
        set_fault(true);
        return;
    }

    /* The number is spent once the file exists, whatever happens next. */
    s_file_num = s_next_num++;
    s_files++;
    s_msgnr = 0;
    s_file_frames = 0;
    s_t0_us = INT64_MIN;
    s_dropped = 0;

    /* Must be set before any I/O; the buffer outlives the stream (freed at
     * close). If it can't be had, record anyway on the small default. */
    s_io_buf = malloc(TRC_IO_BUF_SIZE);
    if (s_io_buf != NULL) {
        setvbuf(s_file, s_io_buf, _IOFBF, TRC_IO_BUF_SIZE);
    } else {
        ESP_LOGW(TAG, "no %d KB I/O buffer — drops likely", TRC_IO_BUF_SIZE / 1024);
    }

    char header[512];
    const int hn = trc_format_header(header, sizeof(header));
    if (hn > 0 && fwrite(header, 1, (size_t)hn, s_file) != (size_t)hn) {
        writer_fail("header write");
        return;
    }

    s_last_sync_ms = now_ms();
    s_recording = true;
    set_fault(false);
    ESP_LOGI(TAG, "recording %u.trc (session %u)", s_file_num,
             (unsigned)s_gate.sessions);
}

static void writer_write_frame(const ts_frame_t *tf)
{
    if (s_file == NULL) {
        return;
    }
    if (s_t0_us == INT64_MIN) {
        s_t0_us = tf->t_us;
    }
    const double time_ms = (double)(tf->t_us - s_t0_us) / 1000.0;

    char line[128];
    const int n = trc_format_line(line, sizeof(line), ++s_msgnr, time_ms,
                                  &tf->frame);
    if (n <= 0) {
        return;
    }
    line[n] = '\n';     /* trc_format_line leaves room: it NUL-terminates */
    if (fwrite(line, 1, (size_t)n + 1, s_file) != (size_t)n + 1) {
        writer_fail("write (card full?)");
        return;
    }
    s_file_frames++;
}

/* Commit what has been written so far. fflush hands the buffer to the
 * filesystem; fsync makes FAT record the file's length and cluster chain —
 * without it a power cut leaves a file that reads as empty. */
static void writer_sync(void)
{
    if (fflush(s_file) != 0 || fsync(fileno(s_file)) != 0) {
        writer_fail("sync");
    }
}

static void writer_stop(const char *reason)
{
    s_recording = false;

    ts_frame_t tf;
    while (s_file != NULL && xQueueReceive(s_frame_q, &tf, 0) == pdTRUE) {
        writer_write_frame(&tf);
    }
    if (s_file == NULL) {
        return;     /* a write failed while draining; already reported */
    }

    /* Footer comments make each capture self-documenting; readers skip ';'
     * lines. The dropped-frames line matches the logger's byte for byte so
     * the same grep works on both. Its absence marks a truncated capture. */
    fprintf(s_file, ";dropped-frames: %u (RX-to-writer queue overflow)\n",
            (unsigned)s_dropped);
    fprintf(s_file, ";closed-by: %s\n", reason);

    const bool ok = (fflush(s_file) == 0) && (fsync(fileno(s_file)) == 0);
    const bool closed = (fclose(s_file) == 0);
    s_file = NULL;
    free(s_io_buf);
    s_io_buf = NULL;

    if (!ok || !closed) {
        ESP_LOGE(TAG, "%u.trc: final write failed — the tail may be lost",
                 s_file_num);
        set_fault(true);
        return;
    }
    ESP_LOGI(TAG, "closed %u.trc: %u frames, %u dropped (%s)", s_file_num,
             (unsigned)s_file_frames, (unsigned)s_dropped, reason);
}

/* Card yanked mid-capture: nothing can be flushed, so release the handles
 * without pretending the tail was written. */
static void writer_abort(void)
{
    s_recording = false;
    ts_frame_t tf;
    while (xQueueReceive(s_frame_q, &tf, 0) == pdTRUE) {
    }
    if (s_file != NULL) {
        fclose(s_file);
        s_file = NULL;
    }
    free(s_io_buf);
    s_io_buf = NULL;
    ESP_LOGW(TAG, "card removed mid-capture: %u.trc keeps what was synced "
             "(%u frames written)", s_file_num, (unsigned)s_file_frames);
}

/* ---- Writer: the gate ---------------------------------------------------- */

static void gate_eval(void)
{
    can_signals_t sig;
    uint32_t now;
    sig_snapshot(&sig, &now);
    const sig_value_t *c = &sig.engine_cutoff;

    /* sig_snapshot() reads the clock before copying the signals, so a frame
     * decoded in between can carry last_ms == now + 1. Clamp rather than let
     * the unsigned subtraction wrap to ~49 days and read as "silent". */
    const int32_t age = (int32_t)(now - c->last_ms);
    const log_gate_obs_t obs = {
        .seen = c->seen,
        .cutoff = c->value != 0.0f,
        .age_ms = age < 0 ? 0u : (uint32_t)age,
    };

    const log_gate_event_t ev = log_gate_update(&s_gate, &obs, &s_gate_cfg);
    switch (ev) {
    case LOG_GATE_START:
        ESP_LOGI(TAG, "kill switch RUN — capture session %u",
                 (unsigned)s_gate.sessions);
        s_want_file = true;
        break;
    case LOG_GATE_STOP_CUTOFF:
    case LOG_GATE_STOP_SILENT: {
        const char *why = (ev == LOG_GATE_STOP_CUTOFF)
                              ? "kill switch CUTOFF"
                              : "bus silent (key off?)";
        s_want_file = false;
        if (s_file != NULL) {
            writer_stop(why);
        } else {
            ESP_LOGI(TAG, "capture session %u ended (%s)",
                     (unsigned)s_gate.sessions, why);
        }
        break;
    }
    case LOG_GATE_NONE:
    default:
        break;
    }

    if (s_gate.active && s_want_file && s_file == NULL) {
        s_want_file = false;
        writer_start();
    }
}

/* ---- Writer task ----------------------------------------------------------- */

static void writer_handle_card_inserted(void)
{
    ESP_LOGI(TAG, "microSD inserted");
    if (!s_sd_ok) {
        sd_bring_up();
    }
    /* Mid-ride insertion: the session is already running, so ask for a file
     * now rather than waiting for the next CUTOFF -> RUN. */
    if (s_gate.active && s_file == NULL) {
        s_want_file = true;
    }
}

static void writer_handle_card_removed(void)
{
    ESP_LOGI(TAG, "microSD removed");
    s_card_present = false;
    if (s_file != NULL) {
        writer_abort();
    }
    if (s_sd_ok) {
        sd_unmount();
        s_sd_ok = false;
    }
    set_fault(false);   /* a card fault leaves with the card */
}

static void writer_task(void *arg)
{
    (void)arg;
    sd_bring_up();

    uint32_t last_gate_ms = now_ms();
    for (;;) {
        /* Card events first, so they act promptly under a frame flood.
         *
         * Do NOT multiplex these two queues through a FreeRTOS queue set:
         * writer_start/stop/abort drain s_frame_q directly, and reading a
         * set member outside xQueueSelectFromSet() desyncs the set — the
         * logger firmware learned this the hard way (see logger_main.c). */
        log_cmd_t cmd;
        if (xQueueReceive(s_ctrl_q, &cmd, 0) == pdTRUE) {
            if (cmd == CMD_CARD_INSERTED) {
                writer_handle_card_inserted();
            } else {
                writer_handle_card_removed();
            }
            continue;
        }

        /* Short block: a quiet bus parks here, and the timeout keeps the
         * gate and sync running when no frames arrive at all — which is
         * exactly the key-off case the gate has to notice. */
        ts_frame_t tf;
        if (xQueueReceive(s_frame_q, &tf, pdMS_TO_TICKS(20)) == pdTRUE) {
            writer_write_frame(&tf);
        }

        const uint32_t t = now_ms();
        if (t - last_gate_ms >= GATE_PERIOD_MS) {
            last_gate_ms = t;
            gate_eval();
        }
        if (s_file != NULL && t - s_last_sync_ms >= CONFIG_CHMBL_TRC_LOG_SYNC_MS) {
            s_last_sync_ms = t;
            writer_sync();
        }
    }
}

/* ---- Card-detect task ------------------------------------------------------ */

/* Poll DET_A and post debounced transitions to the writer, which owns
 * everything that touches the card. */
static void card_detect_task(void *arg)
{
    (void)arg;

    bool committed = sd_card_present();
    bool last_raw = committed;
    int agree = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CARD_POLL_MS));

        const bool raw = sd_card_present();
        if (raw != last_raw) {
            last_raw = raw;     /* still bouncing — restart the count */
            agree = 0;
            continue;
        }
        if (raw == committed) {
            agree = 0;
            continue;
        }
        if (++agree < CARD_STABLE_POLLS) {
            continue;
        }

        committed = raw;
        agree = 0;
        const log_cmd_t cmd = raw ? CMD_CARD_INSERTED : CMD_CARD_REMOVED;
        xQueueSend(s_ctrl_q, &cmd, 0);
    }
}

/* ---- Public API -------------------------------------------------------------- */

void trc_log_init(void)
{
    s_frame_q = xQueueCreate(CONFIG_CHMBL_TRC_LOG_QUEUE_LEN, sizeof(ts_frame_t));
    s_ctrl_q = xQueueCreate(CTRL_QUEUE_LEN, sizeof(log_cmd_t));
    if (s_frame_q == NULL || s_ctrl_q == NULL) {
        ESP_LOGE(TAG, "no RAM for the frame queue — ride logging disabled");
        set_fault(true);
        return;
    }

    sd_det_init();
    log_gate_init(&s_gate);

    /* Priorities: logging must never cost the brake light. The writer sits
     * BELOW the 50 Hz FSM tick (7) and the ESP-NOW heartbeat (5) — under a
     * frame flood it is always ready, and at equal priority it would
     * round-robin with the heartbeat. It can afford to wait: the frame queue
     * holds ~0.7 s. CAN-RX (10) stays above everything so frames are queued
     * promptly; card-detect is slower still. */
    xTaskCreate(writer_task, "trc_writer", 4096, NULL, 4, NULL);
    xTaskCreate(card_detect_task, "card_detect", 2560, NULL, 3, NULL);

    ESP_LOGI(TAG, "ride logging armed: records while the kill switch is in RUN "
             "(close after %d ms silent, sync every %d ms)",
             CONFIG_CHMBL_TRC_LOG_CLOSE_MS, CONFIG_CHMBL_TRC_LOG_SYNC_MS);
}

void trc_log_frame(const twai_message_t *msg)
{
    if (!s_recording) {
        return;
    }

    ts_frame_t tf = {
        .t_us = esp_timer_get_time(),
        .frame = {
            .id       = msg->identifier,
            .extended = msg->extd,
            .rtr      = msg->rtr,
            .dlc      = msg->data_length_code,
        },
    };
    memcpy(tf.frame.data, msg->data, sizeof(tf.frame.data));

    if (xQueueSend(s_frame_q, &tf, 0) != pdTRUE) {
        s_dropped++;
    }
}

void trc_log_get_status(trc_log_status_t *out)
{
    out->enabled = true;
    out->card_present = s_card_present;
    out->mounted = s_sd_ok;
    out->gate_active = s_gate.active;
    out->recording = s_recording;
    out->fault = s_fault;
    out->file_num = s_file_num;
    out->file_frames = s_file_frames;
    out->dropped = s_dropped;
    out->sessions = s_gate.sessions;
    out->files = s_files;
}

#else /* !CONFIG_CHMBL_TRC_LOG */

void trc_log_init(void)
{
}

void trc_log_frame(const twai_message_t *msg)
{
    (void)msg;
}

void trc_log_get_status(trc_log_status_t *out)
{
    memset(out, 0, sizeof(*out));
}

#endif /* CONFIG_CHMBL_TRC_LOG */
