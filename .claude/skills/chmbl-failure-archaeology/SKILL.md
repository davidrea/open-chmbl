---
name: chmbl-failure-archaeology
description: >
  The chronicle of every settled investigation, dead end, rejected alternative,
  and pivot in the open-chmbl repo — each as symptom → root cause → evidence →
  status. Load this BEFORE re-investigating any past decision, proposing a
  "new" approach (brake-wire tap, IMU, addressable-LED bar, LCD status display,
  generated per-bike decode code, Raspberry Pi logger), re-deriving a CAN
  signal decode, or debugging a symptom that smells historical (frozen
  acceleration, dropped log frames, watchdog crash at idle, pairing that works
  one-way, -Werror=format CI failures). Do NOT load for step-by-step triage of
  a live failure (use chmbl-debugging-playbook), for the rules that gate
  changes (chmbl-change-control), for current architecture rationale
  (chmbl-architecture-contract), or to execute the DE-09 work
  (chmbl-de09-campaign). This skill tells you which battles are already won or
  lost so you never re-fight one.
---

# open-chmbl failure archaeology

Every major investigation this project has run, with its outcome and the
evidence, so a zero-context session never re-fights a settled battle. Each
dossier: **symptom → root cause → evidence → status**. Verify any hash with
`git show <hash>` in `/home/user/open-chmbl`.

Jargon used below (defined once):

- **CAN** — Controller Area Network, the motorcycle's internal message bus
  (broadcast frames with an 11-bit ID + up to 8 data bytes). **TWAI** is
  Espressif's name for its CAN peripheral/driver.
- **ESP-NOW** — Espressif's connectionless 2.4 GHz radio protocol used for the
  bike→helmet link.
- **DBC** — an industry-standard text format describing how signals are packed
  into CAN frames; `profiles/triumph_tr.dbc` is this repo's ground truth.
- **FSM** — finite state machine; here, the OFF/BRAKING/STOPPED braking logic
  (design element **DE-09**). **DE-nn** = numbered design element docs under
  `docs/design/`.
- **`.trc`** — PEAK PCAN ASCII CAN-capture format; the logger writes it, all
  offline tools read it.
- **WROVER-KIT** — the ESP-WROVER-KIT v4.1 dev board (classic ESP32 + LCD +
  microSD) used as the ride logger. **BSP** = Espressif's board support
  package for it.
- **SMC** — State Machine Compiler, a Java tool planned to generate the DE-09
  FSM's C code from a `.sm` model (not yet in the repo).

## When NOT to use this skill

- Live symptom you need to triage now → `chmbl-debugging-playbook` (it has the
  symptom→experiment tables; this file has the history behind them).
- "May I change X / what's the process?" → `chmbl-change-control`.
- "Why is the system shaped this way?" (invariants, current contracts) →
  `chmbl-architecture-contract`.
- Actually implementing DE-09 / fixing the `SPEED_HIST` bug → that work is
  owned by `chmbl-de09-campaign`; this file only records that the bug exists
  and how it was found.
- CAN/DBC/`.trc` concepts in depth → `chmbl-can-reference`.

## Table of contents

