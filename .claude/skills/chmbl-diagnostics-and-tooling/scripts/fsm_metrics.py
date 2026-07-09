#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "cantools>=39",
#     "python-can>=4.3",
#     "numpy>=1.26",
# ]
# ///
"""DE-09 FSM replay metrics that `trc_viz.py --headless-check` does NOT print:
per-episode brake-light statistics, most importantly the count of sub-0.5 s
"blips" — the second number in the project's accepted dry-run baselines
(transitions 162->48->30, blips 65->8->3 on the 40 mph ride log).

This is a thin metrics layer over tools/trc_viz.py — it imports trc_viz's own
load_log / derive_accel / run_fsm so the FSM under measurement is byte-for-byte
the calibration bench's, never a reimplementation. All BrakeTunables are
exposed as flags with the same names trc_viz uses.

Usage:
    uv run .claude/skills/chmbl-diagnostics-and-tooling/scripts/fsm_metrics.py \
        logger/40mph_drive_cycle.trc [--decel-on-mphps 3.0] [--blip-s 0.5] ...

Run from the repo root (or pass --trc-viz with the path to tools/trc_viz.py).
Exit status is always 0; it is a measurement tool, not a gate.
"""

from __future__ import annotations

import argparse
import importlib.util
import sys
from dataclasses import fields
from pathlib import Path

import numpy as np


def load_trc_viz(path: Path):
    spec = importlib.util.spec_from_file_location("trc_viz", path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["trc_viz"] = mod
    spec.loader.exec_module(mod)
    return mod


def episodes(on: np.ndarray, dt_s: float):
    """Yield (start_s, dur_s) for each contiguous light-on run."""
    diff = np.diff(np.concatenate(([0], on.astype(np.int8), [0])))
    starts = np.flatnonzero(diff == 1)
    ends = np.flatnonzero(diff == -1)
    for s, e in zip(starts, ends):
        yield s * dt_s, (e - s) * dt_s


def main(argv=None) -> int:
    default_viz = Path(__file__).resolve()
    # repo root = four levels up from .claude/skills/<name>/scripts/
    default_viz = default_viz.parents[4] / "tools" / "trc_viz.py"

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("trc", help="PCAN .trc capture to replay")
    ap.add_argument("--dbc", default="profiles/triumph_tr.dbc")
    ap.add_argument("--trc-viz", default=str(default_viz),
                    help="path to tools/trc_viz.py (FSM source of truth)")
    ap.add_argument("--blip-s", type=float, default=0.5,
                    help="on-episodes shorter than this count as blips "
                         "(default 0.5 s, matching docs/firmware.md)")
    args, extra = ap.parse_known_args(argv)

    viz = load_trc_viz(Path(args.trc_viz))

    # accept every BrakeTunables field as a flag, same spelling as trc_viz
    tp = argparse.ArgumentParser()
    for f in fields(viz.BrakeTunables):
        tp.add_argument("--" + f.name.replace("_", "-"), type=float,
                        default=None, dest=f.name)
    tv = tp.parse_args(extra)
    tun = viz.BrakeTunables()
    overrides = []
    for f in fields(viz.BrakeTunables):
        v = getattr(tv, f.name)
        if v is not None:
            setattr(tun, f.name, v)
            overrides.append(f"{f.name}={v:g}")

    log = viz.load_log(args.dbc, args.trc)
    accel = viz.derive_accel(log.raw_spd_t_ms, log.raw_spd_v_mph, log.grid_t,
                             tun.speed_smooth_ms)
    state = viz.run_fsm(log, tun, accel)
    dt_s = viz.FSM_DT_MS / 1000.0

    on = state != viz.OFF
    eps = list(episodes(on, dt_s))
    blips = [(t, d) for t, d in eps if d < args.blip_s]
    transitions = int(np.count_nonzero(np.diff(state.astype(np.int16))))
    durs = np.array([d for _, d in eps]) if eps else np.array([0.0])

    print(f"capture          : {args.trc}")
    print(f"tunable overrides: {', '.join(overrides) if overrides else '(none — trc_viz defaults)'}")
    print(f"FSM transitions  : {transitions:6d}")
    print(f"light-on episodes: {len(eps):6d}")
    print(f"blips (<{args.blip_s:.1f} s)    : {len(blips):6d}")
    print(f"on-time          : {float(on.sum()) * dt_s:8.1f} s "
          f"({100.0 * on.mean():.0f}% of ride)")
    print(f"episode duration : min {durs.min():.2f} s, "
          f"median {float(np.median(durs)):.2f} s, max {durs.max():.2f} s")
    if blips:
        print("blip times (s)   : "
              + ", ".join(f"{t:.1f}" for t, _ in blips[:12])
              + (" ..." if len(blips) > 12 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
