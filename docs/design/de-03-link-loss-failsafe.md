# DE-03 — Link-loss failsafe

**Status:** 🟡 placeholder landed · **Device(s):** brake_light · **Depends on:** DE-00, DE-01

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
- `LINK_TIMEOUT_MS` (≤ 300 ms), fault-blink period, running-light level.

## 5. Firmware module/task decomposition
- Link-watchdog tick (~10 Hz): compute link state from `now - last_rx_time`; publish a
  `link_status` that the render engine (DE-04) honours above the received state.
- Pure/host-testable: the timeout state machine (fed synthetic timestamps).

## 6. CLI hooks
- `link show` (state, last-rx age, timeout), `in source link|fake`, `render show`.

## 7. Isolation acceptance
- Steady heartbeat → normal; stop it → within `LINK_TIMEOUT_MS` the link-lost
  indication appears (running light + slow blink), **never dark, never `BRAKE`**;
  resume → returns to normal; cold boot before first packet → "waiting" indication.

## 8. Open items
- Exact link-lost vs. waiting visual distinction.
- Whether a brief glitch should hysteresis-hold before declaring loss.

## 9. Implementation notes

A **placeholder** landed alongside DE-01 (`link.c`), since the ESP32 DevKitC bench
boards only expose the one stand-in LED (no running-light + separate status LED yet,
DE-10). The 10 Hz-ish link watchdog computes `WAITING`/`UP`/`LOST` from the last-rx
age and, while up, mirrors the received state onto the stand-in brake light exactly
as designed. But for both `WAITING` and `LOST` — since there's only one LED to work
with — it just **blinks that same LED** (`CHMBL_LINK_BLINK_MS` half-period) rather
than the designed "steady running light + slow fault blink". This never latches a
fake `BRAKE` and is observable via `net stop`/`net start` on the transmitter plus
`link show` on the brake_light, but is not yet the real indication — that lands with
DE-10 once a second LED is available. No hysteresis on the timeout yet either.