| # | Battle | Verdict | Status |
|---|--------|---------|--------|
| 1 | No brake-switch bit on the bus → wheel-speed-decel pivot | Braking is inferred from CAN wheel-speed deceleration | SETTLED — do not reopen |
| 2 | Free-running broadcast vs. request/response diag port | Free-running broadcast, confirmed experimentally | SETTLED |
| 3 | WROVER-KIT LCD status console | Dead end — LCD DC pin is hardwired to GPIO21, contended by the microSD card-detect; red LED (GPIO0) replaced it | SETTLED — hardware-level, do not re-attempt |
| 4 | Logger dropping ~70% of frames to SD | Default 128 B stdio buffer; fixed with 32 KB `setvbuf` + deeper RX queue | SETTLED |
| 5 | Logger watchdog crash ~5 s after idle boot | CAN controller started at boot spun on garbage; now gated to recording | SETTLED |
| 6 | Asymmetric ESP-NOW pairing race | First board went silent on discovery before the second heard it; fixed with a post-discovery broadcast grace period | SETTLED |
| 7 | Transmitter CI build broken on IDF 5.3 | `esp_driver_twai` component doesn't exist until IDF 5.5; use umbrella `driver` | SETTLED |
| 8 | `-Werror=format` printf failures (twice) | xtensa/riscv `uint32_t` is `long unsigned int`; use `PRIu32` | SETTLED |
| 9 | Build compiled the whole IDF tree (two attempts to trim) | `PRIV_REQUIRES` alone doesn't trim compilation; `COMPONENTS=main` does | SETTLED |
| 10 | trc_viz frozen acceleration arc | Tool bugs fixed; exposed the still-live firmware `CAN_DECODE_SPEED_HIST=16` bug | Tool side SETTLED; firmware bug OPEN (see §Open) |
| 11 | `0x145` B5 as throttle — rejected decode hypothesis | Swept on the bench but dithers 0–100% in-ride; real throttle is `0x140` B0 | SETTLED |
| 12 | `0x121`/`0x113` lamp-cluster byte as side stand — rejected | Fires at first >~10 km/h (lamp self-check clearing); real stand is `0x481` B7 bit 0 | SETTLED |
| 13 | Stop-and-go flicker (FSM strobing in creep traffic) | Hysteresis + debounce tuned on the ride log (162→48→30 transitions) | Mechanism SETTLED; final `MOVING_SPEED` value OPEN |
| 14 | Rejected design alternatives (brake-wire tap, IMU, addressable-LED bar, Pi ride rig, cantools `generate_c_source`) | Each fenced with a written rationale | SETTLED — do not re-propose without new facts |
| — | Open items ledger | `SPEED_HIST` firmware bug; final `MOVING_SPEED`; missing `throttle.trc`/`wheel.trc` bench captures | OPEN |

---

## SETTLED battles

### 1. No brake bit → the wheel-speed-decel pivot

- **Symptom / question:** the entire product concept assumed reading a
  brake-switch bit from the bike's CAN bus. Does the Triumph Speed 400 publish
  one?
- **Investigation:** first profiling ride + repeated captures while working
  the brakes; the ~220 s ride capture `logger/40mph_drive_cycle.trc` (315k
  lines) was committed as evidence.
- **Root cause / finding:** **no bit on the bus toggles with the brakes.**
  Confirmed absent, not merely un-decoded.
- **Resolution:** the braking design was rebuilt around
  **deceleration derived from the bike's own CAN wheel-speed** (`0x102`),
  producing the OFF/BRAKING/STOPPED FSM (DE-09). This deliberately stays clear
  of the inertial-detection patent family — the input is CAN data, not an IMU.
  The pivot touched ARCHITECTURE, firmware, protocol (`ST_DECEL` reserved),
  can-profiles, feature-functions, CLI, DE-08/DE-09, roadmap, and safety docs
  in one change (the docs-are-spec doctrine in action).
- **Evidence:** commits `5c094a5` (the redesign, full rationale in the commit
  body), `f8ba271` (first profiling ride + capture + decode table);
  `docs/roadmap.md` open-questions row "Brake signal source: **Resolved — no
  brake bit on the reference bus**"; `docs/can-profiles.md` §5 resolved box.
- **Status:** SETTLED. Do not go hunting for a brake bit on the reference
  bike, and do not propose IMU or brake-wire alternatives (see dossier 14).

### 2. Broadcast vs. request/response — the listen-only gating question

- **Question:** does the Euro-5 diagnostic port carry free-running broadcast
  CAN traffic, or only request/response diagnostics? This gated the whole
  listen-only architecture — if signals were request-only, passive sniffing
  could never see them and the design would need a rethink.
