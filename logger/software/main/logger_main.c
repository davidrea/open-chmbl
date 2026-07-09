/*
 * open-chmbl CAN data logger — custom ESP32-S3 logger board firmware.
 *
 * Captures ALL CAN traffic (no filtering) in listen-only mode and writes it to
 * the on-board microSD as PCAN .trc (v2.1) ASCII files. A single debounced
 * pushbutton toggles recording: each start opens a new N.trc (N an increasing
 * integer), each stop closes it. The status LED (status_led.h) shows
 * idle/recording/error at a glance; the full operations log goes to serial.
 *
 * Target is the custom logger PCB (see logger/hardware/): ESP32-S3-WROOM-1-N8,
 * onboard TCAN330 CAN transceiver (with its silent-mode S pin on a GPIO), and a
 * microSD wired to the SoC's native SDMMC host (full 4-bit bus). Pin map comes
 * from the schematic; the retired ESP-WROVER-KIT bring-up rig used different
 * GPIOs (see the Kconfig defaults and logger/software/README.md).
 *
 * This is the DE-07 "ride logger" — a self-contained ESP32 replacement for the
 * Raspberry Pi rig (see docs/can-profiles.md §3). Power-loss / card-removal
 * robustness is intentionally out of scope.
 *
 * Tasks:
 *   CAN-RX  — twai_receive(); timestamps each frame and queues it (only while
 *             recording); counts drops if the queue is full.
 *   Writer  — owns the open file and the recording state; services the
 *             button-command queue first (so toggles act promptly), then drains
 *             the frame queue, formats each frame and writes it, and opens/closes
 *             files on toggle.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/twai.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"

#include "button_gpio.h"
#include "iot_button.h"

#include "status_led.h"
#include "trc_format.h"
#include "ui_log.h"

#define TRC_DIR         "/sdcard"
#define RX_QUEUE_LEN    CONFIG_LOGGER_RX_QUEUE_LEN
#define CTRL_QUEUE_LEN  4

/* Full-buffering size for the .trc stream. The default newlib buffer (~128 B)
 * flushes an SD block write every few frames; at motorcycle bus loads (~1500
 * frames/s here) those tiny writes cap the writer near ~400 frames/s and the
 * RX-to-writer queue overflows. A large buffer batches writes into full SD
 * clusters, lifting the ceiling far above the offered load. */
#define TRC_IO_BUF_SIZE (32 * 1024)

/* A CAN frame stamped with its receive time (esp_timer microseconds). */
typedef struct {
    int64_t     t_us;
    trc_frame_t frame;
} ts_frame_t;

typedef enum { CMD_TOGGLE } logger_cmd_t;

static QueueHandle_t    s_frame_q;
static QueueHandle_t    s_ctrl_q;

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

/* ---- microSD -------------------------------------------------------------- */

static sdmmc_card_t *s_card;

/* Mount the microSD as FAT at TRC_DIR over the ESP32-S3 SDMMC host (full 4-bit
 * bus, J5). Unlike the classic ESP32 (fixed SDMMC IO-MUX pins), the ESP32-S3
 * routes the SDMMC host through the GPIO matrix, so every bus pin must be
 * assigned explicitly from the board's routing (Kconfig defaults, see
 * ../hardware/README.md §3.2). Card-detect (DET_A, GPIO8 on this board) is left
 * unused: this firmware has no hot-plug path, so gating the mount on a DET
 * polarity we can't verify on the bench would only add a failure mode.
 * Returns true on success. */
static bool sd_mount(void)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();               /* SDMMC slot 1 */
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = CONFIG_LOGGER_SD_CLK_GPIO;
    slot.cmd = CONFIG_LOGGER_SD_CMD_GPIO;
    slot.d0  = CONFIG_LOGGER_SD_D0_GPIO;
    slot.d1  = CONFIG_LOGGER_SD_D1_GPIO;
    slot.d2  = CONFIG_LOGGER_SD_D2_GPIO;
    slot.d3  = CONFIG_LOGGER_SD_D3_GPIO;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    return esp_vfs_fat_sdmmc_mount(TRC_DIR, &host, &slot, &mount_cfg, &s_card) == ESP_OK;
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
static char   *s_io_buf;      /* full-buffering block for s_file (see setvbuf) */
static uint32_t s_msgnr;
static int64_t  s_t0_us;      /* timestamp of the first frame in this file */
static uint32_t s_file_frames;

