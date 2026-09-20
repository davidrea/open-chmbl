/*
 * open-chmbl CAN data logger — firmware for the custom logger PCB
 * (ESP32-S3-WROOM-1-N4; logger/hardware/logger.kicad_sch).
 *
 * Captures ALL CAN traffic (no filtering) in listen-only mode and writes it to
 * the microSD as PCAN .trc (v2.1) ASCII files. A single debounced pushbutton
 * (IO6, on the operator's pod at J4) toggles recording: each start opens a new
 * N.trc (N an increasing integer), each stop closes it. Three LEDs show logger
 * state, bus liveness and card activity at a glance (status_led.h); the full
 * operations log goes to the console over native USB.
 *
 * This is the DE-07 "ride logger" — a self-contained replacement for the
 * Raspberry Pi rig (see docs/can-profiles.md §3). Power-loss robustness is
 * intentionally out of scope; card removal is handled only to the extent of
 * shutting the capture down cleanly rather than wedging the writer.
 *
 * SILENT MODE IS A HARDWARE PROPERTY HERE. R16 pulls the TCAN330's S pin
 * (IO35 — U1 pin 28) up to 3V3, and the firmware never drives that pin: it is
 * configured high-impedance and left there. The transceiver is therefore silent
 * from power-on, before app_main runs, and stays silent through a crash. The
 * TWAI controller is *also* held in listen-only mode. Both halves of the
 * project's golden rule — never disturb a live vehicle bus — are in force.
 *
 * Tasks:
 *   CAN-RX   — twai_receive(); flags bus liveness for D5, and while recording
 *              timestamps each frame and queues it; counts drops if full.
 *   Writer   — owns the microSD, the open file and the recording state. Serves
 *              the control queue first (so button toggles and card events act
 *              promptly), then drains the frame queue.
 *   Card-det — polls DET_A (IO8) and posts insert/remove to the control queue.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
#define CTRL_QUEUE_LEN  8

/* Full-buffering size for the .trc stream. The default newlib buffer (~128 B)
 * flushes an SD block write every few frames; at motorcycle bus loads (~1500
 * frames/s here) those tiny writes cap the writer near ~400 frames/s and the
 * RX-to-writer queue overflows. A large buffer batches writes into full SD
 * clusters, lifting the ceiling far above the offered load. */
#define TRC_IO_BUF_SIZE (32 * 1024)

/* DET_A polling. 50 ms sampling with a 3-sample agreement filter debounces the
 * mechanical detect switch in ~150 ms — far below human insert/remove speed,
 * and slow enough that a bouncing contact can't spam the control queue. */
#define CARD_POLL_MS      50
#define CARD_STABLE_POLLS 3

/* A CAN frame stamped with its receive time (esp_timer microseconds). */
typedef struct {
    int64_t     t_us;
    trc_frame_t frame;
} ts_frame_t;

typedef enum {
    CMD_TOGGLE,
    CMD_CARD_INSERTED,
    CMD_CARD_REMOVED,
} logger_cmd_t;

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

/* ---- Indicator state ------------------------------------------------------ */

/* Latched once the CAN silent pin reads low. That is the one fault the operator
 * must not be able to lose track of — everything else here is recoverable, but a
 * transceiver that might not be silent stays reported until the board is power
 * cycled. Without the latch a successful microSD mount a few lines later in
 * app_main would quietly reset the indicator to idle. */
static bool s_silent_fault;

/* Set the indicator state, honouring the latch. Use this rather than calling
 * status_led_set() directly from anywhere in this file. */
static void logger_set_state(led_state_t state)
{
    status_led_set(s_silent_fault ? LED_STATE_ERROR : state);
}

/* ---- CAN transceiver silent pin ------------------------------------------ */

