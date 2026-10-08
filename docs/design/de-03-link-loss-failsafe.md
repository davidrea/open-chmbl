# DE-03 — Link-loss failsafe

**Status:** 🟢 implemented (indication moved to the status LED) · **Device(s):** brake_light · **Depends on:** DE-00, DE-01

The brake_light's behaviour when the radio link degrades or drops. The safety-critical
rule: **fail honest** — never go silently dark, never latch a fake `BRAKE`. See
[`protocol.md §4`](../protocol.md#4-failsafe--link-health).

## 1. Scope & isolation boundary
- **In:** link-state evaluation from last-rx age, the timeout, the link-lost
  indication selection, and the pre-first-packet "waiting" state.
- **Out (faked at edges):** the actual link is provided by DE-01, but for isolation we
  drive `last_rx_time` directly — stop the TX heartbeat, or use `in source fake` and
  let it go stale — and read the resulting indication via `link show` / `render show`.
- **Isolation test:** brake_light board; start/stop the heartbeat (or fake staleness)
  and observe the transition into and out of the link-lost indication.

## 2. FFL traceability
BL-FS-1…4.

## 3. Component selection
None beyond DE-01/DE-04 (pure logic over a timestamp).

## 4. I/O assignments & configuration
- `CHMBL_LINK_TIMEOUT_MS` (300 ms, ≤ 300 ms per the protocol), `CHMBL_LINK_TICK_MS`
  (100 ms ≈ 10 Hz watchdog rate), `CHMBL_STATUS_BLINK_MS` (500 ms, the **status LED's**
  blink half-period).
- There is no running-light level any more: the bar is binary (see §4.1 below), so `OFF`
  is dark.

### 4.1 Where the indication lives — the bar is never blinked

The original design said the link-lost indication is "a steady running light plus a slow
fault blink" **on the bar**, because the bench dev board had only one LED. That is now
wrong on two counts, and has been changed:

1. **The bar is a binary GPIO** — high when braking, low otherwise
   ([DE-04](de-04-led-render.md)). There is no dim tier to render a running light with,
   and a blink of that same pin directly contradicts the contract.
2. **Blinking the brake bar is forbidden.**
   [`safety-regulatory.md §1`](../safety-regulatory.md#1-legal--regulatory-varies-by-jurisdiction)
   rules out flashing stop lamps outright — legally and because crying wolf trains
   following traffic to ignore the light. The old placeholder did exactly this.
3. **The board now has somewhere better to put it.** The first-pass brake_light PCB
   carries two discrete status LEDs, **D13 (RED)** and **D14 (GRN)**, independent of the
   bar — which the old ESP32 DevKitC bench board did not have. That is
   [DE-10](de-10-status-indicator.md)'s job.

So the split is now:

| Link status | Brake bar | Status LED |
|-------------|-----------|------------|
| `UP` | tracks the received state | GRN steady |
| `WAITING` (booted, no packet yet) | **steady off** | GRN slow blink |
| `LOST` | **steady off** | RED slow blink |

**How this still satisfies "never silently dark, never a latched fake `BRAKE`".** The
bar going dark on link loss is, taken alone, the "silently dark" failure the safety doc
warns about — the word that matters is *silently*. It is not silent here:

- the **status LED** switches to a distinct, independent red blink that is legible even
  with the bar off (that is the whole point of DE-10's independence requirement), and
- `link show` on the console reports `LOST`, the last-rx age and the timeout.

And the alternative is worse: with a binary bar the only "not dark" options are a latched
stale `BRAKE` (explicitly forbidden) or a blinking bar (illegal). Holding the bar off and
being loud about it on a separate channel is the honest reading. The bar also *releases*
a stale `BRAKE` promptly — within `LINK_TIMEOUT_MS` + one watchdog tick + at most
`CHMBL_RENDER_MIN_ON_MS` of anti-strobe hold — rather than latching it.

If a non-braking "I'm alive" presence on the bar itself is wanted later, it needs a dim
tier, which means un-deferring [DE-02](de-02-auto-brightness.md)/the PWM path. Tracked in
§8.

## 5. Firmware module/task decomposition
- Link-watchdog tick (`CHMBL_LINK_TICK_MS`, ~10 Hz, `link.c`): compute link state from
  `now - last_rx_time`, then publish **both**
  - the *effective* braking state to the render stage — the received state while `UP`,
    `ST_OFF` otherwise, so the bar is held steady off and a stale `BRAKE` cannot latch;
    and
  - the link status to the status indicator (`status.c`), which owns the visual.
- `link.c` no longer touches the bar GPIOs at all; the render stage is their single
  writer.
- Pure/host-testable: the timeout state machine (fed synthetic timestamps) — **still to
  be factored out**; the dwell/anti-strobe half of the output path *is* host-tested
  (`brake_light/software/test_host/`).

## 6. CLI hooks
- `link show` (status, last state/seq, last-rx age vs. timeout, rx/drop counters).
- `ind show` — the status being indicated and the resulting LED pattern.
- `render show` — confirms the bar is held **off** (and shows any anti-strobe hold while
  a stale `BRAKE` is being released).
- `net stop` / `net start` on the transmitter is the bench way to drive this.
- `in source link|fake` — not implemented.

## 7. Isolation acceptance
- Steady heartbeat → bar tracks the state, GRN steady.
- Stop the heartbeat → within `LINK_TIMEOUT_MS` (+ one tick) the bar goes **steady off**
  and the status LED goes to the RED slow blink; `link show` reports `LOST`.
  **Never a blink of the bar, never a latched `BRAKE`.**
- Resume → returns to normal.
- Cold boot before the first packet → bar off, GRN slow blink ("waiting"), `link show`
  reports `WAITING`.
- ⏳ Pending a built board for the LED half; the bar-side behaviour (steady off, bounded
  release of a stale `BRAKE`) is covered by the host render-core test.

## 8. Open items
- ~~Exact link-lost vs. waiting visual distinction~~ — **resolved** for now: RED slow
  blink vs. GRN slow blink on the status LED (§4.1). Final colour/blink coding belongs to
  the full [DE-10](de-10-status-indicator.md) code table, alongside the battery and fault
  classes.
- Whether a brief glitch should hysteresis-hold before declaring loss. Still open — there
  is no hysteresis on the timeout. Note the render stage's `CHMBL_RENDER_MIN_ON_MS` floor
  already absorbs a short dropout *while braking* (the bar is held lit through it), so
  this matters less than it did.
- Whether the bar should ever carry a non-braking "alive" cue on link loss. Needs a dim
  tier ⇒ gated on un-deferring [DE-02](de-02-auto-brightness.md).
- Factor the timeout state machine into a pure function and host-test it, as §5 intends.

## 9. Implementation notes

**First cut (superseded).** A placeholder landed alongside DE-01 in `link.c`: since the
ESP32 DevKitC bench board exposed only one stand-in LED, the watchdog **blinked that same
brake-light pin** (`CHMBL_LINK_BLINK_MS` half-period) for both `WAITING` and `LOST`. It
never latched a fake `BRAKE`, but it was not the real indication.

**Current (this revision).** With the bar now a binary GPIO (DE-04) and the first-pass
PCB carrying discrete status LEDs, the blink is **gone**:

- `link.c` publishes the effective state to the render stage (`ST_OFF` unless the link is
  `UP`) and the link status to `status.c`. It no longer drives any LED itself.
- The link-lost / waiting indication is on **D13/D14** (§4.1), a minimal slice of
  [DE-10](de-10-status-indicator.md) — link health only, no fault classes or battery
  codes yet.
- `CHMBL_LINK_BLINK_MS` is replaced by `CHMBL_LINK_TICK_MS` (watchdog rate) and
  `CHMBL_STATUS_BLINK_MS` (the status LED's blink), so no setting still implies blinking
  the bar.

Still outstanding: no hysteresis on the timeout, and the timeout state machine is not yet
factored out for host testing (§8).
