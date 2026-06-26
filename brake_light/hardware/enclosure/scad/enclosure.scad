// =============================================================================
// Open-CHMBL — brake_light enclosure (parametric, conformal CLAMSHELL)
// =============================================================================
// Two-piece clamshell, gently curved along its length to conform to a helmet
// (or a pack/jacket panel). See ../README.md for the strategy and the sketch.
//
//   - INNER SHELL  (helmet-facing): curved tray that holds the PCB; recessed
//                  magnet pockets in its outer (helmet) face.
//   - OUTER SHELL  (lens side): perimeter frame that captures the overmolded
//                  silicone LENS and shows it through a window. Doubles as the
//                  pour mould for the lens (pour, set the PCB, cure).
//   - PCB + LENS   are captured at the parting line when the shells close.
//
// Radial stack, from the helmet outward (all on a common curvature centre):
//     R0   helmet face (inner-shell outer surface)
//     R1   = R0 + inner_shell_th
//     ...  under_h component clearance
//     PCB  (pcb_th)
//     Rpart= parting line (LED face) — clamshell splits here
//     LENS (lens_th)
//     Rout = lens / outer surface
//
// Axis convention (curvature is in the X-Y plane; bend axis = Z = width):
//     X = length   (the long, curved axis)
//     Y = height   (radial; +Y is toward the helmet centre, device hangs at -Y)
//     Z = width    (straight; not curved in this v1 — single-axis bend)
//
// Magnets are RECESSED into the helmet face — the plan of record from
// docs/design/explorations/mounting-magnetic.md (see ../README.md §4).
// All dimensions are millimetres. The PCB block is a PLACEHOLDER.
// =============================================================================

PI = 3.14159265358979;

// ---- PCB (PLACEHOLDER — replace with the real board outline) -----------------
pcb_len    = 70;    // along the curved length (arc length)
pcb_width  = 30;    // Z
pcb_th     = 1.6;   // radial

// ---- Conformance -------------------------------------------------------------
conform_R  = 150;   // inner (helmet) face radius. Helmet ~140-160; pack/jacket ~300+
dev_margin = 6;     // shell length added beyond the PCB at each end (for closure)

// ---- Shell / lens stack (radial thicknesses) ---------------------------------
inner_min_wall = 2.4;  // helmet-face wall where there is no magnet
under_h        = 2.0;   // clearance between inner shell and PCB top (components)
lens_th        = 4.0;   // silicone lens between PCB LED face and outer surface
frame_w        = 3.0;   // outer-shell perimeter frame width around the lens window

// ---- Fit / clearances --------------------------------------------------------
clr        = 0.4;   // general clearance
ledge      = 1.5;   // PCB-support ledge width per side (Z)

// ---- Magnets (starting candidate from mounting-magnetic.md: N42 20x4 disc) ----
magnet_d        = 20;
magnet_th       = 4;
vhb_th          = 0.8;
magnet_recess   = 1.2;
magnet_back_min = 1.6;   // material left between pocket bottom and PCB cavity
magnet_count    = 2;
magnet_pitch_mm = 40;    // centre-to-centre arc length along the length

// ---- Clamshell fasteners -----------------------------------------------------
screw_d      = 2.5;  // radial fastener through both shells (M2.5 / self-tap)
screw_inset  = 3.0;  // arc-length inset from each end for the fastener

// ---- USB-C port (cut in one end wall) ----------------------------------------
usb_w   = 9.5;
usb_h   = 3.6;
usb_end = 1;         // +1 = +X end, -1 = -X end

// ---- Render quality ----------------------------------------------------------
$fa = 1.5;
$fs = 0.4;
eps = 0.05;
arc_fn = 240;        // facets for the large curvature circles

// ---- Derived radii -----------------------------------------------------------
pocket_depth   = magnet_recess + magnet_th + vhb_th;
pocket_d       = magnet_d + clr;
// inner shell must be thick enough to bury a magnet + leave a backing wall
inner_shell_th = max(inner_min_wall, pocket_depth + magnet_back_min);

R0     = conform_R;                       // helmet face
R1     = R0 + inner_shell_th;             // inner-shell inner face
Rpcb_lo= R1 + under_h;                    // PCB inner (component) face
Rpart  = Rpcb_lo + pcb_th;                // parting line = PCB LED face
Rout   = Rpart + lens_th;                 // lens / outer surface

// ---- Derived widths / lengths ------------------------------------------------
cav_w   = pcb_width + 2*clr;              // PCB cavity width
ledge_w = cav_w - 2*ledge;               // narrower clearance below PCB -> ledges
ap_w    = pcb_width - 2*frame_w;          // lens window width (Z)

