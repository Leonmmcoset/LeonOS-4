#ifndef LEONOS_TEXT_H
#define LEONOS_TEXT_H

#include <stdint.h>

#define LEONOS_TEXT_IOCTL_LAYOUT_UTF8 0x4c545854UL

#define LEONOS_UNICODE_OP_LAYOUT_UTF8 1u
#define LEONOS_UNICODE_OP_UTF8_TO_UTF16LE 2u
#define LEONOS_UNICODE_OP_UTF16LE_TO_UTF8 3u
#define LEONOS_UNICODE_OP_VALIDATE_UTF8 4u

#define LEONOS_TEXT_REPLACEMENT_CHAR 0xfffdu

struct leonos_text_glyph {
    uint32_t codepoint;
    uint32_t byte_offset;
    uint32_t byte_len;
    uint32_t cell_width;
    uint32_t pixel_width;
};

struct leonos_text_layout {
    const char *text;
    uint32_t byte_len;
    uint32_t capacity;
    uint32_t count;
    uint32_t total_cells;
    uint32_t total_px;
    struct leonos_text_glyph *glyphs;
};

struct leonos_unicode_utf8_to_utf16 {
    const char *utf8;
    uint32_t utf8_len;
    uint16_t *utf16;
    uint32_t utf16_capacity;
    uint32_t utf16_len;
};

struct leonos_unicode_utf16_to_utf8 {
    const uint16_t *utf16;
    uint32_t utf16_len;
    char *utf8;
    uint32_t utf8_capacity;
    uint32_t utf8_len;
};

int leonos_text_layout_utf8(const char *text, uint32_t byte_len,
                            struct leonos_text_glyph *glyphs,
                            uint32_t capacity,
                            struct leonos_text_layout *out_layout);

#endif