/* Park the TCAN330's S pin (IO35) high-impedance so R16's pull-up to 3V3 holds
 * the transceiver in silent mode. We never drive this pin — see the file header.
 *
 * Both internal pulls are disabled on purpose: an internal pull-up would hold S
 * high even if R16 were missing or unstuffed, hiding exactly the board fault
 * that would let us transmit onto a live bus. Same reasoning as DET_A in
 * BRINGUP.md §4.5, but the stakes are higher here. */
static void can_silent_pin_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_LOGGER_CAN_SILENT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    /* Read it back: high means R16 is doing its job. A low reading means the
     * transceiver is NOT silent and could ACK a live bus — say so loudly. */
    const int level = gpio_get_level(CONFIG_LOGGER_CAN_SILENT_GPIO);
    ui_log_line("CAN silent pin IO%d = %d (hi-z, R16 pull-up)",
                CONFIG_LOGGER_CAN_SILENT_GPIO, level);
    if (level != 1) {
        ui_log_line("WARNING: silent pin reads LOW — transceiver may not be silent!");
        ui_log_line("  check R16; do NOT connect to a vehicle bus until resolved");
        s_silent_fault = true;
        logger_set_state(LED_STATE_ERROR);
    }
}

/* ---- microSD -------------------------------------------------------------- */

static sdmmc_card_t *s_card;

/* DET_A is active-low: R17 pulls it up, a seated card pulls J5 pin10 to GND. */
static bool sd_card_present(void)
{
    return gpio_get_level(CONFIG_LOGGER_SD_DET_GPIO) == 0;
}

static void sd_det_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_LOGGER_SD_DET_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,   /* R17 does this — see Kconfig */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

/* Mount the microSD as FAT at TRC_DIR over the S3's SDMMC host, on the pins the
 * logger PCB routes (BRINGUP.md §0). Returns true on success.
 *
 * Card-detect is NOT handed to the driver (slot.cd stays NC) even though it
 * supports it: we gate on DET_A ourselves so a missing card produces our own
 * log line and LED state rather than an opaque mount error. */
static bool sd_mount(void)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();               /* SDMMC slot 1 */
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();

    /* The S3 routes SDMMC through the GPIO matrix, so the pins are explicit
     * rather than fixed IO-MUX assignments as they were on the ESP32. */
    slot.clk = CONFIG_LOGGER_SD_CLK_GPIO;
    slot.cmd = CONFIG_LOGGER_SD_CMD_GPIO;
    slot.d0  = CONFIG_LOGGER_SD_D0_GPIO;
#if CONFIG_LOGGER_SD_BUS_WIDTH_4
    slot.d1  = CONFIG_LOGGER_SD_D1_GPIO;
    slot.d2  = CONFIG_LOGGER_SD_D2_GPIO;
    slot.d3  = CONFIG_LOGGER_SD_D3_GPIO;
    slot.width = 4;
#else
    slot.width = 1;
