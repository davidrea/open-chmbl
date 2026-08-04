#!/usr/bin/env python3
"""
BRINGUP.md §4.3 -- boot-repeatability soak.

Resets the logger PCB N times and checks each cycle actually rebooted and
reached app_main. Run from an ESP-IDF environment (`. $IDF_PATH/export.sh`),
with the bringup blink app flashed and nothing else holding the serial port
(close `idf.py monitor` first -- it will silently eat the output):

    python3 reset_soak.py --count 20

HOW THE RESET IS DONE. `idf.py` has no `reset` action (5.5 offers only `flash`
and `monitor`), so this calls esptool's own `HardReset` strategy -- the same one
`idf.py flash` uses for its closing "Hard resetting via RTS pin". Do NOT
substitute `USBJTAGSerialReset`: that is the *enter-download-mode* sequence, it
drives IO0 low, and it leaves the board parked in the ROM bootloader instead of
running the app.

WHY THE APP REPORTS UPTIME. Resetting this board drops the native
USB-serial-JTAG link. Host re-enumeration takes ~2 s, so the boot ROM output,
the bootloader lines and the app banner are all gone before a script can reopen
the port -- they are not capturable over USB alone. (An external USB-UART on the
TP1/TP2 console pads would see all of it; that is the §3.2 path, not done here.)
So the app prints a twice-a-second heartbeat carrying its uptime and reset
reason, and a cycle passes when, after the reset, the heartbeat reappears with a
*fresh* uptime. A board that failed to reset would keep counting up; a board
that failed to reach app_main would go silent.

WHAT THIS COVERS. A chip reset re-runs the boot ROM, re-samples strapping, and
re-reads flash -- which is the substance of §4.3: intermittent flash errors or
strapping that fails to settle show up as a silent or looping cycle. It does NOT
exercise SW1 or the EN RC network (R13/C9), because the reset is commanded over
USB rather than by pulling the EN pin. Press SW1 by hand a few times separately
if that circuit needs covering.
"""

import argparse
import re
import sys
import time

try:
    import serial
    from esptool.reset import HardReset
except ImportError:
    sys.exit("need pyserial + esptool -- run '. $IDF_PATH/export.sh' first")

HEARTBEAT = re.compile(r"soak: alive up=(\d+) ms rst=(\d+)\(([A-Z_]+)\)")
BAD = (
    ("error log", re.compile(r"^E \(\d+\)", re.M)),
    ("assert", re.compile(r"assert failed")),
    ("panic", re.compile(r"Guru Meditation|panic'ed")),
    ("reboot", re.compile(r"Rebooting\.\.\.")),
)


def read_for(port, seconds, until=None):
    """Open `port` and read up to `seconds`, stopping early on `until`."""
    deadline = time.time() + seconds
    buf = b""
    while time.time() < deadline:
        try:
            with serial.Serial(port, 115200, timeout=0.2) as ser:
                while time.time() < deadline:
                    buf += ser.read(4096)
                    if until and until.search(buf.decode("utf-8", "replace")):
                        return buf.decode("utf-8", "replace")
        except (serial.SerialException, OSError):
            time.sleep(0.05)  # mid re-enumeration; try again
    return buf.decode("utf-8", "replace")


def one_cycle(port, settle, fresh_ms):
    with serial.Serial(port, 115200, timeout=0.2) as ser:
        # Force a known idle state first. pyserial asserts DTR on open (macOS),
        # and with DTR already high the USB-JTAG peripheral does not act on the
        # RTS pulse -- HardReset returns cleanly and the board just keeps
        # running. Without these two lines the soak silently never resets.
        ser.dtr = False
        ser.rts = False
        time.sleep(0.05)
        HardReset(ser, uses_usb=True)()
    log = read_for(port, settle, until=HEARTBEAT)
    hb = HEARTBEAT.search(log)
    bad = [name for name, rx in BAD if rx.search(log)]
    if not hb:
        return False, "no heartbeat after reset (never reached app_main?)", None, None
    up, reason = int(hb.group(1)), hb.group(3)
    if up > fresh_ms:
        return False, f"stale uptime {up} ms -- board did not reset", up, reason
    if bad:
        return False, "saw " + ", ".join(bad), up, reason
    return True, "", up, reason


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem101")
    ap.add_argument("--count", type=int, default=10)
    ap.add_argument("--settle", type=float, default=8.0,
                    help="seconds to wait for the heartbeat to reappear")
    ap.add_argument("--fresh-ms", type=int, default=8000,
                    help="max uptime still considered a fresh boot")
    args = ap.parse_args()

    print(f"§4.3 boot soak: {args.count} resets on {args.port}\n")
    passes, reasons, ups = 0, {}, []
    for i in range(1, args.count + 1):
        ok, why, up, reason = one_cycle(args.port, args.settle, args.fresh_ms)
        if reason:
            reasons[reason] = reasons.get(reason, 0) + 1
        if ok:
            passes += 1
            ups.append(up)
            print(f"  {i:3d}/{args.count}  PASS  up={up:5d} ms  rst={reason}")
        else:
            print(f"  {i:3d}/{args.count}  FAIL  {why}")

    print(f"\n{passes}/{args.count} clean boots")
    if reasons:
        print("reset reasons: " + ", ".join(f"{k} x{v}" for k, v in reasons.items()))
    if ups:
        print(f"uptime at first heartbeat seen: min {min(ups)} ms, max {max(ups)} ms")
    return 0 if passes == args.count else 1


if __name__ == "__main__":
    sys.exit(main())
