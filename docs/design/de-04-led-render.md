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

So a **single series string of ≥3 red LEDs**, driven by one **boost constant-current
regulator**, is the right topology: the string voltage is always above the battery (a
pure boost is valid across the entire discharge), one current loop sets every LED's
brightness identically, and a taller string at lower current keeps the boost's input
current — and conduction losses — down. We use **one series string for the whole bar**
(not parallel strings) so a single CC loop guarantees matched brightness with no ballast
resistors or current mirrors.

This is also why a buck-boost part is *not* needed and would only add cost: we
deliberately choose the series count so we never enter the Vbatt-crosses-Vstring zone.

### 3.2 LED emitter

- **Class:** high-power red, **not** indicator-grade 5 mm (those top out at a few cd —
  invisible in daylight). Candidates: **Cree XLamp XP-E2 Red**, **ams-OSRAM Oslon
  SSL/Signal red**, **Lumileds Luxeon C/Rebel red**.
- **Wavelength:** bin for **dominant wavelength 620–630 nm** (clearly *red*, for the
  legal rear-lamp color). Avoid ~615 nm red-orange.
- **Count/current (baseline):** ~6 emitters in one series string, run at **~150 mA at
  full daylight brake** (power reds are bright at 150 mA and run cool, keeping the boost
  input current modest — see §3.4).
- **Brightness target:** brake-light conspicuity is **luminous intensity (candela)**
  toward following traffic = emitter flux × optic, not raw lumens. Targets:
  - **Daylight brake (near cap):** order of **low-hundreds of cd** on-axis with a
    diffuser/optic (≈ **30–100 lm** of red emitted), to read in direct sun.
  - **Night (auto-dimmed, DE-02):** down to **tens → single-digit cd** so it never
    dazzles a following rider.
  - **Auto-dim span ≥ 50:1** (ideally 100:1). For reference, an automotive CHMSL
    minimum is only ~25–40 cd on-axis.

### 3.3 Driver IC trade study

Requirements ranked for **this** application (helmet-worn, 1S, small/low-profile,
open-hardware with an LCSC sourcing soft-preference, co-located with a 2.4 GHz ESP-NOW
radio, and an auto-dim that is a *safety* feature):

1. **Vin reaches down to ~2.8 V** so we use the *whole* 1S discharge (a gate — a
   driver that drops out at 3 V throws away the bottom of the cell).
2. Switch / power headroom for the ~1–1.5 A peak *input* current (§3.4).
3. Dimming quality (deep, smooth, high-frequency) for the DE-02 auto-dim.
4. Integration / board area / hand-assembly (helmet = small; few externals).
5. Sourcing & cost (LCSC availability, price).
6. Robustness — OVP / open-LED protection, thermal, fault flag.
7. Low EMI near the 2.4 GHz radio (spread-spectrum a plus).

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
with a **2.8 A** integrated switch (large margin over our ~1.3 A peak), current-mode CC
set by one sense resistor, PWM + analog dimming, cycle-by-cycle limit, OVP and thermal
shutdown, in a small hand-solderable package (**use WSON‑6 or MSOP‑8 PowerPAD** for the
thermal pad, not bare SOT‑23, at full-brake power), at ~$1.5 and stocked at LCSC. TI
longevity + reference designs de-risk the open-hardware build.

Known trade-offs we accept (and how we handle them):
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
Bar: 6× high-power red (620–630 nm), single series string
Full daylight brake:  I_LED ≈ 150 mA,  V_string ≈ 6 × 2.2 V ≈ 13.2 V
  → P_out ≈ 2.0 W
  → I_in @ 3.3 V, ~85% eff ≈ 0.71 A   (≈ 0.81 A at the 3.0 V cutoff)
  → peak switch current incl. ripple ≈ 1.1–1.3 A  ⟹  well inside LM3410's 2.8 A
Sense resistor: R_sense ≈ V_FB / I_LED ≈ 0.19 V / 0.15 A ≈ 1.27 Ω  (~30 mW)
OVP: set above 13.2 V string + open-LED margin, below the 24 V abs-max output
Night: auto-dimmed to ~2–5% → tens of mA; idle/running dominates the
       ~120 mA system average from hardware.md §2.
```

Final LED count, current, and optic are tunable — they set R_sense and the OVP point,
not the part choice (LM3410 covers 3–24 V out / up to 2.8 A across the plausible range).

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
- Final **LED count / current / optic** → sets R_sense and the OVP threshold.
- **Confirm DIO5661** Vin-min and switch-current limit against its datasheet before
  treating it as a real alternate.
- **Synchronous upgrade (LT3922‑1)** decision — gate on Phase-4 EMI (near the ESP-NOW
  radio) and efficiency/runtime measurements.
- Addressable (WS2812) vs. discrete CC string is leaning **discrete** in
  [`roadmap.md`](../roadmap.md) (simpler legal-color / no-flash story); DE-04 assumes the
  discrete CC string. Revisit only if that lean changes.
- Whether to add a per-bar **NTC** for firmware thermal derate, or rely solely on the
  driver's thermal shutdown.