- **Finding:** **free-running broadcast, confirmed experimentally** — the
  listen-only logger captured continuous traffic (~1490 frames/s at 500
  kbit/s) with no stimulation.
- **Evidence:** `docs/can-profiles.md` §5 resolved box ("_Yes, we have
  confirmed this experimentally_") and decode-table row "Free-running vs.
  request/response → **free-running broadcast**";
  `docs/design/de-08-can-decode.md` ("— **resolved**, free-running").
  Note `docs/roadmap.md` still says "Unknown — Phase 2 gate" for this row —
  that row is stale; can-profiles.md is the doc of record here.
- **Status:** SETTLED for the reference bike. Re-verify per new bike model,
  never assume.

### 3. The WROVER-KIT LCD dead end (full arc)

The most instructive dead end in the repo — two whole attempts, correct
software, killed by a PCB trace. Chronology (all `logger:` commits):

| Commit | Step |
|--------|------|
| `12f2720` | Logger created with an LVGL LCD operations log via the `esp_wrover_kit` BSP |
| `4f17d58` | Backlight was never turned on — fixed (panel still wrong) |
| `ffb8cbf` | Assumed v4.1 ships ST7789; pinned `CONFIG_BSP_LCD_ST7789` — still stripes |
| `5462acb` | Reversed: assumed ILI9341, brought in the real `esp_lcd_ili9341` driver — still stripes |
| `85587d7` | Stopped guessing: direct-draw R/G/B/W/K self-test + verbose init logging to isolate the layer |
| `bc5a73e` | First removal: ripped out the display, serial-only op log |
| `3506323` | Second attempt (PR #7): from-scratch ST7789 console, no LVGL/no BSP, 20 MHz, 8x8 font, panel-ID readback — removing every entangled variable at once |
| `b7bceb8` | On hardware: zero LCD log lines → added tracer logs to bisect bring-up |
| `5ddf728` | Breakthrough on real hardware: **panel renders perfectly with the microSD card removed** (only color-inverted — default flipped); with a card inserted the LCD never updates |
| `b42e09c` | **Verdict: dead end.** LCD removed permanently |
| `51991ae` | Replacement: status on the onboard **red** LED die (GPIO0) |

- **Root cause:** the LCD's DC (data/command) line is **soldered to GPIO21**,
  which the WROVER-KIT also routes to the microSD socket's **card-detect
  switch** — inserting a card grounds that net and contends with DC the entire
  time a card is present. No alternate pin exists. The microSD is the entire
  point of the logger, so the LCD can never coexist with logging. This is a
  **PCB-level conflict, not a driver bug** — no firmware can route around it.
- **Corollary findings:** the RGB LED's green/blue legs (GPIO2/GPIO4) are the
  SD bus's D0/D1 and equally unusable; only the red die on GPIO0 (the boot
  strap pin, sampled once at reset — safe as an output afterward, exactly how
  Espressif's own BSP wires it) remained. Also: the BSP's "ILI9341" menuconfig
  option only flips a mirror flag and always issues ST7789 init commands, so
  attempt-one was partly chasing the wrong controller.
- **Evidence:** commit bodies above (each is a small essay — read
  `git show 5ddf728 b42e09c`); `logger/software/README.md` ("the LCD is a
  dead end here").
- **Status:** SETTLED at the hardware level. Do not re-attempt an LCD on the
  WROVER-KIT logger while it uses the microSD. Any status indication goes
  through the GPIO0 red LED (`status_led.c`, three patterns: heartbeat=idle,
  solid=recording, fast blink=error).

### 4. Logger SD frame drops (~70% loss)

- **Symptom:** at real bus load (~1490 frames/s) the logger recorded only
  ~400 frames/s; wheel-speed ID `0x102` showed up at 22 Hz instead of 100 Hz.
- **Root cause:** the default ~128 B stdio buffer flushed an SD block write
  every few frames; the RX→writer queue overflowed silently.
- **Fix:** `setvbuf` the `.trc` stream to a 32 KB fully-buffered block (fwrite
  batches into full SD clusters); TWAI RX queue grown 32→128 (~85 ms slack);
  a `";dropped-frames: N"` footer comment on close makes every capture
  self-documenting (python-can skips `;` lines).
- **Evidence:** commit `687d400`, merged as **PR #8** (`83320eb`).
  Field-verified: a 10 s idle capture records the full ~1490 fps with
  `dropped-frames: 0`.
- **Status:** SETTLED. If you see drops again, check the footer first — the
  instrumentation for this battle is still in place.

### 5. Logger idle watchdog crash

- **Symptom:** task watchdog tripped ~5 s after boot, before any recording,
  when no transceiver was attached (or RX floated).
- **Root cause:** the CAN controller was started at boot; a floating RX pin
  delivered continuous garbage, `twai_receive` never blocked, and the
  priority-6 `can_rx` task starved IDLE0.
- **Fix:** `can_init()` installs the driver but does not start it; TWAI is
  started/stopped by recording start/stop; the RX task sleeps 20 ms while
  idle and uses a 100 ms receive timeout while recording so it always yields.
- **Evidence:** commit `b860f06`.
- **Status:** SETTLED.

### 6. Asymmetric ESP-NOW pairing race

- **Symptom:** starting `pair start` on both boards in the same window
  sometimes paired only one of them: board A paired fine, board B's 15 s
  window expired without ever hearing a packet.
- **Root cause:** whichever board was already broadcasting heard the second
  board's *first* announcement almost instantly (bench-proximity radio
  latency) — often before its own next 200 ms broadcast — and immediately went
  silent on discovery, so the late joiner never got an announcement back.
- **Fix:** `pairing_start()` keeps broadcasting for a grace period
  (`PAIR_GRACE_SENDS`, ~1.6 s) after discovering a peer.
- **Evidence:** commit `d628425` (merged in PR #10, `2ed5687`).
- **Status:** SETTLED.

### 7. IDF 5.3 TWAI component dependency

- **Symptom:** transmitter CI failed at cmake configure: "Failed to resolve
  component 'esp_driver_twai'".
- **Root cause:** the standalone `esp_driver_twai` component only exists on
  ESP-IDF ≥ 5.5; CI (and the project) pins **release-v5.3**, where the legacy
  TWAI API (`driver/twai.h`) lives in the umbrella `driver` component.
- **Fix:** depend on `driver` in `PRIV_REQUIRES`.
- **Evidence:** commit `dd09fef`, merged as **PR #13** (`059e0e8`).
- **Status:** SETTLED as long as the project targets IDF 5.3. Revisit only on
  an IDF version bump (which itself needs a trade study — see
  `chmbl-change-control`).

### 8. `-Werror=format` printf failures (hit twice)

- **Symptom:** all four transmitter/brake_light CI matrix jobs (and earlier
  the logger) failed with format-mismatch errors that don't appear on host
  builds.
- **Root cause:** on the xtensa/riscv ESP toolchains, `uint32_t` is
  `long unsigned int`, so `printf("%u", some_uint32)` trips `-Werror=format`.
  `uint16_t`/`uint8_t` promote to `int` and are fine as `%u`.
- **Fix:** use `PRIu32` (from `<inttypes.h>`) for `uint32_t`, or cast.
- **Evidence:** commits `4571558` (net/link CLI) and `73c6553` (logger) — the
  same trap independently, twice.
- **Status:** SETTLED as a known trap. Any new printf of a `uint32_t` in
  firmware must use `PRIu32` on day one.

### 9. Trimming the firmware build (took two attempts)

- **Attempt 1** (`f4ef5a6`): declared `PRIV_REQUIRES esp_driver_gpio` so the
  `main` component stopped implicitly depending on everything. **Did not
  work**: ESP-IDF compiles every *discoverable* component regardless; the
  REQUIRES graph governs linking only. CI stayed at 951 build steps.
- **Attempt 2** (`6b189e6`): set `COMPONENTS=main` in each top-level
  CMakeLists — this trims the build to main's dependency closure plus the
  always-on common components. Worked.
- **Status:** SETTLED, with the lesson: in ESP-IDF, `REQUIRES` controls
  linking; `COMPONENTS` controls what compiles. Don't re-try attempt 1.

### 10. The trc_viz frozen-acceleration arc (→ the open firmware bug)

`tools/trc_viz.py` (the Dear PyGui `.trc` playback / DE-09 calibration bench)
went through a debugging arc whose final discovery is the repo's most
important **open** firmware bug:

| Commit | Finding |
|--------|---------|
| `2434c17` | Tool created; faithfully ported the firmware's accel derivation (16-deep ring, 200 ms window) and the DE-09 FSM with live tunable sliders |
| `bca0d14` | Gauge center numbers frozen at "0" while needles moved — Dear PyGui drawlist text updates need `configure_item(text=...)`, not `set_value` (a silent no-op) |
| `f44b0ed` | **The big one:** derived acceleration was effectively frozen. Wheel-speed frames arrive at ~100 Hz, so a 16-deep history ring spans only ~150 ms — less than the 200 ms accel window — and the "newest sample ≥ 200 ms old" search almost never succeeds. Deepened the tool's ring to 32 (~320 ms); FSM brake on-time went 24%→41%, transitions 12→37. **This mirrors a latent firmware bug: `can_decode.h`'s `CAN_DECODE_SPEED_HIST=16` is too small for its own window.** |
| `5d78036` | Tool made runnable via `uv run` (PEP 723 inline deps) |
| `6930e19` | Quantization steps / single-sample glitches in raw wheel speed injected spurious slope spikes tripping the decel trigger → causal dt-aware EMA low-pass **before** the slope estimator (`speed_smooth_ms`, default 80 ms — the knee: ≥200 ms starts attenuating genuine hard braking). Transitions 37→29, sub-0.5 s blips 4→1 |

- **Sizing law extracted:** history depth ≥ accel_window_ms × frame rate.
- **Status:** tool side SETTLED. The firmware side —
  `transmitter/software/main/can_decode.h` still has
  `CAN_DECODE_SPEED_HIST 16u` vs `CAN_DECODE_ACCEL_WINDOW_MS 200u` (verified
  as of 2026-07-07) — is **OPEN and reserved for `chmbl-de09-campaign`**. The
  bug and the fix rationale are documented in the `tools/trc_viz.py` header
  (lines ~49–56). Do not "drive-by fix" it outside the campaign.

### 11. Rejected decode hypothesis: `0x145` B5 as throttle

- **Hypothesis:** `0x145` B5 swept the full range during the bench throttle
  sweep, making it a throttle candidate alongside `0x140` B0.
- **Refutation:** during the real ride, `0x145` B5 **dithers wildly 0–100%**
  (a fast control/dither channel), while `0x140` B0 is smooth, sweeps 0–255 on
  the bench, and sits ~2.5% at idle (ride-by-wire idle air) — the rider-demand
  signal.
- **Evidence:** `docs/can-profiles.md` §5 decode-table warning box and the
  "Throttle" decode note.
- **Status:** SETTLED. Throttle is `0x140` B0, `raw/2.55` → %. Do not
  re-promote `0x145` B5. (Method lesson: a signal that sweeps on the bench is
  not thereby the rider signal — check its in-ride behavior. See
  `chmbl-research-methodology`.)

### 12. Rejected decode hypothesis: `0x121`/`0x113` as side stand

- **Hypothesis:** a lamp-cluster byte in `0x121`/`0x113` looked like the
  side-stand indicator.
- **Refutation:** it fires **the first time the bike exceeds ~10 km/h** —
  that's the warning-lamp self-check clearing, not the stand. The real signal
  is `0x481` B7 bit 0 (1 = stand up), which flips shortly after the ride
  starts and reads 0 (down) in every on-stand bench capture.
- **Evidence:** `docs/can-profiles.md` "Side stand" decode note.
- **Status:** SETTLED.

### 13. Stop-and-go flicker (FSM strobing in creep traffic)

- **Symptom:** the literal DE-09 rules strobed the light through the ride
  log's two creep zones — 162 FSM transitions and 65 sub-0.5 s light blips on
  the 40 mph capture.
- **Fixes (dry-run tuned on `logger/40mph_drive_cycle.trc`):**
  1. `MOVING_SPEED_MPH` (3.0) hysteresis on the `STOPPED` exit + a rolling
     qualifier on the launch guard → transitions **162→48**, blips **65→8**
     (`8192663`, doc'd in `docs/firmware.md` and DE-09 §8's struck-through,
     now-resolved open item).
  2. `DECEL_ON_DEBOUNCE_MS` (120 ms) continuous-hold requirement on the
     decel-on trigger → transitions **48→30**, blips **8→3**, on-time
     essentially unchanged; larger values clip real short brake taps, so
     120 ms is the knee. Explicitly a **debounce, not a low-pass** —
     preserving trigger latency (`docs/firmware.md`).
- **Status:** the *mechanism* (hysteresis + debounce) is SETTLED. The **final
  `MOVING_SPEED` value (3.0 mph) is still OPEN** — DE-09 §8 says "Final value
  (3.0 mph) still to confirm on more logs", and roadmap keeps "Stop-and-go
  flicker" open ("may add a `STOPPED`→`OFF` hold"). Confirming it needs more
  ride captures (see Open ledger).

### 14. Rejected design alternatives (fenced — do not re-propose)

Each was considered and rejected with a written rationale. Re-opening any of
them requires new facts plus the trade-study process in
`chmbl-change-control`.

| Alternative | Why rejected | Where written |
|-------------|-------------|---------------|
| **Brake-light-wire tap** | Patented approach; also modifies bike wiring. Absolute fence. | `docs/safety-regulatory.md` ("No tapping the brake-light wiring. Also patented") |
| **IMU / inertial brake detection** | Patented approach the project deliberately avoids, and less trustworthy than bus data. Deceleration must come from CAN wheel-speed. | `docs/safety-regulatory.md` ("No inertial brake detection … a patented approach we're avoiding"); `5c094a5` commit body |
| **Addressable RGB (WS2812) as the main light bar** | Flux math kills it: red die ≈ 1–2 lm each (indicator-grade) → dozens–hundreds of pixels + ~1–2 A to hit the ~50–80 cd CHMSL band; RGB-white-die red is a weak, off-spec stop color. Verdict: discrete 620–630 nm red array on a TI LM3410 boost constant-current driver; addressable RGB survives **only** as the DE-10 status indicator. | `docs/led-brightness-benchmark.md` (❌ Rejected-for-the-bar row + "resolved by the flux math"); `docs/roadmap.md` LED-array row; DE-04 trade study `69c0ecf`/`a0ab42d` (PR #4/#5) |
| **Raspberry Pi / SocketCAN ride rig ("Rig B" v1)** | Sketched in `2207663` as Pi + `candump`; superseded by the self-contained ESP-WROVER-KIT logger (`12f2720`, PR #6) — battery/boot/vibration-friendlier, writes `.trc` directly. | `docs/can-profiles.md` §Rig B ("replaces the Raspberry Pi / SocketCAN + candump rig originally sketched here") |
| **cantools `generate_c_source` (DE-08 Option B)** | Would invert the data-driven design: per-bike generated *code* + glue + dispatcher, pushing profile selection to compile time. Chosen instead: Option C hybrid — DBC as ground truth → `tools/gen_profile.py` emits a **data table**, one generic hand-written extractor, host golden test proving C ≡ cantools. Option B remains a documented escape hatch for a future bike with genuinely irregular decode (e.g. multiplexed messages). | `docs/design/de-08-can-decode.md` §3a; commit `d82fe03` (PR #12) |

Also permanently fenced by doctrine (not "alternatives", never viable):
transmitting/ACKing on the bike CAN bus, flashing/strobing patterns, drilling
helmets. See `docs/safety-regulatory.md` and `chmbl-change-control`.

---

## OPEN items (the only battles still live, as of 2026-07-07)

| Item | State | Owner / next step |
|------|-------|-------------------|
| **`CAN_DECODE_SPEED_HIST` firmware bug** | `transmitter/software/main/can_decode.h` has `SPEED_HIST 16u` vs a 200 ms window at ~100 Hz wheel-speed frames → derived acceleration freezes in firmware (proven in the tool at `f44b0ed`; tool uses 32). Sizing law: hist ≥ window_ms × frame_rate. | **Owned by `chmbl-de09-campaign`** — fix lands with the DE-09 implementation, not as a drive-by. |
| **Final `MOVING_SPEED_MPH` value** | Mechanism settled at 3.0 mph hysteresis, but DE-09 §8 marks the value unconfirmed beyond the single 40 mph log; roadmap keeps a possible `STOPPED`→`OFF` hold open. | Needs additional ride captures with stop-and-go traffic; validate via `tools/trc_viz.py` before touching tunables. |
| **Missing bench captures `logger/throttle.trc` and `logger/wheel.trc`** | `docs/can-profiles.md` cites both as cross-checks for the decode table, but only `logger/40mph_drive_cycle.trc` is committed (verified: `ls logger/`). The decode conclusions stand (they're also corroborated in-ride), but the bench evidence is not reproducible from the repo. | Re-capture on the reference bike, or annotate can-profiles.md. Do NOT delete the citations — they describe real experiments. |

Related honest gaps (not investigations, just doc-vs-repo drift): the SMC
`.sm` model and `tools/smc/Smc.jar` described in `docs/firmware.md` §4 do not
exist yet (DE-09 is status 🔲); roadmap's "CAN access mode" row still says
"Unknown" despite can-profiles.md resolving it; can-profiles.md tells
contributors to commit captures under `transmitter/software/captures/` but the
committed capture lives in `logger/`.

---

## Provenance and maintenance

All hashes and numbers verified against this repo on **2026-07-07** (branch
`claude/skill-library-continuity-mib7ua`). Re-verify before trusting:

| Fact class | Re-verification command |
|------------|------------------------|
| Any commit's full story | `git show -s --format='%s%n%b' <hash>` |
| PR number ↔ commit mapping | `git log --all --merges --oneline` |
| `SPEED_HIST` bug still open | `grep -n "SPEED_HIST\|ACCEL_WINDOW" transmitter/software/main/can_decode.h` (open while 16u vs 200u) |
| Tool-side workaround + bug note | `sed -n '45,60p' tools/trc_viz.py` |
| Missing bench captures | `ls logger/*.trc` (open while only `40mph_drive_cycle.trc`) |
| DE-09 open/resolved items | `grep -n -i "resolved\|open" docs/design/de-09-brake-decel-logic.md` |
| Decode-hypothesis rejections | `grep -n "0x145\|0x113" docs/can-profiles.md` |
| LCD dead-end record | `grep -n -i "dead end\|GPIO21" logger/software/README.md` |
| Roadmap resolved/open rows | open-questions table in `docs/roadmap.md` |
| Fenced alternatives | `grep -n -i "patent\|inertial\|tapping" docs/safety-regulatory.md`; `docs/design/de-08-can-decode.md` §3a; `docs/led-brightness-benchmark.md` |
