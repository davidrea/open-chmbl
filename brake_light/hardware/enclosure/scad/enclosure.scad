// =============================================================================
// Open-CHMBL — brake_light enclosure (parametric, conformal CLAMSHELL)
// =============================================================================
// Two-piece clamshell. The OUTER WALLS (helmet face + lens face) are curved to
// conform to a helmet (or a pack/jacket panel); the PARTING PLANE between the
// shells is FLAT, because the shells clamshell onto either side of a flat, rigid
// PCB. See ../README.md for the strategy and the sketch.
//
//   - PCB          a flat, rigid board (modelled with USB-C / module / LEDs).
//   - INNER SHELL  (helmet side): curved helmet face, FLAT inner (parting) face;
//                  holds the PCB; recessed magnet pockets in the helmet face.
//   - OUTER SHELL  (lens side): FLAT parting face, curved lens face with a window
//                  that exposes the captured silicone LENS. Doubles as the lens
//                  pour mould.
//   - The shells capture the PCB + lens at the flat parting plane and bolt
//                  together (vertical fasteners). USB-C exits one end.
//
// We build the flat PCB first, then construct each shell as a curved outer band
// CLIPPED to the flat parting plane.
//
// Axis convention (curvature is a single-axis bend in the X-Y plane):
//     X = length   (the long axis; outer walls curve along it)
//     Y = height   (+Y toward the curvature/helmet centre; device hangs at -Y)
//     Z = width    (straight)
// The flat parting plane is horizontal at y = y_part.
//
// Magnets are RECESSED into the helmet face — the plan of record from
// docs/design/explorations/mounting-magnetic.md (see ../README.md §4).
// All dimensions are millimetres. The PCB block is a PLACEHOLDER.
// =============================================================================

PI = 3.14159265358979;

// ---- PCB (PLACEHOLDER — replace with the real board outline) -----------------
pcb_len    = 70;    // X
pcb_width  = 30;    // Z
pcb_th     = 1.6;   // Y (flat board)

// ---- Conformance (outer-wall curvature) --------------------------------------
conform_R  = 150;   // helmet-face radius. Helmet ~140-160; pack/jacket ~300+
dev_margin = 6;     // shell length added beyond the PCB at each end

// ---- Shell / lens stack (thicknesses, measured at the centre) ----------------
inner_min_wall = 2.4;  // helmet-face wall where there is no magnet
under_h        = 3.5;  // clearance between PCB top and inner shell (components)
lens_th        = 4.0;  // silicone lens between PCB LED face and outer surface
frame_w        = 3.0;  // outer-shell perimeter frame width around the lens window

// ---- Fit / clearances --------------------------------------------------------
clr        = 0.4;
ledge      = 1.5;   // PCB-retention ledge width per side

// ---- Magnets (starting candidate from mounting-magnetic.md: N42 20x4 disc) ----
magnet_d        = 20;
magnet_th       = 4;
vhb_th          = 0.8;
magnet_recess   = 1.2;
magnet_back_min = 1.6;
magnet_count    = 2;
magnet_pitch_mm = 40;    // centre-to-centre along X

// ---- Clamshell fasteners (vertical, clamp the flat parting) -------------------
screw_d      = 2.5;  // M2.5 / self-tap
screw_inset  = 3.0;  // inset from each PCB end

// ---- USB-C port (cut in one end) ---------------------------------------------
usb_w   = 9.5;
usb_h   = 3.6;
usb_end = 1;         // +1 = +X end, -1 = -X end

// ---- Render quality ----------------------------------------------------------
$fa = 1.5;
$fs = 0.4;
eps = 0.05;
arc_fn = 240;        // facets for the large curvature circles
BIGC   = 600;        // half-space clip cube size

// ---- Derived -----------------------------------------------------------------
pocket_depth   = magnet_recess + magnet_th + vhb_th;
pocket_d       = magnet_d + clr;
// inner shell must be thick enough to bury a magnet + a backing wall
inner_shell_th = max(inner_min_wall, pocket_depth + magnet_back_min);

R0     = conform_R;                                  // helmet face radius (top)
// depth from the curvature centre down to the flat parting plane (centre stack)
Rpart  = R0 + inner_shell_th + under_h + pcb_th;
Rout   = Rpart + lens_th;                            // lens face radius (bottom)
y_part = -Rpart;                                     // the flat parting plane

dev_w  = pcb_width + 2*frame_w;                      // full footprint width
ap_w   = pcb_width - 2*frame_w;                      // lens window width

function ang(s, r) = (s/2) / r * 180 / PI;
dev_len = pcb_len + 2*dev_margin;
ha_dev  = ang(dev_len, Rpart);
ha_lens = ang(pcb_len, Rpart);

function mag_x(i) = (i - (magnet_count - 1)/2) * magnet_pitch_mm;

// =============================================================================
// Primitives
// =============================================================================

