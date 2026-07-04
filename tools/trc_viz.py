#!/usr/bin/env python3
"""Real-time visual playback / scrubber for an open-chmbl CAN ``.trc`` log.

Decodes a PEAK ``.trc`` capture through the ``triumph_tr`` DBC (the same
``python-can`` + ``cantools`` path as :mod:`tools.golden_check`), reproduces the
firmware's derived acceleration and the DE-09 brake state machine, and drives a
live gauge dashboard (Dear PyGui) that plays back or scrubs the ride:

    * throttle %, live tach (rpm), wheel speed (mph), gear, clutch
    * the computed OFF / BRAKING / STOPPED brake-light state

The acceleration derivation is a faithful Python port of ``accel_update`` in
``transmitter/software/main/can_decode.c`` and the state machine implements
``docs/design/de-09-brake-decel-logic.md``. Every FSM tunable is exposed as a
live slider so this doubles as a DE-09 calibration bench.

Usage::

    python3 tools/trc_viz.py logger/40mph_drive_cycle.trc
    python3 tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check
"""

from __future__ import annotations

import argparse
import math
import sys
import time
from dataclasses import dataclass, fields

import numpy as np

import can
import cantools

# ---- decode constants (mirror can_decode.h) -------------------------------
KMH_TO_MPH = 0.621371
ACCEL_WINDOW_MS = 200.0     # CAN_DECODE_ACCEL_WINDOW_MS
ACCEL_ALPHA = 0.3           # CAN_DECODE_ACCEL_ALPHA
SPEED_HIST = 16             # CAN_DECODE_SPEED_HIST
STALE_MS = 1000.0           # CAN_DECODE_STALE_MS
CUTOFF_REASON_VALUE = 0x28

# CAN IDs of interest (decimal frame ids in the DBC)
ID_WHEEL = 0x102
ID_ENGINE_LIVE = 0x140
ID_TRANS = 0x142
ID_ENGINE_ECU = 0x146
ID_BODY = 0x121

# FSM poll grid: 50 Hz (20 ms) per the DE-09 spec.
FSM_DT_MS = 20.0

# Brake FSM states.
OFF, BRAKING, STOPPED = 0, 1, 2
STATE_NAMES = {OFF: "OFF", BRAKING: "BRAKING", STOPPED: "STOPPED"}


# ---- FSM tunables ----------------------------------------------------------
@dataclass
class BrakeTunables:
    """DE-09 tunables. Values stated in the design doc are fixed here; the four
    it leaves open (decel_on, accel_off, steady_band, steady_timeout) and the
    anti-strobe dwell get sensible defaults, all overridable via the UI/CLI."""

    decel_on_mphps: float = 2.0        # rule 1 trigger (open — default)
    decel_on_debounce_ms: float = 120.0  # doc: the knee
    stop_speed_mph: float = 1.0        # doc: STOPPED entry / rolling qualifier
    moving_speed_mph: float = 3.0      # doc: STOPPED->OFF hysteresis
    accel_off_mphps: float = 0.5       # rule 4 (open — default)
    accel_off_min_speed_mph: float = 5.0   # doc: rule 4 "speed > 5"
    steady_band_mphps: float = 0.75    # rule 5 (open — default)
    steady_timeout_ms: float = 1500.0  # rule 5 (open — default)
    stop_timeout_ms: float = 60000.0   # doc: "the 60 s timeout fires"
    state_min_dwell_ms: float = 250.0  # anti-strobe floor (open — default)


# ---- decode pass -----------------------------------------------------------
@dataclass
class DecodedLog:
    """Per-signal event series (relative seconds) plus the resampled 50 Hz grid
    used for both playback and the FSM."""

    duration_s: float
    grid_t: np.ndarray          # seconds, 50 Hz
    speed_mph: np.ndarray       # forward-filled onto grid
    accel_mphps: np.ndarray
    throttle_pct: np.ndarray
    rpm_live: np.ndarray
    rpm_ecu: np.ndarray
    gear: np.ndarray
    clutch_pulled: np.ndarray
    engine_cutoff: np.ndarray


def _resample(evt_t: list, evt_v: list, grid_t: np.ndarray,
              fill: float = math.nan) -> np.ndarray:
    """Forward-fill event samples onto ``grid_t`` (zero-order hold)."""
    if not evt_t:
        return np.full_like(grid_t, fill)
    et = np.asarray(evt_t)
    ev = np.asarray(evt_v)
    # index of the latest event at or before each grid point
    idx = np.searchsorted(et, grid_t, side="right") - 1
    out = np.where(idx >= 0, ev[idx.clip(min=0)], fill)
    return out


