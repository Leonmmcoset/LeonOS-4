/*
 * LeonOS framebuffer interface: declares early and kernel display services.
 * Handles framebuffer discovery, pixel output, fonts, and console rendering.
 */
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

/**
 * @brief Discover and set up the framebuffer described by the boot info.
 */
void framebuffer_init(const struct boot_info *boot);
const struct framebuffer *framebuffer_get(void);
/**
 * @brief Switch the display to width x height; returns 0 on success.
 */
int framebuffer_set_mode(uint32_t width, uint32_t height);
/**
 * @brief Fill the whole screen with color.
 */
void framebuffer_clear(uint32_t color);
/**
 * @brief Fill the axis-aligned rectangle at (x,y) of size w x h with color.
 */
void framebuffer_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
/**
 * @brief Draw text at (x,y) using fg as the foreground and bg as the background color.
 */
void framebuffer_text(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg);
/**
 * @brief Copy a w x h pixel buffer (stride words per row) to (x,y).
 */
void framebuffer_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t stride, const uint32_t *pixels);
/**
 * @brief Flush the back buffer so pending drawing becomes visible.
 */
void framebuffer_present(void);
/**
 * @brief Flush only the (x,y,width,height) sub-region of the back buffer.
 */
void framebuffer_present_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
/**
 * @brief Read the color of the pixel at (x,y).
 */
uint32_t framebuffer_get_pixel_public(uint32_t x, uint32_t y);
/**
 * @brief Set the pixel at (x,y) to color.
 */
void framebuffer_put_pixel_public(uint32_t x, uint32_t y, uint32_t color);
/**
 * @brief Draw the initial desktop wallpaper/background at startup.
 */
void desktop_boot_paint(void);
/**
 * @brief Update desktop state for a mouse move to (x,y) with the given buttons.
 */
void desktop_handle_mouse(uint32_t x, uint32_t y, uint8_t buttons);
/**
 * @brief Repaint the mouse cursor at (x,y).
 */
void desktop_draw_mouse(uint32_t x, uint32_t y);

#endif
