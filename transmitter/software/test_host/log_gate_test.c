/*
 * Host unit test for the ride-logging gate (main/log_gate.c).
 *
 * Each case is a sequence of engine_cutoff observations and the event the
 * gate must return for each one. Exits non-zero on any mismatch, so CI fails.
 */
#include <stdio.h>

#include "log_gate.h"

#define STALE 1000u
#define CLOSE 3000u

static const log_gate_cfg_t CFG = { .stale_ms = STALE, .close_ms = CLOSE };

typedef struct {
    bool             seen;
    bool             cutoff;
    uint32_t         age_ms;
    log_gate_event_t expect;
} step_t;

#define RUN(age)    { true, false, (age), LOG_GATE_NONE }
#define CUT(age)    { true, true, (age), LOG_GATE_NONE }
#define UNSEEN      { false, false, 0, LOG_GATE_NONE }

static int s_failures;

static void run_case(const char *name, const step_t *steps, int n,
                     uint32_t expect_sessions)
{
    log_gate_t g;
    log_gate_init(&g);
    for (int i = 0; i < n; i++) {
        const log_gate_obs_t obs = {
            .seen = steps[i].seen,
            .cutoff = steps[i].cutoff,
            .age_ms = steps[i].age_ms,
        };
        const log_gate_event_t got = log_gate_update(&g, &obs, &CFG);
        if (got != steps[i].expect) {
            printf("FAIL %s, step %d: expected \"%s\", got \"%s\"\n", name, i,
                   log_gate_event_name(steps[i].expect),
                   log_gate_event_name(got));
            s_failures++;
            return;
        }
    }
    if (g.sessions != expect_sessions) {
        printf("FAIL %s: expected %u session(s), got %u\n", name,
               (unsigned)expect_sessions, (unsigned)g.sessions);
        s_failures++;
        return;
    }
    printf("ok   %s\n", name);
}

#define CASE(name, sessions, ...)                                         \
    do {                                                                  \
        const step_t steps_[] = { __VA_ARGS__ };                          \
        run_case(name, steps_, (int)(sizeof(steps_) / sizeof(steps_[0])), \
                 sessions);                                               \
    } while (0)

int main(void)
{
    /* Key on with the switch already in RUN: the first fresh RUN starts a
     * session, and further RUNs neither restart nor split it. */
    CASE("boot in RUN starts one session", 1,
         UNSEEN,
         { true, false, 0, LOG_GATE_START },
         RUN(10), RUN(10), RUN(10));

    /* Key on with the switch in CUTOFF: nothing until it moves to RUN. */
    CASE("boot in CUTOFF waits for RUN", 1,
         UNSEEN, CUT(0), CUT(10),
         { true, false, 0, LOG_GATE_START },
         RUN(10));

    /* RUN -> CUTOFF closes; CUTOFF -> RUN opens a NEW session every time. */
    CASE("each CUTOFF->RUN is a new session", 3,
         { true, false, 0, LOG_GATE_START },
         { true, true, 0, LOG_GATE_STOP_CUTOFF },
         { true, false, 0, LOG_GATE_START },
         { true, true, 0, LOG_GATE_STOP_CUTOFF },
         CUT(10),
         { true, false, 0, LOG_GATE_START });

    /* A dropout between stale_ms and close_ms holds the session open: no
     * split, and recovery in RUN does not count as a new session. */
    CASE("short dropout holds the session", 1,
         { true, false, 0, LOG_GATE_START },
         RUN(STALE + 1), RUN(CLOSE - 1), RUN(CLOSE), RUN(0));

    /* Key off: the bus goes quiet, and once it has been quiet longer than
     * close_ms the session ends. Key back on in RUN = new session. */
    CASE("bus silence closes, next RUN reopens", 2,
         { true, false, 0, LOG_GATE_START },
         RUN(CLOSE),
         { true, false, CLOSE + 1, LOG_GATE_STOP_SILENT },
         RUN(CLOSE + 500),
         { true, false, 0, LOG_GATE_START });

    /* A stale observation must never START a session, whatever it says. */
    CASE("stale RUN does not start", 0,
         RUN(STALE + 1), RUN(CLOSE + 1), CUT(STALE + 1));

    /* A stale CUTOFF must not END one either — only a fresh CUTOFF, or
     * silence past close_ms, does. */
    CASE("stale CUTOFF does not stop", 1,
         { true, false, 0, LOG_GATE_START },
         CUT(STALE + 1), CUT(CLOSE));

    /* The signal disappearing entirely (never seen again after a reset of
     * the decode state) ends an active session. */
    CASE("unseen ends an active session", 1,
         { true, false, 0, LOG_GATE_START },
         { false, false, 0, LOG_GATE_STOP_SILENT },
         UNSEEN);

    /* Boundaries: stale_ms itself is still fresh, close_ms itself still
     * holds; one past each is the transition. */
    CASE("stale_ms boundary is inclusive", 1,
         { true, false, STALE, LOG_GATE_START });
    CASE("close_ms boundary is inclusive", 1,
         { true, false, 0, LOG_GATE_START },
         RUN(CLOSE),
         { true, false, CLOSE + 1, LOG_GATE_STOP_SILENT });

    if (s_failures) {
        printf("%d case(s) FAILED\n", s_failures);
        return 1;
    }
    printf("all log_gate cases passed\n");
    return 0;
}