// half-angle (deg) for an arc length s measured at radius r
function ang(s, r) = (s/2) / r * 180 / PI;

dev_len    = pcb_len + 2*dev_margin;
ha_dev     = ang(dev_len, Rpart);         // whole-shell half-angle
ha_pcb     = ang(pcb_len, Rpcb_lo);       // PCB cavity half-angle
ha_lens    = ang(pcb_len, Rpart);         // lens window half-angle (~ PCB length)

// =============================================================================
// Primitives — curved (annular-sector) bands in the X-Y plane, extruded in Z
// =============================================================================

// 2D radial sector between r_lo and r_hi, spanning +/- half_deg about -Y.
module arc_sector_2d(r_lo, r_hi, half_deg) {
    intersection() {
        difference() { circle(r_hi, $fn = arc_fn); circle(r_lo, $fn = arc_fn); }
        polygon([[0, 0],
                 [ (r_hi + 2)*sin(half_deg), -(r_hi + 2)*cos(half_deg)],
                 [-(r_hi + 2)*sin(half_deg), -(r_hi + 2)*cos(half_deg)]]);
    }
}

// 3D curved band: the sector given width w (centred in Z).
module band(r_lo, r_hi, half_deg, w) {
    translate([0, 0, -w/2]) linear_extrude(w) arc_sector_2d(r_lo, r_hi, half_deg);
}

// A radial cylinder (axis points from the curvature centre outward), located on
// the arc at arc-length s from centre, reaching radii [r_lo, r_hi].
module radial_cyl(s, dia, r_lo, r_hi, fn = 64) {
    th = s / Rpart * 180 / PI;            // angle for this arc position
    rotate([0, 0, th])
        translate([0, -r_lo, 0])
            rotate([90, 0, 0])            // +Z cylinder -> points -Y (outward)
                cylinder(h = r_hi - r_lo, d = dia, $fn = fn);
}

// magnet arc positions (centre-to-centre along the length)
function magnet_s(i) = (i - (magnet_count - 1)/2) * magnet_pitch_mm;

// =============================================================================
// PCB + Lens (reference / assembly; not printed)
// =============================================================================
module pcb() {
    color("ForestGreen") band(Rpcb_lo, Rpart, ha_pcb, pcb_width);
}

module lens() {
    color("LightCyan", 0.45) band(Rpart, Rout, ha_lens, pcb_width);
}

// =============================================================================
// Inner shell (helmet side) — printed
// =============================================================================
module inner_shell() {
    color("DimGray")
    difference() {
        band(R0, Rpart, ha_dev, pcb_width + 2*frame_w);   // solid tray (full body)
        // PCB cavity (slightly proud of parting so the PCB face is captured)
        band(Rpcb_lo, Rpart + eps, ha_pcb, cav_w);
        // component clearance below the PCB, narrower -> leaves support ledges
        band(R1, Rpcb_lo, ha_pcb, ledge_w);
        // recessed magnet pockets in the helmet face
        for (i = [0 : magnet_count - 1])
            radial_cyl(magnet_s(i), pocket_d, R0 - eps, R0 + pocket_depth);
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
        band(Rpart, Rout, ha_dev, pcb_width + 2*frame_w); // perimeter frame body
        // lens window (leaves a frame of frame_w around it)
        band(Rpart - eps, Rout + eps, ha_lens, ap_w);
        clamshell_screws();
        usb_cutout();
    }
}

// =============================================================================
// Shared cut features
// =============================================================================
module clamshell_screws() {
    s_end = dev_len/2 - screw_inset;
    for (sgn = [-1, 1])
        radial_cyl(sgn * s_end, screw_d, R0 - 1, Rout + 1, 32);
}

// USB-C slot through the end wall, straddling the parting line.
module usb_cutout() {
    th = usb_end * ha_dev;
    rotate([0, 0, th])
        translate([0, -(Rpart), 0])
            rotate([0, 90, 0])
                translate([0, 0, -3])
                    // slot centred on the parting radius, cut through the end wall
                    linear_extrude(6)
                        offset(1) square([usb_h, usb_w], center = true);
}

// =============================================================================
// Assembly preview
// =============================================================================
module assembly() {
    inner_shell();
    pcb();
    lens();
    outer_shell();
    // magnets seated (recessed) in their pockets
    for (i = [0 : magnet_count - 1])
        color("Silver")
        radial_cyl(magnet_s(i), magnet_d, R0 + magnet_recess, R0 + magnet_recess + magnet_th);
}
