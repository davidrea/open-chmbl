// =============================================================================
// Open-CHMBL — brake_light enclosure (parametric)
// =============================================================================
// Strategy (see ../README.md):
//   1. Pour a clear silicone LENS into a 3D-printed MOLD (lens_mold()).
//   2. Set the PCB into the mold; after cure, the PCB + lens come out as one part.
//   3. That assembly SLIDES into a 3D-printed extruded OUTER BODY (outer_body())
//      and is closed with an END CAP (end_cap()).
//   4. Magnets drop into pockets in the BOTTOM (helmet-facing) face, retained with
//      VHB, sitting slightly RECESSED so the helmet steel pad keys into the recess
//      (the "recess-the-magnets" plan of record — see ../README.md and
//      docs/design/explorations/mounting-magnetic.md).
//
// Axis convention for every module here:
//     X = width      (left/right across the bar)
//     Y = height     (Y=0 is the bottom/helmet-facing face; lens emits at +Y top)
//     Z = length     (the slide / extrusion axis)
//
// All dimensions are millimetres. Everything is parametric: the PCB numbers are
// PLACEHOLDERS until the real board exists — change the PCB block and the rest of
// the geometry follows.
// =============================================================================

// ---- PCB (PLACEHOLDER — replace with the real board outline) -----------------
pcb_len    = 70;    // Z, slide axis
pcb_width  = 30;    // X
pcb_th     = 1.6;   // Y

// ---- Lens (poured silicone) --------------------------------------------------
lens_base_h = 2.0;  // shoulder height (full PCB width) that sits UNDER the lips
lens_neck_h = 4.0;  // emitting neck height that shows THROUGH the aperture
lens_neck_w = 22;   // width of the emitting face (must be < pcb_width to be captured)

// ---- Fit / clearances --------------------------------------------------------
clr      = 0.4;     // general sliding clearance
wall     = 2.4;     // outer side-wall thickness
ledge    = 1.5;     // PCB support ledge width per side
under_h  = 2.0;     // clearance under the PCB for bottom-side components

// ---- Magnets (starting candidate from mounting-magnetic.md: N42 20x4 disc) ----
magnet_d        = 20;    // disc diameter
magnet_th       = 4;     // disc thickness
vhb_th          = 0.8;   // VHB tape under the magnet, inside the pocket
magnet_recess   = 1.2;   // how far the magnet face sits below the pocket rim
magnet_back_min = 1.6;   // solid material left between pocket and inner channel
magnet_count    = 2;     // discs along the length
magnet_pitch    = 40;    // centre-to-centre along Z

// ---- Outer body / end cap ----------------------------------------------------
back_wall    = 3.0;  // integrally-closed far end (acts as a slide stop)
cap_th       = 3.0;  // end-cap flange thickness
cap_plug_len = 6.0;  // how far the cap plug reaches into the channel
screw_d      = 2.5;  // end-cap fastener clearance (M2.5 / self-tap), parametric

// ---- Render quality ----------------------------------------------------------
$fa = 2;
$fs = 0.4;
eps = 0.05;

// ---- Derived -----------------------------------------------------------------
inner_w    = pcb_width  + 2*clr;          // channel width at the PCB
aperture_w = lens_neck_w + 2*clr;         // top opening width
W          = inner_w + 2*wall;            // outer width
// floor must be deep enough to bury a magnet + VHB + recess and leave a backing wall
floor_t    = max(3.0, magnet_recess + magnet_th + vhb_th + magnet_back_min);
pcb_bottom = floor_t + under_h;
pcb_top    = pcb_bottom + pcb_th;
lip_z      = pcb_top + lens_base_h;       // underside of the retention lips
H          = lip_z + lens_neck_h;         // outer height (neck flush with top)
under_void_w = inner_w - 2*ledge;         // narrower channel below the PCB (ledges)

channel_len  = pcb_len + clr + cap_plug_len;  // open channel section
body_len     = back_wall + channel_len;       // total extruded length

pocket_depth = magnet_recess + magnet_th + vhb_th;
pocket_d     = magnet_d + clr;

// =============================================================================
// 2D cross-sections (in the X=width / Y=height plane)
// =============================================================================

// The negative space the PCB + lens slide through. Open at the top (aperture) so
// the lens neck emerges; the step from inner_w -> aperture_w forms the lips.
module channel_void_2d() {
    union() {
        // clearance under the PCB (its narrower width forms the support ledges)
        translate([-under_void_w/2, floor_t])
            square([under_void_w, pcb_bottom - floor_t]);
        // PCB + lens shoulder region
        translate([-inner_w/2, pcb_bottom])
            square([inner_w, lip_z - pcb_bottom]);
        // aperture: open to the top
        translate([-aperture_w/2, lip_z])
            square([aperture_w, H - lip_z + eps]);
    }
}

// Solid outer cross-section (used for the closed back wall).
module outer_2d() {
    translate([-W/2, 0]) square([W, H]);
}

