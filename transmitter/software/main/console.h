/*
 * Developer console (DE-00) — public entry points.
 *
 * The console is the backbone of the isolation-first build strategy
 * (see ../../../docs/cli.md): each design element fakes its inputs and views
 * its outputs over this shell. This header exposes the bootstrap plus the
 * per-domain command registration hooks; each command lives in its own
 * cmd_*.c so the registry grows one self-contained slice at a time.
 *
 * NOTE: console.c / console.h / cmd_system.c are duplicated verbatim with the
 * brake_light firmware for now; per the roadmap they get promoted to a shared
 * component once the shell stabilizes.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Build the REPL on the active console transport (USB Serial/JTAG by default,
 * UART as a fallback), register all commands, and start the shell task. */
void console_start(void);

/* Per-domain command registration (called by console_start). */
void cmd_system_register(void);   /* `id`    — chip MAC / unique ID + chip info */
void cmd_state_register(void);    /* `state` — set/show the stand-in braking output state */

#ifdef __cplusplus
}
#endif