static void writer_start_recording(void)
{
    if (!s_sd_ok) {
        ui_log_line("cannot record: no microSD");
        status_led_set(LED_STATE_ERROR);
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
        status_led_set(LED_STATE_ERROR);
        return;
    }

    /* Batch writes into full SD clusters (see TRC_IO_BUF_SIZE). Must be set
     * before any I/O on the stream, and the buffer must outlive it (freed in
     * writer_stop_recording). If the allocation fails, fall back to the default
     * small buffer rather than aborting the recording. */
    s_io_buf = malloc(TRC_IO_BUF_SIZE);
    if (s_io_buf != NULL) {
        setvbuf(s_file, s_io_buf, _IOFBF, TRC_IO_BUF_SIZE);
    } else {
        ui_log_line("warn: no I/O buffer, drops likely");
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

    /* Bring the CAN controller live only now, and flip the recording flag only
     * after it is running so the RX task never calls twai_receive on a stopped
     * controller. */
    esp_err_t err = twai_start();
    if (err != ESP_OK) {
        ui_log_line("CAN start FAILED (%s)", esp_err_to_name(err));
        status_led_set(LED_STATE_ERROR);
        fclose(s_file);
        s_file = NULL;
        free(s_io_buf);
        s_io_buf = NULL;
        return;
    }
    s_recording = true;
    s_next_num++;
    ui_log_line("recording started");
    status_led_set(LED_STATE_RECORDING);
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
    /* Stop accepting new frames and take the CAN controller offline, then flush
     * any frames already queued for this file. */
    s_recording = false;
    twai_stop();

    ts_frame_t tf;
    while (xQueueReceive(s_frame_q, &tf, 0) == pdTRUE) {
        writer_write_frame(&tf);
    }

    if (s_file != NULL) {
        /* Footer comment so every capture is self-documenting: readers skip
         * lines starting with ';', and "dropped-frames: N" is greppable. A
         * clean capture records 0, so absence of the line means an older/
         * truncated file, not a lossless one. */
        char footer[96];
        int fn = snprintf(footer, sizeof(footer),
                          ";dropped-frames: %u (RX-to-writer queue overflow)\n",
                          (unsigned)s_dropped);
        if (fn > 0) {
            fwrite(footer, 1, (size_t)fn, s_file);
        }
        fflush(s_file);
        fclose(s_file);
        s_file = NULL;
        free(s_io_buf);
        s_io_buf = NULL;
    }
    ui_log_line("recording stopped");
    status_led_set(LED_STATE_IDLE);
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
        /* Service the (rare) button command first so a toggle is always acted on
         * promptly, even while frames are flooding in. Then block briefly on the
         * frame queue: a silent-but-recording bus parks here (letting lower-prio
         * tasks run) while a control press is still noticed within the timeout.
         *
         * NOTE: do not multiplex these two queues through a FreeRTOS queue set.
         * writer_start/stop_recording drain s_frame_q directly, and reading a
         * queue-set member outside xQueueSelectFromSet() desyncs the set's token
         * accounting -- which stranded/batched button toggles under heavy frame
         * load and left the status LED out of sync with the recording state. */
        logger_cmd_t cmd;
        if (xQueueReceive(s_ctrl_q, &cmd, 0) == pdTRUE) {
            if (cmd == CMD_TOGGLE) {
                writer_handle_toggle();
            }
            continue;
        }

        ts_frame_t tf;
        if (xQueueReceive(s_frame_q, &tf, pdMS_TO_TICKS(20)) == pdTRUE) {
            writer_write_frame(&tf);
        }
    }
}

/* ---- CAN-RX task --------------------------------------------------------- */

