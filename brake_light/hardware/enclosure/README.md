# Brake_light — enclosure

Mechanical package for the helmet-side brake light: a **clear poured-silicone lens**
over the PCB, dropped into a **3D-printed extruded outer body** closed with a
**3D-printed end cap**, mounted to the helmet by **recessed magnets**.

This directory holds the **strategy** (this file) and the **parametric OpenSCAD**
model ([`scad/`](scad)). Nothing here is final — the PCB outline is a placeholder and
every dimension is a parameter — but the geometry is real, renders manifold, and
captures the agreed approach so the eventual board design has a package to target.

> Mounting and LiPo safety are mandatory reading and govern this design:
> [`docs/safety-regulatory.md`](../../../docs/safety-regulatory.md). See also the parts
> sketch in [`docs/hardware.md §2`](../../../docs/hardware.md#2-brake_light-helmet-side).

---

## 1. Strategy

Four printed/poured pieces and the PCB, in this build order:

1. **Lens (poured silicone).** A clear silicone lens is **cast in a 3D-printed mold**
   ([`scad/lens_mold.scad`](scad/lens_mold.scad)). The mold cavity is the lens shape
   with its emitting face down and an open top.
2. **Lens + PCB become one part.** After pouring, the **PCB is set into the mold** onto
   the still-liquid silicone (LED side down, into the lens). The silicone cures bonded
   to the board. **After cure the PCB + lens lift out of the mold as a single
   sub-assembly** — the lens is both the optic and the front gasket.
3. **Outer body (3D-printed, extruded).** The lens + PCB sub-assembly **slides
   lengthwise into an extruded outer body** ([`scad/outer_body.scad`](scad/outer_body.scad)).
   The body is a constant cross-section: the PCB is captured under two retention
   **lips**, and the lens **neck protrudes through the top aperture** as the light-
   emitting face. The far end is closed integrally and acts as a slide stop.
4. **End cap (3D-printed).** A printed **end cap** ([`scad/end_cap.scad`](scad/end_cap.scad))
   plugs the open end, retains the sub-assembly against the back stop, and closes the
   package. Fastened (2× M2.5 self-tap, parametric) — kept removable for service.
5. **Magnets.** Disc magnets **drop into pockets in the bottom (helmet-facing) face**,
   bonded with **VHB tape** at the bottom of each pocket. They sit slightly **recessed**
   — see [§4](#4-magnet-mounting--anti-shear-reconciliation).

```
            lens neck emits here  ▲ +Y (top)
        ┌───────────────────────────────────┐
   lips │  ░░░ lens (poured silicone) ░░░    │ lips     ← top aperture
        │ ┌───────────────────────────────┐ │
        │ │           PCB                  │ │          ← captured on ledges
        │ └───────────────────────────────┘ │
        │   (under-PCB component clearance)  │
        │  ●          magnet            ●    │          ← recessed pockets + VHB
        └───────────────────────────────────┘ ▼ -Y (helmet-facing bottom)
              ◀──── end cap        back stop ────▶  (slide axis = length)
```

### Why this approach
- **A cast silicone lens** gives an optically clear, shock-absorbing, weather-sealing
  front in one step, with **no machining and no separate gasket** — the lens *is* the
  seal at the aperture. Silicone tolerates the helmet thermal/vibration environment and
  the [breakaway](../../../docs/safety-regulatory.md#3-helmet--rider-safety) philosophy
  (compliant, low-mass front).
- **3D-printed mold + body + cap** means **zero hard tooling** and fast iteration —
  re-print to change the board size, lens optics, or magnet layout.
- **Drop-in + end cap** keeps the board **serviceable** (battery, USB-C) and the part
  count low.

---

## 2. Outer-body architecture: extrusion + end cap (chosen)

Two outer-enclosure architectures were on the table:

| Option | What it is | Verdict |
|--------|-----------|---------|
| **Extrusion + end cap** *(chosen)* | Constant-profile body; the lens + PCB slide in along a slot and an end cap closes it. | **Primary.** Prints flat with no supports, one continuous sealing path, maps directly to "drop into a slot, seat, end cap." Magnet pockets live in the extruded floor. |
| **Clamshell (inner + outer)** | Two half-shells with inner/outer shelves that capture the lens + PCB sandwich; screwed/snapped together. | **Documented alternative.** More familiar and slightly easier service access, but more sealing surface to manage and usually needs supports or gasket detail. Kept on record; not modeled. |

The model implements the **extrusion + end cap** option. If we revisit the clamshell,
the lens/PCB sub-assembly and the magnet-pocket detail carry over unchanged — only the
outer shell changes.

---

## 3. OpenSCAD project

```
enclosure/
├── README.md            ← this strategy + design doc
├── Makefile             ← `make` renders printable STLs; `make assembly` renders the preview
├── .gitignore           ← ignores rendered stl/ output
└── scad/
    ├── enclosure.scad   ← the library: ALL parameters + every module
    ├── outer_body.scad  ← render target → outer body (printable)
    ├── end_cap.scad     ← render target → end cap (printable)
    ├── lens_mold.scad   ← render target → silicone pour mold (printable)
    └── assembly.scad    ← preview only: body + PCB + lens + cap + magnets
```

### Render

Requires the OpenSCAD CLI (`openscad`). From this directory:

```sh
make            # renders stl/outer_body.stl, stl/end_cap.stl, stl/lens_mold.stl
make assembly   # renders stl/assembly.stl (preview, not for printing)
make clean
```

Or open any `scad/*.scad` in the OpenSCAD GUI. STL output is **git-ignored** —
regenerate it with `make`.

### Parameters

Everything is driven from the top of [`scad/enclosure.scad`](scad/enclosure.scad).
The **PCB block is a placeholder** (`pcb_len/pcb_width/pcb_th`) — replace it with the
real outline and the body, lens, lips, aperture, and mold all follow. Axis convention
(used by every module): **X = width, Y = height (Y=0 is the helmet-facing bottom), Z =
length / slide axis.** Notable derived dimensions are computed, not hand-entered — e.g.
the floor thickness is derived so it always buries a magnet:

```
floor_t = max(3.0, magnet_recess + magnet_th + vhb_th + magnet_back_min)
```

Magnet defaults (`magnet_d = 20`, `magnet_th = 4`, N42) come from the starting bench
candidate in the [magnetic-mount exploration](../../../docs/design/explorations/mounting-magnetic.md#starting-candidates-to-evaluate).

---

## 4. Magnet mounting & anti-shear reconciliation

The magnets **drop into pockets in the bottom face and are held by VHB tape.** This
deserves a careful note, because at first glance it contradicts the mount exploration.

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

**This enclosure implements exactly that plan of record.** Each magnet pocket holds the
magnet **`magnet_recess` (1.2 mm default) below the pocket rim**, so the proud rim forms
the shear key the steel pad keys into. The pockets therefore serve double duty:

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

- **Real PCB outline** — replace the placeholder `pcb_*` parameters; confirm LED-bar
  footprint, USB-C/button locations (and how they exit the sealed package), and whether
  the lens covers the whole top or just the LED bar.
- **Lens optics & material** — silicone shore hardness, clarity/diffusion, and whether
  the neck needs a lens/diffuser profile rather than a flat face; mold **draft angles
  and a release agent** for clean demolding (current cavity is straight-walled).
- **Sealing target** — the lens seals the aperture, but quantify the IP rating at the
  end cap and USB-C; decide on an end-cap gasket vs. an interference plug.
- **End-cap fastening** — model the matching pilot bosses in the body (cap holes exist;
  body bosses are a TODO), or switch to a snap/latch.
- **Magnet spec & count** — `magnet_count`/`magnet_pitch` and grade are placeholders;
  finalize against the bench force-window result.
- **Mass budget** — track total mass; helmet-mounted mass drives neck load
  ([safety doc §3](../../../docs/safety-regulatory.md#3-helmet--rider-safety)).
- **Promotion** — when the magnetic mount is promoted from exploration to a `de-*`
  element, this enclosure becomes its mechanical interface.
