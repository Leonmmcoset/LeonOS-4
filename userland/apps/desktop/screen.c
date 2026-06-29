#include "desktop.h"

void draw_cursor_shape(uint32_t x, uint32_t y)
{
    if (x + cursor_width > fb_w()) {
        x = fb_w() > cursor_width ? fb_w() - cursor_width : 0;
    }
    if (y + cursor_height > fb_h()) {
        y = fb_h() > cursor_height ? fb_h() - cursor_height : 0;
    }
    if (cursor_bitmap_loaded) {
        for (uint32_t row = 0; row < cursor_height; ++row) {
            for (uint32_t col = 0; col < cursor_width; ++col) {
                uint32_t px = cursor_pixels[row * CURSOR_MAX_W + col];
                if ((px >> 24) != 0) {
                    put_pixel(x + col, y + row, px & 0x00ffffffu);
                }
            }
        }
        return;
    }
    for (uint32_t row = 0; row < FALLBACK_CURSOR_H; ++row) {
        for (uint32_t col = 0; col < FALLBACK_CURSOR_W; ++col) {
            char cell = cursor_art[row][col];
            if (cell == 'X') {
                put_pixel(x + col, y + row, 0x00000000);
            } else if (cell == 'O') {
                put_pixel(x + col, y + row, 0x00ffffff);
            }
        }
    }
}

void redraw_region(struct rect dirty)
{
    dirty = rect_clip(dirty);
    if (dirty.w <= 0 || dirty.h <= 0) {
        return;
    }

    rect_fill((uint32_t)dirty.x, (uint32_t)dirty.y, (uint32_t)dirty.w, (uint32_t)dirty.h, 0x00008080);

    if (active_window_is_fullscreen()) {
        if (rect_intersects(dirty, window_rect((uint8_t)active_window))) {
            draw_window((uint8_t)active_window);
        }
        if (cursor_visible) {
            draw_cursor_shape(cursor_x, cursor_y);
        }
        return;
    }

    if (rect_intersects(dirty, rect_make(8, 32, 72, 58))) {
        rect_fill(24, 32, 48, 38, 0x00c0c0c0);
        rect_fill(24, 32, 48, 2, 0x00ffffff);
        rect_fill(24, 32, 2, 38, 0x00ffffff);
        rect_fill(70, 32, 2, 38, 0x00000000);
        rect_fill(24, 68, 48, 2, 0x00000000);
        text_draw(16, 78, "0:/", 0x00ffffff, 0x00008080);
    }
    if (rect_intersects(dirty, rect_make(8, 112, 72, 58))) {
        rect_fill(24, 112, 48, 38, 0x00c0c0c0);
        rect_fill(24, 112, 48, 2, 0x00ffffff);
        rect_fill(24, 112, 2, 38, 0x00ffffff);
        rect_fill(70, 112, 2, 38, 0x00000000);
        rect_fill(24, 148, 48, 2, 0x00000000);
        text_draw(8, 158, "Apps", 0x00ffffff, 0x00008080);
    }

    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (rect_intersects(dirty, window_rect(z_order[i]))) {
            draw_window(z_order[i]);
        }
    }

    draw_snap_preview();

    uint32_t tb_y = taskbar_y();
    if ((uint32_t)(dirty.y + dirty.h) >= tb_y) {
        leonos_ui_taskbar(&ui, tb_y, TASKBAR_H);
        leonos_ui_button(&ui, 6, tb_y + 5, 86, LEONOS_UI_BUTTON_H, "Start",
                         start_menu_open ? LEONOS_UI_BUTTON_PRESSED : 0);
        uint32_t x = 106;
        uint32_t button_w = taskbar_button_width(running_window_count());
        for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
            if (windows[i].visible && button_w > 0) {
                draw_taskbar_button(i, x, tb_y + 5, button_w);
                x += button_w;
            }
        }
    }

    draw_start_menu();
    draw_alt_tab_overlay();
    if (cursor_visible) {
        draw_cursor_shape(cursor_x, cursor_y);
    }
}

void flush_region(struct rect dirty)
{
    dirty = rect_clip(dirty);
    if (dirty.w <= 0 || dirty.h <= 0) {
        return;
    }
    if (desktop_scale <= 1) {
        const uint32_t *src = screen + (uint64_t)dirty.y * MAX_FB_W + dirty.x;
        leonos_fb_blit((uint32_t)dirty.x, (uint32_t)dirty.y, (uint32_t)dirty.w, (uint32_t)dirty.h, MAX_FB_W, src);
        return;
    }
    uint32_t scale = desktop_scale;
    static uint32_t scaled[384 * 384];
    const uint32_t max_out_w = 384;
    const uint32_t max_out_h = 384;
    for (uint32_t chunk_y = 0; chunk_y < (uint32_t)dirty.h;) {
        uint32_t logical_h = (uint32_t)dirty.h - chunk_y;
        if (logical_h * scale > max_out_h) {
            logical_h = max_out_h / scale;
        }
        if (!logical_h) {
            logical_h = 1;
        }
        for (uint32_t chunk_x = 0; chunk_x < (uint32_t)dirty.w;) {
            uint32_t logical_w = (uint32_t)dirty.w - chunk_x;
            if (logical_w * scale > max_out_w) {
                logical_w = max_out_w / scale;
            }
            if (!logical_w) {
                logical_w = 1;
            }
            for (uint32_t y = 0; y < logical_h; ++y) {
                const uint32_t *src = screen + (uint64_t)(dirty.y + (int)chunk_y + (int)y) * MAX_FB_W + dirty.x + (int)chunk_x;
                for (uint32_t sy = 0; sy < scale; ++sy) {
                    uint32_t *dst = scaled + (uint64_t)(y * scale + sy) * max_out_w;
                    for (uint32_t x = 0; x < logical_w; ++x) {
                        for (uint32_t sx = 0; sx < scale; ++sx) {
                            dst[x * scale + sx] = src[x];
                        }
                    }
                }
            }
            leonos_fb_blit((uint32_t)(dirty.x + (int)chunk_x) * scale,
                           (uint32_t)(dirty.y + (int)chunk_y) * scale,
                           logical_w * scale, logical_h * scale,
                           max_out_w, scaled);
            chunk_x += logical_w;
        }
        chunk_y += logical_h;
    }
}

void repaint_and_flush(struct rect dirty)
{
    dirty = rect_clip(dirty);
    redraw_region(dirty);
    flush_region(dirty);
}

void redraw_all(void)
{
    struct rect full = rect_make(0, 0, (int)fb_w(), (int)fb_h());
    if (fb.width > fb_w() * desktop_scale) {
        leonos_fb_rect(fb_w() * desktop_scale, 0,
                       fb.width - fb_w() * desktop_scale,
                       fb.height, 0x00000000);
    }
    if (fb.height > fb_h() * desktop_scale) {
        leonos_fb_rect(0, fb_h() * desktop_scale,
                       fb.width,
                       fb.height - fb_h() * desktop_scale, 0x00000000);
    }
    redraw_region(full);
    flush_region(full);
    full_redraw_pending = start_menu_animating ? 1 : 0;
}

int hit_window(uint32_t x, uint32_t y)
{
    for (int zi = MAX_WINDOWS - 1; zi >= 0; --zi) {
        uint8_t id = z_order[zi];
        struct desktop_window *w = &windows[id];
        if (w->visible && !w->minimized && hit_rect(x, y, w->x, w->y, w->width, w->height)) {
            return id;
        }
    }
    return -1;
}
