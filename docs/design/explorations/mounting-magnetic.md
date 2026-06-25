# Exploration — Magnetic mounting

**Status:** 💭 future-state exploration (not in the [build order](../README.md#3-design-elements-build-order)) ·
**Device:** brake_light · **Supersedes nothing yet** — the adhesive/strap breakaway
mount in [`hardware.md §2`](../../hardware.md#2-brake_light-helmet-side) remains the
baseline until one of these is promoted to a real `de-*` element.

Two related ideas, both built on the same insight: **a magnetic interface gives us a
shear/peel release "for free."** A magnet pair holds firmly in normal use but lets go
along a predictable, tunable force when the light is snagged or struck — which is
exactly the **breakaway / frangible** behaviour the [safety doc](../../safety-regulatory.md#3-helmet--rider-safety)
already requires of any mount. Instead of engineering a separate shear pin or
breakaway adhesive, the mount *is* the release.

Both explorations keep the project's hard rule intact: **never penetrate the helmet
shell.** The only thing ever bonded to a helmet here is **VHB tape**, which is already
the accepted non-penetrating method.

---

## Exploration A — Garment / backpack shoulder mount

Move the light **off the helmet entirely** and onto the rider's upper back — across
the shoulder blades on a jacket, or the upper panel of a backpack.

```
   (rider's back, between the shoulder blades)
   ┌──────────────────────────────────────────┐
   │  outer garment shell                       │
   │   ███ magnets (on the light) ███           │  ← brake_light clamps here
   │  ────────── fabric ──────────              │
   │   ▓▓▓ padded steel strip (inside) ▓▓▓      │  ← ferrous target, sewn/pocketed in
   └──────────────────────────────────────────┘
```

- **How it attaches:** magnets live in the light; a **padded steel strip** (thin
  ferrous shim in a fabric/foam sleeve) sits **inside** the garment. The fabric is
  sandwiched between them. No clips, no holes in the garment required for a backpack
  panel; jackets can carry the strip in an internal pocket or a sewn sleeve.
- **Why it's attractive:**
  - **Sidesteps helmet-certification concerns entirely** — nothing touches the helmet,
    so the DOT/ECE "don't attach things to a certified helmet" worry doesn't apply.
  - **Inherent shear release** — if a strap, branch, or a fall catches the light, it
    peels off the magnets rather than dragging on the rider.
  - Mounting surface is large and roughly flat; easy to get a high, central, visible
    position.
- **Trade-offs / open questions:**
  - **Lower and less stable than a helmet** — a shoulder/back panel pitches and shifts
    with the rider's torso and with wind buffeting; retention vs. clean release is a
    tighter window than on a rigid shell.
  - **Depends on the rider wearing the right garment** with the strip fitted; not
    universal.
  - **Padding & comfort** — the inside strip must be padded so it isn't a hard lump
    against the back; placement must avoid the spine and any armour pockets.
  - Aiming: a torso-mounted light points wherever the rider is leaning — acceptable for
    a courtesy cue, worth noting for visibility claims.

## Exploration B — Helmet magnetic mount (interchangeable across helmets)

Keep the light on the helmet, but split the mount into a **cheap ferrous target that
stays on each helmet** and the **magnets that live in the light**.

```
   helmet shell                       brake_light
   ┌─────────────┐                    ┌─────────────┐
   │  VHB tape    │  ⇦ bonds to shell │  ███ magnets │
   │  ▓ steel ▓   │ ))   (no drilling) │  ███         │
   │  target      │  ⇨ magnetic catch │             │
   └─────────────┘                    └─────────────┘
```

- **How it attaches:** a thin **steel plate / strip** is bonded to each helmet with
  **VHB tape** (the existing non-penetrating method). The **magnets are in the light.**
  The light snaps onto whichever helmet's steel target it's near.
- **Why it's attractive — interchangeability:** a rider with multiple helmets (commuter
  lid, track lid, adventure lid) fits a cheap steel target to each **once**, then moves
  the **single** (expensive) light between them in seconds. No re-stickering, no second
  electronics unit.
- **Shear release preserved** — same peel/shear behaviour as Exploration A; the light
  pops off instead of transmitting load to the head/neck or snagging.
- **Trade-offs / open questions:**
  - **Ferrous mass on the helmet** — the steel target adds a little mass and offset.
    Keep it **thin and low-profile**; the safety doc's "minimize mass, keep it close to
    the shell" rule applies to the target, not just the light.
  - **VHB still owns the helmet interface** — its bond strength and the curved-shell
    contact patch set the *real* upper limit on hold; the magnets must be tuned to
    release **below** what would peel the VHB (and well below any head/neck load).
  - Target alignment/indexing so the light seats in the correct orientation every time.

---

## Force analysis and initial magnet sizing

The whole game is the **force window**: hold firmly through everything normal riding
throws at the mount; release cleanly and predictably in the direction a crash snag
would pull. This section develops that window from first principles and translates it
into a starting magnet size and remanence (Br, in Tesla) to evaluate on the bench.

### Required hold force (minimum)

Normal-riding loads on a helmet-mounted light are smaller than intuition suggests —
aerodynamic drag on a small body is modest, and it's inertial shock loads that set
the floor:

| Load | Basis | Force |
|------|-------|-------|
| Aerodynamic drag at 130 km/h (36 m/s) | F = ½ρCdAv²; ρ = 1.225 kg/m³, Cd ≈ 0.8 (bluff body), A ≈ 80 cm² = 0.008 m² | **≈ 5 N** |
| Inertial — hard braking at 0.8g | F = m × a; device mass ≈ 200 g, a = 0.8 × 9.81 | **≈ 1.6 N** |
| Inertial — road impact / kerb at 3g | F = m × a; 200 g × 3 × 9.81 | **≈ 5.9 N** |
| Vibration (sustained lateral/vertical) | ~1–2 N continuous | **≈ 2 N** |

Realistic worst case (aero + road shock, same axis): **~11 N**.
With a **2.5× safety margin** to account for gusts, resonance, and device-mass growth:
**hold ≥ 28 N → round to 30 N target.**

Wind force alone is ≤ 5 N at highway speed. The magnets are *not* at risk of
shearing off from wind; vibration and shock are the constraining loads, and they're
still modest.

### Required release force (maximum)

The release limit is set by **what happens to the rider, not the device**, if the
mount does *not* let go during a crash snag.

For the **helmet mount (Exploration B)**, a snagged light applies a lateral force to
the helmet shell. The helmet sits approximately 60–80 mm from the effective neck pivot
(occiput–C1). The torque that force creates about the neck:

```
M_neck = F_snag × L_arm   (L_arm ≈ 70 mm)
```

Rotational neck-injury risk rises sharply above roughly 30–40 N·m of applied moment
(this is the regime addressed by ECE 22.06 oblique-impact tests). Working backwards:

```
F_snag limit = M_neck / L_arm = 35 N·m / 0.07 m ≈ 500 N
```

So the neck is mechanically tolerant of much higher forces than the magnet needs to
produce — but **the goal of a breakaway mount is to release before any significant
load is transmitted**, not to resist injury-level loads. Practical breakaway-mount
targets for helmet attachments (e.g. action-camera breakaway mounts designed for
motorsport helmets) converge on **50–100 N**, providing a large margin below the
neck-tolerance limit and releasing early in the snag event.

**Release target: ≤ 80 N** (in the dominant snag direction — peel).

For the **garment mount (Exploration A)**, the snag-to-neck force path is much longer
and less direct; the same 30–80 N window applies, but here it governs device retention
(shedding at speed = road debris) rather than direct rider injury risk.

**Design force window: hold ≥ 30 N, release ≤ 80 N.**
Good operating point: **40–60 N** in the primary failure direction.

### Why peel, not shear, is the right failure mode

There are two ways a magnet can separate from a steel target:

| Mode | Description | Force relative to pull |
|------|-------------|----------------------|
| **Shear** | Magnet slides laterally across the target face | ≈ μ × F_pull; μ ≈ 0.10–0.15 (smooth coated surfaces) → **10–15 % of pull** |
| **Peel** | One edge lifts first; the contact area reduces progressively | ≈ 40–60 % of F_pull, geometry-dependent |

Shear force is actually **lower** than peel force for a given magnet, but its
relationship to the pull force is dominated by surface friction — highly variable
depending on coatings, contamination, and surface finish. More importantly, shear is
the direction **wind acts on the mount** (lateral to the mount face). If we rely on
shear release in a crash, we've also created a mount that the wind can release.

**Peel is better for two reasons:**

1. Wind applies a *lateral shear* force, which a shallow retention pocket resists
   independently of the magnets. A 2–3 mm pocket around the steel target means the
   device must lift **up** before it can slide sideways — converting a wind-shear
   load into a peel load that the magnet resists strongly.
2. A crash snag applies a force with a moment arm about the mount edge — exactly the
   condition that initiates peel. Peel force is tunable (via pocket depth, magnet
   area, and Br) and more consistent than friction-dependent shear.

**Design the pocket to make peel the only release mode.** Wind forces stay inside the
pocket; snag forces peel it off.

### Pull-force formula and magnet sizing

For a neodymium disc magnet pressed flush against a thick ferromagnetic steel plate
(the theoretical maximum, achieved at zero air gap):

```
F_pull = (Br² × A) / (2μ₀)
```

- **Br** = remanence field (T) — the magnet's N-grade specification
- **A** = pole-face area (m²)
- **μ₀** = 4π × 10⁻⁷ ≈ 1.257 × 10⁻⁶ H/m

Real-world efficiency relative to this formula: **60–70 %** (coatings, fabric layer
in the garment mount, plate thickness, surface irregularities). Use **65 %** as the
planning factor.

Peel release at the geometry of interest is approximately **50 % of the effective pull
force** (this varies with aspect ratio and pocket depth — characterize on the bench).
So the chain is:

```
F_peel ≈ 0.50 × F_effective = 0.50 × 0.65 × F_theoretical = 0.33 × F_theoretical
```

To hit a peel release of **50 N** (midpoint of our window):

```
F_theoretical = 50 / 0.33 ≈ 152 N

A = F_theoretical × 2μ₀ / Br²
```

#### N42H example (Br = 1.30 T)

```
A = 152 × 2 × 1.257 × 10⁻⁶ / (1.30²)
  = 3.82 × 10⁻⁴ / 1.69
  = 2.26 × 10⁻⁴ m²

Disc diameter d = 2 × √(A/π) = 2 × √(2.26 × 10⁻⁴ / 3.14159) ≈ 17 mm
```

#### N35H example (Br = 1.18 T)

```
A = 152 × 2 × 1.257 × 10⁻⁶ / (1.18²)
  = 3.82 × 10⁻⁴ / 1.393
  = 2.74 × 10⁻⁴ m²

Disc diameter d = 2 × √(2.74 × 10⁻⁴ / 3.14159) ≈ 19 mm
```

### Starting candidates to evaluate

All candidates are **disc magnets** (circular cross-section). Dimensions are
**diameter × thickness**.

| Candidate | Br (T) | Diameter | Thickness | Predicted peel release | Notes |
|-----------|--------|----------|-----------|----------------------|-------|
| **20 mm dia. × 4 mm thick N42H** | 1.28–1.32 | 20 mm | 4 mm | **~55–65 N** | Starting point; sits near the upper end of the window — good margin. |
| **15 mm dia. × 4 mm thick N42H** | 1.28–1.32 | 15 mm | 4 mm | **~30–40 N** | Weaker alternative if 20 mm proves too strong; lower end of window. |
| **20 mm dia. × 4 mm thick N35H** | 1.17–1.22 | 20 mm | 4 mm | **~45–55 N** | Slightly softer; good if N42H peel is too close to the 80 N ceiling. |

All three are **H-grade** (continuous working temperature ≥ 120 °C) — required because
a black helmet or dark jacket in direct sun can reach 60–80 °C at the surface, and
standard N-grade magnets (80 °C max) would partially demagnetize and lose pull
unpredictably over time. See [magnet selection](#magnet-selection) below for coating
and corrosion notes.

**Use N42H / 20 mm dia. × 4 mm thick as the initial bench candidate.** Tune from there:
- Peel too high (>80 N): try N35H 20 mm dia., or reduce diameter to 15 mm.
- Peel too low (<30 N): increase thickness to 6 mm, or step to N48H (Br ≈ 1.40 T).
- Adjust pocket depth (shallower → easier peel; deeper → more wind resistance before peel).

### Bench verification protocol

A spring-force gauge is all that's needed:

1. Bond the steel target to a rigid backing with the correct VHB stack-up (simulating
   helmet shell + tape).
2. Seat the device; check that it indexes correctly into the pocket.
3. **Shear pull (lateral):** apply force parallel to the face with a gauge — this
   should hold above 30 N without releasing (peel should not initiate until the edge
   lifts).
4. **Peel pull:** apply force at the trailing edge to initiate peel — record the
   peak force. Target: 40–60 N. Repeat 20 cycles; release force should be consistent.
5. **Wind simulation:** aim a fan at realistic angles; confirm no detachment at
   highway-equivalent dynamic pressure.
6. **Temperature soak:** repeat after 30-minute oven soak at 70 °C (simulating sun-
   heated surface) — confirm pull force does not drop more than 10–15 %.

---

## Shared design notes (apply to both)

### Tuning the release
- **Hold vs. release is the whole game.** The magnet pull must beat wind, vibration,
  and normal head/torso motion, yet release below the snag/neck-load threshold. Target
  a **specified peel and shear force**, then verify on the bench (spring-gauge pulls)
  and in airflow.
- **Pole geometry sets directionality.** Arranging poles (alternating array / keyed
  pockets) can give a **strong normal hold and rotation resistance** while still
  **peeling away cleanly** at an edge — the failure direction we *want*.
- **Self-seating features** (a chamfer or shallow pocket around the target) help the
  light index to the right position and orientation without fiddling.

### Magnet selection
- **Neodymium**, but mind temperature: a **black helmet or jacket in direct sun** can
  sit well above ambient. Standard N-grades start losing force as they heat; specify a
  **higher working-temperature grade** (e.g. an `xxM`/`xxH` temper) sized for the worst
  case, or de-rate the pull accordingly.
- **Corrosion protection** — sweat and weather mean **Ni-Cu-Ni or epoxy-coated**
  magnets and a **coated/stainless or sealed** steel target.
- No magnetic-sensitivity worries on our side (the ESP32 and radio don't care); just
  keep magnets from chafing the LiPo.

### Safety alignment
This direction is **consistent with**, not a relaxation of,
[`safety-regulatory.md §3`](../../safety-regulatory.md#3-helmet--rider-safety):
non-penetrating (VHB-only on any helmet), breakaway (the magnetic release *is* the
breakaway), minimal mass/offset (keep targets thin), sealed enclosure unchanged. The
new failure mode to characterize is **unintended detachment at speed** — a lost device
and possible road debris, not a rider-injury path — which is the same hold-force
trade-off above, biased so it never sheds in normal riding.

### Component availability (LCSC)
Per the project's soft LCSC-availability preference: **magnets, steel shim/target
stock, and VHB tape are generally *not* LCSC SKUs** (LCSC stocks electronic
components). Expect to source:
- **Magnets** from a magnet specialist (graded, coated, temperature-rated).
- **Steel targets** from shim/spring-steel stock (or laser-cut service), corrosion-coated.
- **VHB** from 3M / distributors.

So this mount sits **outside** the LCSC BOM by nature; that's expected and fine for a
mechanical interface.

---

## What promotion would look like
To become a real `de-*` design element this needs: a chosen variant (A, B, or both as
options), measured hold/release forces against defined targets, a temperature-validated
magnet spec, and a mechanical interface to the [enclosure](../../hardware.md#2-brake_light-helmet-side).
Until then it stays here as a direction, and the adhesive/strap breakaway mount remains
the baseline.
