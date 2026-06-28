// =============================================================================
// Open-CHMBL — brake_light enclosure (parametric, conforming clamshell)
// =============================================================================
// Two-piece clamshell. The INNER (helmet) shell has a CONCAVE dished face: the
// convex rear of the helmet nests into it (the helmet installs from the +Y
// side). The inner shell is THINNEST at the centre — just enough to cover the
// PCB — and thickens toward the ends, where the MAGNETS live (outboard, clear
// of the thin centre). The OUTER (lens) shell is a convex cap. The parting
// plane between the shells is FLAT, clamping the flat, rigid PCB.
//
//   helmet (convex) ─┐  installs from +Y
//                    ▼
//        ╲___________________________╱      ◀ INNER shell: concave cup (thin centre)
//        █ magnet █   ░░ PCB ░░   █ magnet █
//        ├───────── flat parting plane ──────┤
//        ╲___   overmolded lens (captured) ___╱   ◀ OUTER shell: convex, emits -Y
//
// Build order: model the flat PCB / parting plane, then the cup + lens caps.
//
// Axis convention:
//     X = length, Z = width (the parting-plane footprint)
//     Y = height (+Y = helmet side; -Y = lens side); parting plane at y = 0.
//
// Magnets are RECESSED into the helmet face — the plan of record from
// docs/design/explorations/mounting-magnetic.md (see ../README.md §4).
// All dimensions are millimetres. The PCB block is a PLACEHOLDER.
// =============================================================================

// ---- PCB (PLACEHOLDER — replace with the real board outline) -----------------
pcb_len    = 70;    // X
pcb_width  = 30;    // Z
pcb_th     = 1.6;   // Y (flat board)

// ---- Helmet conformance (concave cup) ----------------------------------------
conform_R       = 150;  // helmet rear radius the concave cup mates (~140-160)
helmet_min_wall = 1.5;  // inner-shell wall over the PCB at the thin centre
under_h         = 3.5;  // component clearance between PCB top and the cup

// ---- Lens side (convex cap) --------------------------------------------------
lens_bulge = 5.0;   // lens cap height at the centre (≈ lens thickness)
frame_w    = 3.0;   // outer-shell frame width around the lens window

// ---- Footprint ---------------------------------------------------------------
side_margin = 4;    // shell beyond the PCB at each Z side
corner_r    = 8;    // footprint corner radius
edge_wall   = 3;    // material beyond a magnet at the X ends

// ---- Fit / clearances --------------------------------------------------------
clr   = 0.4;
ledge = 1.5;        // PCB-retention ledge per side

// ---- Magnets (starting candidate from mounting-magnetic.md: N42 20x4 disc) ----
magnet_d        = 20;
magnet_th       = 4;
vhb_th          = 0.8;
magnet_recess   = 1.2;
magnet_count    = 2;        // one near each X end
// magnets sit OUTBOARD, just inside the PCB ends, in the thick part of the cup.
// The PCB carries a keep-out (notch) at each magnet so the pocket reaches depth.
magnet_x        = pcb_len/2 - 2;

// ---- Clamshell fasteners (vertical, clamp the flat parting) -------------------
screw_d     = 2.5;
screw_inset = 8.0;  // X inset from each end

// ---- USB-C port (exits a long SIDE near one end — see README) -----------------
usb_w   = 9.5;
usb_h   = 3.6;
usb_end = -1;       // which X end the USB sits near
usb_side = 1;       // which Z side it exits (+1 / -1)

// ---- Render quality ----------------------------------------------------------
$fa = 2;
$fs = 0.5;
eps = 0.05;
sph_fn = 200;

// ---- Derived -----------------------------------------------------------------
pocket_depth   = magnet_recess + magnet_th + vhb_th;
pocket_d       = magnet_d + clr;
keepout_d      = pocket_d + 3;     // solid column / PCB notch around a magnet

helmet_center_th = pcb_th + under_h + helmet_min_wall;   // thin-centre inner thickness
cy_h = conform_R + helmet_center_th;                     // cup-sphere centre (above)

// footprint length: contain the outboard magnets + an edge wall
end_margin = max(edge_wall, (magnet_x + magnet_d/2 + edge_wall) - pcb_len/2);
Lf = pcb_len   + 2*end_margin;
Wf = pcb_width + 2*side_margin;
a  = sqrt(pow(Lf/2, 2) + pow(Wf/2, 2));   // footprint half-diagonal

