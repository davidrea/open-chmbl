#!/usr/bin/env python3
"""Golden test: the embedded C decoder must agree with python-cantools.

Replays a PCAN .trc capture through both decoders of the same DBC:
  * the C extractor (transmitter/software/test_host/trc_replay), and
  * cantools' reference decode,
and asserts every decoded signal value matches, plus the C-side derived
predicates (clutch_pulled, engine_cutoff). This is what lets the firmware
keep a tiny data-driven decoder while inheriting cantools' bit-unpacking
correctness (see docs/design/de-08-can-decode.md).

Usage:
    python3 tools/golden_check.py \
        [--harness transmitter/software/test_host/build/trc_replay] \
        [--dbc profiles/triumph_tr.dbc] \
        [--trc logger/40mph_drive_cycle.trc]
"""

import argparse
import math
import subprocess
import sys

import can
import cantools

CUTOFF_REASON_VALUE = 0x28

REL_TOL = 1e-4
ABS_TOL = 1e-3


def harness_rows(harness, trc):
    out = subprocess.run([harness, trc], check=True, capture_output=True,
                         text=True)
    print(out.stderr.strip(), file=sys.stderr)
    for line in out.stdout.splitlines():
        frame, can_id, name, value = line.split(",")
        yield int(frame), int(can_id, 16), name, float(value)


def cantools_rows(dbc, trc):
    db = cantools.database.load_file(dbc)
    ids = {m.frame_id for m in db.messages}
    frame = 0
    for msg in can.TRCReader(trc):
        frame += 1
        if msg.arbitration_id not in ids:
            continue
        dec = db.decode_message(msg.arbitration_id, msg.data)
        for name, value in dec.items():
            yield frame, msg.arbitration_id, name, float(value)
        # derived predicates, computed the reference way
        if "CLUTCH_RAW" in dec:
            yield (frame, msg.arbitration_id, "clutch_pulled",
                   1.0 if int(dec["CLUTCH_RAW"]) != 0 else 0.0)
        if "ENGINE_CUTOFF_FLAG" in dec:
            cutoff = (int(dec["ENGINE_CUTOFF_FLAG"]) != 0 and
                      int(dec["CUTOFF_REASON"]) == CUTOFF_REASON_VALUE)
            yield (frame, msg.arbitration_id, "engine_cutoff",
                   1.0 if cutoff else 0.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--harness",
                    default="transmitter/software/test_host/build/trc_replay")
    ap.add_argument("--dbc", default="profiles/triumph_tr.dbc")
    ap.add_argument("--trc", default="logger/40mph_drive_cycle.trc")
    args = ap.parse_args()

    got = {}
    for frame, can_id, name, value in harness_rows(args.harness, args.trc):
        got[(frame, name)] = (can_id, value)

    checked = 0
    mismatches = 0
    for frame, can_id, name, expect in cantools_rows(args.dbc, args.trc):
        key = (frame, name)
        if key not in got:
            print(f"MISSING  frame {frame} {name} (expected {expect})")
            mismatches += 1
            continue
        got_id, actual = got.pop(key)
        checked += 1
        if got_id != can_id or not math.isclose(actual, expect,
                                                rel_tol=REL_TOL,
                                                abs_tol=ABS_TOL):
            print(f"MISMATCH frame {frame} {name}: C={actual} "
                  f"cantools={expect}")
            mismatches += 1
            if mismatches > 20:
                print("... aborting after 20 mismatches")
                break

    for (frame, name), (_, value) in list(got.items())[:20]:
        print(f"EXTRA    frame {frame} {name} = {value} (C only)")
        mismatches += 1

    if mismatches:
        print(f"FAIL: {mismatches} mismatch(es) over {checked} "
              f"compared values")
        return 1
    print(f"PASS: {checked} signal values identical between the C decoder "
          f"and cantools")
    return 0


if __name__ == "__main__":
    sys.exit(main())
