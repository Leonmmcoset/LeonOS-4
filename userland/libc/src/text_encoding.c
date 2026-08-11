#include <leonos/text.h>

#include <generated/leonos_gbk_table.h>

static int text_append(char *output, uint32_t capacity, uint32_t *position,
                       const char *bytes, uint32_t length)
{
    uint32_t index;
    if (*position > capacity || length > capacity - *position) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        output[*position + index] = bytes[index];
    }
    *position += length;
    return 1;
}

static int text_append_byte(char *output, uint32_t capacity, uint32_t *position,
                            uint8_t value)
{
    char byte = (char)value;
    return text_append(output, capacity, position, &byte, 1);
}

static int text_append_utf8(char *output, uint32_t capacity, uint32_t *position,
                            uint32_t codepoint)
{
    char bytes[4];
    uint32_t length;
    if (codepoint <= 0x7fu) {
        bytes[0] = (char)codepoint;
        length = 1;
    } else if (codepoint <= 0x7ffu) {
        bytes[0] = (char)(0xc0u | (codepoint >> 6));
        bytes[1] = (char)(0x80u | (codepoint & 0x3fu));
        length = 2;
    } else if (codepoint <= 0xffffu) {
        bytes[0] = (char)(0xe0u | (codepoint >> 12));
        bytes[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        bytes[2] = (char)(0x80u | (codepoint & 0x3fu));
        length = 3;
    } else {
        bytes[0] = (char)(0xf0u | (codepoint >> 18));
        bytes[1] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
        bytes[2] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        bytes[3] = (char)(0x80u | (codepoint & 0x3fu));
        length = 4;
    }
    return text_append(output, capacity, position, bytes, length);
}

static uint32_t text_utf8_next(const char *input, uint32_t input_len,
                               uint32_t offset, uint32_t *out_codepoint)
{
    uint8_t first;
    uint8_t second;
    uint8_t third;
    uint8_t fourth;
    if (!input || offset >= input_len || !out_codepoint) {
        return 0;
    }
    first = (uint8_t)input[offset];
    if (first <= 0x7fu) {
        *out_codepoint = first;
        return 1;
    }
    if (first >= 0xc2u && first <= 0xdfu && offset + 1 < input_len) {
        second = (uint8_t)input[offset + 1];
        if (second >= 0x80u && second <= 0xbfu) {
            *out_codepoint = ((uint32_t)(first & 0x1fu) << 6) | (second & 0x3fu);
            return 2;
        }
    }
    if (first >= 0xe0u && first <= 0xefu && offset + 2 < input_len) {
        second = (uint8_t)input[offset + 1];
        third = (uint8_t)input[offset + 2];
        if (second >= 0x80u && second <= 0xbfu && third >= 0x80u && third <= 0xbfu &&
            !(first == 0xe0u && second < 0xa0u) &&
            !(first == 0xedu && second > 0x9fu)) {
            *out_codepoint = ((uint32_t)(first & 0x0fu) << 12) |
                             ((uint32_t)(second & 0x3fu) << 6) | (third & 0x3fu);
            return 3;
        }
    }
    if (first >= 0xf0u && first <= 0xf4u && offset + 3 < input_len) {
        second = (uint8_t)input[offset + 1];
        third = (uint8_t)input[offset + 2];
        fourth = (uint8_t)input[offset + 3];
        if (second >= 0x80u && second <= 0xbfu && third >= 0x80u && third <= 0xbfu &&
            fourth >= 0x80u && fourth <= 0xbfu &&
            !(first == 0xf0u && second < 0x90u) &&
            !(first == 0xf4u && second > 0x8fu)) {
            *out_codepoint = ((uint32_t)(first & 0x07u) << 18) |
                             ((uint32_t)(second & 0x3fu) << 12) |
                             ((uint32_t)(third & 0x3fu) << 6) | (fourth & 0x3fu);
            return 4;
        }
    }
    return 0;
}

static int text_utf8_valid(const char *input, uint32_t input_len)
{
    uint32_t offset = 0;
    uint32_t codepoint;
    while (offset < input_len) {
        uint32_t length = text_utf8_next(input, input_len, offset, &codepoint);
        if (!length) {
            return 0;
        }
        offset += length;
    }
    return 1;
}

static int text_gbk_pair(uint8_t lead, uint8_t trail, uint32_t encoding,
                         uint32_t *out_codepoint)
{
    uint32_t pointer;
    if (!out_codepoint || lead < 0x81u || lead > 0xfeu || trail == 0x7fu ||
        trail < 0x40u || trail > 0xfeu) {
        return 0;
    }
    if (encoding == LEONOS_TEXT_ENCODING_GB2312 &&
        (lead < 0xa1u || lead > 0xf7u || trail < 0xa1u || trail > 0xfeu)) {
        return 0;
    }
    pointer = (uint32_t)(lead - 0x81u) * 190u +
              (trail < 0x7fu ? (uint32_t)(trail - 0x40u)
                              : (uint32_t)(trail - 0x41u));
    if (pointer >= LEONOS_GBK_POINTER_COUNT || !leonos_gbk_to_unicode[pointer]) {
        return 0;
    }
    *out_codepoint = leonos_gbk_to_unicode[pointer];
    return 1;
}

static int text_gbk_valid(const char *input, uint32_t input_len, uint32_t encoding)
{
    uint32_t offset = 0;
    while (offset < input_len) {
        uint8_t first = (uint8_t)input[offset++];
        uint32_t codepoint;
        if (first <= 0x7fu || (encoding == LEONOS_TEXT_ENCODING_GBK && first == 0x80u)) {
            continue;
        }
        if (offset >= input_len ||
            !text_gbk_pair(first, (uint8_t)input[offset], encoding, &codepoint)) {
            return 0;
        }
        ++offset;
    }
    return 1;
}

static uint16_t text_read_u16(const char *input, uint32_t offset, uint32_t encoding)
{
    uint8_t first = (uint8_t)input[offset];
    uint8_t second = (uint8_t)input[offset + 1];
    return encoding == LEONOS_TEXT_ENCODING_UTF16LE
               ? (uint16_t)((uint16_t)first | ((uint16_t)second << 8))
               : (uint16_t)(((uint16_t)first << 8) | second);
}

static int text_append_u16(char *output, uint32_t capacity, uint32_t *position,
                           uint16_t value, uint32_t encoding)
{
    char bytes[2];
    if (encoding == LEONOS_TEXT_ENCODING_UTF16LE) {
        bytes[0] = (char)(value & 0xffu);
        bytes[1] = (char)(value >> 8);
    } else {
        bytes[0] = (char)(value >> 8);
        bytes[1] = (char)(value & 0xffu);
    }
    return text_append(output, capacity, position, bytes, sizeof(bytes));
}

static int text_find_gbk(uint32_t codepoint, uint32_t encoding,
                         uint8_t *out_lead, uint8_t *out_trail)
{
    uint32_t low = 0;
    uint32_t high = LEONOS_GBK_MAPPED_COUNT;
    uint32_t match;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t candidate = leonos_gbk_to_unicode[leonos_gbk_unicode_pointers[middle]];
        if (candidate < codepoint) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low >= LEONOS_GBK_MAPPED_COUNT ||
        leonos_gbk_to_unicode[leonos_gbk_unicode_pointers[low]] != codepoint) {
        return 0;
    }
    match = low;
    while (match > 0 &&
           leonos_gbk_to_unicode[leonos_gbk_unicode_pointers[match - 1u]] == codepoint) {
        --match;
    }
    while (match < LEONOS_GBK_MAPPED_COUNT &&
           leonos_gbk_to_unicode[leonos_gbk_unicode_pointers[match]] == codepoint) {
        uint32_t pointer = leonos_gbk_unicode_pointers[match];
        uint8_t lead;
        uint8_t trail;
        lead = (uint8_t)(pointer / 190u + 0x81u);
        trail = (uint8_t)(pointer % 190u);
        trail = trail < 63u ? (uint8_t)(trail + 0x40u)
                            : (uint8_t)(trail + 0x41u);
        if (encoding == LEONOS_TEXT_ENCODING_GB2312 &&
            (lead < 0xa1u || lead > 0xf7u || trail < 0xa1u || trail > 0xfeu)) {
            ++match;
            continue;
        }
        *out_lead = lead;
        *out_trail = trail;
        return 1;
    }
    return 0;
}