#endif

    /* No SDMMC_SLOT_FLAG_INTERNAL_PULLUP: the board carries proper external
     * pull-ups (R7–R11), and the internal ones are documented as insufficient
     * anyway. Enabling them would only mask an unstuffed resistor. */

    esp_err_t err = esp_vfs_fat_sdmmc_mount(TRC_DIR, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ui_log_line("mount failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static void sd_unmount(void)
{
    esp_vfs_fat_sdcard_unmount(TRC_DIR, s_card);
    s_card = NULL;
}

/* ---- microSD file bookkeeping -------------------------------------------- */

/* One pass over the card root: log every file found (the boot-time inventory
 * the operator sees on the console), and set s_next_num = max(N in N.trc) + 1
 * so a new capture never overwrites an old one. */
static void sd_scan_and_list(void)
{
    DIR *dir = opendir(TRC_DIR);
    if (dir == NULL) {
        ui_log_line("cannot list %s (opendir failed)", TRC_DIR);
        s_next_num = 1;
        return;
    }

    unsigned max_n = 0;
    int total = 0;
    int trc = 0;

    ui_log_line("card contents:");
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        total++;

        char path[280];
        snprintf(path, sizeof(path), "%s/%s", TRC_DIR, ent->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            ui_log_line("  %-14s %lu bytes", ent->d_name, (unsigned long)st.st_size);
        } else {
            ui_log_line("  %-14s (size unavailable)", ent->d_name);
        }

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

    if (total == 0) {
        ui_log_line("  (empty)");
    }
    s_next_num = max_n + 1;
    ui_log_line("%d file(s), %d .trc capture(s), highest #%u", total, trc, max_n);
    ui_log_line("next file number = %u", s_next_num);
}

/* Mount and inventory the card, reporting each step. Shared by the boot path
 * and the runtime card-insert path so both log identically. */
static void sd_bring_up(void)
{
    if (!sd_card_present()) {
        ui_log_line("microSD: NO CARD DETECTED (DET_A/IO%d high)",
                    CONFIG_LOGGER_SD_DET_GPIO);
        s_sd_ok = false;
        logger_set_state(LED_STATE_ERROR);
        return;
    }

    ui_log_line("microSD: card detected (DET_A/IO%d low)", CONFIG_LOGGER_SD_DET_GPIO);

    s_sd_ok = sd_mount();
    if (!s_sd_ok) {
        ui_log_line("microSD: filesystem mount FAILED");
        logger_set_state(LED_STATE_ERROR);
        return;
    }

    ui_log_line("microSD: filesystem mounted at %s", TRC_DIR);
    if (s_card != NULL) {
        /* The width actually negotiated, not the width the card is capable of:
         * log_bus_width is log2(capability), so reporting it directly would
         * claim "2-bit" for a healthy 4-bit link and, worse, would look
         * identical whether or not the DAT1-3 lines came up. BRINGUP.md §6.2/§6.3
         * turn on telling those two apart. This mirrors sdmmc_card_print_info(). */
        const int bus_width = s_card->is_mmc ? (1 << s_card->log_bus_width)
                                             : (s_card->ssr.cur_bus_width ? 4 : 1);
        ui_log_line("  %s, %llu MB, %d-bit bus", s_card->cid.name,
                    ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) / (1024 * 1024),
                    bus_width);
    }
    sd_scan_and_list();
    logger_set_state(LED_STATE_IDLE);
}

/* ---- Writer task: owns the file + recording state ------------------------ */

static FILE   *s_file;
static char   *s_io_buf;      /* full-buffering block for s_file (see setvbuf) */
static uint32_t s_msgnr;
static int64_t  s_t0_us;      /* timestamp of the first frame in this file */
static uint32_t s_file_frames;

/* Bytes buffered since the last flash of D6. The stream is fully buffered
 * (TRC_IO_BUF_SIZE), so frames do not each cause a card write — flashing per
 * frame would report the wrong thing and, at ~1500 frames/s, would peg D6 solid.
 * Counting up to a buffer's worth pulses the LED roughly when newlib actually
 * pushes a block to the card. */
static size_t s_bytes_buffered;

static void note_bytes_written(size_t n)
{
    s_bytes_buffered += n;
    if (s_bytes_buffered >= TRC_IO_BUF_SIZE) {
        s_bytes_buffered -= TRC_IO_BUF_SIZE;
        status_led_sd_activity();
    }
}

static void writer_start_recording(void)
{
    if (!sd_card_present()) {
        ui_log_line("cannot record: no microSD card");
        logger_set_state(LED_STATE_ERROR);
        return;
    }
    if (!s_sd_ok) {
        ui_log_line("cannot record: microSD not mounted");
        logger_set_state(LED_STATE_ERROR);
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
        logger_set_state(LED_STATE_ERROR);
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

    /* Reset the write accounting before the first byte goes out, so the header
     * counts toward the next activity flash like everything else. */
    s_bytes_buffered = 0;
    status_led_sd_activity();   /* the file create itself hits the card */

    char header[512];
    int hn = trc_format_header(header, sizeof(header));
    if (hn > 0) {
        fwrite(header, 1, (size_t)hn, s_file);
        note_bytes_written((size_t)hn);
    }

    ui_log_line("opened file %u.trc", s_next_num);
    s_msgnr = 0;
    s_file_frames = 0;
    s_t0_us = INT64_MIN;
    s_dropped = 0;

    s_recording = true;
    s_next_num++;
    ui_log_line("recording started");
    logger_set_state(LED_STATE_RECORDING);
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
        note_bytes_written((size_t)n + 1);
    }
}

static void writer_stop_recording(void)
{
    /* Stop accepting new frames, then flush what is already queued for this
     * file. The TWAI controller keeps running — it is what feeds the bus-liveness
     * indicator (D5) while idle. */
    s_recording = false;

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
        status_led_sd_activity();   /* the final flush + close */
    }
    ui_log_line("recording stopped");
    logger_set_state(LED_STATE_IDLE);
    ui_log_line("file closed: %u frames%s", (unsigned)s_file_frames,
                s_dropped ? " (drops!)" : "");
    if (s_dropped) {
        ui_log_line("  dropped %u frames", (unsigned)s_dropped);
    }
}

