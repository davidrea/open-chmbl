# Render the brake_light enclosure parts to STL with the OpenSCAD CLI.
#   make            # render all printable parts
#   make assembly   # render the preview assembly (not for printing)
#   make clean

OPENSCAD ?= openscad
SCAD := scad
STL  := stl

PARTS := inner_shell outer_shell

all: $(PARTS:%=$(STL)/%.stl)

$(STL)/%.stl: $(SCAD)/%.scad $(SCAD)/enclosure.scad | $(STL)
	$(OPENSCAD) -o $@ $<

assembly: $(STL)/assembly.stl
$(STL)/assembly.stl: $(SCAD)/assembly.scad $(SCAD)/enclosure.scad | $(STL)
	$(OPENSCAD) -o $@ $<

pcb: $(STL)/pcb.stl
$(STL)/pcb.stl: $(SCAD)/pcb.scad $(SCAD)/enclosure.scad | $(STL)
	$(OPENSCAD) -o $@ $<

$(STL):
	mkdir -p $(STL)

clean:
	rm -rf $(STL)

.PHONY: all assembly pcb clean
