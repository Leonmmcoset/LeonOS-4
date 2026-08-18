#ifndef LEONOS_PNG_H
#define LEONOS_PNG_H

#include <stdint.h>

/* PNG decoding stays bounded so a malformed or oversized image cannot turn a
 * document preview into an unbounded allocation. */
#define LEONOS_PNG_MAX_PIXELS (1024U * 1024U)
#define LEONOS_PNG_MAX_FILE_BYTES (16U * 1024U * 1024U)

/*
 * Decode a PNG file into LeonOS UI pixels (0x00RRGGBB).  Transparency is
 * composited on white.  On success the caller owns *out_pixels and releases
 * it with leonos_png_free().
 */
int leonos_png_decode_file(const char *path, uint32_t **out_pixels,
                           uint32_t *out_width, uint32_t *out_height);
int leonos_png_decode_memory(const void *data, uint32_t size,
                             uint32_t **out_pixels, uint32_t *out_width,
                             uint32_t *out_height);
void leonos_png_free(uint32_t *pixels);

#endif
