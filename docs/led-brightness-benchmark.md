# LED brightness & luminous-flux benchmark — brake_light bar

A sizing study: **how bright should the helmet brake bar be, and which LEDs do we design
in to get there?** We anchor on the lights this device imitates — automotive **CHMSLs**
(center high-mounted stop lamps, "third brake lights") — read off their regulated
intensity, adjust for the helmet context, convert to a luminous-flux budget, and
down-select an LED architecture.

This is cross-cutting hardware design context (like [`hardware.md`](hardware.md)
itself). It feeds two design elements:
- **DE-04 — LED pattern/render engine + driver** (`BL-LED-*`): the bar, emitter choice,
  and constant-current driver sized here.
- **DE-02 — [auto-brightness](design/de-02-auto-brightness.md)** (`BL-BRT-*`): the
  daylight peak and night floor below set the **endpoints** of the lux→brightness curve.

> Numbers below are of two kinds, kept distinct on purpose: **regulatory/product facts**
> (cited) and **derived design estimates** (with their assumptions stated). The estimates
> are first-pass sizing, to be confirmed with a photometer against a built bar — not a
> compliance claim. Open-CHMBL is an **auxiliary** light and makes **no** lamp-approval
> claim; see [`safety-regulatory.md`](safety-regulatory.md).

---

## 1. What we're imitating, and why the CHMSL is the right benchmark

A helmet brake light is, functionally, a **single central high-mounted red stop signal
that supplements the vehicle's main brake lamps** — which is exactly what an automotive
**CHMSL** is. So the CHMSL, not the primary stop lamp, is the correct photometric
benchmark:

- Like a CHMSL it is **supplementary**, not the legally-required primary stop lamp
  (the bike keeps its own — see [README](../README.md) and the safety doc).