static void can_rx_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* The TWAI controller only runs while recording (see
         * writer_start/stop_recording). When stopped, block cheaply so the idle
         * task runs and the watchdog stays fed, and don't call twai_receive on a
         * stopped controller. */
        if (!s_recording) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        /* Finite timeout: a silent-but-recording bus blocks here (letting the
         * idle task run) and a stop stays responsive — never a tight spin. */
        twai_message_t msg;
        if (twai_receive(&msg, pdMS_TO_TICKS(100)) != ESP_OK) {
            continue;   /* timeout, or the controller was just stopped */
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

/* Drive the CAN transceiver's silent-mode (S) pin (TCAN330 U2 pin 8) to match
 * the configured mode: high forces the transceiver RX-only in hardware (its TX
 * driver is disabled) for listen-only, low allows normal TX/ACK. This is a
 * hardware failsafe on top of the TWAI controller's own listen-only setting;
 * the S pin is a feature of the custom board absent on the WROVER-KIT rig.
 *
 * The default GPIO (45) is an ESP32-S3 strapping pin, but its strap is sampled
 * once at reset before any of our code runs, so driving it as a normal output
 * afterward is safe (see ../hardware/README.md §5). Skipped when the pin is
 * configured to -1 (no S pin wired, e.g. the external-transceiver bring-up rig). */
static void can_transceiver_init(void)
{
#if CONFIG_LOGGER_CAN_SILENT_GPIO >= 0
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_LOGGER_CAN_SILENT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
#if CONFIG_LOGGER_CAN_LISTEN_ONLY
    gpio_set_level(CONFIG_LOGGER_CAN_SILENT_GPIO, 1);   /* silent: RX-only */
#else
    gpio_set_level(CONFIG_LOGGER_CAN_SILENT_GPIO, 0);   /* normal: TX/ACK   */
#endif
#endif
}

static void can_init(void)
{
    /* Put the transceiver into the right RX-only/normal state before the
     * controller is installed, so the bus is never driven unexpectedly. */
    can_transceiver_init();

    /* Select the mode into a variable first: preprocessor directives inside a
     * function-like macro's argument list are undefined behavior. */
#if CONFIG_LOGGER_CAN_LISTEN_ONLY
    const twai_mode_t mode = TWAI_MODE_LISTEN_ONLY;
#else
    const twai_mode_t mode = TWAI_MODE_NORMAL;
#endif
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        CONFIG_LOGGER_CAN_TX_GPIO, CONFIG_LOGGER_CAN_RX_GPIO, mode);
    /* Deep enough to ride out a brief SD write stall without the driver
     * dropping frames in the ISR (a loss path s_dropped can't see). ~128 frames
     * is ~85 ms of slack at the ~1500 frames/s peaks seen on this bus, for a
     * negligible ~2 KB of RAM. */
    g.rx_queue_len = 128;

    twai_timing_config_t t = logger_timing();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();   /* no filtering */

    /* Install the driver but leave the controller STOPPED. It is started only
     * while recording (writer_start_recording) so a disconnected/floating RX
     * pin can't flood the RX task at idle and starve the task watchdog. */
    ESP_ERROR_CHECK(twai_driver_install(&g, &t, &f));
}

/* ---- app_main ------------------------------------------------------------ */

void app_main(void)
{
    status_led_init();

    ui_log_line("booted");
    ui_log_line("CAN %s %s", logger_bitrate_str(), logger_mode_str());

    s_sd_ok = sd_mount();
    if (s_sd_ok) {
        unsigned highest = 0;
        int found = scan_existing_files(&highest);
        ui_log_line("filesystem mounted");
        ui_log_line("listed %d .trc file(s), max #%u", found, highest);
        ui_log_line("next file number = %u", s_next_num);
    } else {
        ui_log_line("microSD mount FAILED");
        status_led_set(LED_STATE_ERROR);
    }

    s_frame_q = xQueueCreate(RX_QUEUE_LEN, sizeof(ts_frame_t));
    s_ctrl_q  = xQueueCreate(CTRL_QUEUE_LEN, sizeof(logger_cmd_t));

    xTaskCreate(writer_task, "trc_writer", 4096, NULL, 5, NULL);
    xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 6, NULL);

    can_init();
    button_init();

    ui_log_line("ready: press to start/stop");
}
