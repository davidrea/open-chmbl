# DE-08 CAN decode core, shared by transmitter/ and logger/ (see ../README.md).
#
# Deliberately has NO ESP-IDF dependencies: no REQUIRES, no PRIV_REQUIRES, and
# nothing in these sources includes an esp_*.h or sdkconfig.h. That is what lets
# the host harnesses in transmitter/software/test_host/ compile the very same
# files with plain gcc for the golden tests. Platform glue (TWAI bring-up, task
# plumbing) stays in each app's main/.
idf_component_register(SRCS "can_decode.c"
                            "bike_profile_triumph_tr.c"
                       INCLUDE_DIRS ".")
