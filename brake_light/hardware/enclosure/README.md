# Brake_light — enclosure

Mechanical package for the helmet-side brake light: a **two-piece lenticular
clamshell** — a doubly-curved pod (two spherical caps bulging from a flat parting
plane, tapering to a thin rim) that conforms to a helmet (or a pack/jacket panel),
capturing the **PCB** and a clear **overmolded silicone lens** between an **inner
shell** (helmet-facing, carries the magnets) and an **outer shell** (lens side).

This directory holds the **strategy** (this file) and the **parametric OpenSCAD** model
([`scad/`](scad)). Nothing here is final — the PCB outline is a placeholder and every
dimension is a parameter — but the geometry is real, renders manifold, and captures the
agreed approach so the eventual board design has a package to target.

> Mounting and LiPo safety are mandatory reading and govern this design:
> [`docs/safety-regulatory.md`](../../../docs/safety-regulatory.md). See also the parts
> sketch in [`docs/hardware.md §2`](../../../docs/hardware.md#2-brake_light-helmet-side).

---

## 1. Strategy

A **lenticular clamshell** of two printed shells closing on a flat PCB + lens sandwich.
The device is a **doubly-curved pod**: a spherical cap bulges up on the helmet side and
down on the lens side, **both tapering to a thin rim** — curved in length *and* width.
The **parting plane between the shells is FLAT**, because the shells clamshell onto
either side of a flat, rigid PCB. We model the flat PCB first, then build each spherical
cap from that flat parting plane.

1. **PCB (flat, rigid).** Modelled explicitly ([`scad/pcb.scad`](scad/pcb.scad)) with a
   representative USB-C, module, and LED bar. Its LED face defines the flat parting plane.
2. **Outer shell (lens side, 3D-printed).** A perimeter frame with a window; it
   **doubles as the pour mould for the lens.** Pour clear silicone into the window
   cavity, then **set the PCB** onto the liquid silicone (LED side down). The silicone
   cures bonded to the board and captured in the frame — the lens is both the optic and
   the front gasket. (Alternative: pour in a separate reusable mould and drop the
   demoulded lens in — keeps the outer shell serviceable; costs a part.)
3. **Inner shell (helmet side, 3D-printed).** Seats the PCB on support ledges and closes
   over the top with a **flat inner (parting) face** and a **curved helmet face** that
   carries the **recessed magnet pockets** ([§4](#4-magnet-mounting--anti-shear-reconciliation)).
4. **Close the clamshell.** The two shells **capture the PCB and lens at the flat parting
   plane** and fasten together (4× vertical M2.5 in the frame corners, parametric).
   USB-C exits through one end.

```
   side section — lenticular: spherical caps bulge from a FLAT parting plane and
   taper to a thin rim (curved in length AND width):

            _____  inner shell (helmet side), convex cap  _____
        ╱‾‾                                                     ‾‾╲   ◀ convex helmet
       │  ●  recessed magnet   ▢ components ▢   recessed magnet ●  │      face
        ├═════════════ flat parting plane ═══ ░░ PCB ░░ ═══════════┤   ◀ straight seam
        ╲__   ▒▒▒▒  overmolded silicone lens (captured)  ▒▒▒▒   __╱   ◀ convex lens
            ‾‾‾‾‾‾  outer shell (lens window), convex cap  ‾‾‾‾‾        face, emits out
   USB-C ╝  (one end)
```

### Why this approach
- **A clamshell conforms.** A curved two-piece shell can follow a helmet/pack surface
  and capture the PCB + lens cleanly at the seam — which a straight slide-in extrusion
  cannot. (We **backed out** the earlier extrusion + end-cap concept for exactly this:
  it was a constant straight profile and could not be curved.)
- **Flat parting, curved walls.** Only the outer walls curve; the parting plane is flat
  so the two shells clamp a flat, rigid PCB. This sidesteps the rigid-board-vs-curve
  problem entirely — the board never has to bend.
- **Lenticular & doubly curved.** Spherical caps curve in both length and width and
  taper to a thin rim, matching the design sketch and giving a smooth, low-snag pod.
- **A cast silicone lens** gives an optically clear, shock-absorbing, weather-sealing
  front in one step, with no separate gasket — the lens *is* the seal at the window.
- **3D-printed shells** mean **zero hard tooling** and fast iteration; the outer shell
  doubling as the lens mould keeps the part count low.
- **Serviceable:** opening the inner shell exposes the PCB back (battery, USB-C).

---

## 2. Architecture: lenticular clamshell (chosen)

| Option | What it is | Verdict |
|--------|-----------|---------|
| **Lenticular clamshell (inner + outer shell)** *(chosen)* | Two shells closing on the PCB + lens; doubly-curved spherical caps from a flat parting, tapering to a thin rim. | **Primary.** Conforms to a helmet/pack in both axes and captures the sandwich at a flat seam. Modeled here. |
| **Extrusion + end cap** *(backed out)* | Constant straight profile; lens + PCB slide in along a slot, closed by an end cap. | **Removed.** Prints easily but is inherently straight — cannot conform to a curved surface. Superseded by this clamshell. |

---

## 3. OpenSCAD project

```
enclosure/
├── README.md            ← this strategy + design doc
├── Makefile             ← `make` renders printable STLs; `make assembly` the preview
├── .gitignore           ← ignores rendered stl/ output
└── scad/
    ├── enclosure.scad   ← the library: ALL parameters + every module
    ├── inner_shell.scad ← render target → inner (helmet) shell (printable)
    ├── outer_shell.scad ← render target → outer (lens) shell / pour mould (printable)
    ├── pcb.scad         ← reference target → the flat PCB (defines the parting plane)
    └── assembly.scad    ← preview only: both shells + PCB + lens + magnets
```

### Render

Requires the OpenSCAD CLI (`openscad`). From this directory:

```sh
make            # renders stl/inner_shell.stl and stl/outer_shell.stl
make assembly   # renders stl/assembly.stl (preview, not for printing)
make pcb        # renders stl/pcb.stl (reference flat PCB)
make clean
```

Or open any `scad/*.scad` in the OpenSCAD GUI. STL output is **git-ignored** —
regenerate it with `make`.

### Parameters & curvature

Everything is driven from the top of [`scad/enclosure.scad`](scad/enclosure.scad).
The **PCB block is a placeholder** (`pcb_len/pcb_width/pcb_th`). Axis convention (used
by every module): **X = length, Z = width** (the parting-plane footprint), **Y = height**
(+Y = helmet side, −Y = lens side). The **parting plane is flat at y = 0**; each outer
face is a **spherical cap** through the footprint rim:

- **`helmet_min_bulge` / `lens_bulge`** — the cap heights at the centre (helmet side /
  lens side). Bigger bulge → tighter curvature. Each cap is the sphere through its apex
  and the footprint half-diagonal `a`, so it tapers to the rim.
- **`end_margin` / `side_margin` / `corner_r`** — the rounded-rectangle footprint around
  the PCB (length, width, corner radius).

The cap radii are *derived* from the bulge + footprint; the helmet bulge is itself
derived so the dome is always deep enough to bury a magnet above the PCB:

```
helmet_bulge = max(helmet_min_bulge, pocket_depth + pcb_th + under_h + magnet_back_min)
```

> **Convex vs. concave helmet face.** The helmet cap is **convex** (lenticular, per the
> sketch) and held to the helmet/pack by the magnets. A true **concave** cup that mates a
> specific helmet radius is the natural alternative (invert the helmet cap to a sphere
> centred above the parting); left as a parameter direction — say the word and it flips.

Magnet defaults (`magnet_d = 20`, `magnet_th = 4`, N42) come from the starting bench
candidate in the [magnetic-mount exploration](../../../docs/design/explorations/mounting-magnetic.md#starting-candidates-to-evaluate).

---

## 4. Magnet mounting & anti-shear reconciliation

The magnets **drop into pockets in the inner-shell helmet face and are held by VHB
tape.** This deserves a careful note, because at first glance it contradicts the mount
exploration.

The [magnetic-mount exploration](../../../docs/design/explorations/mounting-magnetic.md)
argues that **peel**, not **shear**, must be the release mode, and that a **retention
pocket** should resist wind-shear so the device must lift before it can slide. The
**original** framing put that pocket on the *steel-target* side (recess the helmet/
garment target; keep the magnet face proud).

The **plan of record reverses this**: per the merged
["recess the magnets" revision](../../../docs/design/explorations/mounting-magnetic.md#why-peel-not-shear-is-the-right-failure-mode),
the pocket moves to the **magnet** side — the **magnets are recessed**, and the helmet's
steel landing pad (or crimped fabric, for a garment mount) seats **into** that recess.
Recessing the magnets resists shear in both helmet and garment cases while preserving
clean peel release.

**This enclosure implements exactly that plan of record.** Each pocket holds the magnet
**`magnet_recess` (1.2 mm default) below the helmet face**, so the proud rim forms the
shear key the steel pad keys into. The pockets serve double duty:

- **Retention** — VHB tape at the bottom of the pocket bonds the magnet; the pocket
  walls take shear load off the tape.
- **Anti-shear interface** — the recessed face + proud rim is the keyed pocket the plan
  of record calls for.

So the apparent conflict is only with the *superseded* (recess-the-target) wording; the
magnet-in-pocket layout here is **consistent with the current plan of record**. The
small remaining air gap from the recess slightly reduces pull and is part of the
force-window tuning the exploration's [bench protocol](../../../docs/design/explorations/mounting-magnetic.md#bench-verification-protocol)
already covers (hold ≥ 30 N, release ≤ 80 N).

---

## 5. Open items

- **Rigid PCB vs. curvature — resolved by the flat parting.** The board stays flat; only
  the outer caps curve, so the rigid PCB never has to bend. Remaining nuance: the flat
  board is a chord of the curved body, so the lens cap **pinches toward the rim** — keep
  the active PCB/lens area inside the region where the lens cap still has depth (raise the
  bulge/footprint, or shrink the board, if the edges get too thin).
- **Convex vs. concave helmet face.** Helmet cap is currently **convex** (lenticular, per
  the sketch), held by magnets. Flipping it to a **concave** cup that mates a chosen
  helmet radius is the natural alternative — a one-parameter change (see §3).
- **Device thickness.** Burying a 20 × 4 mm magnet **under** the PCB drives the centre
  stack (and thus the helmet bulge) to ~13 mm; the pod is ~18 mm thick at the centre,
  tapering to the rim. Thin it with a smaller/thinner magnet, magnets at a locally-thicker
  spot, or relocating them outboard of the PCB. Track against the mass budget.
- **Curvature is doubly-axial (spherical caps)** now — curves in length and width. A real
  helmet is closer to spherical, so a single `conform`-style radius could replace the two
  bulge params if a specific helmet fit is targeted.
- **Lens optics & material** — silicone shore hardness, clarity/diffusion, whether the
  window needs a lens/diffuser profile; mould **draft + release agent** for demoulding
  (or accept the lens bonding into the outer shell if it doubles as the mould).
- **Render cost** — the cap spheres use `sph_fn = 220`; lower it for faster iteration,
  raise it for a smoother surface.
- **Sealing target** — quantify the IP rating at the clamshell seam and the **USB-C**
  opening; decide on a seam gasket/bead vs. an interference fit.
- **Clamshell fastening** — vertical M2.5 holes through the frame corners are modeled;
  add matching bosses/heat-set inserts, or switch to snaps/latches.
- **Magnet spec & count** — `magnet_count`/`magnet_pitch_mm` and grade are placeholders;
  finalize against the bench force-window result.
- **Mass budget** — helmet-mounted mass drives neck load
  ([safety doc §3](../../../docs/safety-regulatory.md#3-helmet--rider-safety)).
- **Promotion** — when the magnetic mount is promoted from exploration to a `de-*`
  element, this enclosure becomes its mechanical interface.
