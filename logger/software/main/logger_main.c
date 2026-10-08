/*
 * open-chmbl CAN data logger + DE-09 brake-light preview — firmware for the
 * custom logger PCB (ESP32-S3-WROOM-1-N4; logger/hardware/logger.kicad_sch).
 *
 * This is the RIDE-VALIDATION build. It does two things at once:
 *
 *  1. Captures ALL CAN traffic (no filtering) in listen-only mode to the
 *     microSD as PCAN .trc (v2.1) files, exactly as before — but recording is
 *     now FULLY AUTOMATIC and silent, gated on the engine cutoff (kill) switch
 *     rather than on a pushbutton. See "Recording policy" below.
 *
 *  2. Runs the real DE-09 braking state machine (components/brake_fsm) against
 *     the real decoded signals (components/chmbl_can) at 50 Hz, and lights the
 *     REMOTE LED on J4 pin 2 (IO18, via Q1) whenever the firmware would be
 *     commanding the rider-side brake light ON. See fsm_preview.h.
 *
 *     There is NO ESP-NOW in this build. The LED stands in for the radio: it
 *     lets the owner ride with the logger, watch the FSM's real decision on a
 *     panel-mount LED, and afterwards read the .trc plus the console transition
 *     log to see exactly why it decided that.
 *
 * RECORDING POLICY (automatic, silent)
 *   The gate signal is `engine_cutoff` from the profile: 0x121 bit 30 set AND
 *   0x121 byte 6 == 0x28. 1.0 means the kill switch is ASSERTED (STOP), 0.0
 *   means RUN.
 *
 *     start a new N.trc  when the gate goes to RUN. That covers both the bike
 *                        being started with the switch already at RUN (the first
 *                        valid RUN seen after boot, or after the bus comes back)
 *                        and a cutoff -> RUN transition mid-session.
 *     close the file     on RUN -> cutoff, and on BUS SILENCE — no 0x121 for
 *                        CONFIG_LOGGER_BUS_SILENCE_MS, which is what ignition-off
 *                        looks like from here.
 *
 *   Because nothing presses a button to end a file cleanly and the board can
 *   lose 12 V at any instant, the writer flushes and fsync()s on an interval and
 *   a byte budget (CONFIG_LOGGER_FLUSH_*), so a yanked-power ride still yields a
 *   readable trace. The large TRC_IO_BUF_SIZE batching stays for throughput —
 *   the flush only bounds the window of loss.
 *
 * NO LED STATUS FOR LOGGING. "Silent" means no LED, not no logging: the console
 * operations log is unchanged and is now the only diagnostic surface. The
 * onboard D5/D6 are invisible inside the sealed enclosure and are left unused
 * by default (fault_led.h has an opt-in fatal-error blink for bench work).
 *
 * THE PUSHBUTTON IS NOT PART OF THE RECORDING PATH in this mode. Its Kconfig pin
 * (CONFIG_LOGGER_BUTTON_GPIO) is retained and documented as unused rather than
 * deleted, and the iot_button dependency is gone.
 *
 * SILENT MODE IS BELT AND BRACES. R16 pulls the TCAN330's S pin (IO35 — U1
 * pin 28) up to 3V3, and S high = silent: the transceiver receives but its
 * driver is disabled, so it cannot put a dominant bit — not even an ACK — on the
 * bus. The firmware reads that pin back as a high-impedance input FIRST (so a
 * missing R16 is still detected and reported, not masked), then drives it high
 * itself and leaves it there. The TWAI controller is *also* held in listen-only
 * mode. Both halves of the project's golden rule — never disturb a live vehicle
 * bus — are in force, and neither depends on the other.
 *
 * Tasks:
 *   CAN-RX    — twai_receive(); feeds the decode tap with EVERY frame, and while
 *               recording timestamps each frame and queues it; counts drops.
 *   Writer    — owns the microSD, the open file and the recording state. Serves
 *               the control queue first, then drains the frame queue, then
 *               flushes on the interval/byte budget.
 *   Rec-gate  — polls the decoded kill switch and posts start/stop.
 *   FSM       — 50 Hz DE-09 tick + remote LED (fsm_preview.c).
 *   Card-det  — polls DET_A (IO8) and posts insert/remove to the control queue.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

#include "can_decode.h"     /* CAN_DECODE_STALE_MS */
#include "can_tap.h"
#include "fault_led.h"
#include "fsm_preview.h"
#include "logger_time.h"
#include "trc_format.h"
#include "ui_log.h"

