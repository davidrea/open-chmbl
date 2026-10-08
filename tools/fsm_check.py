#!/usr/bin/env python3
"""DE-09 cross-check: the C brake FSM must reproduce the tuned Python reference.

The OFF/BRAKING/STOPPED thresholds in ``components/brake_fsm`` were not derived
from first principles — they were tuned on real ride logs using ``run_fsm()`` in
:mod:`tools.trc_viz` (and the identical logic in ``tools/trc_viz.html``). This
script is what keeps the C port honest about that: it replays each committed
capture through

  * the C path — the real firmware decode (``components/chmbl_can``) plus the
    real firmware FSM (``components/brake_fsm``), stepped on the same 50 Hz grid
    the firmware ticks, via the ``fsm_replay`` host harness; and
  * the Python reference — ``load_log`` / ``derive_accel`` / ``run_fsm`` from
    ``tools/trc_viz.py`` with the stock ``BrakeTunables``,

and asserts they agree on the brake light's behaviour: the number of state
transitions and how long the light was on.

Usage::

    cmake -S transmitter/software/test_host -B transmitter/software/test_host/build
    cmake --build transmitter/software/test_host/build
    python3 tools/fsm_check.py

WHY NOT AN EXACT MATCH
----------------------
The two implementations cannot agree tick-for-tick, for reasons that are
properties of the firmware rather than defects:

* **Millisecond frame timestamps.** ``can_decode`` runs on a ``uint32_t``
  millisecond clock, as the firmware's does; the reference reads the ``.trc``
  time column at microsecond resolution. Wheel speed arrives every ~10 ms, so a
  sub-millisecond shift occasionally moves the "oldest sample at least
  ``CAN_DECODE_ACCEL_WINDOW_MS`` back" search by one sample — a 200 ms vs 210 ms
  slope window.
* **float vs. double.** The firmware derives acceleration in ``float``; numpy
  works in ``float64``.
* **Quantization.** Wheel speed is quantized to 0.0625 km/h (~0.039 mph). One
  quantum inside a 200 ms window is ~0.19 mph/s — a tenth of the rule-1
  threshold — so near a guard boundary the two can land on opposite sides.

Where that bites is the 120 ms rule-1 debounce: a marginal decel event that
holds the threshold for *six* ticks in one implementation holds it for *five* in
the other, and one short BRAKING episode appears or disappears. On
``logger/20260709-3.trc`` exactly that happens once, at t≈395 s: the reference
measures 100 ms of continuous decel past 2.0 mph/s and rejects it, the C path
measures 120 ms and turns the light on for 0.9 s. Same decel event, opposite
sides of one tick.

THE TOLERANCE, AND WHY IT HAS TEETH
-----------------------------------
``transitions`` within **2** and ``on_time_s`` within **max(1.0 s, 3%)**.

Measured agreement across the four committed captures is well inside that: 0, 0,
0 and 2 transitions of difference, and -0.9%, -0.0%, +0.5%, +1.8% of on-time.

The tolerance is not slack enough to hide a logic error. With the DE-08 decode
bugs this work fixed still present (``CAN_DECODE_SPEED_HIST`` at 16, no
wheel-speed low-pass) the same harness reports **12** transitions and **53.3 s**
of on-time against the reference's 29 and 89.0 s — a 59% error on on-time, 20x
the tolerance. Dropping just the low-pass gives 37 transitions: 8 over budget.
Any reordering of the guards or a missed hold-timer reset moves the numbers by
far more than one marginal episode.

If a future change needs this loosened, that is a signal to investigate, not to
raise the number.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys

DEFAULT_CAPTURES = [
    "logger/40mph_drive_cycle.trc",
    "logger/20260709-1.trc",
    "logger/20260709-2.trc",
    "logger/20260709-3.trc",
]

# See "THE TOLERANCE" above before changing either of these.
MAX_TRANSITION_DIFF = 2
MAX_ON_TIME_REL = 0.03
MAX_ON_TIME_ABS_S = 1.0


def run_c(harness: str, trc: str) -> dict:
    """Run the fsm_replay harness and parse its key=value summary."""
    proc = subprocess.run([harness, trc, "--quiet"],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"{harness} failed on {trc} (rc={proc.returncode}):\n"
                 f"{proc.stderr}")
    out = {}
    for line in proc.stdout.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k] = v
    for key in ("transitions", "on_time_s", "grid_points"):
        if key not in out:
            sys.exit(f"{harness} produced no '{key}' line for {trc}")
    return out


_TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))


def run_python(dbc: str, trc: str) -> dict:
    """Run the reference FSM over the same capture, in-process."""
    if _TOOLS_DIR not in sys.path:
        sys.path.insert(0, _TOOLS_DIR)
    # Imported lazily so --help still works without numpy/cantools installed.
    import trc_viz

    log = trc_viz.load_log(dbc, trc)
    tun = trc_viz.BrakeTunables()
    # derive_accel with the tunable's own smoothing, exactly as headless_check
    # does, so the comparison uses the shipped speed_smooth_ms.
    accel = trc_viz.derive_accel(log.raw_spd_t_ms, log.raw_spd_v_mph,
                                 log.grid_t, tun.speed_smooth_ms)
    state = trc_viz.run_fsm(log, tun, accel)
    stats = trc_viz.fsm_stats(state, trc_viz.FSM_DT_MS / 1000.0)
    return {
        "transitions": stats["transitions"],
        "on_time_s": stats["on_time_s"],
        "on_frac": stats["on_frac"],
        "grid_points": len(log.grid_t),
    }


def check(capture: str, harness: str, dbc: str) -> bool:
    c = run_c(harness, capture)
    p = run_python(dbc, capture)

    c_tr = int(c["transitions"])
    p_tr = int(p["transitions"])
    c_on = float(c["on_time_s"])
    p_on = float(p["on_time_s"])

    d_tr = abs(c_tr - p_tr)
    d_on = c_on - p_on
    on_budget = max(MAX_ON_TIME_ABS_S, MAX_ON_TIME_REL * p_on)
    rel = (d_on / p_on * 100.0) if p_on else 0.0

    ok_tr = d_tr <= MAX_TRANSITION_DIFF
    ok_on = abs(d_on) <= on_budget

    print(f"\n{capture}")
    print(f"  grid points        C {int(c['grid_points']):>8}   "
          f"ref {int(p['grid_points']):>8}")
    print(f"  transitions        C {c_tr:>8}   ref {p_tr:>8}   "
          f"diff {d_tr}  (<= {MAX_TRANSITION_DIFF})  "
          f"{'OK' if ok_tr else 'FAIL'}")
    print(f"  light on (s)       C {c_on:>8.2f}   ref {p_on:>8.2f}   "
          f"diff {d_on:+.2f} ({rel:+.2f}%)  "
          f"(<= {on_budget:.2f} s)  {'OK' if ok_on else 'FAIL'}")
    print(f"  on fraction        C {float(c['on_frac']) * 100:>7.2f}%   "
          f"ref {float(p['on_frac']) * 100:>7.2f}%")
    print(f"  C accel range      {float(c['accel_min_mphps']):+.2f} .. "
          f"{float(c['accel_max_mphps']):+.2f} mph/s"
          f"   (invalid ticks: {int(c['accel_invalid_ticks'])})")

    return ok_tr and ok_on


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--harness",
                    default="transmitter/software/test_host/build/fsm_replay",
                    help="path to the fsm_replay host binary")
    ap.add_argument("--dbc", default="profiles/triumph_tr.dbc")
    ap.add_argument("trc", nargs="*", default=None,
                    help="captures to check (default: all committed ones)")
    args = ap.parse_args(argv)

    if not os.access(args.harness, os.X_OK):
        sys.exit(f"harness not found or not executable: {args.harness}\n"
                 "build it with:\n"
                 "  cmake -S transmitter/software/test_host "
                 "-B transmitter/software/test_host/build\n"
                 "  cmake --build transmitter/software/test_host/build")

    captures = args.trc or DEFAULT_CAPTURES
    failures = [c for c in captures if not check(c, args.harness, args.dbc)]

    print()
    if failures:
        print(f"FAIL — {len(failures)} of {len(captures)} capture(s) outside "
              f"tolerance: {', '.join(failures)}")
        print("Investigate the cause before widening the tolerance; see the "
              "module docstring.")
        return 1
    print(f"PASS — C FSM matches the tuned reference on all {len(captures)} "
          "capture(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
