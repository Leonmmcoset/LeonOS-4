#include <leonos/fs.h>
#include <leonos/png.h>
#include <leonos/syscall.h>

#include <png.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int png_read_file(const char *path, uint8_t **out_data, uint32_t *out_size)
{
    struct leonos_stat st;
    uint8_t *data;
    uint32_t offset = 0;
    int fd;

    if (!path || !path[0] || !out_data || !out_size ||
        stat(path, &st) < 0 || st.type != LEONOS_FS_TYPE_FILE ||
        st.size == 0 || st.size > LEONOS_PNG_MAX_FILE_BYTES) {
        return -1;
    }

    data = (uint8_t *)malloc((size_t)st.size);
    if (!data) {
        return -1;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        free(data);
        return fd;
    }
    while (offset < (uint32_t)st.size) {
        long got = read(fd, data + offset, (uint32_t)st.size - offset);
        if (got < 0) {
            close(fd);
            free(data);
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        offset += (uint32_t)got;
    }
    close(fd);
    if (offset != (uint32_t)st.size) {
        free(data);
        return -1;
    }
    *out_data = data;
    *out_size = offset;
    return 0;
}

int leonos_png_decode_memory(const void *data, uint32_t file_size,
                             uint32_t **out_pixels, uint32_t *out_width,
                             uint32_t *out_height)
{
    png_image image;
    uint8_t *rgba = 0;
    uint32_t *pixels = 0;
    uint64_t pixel_count;
    int result = -1;

    if (!data || file_size == 0 || file_size > LEONOS_PNG_MAX_FILE_BYTES ||
        !out_pixels || !out_width || !out_height) {
        return -1;
    }
    *out_pixels = 0;
    *out_width = 0;
    *out_height = 0;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, file_size) ||
        image.width == 0 || image.height == 0) {
        goto cleanup;
    }
    pixel_count = (uint64_t)image.width * (uint64_t)image.height;
    if (pixel_count > LEONOS_PNG_MAX_PIXELS) {
        goto cleanup;
    }
    image.format = PNG_FORMAT_RGBA;
    rgba = (uint8_t *)malloc((size_t)pixel_count * 4U);
    pixels = (uint32_t *)malloc((size_t)pixel_count * sizeof(*pixels));
    if (!rgba || !pixels || !png_image_finish_read(&image, 0, rgba, 0, 0)) {
        goto cleanup;
    }
    for (uint64_t i = 0; i < pixel_count; ++i) {
        uint32_t alpha = rgba[i * 4U + 3U];
        uint32_t inverse_alpha = 255U - alpha;
        uint32_t red = (rgba[i * 4U] * alpha + 255U * inverse_alpha + 127U) / 255U;
        uint32_t green = (rgba[i * 4U + 1U] * alpha + 255U * inverse_alpha + 127U) / 255U;
        uint32_t blue = (rgba[i * 4U + 2U] * alpha + 255U * inverse_alpha + 127U) / 255U;
        pixels[i] = (red << 16) | (green << 8) | blue;
    }
    *out_pixels = pixels;
    *out_width = image.width;
    *out_height = image.height;
    pixels = 0;
    result = 0;

cleanup:
    png_image_free(&image);
    free(rgba);
    free(pixels);
    return result;
}

int leonos_png_decode_file(const char *path, uint32_t **out_pixels,
                           uint32_t *out_width, uint32_t *out_height)
{
    uint8_t *file_data = 0;
    uint32_t file_size = 0;
    int result;
    if (png_read_file(path, &file_data, &file_size) < 0) {
        return -1;
    }
    result = leonos_png_decode_memory(file_data, file_size, out_pixels,
                                      out_width, out_height);
    free(file_data);
    return result;
}

void leonos_png_free(uint32_t *pixels)
{
    free(pixels);
}