#define TRC_DIR         "/sdcard"
#define RX_QUEUE_LEN    CONFIG_LOGGER_RX_QUEUE_LEN
#define CTRL_QUEUE_LEN  8

/* Full-buffering size for the .trc stream. The default newlib buffer (~128 B)
 * flushes an SD block write every few frames; at motorcycle bus loads (~1500
 * frames/s here) those tiny writes cap the writer near ~400 frames/s and the
 * RX-to-writer queue overflows. A large buffer batches writes into full SD
 * clusters, lifting the ceiling far above the offered load.
 *
 * This is about THROUGHPUT and is unchanged. Durability is a separate concern,
 * handled by the periodic flush below, which bounds how much of this buffer can
 * be lost to a power cut without throttling the steady-state write path. */
#define TRC_IO_BUF_SIZE (32 * 1024)

#define FLUSH_INTERVAL_MS CONFIG_LOGGER_FLUSH_INTERVAL_MS
#define FLUSH_BYTES       ((size_t)CONFIG_LOGGER_FLUSH_KIB * 1024u)

/* DET_A polling. 50 ms sampling with a 3-sample agreement filter debounces the
 * mechanical detect switch in ~150 ms — far below human insert/remove speed,
 * and slow enough that a bouncing contact can't spam the control queue. */
#define CARD_POLL_MS      50
#define CARD_STABLE_POLLS 3

/* Recording-gate poll period. The gate's own timeouts are seconds, so 100 ms is
 * ample and keeps this task out of the way of the writer. */
#define GATE_POLL_MS      100

/* A CAN frame stamped with its receive time (esp_timer microseconds). */
typedef struct {
    int64_t     t_us;
    trc_frame_t frame;
} ts_frame_t;

typedef enum {
    CMD_RECORD_START,
    CMD_RECORD_STOP,
    CMD_CARD_INSERTED,
    CMD_CARD_REMOVED,
} logger_cmd_t;

/* `detail` is always a string literal (or NULL), so passing the pointer through
 * the queue is safe — there is nothing to outlive. It ends up in the file's
 * `;closed:` footer, which is how a reader tells a kill-switch stop from a bus
 * that went quiet. */
typedef struct {
    logger_cmd_t cmd;
    const char  *detail;
} logger_msg_t;

static QueueHandle_t    s_frame_q;
static QueueHandle_t    s_ctrl_q;

static volatile bool     s_recording;   /* read by CAN-RX, written by Writer */
/* Published by the gate task, read by the writer when a card shows up: the gate
 * only posts on a TRANSITION, so without this a card inserted while the kill
 * switch already reads RUN would never start a capture. */
static volatile bool     s_gate_run;
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

/* ---- Fault latch ---------------------------------------------------------- */

/* Latched once the CAN silent pin reads low. That is the one fault the operator
 * must not be able to lose track of — everything else here is recoverable, but a
 * transceiver that might not be silent stays reported until the board is power
 * cycled. */
static bool s_silent_fault;

/* Raise the (optional, default-off) fatal-error indication, honouring the
 * latch. Everything else is reported on the console only. */
static void logger_set_fault(bool fault)
{
    fault_led_set(s_silent_fault || fault);
}

/* ---- CAN transceiver silent pin ------------------------------------------ */