int leonos_text_detect_encoding(const char *input, uint32_t input_len,
                                uint32_t *out_encoding)
{
    if ((!input && input_len) || !out_encoding) {
        return LEONOS_TEXT_ENCODING_INVALID;
    }
    if (input_len >= 3 && (uint8_t)input[0] == 0xefu &&
        (uint8_t)input[1] == 0xbbu && (uint8_t)input[2] == 0xbfu) {
        *out_encoding = LEONOS_TEXT_ENCODING_UTF8_BOM;
    } else if (input_len >= 2 && (uint8_t)input[0] == 0xffu &&
               (uint8_t)input[1] == 0xfeu) {
        *out_encoding = LEONOS_TEXT_ENCODING_UTF16LE;
    } else if (input_len >= 2 && (uint8_t)input[0] == 0xfeu &&
               (uint8_t)input[1] == 0xffu) {
        *out_encoding = LEONOS_TEXT_ENCODING_UTF16BE;
    } else if (text_utf8_valid(input, input_len)) {
        *out_encoding = LEONOS_TEXT_ENCODING_UTF8;
    } else if (text_gbk_valid(input, input_len, LEONOS_TEXT_ENCODING_GB2312)) {
        *out_encoding = LEONOS_TEXT_ENCODING_GB2312;
    } else {
        *out_encoding = LEONOS_TEXT_ENCODING_GBK;
    }
    return 0;
}