// lens convex cap through apex (0,-lens_bulge,0) and rim (a,0,0)
cy_out = (pow(a, 2) - pow(lens_bulge, 2)) / (2*lens_bulge);
R_out  = sqrt(pow(a, 2) + pow(cy_out, 2));

lens_win_len = pcb_len - 4;          // lens window length (central, over the LEDs)
ap_w         = pcb_width - 2*frame_w;

function mag_x(i)  = (magnet_count == 1) ? 0 : (i == 0 ? -magnet_x : magnet_x);
function hsurf(x)  = cy_h - sqrt(pow(conform_R, 2) - x*x);   // cup height at (x,0)

// =============================================================================
// Primitives
// =============================================================================
module rrect(l, w, r) {
    hull() for (sx = [-1, 1], sy = [-1, 1])
        translate([sx*(l/2 - r), sy*(w/2 - r)]) circle(r);
}
module foot_prism(l, w, r, H) {           // rounded-rect prism along Y
    rotate([90, 0, 0]) translate([0, 0, -H/2]) linear_extrude(H) rrect(l, w, r);
}
module above0() { translate([-400, 0,    -400]) cube(800); }   // y >= 0
module below0() { translate([-400, -800, -400]) cube(800); }   // y <= 0
module ycyl(x, y0, z, h, dia, fn = 64) {
    translate([x, y0, z]) rotate([-90, 0, 0]) cylinder(h = h, d = dia, $fn = fn);
}

// magnet features (vertical, recessed into the concave cup)
module mag_pocket(i) { ys = hsurf(mag_x(i)); ycyl(mag_x(i), ys - pocket_depth, 0, pocket_depth + 8, pocket_d); }
module mag_column(i) { ycyl(mag_x(i), -eps, 0, 60, keepout_d); }   // solid keep-out + PCB notch
module mag_disc(i)   { ys = hsurf(mag_x(i)); ycyl(mag_x(i), ys - magnet_recess - magnet_th, 0, magnet_th, magnet_d); }

// =============================================================================
// PCB (flat, rigid) — with magnet keep-out notches; not printed
// =============================================================================
module pcb() {
    difference() {
        union() {
            color("ForestGreen")
                translate([-pcb_len/2, 0, -pcb_width/2]) cube([pcb_len, pcb_th, pcb_width]);
            color("DimGray")   // ESP32 module (top, central-ish)
                translate([-8, pcb_th, -7]) cube([18, 3, 14]);
            color("Silver")    // USB-C receptacle (exits a long side)
                translate([usb_end*(pcb_len/2 - 9), pcb_th/2, usb_side*(pcb_width/2 - 1)])
                    cube([9, 3.2, 6], center = true);
            color("Red")       // LED bar (bottom / lens side)
                translate([-(lens_win_len - 6)/2, -0.8, -2.5]) cube([lens_win_len - 6, 0.8, 5]);
        }
        for (i = [0 : magnet_count - 1]) mag_column(i);   // magnet keep-out notches
    }
}

// =============================================================================
// Outer (lens) shell + lens
// =============================================================================
module lens_dome() {
    intersection() {
        translate([0, cy_out, 0]) sphere(R_out, $fn = sph_fn);
        foot_prism(Lf, Wf, corner_r, 400);
        below0();
    }
}
module lens_window() { foot_prism(lens_win_len, ap_w, max(0.5, corner_r - frame_w), 400); }

module lens() {
    color("LightCyan", 0.45) intersection() { lens_dome(); lens_window(); }
}
module outer_shell() {
    color("SlateGray")
    difference() {
        lens_dome();
        lens_window();
        clamshell_screws();
        usb_cutout();
    }
}

// =============================================================================
// Inner (helmet) shell — concave cup, thin centre, magnets outboard
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
        // footprint slab above the parting, with the concave helmet cup carved
        difference() {
            intersection() { foot_prism(Lf, Wf, corner_r, 200); above0(); }
            translate([0, cy_h, 0]) sphere(conform_R, $fn = sph_fn);
        }
        // PCB cavity + component clearance, but keep solid columns at the magnets
        difference() {
            union() { pcb_cavity(); comp_clearance(); }
            for (i = [0 : magnet_count - 1]) mag_column(i);
        }
        for (i = [0 : magnet_count - 1]) mag_pocket(i);
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
    translate([usb_end*(pcb_len/2 - 9), pcb_th/2, usb_side*(Wf/2)])
        cube([usb_w, usb_h, 16], center = true);
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
