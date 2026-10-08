/*
 * Developer console (DE-00) — public entry points.
 *
 * The console is the backbone of the isolation-first build strategy
 * (see ../../../docs/cli.md): each design element fakes its inputs and views
 * its outputs over this shell. This header exposes the bootstrap plus the
 * per-domain command registration hooks; each command lives in its own
 * cmd_*.c so the registry grows one self-contained slice at a time.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build the REPL on the active console transport (USB Serial/JTAG by default,
 * UART as a fallback), register all commands, and start the shell task. */
void console_start(void);

/* Hardware bring-up lives with the modules that own it and is called
 * unconditionally from app_main(), before the CONFIG_CHMBL_CLI-gated
 * console_start(), so the real functionality works whether or not the dev
 * CLI is built in: render_init() (render.h), status_init() (status.h),
 * pairing_init() (pairing.h), net_init() (net.h), link_init() (link.h).
 *
 * Per-domain command registration (called by console_start). */
void cmd_system_register(void);   /* `id`     — chip MAC / unique ID + chip info */
void cmd_light_register(void);    /* `light`  — bench override of the brake bar */
void cmd_render_register(void);   /* `render` — view the brake bar's render output */
void cmd_pair_register(void);     /* `pair`   — manage the ESP-NOW peer */
void cmd_link_register(void);     /* `link`   — ESP-NOW link health */
void cmd_ind_register(void);      /* `ind`    — status-indicator LEDs */

#ifdef __cplusplus
}
#endif
