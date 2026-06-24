#ifndef NTCLKS_FRAMEBUFFER_H
#define NTCLKS_FRAMEBUFFER_H

#include <ntclks/multiboot2.h>
#include <ntclks/types.h>

struct framebuffer {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    bool available;
};

struct framebuffer_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
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
void framebuffer_clear(uint32_t color);
void framebuffer_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void framebuffer_text(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg);
void framebuffer_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t stride, const uint32_t *pixels);
uint32_t framebuffer_get_pixel_public(uint32_t x, uint32_t y);
void framebuffer_put_pixel_public(uint32_t x, uint32_t y, uint32_t color);
void desktop_boot_paint(void);
void desktop_handle_mouse(uint32_t x, uint32_t y, uint8_t buttons);
void desktop_draw_mouse(uint32_t x, uint32_t y);

#endif
