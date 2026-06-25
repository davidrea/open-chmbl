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
