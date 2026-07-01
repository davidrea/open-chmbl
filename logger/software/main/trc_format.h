/*
 * PCAN trace file (.trc) formatting — version 2.1.
 *
 * Pure and platform-independent (no ESP-IDF headers) so it can be host
 * unit-tested, per the repo's "keep the pure parts testable" convention
 * (docs/firmware.md §4). The output is byte-for-byte compatible with the
 * python-can TRCReader/TRCWriter v2.1 dialect, which is the project's offline
 * decode/replay path (docs/can-profiles.md §5).
 *
 * Layout (`;$COLUMNS=N,O,T,B,I,d,R,L,D`):
 *   N  message number (1-based)
 *   O  time offset from the first frame, in milliseconds (3 decimals)
 *   T  type: "DT" data frame / "RR" remote frame
 *   B  bus/channel (always 1 here)
 *   I  identifier, hex — standard = up to 3 digits, extended = 8 digits
 *      (the reader treats an ID string longer than 4 chars as extended)
 *   d  direction: always "Rx" (listen-only logger)
 *   R  reserved marker "-"
 *   L  data length code
 *   D  data bytes, space-separated hex (omitted for remote frames)
 */
#ifndef TRC_FORMAT_H
#define TRC_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The ESP-WROVER-KIT has no battery-backed RTC, so absolute wall-clock time is
 * unknown at boot. We emit a fixed, well-formed placeholder $STARTTIME (an OLE
 * automation date — days since 1899-12-30) so readers parse the file cleanly;
 * only the relative time offsets between frames are meaningful, which is exactly
 * what offline decode/replay uses. 43831.0 == 2020-01-01 00:00:00 UTC.
 */
#define TRC_PLACEHOLDER_STARTTIME "43831.0000000"

/* One captured CAN frame in engineering terms. */
typedef struct {
    uint32_t id;        /* 11- or 29-bit identifier          */
    bool     extended;  /* true => 29-bit (extended) frame   */
    bool     rtr;       /* true => remote frame (no data)    */
    uint8_t  dlc;       /* data length code, 0..8            */
    uint8_t  data[8];   /* payload (only dlc bytes are valid) */
} trc_frame_t;

/*
 * Write the PCAN 2.1 header block (comment lines + $FILEVERSION/$STARTTIME/
 * $COLUMNS) into buf. Returns the number of characters written excluding the
 * terminating NUL, or a negative value if buf is too small.
 */
int trc_format_header(char *buf, size_t buf_len);

/*
 * Format a single data line (no trailing newline) into buf. msgnr is 1-based;
 * time_ms is the offset from the first captured frame in milliseconds. Returns
 * the number of characters written excluding the NUL, or a negative value on
 * truncation.
 */
int trc_format_line(char *buf, size_t buf_len,
                    uint32_t msgnr, double time_ms, const trc_frame_t *f);

#endif /* TRC_FORMAT_H */
