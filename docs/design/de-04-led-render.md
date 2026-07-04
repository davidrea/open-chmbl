# DE-04 — LED render & bar driver

**Status:** 🟡 in design · **Device(s):** brake_light · **Depends on:** DE-00

Drives the main red brake bar: turns a braking **state** (`OFF`/`DECEL`/`BRAKE`) and a
**commanded brightness** (from [DE-02](de-02-auto-brightness.md)) into actual light, via
a constant-current LED driver. This element owns both the **render logic** (state +
brightness → a steady pattern and a current setpoint) and the **driver hardware**
(the boost CC stage that lights the string). It is the "actual LED driving" that
[DE-02 §1](de-02-auto-brightness.md) defers to.

## 1. Scope & isolation boundary
- **In:** the state→pattern map, the anti-strobe floor, the brightness→current/duty
  mapping, and the driver stage that converts a 1S LiPo into a regulated LED current.
- **Out (faked at edges):** the braking *state* is injected with `in set state` (the
  real source is the link, DE-01/DE-03); the *commanded brightness* is injected with
  `ambient set` / `render` (the real source is DE-02); the **status-indicator** LED is a
  separate path ([DE-10](de-10-status-indicator.md)), not this element.
- **Isolation test:** brake_light board only; force each state × sweep brightness, watch
  the bar render correctly (steady, anti-strobe, current within limits) — no radio link
  or ambient sensor required.

## 2. FFL traceability
BL-RND-1…3 (state→pattern, steady-only/anti-strobe, dim running light) and
BL-LED-1…2 (drive the bar at commanded brightness via constant current; respect
thermal/current limits). Viewed through BL-CLI-4.

## 3. Component selection

### 3.1 Drive topology — series string + boost CC

The supply is a **1S LiPo: 3.0 V (cutoff) → 4.2 V (full)**, ~3.7 V nominal. A
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

## 4. I/O assignments & configuration
- **PWM dimming:** one ESP32-C3 LEDC GPIO → driver **DIM/EN** pin. Keep the PWM
  **above flicker fusion (~200 Hz–1 kHz)** — this is *brightness* dimming, distinct from
  the illegal *flashing* (pattern-level, handled by BL-RND-2's anti-strobe floor).
- **Analog current trim (optional, for deep night-dim):** filtered PWM or DAC into the
  FB/ISET node to lower absolute LED current below what PWM alone gives cleanly.
- **Enable/shutdown:** driver EN for true off (OFF state / power-save).
- **Thermal (BL-LED-2):** rely on the driver's thermal shutdown as the floor; optionally
  an NTC near the bar for a firmware-side derate. Set R_sense for the worst-case (3.0 V,
  full brake) current and let OVP cover an open-LED string.

## 5. Firmware module/task decomposition
- **Render task:** `(state, commanded_brightness) → (pattern, current/duty setpoint)`.
  Steady patterns only; enforce the anti-strobe minimum-dwell floor (BL-RND-2); optional
  dim running light in `OFF` (BL-RND-3). Drives LEDC (and the analog trim, if used).
- **Pure / host-testable:** the state×brightness→setpoint map and the anti-strobe
  rate-limiter — no hardware needed.
- **Platform:** LEDC PWM config, optional DAC/filtered-PWM, EN control.

## 6. CLI hooks
- `in set state OFF|DECEL|BRAKE` — fake the braking state.
- `ambient set <lux>` / `render` — drive/observe commanded brightness (shared with DE-02).
- `render show` — view state + commanded brightness + pattern + resulting LED-current
  setpoint (BL-CLI-4).
- `bright cap` — user brightness cap interaction.

## 7. Isolation acceptance
- Each forced state renders its correct **steady** pattern; `BRAKE` brightest, `OFF`
  dark or dim-running per config.
- Sweeping commanded brightness moves the LED current setpoint monotonically; the
  anti-strobe floor prevents any flashing on fast transitions.
- Current setpoint stays within the configured limit across 3.0–4.2 V supply; OVP
  behaves on an open-LED string; driver thermal limit holds.

## 8. Open items
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