// Body wall cross-section = outer minus the channel.
module body_2d() {
    difference() {
        outer_2d();
        channel_void_2d();
    }
}

// =============================================================================
// Lens — the cured silicone part (reference / assembly only; not printed)
// =============================================================================
module lens() {
    color("LightCyan", 0.45)
    linear_extrude(pcb_len)
        union() {
            translate([-pcb_width/2, pcb_top])   square([pcb_width, lens_base_h]);
            translate([-lens_neck_w/2, lip_z])   square([lens_neck_w, lens_neck_h]);
        }
}

// PCB stand-in (reference / assembly only).
module pcb() {
    color("ForestGreen")
    translate([-pcb_width/2, pcb_bottom, 0])
        cube([pcb_width, pcb_th, pcb_len]);
}

// =============================================================================
// Lens pour mold (3D-printed) — cavity is the lens, emitting face DOWN,
// open at the top with a rebate that locates the PCB.
// =============================================================================
mold_wall  = 3.0;
mold_floor = 3.0;
fit_pcb    = 0.3;   // PCB-to-rebate fit in the mold

mold_W = pcb_width + fit_pcb + 2*mold_wall;
mold_H = mold_floor + lens_neck_h + lens_base_h + pcb_th;
mold_L = pcb_len + 2*mold_wall;

// Inverted-lens cavity cross-section (neck down -> base -> PCB rebate, open top).
module mold_cavity_2d() {
    union() {
        // neck (down at the cavity floor)
        translate([-lens_neck_w/2, mold_floor])
            square([lens_neck_w, lens_neck_h]);
        // base shoulder
        translate([-pcb_width/2, mold_floor + lens_neck_h])
            square([pcb_width, lens_base_h]);
        // PCB rebate, broken through the top surface for pour + insertion
        translate([-(pcb_width + fit_pcb)/2, mold_floor + lens_neck_h + lens_base_h])
            square([pcb_width + fit_pcb, pcb_th + eps]);
    }
}

module lens_mold() {
    color("Khaki")
    difference() {
        translate([-mold_W/2, 0, 0]) cube([mold_W, mold_H, mold_L]);
        // cavity, inset from each Z end by mold_wall to leave end walls
        translate([0, 0, mold_wall])
            linear_extrude(mold_L - 2*mold_wall) mold_cavity_2d();
        // pry slot at one end to release the cured part
        translate([0, mold_H - 1.0, -eps])
            rotate([-90, 0, 0]) cylinder(h = mold_wall + 2*eps, d = 6);
    }
}

// =============================================================================
// Outer body (3D-printed) — extruded channel, closed far end, magnet pockets.
// =============================================================================
module magnet_pockets() {
    z0 = body_len/2 - magnet_pitch*(magnet_count - 1)/2;
    for (i = [0 : magnet_count - 1]) {
        translate([0, -eps, z0 + i*magnet_pitch])
            rotate([-90, 0, 0])
                cylinder(h = pocket_depth + eps, d = pocket_d, $fn = 64);
    }
}

module outer_body() {
    color("DimGray")
    difference() {
        union() {
            // open channel section
            linear_extrude(channel_len) body_2d();
            // integrally-closed back wall (slide stop)
            translate([0, 0, channel_len])
                linear_extrude(back_wall) outer_2d();
        }
        magnet_pockets();
    }
}

// =============================================================================
// End cap (3D-printed) — flange + plug that retains the lens/PCB and closes the
// open end. Plug points in -Z so it mates at the body's +Z (open) end.
// =============================================================================
module cap_plug_2d() {
    // the channel cross-section, shrunk by clearance so it slides in
    offset(delta = -clr) channel_void_2d();
}

module end_cap() {
    color("SlateGray")
    difference() {
        union() {
            // flange (outside the body)
            translate([-W/2, 0, 0]) cube([W, H, cap_th]);
            // plug (reaches into the channel)
            translate([0, 0, -cap_plug_len])
                linear_extrude(cap_plug_len) cap_plug_2d();
        }
        // two fastener holes through the flange into the side-wall end-grain
        for (sx = [-1, 1])
            translate([sx*(W/2 - wall/2), H/2, -cap_plug_len - eps])
                cylinder(h = cap_th + cap_plug_len + 2*eps, d = screw_d, $fn = 32);
    }
}

// =============================================================================
// Assembly preview
// =============================================================================
module assembly() {
    outer_body();
    // lens + PCB inserted against the back wall
    translate([0, 0, back_wall]) { pcb(); lens(); }
    // end cap at the open end
    translate([0, 0, body_len]) end_cap();
    // magnets sitting in their pockets (reference)
    z0 = body_len/2 - magnet_pitch*(magnet_count - 1)/2;
    for (i = [0 : magnet_count - 1])
        color("Silver")
        translate([0, pocket_depth - magnet_th, z0 + i*magnet_pitch])
            rotate([-90, 0, 0])
                cylinder(h = magnet_th, d = magnet_d, $fn = 64);
}
