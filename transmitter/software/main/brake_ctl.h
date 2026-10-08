/*
 * DE-09 — braking state machine, on target.
 *
 * Owns the 50 Hz tick that feeds the decoded signal snapshot (DE-08) into the
 * pure FSM core (brake_fsm.h) and drives the result onto the brake-light
 * output. Everything decision-shaped lives in brake_fsm.c; this file is the
 * plumbing: the task, the signal→input mapping, the force override, the live
 * tunables, and the status snapshot `state show` prints.
 *
 * The output today is the logger PCB's external-indicator pin (IO18, J4
 * pin2) — the remote pod LED, standing in as the brake light on the bench.
 * The same state is what net.c broadcasts over ESP-NOW, so wiring up a real
 * light later changes nothing here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "brake_fsm.h"
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BRAKE_FORCE_AUTO = 0, /* the state machine decides */
    BRAKE_FORCE_OFF,      /* held off  (`state force off`)   */
    BRAKE_FORCE_BRAKE,    /* held on   (`state force brake`) */
} brake_force_t;

typedef struct {
    brake_fsm_state_t fsm_state;
    brake_state_t     emitted;      /* what the output and net.c carry */
    brake_force_t     force;
    bool              light_on;

    /* the inputs the last tick actually saw */
    float   speed_mph;
    bool    speed_valid;
    float   accel_mphps;
    bool    accel_valid;
    bool    clutch_pulled;
    uint8_t gear;
    bool    gear_valid;

    /* FSM timers, in ms (since_ms saturates while the machine is rearmed) */
    uint32_t since_ms;
    uint32_t decel_hold_ms;
    uint32_t steady_hold_ms;
    uint32_t stopped_hold_ms;
    uint32_t transitions;
} brake_ctl_status_t;

/* Bring up the brake-light output and start the 50 Hz tick. Called
 * unconditionally from app_main(), before the CONFIG_CHMBL_CLI-gated
 * console, so the light works whether or not the dev CLI is built in. */
void brake_ctl_init(void);

void brake_ctl_get(brake_ctl_status_t *out);

/* Current emitted braking state — net.c reads this into every heartbeat. */
brake_state_t state_get(void);

void          brake_ctl_force(brake_force_t force);
brake_force_t brake_ctl_get_force(void);

/* Live tunables (`state tune`). brake_ctl_tune_set() also republishes
 * speed_smooth_ms to the decoder, which is where that one is applied. */
void brake_ctl_get_tunables(brake_tunables_t *out);
bool brake_ctl_tune_set(const char *name, float value);

#ifdef __cplusplus
}
#endif
