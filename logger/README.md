# logger

A self-contained **CAN data logger** on a **custom ESP32-S3 PCB** with an onboard CAN
transceiver. It captures **all** bus traffic (no filtering) in **listen-only** mode
and writes it to the on-board microSD as **PCAN `.trc`** ASCII files that drop
straight into the project's offline decode/replay path (`python-can` + `cantools`,
see [`docs/can-profiles.md`](../docs/can-profiles.md)).

This is the **ride-logging rig** for design element
[**DE-07**](../docs/design/README.md) — a no-Linux ESP32 alternative to the Raspberry
Pi originally sketched for capturing wheel-speed and other in-motion signals. The
stationary bench captures are done separately with PCAN-USB + a laptop.

This board is also the base design the [`transmitter/`](../transmitter) reuses
(same PCB, connectors for the microSD and button/LED removed) — see
[`hardware/README.md §4`](hardware/README.md#4-shared-design-with-the-transmitter).

- [`hardware/`](hardware) — the custom ESP32-S3 PCB: schematics, connectors, power,
  and a known strapping-pin issue to check before relying on a board. See
  [`hardware/README.md`](hardware/README.md).
- [`software/`](software) — ESP-IDF firmware. **Currently still targets the retired
  ESP-WROVER-KIT bring-up hardware (`esp32`)**, not yet ported to the custom board
  above — see the status note in [`software/README.md`](software/README.md). Pins,
  wiring, the `.trc` format, and build/flash steps for the current target are
  documented there.

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

The script carries its dependencies inline ([PEP 723]), so [uv] installs them into an
ephemeral environment on first run — no venv or `pip install` step:

```sh
uv run tools/trc_viz.py logger/40mph_drive_cycle.trc          # interactive dashboard
uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check  # decode + stats, no GUI
```

Prefer plain `pip`? `pip install -r tools/requirements.txt` then run with `python`
instead of `uv run`.

The GUI needs a display; on a headless box run it under `xvfb-run`. Use the play/pause
button and speed multiplier to play in real time (or 0.5/2/4×), or drag the timeline
cursor / scrub slider to seek.

[PEP 723]: https://peps.python.org/pep-0723/
[uv]: https://docs.astral.sh/uv/
