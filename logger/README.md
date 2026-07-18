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

Open [`tools/trc_viz.html`](../tools/trc_viz.html) directly in any modern browser,
then drag a logger `.trc` file onto the page (or use **Open trace**). It is one local
HTML file with no server, install, or network access required. The viewer decodes the
Triumph TR profile in-browser and provides live playback gauges, a zoomable timeline,
nearby raw-frame inspection, and live [DE-09 brake FSM](../docs/design/de-09-brake-decel-logic.md)
tuning. It does not upload or retain the trace.

For automated decode comparisons and the older native dashboard, the Python tools
remain available:

```sh
uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check
```

[uv]: https://docs.astral.sh/uv/
