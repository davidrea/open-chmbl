/*
 * Host test for the DE-04 render core — the state->binary-output map and the
 * anti-strobe dwell floor, proved with a synthetic clock and no hardware.
 *
 * Build/run:
 *   cmake -S . -B build && cmake --build build && ./build/render_core_test
 */
#include <stdio.h>
#include <stdlib.h>

#include "render_core.h"

static int g_fail;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

static const render_cfg_t CFG = { .min_on_ms = 600, .min_off_ms = 150 };

/* Step the core once per `tick` ms from `from` up to and including `to`,
 * holding `desired`. Returns the committed output at the end. */
static bar_out_t run(render_core_t *rc, bar_out_t desired,
                     int64_t from, int64_t to, int tick)
{
    bar_out_t out = rc->out;
    for (int64_t t = from; t <= to; t += tick) {
        out = render_core_step(rc, desired, t);
    }
    return out;
}

/* 1. The state -> output map, including the reserved ST_DECEL value. */
static void test_map(void)
{
    CHECK(render_core_map(ST_OFF) == BAR_OFF, "ST_OFF must render dark");
    CHECK(render_core_map(ST_BRAKE) == BAR_ON, "ST_BRAKE must render lit");
    /* Reserved, never emitted by the TX FSM; mapped to ON deliberately so an
     * unexpected soft-cue tier fails toward a lit brake light. */
    CHECK(render_core_map(ST_DECEL) == BAR_ON, "ST_DECEL must render lit");
    /* Anything off-protocol must not latch a fake BRAKE. */
    CHECK(render_core_map((brake_state_t)7) == BAR_OFF,
          "an unknown wire value must render dark, not BRAKE");
    CHECK(render_core_map((brake_state_t)255) == BAR_OFF,
          "an unknown wire value must render dark, not BRAKE");
}

/* 2. Brake-on from a settled dark bar is immediate (latency budget). */
static void test_brake_on_is_prompt(void)
{
    render_core_t rc;
    render_core_init(&rc, &CFG, 0);
    /* Bar has been dark since boot, well past min_off_ms. */
    run(&rc, BAR_OFF, 0, 1000, 20);
    bar_out_t out = render_core_step(&rc, BAR_ON, 1000);
    CHECK(out == BAR_ON, "a settled-dark bar must light on the same step");
    CHECK(render_core_hold_remaining_ms(&rc, 1000) == 0, "no hold expected");
}

/* 3. min_on_ms: an OFF arriving right after an ON is deferred, not dropped. */
static void test_min_on_floor(void)
{
    render_core_t rc;
    render_core_init(&rc, &CFG, 0);
    run(&rc, BAR_OFF, 0, 200, 20);

    CHECK(render_core_step(&rc, BAR_ON, 200) == BAR_ON, "should light");

    /* Immediately ask for OFF; it must be held for min_on_ms. */
    CHECK(render_core_step(&rc, BAR_OFF, 210) == BAR_ON,
          "OFF 10 ms after ON must be held");
    CHECK(render_core_hold_remaining_ms(&rc, 210) == 590,
          "hold remaining should be 590 ms, got %u",
          render_core_hold_remaining_ms(&rc, 210));

    CHECK(run(&rc, BAR_OFF, 220, 790, 10) == BAR_ON,
          "must still be lit just before the floor elapses");
    CHECK(render_core_step(&rc, BAR_OFF, 800) == BAR_OFF,
          "must go dark once min_on_ms has elapsed");
    /* The deferred change is committed, not forgotten. */
    CHECK(rc.changes == 2, "expected 2 committed transitions, got %u", rc.changes);
    CHECK(rc.held > 0, "the deferred steps should be counted as held");
}