/* Make sure the TCAN330's S pin (IO35) is high, which is silent mode: the
 * receiver stays active, the driver is disabled. Belt and braces, in this order
 * and for these reasons:
 *
 *  1. Configure it as a high-impedance input with BOTH internal pulls disabled
 *     and read it back. High means R16 is doing its job. An internal pull-up
 *     here would hold S high even if R16 were missing or unstuffed, hiding
 *     exactly the board fault that would let us transmit onto a live bus — so
 *     the first read has to be with no internal pull. Same reasoning as DET_A
 *     in BRINGUP.md §4.5, but the stakes are higher.
 *  2. THEN drive it high as an output and leave it there, so silence does not
 *     depend on a single 10K resistor for the whole ride.
 *
 * Silence still holds through reset and through boot before app_main runs,
 * because R16 covers those windows — and a GPIO output latch holds its level
 * through a firmware crash, so step 2 does not weaken that. */
static void can_silent_pin_init(void)
{
    const gpio_config_t in_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_LOGGER_CAN_SILENT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg));

    const int level = gpio_get_level(CONFIG_LOGGER_CAN_SILENT_GPIO);
    ui_log_line("CAN silent pin IO%d reads %d as hi-z (R16 pull-up)",
                CONFIG_LOGGER_CAN_SILENT_GPIO, level);
    if (level != 1) {
        ui_log_line("WARNING: silent pin reads LOW with no internal pull —");
        ui_log_line("  R16 may be missing. Driving it high anyway, but do NOT");
        ui_log_line("  connect to a vehicle bus until the board is checked.");
        s_silent_fault = true;
    }

    const gpio_config_t out_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_LOGGER_CAN_SILENT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_LOGGER_CAN_SILENT_GPIO, 1));
    ui_log_line("CAN silent pin IO%d driven HIGH — transceiver silent "
                "(RX active, driver disabled)", CONFIG_LOGGER_CAN_SILENT_GPIO);

    logger_set_fault(false);
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
 * log line rather than an opaque mount error. */
static bool sd_mount(void)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();               /* SDMMC slot 1 */
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();

    /* The S3 has a real SDMMC host whose slot pins route through the GPIO
     * matrix, so every pin is set explicitly from Kconfig rather than inherited
     * from a fixed IO-MUX slot assignment. */
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
        logger_set_fault(true);
        return;
    }

    ui_log_line("microSD: card detected (DET_A/IO%d low)", CONFIG_LOGGER_SD_DET_GPIO);

    s_sd_ok = sd_mount();
    if (!s_sd_ok) {
        ui_log_line("microSD: filesystem mount FAILED");
        logger_set_fault(true);
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
    logger_set_fault(false);
}

/* ---- Writer task: owns the file + recording state ------------------------ */

static FILE   *s_file;
static char   *s_io_buf;      /* full-buffering block for s_file (see setvbuf) */
static uint32_t s_msgnr;
static int64_t  s_t0_us;      /* timestamp of the first frame in this file */
static uint32_t s_file_frames;

/* Durability accounting. Both are reset by writer_flush(). */
static size_t   s_bytes_since_flush;
static uint32_t s_last_flush_ms;
static uint32_t s_flushes;

static void note_bytes_written(size_t n)
{
    s_bytes_since_flush += n;
}

/* Push whatever is in the stdio buffer down to the card and ask the filesystem
 * to commit it.
 *
 * WHY THIS EXISTS. There is no longer a button press to end a file cleanly, and
 * the board loses 12 V the moment the ignition goes off or a connector moves. A
 * 32 KB stdio buffer plus FATFS metadata held in RAM is a large window of a ride
 * to lose. fflush() hands the bytes to the VFS; fsync() is what makes FATFS
 * write the dirty sectors AND update the directory entry, so the file has a
 * non-zero length and a reader can actually see the data.
 *
 * It is deliberately interval/byte driven rather than per-frame: at ~1500
 * frames/s a per-frame fsync would collapse throughput, which is the problem
 * TRC_IO_BUF_SIZE exists to solve. This bounds the loss window without
 * reintroducing it. */
static void writer_flush(const char *why)
{
    if (s_file == NULL) {
        return;
    }
    fflush(s_file);
    const int fd = fileno(s_file);
    if (fd >= 0) {
        fsync(fd);
    }
    s_bytes_since_flush = 0;
    s_last_flush_ms = logger_now_ms();
    s_flushes++;
    if (why != NULL) {
        ui_log_line("flush (%s): %u frames so far, %u flush(es)", why,
                    (unsigned)s_file_frames, (unsigned)s_flushes);
    }
}

