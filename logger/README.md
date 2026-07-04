# logger

A self-contained **CAN data logger** built on an **ESP-WROVER-KIT v4.1** plus an
external CAN transceiver. It captures **all** bus traffic (no filtering) in
**listen-only** mode and writes it to the on-board microSD as **PCAN `.trc`** ASCII
files that drop straight into the project's offline decode/replay path
(`python-can` + `cantools`, see [`docs/can-profiles.md`](../docs/can-profiles.md)).

This is the **ride-logging rig** for design element
[**DE-07**](../docs/design/README.md) — a no-Linux ESP32 alternative to the Raspberry
Pi originally sketched for capturing wheel-speed and other in-motion signals. The
stationary bench captures are done separately with PCAN-USB + a laptop.

Unlike the `transmitter/` and `brake_light/` units, this device is built on an
**off-the-shelf dev kit**, so there is no custom board — hence a **`software/` folder
only**, no `hardware/`.

- [`software/`](software) — ESP-IDF firmware (target `esp32`). Pins, wiring, the
  `.trc` format, and build/flash steps are documented in
  [`software/README.md`](software/README.md).

> Listen-only by default — the logger never ACKs or transmits, per the repo's
> [golden rule](../docs/can-profiles.md#1-golden-rule-listen-only). Power-loss and
> card-removal robustness are intentionally out of scope.

## Visualizing a log

[`tools/trc_viz.py`](../tools/trc_viz.py) plays back or scrubs a `.trc` on a PC with
live gauges — decoded **throttle, rpm, speed, gear, clutch** plus the computed
**brake-light state**. It decodes through `profiles/triumph_tr.dbc` (same `python-can`
+ `cantools` path as the golden test), reproduces the firmware's derived acceleration
(`can_decode.c`) and the [DE-09 brake FSM](../docs/design/de-09-brake-decel-logic.md),
and exposes every FSM tunable as a live slider so it doubles as a calibration bench.

```sh
pip install -r tools/requirements.txt
python tools/trc_viz.py logger/40mph_drive_cycle.trc          # interactive dashboard
python tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check  # decode + stats, no GUI
```

The GUI needs a display; on a headless box run it under `xvfb-run`. Use the play/pause
button and speed multiplier to play in real time (or 0.5/2/4×), or drag the timeline
cursor / scrub slider to seek.
