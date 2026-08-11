#ifndef LEONOS_TEXT_H
#define LEONOS_TEXT_H

#include <stdint.h>

#define LEONOS_TEXT_IOCTL_LAYOUT_UTF8 0x4c545854UL

#define LEONOS_UNICODE_OP_LAYOUT_UTF8 1u
#define LEONOS_UNICODE_OP_UTF8_TO_UTF16LE 2u
#define LEONOS_UNICODE_OP_UTF16LE_TO_UTF8 3u
#define LEONOS_UNICODE_OP_VALIDATE_UTF8 4u

#define LEONOS_TEXT_REPLACEMENT_CHAR 0xfffdu

/* Encodings accepted by the portable text conversion helpers. */
#define LEONOS_TEXT_ENCODING_UTF8 1u
#define LEONOS_TEXT_ENCODING_UTF8_BOM 2u
#define LEONOS_TEXT_ENCODING_UTF16LE 3u
#define LEONOS_TEXT_ENCODING_UTF16BE 4u
#define LEONOS_TEXT_ENCODING_GBK 5u
#define LEONOS_TEXT_ENCODING_GB2312 6u

#define LEONOS_TEXT_ENCODING_INVALID -1
#define LEONOS_TEXT_ENCODING_NO_SPACE -2

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

/* Detect a text file's byte encoding. ASCII is reported as UTF-8. */
int leonos_text_detect_encoding(const char *input, uint32_t input_len,
                                uint32_t *out_encoding);

/* Decode input into UTF-8, replacing malformed source sequences with U+FFFD. */
int leonos_text_decode(const char *input, uint32_t input_len, uint32_t encoding,
                       char *output, uint32_t output_capacity,
                       uint32_t *out_len, uint32_t *out_replacements);

/* Encode UTF-8 input, replacing characters unavailable in the target with '?'. */
int leonos_text_encode(const char *input, uint32_t input_len, uint32_t encoding,
                       char *output, uint32_t output_capacity,
                       uint32_t *out_len, uint32_t *out_replacements);

#endif
