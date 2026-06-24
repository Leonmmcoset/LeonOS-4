#ifndef LEONOS_PSF_FONT_H
#define LEONOS_PSF_FONT_H

#include <stdint.h>

#include <leonos/lat15_vga16_psf.inc>

#define LEONOS_FONT_W 8u
#define LEONOS_FONT_H 16u

static inline const uint8_t *leonos_psf_glyph(char ch)
{
    const uint8_t *font = leonos_lat15_vga16_psf;
    uint32_t glyph_count = 256;
    uint32_t char_size = 16;
    uint32_t glyph = (uint8_t)ch;

    if (leonos_lat15_vga16_psf_len >= 4 && font[0] == 0x36 && font[1] == 0x04) {
        char_size = font[3];
        glyph_count = (font[2] & 0x01u) ? 512u : 256u;
        font += 4;
    } else if (leonos_lat15_vga16_psf_len >= 32 &&
               font[0] == 0x72 && font[1] == 0xb5 && font[2] == 0x4a && font[3] == 0x86) {
        uint32_t header_size = (uint32_t)font[8] |
                               ((uint32_t)font[9] << 8) |
                               ((uint32_t)font[10] << 16) |
                               ((uint32_t)font[11] << 24);
        glyph_count = (uint32_t)font[16] |
                      ((uint32_t)font[17] << 8) |
                      ((uint32_t)font[18] << 16) |
                      ((uint32_t)font[19] << 24);
        char_size = (uint32_t)font[20] |
                    ((uint32_t)font[21] << 8) |
                    ((uint32_t)font[22] << 16) |
                    ((uint32_t)font[23] << 24);
        font += header_size;
    }

    if (glyph < 32 || glyph >= glyph_count) {
        glyph = '?';
    }
    return font + glyph * char_size;
}

#endif
