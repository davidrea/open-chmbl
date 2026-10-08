# components — shared ESP-IDF components

The parts of the firmware that **more than one device needs**, promoted out of a
single app's `main/` so they are compiled from one copy rather than duplicated or
reached across directories with `../../` source paths.

Every component here is **pure C with no ESP-IDF headers**, which is what makes it
both shareable and host-testable (see
[`docs/firmware.md §4`](../docs/firmware.md#4-build--toolchain)).

| Component | Contents | Design element | Used by |
|-----------|----------|----------------|---------|
| [`chmbl_can/`](chmbl_can) | `bike_profile.h`, the generated Triumph TR profile table, and the generic profile decoder `can_decode.[ch]` | [DE-08](../docs/design/de-08-can-decode.md) | `transmitter/software`, `logger/software` |
| [`brake_fsm/`](brake_fsm) | the `OFF`/`BRAKING`/`STOPPED` braking state machine | [DE-09](../docs/design/de-09-brake-decel-logic.md) | `logger/software` (on-board preview); `transmitter/software` when DE-09 lands there |

## How an app picks them up

Each app's **top-level** `CMakeLists.txt` adds this directory to
`EXTRA_COMPONENT_DIRS` before including `project.cmake`:

```cmake
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
```

and its `main` component names what it needs in `PRIV_REQUIRES`
(`chmbl_can`, `brake_fsm`). The apps set `COMPONENTS main` to trim the build, and
the dependency closure pulls these in from there — so adding a component to
`PRIV_REQUIRES` is the only step.

## Host builds

Nothing here needs ESP-IDF, so the host harnesses in
[`transmitter/software/test_host/`](../transmitter/software/test_host) compile these
sources directly with plain CMake + gcc:

- `trc_replay` — DE-08 decode golden test vs. `cantools`
  ([`tools/golden_check.py`](../tools/golden_check.py)).
- `fsm_replay` — DE-09 FSM replay vs. the tuned Python reference
  ([`tools/fsm_check.py`](../tools/fsm_check.py)).

Keep them that way: **no `#include "esp_*.h"`, no `sdkconfig.h`, no FreeRTOS** in a
component under this directory. Platform glue belongs in the app's `main/`.
