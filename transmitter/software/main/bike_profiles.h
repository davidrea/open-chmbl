/*
 * Registry of available bike profiles (one extern per generated
 * bike_profile_<name>.c — see tools/gen_profile.py).
 */
#pragma once

#include "bike_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const bike_profile_t bike_profile_triumph_tr;

/* The compile-time default profile. A runtime selector (roadmap Phase 5)
 * would replace direct uses of this with an NVS-backed choice. */
#define BIKE_PROFILE_DEFAULT (&bike_profile_triumph_tr)

#ifdef __cplusplus
}
#endif
