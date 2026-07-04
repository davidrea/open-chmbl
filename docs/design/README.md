# Design documents

How we get from capabilities to code: **feature-function lists → design documents →
isolated implementation → integration.** We deliberately avoid formal shall-statement
requirements; the [feature-function lists](../feature-functions.md) are the
capability baseline, and each **design element** below is a self-contained slice we
design and build **one at a time, in isolation**, exercised through the
[developer CLI](../cli.md).

---

## 1. Process

1. **Capabilities** live in [`feature-functions.md`](../feature-functions.md) (FFL IDs).
2. **Design elements** (this directory) each take a small set of FFL IDs and work them
   into a buildable design: components, I/O, firmware tasks, and the CLI hooks that let
   us test the element alone.
3. **Implement one element at a time.** Inputs at the element's boundary are **faked**
   via the CLI; outputs are **viewed** via the CLI. The element is "done" when its FFL
   IDs are demonstrable in isolation.
4. **Integrate** elements once each is independently proven.

Cross-cutting design context already in progress lives one level up:
[`hardware.md`](../hardware.md), [`firmware.md`](../firmware.md),
[`protocol.md`](../protocol.md), [`can-profiles.md`](../can-profiles.md), and the
[`led-brightness-benchmark.md`](../led-brightness-benchmark.md) sizing study (feeds DE-04
and DE-02). Design elements here reference those rather than repeating them.

---

## 2. Design-document template

Every `de-*.md` follows this structure:

1. **Scope & isolation boundary** — what's in, what's out, and *what is faked at the
   edges* to test it alone.
2. **FFL traceability** — the [feature-function](../feature-functions.md) IDs this
   element realizes.
3. **Component selection** — hardware parts and/or software libraries this element
   depends on, with rationale (defer to [`hardware.md`](../hardware.md) for shared parts).
4. **I/O assignments & configuration** — pins, peripherals, registers, bit rates,
   timings, message layouts.
5. **Firmware module/task decomposition** — tasks, queues, shared state, rates,
   ownership; the pure (host-testable) vs. platform parts.
6. **CLI hooks** — the [CLI](../cli.md) commands used to fake inputs / view outputs for
   isolation testing.
7. **Isolation acceptance** — what we demonstrate, via the CLI, to call it done.
8. **Open items.**

---

## 3. Design elements (build order)

Ordered by dependency and the agreed sequence (CLI first so everything else is
testable in isolation; CAN-dependent elements last, after captures exist). Status:
🔲 not started · 🟡 in design · 🟢 implemented.

| ID | Element | Device(s) | Realizes (FFL) | Depends on | Status |
|----|---------|-----------|----------------|-----------|--------|
| **DE-00** | [CLI / shell framework](../cli.md) | both | TX-CLI-*, BL-CLI-* | — | 🟡 |
| **DE-01** | [ESP-NOW link](de-01-espnow-link.md) | both | TX-NET-*, BL-NET-* | DE-00 | 🔲 |
| **DE-02** | [Auto-brightness](de-02-auto-brightness.md) | brake_light | BL-BRT-* | DE-00 | 🔲 |
| **DE-03** | [Link-loss failsafe](de-03-link-loss-failsafe.md) | brake_light | BL-FS-* | DE-00, DE-01 | 🔲 |
| **DE-04** | [LED render & bar driver](de-04-led-render.md) | brake_light | BL-RND-*, BL-LED-* | DE-00 | 🟡 |
| **DE-05** | Battery & charge management | brake_light | BL-PWR-* | DE-00 | 🔲 |
| **DE-06** | TX power / sleep / wake | transmitter | TX-PWR-* | DE-00 | 🔲 |
| **DE-07** | CAN capture & offline analysis — bench (PCAN-USB) + [ride logger](../../logger/) (ESP-WROVER-KIT) | host + [`logger/`](../../logger/) | (enables TX-DEC) | — | 🟡 |
| **DE-08** | [Embedded CAN decode](de-08-can-decode.md) | transmitter | TX-CAN-*, TX-DEC-* | DE-00, DE-07 | 🔲 |
| **DE-09** | [Braking state machine](de-09-brake-decel-logic.md) | transmitter | TX-SM-* | DE-00, DE-08 | 🔲 |
| **DE-10** | [Status-indicator LED](de-10-status-indicator.md) | brake_light | BL-IND-* | DE-00 | 🔲 |

DE-05…DE-06 don't have stub docs yet; they get one when scheduled. DE-07 is the
bench/ride reverse-engineering captured in [`can-profiles.md`](../can-profiles.md); the
**ride-logging half is now a real device** — a self-contained ESP-WROVER-KIT logger
under [`logger/`](../../logger/) (firmware implemented) that replaces the Raspberry Pi
rig, while the stationary bench captures stay on PCAN-USB + a laptop.

This table is the single source of truth for "what's the next isolated piece."

---

## 4. Future-state explorations

Directions we want on record but have **not** committed to the build order above.
They live in [`explorations/`](explorations/README.md) and get promoted to a
`de-*` element (with FFL traceability and an isolation test) only when scheduled:

- [Magnetic mounting](explorations/mounting-magnetic.md) — garment/backpack shoulder
  mount and interchangeable VHB-on-helmet steel targets, both using magnets for a
  built-in shear release.
