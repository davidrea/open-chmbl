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
- [`software/`](software) — ESP-IDF firmware, targeting the custom board above
  (`esp32s3`; the retired ESP-WROVER-KIT is no longer supported). Pins, wiring, the
  `.trc` format, the recording policy, the FSM tunables and build/flash steps are all
  documented in [`software/README.md`](software/README.md).

> Listen-only by default — the logger never ACKs or transmits, per the repo's
> [golden rule](../docs/can-profiles.md#1-golden-rule-listen-only). The TCAN330's
> silent pin is read back at boot (so a missing `R16` is reported rather than masked)
> and then driven high, and the TWAI controller is held in listen-only mode as well.

## Two jobs at once: logging, and the brake-light preview

The firmware is the **ride-validation build**. Alongside capturing the bus it runs the
real [DE-09 braking state machine](../docs/design/de-09-brake-decel-logic.md) on-board,
against the real decoded signals, at the real 50 Hz tick — and lights the **remote LED**
on `J4` pin 2 whenever it would be commanding the rider-side brake light **ON**
(`BRAKING` or `STOPPED`; steady, never blinking).

There is **no ESP-NOW in this build**: the LED stands in for the radio. So you can ride
with the logger, watch the FSM's actual decision on a panel-mount LED, and afterwards
read the `.trc` together with the console transition log — which records every
transition with its rule number and the decisive speed/accel/clutch/gear values — to see
exactly why it decided that. The product path remains the
[`transmitter`](../transmitter).

**Recording is automatic and silent.** There is no start/stop button and no status LED:
the device opens a new `N.trc` whenever the engine kill switch reads **RUN**, and closes
it when the switch goes to STOP or the bus falls silent. It flushes and `fsync`s every
couple of seconds so losing 12 V mid-ride still leaves a readable trace. Details and
timeouts: [`software/README.md`](software/README.md).

> **Power loss** no longer loses the whole capture — the periodic flush/`fsync` bounds
> the loss to the last couple of seconds — but a file cut short that way has no
> `;closed:` footer. Card-removal robustness goes no further than shutting the capture
> down cleanly rather than wedging the writer.

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