def _compute_accel(spd_t_ms: list, spd_v_mph: list) -> tuple:
    """Faithful port of ``accel_update`` (can_decode.c): 16-deep ring, slope
    against the newest sample >= 200 ms back, prime-then-EMA (alpha 0.3).
    Returns (event_times_ms, accel_values) aligned to the wheel-speed frames."""
    n = len(spd_t_ms)
    out = np.full(n, math.nan)
    ring_v = [0.0] * SPEED_HIST
    ring_t = [0.0] * SPEED_HIST
    head = 0
    count = 0
    primed = False
    accel = math.nan
    for k in range(n):
        now = spd_t_ms[k]
        sp = spd_v_mph[k]
        ring_v[head] = sp
        ring_t[head] = now
        head = (head + 1) % SPEED_HIST
        if count < SPEED_HIST:
            count += 1
        best = -1
        for i in range(count):
            idx = (head + SPEED_HIST - 1 - i) % SPEED_HIST
            if now - ring_t[idx] >= ACCEL_WINDOW_MS:
                best = idx
                break
        if best < 0:
            out[k] = accel     # window not yet spanned — hold previous
            continue
        dt_s = (now - ring_t[best]) / 1000.0
        if dt_s <= 0.0:
            out[k] = accel
            continue
        slope = (sp - ring_v[best]) / dt_s
        if not primed:
            accel = slope
            primed = True
        else:
            accel += ACCEL_ALPHA * (slope - accel)
        out[k] = accel
    return spd_t_ms, out


def load_log(dbc_path: str, trc_path: str) -> DecodedLog:
    db = cantools.database.load_file(dbc_path)
    ids = {m.frame_id for m in db.messages}

    evt: dict = {k: ([], []) for k in
                 ("speed", "throttle", "rpm_live", "rpm_ecu", "gear",
                  "clutch", "cutoff")}
    spd_t_ms: list = []
    spd_v_mph: list = []
    t0 = None

    for msg in can.TRCReader(trc_path):
        if msg.arbitration_id not in ids:
            continue
        if t0 is None:
            t0 = msg.timestamp
        t = msg.timestamp - t0
        try:
            dec = db.decode_message(msg.arbitration_id, msg.data)
        except Exception:
            continue
        aid = msg.arbitration_id
        if aid == ID_WHEEL and "WHEEL_SPEED_FRONT" in dec:
            mph = float(dec["WHEEL_SPEED_FRONT"]) * KMH_TO_MPH
            evt["speed"][0].append(t)
            evt["speed"][1].append(mph)
            spd_t_ms.append(t * 1000.0)
            spd_v_mph.append(mph)
        if aid == ID_ENGINE_LIVE:
            if "THROTTLE_PCT" in dec:
                evt["throttle"][0].append(t)
                evt["throttle"][1].append(float(dec["THROTTLE_PCT"]))
            if "RPM_LIVE" in dec:
                evt["rpm_live"][0].append(t)
                evt["rpm_live"][1].append(float(dec["RPM_LIVE"]))
        if aid == ID_ENGINE_ECU and "RPM_ECU" in dec:
            evt["rpm_ecu"][0].append(t)
            evt["rpm_ecu"][1].append(float(dec["RPM_ECU"]))
        if aid == ID_TRANS:
            if "GEAR" in dec:
                evt["gear"][0].append(t)
                evt["gear"][1].append(float(dec["GEAR"]))
            if "CLUTCH_RAW" in dec:
                evt["clutch"][0].append(t)
                evt["clutch"][1].append(1.0 if int(dec["CLUTCH_RAW"]) else 0.0)
        if aid == ID_BODY and "ENGINE_CUTOFF_FLAG" in dec:
            cutoff = (int(dec["ENGINE_CUTOFF_FLAG"]) != 0 and
                      int(dec.get("CUTOFF_REASON", -1)) == CUTOFF_REASON_VALUE)
            evt["cutoff"][0].append(t)
            evt["cutoff"][1].append(1.0 if cutoff else 0.0)

    if t0 is None or not spd_t_ms:
        raise SystemExit("no decodable frames found in " + trc_path)

    duration = max(e[0][-1] if e[0] else 0.0 for e in evt.values())
    grid_t = np.arange(0.0, duration + FSM_DT_MS / 1000.0, FSM_DT_MS / 1000.0)

    at_ms, accel_v = _compute_accel(spd_t_ms, spd_v_mph)
    accel_t = [t / 1000.0 for t in at_ms]

    return DecodedLog(
        duration_s=duration,
        grid_t=grid_t,
        speed_mph=_resample(evt["speed"][0], evt["speed"][1], grid_t, 0.0),
        accel_mphps=_resample(accel_t, list(accel_v), grid_t, math.nan),
        throttle_pct=_resample(evt["throttle"][0], evt["throttle"][1], grid_t, 0.0),
        rpm_live=_resample(evt["rpm_live"][0], evt["rpm_live"][1], grid_t, 0.0),
        rpm_ecu=_resample(evt["rpm_ecu"][0], evt["rpm_ecu"][1], grid_t, 0.0),
        gear=_resample(evt["gear"][0], evt["gear"][1], grid_t, 0.0),
        clutch_pulled=_resample(evt["clutch"][0], evt["clutch"][1], grid_t, 0.0),
        engine_cutoff=_resample(evt["cutoff"][0], evt["cutoff"][1], grid_t, 0.0),
    )


