/*
 * Public-domain 8x8 monochrome bitmap font, ASCII 0x00-0x7F. One byte per
 * pixel row (8 rows/glyph), bit 0 = leftmost pixel.
 *
 * Author: Daniel Hepper <daniel@hepper.net>, based on Marcel Sondaar /
 * IBM's public-domain VGA font data. https://github.com/dhepper/font8x8
 * (public domain).
 */
#ifndef FONT8X8_H
#define FONT8X8_H

#include <stdint.h>

extern const uint8_t font8x8_basic[128][8];

#endif /* FONT8X8_H */
