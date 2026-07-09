#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "cantools>=39",
#     "python-can>=4.3",
# ]
# ///
"""Summarize an open-chmbl PCAN .trc CAN capture: duration, frame counts,
per-ID rates, wheel-speed range, and wheel-speed frame gaps.

Reads the capture with python-can's TRCReader and decodes wheel speed through
the profile DBC (the same decode path as tools/golden_check.py and
tools/trc_viz.py), so its numbers are directly comparable to theirs.

Usage (either works):
    uv run .claude/skills/chmbl-diagnostics-and-tooling/scripts/trc_stats.py \
        logger/40mph_drive_cycle.trc
    python3 .../trc_stats.py logger/40mph_drive_cycle.trc   # after
        pip install -r tools/requirements.txt

Options:
    --dbc PATH        profile DBC (default profiles/triumph_tr.dbc)
    --gap-ms FLOAT    wheel-speed gap threshold in ms (default 200.0 — the
                      firmware's CAN_DECODE_ACCEL_WINDOW_MS; gaps beyond it
                      stall the acceleration derivation)
    --top N           how many IDs to list by rate (default 15; 0 = all)

Exit status: 0 on success, 1 if the capture has no readable frames.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter

import can
import cantools

KMH_TO_MPH = 0.621371

# IDs named in docs/can-profiles.md §5 (single reference bike, Triumph
# Speed 400). Anything else seen on the bus is still counted, just unnamed.
KNOWN_IDS = {
    0x102: "wheel speeds (front/rear)",
    0x121: "body: engine-cutoff flag/reason",
    0x140: "engine live: throttle, rpm",
    0x142: "transmission: gear, clutch",
    0x146: "engine ECU: rpm (x0.25)",
    0x481: "chassis: side stand",
}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("trc", help="PCAN .trc capture to summarize")
    ap.add_argument("--dbc", default="profiles/triumph_tr.dbc")
    ap.add_argument("--gap-ms", type=float, default=200.0,
                    help="wheel-speed gap threshold in ms (default 200)")
    ap.add_argument("--top", type=int, default=15,
                    help="IDs to list by rate (default 15, 0 = all)")
    args = ap.parse_args(argv)

    db = cantools.database.load_file(args.dbc)
    try:
        wheel_msg = db.get_message_by_name("WHEEL_SPEEDS")
        wheel_id = wheel_msg.frame_id
    except KeyError:
        # fall back: whichever message carries WHEEL_SPEED_FRONT
        wheel_id = next(m.frame_id for m in db.messages
                        if any(s.name == "WHEEL_SPEED_FRONT"
                               for s in m.signals))

    counts: Counter = Counter()
    t_first = t_last = None
    spd_min = spd_max = None
    spd_prev_t = None
    gaps = []          # (time_s, gap_ms)
    n_frames = 0

    for msg in can.TRCReader(args.trc):
        n_frames += 1
        t = msg.timestamp
        if t_first is None:
            t_first = t
        t_last = t
        counts[msg.arbitration_id] += 1

        if msg.arbitration_id == wheel_id:
            if spd_prev_t is not None:
                gap_ms = (t - spd_prev_t) * 1000.0
                if gap_ms > args.gap_ms:
                    gaps.append((t - t_first, gap_ms))
            spd_prev_t = t
            try:
                dec = db.decode_message(msg.arbitration_id, msg.data)
            except Exception:
                continue
            if "WHEEL_SPEED_FRONT" in dec:
                mph = float(dec["WHEEL_SPEED_FRONT"]) * KMH_TO_MPH
                spd_min = mph if spd_min is None else min(spd_min, mph)
                spd_max = mph if spd_max is None else max(spd_max, mph)

    if n_frames == 0 or t_first is None:
        print(f"error: no readable frames in {args.trc}", file=sys.stderr)
        return 1

    dur = t_last - t_first
    print(f"capture         : {args.trc}")
    print(f"duration        : {dur:8.1f} s")
    print(f"frames          : {n_frames:8d}  ({n_frames / dur:.0f} fps overall)")
    print(f"distinct IDs    : {len(counts):8d}")
    print()
    print(f"{'ID':>6} {'frames':>8} {'rate Hz':>8}  meaning")
    shown = counts.most_common(args.top if args.top > 0 else None)
    for can_id, n in shown:
        name = KNOWN_IDS.get(can_id, "")
        print(f" 0x{can_id:03X} {n:8d} {n / dur:8.1f}  {name}")
    if args.top > 0 and len(counts) > args.top:
        rest = sum(n for _, n in counts.most_common()[args.top:])
        print(f"{'...':>6} {rest:8d} {'':8}  ({len(counts) - args.top} more IDs)")
    print()
    if spd_min is None:
        print("wheel speed     : no decodable wheel-speed frames")
    else:
        print(f"wheel speed     : {spd_min:.1f} .. {spd_max:.1f} mph "
              f"({counts[wheel_id]} frames @ "
              f"{counts[wheel_id] / dur:.1f} Hz on 0x{wheel_id:03X})")
    print(f"wheel gaps >{args.gap_ms:.0f}ms: {len(gaps):5d}"
          + ("" if not gaps else "   (t_rel_s, gap_ms): "
             + ", ".join(f"({t:.1f}, {g:.0f})" for t, g in gaps[:8])
             + (" ..." if len(gaps) > 8 else "")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