# ---- brake state machine ---------------------------------------------------
def run_fsm(log: DecodedLog, tun: BrakeTunables) -> np.ndarray:
    """Step the DE-09 OFF/BRAKING/STOPPED machine over the 50 Hz grid.
    First-matching-guard-wins, with the anti-strobe min-dwell floor."""
    n = len(log.grid_t)
    speed = log.speed_mph
    accel = log.accel_mphps
    clutch = log.clutch_pulled
    gear = log.gear
    out = np.zeros(n, dtype=np.int8)

    s = OFF
    since_trans = 1e12       # allow the first transition immediately
    decel_hold = 0.0
    steady_hold = 0.0
    stopped_hold = 0.0
    dt = FSM_DT_MS

    for i in range(n):
        sp = speed[i]
        ac = accel[i]
        cl = clutch[i]
        g = gear[i]
        ac_valid = not math.isnan(ac)

        # condition timers (accumulate before evaluating guards)
        decel_active = ac_valid and ac < -tun.decel_on_mphps
        decel_hold = decel_hold + dt if decel_active else 0.0
        steady_active = (s == BRAKING and ac_valid and
                         abs(ac) < tun.steady_band_mphps)
        steady_hold = steady_hold + dt if steady_active else 0.0
        stopped_hold = stopped_hold + dt if s == STOPPED else 0.0

        new_s = s
        if since_trans >= tun.state_min_dwell_ms:
            if s == OFF:
                if decel_hold >= tun.decel_on_debounce_ms:
                    new_s = BRAKING                                  # rule 1
                elif sp < tun.stop_speed_mph:
                    new_s = STOPPED                                  # rule 2
            elif s == BRAKING:
                if sp < tun.stop_speed_mph:
                    new_s = STOPPED                                  # rule 3
                elif (ac_valid and ac > tun.accel_off_mphps and
                      sp > tun.accel_off_min_speed_mph):
                    new_s = OFF                                      # rule 4
                elif steady_hold >= tun.steady_timeout_ms:
                    new_s = OFF                                      # rule 5
            else:  # STOPPED
                if sp > tun.moving_speed_mph:
                    new_s = OFF                                      # rule 6a
                elif cl == 0.0 and g != 0.0 and sp > tun.stop_speed_mph:
                    new_s = OFF                                      # rule 6b
                elif stopped_hold >= tun.stop_timeout_ms:
                    new_s = OFF                                      # rule 6c

        if new_s != s:
            s = new_s
            since_trans = 0.0
            decel_hold = 0.0
            steady_hold = 0.0
            stopped_hold = 0.0
        else:
            since_trans += dt
        out[i] = s
    return out


def fsm_stats(state: np.ndarray, dt_s: float) -> dict:
    light_on = state != OFF
    transitions = int(np.count_nonzero(np.diff(state.astype(np.int16))))
    return {
        "transitions": transitions,
        "on_time_s": float(np.count_nonzero(light_on) * dt_s),
        "on_frac": float(np.mean(light_on)) if len(state) else 0.0,
    }


