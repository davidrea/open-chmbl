// =============================================================================
// Open-CHMBL — brake_light enclosure (parametric, lenticular CLAMSHELL)
// =============================================================================
// Two-piece clamshell shaped as a LENTICULAR POD: two spherical caps bulging
// from a FLAT parting plane, tapering to a thin rim — doubly curved (length AND
// width), matching the uploaded sketch. The parting plane is flat because the
// shells clamshell onto either side of a flat, rigid PCB.
//
//   - PCB          flat, rigid board (USB-C / module / LED bar). Its LED face
//                  sits at the flat parting plane (y = 0).
//   - INNER SHELL  (helmet side): solid spherical cap above the parting; holds
//                  the PCB; recessed magnet pockets in the convex helmet face.
//   - OUTER SHELL  (lens side): spherical-cap perimeter frame below the parting
//                  with a window exposing the captured silicone LENS. Doubles
//                  as the lens pour mould.
//
// Build order: model the flat PCB / parting plane, then the two caps around it.
//
// Axis convention:
//     X = length, Z = width  (the parting-plane footprint)
//     Y = height (+Y = helmet side; -Y = lens side); parting plane at y = 0.
//
// NOTE on conformance: the helmet face is CONVEX here (lenticular, per the
// sketch) and is held to the helmet/pack by the magnets. For a CONCAVE cupping
// fit to a specific helmet radius, the helmet cap would invert — left as a
// parameter direction (see ../README.md §5).
//
// Magnets are RECESSED into the helmet face — the plan of record from
// docs/design/explorations/mounting-magnetic.md (see ../README.md §4).
// All dimensions are millimetres. The PCB block is a PLACEHOLDER.
// =============================================================================

// ---- PCB (PLACEHOLDER — replace with the real board outline) -----------------
pcb_len    = 70;    // X
pcb_width  = 30;    // Z
pcb_th     = 1.6;   // Y (flat board)

// ---- Footprint (the flat parting-plane outline) ------------------------------
end_margin  = 6;    // shell beyond the PCB at each X end
side_margin = 4;    // shell beyond the PCB at each Z side
corner_r    = 6;    // footprint corner radius

// ---- Curvature (bulge heights at the centre) ---------------------------------
helmet_min_bulge = 8.0;   // desired min dome height on the helmet side
lens_bulge       = 5.0;   // dome height on the lens side (≈ lens thickness)

// ---- Shell stack -------------------------------------------------------------
under_h     = 3.5;  // clearance between PCB top and the helmet cap (components)
frame_w     = 3.0;  // outer-shell frame width around the lens window
inner_min_wall = 2.4;

// ---- Fit / clearances --------------------------------------------------------
clr        = 0.4;
ledge      = 1.5;   // PCB-retention ledge per side

// ---- Magnets (starting candidate from mounting-magnetic.md: N42 20x4 disc) ----
magnet_d        = 20;
magnet_th       = 4;
vhb_th          = 0.8;
magnet_recess   = 1.2;
magnet_back_min = 1.6;
magnet_count    = 2;
magnet_pitch_mm = 40;

// ---- Clamshell fasteners (vertical, clamp the flat parting) -------------------
screw_d      = 2.5;
screw_inset  = 10.0;  // X inset from each end

// ---- USB-C port (cut in one end) ---------------------------------------------
usb_w   = 9.5;
usb_h   = 3.6;
usb_end = 1;          // +1 = +X end, -1 = -X end

// ---- Render quality ----------------------------------------------------------
$fa = 2;
$fs = 0.5;
eps = 0.05;
sph_fn = 220;         // facets for the cap spheres

// ---- Derived -----------------------------------------------------------------
pocket_depth = magnet_recess + magnet_th + vhb_th;
pocket_d     = magnet_d + clr;

Lf = pcb_len   + 2*end_margin;     // footprint length
Wf = pcb_width + 2*side_margin;    // footprint width
a  = sqrt(pow(Lf/2, 2) + pow(Wf/2, 2));   // footprint half-diagonal (cap meets rim here)

// helmet bulge must bury a magnet above the PCB + component clearance
helmet_bulge = max(helmet_min_bulge,
                   pocket_depth + pcb_th + under_h + magnet_back_min);

// spherical cap through apex (0, h, 0) and rim (a, 0, 0)
cy_in  = (pow(helmet_bulge, 2) - pow(a, 2)) / (2*helmet_bulge);   // centre below parting
R_in   = sqrt(pow(a, 2) + pow(cy_in, 2));
cy_out = (pow(a, 2) - pow(lens_bulge, 2)) / (2*lens_bulge);       // centre above parting
R_out  = sqrt(pow(a, 2) + pow(cy_out, 2));

