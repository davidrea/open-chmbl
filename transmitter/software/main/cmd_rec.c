/*
 * `rec` command — ride-logging status (trc_log.h).
 *
 *     rec            card, kill-switch session, open file, frame/drop counts
 *
 * Recording itself is automatic, gated on the engine cutoff switch, so this
 * is read-only. To exercise the gate on the bench, fake the switch through
 * the existing signal override — `sig source fake`, then
 * `sig set engine_cutoff 0` (RUN) / `1` (CUTOFF) / `na` (silent) — and watch
 * files open and close here. Frames recorded are still the live bus.
 */
#include <stdio.h>

#include "esp_console.h"

#include "console.h"
#include "trc_log.h"

static int cmd_rec(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    trc_log_status_t st;
    trc_log_get_status(&st);

    if (!st.enabled) {
        printf("ride logging: not built in (CONFIG_CHMBL_TRC_LOG=n)\n");
        return 0;
    }

    printf("card:     %s%s\n",
           st.card_present ? (st.mounted ? "mounted" : "present, NOT mounted")
                           : "none",
           st.fault ? "  [FAULT — see log]" : "");
    printf("session:  %s (%lu since boot)\n",
           st.gate_active ? "kill switch RUN" : "idle",
           (unsigned long)st.sessions);
    if (st.file_num == 0) {
        printf("file:     none yet\n");
    } else {
        printf("file:     %u.trc %s — %lu frames, %lu dropped\n", st.file_num,
               st.recording ? "RECORDING" : "(closed)",
               (unsigned long)st.file_frames, (unsigned long)st.dropped);
    }
    printf("files:    %lu opened since boot\n", (unsigned long)st.files);
    return 0;
}

void cmd_rec_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "rec",
        .help = "Ride-logging status: card, kill-switch session, open file",
        .hint = NULL,
        .func = &cmd_rec,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