int leonos_text_decode(const char *input, uint32_t input_len, uint32_t encoding,
                       char *output, uint32_t output_capacity,
                       uint32_t *out_len, uint32_t *out_replacements)
{
    uint32_t offset = 0;
    uint32_t position = 0;
    uint32_t replacements = 0;
    if ((!input && input_len) || (!output && output_capacity) || !out_len ||
        !out_replacements) {
        return LEONOS_TEXT_ENCODING_INVALID;
    }
    if (encoding != LEONOS_TEXT_ENCODING_UTF8 &&
        encoding != LEONOS_TEXT_ENCODING_UTF8_BOM &&
        encoding != LEONOS_TEXT_ENCODING_UTF16LE &&
        encoding != LEONOS_TEXT_ENCODING_UTF16BE &&
        encoding != LEONOS_TEXT_ENCODING_GBK &&
        encoding != LEONOS_TEXT_ENCODING_GB2312) {
        return LEONOS_TEXT_ENCODING_INVALID;
    }
    if (encoding == LEONOS_TEXT_ENCODING_UTF8_BOM && input_len >= 3 &&
        (uint8_t)input[0] == 0xefu && (uint8_t)input[1] == 0xbbu &&
        (uint8_t)input[2] == 0xbfu) {
        offset = 3;
    }
    if ((encoding == LEONOS_TEXT_ENCODING_UTF16LE ||
         encoding == LEONOS_TEXT_ENCODING_UTF16BE) && input_len >= 2 &&
        ((encoding == LEONOS_TEXT_ENCODING_UTF16LE && (uint8_t)input[0] == 0xffu &&
          (uint8_t)input[1] == 0xfeu) ||
         (encoding == LEONOS_TEXT_ENCODING_UTF16BE && (uint8_t)input[0] == 0xfeu &&
          (uint8_t)input[1] == 0xffu))) {
        offset = 2;
    }
    while (offset < input_len) {
        uint32_t codepoint = LEONOS_TEXT_REPLACEMENT_CHAR;
        int invalid = 0;
        if (encoding == LEONOS_TEXT_ENCODING_UTF8 ||
            encoding == LEONOS_TEXT_ENCODING_UTF8_BOM) {
            uint32_t length = text_utf8_next(input, input_len, offset, &codepoint);
            if (length) {
                offset += length;
            } else {
                ++offset;
                invalid = 1;
            }
        } else if (encoding == LEONOS_TEXT_ENCODING_UTF16LE ||
                   encoding == LEONOS_TEXT_ENCODING_UTF16BE) {
            uint16_t first;
            if (offset + 1 >= input_len) {
                ++offset;
                invalid = 1;
            } else {
                first = text_read_u16(input, offset, encoding);
                offset += 2;
                if (first >= 0xd800u && first <= 0xdbffu && offset + 1 < input_len) {
                    uint16_t second = text_read_u16(input, offset, encoding);
                    if (second >= 0xdc00u && second <= 0xdfffu) {
                        codepoint = 0x10000u + ((uint32_t)(first - 0xd800u) << 10) +
                                    (uint32_t)(second - 0xdc00u);
                        offset += 2;
                    } else {
                        invalid = 1;
                    }
                } else if (first >= 0xd800u && first <= 0xdfffu) {
                    invalid = 1;
                } else {
                    codepoint = first;
                }
            }
        } else {
            uint8_t first = (uint8_t)input[offset++];
            if (first <= 0x7fu) {
                codepoint = first;
            } else if (encoding == LEONOS_TEXT_ENCODING_GBK && first == 0x80u) {
                codepoint = 0x20acu;
            } else if (offset < input_len &&
                       text_gbk_pair(first, (uint8_t)input[offset], encoding, &codepoint)) {
                ++offset;
            } else {
                invalid = 1;
            }
        }
        if (invalid) {
            ++replacements;
        }
        if (!text_append_utf8(output, output_capacity, &position, codepoint)) {
            *out_len = position;
            *out_replacements = replacements;
            return LEONOS_TEXT_ENCODING_NO_SPACE;
        }
    }
    *out_len = position;
    *out_replacements = replacements;
    return 0;
}