function mag_x(i)   = (i - (magnet_count - 1)/2) * magnet_pitch_mm;
function dome_y(x)  = cy_in + sqrt(pow(R_in, 2) - x*x);   // helmet surface height at (x,0)

// =============================================================================
// Primitives
// =============================================================================
module rrect(l, w, r) {
    hull() for (sx = [-1, 1], sy = [-1, 1])
        translate([sx*(l/2 - r), sy*(w/2 - r)]) circle(r);
}

// rounded-rect prism extruded along Y (footprint in X-Z)
module foot_prism(l, w, r, H) {
    rotate([90, 0, 0]) translate([0, 0, -H/2]) linear_extrude(H) rrect(l, w, r);
}

module above0() { translate([-300, 0,    -300]) cube(600); }   // y >= 0
module below0() { translate([-300, -600, -300]) cube(600); }   // y <= 0

// helmet-side spherical cap solid (above the parting, within the footprint)
module helmet_dome() {
    intersection() {
        translate([0, cy_in, 0]) sphere(R_in, $fn = sph_fn);
        foot_prism(Lf, Wf, corner_r, 400);
        above0();
    }
}
// lens-side spherical cap solid (below the parting, within the footprint)
module lens_dome() {
    intersection() {
        translate([0, cy_out, 0]) sphere(R_out, $fn = sph_fn);
        foot_prism(Lf, Wf, corner_r, 400);
        below0();
    }
}

// vertical (Y) cylinder, base at y0
module ycyl(x, y0, z, h, dia, fn = 64) {
    translate([x, y0, z]) rotate([-90, 0, 0]) cylinder(h = h, d = dia, $fn = fn);
}
module mag_pocket(i) { ys = dome_y(mag_x(i)); ycyl(mag_x(i), ys - pocket_depth, 0, pocket_depth + 8, pocket_d); }
module mag_disc(i)   { ys = dome_y(mag_x(i)); ycyl(mag_x(i), ys - magnet_recess - magnet_th, 0, magnet_th, magnet_d); }

// =============================================================================
// PCB (flat, rigid) — modelled with representative parts; not printed
// =============================================================================
module pcb() {
    color("ForestGreen")
        translate([-pcb_len/2, 0, -pcb_width/2]) cube([pcb_len, pcb_th, pcb_width]);
    color("DimGray")     // ESP32 module (top / helmet side, away from USB)
        translate([-usb_end*(pcb_len/2 - 14), pcb_th, -7]) cube([18, 3, 14]);
    color("Silver")      // USB-C receptacle (at the usb_end, straddling the edge)
        translate([usb_end*(pcb_len/2 + 1), pcb_th/2, 0]) cube([8, 3.2, 9], center = true);
    color("Red")         // LED bar (bottom / lens side)
        translate([-(pcb_len - 10)/2, -0.8, -2.5]) cube([pcb_len - 10, 0.8, 5]);
}

// =============================================================================
// Lens (silicone) — fills the lens cap inside the frame
// =============================================================================
module lens() {
    color("LightCyan", 0.45)
    intersection() {
        lens_dome();
        foot_prism(Lf - 2*frame_w, Wf - 2*frame_w, max(0.5, corner_r - frame_w), 400);
    }
}

// =============================================================================
// Inner shell (helmet side) — printed
// =============================================================================
module pcb_cavity() {
    translate([-(pcb_len + 2*clr)/2, -eps, -(pcb_width + 2*clr)/2])
        cube([pcb_len + 2*clr, pcb_th + eps, pcb_width + 2*clr]);
}
module comp_clearance() {
    translate([-(pcb_len - 2*ledge)/2, pcb_th, -(pcb_width - 2*ledge)/2])
        cube([pcb_len - 2*ledge, under_h, pcb_width - 2*ledge]);
}

module inner_shell() {
    color("DimGray")
    difference() {
        helmet_dome();
        pcb_cavity();
        comp_clearance();
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
        lens_dome();
        // remove the central lens region, leaving a perimeter frame
        foot_prism(Lf - 2*frame_w, Wf - 2*frame_w, max(0.5, corner_r - frame_w), 400);
        clamshell_screws();
        usb_cutout();
    }
}

// =============================================================================
// Shared cut features
// =============================================================================
module clamshell_screws() {
    for (sx = [-1, 1], sz = [-1, 1])
        translate([sx*(Lf/2 - screw_inset), 0, sz*(Wf/2 - frame_w/2)])
            rotate([90, 0, 0]) cylinder(h = 80, d = screw_d, center = true, $fn = 32);
}

module usb_cutout() {
    translate([usb_end * (Lf/2), pcb_th/2, 0])
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
