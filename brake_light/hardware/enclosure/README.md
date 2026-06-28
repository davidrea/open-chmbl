# Brake_light — enclosure

Mechanical package for the helmet-side brake light: a **two-piece clamshell** whose
**inner shell is a concave dish** — the convex rear of the helmet *nests into it* — and
whose **outer shell is a convex lens cap**. The inner shell is **thinnest at the centre**
(just covering the PCB) and thickens toward the ends, where the **magnets** sit outboard.
It captures the **PCB** and a clear **overmolded silicone lens** at a **flat parting
plane** between the two shells.

This directory holds the **strategy** (this file) and the **parametric OpenSCAD** model
([`scad/`](scad)). Nothing here is final — the PCB outline is a placeholder and every
dimension is a parameter — but the geometry is real, renders manifold, and captures the
agreed approach so the eventual board design has a package to target.

> Mounting and LiPo safety are mandatory reading and govern this design:
> [`docs/safety-regulatory.md`](../../../docs/safety-regulatory.md). See also the parts
> sketch in [`docs/hardware.md §2`](../../../docs/hardware.md#2-brake_light-helmet-side).

---

## 1. Strategy

A **clamshell** of two printed shells closing on a flat PCB + lens sandwich. The
**inner (helmet) shell is a concave spherical dish** of radius `conform_R`, so the
convex rear of the helmet **nests into it** (the helmet installs from the +Y side). The
dish is **thinnest at the centre** — just enough to cover the PCB — and rises toward the
ends. The **outer (lens) shell is a convex cap**. The **parting plane between the shells
is FLAT**, because the shells clamshell onto either side of a flat, rigid PCB. We model
the flat PCB first, then build the cup and lens cap from that flat parting plane.

1. **PCB (flat, rigid).** Modelled explicitly ([`scad/pcb.scad`](scad/pcb.scad)) with a
   representative USB-C, module, and LED bar, plus **magnet keep-out notches** at the
   ends. Its LED face defines the flat parting plane.
2. **Outer shell (lens side, 3D-printed).** A perimeter frame with a central window; it
   **doubles as the pour mould for the lens.** Pour clear silicone into the window
   cavity, then **set the PCB** onto the liquid silicone (LED side down). The silicone
   cures bonded to the board and captured in the frame — the lens is both the optic and
   the front gasket. (Alternative: a separate reusable mould; costs a part.)
3. **Inner shell (helmet side, 3D-printed).** A **concave dish** (helmet nests here),
   thin at the centre over the PCB, thickening toward the ends where the **recessed
   magnet pockets** live ([§4](#4-magnet-mounting--anti-shear-reconciliation)).
4. **Close the clamshell.** The two shells **capture the PCB and lens at the flat parting
   plane** and fasten together (4× vertical M2.5 near the corners). USB-C exits a long
   **side** near one end (the end regions are taken up by the magnet pockets).

```
   helmet (convex) installs from +Y, nesting in the concave cup:
                              ▼
   length section ─ INNER shell is a concave dish, thin at centre, magnets outboard:

        ╲▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁╱      ◀ concave helmet cup
        █ magnet █    ░░ PCB ░░    █ magnet █
        ├──────────── flat parting plane ──────┤   ◀ straight seam
        ╲▁▁▁   overmolded silicone lens (captured)   ▁▁▁╱   ◀ convex lens, emits -Y
   (USB-C exits a long side, not shown)
```

### Why this approach
- **A concave cup conforms.** The dished helmet face mates the convex helmet shell
  (radius `conform_R`); magnets hold it on. (We **backed out** the earlier straight
  slide-in extrusion — it could not be curved at all.)
- **Flat parting clamps a rigid board.** Only the outer faces curve; the parting plane is
  flat, so the two shells clamp a flat, rigid PCB — the board never has to bend.
- **Thin centre, magnets outboard.** The cup is just thick enough to cover the PCB at the
  centre; the ends are thicker, so the magnets sit there, clear of the thin centre.
- **A cast silicone lens** gives an optically clear, shock-absorbing, weather-sealing
  front in one step, with no separate gasket — the lens *is* the seal at the window.
- **3D-printed shells** mean **zero hard tooling** and fast iteration; the outer shell
  doubling as the lens mould keeps the part count low.
- **Serviceable:** opening the inner shell exposes the PCB back (battery, USB-C).

---

## 2. Architecture: conforming clamshell (chosen)

| Option | What it is | Verdict |
|--------|-----------|---------|
| **Conforming clamshell (concave cup + convex lens)** *(chosen)* | Two shells closing on the PCB + lens; the inner shell is a concave dish the helmet nests into, the outer a convex lens cap, split at a flat parting. | **Primary.** Cups the convex helmet, thin over the PCB, magnets outboard. Modeled here. |
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
(+Y = helmet side, −Y = lens side). The **parting plane is flat at y = 0**:

- **`conform_R`** — radius of the **concave helmet cup** (default 150 mm; ~140–160 for a
  helmet, larger for a flatter pack/jacket). The cup is the lower face of a sphere
  centred above the parting, so it is **thinnest at the centre and rises toward the ends**.
- **`helmet_min_wall` / `under_h`** — wall over the PCB and component clearance; together
  they set the thin-centre thickness (`helmet_center_th = pcb_th + under_h +
  helmet_min_wall`).
- **`lens_bulge`** — the convex lens cap height at the centre; the cap tapers to the rim.
- **`magnet_x`** — how far outboard the magnets sit (default near the PCB ends). The
  footprint length (`end_margin`) is *derived* to contain them plus an `edge_wall`.
- **`side_margin` / `corner_r`** — width margin and footprint corner radius.

The cup centre, lens cap radius, and footprint length are all derived. The helmet cup is
intentionally **convex-helmet-mating (concave)**; flipping it to a convex pod is a
one-line change (centre the sphere below the parting instead of above).

The cap radii are *derived* from the bulge + footprint; e.g. the lens cap passes
through its apex and the footprint half-diagonal:

```
helmet_center_th = pcb_th + under_h + helmet_min_wall   // thin-centre thickness
cy_h             = conform_R + helmet_center_th          // cup-sphere centre, above parting
```

Magnet defaults (`magnet_d = 20`, `magnet_th = 4`, N42) come from the starting bench
candidate in the [magnetic-mount exploration](../../../docs/design/explorations/mounting-magnetic.md#starting-candidates-to-evaluate).

---

## 4. Magnet mounting & anti-shear reconciliation

The magnets **drop into pockets in the inner-shell helmet face and are held by VHB
tape.** They sit **outboard** (one near each end), in the thick part of the concave cup,
clear of the thin centre. Because a 20 mm × 4 mm magnet needs ~6 mm of pocket depth, the
PCB carries a **keep-out notch** at each magnet so the cup stays solid down to the
parting there (no board under the magnet). This deserves a careful note, because at first
glance it contradicts the mount exploration.

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
  the helmet cup and lens cap curve, so the rigid PCB never has to bend.
- **Concave cup conformance.** The helmet face is a concave sphere of `conform_R` and is
  **doubly curved** (length and width), so it nests on a roughly spherical helmet rear.
  A single radius is an approximation — a real helmet is not a perfect sphere; tune
  `conform_R`, or move to a measured helmet surface, when a specific lid is targeted.
- **Magnet keep-outs in the PCB.** Outboard magnets need ~6 mm of pocket depth, more than
  the thin cup leaves over the board, so the PCB has a **keep-out notch** (≈`keepout_d`)
  at each magnet. Confirm the real board can give up that area (it's at the ends, clear of
  the central LED bar / module). Alternatives: a smaller/thinner magnet (15 mm option in
  the exploration), or longer end lobes fully beyond the PCB.
- **Thin centre vs. components.** The centre is sized to `pcb_th + under_h +
  helmet_min_wall` (~6.6 mm). Keep tall parts (e.g. the module) off the dead centre, or
  raise `under_h`. Overall pod is ~19 mm at the thick ends, ~12 mm at the centre.
- **USB-C on the side.** Because the magnet pockets occupy the ends, USB-C exits a long
  **side** near one end (not the end, as in the sketch). Revisit if an end exit is
  required (e.g. magnets on the long sides instead).
- **Lens optics & material** — silicone shore hardness, clarity/diffusion, whether the
  window needs a lens/diffuser profile; mould **draft + release agent** for demoulding
  (or accept the lens bonding into the outer shell if it doubles as the mould).
- **Render cost** — the cap/cup spheres use `sph_fn = 200`; lower it for faster iteration,
  raise it for a smoother surface.
- **Sealing target** — quantify the IP rating at the clamshell seam and the **USB-C**
  opening; decide on a seam gasket/bead vs. an interference fit.
- **Clamshell fastening** — vertical M2.5 holes through the frame corners are modeled;
  add matching bosses/heat-set inserts, or switch to snaps/latches.
- **Magnet spec & count** — `magnet_count`/`magnet_x` and grade are placeholders;
  finalize against the bench force-window result.
- **Mass budget** — helmet-mounted mass drives neck load
  ([safety doc §3](../../../docs/safety-regulatory.md#3-helmet--rider-safety)).
- **Promotion** — when the magnetic mount is promoted from exploration to a `de-*`
  element, this enclosure becomes its mechanical interface.