# ---- headless check --------------------------------------------------------
def headless_check(log: DecodedLog, tun: BrakeTunables) -> int:
    state = run_fsm(log, tun)
    st = fsm_stats(state, FSM_DT_MS / 1000.0)
    peak = float(np.nanmax(log.speed_mph))
    print(f"duration        : {log.duration_s:8.1f} s")
    print(f"grid points     : {len(log.grid_t):8d}  @ {1000/FSM_DT_MS:.0f} Hz")
    print(f"peak speed      : {peak:8.1f} mph")
    print(f"peak rpm (live) : {float(np.nanmax(log.rpm_live)):8.0f} rpm")
    print(f"peak throttle   : {float(np.nanmax(log.throttle_pct)):8.1f} %")
    print(f"gears seen      : {sorted(set(int(g) for g in np.unique(log.gear)))}")
    print(f"accel range     : {float(np.nanmin(log.accel_mphps)):+.1f} .. "
          f"{float(np.nanmax(log.accel_mphps)):+.1f} mph/s")
    print(f"FSM transitions : {st['transitions']:8d}")
    print(f"brake light on  : {st['on_time_s']:8.1f} s "
          f"({st['on_frac']*100:.0f}% of ride)")
    ok = 40.0 <= peak <= 46.0
    print(f"peak-speed sanity (43 mph ± 3): {'PASS' if ok else 'CHECK'}")
    return 0


