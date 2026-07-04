#!/usr/bin/env python3
"""Generate a const bike_profile_t data table from a bike's DBC.

The DBC under profiles/ is the machine-readable ground truth for a bike's
CAN layout (docs/can-profiles.md §3). This script parses it with cantools
and emits the compact C data table the embedded decoder interprets — so the
offline python validation path and the firmware always decode from the same
source, with no hand transcription.

Usage:
    python3 tools/gen_profile.py profiles/triumph_tr.dbc \
        --name "Triumph Speed 400 / Scrambler 400X (TR-series)" \
        --bitrate 500000 \
        --symbol bike_profile_triumph_tr \
        --out transmitter/software/main/bike_profile_triumph_tr.c

Regenerate and commit the output whenever the DBC changes; CI fails if the
committed file is stale.
"""

import argparse
import sys

import cantools

# DBC signal name -> bike_profile_t field. Every profile DBC must use these
# names for the signals it provides; unmapped DBC signals are an error so
# nothing silently falls out of the profile.
FIELD_MAP = {
    "WHEEL_SPEED_FRONT": "wheel_speed",
    "WHEEL_SPEED_REAR": "wheel_speed_rear",
    "CLUTCH_RAW": "clutch_raw",
    "GEAR": "gear",
    "THROTTLE_PCT": "throttle_pct",
    "RPM_LIVE": "rpm",
    "RPM_ECU": "rpm_ecu",
    "SIDE_STAND_UP": "side_stand_up",
    "ENGINE_CUTOFF_FLAG": "engine_cutoff_flag",
    "CUTOFF_REASON": "cutoff_reason",
}

REQUIRED = {"WHEEL_SPEED_FRONT", "CLUTCH_RAW", "GEAR"}

CUTOFF_REASON_VALUE = 0x28  # observed kill-switch reason code (can-profiles.md §5)


def signal_initializer(field, frame_id, sig):
    order = "CAN_SIG_BE" if sig.byte_order == "big_endian" else "CAN_SIG_LE"
    return (
        f"    .{field} = {{ .can_id = 0x{frame_id:03X}, .bit_start = {sig.start}, "
        f".bit_len = {sig.length}, .byte_order = {order}, "
        f".is_signed = {1 if sig.is_signed else 0}, "
        f".scale = {float(sig.scale)!r}f, .offset = {float(sig.offset)!r}f }},"
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dbc")
    ap.add_argument("--name", required=True, help="human-readable profile name")
    ap.add_argument("--bitrate", type=int, required=True, help="bus bit rate")
    ap.add_argument("--symbol", required=True, help="C symbol / file base name")
    ap.add_argument("--out", help="output .c path (default: stdout)")
    args = ap.parse_args()

    db = cantools.database.load_file(args.dbc)

    found = {}
    kmh = False
    for msg in db.messages:
        for sig in msg.signals:
            if sig.name not in FIELD_MAP:
                sys.exit(f"error: DBC signal {sig.name} has no bike_profile_t field "
                         f"mapping (add it to FIELD_MAP in {sys.argv[0]})")
            if sig.name in found:
                sys.exit(f"error: duplicate signal {sig.name}")
            found[sig.name] = (msg.frame_id, sig)
            if sig.name == "WHEEL_SPEED_FRONT":
                kmh = sig.unit == "km/h"

    missing = REQUIRED - found.keys()
    if missing:
        sys.exit(f"error: DBC is missing required signal(s): {sorted(missing)}")

    lines = [
        "/*",
        f" * GENERATED FILE — do not edit by hand.",
        f" * Source of truth: {args.dbc}",
        f" * Regenerate: python3 tools/gen_profile.py {args.dbc} \\",
        f" *     --name \"{args.name}\" --bitrate {args.bitrate} \\",
        f" *     --symbol {args.symbol} --out <this file>",
        " */",
        '#include "bike_profile.h"',
        "",
        f"const bike_profile_t {args.symbol} = {{",
        f'    .name = "{args.name}",',
        f"    .bitrate = {args.bitrate},",
        f"    .wheel_speed_kmh = {1 if kmh else 0},",
        f"    .cutoff_reason_value = 0x{CUTOFF_REASON_VALUE:02X},",
    ]
    for dbc_name, field in FIELD_MAP.items():
        if dbc_name in found:
            frame_id, sig = found[dbc_name]
            lines.append(signal_initializer(field, frame_id, sig))
        else:
            lines.append(f"    .{field} = {{ .can_id = 0 }}, /* not on this bike */")
    lines.append("};")
    lines.append("")

    text = "\n".join(lines)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
