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

#ifdef __cplusplus
extern "C" {
#endif

/* Build the REPL on the active console transport (USB Serial/JTAG by default,
 * UART as a fallback), register all commands, and start the shell task. */
void console_start(void);

/* Per-domain command registration (called by console_start). */
void cmd_system_register(void);   /* `id` — chip MAC / unique ID + chip info */
void cmd_light_register(void);    /* `light` — drive the stand-in brake-light GPIO */

#ifdef __cplusplus
}
#endif
