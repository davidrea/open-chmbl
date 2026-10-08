#!/usr/bin/env python3
"""Parity test: the firmware's DE-09 state machine must agree with the reference.

The braking logic exists twice on purpose — once in C, where it ships
(``transmitter/software/main/brake_fsm.c``), and once in Python, where it is
tuned (``tools/trc_viz.py``, the twin of the in-browser bench
``tools/trc_viz.html`` the thresholds were calibrated on). This script replays
a ride capture through both and asserts they produce the *same state at every
tick*, so the two cannot drift apart unnoticed: retune the bench and forget the
firmware, or "clean up" the firmware and change its behaviour, and CI says so.

It checks two things, deliberately separated so a failure points somewhere:

  1. **Derived acceleration.** The C harness emits the raw wheel-speed samples
     it decoded; the reference re-derives acceleration from those same samples
     (same low-pass, same slope window, same EMA) and the two series must
     match. This isolates the derivation from CAN decoding, which
     ``golden_check.py`` already covers against cantools.

  2. **State machine.** The reference FSM is run over the C harness's own
     per-tick inputs, so only the transition logic is under test. Comparison is
     per contiguous run of valid wheel speed: the firmware holds the light off
     and rearms the machine whenever that signal is missing or stale, which the
     reference — working on a zero-filled array with no notion of validity —
     has no way to express. Those ticks are checked for being OFF instead.

Usage:
    python3 tools/fsm_check.py \\
        [--harness transmitter/software/test_host/build/fsm_replay] \\
        [--trc logger/40mph_drive_cycle.trc]
"""

import argparse
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trc_viz  # noqa: E402  (needs the path fix above)

# The C harness prints values with %.6f, so the reference sees them rounded to
# ~1e-6. Everything downstream is float32 in C and float64 in Python, which is
# the only reason these are not exact comparisons.
ACCEL_TOL = 1e-3   # mph/s


def run_harness(harness, trc):
    """Replay the capture through the C decoder + FSM; return its series."""
    out = subprocess.run([harness, trc], check=True, capture_output=True,
                         text=True)
    print(out.stderr.strip(), file=sys.stderr)

    raw_t, raw_v = [], []
    t, speed, speed_valid, accel, clutch, gear, state = [], [], [], [], [], [], []
    for line in out.stdout.splitlines():
        f = line.split(",")
        if f[0] == "S":
            raw_t.append(float(f[1]))
            raw_v.append(float(f[2]))
        elif f[0] == "T":
            t.append(float(f[1]))
            speed.append(float(f[2]))
            speed_valid.append(f[3] == "1")
            accel.append(float(f[4]))          # "nan" when not yet derived
            clutch.append(float(f[6]))
            gear.append(float(f[7]))
            state.append(int(f[8]))

    if not t:
        sys.exit(f"{harness}: no FSM ticks emitted for {trc}")

    return {
        "raw_t_ms": raw_t,
        "raw_v_mph": raw_v,
        "grid_s": np.asarray(t) / 1000.0,
        "speed": np.asarray(speed),
        "speed_valid": np.asarray(speed_valid),
        "accel": np.asarray(accel),
        "clutch": np.asarray(clutch),
        "gear": np.asarray(gear),
        "state": np.asarray(state, dtype=np.int8),
    }


def check_accel(c, tun):
    """The reference derivation, over the C harness's own raw speed samples."""
    ref = trc_viz.derive_accel(c["raw_t_ms"], c["raw_v_mph"], c["grid_s"],
                               tun.speed_smooth_ms)
    got = c["accel"]

    # The firmware also ages the derived value out (CAN_DECODE_STALE_MS), which
    # the reference does not model, so "reference has a value where the
    # firmware has none" is expected after a long wheel-speed gap. The reverse
    # would mean the firmware invented one.
    invented = np.isnan(ref) & ~np.isnan(got)
    stale = ~np.isnan(ref) & np.isnan(got)
    both = ~np.isnan(ref) & ~np.isnan(got)

    delta = np.abs(ref[both] - got[both])
    worst = float(delta.max()) if delta.size else 0.0
    bad = int(np.count_nonzero(delta > ACCEL_TOL))

    print(f"accel   : {int(both.sum())} compared, max |delta| {worst:.2e} mph/s, "
          f"{int(stale.sum())} stale-only, {int(invented.sum())} unexplained")
    if int(invented.sum()):
        i = int(np.argmax(invented))
        print(f"  FAIL: firmware derived accel at t={c['grid_s'][i]:.2f}s where "
              f"the reference has none", file=sys.stderr)
        return False
    if bad:
        i = int(np.argmax(delta > ACCEL_TOL))
        idx = np.flatnonzero(both)[i]
        print(f"  FAIL: {bad} ticks differ by more than {ACCEL_TOL} mph/s; "
              f"first at t={c['grid_s'][idx]:.2f}s "
              f"(ref {ref[idx]:.6f}, firmware {got[idx]:.6f})", file=sys.stderr)
        return False
    return True


