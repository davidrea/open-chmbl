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
