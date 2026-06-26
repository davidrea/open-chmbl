# Brake_light — enclosure

Mechanical package for the helmet-side brake light: a **two-piece, gently curved
clamshell** that conforms to a helmet (or a pack/jacket panel), capturing the **PCB**
and a clear **overmolded silicone lens** between an **inner shell** (helmet-facing,
carries the magnets) and an **outer shell** (lens side).

This directory holds the **strategy** (this file) and the **parametric OpenSCAD** model
([`scad/`](scad)). Nothing here is final — the PCB outline is a placeholder and every
dimension is a parameter — but the geometry is real, renders manifold, and captures the
agreed approach so the eventual board design has a package to target.

> Mounting and LiPo safety are mandatory reading and govern this design:
> [`docs/safety-regulatory.md`](../../../docs/safety-regulatory.md). See also the parts
> sketch in [`docs/hardware.md §2`](../../../docs/hardware.md#2-brake_light-helmet-side).

---

## 1. Strategy

A **clamshell** of two printed shells closing on a PCB + lens sandwich, curved along
its length so the helmet-facing face conforms to the shell it mounts on:

1. **Outer shell (lens side, 3D-printed).** A perimeter frame with a window; it
   **doubles as the pour mould for the lens.** Pour clear silicone into the window
   cavity, then **set the PCB** onto the liquid silicone (LED side down). The silicone
   cures bonded to the board and captured in the frame — the lens is both the optic and
   the front gasket. (Alternative: pour in a separate reusable mould and drop the
   demoulded lens in — keeps the outer shell serviceable; costs a part.)
2. **Inner shell (helmet side, 3D-printed).** A curved tray that seats the PCB on
   support ledges and closes over the top. Its outer (helmet) face carries the
   **recessed magnet pockets** ([§4](#4-magnet-mounting--anti-shear-reconciliation)).
3. **Close the clamshell.** The two shells **capture the PCB and lens at the parting
   line** (the PCB's LED face) and fasten together (radial M2.5 near each end,
   parametric). USB-C exits through one end wall.

```
   side section (the long axis is curved to conform):

        ╭───────────────  inner shell (helmet side) ───────────────╮   ◀ concave face
        │  ●  recessed magnet      ░░ PCB ░░       recessed magnet ●│      nests on helmet
        ╞══════════════════ parting line (LED face) ════════════════╡   ◀ clamshell seam
        │ ▒▒▒▒▒▒▒▒▒  overmolded silicone lens (captured)  ▒▒▒▒▒▒▒▒▒ │
        ╰──────────────────  outer shell (lens window)  ────────────╯   ◀ convex, emits
   USB-C ╝  (one end)                                                       outward
```

### Why this approach
- **A clamshell conforms.** A curved two-piece shell can follow a helmet/pack surface
  and capture the PCB + lens cleanly at the seam — which a straight slide-in extrusion
  cannot. (We **backed out** the earlier extrusion + end-cap concept for exactly this:
  it was a constant straight profile and could not be curved.)
- **A cast silicone lens** gives an optically clear, shock-absorbing, weather-sealing
  front in one step, with no separate gasket — the lens *is* the seal at the window.
- **3D-printed shells** mean **zero hard tooling** and fast iteration; the outer shell
  doubling as the lens mould keeps the part count low.
- **Serviceable:** opening the inner shell exposes the PCB back (battery, USB-C).

---

## 2. Architecture: curved clamshell (chosen)

| Option | What it is | Verdict |
|--------|-----------|---------|
| **Curved clamshell (inner + outer shell)** *(chosen)* | Two shells closing on the PCB + lens, curved along the length to conform. | **Primary.** The only one of the two that conforms to a helmet/pack and captures the sandwich at a seam. Modeled here. |
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
    └── assembly.scad    ← preview only: both shells + PCB + lens + magnets
```

### Render

Requires the OpenSCAD CLI (`openscad`). From this directory:

```sh
make            # renders stl/inner_shell.stl and stl/outer_shell.stl
make assembly   # renders stl/assembly.stl (preview, not for printing)
make clean
```

Or open any `scad/*.scad` in the OpenSCAD GUI. STL output is **git-ignored** —
regenerate it with `make`.

### Parameters & curvature

Everything is driven from the top of [`scad/enclosure.scad`](scad/enclosure.scad).
The **PCB block is a placeholder** (`pcb_len/pcb_width/pcb_th`). Axis convention (used
by every module): **X = length (the curved axis), Y = height/radial (+Y toward the
helmet centre), Z = width.** Curvature is a single-axis bend in the X-Y plane:

- **`conform_R`** — radius of the helmet-facing face. Default **150 mm** (helmet-ish;
  ~140–160 mm). Set larger (~300 mm+) for a flatter pack/jacket panel. The shells are
  concentric arcs about this radius, so the inner face nests flush on a matching shell.

Notable dimensions are *derived*, not hand-entered — e.g. the inner shell thickness is
derived so it always buries a magnet:

```
inner_shell_th = max(inner_min_wall, pocket_depth + magnet_back_min)
```

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

- **Rigid PCB vs. curvature.** The model treats the PCB as following the arc, but a
  rigid FR-4 board is **flat**. Over a 70 mm length at `conform_R = 150` the arc deviates
  ~4 mm from a flat chord. Resolve by **(a)** a short/segmented board, **(b)** a
  **rigid-flex or flexible PCB** that bends to the arc, or **(c)** keeping the board flat
  and letting the inner cavity carry the gap (cavity becomes a flat pocket). Decide
  before committing the board outline.
- **Device thickness.** Burying a 20 × 4 mm magnet **under** the PCB drives the radial
  stack to ~15 mm. Thin it by using a smaller/thinner magnet, placing magnets only at a
  locally-thicker mid-section, or accepting the thickness. Track against the mass budget.
- **Single- vs. double-axis curvature.** v1 bends along the length only (cylindrical).
  A helmet is closer to spherical — add width-axis curvature if conformance needs it.
- **Lens optics & material** — silicone shore hardness, clarity/diffusion, whether the
  window needs a lens/diffuser profile; mould **draft + release agent** for demoulding
  (or accept the lens bonding into the outer shell if it doubles as the mould).
- **Sealing target** — quantify the IP rating at the clamshell seam and the **USB-C**
  opening; decide on a seam gasket/bead vs. an interference fit.
- **Clamshell fastening** — radial M2.5 holes are modeled; add matching bosses/heat-set
  inserts, or switch to snaps/latches.
- **Magnet spec & count** — `magnet_count`/`magnet_pitch_mm` and grade are placeholders;
  finalize against the bench force-window result.
- **Mass budget** — helmet-mounted mass drives neck load
  ([safety doc §3](../../../docs/safety-regulatory.md#3-helmet--rider-safety)).
- **Promotion** — when the magnetic mount is promoted from exploration to a `de-*`
  element, this enclosure becomes its mechanical interface.
