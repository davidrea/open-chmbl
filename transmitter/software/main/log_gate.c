/* Ride-logging gate on the engine cutoff switch. Pure C, host-testable. */

#include "log_gate.h"

#include <string.h>

void log_gate_init(log_gate_t *g)
{
    memset(g, 0, sizeof(*g));
}

log_gate_event_t log_gate_update(log_gate_t *g, const log_gate_obs_t *obs,
                                 const log_gate_cfg_t *cfg)
{
    const bool fresh = obs->seen && obs->age_ms <= cfg->stale_ms;
    const bool run = fresh && !obs->cutoff;
    const bool cutoff = fresh && obs->cutoff;
    const bool silent = !obs->seen || obs->age_ms > cfg->close_ms;

    if (!g->active) {
        if (run) {
            g->active = true;
            g->sessions++;
            return LOG_GATE_START;
        }
        return LOG_GATE_NONE;
    }

    if (cutoff) {
        g->active = false;
        return LOG_GATE_STOP_CUTOFF;
    }
    if (silent) {
        g->active = false;
        return LOG_GATE_STOP_SILENT;
    }
    return LOG_GATE_NONE;
}

const char *log_gate_event_name(log_gate_event_t ev)
{
    switch (ev) {
    case LOG_GATE_NONE:        return "none";
    case LOG_GATE_START:       return "start";
    case LOG_GATE_STOP_CUTOFF: return "stop: cutoff";
    case LOG_GATE_STOP_SILENT: return "stop: bus silent";
    default:                   return "?";
    }
}