/* Periodic durability flush, called from the writer loop. Quiet: logging every
 * 2 s flush would bury the FSM transition log, which is the thing worth
 * reading. */
static void writer_flush_if_due(void)
{
    if (s_file == NULL) {
        return;
    }
    const uint32_t now = logger_now_ms();
    if ((now - s_last_flush_ms) >= FLUSH_INTERVAL_MS ||
        s_bytes_since_flush >= FLUSH_BYTES) {
        writer_flush(NULL);
    }
}

static void writer_start_recording(void)
{
    if (s_recording) {
        return;         /* already running — the gate is idempotent */
    }
    if (!sd_card_present()) {
        ui_log_line("cannot record: no microSD card");
        logger_set_fault(true);
        return;
    }
    if (!s_sd_ok) {
        ui_log_line("cannot record: microSD not mounted");
        logger_set_fault(true);
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
        logger_set_fault(true);
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

    s_bytes_since_flush = 0;
    s_last_flush_ms = logger_now_ms();
    s_flushes = 0;

    char header[512];
    int hn = trc_format_header(header, sizeof(header));
    if (hn > 0) {
        fwrite(header, 1, (size_t)hn, s_file);
        note_bytes_written((size_t)hn);
    }

    s_msgnr = 0;
    s_file_frames = 0;
    s_t0_us = INT64_MIN;
    s_dropped = 0;

    s_recording = true;
    ui_log_line("recording -> %u.trc (flush every %u ms / %u KiB)",
                s_next_num, (unsigned)FLUSH_INTERVAL_MS,
                (unsigned)CONFIG_LOGGER_FLUSH_KIB);
    s_next_num++;

    /* Commit the header and the new directory entry immediately, so even a
     * power cut seconds later leaves a well-formed (if short) file rather than
     * a zero-length one. */
    writer_flush("file opened");
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

static void writer_stop_recording(const char *why)
{
    if (!s_recording && s_file == NULL) {
        return;         /* nothing open — the gate is idempotent */
    }

    /* Stop accepting new frames, then flush what is already queued for this
     * file. The TWAI controller and the decode tap keep running: the gate has
     * to be able to see the kill switch go back to RUN. */
    s_recording = false;

    ts_frame_t tf;
    while (xQueueReceive(s_frame_q, &tf, 0) == pdTRUE) {
        writer_write_frame(&tf);
    }

    if (s_file != NULL) {
        /* Footer comment so every capture is self-documenting: readers skip
         * lines starting with ';', and "dropped-frames: N" is greppable. A
         * clean capture records 0, so absence of the line means the file was
         * cut short — by a power loss or a card removal — rather than closed. */
        char footer[128];
        int fn = snprintf(footer, sizeof(footer),
                          ";dropped-frames: %u (RX-to-writer queue overflow)\n"
                          ";closed: %s\n",
                          (unsigned)s_dropped, why != NULL ? why : "unknown");
        if (fn > 0) {
            fwrite(footer, 1, (size_t)fn, s_file);
        }
        fflush(s_file);
        fclose(s_file);
        s_file = NULL;
        free(s_io_buf);
        s_io_buf = NULL;
    }
    ui_log_line("recording stopped (%s): %u frames%s",
                why != NULL ? why : "unknown", (unsigned)s_file_frames,
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
    ui_log_line("  %u frames were written; everything since the last flush "
                "(<= %u ms / %u KiB) is lost", (unsigned)s_file_frames,
                (unsigned)FLUSH_INTERVAL_MS, (unsigned)CONFIG_LOGGER_FLUSH_KIB);
}

static void writer_handle_card_inserted(void)
{
    ui_log_line("microSD: card inserted");
    if (!s_sd_ok) {
        sd_bring_up();
    }

    /* Catch up with the gate. It posts only on a transition, so if the kill
     * switch already read RUN when the card arrived — booting with no card in,
     * or swapping cards at a stop — nothing would otherwise start the capture. */
    if (s_sd_ok && s_gate_run && !s_recording) {
        ui_log_line("gate already RUN — starting capture now the card is in");
        writer_start_recording();
    }
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
    logger_set_fault(true);
}

static void writer_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* Service control commands first so a gate transition or a card event is
         * always acted on promptly, even while frames are flooding in. Then
         * block briefly on the frame queue: a silent-but-recording bus parks
         * here (letting lower-prio tasks run) while a control command is still
         * noticed within the timeout.
         *
         * NOTE: do not multiplex these two queues through a FreeRTOS queue set.
         * writer_start/stop_recording drain s_frame_q directly, and reading a
         * queue-set member outside xQueueSelectFromSet() desyncs the set's token
         * accounting -- which stranded/batched control commands under heavy
         * frame load and left the recording state out of step with the gate. */
        logger_msg_t msg;
        if (xQueueReceive(s_ctrl_q, &msg, 0) == pdTRUE) {
            switch (msg.cmd) {
            case CMD_RECORD_START:   writer_start_recording();           break;
            case CMD_RECORD_STOP:    writer_stop_recording(msg.detail);  break;
            case CMD_CARD_INSERTED:  writer_handle_card_inserted();      break;
            case CMD_CARD_REMOVED:   writer_handle_card_removed();       break;
            }
            continue;
        }

        ts_frame_t tf;
        if (xQueueReceive(s_frame_q, &tf, pdMS_TO_TICKS(20)) == pdTRUE) {
            writer_write_frame(&tf);
        }

        writer_flush_if_due();
    }
}

/* ---- CAN-RX task --------------------------------------------------------- */

static void can_rx_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* The controller runs continuously, not just while recording. It has to:
         * the decode tap below is what feeds both the DE-09 preview and the
         * recording gate, and the gate can only turn recording ON if it can see
         * the kill switch while idle.
         *
         * Receiving at idle is safe on this board: RX comes from the TCAN330's
         * push-pull RXD output, which idles recessive-high with no bus attached,
         * so there is no floating pin to flood the task.
         *
         * Finite timeout: a silent bus blocks here (letting the idle task run)
         * and the task stays responsive — never a tight spin. */
        twai_message_t msg;
        const esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(100));
        if (err == ESP_ERR_TIMEOUT) {
            continue;   /* no traffic */
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

        /* ALWAYS feed the decode tap, recording or not. Standard frames only:
         * every profile signal lives on an 11-bit ID, and the decoder keys on
         * the raw identifier, so an extended frame whose low 11 bits collide
         * with a profile ID would otherwise be decoded as that message. */
        if (!msg.extd && !msg.rtr) {
            can_tap_feed(msg.identifier, msg.data, msg.data_length_code,
                         logger_now_ms());
        }

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

/* ---- Recording gate task ------------------------------------------------- */

/* The automatic recording policy. See the "RECORDING POLICY" block at the top
 * of this file for the contract; this is the mechanism.
 *
 * Three states rather than a bool, because "we have never seen the kill switch"
 * is genuinely different from "the kill switch says STOP": at boot, before any
 * 0x121 has arrived, there is nothing to act on and nothing to log. */
typedef enum {
    GATE_UNKNOWN = 0,   /* no 0x121 decoded yet                    */
    GATE_RUN,           /* kill switch released — capture the ride */
    GATE_STOP,          /* kill asserted, or the bus went quiet    */
} gate_state_t;

static const char *gate_name(gate_state_t g)
{
    switch (g) {
    case GATE_RUN:  return "RUN";
    case GATE_STOP: return "STOP";
    default:        return "unknown";
    }
}

static void record_gate_task(void *arg)
{
    (void)arg;
    gate_state_t gate = GATE_UNKNOWN;
    const char *reason = "boot";

    ui_log_line("recording gate: automatic, on engine_cutoff "
                "(0x121 bit30 && byte6==0x28)");
    ui_log_line("  RUN -> new N.trc · STOP or %u ms of bus silence -> close",
                (unsigned)CONFIG_LOGGER_BUS_SILENCE_MS);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(GATE_POLL_MS));

        const uint32_t now_ms = logger_now_ms();
        can_tap_snap_t s;
        can_tap_snapshot(&s, now_ms);

        gate_state_t next = gate;
        const char *next_reason = reason;

        if (!s.cutoff_seen) {
            /* Nothing decoded yet. Stay put — at boot that means UNKNOWN, and
             * `seen` is sticky so this branch cannot un-decide a live gate. */
        } else if (s.cutoff_age_ms > CONFIG_LOGGER_BUS_SILENCE_MS) {
            /* No 0x121 for the silence timeout: the bus is gone. That is what
             * ignition-off looks like from here, and it is the only way a file
             * gets closed when the key is simply turned off. */
            next = GATE_STOP;
            next_reason = "bus silent";
        } else if (s.cutoff_age_ms <= CAN_DECODE_STALE_MS) {
            /* A reading we can trust (can_sig_valid's window). */
            next = s.cutoff ? GATE_STOP : GATE_RUN;
            next_reason = s.cutoff ? "kill asserted (STOP)"
                                   : "kill released (RUN)";
        }
        /* else: stale but not yet silent — hold the current decision. The gap
         * between the two timeouts is deliberate hysteresis, so a brief run of
         * missed frames cannot chop a ride into two files. */

        if (next != gate) {
            ui_log_line("gate %s -> %s (%s, 0x121 age %u ms)",
                        gate_name(gate), gate_name(next), next_reason,
                        (unsigned)s.cutoff_age_ms);
            gate = next;
            reason = next_reason;
            s_gate_run = (gate == GATE_RUN);

            const logger_msg_t msg = {
                .cmd = (gate == GATE_RUN) ? CMD_RECORD_START : CMD_RECORD_STOP,
                .detail = next_reason,
            };
            xQueueSend(s_ctrl_q, &msg, 0);
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
        const logger_msg_t msg = {
            .cmd = raw ? CMD_CARD_INSERTED : CMD_CARD_REMOVED,
            .detail = NULL,
        };
        xQueueSend(s_ctrl_q, &msg, 0);
    }
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

    /* Start the controller now and leave it running for the life of the app:
     * the decode tap, the DE-09 preview and the recording gate all need frames
     * while idle. In listen-only mode, with the transceiver held silent, running
     * it costs nothing on the bus. */
    esp_err_t err = twai_start();
    if (err != ESP_OK) {
        ui_log_line("CAN start FAILED (%s)", esp_err_to_name(err));
        logger_set_fault(true);
    }
}

