// Reference target: the flat, rigid PCB (board + USB-C + module + LED bar).
// Not printed — it defines the flat parting plane the shells are built around.
//   openscad -o ../stl/pcb.stl pcb.scad
include <enclosure.scad>
pcb();