/* Card yanked mid-capture. Nothing can be flushed — the file is whatever
 * reached the card before the contacts opened — so just release the handles
 * without pretending the tail was written. */
static void writer_abort_recording(void)
{
    s_recording = false;

    ts_frame_t tf;
    while (xQueueReceive(s_frame_q, &tf, 0) == pdTRUE) {
    }

    if (s_file != NULL) {
        fclose(s_file);
        s_file = NULL;
        free(s_io_buf);
        s_io_buf = NULL;
    }
    ui_log_line("recording ABORTED: card removed mid-capture");
    ui_log_line("  %u frames were written; the tail of the file is lost",
                (unsigned)s_file_frames);
}

static void writer_handle_toggle(void)
{
    if (s_recording) {
        writer_stop_recording();
    } else {
        writer_start_recording();
    }
}

static void writer_handle_card_inserted(void)
{
    ui_log_line("microSD: card inserted");
    if (s_sd_ok) {
        return;     /* already mounted; nothing to do */
    }
    sd_bring_up();
}

static void writer_handle_card_removed(void)
{
    ui_log_line("microSD: card removed");
    if (s_recording) {
        writer_abort_recording();
    }
    if (s_sd_ok) {
        sd_unmount();
        s_sd_ok = false;
        ui_log_line("microSD: unmounted");
    }
    logger_set_state(LED_STATE_ERROR);
}

