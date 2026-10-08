# DE-09 braking state machine, shared by logger/ (on-board preview) and, when
# DE-09 lands there, transmitter/. See ../README.md.
#
# No ESP-IDF dependencies on purpose — the same brake_fsm.c is compiled by
# transmitter/software/test_host/ with plain gcc for the replay test against the
# Python reference. The Kconfig symbols in ./Kconfig are read by the APP's glue
# (which already includes sdkconfig.h), never from brake_fsm.c.
idf_component_register(SRCS "brake_fsm.c"
                       INCLUDE_DIRS ".")
