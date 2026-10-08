/*
 * One monotonic millisecond clock for everything in this app that needs one.
 *
 * The shared cores (components/chmbl_can, components/brake_fsm) take uint32_t
 * millisecond timestamps and do all their arithmetic with unsigned differences,
 * so the ~49.7-day wraparound is safe as long as every caller uses the SAME
 * clock. This is it.
 */
#ifndef LOGGER_TIME_H
#define LOGGER_TIME_H

#include <stdint.h>

#include "esp_timer.h"

static inline uint32_t logger_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

#endif /* LOGGER_TIME_H */