def segments(valid):
    """Yield (start, stop) index pairs for each contiguous run of True."""
    start = None
    for i, v in enumerate(valid):
        if v and start is None:
            start = i
        elif not v and start is not None:
            yield start, i
            start = None
    if start is not None:
        yield start, len(valid)


def reference_states(c, sl, tun):
    """Run the reference FSM over one segment of the harness's own inputs."""
    n = sl.stop - sl.start
    zeros = np.zeros(n)
    log = trc_viz.DecodedLog(
        duration_s=float(c["grid_s"][sl.stop - 1] - c["grid_s"][sl.start]),
        grid_t=c["grid_s"][sl],
        speed_mph=c["speed"][sl],
        accel_mphps=c["accel"][sl],
        throttle_pct=zeros, rpm_live=zeros, rpm_ecu=zeros,
        gear=c["gear"][sl],
        clutch_pulled=c["clutch"][sl],
        engine_cutoff=zeros,
        raw_spd_t_ms=[], raw_spd_v_mph=[],
    )
    return trc_viz.run_fsm(log, tun, accel=c["accel"][sl])


def check_fsm(c, tun):
    ok = True
    compared = 0
    segs = list(segments(c["speed_valid"]))

    for start, stop in segs:
        sl = slice(start, stop)
        ref = reference_states(c, sl, tun)
        got = c["state"][sl]
        compared += stop - start
        diff = np.flatnonzero(ref != got)
        if diff.size:
            ok = False
            i = start + int(diff[0])
            print(f"  FAIL: {diff.size} of {stop - start} ticks differ in the "
                  f"segment starting t={c['grid_s'][start]:.2f}s; first at "
                  f"t={c['grid_s'][i]:.2f}s: reference "
                  f"{trc_viz.STATE_NAMES[int(ref[diff[0]])]}, firmware "
                  f"{trc_viz.STATE_NAMES[int(got[i])]} "
                  f"(speed {c['speed'][i]:.2f} mph, accel {c['accel'][i]:.2f} "
                  f"mph/s)", file=sys.stderr)

    # Ticks with no usable wheel speed: the firmware must hold the light off.
    dark = ~c["speed_valid"]
    lit = np.count_nonzero(dark & (c["state"] != trc_viz.OFF))
    if lit:
        ok = False
        print(f"  FAIL: light on for {lit} tick(s) with no valid wheel speed",
              file=sys.stderr)

    print(f"fsm     : {compared} ticks compared across {len(segs)} valid-speed "
          f"segment(s), {int(dark.sum())} held off for want of wheel speed")
    return ok


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--harness", default=os.path.join(
        here, "transmitter/software/test_host/build/fsm_replay"))
    ap.add_argument("--trc", default=os.path.join(
        here, "logger/40mph_drive_cycle.trc"))
    args = ap.parse_args()

    tun = trc_viz.BrakeTunables()
    c = run_harness(args.harness, args.trc)

    # Count from an implicit leading OFF so this matches the harness's own
    # counter, which sees the very first tick's transition out of OFF.
    trace = np.concatenate(([trc_viz.OFF], c["state"].astype(np.int16)))
    transitions = int(np.count_nonzero(np.diff(trace)))
    on = int(np.count_nonzero(c["state"] != trc_viz.OFF))
    print(f"capture : {len(c['state'])} ticks, {transitions} transitions, "
          f"light on {on * trc_viz.FSM_DT_MS / 1000:.1f} s "
          f"({100 * on / len(c['state']):.0f}%)")

    ok = check_accel(c, tun)
    ok = check_fsm(c, tun) and ok

    print("PASS: firmware FSM matches the reference" if ok else
          "FAIL: firmware FSM diverges from the reference")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