int leonos_text_encode(const char *input, uint32_t input_len, uint32_t encoding,
                       char *output, uint32_t output_capacity,
                       uint32_t *out_len, uint32_t *out_replacements)
{
    uint32_t offset = 0;
    uint32_t position = 0;
    uint32_t replacements = 0;
    if ((!input && input_len) || (!output && output_capacity) || !out_len ||
        !out_replacements) {
        return LEONOS_TEXT_ENCODING_INVALID;
    }
    if (encoding != LEONOS_TEXT_ENCODING_UTF8 &&
        encoding != LEONOS_TEXT_ENCODING_UTF8_BOM &&
        encoding != LEONOS_TEXT_ENCODING_UTF16LE &&
        encoding != LEONOS_TEXT_ENCODING_UTF16BE &&
        encoding != LEONOS_TEXT_ENCODING_GBK &&
        encoding != LEONOS_TEXT_ENCODING_GB2312) {
        return LEONOS_TEXT_ENCODING_INVALID;
    }
    if (encoding == LEONOS_TEXT_ENCODING_UTF8_BOM &&
        (!text_append_byte(output, output_capacity, &position, 0xefu) ||
         !text_append_byte(output, output_capacity, &position, 0xbbu) ||
         !text_append_byte(output, output_capacity, &position, 0xbfu))) {
        *out_len = position;
        *out_replacements = replacements;
        return LEONOS_TEXT_ENCODING_NO_SPACE;
    }
    if ((encoding == LEONOS_TEXT_ENCODING_UTF16LE ||
         encoding == LEONOS_TEXT_ENCODING_UTF16BE) &&
        !text_append_u16(output, output_capacity, &position, 0xfeffu, encoding)) {
        *out_len = position;
        *out_replacements = replacements;
        return LEONOS_TEXT_ENCODING_NO_SPACE;
    }
    while (offset < input_len) {
        uint32_t codepoint;
        uint32_t length = text_utf8_next(input, input_len, offset, &codepoint);
        int invalid = !length;
        if (invalid) {
            codepoint = LEONOS_TEXT_REPLACEMENT_CHAR;
            length = 1;
            ++replacements;
        }
        offset += length;
        if (encoding == LEONOS_TEXT_ENCODING_UTF8 ||
            encoding == LEONOS_TEXT_ENCODING_UTF8_BOM) {
            if (!text_append_utf8(output, output_capacity, &position, codepoint)) {
                *out_len = position;
                *out_replacements = replacements;
                return LEONOS_TEXT_ENCODING_NO_SPACE;
            }
        } else if (encoding == LEONOS_TEXT_ENCODING_UTF16LE ||
                   encoding == LEONOS_TEXT_ENCODING_UTF16BE) {
            if (codepoint <= 0xffffu) {
                if (!text_append_u16(output, output_capacity, &position,
                                     (uint16_t)codepoint, encoding)) {
                    *out_len = position;
                    *out_replacements = replacements;
                    return LEONOS_TEXT_ENCODING_NO_SPACE;
                }
            } else if (!text_append_u16(output, output_capacity, &position,
                                        (uint16_t)(0xd800u + ((codepoint - 0x10000u) >> 10)), encoding) ||
                       !text_append_u16(output, output_capacity, &position,
                                        (uint16_t)(0xdc00u + ((codepoint - 0x10000u) & 0x3ffu)), encoding)) {
                *out_len = position;
                *out_replacements = replacements;
                return LEONOS_TEXT_ENCODING_NO_SPACE;
            }
        } else if (codepoint <= 0x7fu) {
            if (!text_append_byte(output, output_capacity, &position, (uint8_t)codepoint)) {
                *out_len = position;
                *out_replacements = replacements;
                return LEONOS_TEXT_ENCODING_NO_SPACE;
            }
        } else if (encoding == LEONOS_TEXT_ENCODING_GBK && codepoint == 0x20acu) {
            if (!text_append_byte(output, output_capacity, &position, 0x80u)) {
                *out_len = position;
                *out_replacements = replacements;
                return LEONOS_TEXT_ENCODING_NO_SPACE;
            }
        } else {
            uint8_t lead;
            uint8_t trail;
            if (!text_find_gbk(codepoint, encoding, &lead, &trail)) {
                ++replacements;
                if (!text_append_byte(output, output_capacity, &position, '?')) {
                    *out_len = position;
                    *out_replacements = replacements;
                    return LEONOS_TEXT_ENCODING_NO_SPACE;
                }
            } else if (!text_append_byte(output, output_capacity, &position, lead) ||
                       !text_append_byte(output, output_capacity, &position, trail)) {
                *out_len = position;
                *out_replacements = replacements;
                return LEONOS_TEXT_ENCODING_NO_SPACE;
            }
        }
    }
    *out_len = position;
    *out_replacements = replacements;
    return 0;
}