static void writer_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* Service control commands first so a toggle or a card event is always
         * acted on promptly, even while frames are flooding in. Then block
         * briefly on the frame queue: a silent-but-recording bus parks here
         * (letting lower-prio tasks run) while a control press is still noticed
         * within the timeout.
         *
         * NOTE: do not multiplex these two queues through a FreeRTOS queue set.
         * writer_start/stop_recording drain s_frame_q directly, and reading a
         * queue-set member outside xQueueSelectFromSet() desyncs the set's token
         * accounting -- which stranded/batched button toggles under heavy frame
         * load and left the status LED out of sync with the recording state. */
        logger_cmd_t cmd;
        if (xQueueReceive(s_ctrl_q, &cmd, 0) == pdTRUE) {
            switch (cmd) {
            case CMD_TOGGLE:         writer_handle_toggle();        break;
            case CMD_CARD_INSERTED:  writer_handle_card_inserted(); break;
            case CMD_CARD_REMOVED:   writer_handle_card_removed();  break;
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
        /* The controller runs continuously, not just while recording: D5 reports
         * whether the bus is alive, and that has to be answerable *before* you
         * press record — walking back to the bike to find out the harness was
         * unplugged is the failure this indicator exists to prevent.
         *
         * Receiving at idle is safe on this board in a way it was not on the
         * WROVER-KIT rig: RX comes from the TCAN330's push-pull RXD output,
         * which idles recessive-high with no bus attached, so there is no
         * floating pin to flood the task. Frames received while not recording
         * are counted for the indicator and dropped on the floor.
         *
         * Finite timeout: a silent bus blocks here (letting the idle task run)
         * and the task stays responsive — never a tight spin. */
        twai_message_t msg;
        const esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(100));
        if (err == ESP_ERR_TIMEOUT) {
            continue;   /* no traffic; D5's hold-off will decay */
        }
        if (err != ESP_OK) {
            /* Controller not running (not yet installed at boot, or twai_start
             * failed). twai_receive returns ESP_ERR_INVALID_STATE *immediately*
             * in that case, with no timeout — so falling straight back into the
             * loop would be a tight spin at priority 6, starving the writer and
             * the idle task and taking the watchdog with it. Back off instead. */
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        status_led_can_activity();

        if (!s_recording) {
            continue;
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

/* ---- Card-detect task ---------------------------------------------------- */

/* Poll DET_A and post transitions to the writer, which owns everything that
 * touches the card. Polled rather than interrupt-driven: the detect contact is
 * mechanical and bounces, and at these timescales an ISR buys nothing. */
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
            last_raw = raw;     /* still bouncing — restart the agreement count */
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
        logger_cmd_t cmd = raw ? CMD_CARD_INSERTED : CMD_CARD_REMOVED;
        xQueueSend(s_ctrl_q, &cmd, 0);
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
        .active_level = 0,       /* button pulls BTN_SIG to GND when pressed */
        .enable_power_save = false,
        /* Rely on the board's R5 pull-up rather than an internal one, so an
         * unstuffed R5 shows up as spurious toggles during bring-up instead of
         * being silently papered over. Debounce is the component's own. */
        .disable_pull = true,
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
    /* Deep enough to ride out a brief SD write stall without the driver
     * dropping frames in the ISR (a loss path s_dropped can't see). ~128 frames
     * is ~85 ms of slack at the ~1500 frames/s peaks seen on this bus, for a
     * negligible ~2 KB of RAM. */
    g.rx_queue_len = 128;

    twai_timing_config_t t = logger_timing();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();   /* no filtering */

    ESP_ERROR_CHECK(twai_driver_install(&g, &t, &f));

    /* Start the controller now and leave it running for the life of the app.
     * It is the source of the bus-liveness indication on D5, which has to work
     * while idle; in listen-only mode, with the transceiver held silent by R16,
     * running it costs nothing on the bus. */
    esp_err_t err = twai_start();
    if (err != ESP_OK) {
        ui_log_line("CAN start FAILED (%s)", esp_err_to_name(err));
        logger_set_state(LED_STATE_ERROR);
    }
}

/* ---- app_main ------------------------------------------------------------ */

void app_main(void)
{
    status_led_init();

    ui_log_line("booted — logger PCB (ESP32-S3)");
    ui_log_line("CAN %s %s", logger_bitrate_str(), logger_mode_str());

    /* Before anything else that could touch the bus. */
    can_silent_pin_init();

    sd_det_init();
    sd_bring_up();

    s_frame_q = xQueueCreate(RX_QUEUE_LEN, sizeof(ts_frame_t));
    s_ctrl_q  = xQueueCreate(CTRL_QUEUE_LEN, sizeof(logger_cmd_t));

    /* Install and start the controller BEFORE the RX task exists, so that task
     * never runs against a stopped controller. It defends against that anyway
     * (see can_rx_task), but not having the window is better than handling it. */
    can_init();

    xTaskCreate(writer_task, "trc_writer", 4096, NULL, 5, NULL);
    xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 6, NULL);
    xTaskCreate(card_detect_task, "card_detect", 2560, NULL, 3, NULL);

    button_init();

    ui_log_line("ready: press to start/stop");
}