# ---- Dear PyGui dashboard --------------------------------------------------
def run_gui(log: DecodedLog, tun: BrakeTunables, selftest: int = 0,
            snapshot: str = "") -> int:
    import dearpygui.dearpygui as dpg

    dt_s = FSM_DT_MS / 1000.0
    state = {"arr": run_fsm(log, tun)}
    play = {"on": False, "t": 0.0, "rate": 1.0}
    grid = log.grid_t
    dur = log.duration_s

    # palette (dataviz-style dark dashboard)
    BG = (18, 20, 24)
    PANEL = (28, 31, 38)
    INK = (226, 232, 240)
    MUTED = (140, 150, 165)
    ACCENT = (90, 170, 255)
    WARN = (255, 176, 32)
    BRAKE_RED = (240, 60, 55)
    GREEN = (60, 200, 120)

    def sample_idx(t: float) -> int:
        return int(min(len(grid) - 1, max(0, round(t / dt_s))))

    def recompute_fsm():
        state["arr"] = run_fsm(log, tun)
        # refresh timeline brake trace + stats readout
        on = (state["arr"] != OFF).astype(float)
        dpg.set_value("brake_series", [list(grid), list(on)])
        st = fsm_stats(state["arr"], dt_s)
        dpg.set_value("fsm_stats",
                      f"transitions: {st['transitions']}    "
                      f"light on: {st['on_time_s']:.1f}s "
                      f"({st['on_frac']*100:.0f}%)")

    # ---- gauge drawing -----------------------------------------------------
    def draw_gauge(tag, cx, cy, r, label):
        """Static gauge face: outer ring + 240 deg tick arc. Needle + readout
        are drawn separately and updated each frame."""
        with dpg.draw_layer(tag=tag + "_static"):
            dpg.draw_circle((cx, cy), r, color=(60, 66, 78), thickness=2,
                            fill=PANEL)
            for k in range(11):
                frac = k / 10.0
                th = math.radians(225 - 270 * frac)
                x1 = cx + (r - 10) * math.cos(th)
                y1 = cy - (r - 10) * math.sin(th)
                x2 = cx + r * math.cos(th)
                y2 = cy - r * math.sin(th)
                dpg.draw_line((x1, y1), (x2, y2), color=MUTED, thickness=2)
            dpg.draw_text((cx - 26, cy + r * 0.45), label, size=15,
                          color=MUTED)
        with dpg.draw_layer(tag=tag + "_dyn"):
            dpg.draw_line((cx, cy), (cx, cy - r * 0.75), color=ACCENT,
                          thickness=4, tag=tag + "_needle")
            dpg.draw_circle((cx, cy), 6, fill=INK, color=INK)
            dpg.draw_text((cx - 34, cy - r * 0.30), "0", size=26, color=INK,
                          tag=tag + "_val")

    GAUGES = {
        "speed": dict(cx=130, cy=140, r=110, label="mph", vmax=60.0),
        "rpm": dict(cx=390, cy=140, r=110, label="rpm x1000", vmax=8000.0),
    }

    def set_gauge(tag, cx, cy, r, frac, text):
        frac = min(1.0, max(0.0, frac))
        th = math.radians(225 - 270 * frac)
        dpg.configure_item(tag + "_needle",
                           p2=(cx + r * 0.75 * math.cos(th),
                               cy - r * 0.75 * math.sin(th)))
        dpg.set_value(tag + "_val", text)

    # ---- transport / tuning callbacks -------------------------------------
    def on_play():
        play["on"] = not play["on"]
        if play["on"] and play["t"] >= dur:
            play["t"] = 0.0
        dpg.set_item_label("btn_play", "Pause" if play["on"] else "Play")

    def on_rate(_s, val):
        play["rate"] = float(val.replace("x", ""))

    def on_seek_slider(_s, val):
        play["t"] = float(val)

    def on_seek_cursor(_s, _a, _u):
        play["t"] = min(dur, max(0.0, dpg.get_value("scrub_line")))

    def make_tun_cb(field):
        def cb(_s, val):
            setattr(tun, field, float(val))
            recompute_fsm()
        return cb

    # ---- layout ------------------------------------------------------------
    dpg.create_context()
    with dpg.theme() as global_theme:
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_WindowBg, BG)
            dpg.add_theme_color(dpg.mvThemeCol_ChildBg, PANEL)
            dpg.add_theme_color(dpg.mvThemeCol_Text, INK)
    dpg.bind_theme(global_theme)

    with dpg.window(tag="main"):
        with dpg.group(horizontal=True):
            # left: gauges + indicators
            with dpg.child_window(width=540, height=340):
                with dpg.drawlist(width=520, height=300, tag="gauges"):
                    for _name, _g in GAUGES.items():
                        draw_gauge(_name, _g["cx"], _g["cy"], _g["r"],
                                   _g["label"])
            # right: gear / clutch / brake indicators
            with dpg.child_window(width=320, height=340):
                dpg.add_text("GEAR", color=MUTED)
                dpg.add_text("N", tag="gear_val", color=INK)
                dpg.add_spacer(height=6)
                dpg.add_text("throttle", color=MUTED)
                dpg.add_progress_bar(tag="throttle_bar", width=280,
                                     overlay="0 %")
                dpg.add_spacer(height=10)
                dpg.add_text("clutch: --", tag="clutch_val", color=MUTED)
                dpg.add_text("engine cutoff: --", tag="cutoff_val", color=MUTED)
                dpg.add_spacer(height=14)
                dpg.add_text("BRAKE LIGHT", color=MUTED)
                dpg.add_text("OFF", tag="brake_state", color=GREEN)
                dpg.add_text("accel: 0.0 mph/s", tag="accel_val", color=MUTED)

        # timeline strip chart
        with dpg.plot(height=200, width=-1, tag="timeline",
                      no_menus=True):
            dpg.add_plot_axis(dpg.mvXAxis, label="time (s)", tag="tl_x")
            with dpg.plot_axis(dpg.mvYAxis, label="speed (mph)", tag="tl_y"):
                dpg.add_line_series(list(grid), list(log.speed_mph),
                                    label="speed", tag="speed_series")
            with dpg.plot_axis(dpg.mvYAxis2, label="brake", tag="tl_y2"):
                on = (state["arr"] != OFF).astype(float)
                dpg.add_line_series(list(grid), list(on), label="brake on",
                                    tag="brake_series", parent="tl_y2")
            dpg.set_axis_limits("tl_y2", -0.05, 1.2)
            dpg.add_drag_line(tag="scrub_line", color=WARN, default_value=0.0,
                              vertical=True, callback=on_seek_cursor)

        # transport controls
        with dpg.group(horizontal=True):
            dpg.add_button(label="Play", tag="btn_play", callback=on_play,
                           width=90)
            dpg.add_combo(("0.5x", "1x", "2x", "4x"), default_value="1x",
                          width=70, callback=on_rate)
            dpg.add_slider_float(tag="scrub", width=-1, min_value=0.0,
                                 max_value=dur, callback=on_seek_slider)

        # FSM tuning panel
        with dpg.collapsing_header(label="DE-09 brake FSM tunables",
                                   default_open=True):
            dpg.add_text("", tag="fsm_stats", color=WARN)
            slider_specs = [
                ("decel_on_mphps", 0.2, 8.0),
                ("decel_on_debounce_ms", 0.0, 500.0),
                ("accel_off_mphps", 0.1, 5.0),
                ("accel_off_min_speed_mph", 0.0, 15.0),
                ("steady_band_mphps", 0.1, 3.0),
                ("steady_timeout_ms", 200.0, 5000.0),
                ("moving_speed_mph", 1.0, 10.0),
                ("stop_speed_mph", 0.2, 4.0),
                ("state_min_dwell_ms", 0.0, 1000.0),
                ("stop_timeout_ms", 5000.0, 120000.0),
            ]
            for name, lo, hi in slider_specs:
                dpg.add_slider_float(label=name, min_value=lo, max_value=hi,
                                     default_value=getattr(tun, name),
                                     width=320, callback=make_tun_cb(name))

    recompute_fsm()

    dpg.create_viewport(title="open-chmbl CAN decode playback",
                        width=900, height=780)
    dpg.setup_dearpygui()
    dpg.show_viewport()
    dpg.set_primary_window("main", True)

    # In self-test mode, sweep the playhead across the whole ride over a
    # fixed number of frames and exit — lets CI/headless drivers exercise the
    # full UI (gauges, timeline, FSM recompute) without a real display.
    if selftest:
        if snapshot:
            dpg.set_frame_callback(selftest - 2,
                                   lambda: dpg.output_frame_buffer(snapshot))
        frame = 0

    # ---- render loop -------------------------------------------------------
    last = time.perf_counter()
    while dpg.is_dearpygui_running():
        now = time.perf_counter()
        frame_dt = now - last
        last = now
        if selftest:
            frame += 1
            play["t"] = dur * (frame / selftest)
            if frame >= selftest:
                dpg.render_dearpygui_frame()
                break
        if play["on"]:
            play["t"] += frame_dt * play["rate"]
            if play["t"] >= dur:
                play["t"] = dur
                play["on"] = False
                dpg.set_item_label("btn_play", "Play")

        i = sample_idx(play["t"])
        sp = float(log.speed_mph[i])
        rpm = float(log.rpm_live[i])
        thr = float(log.throttle_pct[i])
        gr = int(round(log.gear[i]))
        cl = log.clutch_pulled[i] > 0.5
        cut = log.engine_cutoff[i] > 0.5
        ac = log.accel_mphps[i]
        st = int(state["arr"][i])

        gs = GAUGES["speed"]
        set_gauge("speed", gs["cx"], gs["cy"], gs["r"], sp / gs["vmax"],
                  f"{sp:4.1f}")
        gr_g = GAUGES["rpm"]
        set_gauge("rpm", gr_g["cx"], gr_g["cy"], gr_g["r"], rpm / gr_g["vmax"],
                  f"{rpm:0.0f}")

        dpg.set_value("gear_val", "N" if gr == 0 else str(gr))
        dpg.set_value("throttle_bar", max(0.0, min(1.0, thr / 100.0)))
        dpg.configure_item("throttle_bar", overlay=f"{thr:.0f} %")
        dpg.set_value("clutch_val", f"clutch: {'PULLED' if cl else 'released'}")
        dpg.set_value("cutoff_val",
                      f"engine cutoff: {'YES' if cut else 'no'}")
        dpg.set_value("accel_val",
                      "accel: --" if math.isnan(ac)
                      else f"accel: {ac:+.1f} mph/s")
        dpg.set_value("brake_state", STATE_NAMES[st])
        dpg.configure_item("brake_state",
                           color=BRAKE_RED if st != OFF else GREEN)

        # keep scrub widgets in sync without firing their callbacks
        dpg.set_value("scrub", play["t"])
        dpg.set_value("scrub_line", play["t"])

        dpg.render_dearpygui_frame()

    dpg.destroy_context()
    return 0


# ---- entry -----------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trc", help="PCAN .trc capture to play back")
    ap.add_argument("--dbc", default="profiles/triumph_tr.dbc")
    ap.add_argument("--headless-check", action="store_true",
                    help="decode + run FSM, print stats, no GUI")
    ap.add_argument("--selftest-frames", type=int, default=0,
                    help="render N frames sweeping the ride then exit (testing)")
    ap.add_argument("--snapshot", default="",
                    help="write a frame-buffer PNG during --selftest-frames")
    for f in fields(BrakeTunables):
        ap.add_argument("--" + f.name.replace("_", "-"), type=float,
                        default=None, dest=f.name,
                        help=f"FSM tunable (default {getattr(BrakeTunables, f.name)})")
    args = ap.parse_args(argv)

    tun = BrakeTunables()
    for f in fields(BrakeTunables):
        v = getattr(args, f.name)
        if v is not None:
            setattr(tun, f.name, v)

    log = load_log(args.dbc, args.trc)
    if args.headless_check:
        return headless_check(log, tun)
    return run_gui(log, tun, selftest=args.selftest_frames,
                   snapshot=args.snapshot)


if __name__ == "__main__":
    sys.exit(main())