/* ---- app_main ------------------------------------------------------------ */

void app_main(void)
{
    fault_led_init();

    ui_log_line("booted — logger PCB (ESP32-S3), DE-09 brake-preview build");
    ui_log_line("CAN %s %s, all IDs", logger_bitrate_str(), logger_mode_str());
    ui_log_line("button IO%d is UNUSED in this mode (recording is automatic)",
                CONFIG_LOGGER_BUTTON_GPIO);

    /* Before anything else that could touch the bus. */
    can_silent_pin_init();

    sd_det_init();
    sd_bring_up();

    s_frame_q = xQueueCreate(RX_QUEUE_LEN, sizeof(ts_frame_t));
    s_ctrl_q  = xQueueCreate(CTRL_QUEUE_LEN, sizeof(logger_msg_t));

    /* The tap must exist before any frame can be fed to it. */
    can_tap_init();

    /* Install and start the controller BEFORE the RX task exists, so that task
     * never runs against a stopped controller. It defends against that anyway
     * (see can_rx_task), but not having the window is better than handling it. */
    can_init();

    xTaskCreate(writer_task, "trc_writer", 4096, NULL, 5, NULL);
    xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 6, NULL);
    xTaskCreate(card_detect_task, "card_detect", 2560, NULL, 3, NULL);
    xTaskCreate(record_gate_task, "rec_gate", 3072, NULL, 3, NULL);

    fsm_preview_init();

    ui_log_line("ready — waiting for the kill switch to read RUN");
}
