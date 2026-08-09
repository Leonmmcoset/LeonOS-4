#ifndef NTCLKS_FRAMEBUFFER_H
#define NTCLKS_FRAMEBUFFER_H

#include <ntclks/multiboot2.h>
#include <ntclks/types.h>

struct framebuffer {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t max_bytes;
    uint32_t backend;
    uint64_t reservation_start;
    uint32_t reservation_bytes;
    uint8_t bpp;
    uint8_t bytes_per_pixel;
    uint16_t capabilities;
    uint8_t type;
    uint8_t red_field_position;
    uint8_t red_mask_size;
    uint8_t green_field_position;
    uint8_t green_mask_size;
    uint8_t blue_field_position;
    uint8_t blue_mask_size;
    bool available;
};

struct framebuffer_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
};

struct framebuffer_capabilities {
    uint8_t bytes_per_pixel;
    uint8_t reserved;
    uint16_t capabilities;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t max_bytes;
    uint32_t backend;
};

#define FRAMEBUFFER_CAP_MODE_SET 0x0001u

#define FRAMEBUFFER_BACKEND_BOOT 0u
#define FRAMEBUFFER_BACKEND_BOCHS_VBE 1u
#define FRAMEBUFFER_BACKEND_VMWARE_SVGA 2u

struct framebuffer_mode_cmd {
    uint32_t width;
    uint32_t height;
};

struct framebuffer_rect_cmd {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
};

struct framebuffer_text_cmd {
    uint32_t x;
    uint32_t y;
    uint32_t fg;
    uint32_t bg;
    const char *text;
};

struct framebuffer_blit_cmd {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    const uint32_t *pixels;
};

void framebuffer_init(const struct boot_info *boot);
const struct framebuffer *framebuffer_get(void);
int framebuffer_set_mode(uint32_t width, uint32_t height);
void framebuffer_clear(uint32_t color);
void framebuffer_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void framebuffer_text(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg);
void framebuffer_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t stride, const uint32_t *pixels);
void framebuffer_present(void);
void framebuffer_present_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
uint32_t framebuffer_get_pixel_public(uint32_t x, uint32_t y);
void framebuffer_put_pixel_public(uint32_t x, uint32_t y, uint32_t color);
void desktop_boot_paint(void);
void desktop_handle_mouse(uint32_t x, uint32_t y, uint8_t buttons);
void desktop_draw_mouse(uint32_t x, uint32_t y);

#endif