/* 4. min_off_ms: a re-light right after going dark is deferred. */
static void test_min_off_floor(void)
{
    render_core_t rc;
    render_core_init(&rc, &CFG, 0);
    run(&rc, BAR_OFF, 0, 200, 20);
    render_core_step(&rc, BAR_ON, 200);
    run(&rc, BAR_OFF, 210, 900, 10);   /* commits OFF at 800 */
    CHECK(rc.out == BAR_OFF, "bar should be dark here");

    CHECK(render_core_step(&rc, BAR_ON, 905) == BAR_OFF,
          "re-light inside min_off_ms must be held");
    CHECK(run(&rc, BAR_ON, 910, 945, 5) == BAR_OFF,
          "still held just before min_off_ms elapses");
    CHECK(run(&rc, BAR_ON, 950, 960, 5) == BAR_ON,
          "must relight once min_off_ms has elapsed");
}

/* 5. A change requested and withdrawn inside the floor never reaches the pin. */
static void test_transient_is_swallowed(void)
{
    render_core_t rc;
    render_core_init(&rc, &CFG, 0);
    run(&rc, BAR_OFF, 0, 200, 20);
    render_core_step(&rc, BAR_ON, 200);

    render_core_step(&rc, BAR_OFF, 210);   /* blip */
    render_core_step(&rc, BAR_ON, 220);    /* back again, still inside min_on */
    CHECK(run(&rc, BAR_ON, 240, 2000, 20) == BAR_ON,
          "a transient OFF inside the floor must not reach the pin");
    CHECK(rc.changes == 1, "expected exactly 1 transition, got %u", rc.changes);
}

/* 6. The headline guarantee: a state oscillating every render tick cannot
 *    strobe the bar. Over 10 s the bar may toggle at most once per
 *    (min_on + min_off); check the measured rate stays under that bound. */
static void test_cannot_strobe(void)
{
    render_core_t rc;
    render_core_init(&rc, &CFG, 0);

    const int tick = 16;         /* CONFIG_CHMBL_RENDER_TICK_MS default */
    const int64_t span = 10000;
    int flips = 0;
    bar_out_t prev = rc.out;
    int i = 0;
    for (int64_t t = 0; t <= span; t += tick, i++) {
        /* Worst case: the upstream state flaps on every single tick. */
        bar_out_t desired = (i & 1) ? BAR_ON : BAR_OFF;
        bar_out_t out = render_core_step(&rc, desired, t);
        if (out != prev) {
            flips++;
            prev = out;
        }
    }

    /* One full on->off->on cycle costs min_on + min_off = 750 ms, i.e. two
     * flips per 750 ms. Allow one extra flip for the partial cycle at each end. */
    int max_flips = (int)((2 * span) / (CFG.min_on_ms + CFG.min_off_ms)) + 2;
    CHECK(flips <= max_flips,
          "bar flipped %d times in %lld ms; anti-strobe bound is %d",
          flips, (long long)span, max_flips);
    /* Sanity: the floors did not wedge the output entirely. */
    CHECK(flips > 0, "the bar should still follow a flapping state, slowly");
    printf("  anti-strobe: %d flips in %lld ms (bound %d)\n",
           flips, (long long)span, max_flips);
}

/* 7. A backwards clock must not wedge the output. */
static void test_clock_regression(void)
{
    render_core_t rc;
    render_core_init(&rc, &CFG, 1000);
    run(&rc, BAR_OFF, 1000, 2000, 20);
    render_core_step(&rc, BAR_ON, 2000);

    /* Time jumps backwards; the step is held and the clock re-anchored. */
    CHECK(render_core_step(&rc, BAR_OFF, 500) == BAR_ON, "held on a back-step");
    CHECK(run(&rc, BAR_OFF, 520, 1200, 20) == BAR_OFF,
          "must recover and commit once the floor elapses from the new anchor");
}

int main(void)
{
    test_map();
    test_brake_on_is_prompt();
    test_min_on_floor();
    test_min_off_floor();
    test_transient_is_swallowed();
    test_cannot_strobe();
    test_clock_regression();

    if (g_fail) {
        printf("render_core_test: %d check(s) FAILED\n", g_fail);
        return EXIT_FAILURE;
    }
    printf("render_core_test: all checks passed\n");
    return EXIT_SUCCESS;
}