- Like a CHMSL it sits **high and centered** in the following driver's field of view.
- Unlike a car CHMSL it sits **even higher** (~1.4–1.6 m, at/above a following car
  driver's eye line) and, in traffic, often **closer** — which *raises* the glare risk
  and makes night auto-dimming non-negotiable (§4, DE-02).

So we benchmark the **CHMSL band**, deliberately **not** the much brighter primary
stop-lamp band.

---

## 2. Regulatory benchmark — the intensity envelope (candela)

Stop signals are regulated in **luminous intensity (candela, cd)** on the reference
axis, not in lumens. The two regimes that matter:

| Lamp role | Standard | On-axis (H-V) intensity | Notes |
|-----------|----------|--------------------------|-------|
| **CHMSL** (3rd brake light) | **ECE R7**, cat. **S3/S4** | **min 25 cd, max 110 cd** | The supplementary high-mount signal. This is our anchor. |
| **CHMSL** (3rd brake light) | **FMVSS 108 / SAE J186** (US) | **≈ 25 cd** on-axis min; edge test points fall to ~16 cd and ~4 cd | Photometric *table* of points, not a single number; supplementary lamp. |
| Primary stop lamp | **ECE R7**, cat. **S1/S2** | **min 60 cd, max 260 cd** | The bike's *own* brake light class — **brighter** than a CHMSL by design. We do **not** target this. |
| Motorcycle stop lamp | ECE R50 / FMVSS 108 (m/c) | order of **tens of cd** (≈ 40 cd min class) | The bike's own rear stop; same "much brighter than a CHMSL is required to be" point. |

**Read-off:** the regulated envelope for a *high-mounted supplementary* red stop signal
is **≈ 25–110 cd on-axis** (ECE S3/S4), with US CHMSL minimums in the same ~25 cd range.
That band is the benchmark.

> ECE R7 explicitly *excludes motorcycles* from its scope, and Open-CHMBL claims no
> approval under it — we borrow its **CHMSL photometric band as a design target**, which
> is what makes the light recognizable as a brake signal to following traffic.

Sources: [49 CFR 571.108 (FMVSS 108)](https://www.law.cornell.edu/cfr/text/49/571.108) ·
[UNECE Regulation No. 7 (full text PDF)](https://www.unece.org/fileadmin/DAM/trans/main/wp29/wp29regs/2015/R007r6e.pdf) ·
[Dun-Bri guide to UNECE R7](https://www.dun-bri.com/Regulations-Standards/UNECE-Regulations/Guide-To-ECE-R7-Position-Tail-Stop-and-Clearance-Lamps) ·
[Daniel Stern Lighting — lighting codes](https://www.danielsternlighting.com/tech/lights/codes/codes.html).

---

## 3. Product benchmark — what real LED CHMSLs actually are

To sanity-check the intensity band against hardware reality:

| Datum | Value | Source |
|-------|-------|--------|
| Incandescent CHMSL bulb (921) raw flux | **≈ 264 lm** — but emitted into 4π, most wasted in the housing; optics deliver only the ~25–80 cd that leaves the lens | product/TI |
| Typical OEM/aftermarket **LED CHMSL construction** | a **linear bar of ~6–16 mid-power red LEDs** behind reflector + lenticular optics, **constant-current** driven | [TI SSZT791 "CHMSL: the Third Brake Light"](https://www.ti.com/lit/ta/sszt791/sszt791.pdf) |
| LED CHMSL replacement **module flux** | **~170 lm** (modest) up to marketing-claimed **~1400 lm**; e.g. an [ORACLE linear CHMSL module](https://www.oraclelights.com/products/linear-universal-led-3rd-brake-light-chmsl-module-red) is **~6 W**, 12–24 V | product listings |
| LED CHMSL electrical power (typical) | **~1–3 W** for an OEM-style module | TI / product |

**Read-off:** real LED CHMSLs are a **handful of mid-power red LEDs, ~1–3 W**, and the
*module* flux that matters is small (~tens to low-hundreds of lumens) because optics
concentrate it into the regulated cone. Brute-force lumens are **not** how a CHMSL is
built — beam shaping is. That directly informs the down-select (§5).

---

## 4. Design target for the helmet bar

Combining §2 (intensity band) and §4-context (helmet sits higher/closer, battery-powered,
auto-dimmed):

| Quantity | Design target | Rationale |
|----------|---------------|-----------|
| **Daylight peak on-axis intensity** (BRAKE) | **≈ 50–80 cd** | Mid-to-upper of the ECE S3/S4 CHMSL band (25–110 cd). Enough to read as a brake signal in sun; not into primary-stop-lamp territory. |
| **Night floor on-axis intensity** (BRAKE) | **≈ 5–15 cd** | Still clearly "on/brake" at night without dazzling a close follower. Sets DE-02's dark-end. |
| **DECEL state** | **~30–50 %** of the current BRAKE level | Soft secondary cue; never brighter than BRAKE (see [ARCHITECTURE §4](../ARCHITECTURE.md#4-braking-state-machine)). |
| **OFF / running light** (optional) | **≈ 1–3 cd** | Dim presence marker only. |
| **Strobe** | **none** | Steady only; anti-strobe floor (`BL-RND-2`). |

We deliberately target the **CHMSL band, auto-dimmed**, rather than maxing out: it's
auxiliary, it's close to following eyes, and every lumen costs runtime (§6).

### 4.1 Candela → luminous-flux budget (so we can spec LEDs in lumens)

LEDs are sold in **lumens (Φ)**, not candela, so we convert through the beam solid angle:
**Φ ≈ I_peak × Ω_eff**.

**Beam assumption (design choice):** a helmet light must be read across lanes and at
varying follow distances and rider head positions, so we spread wider than a car CHMSL —
target a main lobe of about **±40° horizontal × ±12° vertical** (full 80° × 24°).

- Geometric solid angle ≈ (80°=1.40 rad) × (24°=0.42 rad) ≈ **0.59 sr**; peak-weighted
  effective ≈ **0.4–0.5 sr**.
- Directed flux at the daylight peak: Φ ≈ 65 cd × ~0.45 sr ≈ **~30 lm into the main
  lobe** → call it **≈ 25–50 lm of *directed* red flux** at full brightness.
- Account for optics + off-axis spill + thermal derating (η ≈ 0.5–0.7): required
  **raw LED-level red flux ≈ 40–90 lm**.

**Design point:** build in **≈ 60–100 lm of installed red LED capability**, and run it
**well below maximum** in normal use. That gives daylight headroom, thermal margin, and a
long-lived array that idles cool — and night/idle states drop to a few lumens.

> Sensitivity: these flux figures scale directly with the assumed beam width. A tighter
> beam (more like a car CHMSL, ±20°×±8°) needs **less** flux for the same cd but is less
> forgiving of head/lane position; a wider wash needs more. Confirm Ω with the chosen
> optic/diffuser, then re-derive. The **intensity** targets in §4 are the firm ones.

---

## 5. LED down-select

### 5.1 Color / wavelength
Red, **620–630 nm** (the automotive "stop red" band). This is where red luminous
efficacy is high and the color reads unambiguously as a brake signal. **Avoid deep red
(≈ 660 nm)** horticulture/IR-adjacent emitters: they look dim per watt to the eye and
sit at the edge of/outside the legal stop-red region. A red emitter (not a white LED
behind a red lens) is also the simplest legal-color story — matching the lean already in
[`hardware.md §2`](hardware.md#2-brake_light-helmet-side).

### 5.2 Architecture trade — three candidates

| Architecture | Hit 60–100 lm by | For | Against | Verdict |
|--------------|------------------|-----|---------|---------|
| **Mid-power red array** (2835/3030, ~6–12 emitters) | many low-driven emitters, ~20–40 lm each @ 60–100 mA, Vf ~2.0–2.3 V | **Even bar look**; low current per emitter; **redundancy / graceful degradation**; cheap; LCSC-sourceable; trivial constant-current driver; runs cool below rated → long life + daylight headroom | needs a (simple) CC driver and a diffuser | ✅ **Recommended** |
| **High-power emitters** (2–4 × Cree XP-E2 Red / OSRAM Oslon, ~70–90 lm @ 350 mA) | a few bright nodes + secondary optics | fewest parts; compact | hotspots / uneven bar; needs real optics + heatsinking on a helmet; higher per-node current; harder to keep low-profile & cool | ➖ Possible for a compact spot, worse for an even helmet bar |
| **Addressable RGB (WS2812-class) as the bar** | ~50–100 pixels at full red | per-pixel patterns | red die ≈ **1–2 lm each** (indicator-grade, hundreds of mcd) → need **dozens–hundreds** + **~1–2 A** to reach the flux target; expensive; RGB-white-die red is a weak, slightly-off stop color | ❌ **Rejected for the bar** |

**The addressable-vs-discrete open question is resolved by the flux math:** WS2812-class
parts are display/indicator emitters (~1–2 lm of red apiece), so meeting a CHMSL-class
**60–100 lm of red** would take dozens to hundreds of them and on the order of an amp —
absurd next to a handful of mid-power reds. Addressable RGB therefore stays exactly where
the architecture already puts it: the **[status indicator](design/de-10-status-indicator.md)
(DE-10/`BL-IND`)**, **not** the brake bar. The bar is **discrete 620–630 nm red on a
constant-current driver**.

### 5.3 Concrete emitter candidates (design-in shortlist)
Mid-power 2835/3030 red, 620–630 nm — verify the live datasheet (flux bin, Vf, thermal
derating) before committing, and prefer LCSC-stocked parts to match the BOM philosophy in
[`hardware.md §2.1`](hardware.md#21-integrated-module-candidates-ws2812--lipo-charger):

- **OSRAM** SYNIOS P2720 / TOPLED red (automotive-grade, 620–630 nm).
- **Cree** JSeries 2835 red.
- **Everlight / Lite-On / generic** 2835 red, ~625 nm, ~28–34 lm @ 60 mA, Vf ~2.0–2.2 V
  (the cheap, LCSC-clean baseline).

**Indicative array:** **~8–12** such emitters. At ~30 lm/emitter rated they total ~240–360
lm of *capacity*, so the bar delivers the **60–100 lm** target while each emitter runs at
**a fraction of its rated current** — cool, efficient, redundant, with plenty of daylight
headroom and lots of dimming range for DE-02. Final count follows the chosen optic/diffuser
and bar length.

---

## 6. Power & runtime cross-check (closes the loop with `hardware.md`)

At the **60–100 lm** raw-red design point and a driven-red wall-plug efficacy of
~**50–100 lm/W**, the bar draws **≈ 0.6–2 W** at the **daylight BRAKE peak** → on a
~3.3–3.7 V rail, roughly **0.2–0.6 A peak**. But:

- BRAKE events are **brief**; OFF/DECEL/night states sit far lower (a few lm, tens of mA).
- DE-02 **auto-dim** pulls the *average* down hard.

So the **average** LED draw stays in line with the **~120 mA** already assumed in the
[`hardware.md` runtime worked example](hardware.md#runtime-budgeting-worked-example), and
the **1500 mAh / ~8.5 h** full-day target holds. This benchmark **sizes the peak and the
emitter**; it doesn't blow the runtime budget. If a brighter beam is chosen later, re-run
that worked example — peak daylight BRAKE is the worst case for both glare and current.

---

## 7. Outputs / what this pins down

- **Intensity target:** daylight BRAKE **≈ 50–80 cd** on-axis; night floor **≈ 5–15 cd**
  → the endpoints of the DE-02 brightness curve.
- **Flux budget:** **≈ 60–100 lm** of installed 620–630 nm red LED capability, run below
  max.
- **Architecture:** **discrete mid-power red 2835/3030 array (~8–12 emitters) on a
  constant-current driver** — resolves the `hardware.md §3` "addressable vs. discrete"
  open question in favor of discrete red; addressable RGB is **status-indicator only**.
- **Feeds:** DE-04 (`BL-LED-*`) emitter/driver design; DE-02 (`BL-BRT-*`) curve endpoints.

## 8. Open items
- Confirm beam solid angle with the actual optic/diffuser, then re-derive the flux budget
  (§4.1 is beam-width-sensitive).
- Pick the final emitter part + bin and bar length / emitter count against it.
- Bench-verify on-axis cd and the day/night dim endpoints with a photometer on a built
  bar (this study is sizing, not a measurement).
- Thermal: confirm the chosen emitters stay within derating at the daylight peak inside a
  sealed IP65+ enclosure on a helmet.
</content>
</invoke>
