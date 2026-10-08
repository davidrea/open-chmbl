# DE-04 — LED render & bar driver

**Status:** 🟢 render stage implemented as a **binary GPIO output** (brightness deferred)
· **Device(s):** brake_light · **Depends on:** DE-00

Drives the main red brake bar: turns a braking **state** (`OFF`/`DECEL`/`BRAKE`) into
actual light, via a constant-current LED driver. This element owns both the **render
logic** (state → a steady output) and the **driver hardware** (the boost CC stage that
lights the string).

> ## Decision — brightness is deferred; `EN` is a static enable
>
> **The render stage is a plain binary GPIO output: high when the brake light should be
> on, low when it should be off.** Brightness levels are eliminated for now. There is no
> PWM/LEDC, no duty cycle, no commanded brightness, no user brightness cap, and the
> ambient-light sensor is not read. The owner's call was to defer the whole
> brightness/auto-dimming axis and get a working on/off light first.
>
> Concretely:
>
> - **The driver's `EN`/`CTRL` pin is used as a *static* enable, not a dimming input.**
>   In the usual design for these parts that pin *is* the brightness control (PWM
>   dimming) — see §4 below, which this decision supersedes for now. High = the string
>   lit at its fixed setpoint; low = dark.
> - **The current setpoint is fixed in hardware by the sense resistor**, not by firmware.
>   On the first-pass PCB that is **560 mΩ → ≈ 357 mA per string**
>   ([`brake_light/hardware/README.md`](../../brake_light/hardware/README.md)). The
>   intensity it *should* be is still governed by the
>   [LED brightness benchmark](../led-brightness-benchmark.md) — that study is **not**
>   obsolete; it is now the *only* thing that sets the operating point, so the sense
>   resistors are the single adjustment. §8 carries the open item to reconcile the two.
> - **Anti-strobe is retained**, and is now the render stage's main job besides the
>   state map (§5).
> - **The topology and component analysis in §3 stands unchanged** — the series-string +
>   boost-CC conclusion, the emitter down-select and the driver trade study are all
>   still the design. Only the *render behaviour* changed.
>
> What this defers: [DE-02](de-02-auto-brightness.md) (`BL-BRT-*`) in full, `BL-LED-1`'s
> "at commanded brightness" clause (the bar is driven at *one* brightness), `BL-RND-3`'s
> dim running light (there is no dim tier, so `OFF` is dark), and the `ambient *` /
> `bright cap` CLI commands. The `ISL29035` ambient-light sensor stays **populated but
> unread**, so picking DE-02 back up needs no hardware change.
>
> Safety note: deferring *dimming* is not the same as deferring the
> [no-blinding requirement](../safety-regulatory.md#3-helmet--rider-safety). A bar with a
> fixed, undimmed setpoint can dazzle following traffic at night — which is exactly why
> the setpoint must be reconciled against the benchmark (§8) and why DE-02 is deferred,
> not dropped.

## 1. Scope & isolation boundary
- **In:** the state→output map, the anti-strobe floor, and the driver stage that converts
  a 1S Li-ion cell into a regulated LED current.
- **Out (faked at edges):** the braking *state* is injected with `light on|off` (and in
  future `in set state`); the real source is the link (DE-01/DE-03). The
  **status-indicator** LEDs are a separate path ([DE-10](de-10-status-indicator.md)), not
  this element — and that is now where link-loss indication lives (DE-03).
  **Brightness is out of scope entirely** (deferred, see the decision above).
- **Isolation test:** brake_light board only; force each state, watch the bar render
  correctly (steady, anti-strobe, current within limits) — no radio link or ambient
  sensor required. The pure half is also proved on the host with no board at all
  (§5, `brake_light/software/test_host/`).

## 2. FFL traceability
BL-RND-1…3 (state→pattern, steady-only/anti-strobe, dim running light) and
BL-LED-1…2 (drive the bar at commanded brightness via constant current; respect
thermal/current limits). Viewed through BL-CLI-4 (`render show`).

Current coverage, given the brightness deferral above:

| FFL | Status |
|-----|--------|
| BL-RND-1 state→pattern | ✅ implemented, as a binary map (`render_core_map`) |
| BL-RND-2 steady-only / anti-strobe | ✅ implemented (asymmetric dwell floor, host-tested) |
| BL-RND-3 dim running light in `OFF` | ⏸ **deferred with brightness** — no dim tier exists, `OFF` is dark |
| BL-LED-1 drive the bar at commanded brightness | ◐ the bar is driven, at **one** fixed hardware setpoint; "commanded brightness" is deferred to DE-02 |
| BL-LED-2 thermal / current limits | ◐ relies on the driver's own CC loop, OVP and thermal shutdown; no firmware derate (§8) |
| BL-CLI-4 view render output | ✅ `render show` (plus `light` as the bench override) |

## 3. Component selection

### 3.1 Drive topology — series string + boost CC

The supply is a **1S Li-ion cell (18650 baseline): 3.0 V (cutoff) → 4.2 V (full)**,
~3.7 V nominal — the same electrical envelope as the earlier 1S LiPo, so the
topology analysis below is unchanged. A
high-brightness red LED has a forward voltage of **~2.0–2.5 V**, which sits *inside*
the battery range. That makes the per-LED count the real design lever:

| Series count | String Vf (≈) | vs. 4.2 V battery | Converter needed |
|---|---|---|---|
| 1× red | 2.0–2.5 V | always **below** | buck-boost (a plain boost can't step *down*) |
| 2× red | 4.0–5.0 V | marginal at top-of-charge | crossover zone → buck-boost / high-Vf parts |
| **≥3× red** | **6.0–7.5 V+** | always **above** | **plain boost — clean step-up headroom** |

So a **series string of ≥3 red LEDs**, driven by one **boost constant-current
regulator**, is the right topology: the string voltage is always above the battery (a
pure boost is valid across the entire discharge), one current loop sets every LED's
brightness identically, and a taller string at lower current keeps the boost's input
current — and conduction losses — down. A single string per CC loop guarantees matched
brightness with no ballast resistors.

This is also why a buck-boost part is *not* needed and would only add cost: we
deliberately choose the series count so we never enter the Vbatt-crosses-Vstring zone.

**Upper bound on series count.** The string voltage is bounded *above* by the driver's
output ceiling. With the down-selected mid-power emitters (§3.2, Vf ≈ 2.0–2.3 V),
**~8 in series ≈ 18 V** fits comfortably under our driver's 24 V ceiling with OVP
headroom; **10–12 in series (~21–26 V) does not**. So the emitter count the benchmark
calls for (~8–12 for an even bar) sets the layout: **≤ ~8 emitters → one series string**;
**more → two parallel series strings** (each ≤ ~8, with a small per-string ballast
resistor to share current, since mid-power reds are well Vf-matched), *or* step up to a
higher-V<sub>out</sub> driver (§3.3). This is the one place the
[brightness benchmark](../led-brightness-benchmark.md) and the driver choice must be
solved together — emitter count × Vf must clear the driver's V<sub>out</sub>.

### 3.2 LED emitter

Emitter class, wavelength, brightness, and flux are **down-selected in the
[LED brightness benchmark](../led-brightness-benchmark.md)** — that study benchmarks the
bar against automotive CHMSLs and does the candela→flux math. DE-04 takes its outputs as
the design inputs here:

- **Architecture:** **discrete mid-power red 2835/3030 array (~8–12 emitters)** on a
  constant-current driver — chosen over a few high-power emitters (Cree XP‑E2 / OSRAM
  Oslon) because a brake *bar* wants **even illumination, per-emitter redundancy, low
  per-emitter current, and cool operation**, and over WS2812-class addressable (rejected
  for the bar — ~1–2 lm red/pixel would need dozens–hundreds of pixels and ~1–2 A to hit
  the target; addressable RGB stays the [status indicator](de-10-status-indicator.md)).
- **Wavelength:** **dominant 620–630 nm** "stop red"; avoid red-orange (~615 nm) and deep
  red (~660 nm).
- **Brightness target (from the benchmark):** daylight BRAKE **≈ 50–80 cd** on-axis
  (CHMSL ECE S3/S4 band, 25–110 cd), night floor **≈ 5–15 cd** → these are the DE-02
  curve endpoints. Installed capability **≈ 60–100 lm of red**, run well below max for
  headroom, cool operation, and dimming range.
- **Count/current (baseline):** ~8–12 emitters, each at **a fraction of its rated
  current** (~60–100 mA range) so the array totals the 60–100 lm target while idling
  cool. Final count follows the optic/diffuser and the series/parallel layout in §3.1.

### 3.3 Driver IC trade study

Requirements ranked for **this** application (helmet-worn, 1S, small/low-profile,
open-hardware with an LCSC sourcing soft-preference, co-located with a 2.4 GHz ESP-NOW
radio, and an auto-dim that is a *safety* feature):

1. **Vin reaches down to ~2.8 V** so we use the *whole* 1S discharge (a gate — a
   driver that drops out at 3 V throws away the bottom of the cell).
2. Switch / power headroom for the ~0.5–0.8 A peak *input* current (§3.4).
3. **V<sub>out</sub> ceiling ≥ the tallest string** the emitter count needs (§3.1) —
   8 mid-power reds ≈ 18 V; 10–12 ≈ 21–26 V.
4. Dimming quality (deep, smooth, high-frequency) for the DE-02 auto-dim.
5. Integration / board area / hand-assembly (helmet = small; few externals).
6. Sourcing & cost (LCSC availability, price).
7. Robustness — OVP / open-LED protection, thermal, fault flag.
8. Low EMI near the 2.4 GHz radio (spread-spectrum a plus).

| Part | Vendor | Topology | Vin | Switch | Vout max | Dimming | Package | ~Price | LCSC | Verdict |
|------|--------|----------|-----|--------|----------|---------|---------|--------|:----:|---------|
| **LM3410(X)** | TI | non-sync boost CC | **2.7–5.5 V** | **2.8 A** int. | 24 V (~8 reds) | PWM (EN) + analog via FB | SOT‑23‑5 / WSON‑6 / MSOP‑8 PowerPAD | ~$1.5 | ✅ | ✅ **primary** |
| DIO5661 | DIOO | non-sync boost CC | ~2.5 V↓ *(confirm)* | ~1.2 A *(confirm)* | 37 V | PWM→constant-current (duty→analog) | SOT‑23‑6 | ~$0.3 | ✅ `C324576` | ◐ budget LCSC alt |
| LT3922‑1 | ADI | **synchronous** boost | 2.8–36 V | 2 A int. (40 V) | 34 V | **25 000:1** PWM + analog, **spread-spectrum** | QFN‑28 4×5 | ~$5–7 | ⚠ limited | ▲ premium upgrade |
| LT3518 | ADI | boost/buck/buck‑boost | 3.0–30 V (40 V tr.) | 2.3 A int. (45 V) | high | 3000:1 PWM, HS sense | QFN‑16 / TSSOP‑16 | ~$4–5 | ⚠ limited | flexible, but Vin‑min 3 V |
| TPS61500 | TI | non-sync boost CC | **2.9 V**↑ | 3 A int. (40 V) | 40 V (~10 reds) | PWM (EN) + analog (DIMC) | HTSSOP‑14 | ~$2 | ✅ `C71160` | ✗ Vin‑min wastes 1S bottom |
| MAX16833 | ADI | boost/buck‑boost/SEPIC + ext FET | **5–65 V** | ext NFET (3 A gate drv) | 65 V | PWM ext‑PFET + analog, HS sense | TSSOP‑16 | ~$2–3 | some | ✗ **Vin‑min 5 V — can't run on 1S** |

**Rejected, with reasons:**
- **MAX16833** — Vin minimum **5 V**. It physically cannot run from a 1S cell
  (3.0–4.2 V); it's a 12–48 V automotive-rail part. Out.
- **TPS61500 / LT3518** — both excellent boost CC parts, but Vin-min **2.9 V / 3.0 V**
  brushes the 1S cutoff, so they stop regulating right where a protected cell still has
  usable charge. They shine on **≥2S**, not a single Li-ion you want to drain to ~3.0 V.

**Conclusion — design in the TI LM3410 (LM3410X / `‑Q1`):** it is the only brand-name,
well-documented part that *passes the Vin gate* (2.7 V min → uses the full 1S discharge),
with a **2.8 A** integrated switch (large margin over our ~0.7 A peak), current-mode CC
set by one sense resistor, PWM + analog dimming, cycle-by-cycle limit, OVP and thermal
shutdown, in a small hand-solderable package (**use WSON‑6 or MSOP‑8 PowerPAD** for the
thermal pad, not bare SOT‑23), at ~$1.5 and stocked at LCSC. TI longevity + reference
designs de-risk the open-hardware build.

Known trade-offs we accept (and how we handle them):
- **24 V output ceiling** caps a single series string at **~8 mid-power reds** (≈ 18 V +
  OVP headroom). That suits the benchmark's lower emitter counts directly; for a **10–12**
  emitter bar, run **two parallel series strings** (§3.1) — total current is still small
  and each string sits ~13–17 V, well inside the ceiling. Only if a design insists on one
  *tall* 10–12 string do we step to a higher-V<sub>out</sub> part (DIO5661 37 V / LT3922‑1
  34 V). This is the explicit hand-off to the [brightness benchmark](../led-brightness-benchmark.md).
- **Non-synchronous** → needs an external Schottky and is a few points less efficient
  than a synchronous part. Acceptable: the §2 runtime budget is dominated by the
  idle/running average, not the brief brake peaks.
- **Moderate PWM dimming ratio** (vs. the LT3922's 25 000:1). For the deep night-dim we
  lean on **analog trim** (filtered PWM / DAC into the FB/ISET node to lower the
  absolute current) in addition to PWM on the EN/DIM pin — see §4.
- **SOT‑23 is thermally limited** → specify the WSON/MSOP‑PowerPAD variant.

**Budget LCSC-native alternate — DIOO DIO5661:** even cheaper and LCSC-native
(`C324576`), 37 V out (room for taller strings), and a *PWM-to-constant-current* input
that converts a digital duty straight into a smooth analog current level (nice for
DE-02). Pick it if LCSC BOM cleanliness/cost dominate and the bar stays low-power —
**but confirm Vin-min and switch-current limit on the datasheet first**, and weigh the
lesser-known-vendor documentation/longevity risk.

**Premium / automotive upgrade — ADI LT3922‑1:** if EMI next to the 2.4 GHz radio,
maximum efficiency (it's **synchronous** → best runtime), the deepest smoothest dimming
(25 000:1), or AEC-Q100 qualification become priorities, this is the drop-in step up. The
costs are price (~$5–7) and a QFN‑28 that is harder to hand-assemble. Gate this decision
on the EMI/efficiency measurements in Phase 4.

### 3.4 Worked operating point (baseline)

```
Bar: 8× mid-power red 2835 (620–630 nm), ONE series string
Full daylight brake:  I_string ≈ 80 mA,  V_string ≈ 8 × 2.1 V ≈ 16.8 V
  → P_out ≈ 1.3 W   (well below the array's ~240 lm rated capacity → runs cool)
  → I_in @ 3.3 V, ~85% eff ≈ 0.48 A   (≈ 0.53 A at the 3.0 V cutoff)
  → peak switch current incl. ripple ≈ 0.6–0.8 A  ⟹  far inside LM3410's 2.8 A
Sense resistor: R_sense ≈ V_FB / I_string ≈ 0.19 V / 0.08 A ≈ 2.4 Ω  (~15 mW)
OVP: set above 16.8 V string + open-LED margin, below the 24 V abs-max output

12-emitter variant (taller bar): 2 parallel strings of 6 (≈ 12.6 V each),
  shared CC, small per-string ballast for matching → total ≈ 160 mA, both
  strings well under the 24 V ceiling.  R_sense ≈ 0.19 / 0.16 ≈ 1.2 Ω.

Night: auto-dimmed to the ~5–15 cd floor → a few lm, tens of mA; idle/running
       dominates the ~120 mA system average from hardware.md §2.
```

Final LED count, current, and optic are tunable — they set R_sense, the string layout,
and the OVP point, not the part choice (LM3410 covers 3–24 V out / up to 2.8 A across the
plausible range, given series count ≤ ~8/string).

### 3.5 As built — first-pass PCB

The committed [brake_light schematic/PCB](../../brake_light/hardware/README.md) is a
first pass and **departs from §3.3/§3.4** in two ways worth recording rather than
discovering later:

| | §3.3/§3.4 design | First-pass PCB |
|---|---|---|
| Driver | TI **LM3410** ×1 (24 V ceiling) | **`AP3019AKTR` ×2** (U3, U6; sheet note "MAX 30V") |
| Strings | 1 × 8 emitters, ≈ 16.8 V | **2 × 10 emitters** (D3…D12, D15…D24), ≈ 21 V each |
| R_sense | ≈ 2.4 Ω → ≈ 80 mA/string | **560 mΩ → ≈ 357 mA/string** (≈ 714 mA total) |
| Inductor | — | 22 µH (L1, L2) |

The two-driver / two-string split is exactly the §3.1 "more than ~8 emitters → two
parallel series strings" branch, and the AP3019A's higher output ceiling is what makes
10-per-string viable — so the *shape* of the board follows this element. The **current
setpoint does not**: it is ~4.5× the §3.4 worked point. That is now the single most
important number on the board, because with dimming deferred there is no firmware knob to
pull it back. See §8.

Firmware consequence: **two `EN` nets, two strings, one logical output.** The render stage
drives both driver enables together, so the bar can never light half-on. Each pin is its
own Kconfig symbol (`CHMBL_BAR_EN_A_GPIO`, `CHMBL_BAR_EN_B_GPIO`), and a board with only
one string sets the other to `-1`.

> ⚠️ Both `CTRL` pads are **unconnected in the committed netlist**, so the GPIO numbers
> in firmware are **provisional defaults pending a schematic review**, not verified
> assignments. The
> [hardware README pin map](../../brake_light/hardware/README.md#pin-map) states exactly
> which pins were verified from the netlist and which were not.

## 4. I/O assignments & configuration

> **Superseded for now by the decision at the top of this document.** §4.1 is what is
> implemented; §4.2 is the PWM-dimming design kept on record for when DE-02 is picked
> back up.

### 4.1 As implemented — binary enable

- **Bar output:** two plain **output GPIOs**, one per driver `CTRL`/`EN` pin, driven
  together. **High = bar lit, low = bar dark.** Active high. No LEDC, no PWM, no duty
  cycle.
- **Current setpoint:** fixed by each driver's sense resistor (§3.5). Firmware does not
  and cannot change it.
- **Pin numbers:** Kconfig (`CHMBL_BAR_EN_A_GPIO`, `CHMBL_BAR_EN_B_GPIO`), `-1` = string
  not fitted, so the classic-ESP32 bench board runs with a single stand-in LED.
- **Thermal (BL-LED-2):** the driver's own CC loop, OVP and thermal shutdown are the
  whole story for now — there is no firmware derate and no NTC on the bar (§8).

### 4.2 Deferred — PWM/analog dimming (for DE-02)

- **PWM dimming:** one ESP32-C3 LEDC GPIO → driver **DIM/EN** pin. Keep the PWM
  **above flicker fusion (~200 Hz–1 kHz)** — this is *brightness* dimming, distinct from
  the illegal *flashing* (pattern-level, handled by BL-RND-2's anti-strobe floor).
- **Analog current trim (optional, for deep night-dim):** filtered PWM or DAC into the
  FB/ISET node to lower absolute LED current below what PWM alone gives cleanly.
- Both land on the *same* pins as §4.1 — the binary enable is a strict subset of the PWM
  design (100 % / 0 % duty), so nothing has to be rewired to pick this up.

## 5. Firmware module/task decomposition

Implemented in `brake_light/software/main/`:

- **Pure / host-testable core** (`render_core.[ch]`): the **state→binary-output map**
  (`ST_BRAKE` → on, `ST_OFF` → off, `ST_DECEL` → on, anything off-protocol → off) and the
  **anti-strobe dwell floor**. No ESP-IDF, no GPIO, caller-supplied clock. Proved in
  `brake_light/software/test_host/render_core_test.c`, which CI runs.
- **Platform half** (`render.[ch]`): configures the two enable GPIOs, runs the render task
  at `CHMBL_RENDER_TICK_MS` (16 ms ≈ 60 Hz), and is the **single writer** of those pins —
  both the link watchdog and the `light` bench override publish *into* it rather than
  poking GPIOs.

**`ST_DECEL` is mapped to ON, deliberately.** It is reserved in the
[protocol](../protocol.md#2-message-format) and not emitted by the TX FSM, but leaving it
undefined in the render map would be worse than choosing: with brightness deferred there
is no middle tier to render it as, and the fail-safe reading of "the bike is slowing" is a
lit brake light, not a dark one.

**Anti-strobe (BL-RND-2), asymmetric on purpose:**

| Floor | Default | Why |
|-------|---------|-----|
| `CHMBL_RENDER_MIN_ON_MS` | 600 ms | Once lit the bar stays lit at least this long. This is the floor that actually stops a strobe, and it is safe to be generous — holding a brake light on slightly too long is honest. |
| `CHMBL_RENDER_MIN_OFF_MS` | 150 ms | Once dark it stays dark at least this long before relighting. Kept small: this is the only floor that can delay a *brake-on* edge against the ≤ 100 ms end-to-end budget, and it only bites if the bar went dark moments ago. |

A change arriving inside a floor is **deferred, not dropped**; the render task commits it
when the floor elapses, unless the state has gone back by then. Worst case — an upstream
state flapping every single tick — the bar is bounded to one on→off→on cycle per 750 ms.
The primary anti-oscillation guards remain upstream (the TX FSM's low-speed hysteresis,
120 ms decel debounce and 150 ms dwell,
[`firmware.md`](../firmware.md#braking-state-machine)) and in `link.c`, which holds the
bar **steady** rather than blinking it. This floor is defence in depth inside the render
stage.

## 6. CLI hooks
- `light on|off|toggle|auto` — the bench override of the bar's binary output; `auto`
  releases it back to the link-driven state. It goes through the render stage (so the
  anti-strobe floor still applies) rather than poking the GPIOs.
- `render show` — view the effective state, the binary output now on the enable pins, the
  anti-strobe floors and any hold in progress, and the override status (BL-CLI-4).
- `in set state OFF|DECEL|BRAKE` — fake the braking state **(not implemented yet;** today
  `light` covers the bench case, and `net stop` on the transmitter covers the link case).
- `ambient set <lux>` / `bright cap` — **deferred** with DE-02; there is no brightness to
  observe.

## 7. Isolation acceptance
- Each forced state renders its correct **steady** output: `BRAKE` (and the reserved
  `DECEL`) lit, `OFF` dark. ✅ host-tested; on-hardware check pending a built board.
- No input sequence can flash the bar: a state flapping at the render rate is bounded by
  the dwell floors, and a transient inside a floor never reaches the pin.
  ✅ host-tested.
- Link-lost / waiting leaves the bar **steady off** (never blinking, never a latched
  `BRAKE`), with the indication on the status LEDs instead
  ([DE-03](de-03-link-loss-failsafe.md), [DE-10](de-10-status-indicator.md)).
- **Still to demonstrate on hardware:** string current within limits across a 3.0–4.2 V
  supply, OVP behaviour on an open-LED string, driver thermal limit, and the
  measured on-axis intensity against the
  [benchmark](../led-brightness-benchmark.md) band. None of this is possible until a board
  is built — and the measured intensity is the gate on §8's first open item.

## 8. Open items

**From the brightness deferral (new):**
- **Reconcile the fixed setpoint with the benchmark.** The first-pass board sits at
  ≈ 357 mA × 2 strings of 10 (§3.5) against §3.4's ≈ 80 mA × 1 string of 8. With no
  firmware dimming, the sense resistors are the *only* adjustment, so this has to be
  settled against the [benchmark](../led-brightness-benchmark.md)'s ≈ 50–80 cd daylight
  band before a board is populated. The benchmark is **not** superseded by this
  deferral — it is now the sole thing setting the operating point.
- **Night glare.** A single undimmed setpoint cannot satisfy both the daylight target and
  the ≈ 5–15 cd night floor. Until DE-02 lands, pick the setpoint knowing which end is
  being compromised and say so in the build docs
  ([`safety-regulatory.md §3`](../safety-regulatory.md#3-helmet--rider-safety)).
- **Confirm `AP3019A` against the §3.3 criteria** (V<sub>in</sub>-min vs. the 1S cutoff,
  switch-current limit, EMI next to the 2.4 GHz radio) and record the outcome, since the
  board substituted it for the LM3410 down-select.
- **Validate the dwell-floor defaults on hardware** (600/150 ms). The host test proves the
  bound; whether 600 ms of extra on-time feels right on a real ride is a Phase-3 question.
- **Picking brightness back up** means DE-02 plus swapping §4.1 for §4.2 — same pins, so
  no board change. The `ISL29035` is already fitted.

**Pre-existing:**
- Final **LED count / current / optic** (from the [benchmark](../led-brightness-benchmark.md)'s
  remaining open items) → sets R_sense, the **series/parallel string layout** (§3.1), and
  the OVP threshold.
- **Confirm DIO5661** Vin-min and switch-current limit against its datasheet before
  treating it as a real alternate.
- **Synchronous upgrade (LT3922‑1)** decision — gate on Phase-4 EMI (near the ESP-NOW
  radio) and efficiency/runtime measurements. Also the fallback if a tall single 10–12
  string is preferred over parallel strings.
- Addressable (WS2812) vs. discrete CC string is **resolved → discrete** by the
  [benchmark](../led-brightness-benchmark.md)'s flux math (addressable is too dim per
  pixel for a CHMSL-class bar); addressable RGB stays the status indicator only (DE-10).
- Whether to add a per-bar **NTC** for firmware thermal derate, or rely solely on the
  driver's thermal shutdown.