// 2D radial sector between r_lo and r_hi, spanning +/- half_deg about -Y.
// The wedge mask has radial edges and a FLAT base at y = -(r_hi+2), which lies
// below the whole arc (using tan, not cos) so it never clips the sector bottom.
module arc_sector_2d(r_lo, r_hi, half_deg) {
    b = r_hi + 2;
    intersection() {
        difference() { circle(r_hi, $fn = arc_fn); circle(r_lo, $fn = arc_fn); }
        polygon([[0, 0],
                 [ b*tan(half_deg), -b],
                 [-b*tan(half_deg), -b]]);
    }
}

// 3D curved band: the sector given width w (centred in Z).
module band(r_lo, r_hi, half_deg, w) {
    translate([0, 0, -w/2]) linear_extrude(w) arc_sector_2d(r_lo, r_hi, half_deg);
}

// half-spaces relative to the flat parting plane
module above_part() { translate([-BIGC/2, y_part,        -BIGC/2]) cube(BIGC); }
module below_part() { translate([-BIGC/2, y_part - BIGC,  -BIGC/2]) cube(BIGC); }

// vertical (Y-axis) cylinder helper, base at y0, height h
module ycyl(x, y0, z, h, dia, fn = 64) {
    translate([x, y0, z]) rotate([-90, 0, 0]) cylinder(h = h, d = dia, $fn = fn);
}

// magnet features (vertical: pocket drilled from the helmet face into a boss)
module mag_pocket(i) { ycyl(mag_x(i), -R0 - pocket_depth, 0, pocket_depth + 12, pocket_d); }
module mag_disc(i)   { ycyl(mag_x(i), -R0 - magnet_recess - magnet_th, 0, magnet_th, magnet_d); }

// =============================================================================
// PCB (flat, rigid) — modelled with representative parts; not printed
// =============================================================================
module pcb() {
    // board
    color("ForestGreen")
        translate([-pcb_len/2, y_part, -pcb_width/2]) cube([pcb_len, pcb_th, pcb_width]);
    // ESP32 module (top / helmet side, near the non-USB end)
    color("DimGray")
        translate([-usb_end*(pcb_len/2 - 14), y_part + pcb_th, -7]) cube([18, 3, 14]);
    // USB-C receptacle (at the usb_end, straddling the board edge)
    color("Silver")
        translate([usb_end*(pcb_len/2 + 1), y_part + pcb_th/2, 0]) cube([8, 3.2, 9], center = true);
    // LED bar (bottom / lens side, runs the length)
    color("Red")
        translate([-(pcb_len - 10)/2, y_part - 0.8, -2.5]) cube([pcb_len - 10, 0.8, 5]);
}

// =============================================================================
// Lens (silicone) — flat top at the parting, curved bottom at the lens face
// =============================================================================
module lens() {
    color("LightCyan", 0.45)
    intersection() { band(R0, Rout, ha_lens, pcb_width); below_part(); }
}

// =============================================================================
// Inner shell (helmet side) — printed
// =============================================================================
// component clearance: a shallow pocket above the PCB (height = under_h), narrower
// than the board so the board is retained on ledges. Stops below the solid helmet
// wall that houses the magnets, so no magnet/clearance conflict.
module comp_clearance() {
    translate([-(pcb_len - 2*ledge)/2, y_part + pcb_th, -(pcb_width - 2*ledge)/2])
        cube([pcb_len - 2*ledge, under_h, pcb_width - 2*ledge]);
}
module pcb_cavity() {
    translate([-(pcb_len + 2*clr)/2, y_part - eps, -(pcb_width + 2*clr)/2])
        cube([pcb_len + 2*clr, pcb_th + 2*eps, pcb_width + 2*clr]);
}

module inner_shell() {
    color("DimGray")
    difference() {
        intersection() { band(R0, Rout + 30, ha_dev, dev_w); above_part(); }
        comp_clearance();
        pcb_cavity();
        for (i = [0 : magnet_count - 1]) mag_pocket(i);
        clamshell_screws();
        usb_cutout();
    }
}

// =============================================================================
// Outer shell (lens side) — printed; doubles as the lens pour mould
// =============================================================================
module outer_shell() {
    color("SlateGray")
    difference() {
        intersection() { band(R0, Rout, ha_dev, dev_w); below_part(); }
        band(R0, Rout + eps, ha_lens, ap_w);   // lens window (leaves a frame)
        clamshell_screws();
        usb_cutout();
    }
}

// =============================================================================
// Shared cut features
// =============================================================================
module clamshell_screws() {
    for (sx = [-1, 1], sz = [-1, 1])
        translate([sx*(pcb_len/2 - screw_inset), y_part, sz*(pcb_width/2 + frame_w/2)])
            rotate([90, 0, 0]) cylinder(h = 80, d = screw_d, center = true, $fn = 32);
}

module usb_cutout() {
    x_end = Rpart * sin(ha_dev);
    translate([usb_end * x_end, y_part + pcb_th/2, 0])
        cube([16, usb_h, usb_w], center = true);
}

// =============================================================================
// Assembly preview
// =============================================================================
module assembly() {
    inner_shell();
    pcb();
    lens();
    outer_shell();
    for (i = [0 : magnet_count - 1]) color("Silver") mag_disc(i);
}
