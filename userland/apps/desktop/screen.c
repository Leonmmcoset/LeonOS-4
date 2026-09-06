#include "desktop.h"
#include <leonos/net_service.h>

static char taskbar_clock_cache[16];
static unsigned long taskbar_clock_cache_second = ~0UL;
static uint8_t taskbar_network_cache_valid;
static uint8_t taskbar_network_connected;
static uint32_t taskbar_network_color = 0x00c00000u;
static unsigned long taskbar_network_cache_ms;
/* The shadow surface is always the base desktop without a software cursor.
 * Cursor pixels are composed into this small staging tile only when it is
 * submitted to the real framebuffer. */
static uint32_t cursor_composite[CURSOR_TILE_W * CURSOR_TILE_H];
#define CURSOR_FRAME_MAX_W 160U
#define CURSOR_FRAME_MAX_H 64U
static uint32_t cursor_frame[CURSOR_FRAME_MAX_W * CURSOR_FRAME_MAX_H];
static uint32_t *cursor_target;
static int cursor_target_x;
static int cursor_target_y;
static uint32_t cursor_target_width;
static uint32_t cursor_target_height;
static uint32_t cursor_target_stride;

static void draw_taskbar_plain_button(uint32_t x, uint32_t y, uint32_t width,
                                      const char *label, uint8_t pressed)
{
    uint32_t text_width;
    uint32_t text_x;
    uint32_t text_y;

    leonos_ui_bevel(&ui, x, y, width, LEONOS_UI_BUTTON_H,
                    LEONOS_UI_GRAY,
                    pressed ? LEONOS_UI_BUTTON_PRESSED : 0);
    if (!label || !label[0] || width <= 8) {
        return;
    }
    text_width = leonos_ui_text_width(label);
    text_x = text_width < width ? x + (width - text_width) / 2 : x + 4;
    text_y = y + (LEONOS_UI_BUTTON_H > LEONOS_FONT_H
                      ? (LEONOS_UI_BUTTON_H - LEONOS_FONT_H) / 2 : 2);
    if (pressed) {
        ++text_x;
        ++text_y;
    }
    leonos_ui_text_transparent_clipped(&ui, text_x, text_y,
                                       width > 8 ? width - 8 : width,
                                       label, LEONOS_UI_BLACK);
}

static void cursor_put_pixel(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= (int)fb_w() || y >= (int)fb_h()) {
        return;
    }
    if (cursor_target) {
        int target_x = x - cursor_target_x;
        int target_y = y - cursor_target_y;
        if (target_x >= 0 && target_y >= 0 &&
            (uint32_t)target_x < cursor_target_width &&
            (uint32_t)target_y < cursor_target_height) {
            cursor_target[(uint64_t)target_y * cursor_target_stride +
                          (uint32_t)target_x] = color;
        }
        return;
    }
    screen[(uint64_t)y * MAX_FB_W + (uint32_t)x] = color;
}

static void format_taskbar_clock(char *buf, uint32_t cap)
{
    struct leonos_time_info time_info;
    unsigned long total_seconds = 0;
    uint32_t hours;
    uint32_t minutes;
    uint32_t seconds;
    uint32_t pos = 0;
    if (leonos_time_info(&time_info) == 0 && time_info.valid) {
        hours = time_info.hour;
        minutes = time_info.minute;
        seconds = time_info.second;
    } else {
        total_seconds = leonos_uptime_ms() / 1000UL;
        hours = (uint32_t)((total_seconds / 3600UL) % 24UL);
        minutes = (uint32_t)((total_seconds / 60UL) % 60UL);
        seconds = (uint32_t)(total_seconds % 60UL);
    }
    buf[0] = 0;
    if (hours < 10) {
        append_char(buf, &pos, cap, '0');
    }
    append_dec(buf, &pos, cap, hours);
    append_char(buf, &pos, cap, ':');
    if (minutes < 10) {
        append_char(buf, &pos, cap, '0');
    }
    append_dec(buf, &pos, cap, minutes);
    append_char(buf, &pos, cap, ':');
    if (seconds < 10) {
        append_char(buf, &pos, cap, '0');
    }
    append_dec(buf, &pos, cap, seconds);
}

static void draw_taskbar_clock(uint32_t tb_y)
{
    uint32_t x;
    unsigned long second;
    if (!desktop_service_rtc_clock || fb_w() < TASKBAR_CLOCK_W + 8) {
        return;
    }
    x = fb_w() - TASKBAR_CLOCK_W;
    second = leonos_uptime_ms() / 1000UL;
    if (taskbar_clock_cache_second != second) {
        format_taskbar_clock(taskbar_clock_cache, sizeof(taskbar_clock_cache));
        taskbar_clock_cache_second = second;
    }
    draw_taskbar_plain_button(x + 4, tb_y + 5, TASKBAR_CLOCK_W - 10,
                              taskbar_clock_cache, 1);
}

static void draw_taskbar_network_icon(uint32_t tb_y)
{
    uint32_t x;
    uint32_t icon_x;
    uint32_t icon_y;
    uint32_t tray_w = desktop_tray_width();
    unsigned long now;
    if (!desktop_service_network_icon || fb_w() < tray_w + 8) {
        return;
    }
    now = leonos_uptime_ms();
    if (!taskbar_network_cache_valid || now - taskbar_network_cache_ms >= 500UL) {
        net_service_config_t config;
        taskbar_network_connected = 0;
        taskbar_network_color = 0x00c00000u;
        if (net_service_config(&config) == 0 &&
            (config.flags & NET_SERVICE_CONFIG_FLAG_ACTIVE) &&
            (config.flags & NET_SERVICE_CONFIG_FLAG_DHCP) &&
            config.source == NET_SERVICE_CONFIG_SOURCE_DHCP &&
            config.local_ip && config.gateway_ip) {
            taskbar_network_connected = 1;
            taskbar_network_color = 0x0000a000u;
        }
        taskbar_network_cache_ms = now;
        taskbar_network_cache_valid = 1;
    }
    x = fb_w() - (desktop_service_rtc_clock ? TASKBAR_CLOCK_W : 0U) - TASKBAR_NET_W;
    icon_x = x + 10;
    icon_y = tb_y + 11;
    draw_taskbar_plain_button(x + 4, tb_y + 5, TASKBAR_NET_W - 6, "", 1);

    leonos_ui_rect(&ui, icon_x, icon_y, 8, 6, LEONOS_UI_WHITE);
    leonos_ui_rect(&ui, icon_x, icon_y, 8, 1, LEONOS_UI_BLACK);
    leonos_ui_rect(&ui, icon_x, icon_y, 1, 6, LEONOS_UI_BLACK);
    leonos_ui_rect(&ui, icon_x + 7, icon_y, 1, 6, LEONOS_UI_BLACK);
    leonos_ui_rect(&ui, icon_x, icon_y + 5, 8, 1, LEONOS_UI_BLACK);
    leonos_ui_rect(&ui, icon_x + 3, icon_y + 6, 2, 2, LEONOS_UI_BLACK);

    leonos_ui_rect(&ui, icon_x + 11, icon_y + 3, 8, 6, LEONOS_UI_WHITE);
    leonos_ui_rect(&ui, icon_x + 11, icon_y + 3, 8, 1, LEONOS_UI_BLACK);
    leonos_ui_rect(&ui, icon_x + 11, icon_y + 3, 1, 6, LEONOS_UI_BLACK);
    leonos_ui_rect(&ui, icon_x + 18, icon_y + 3, 1, 6, LEONOS_UI_BLACK);
    leonos_ui_rect(&ui, icon_x + 11, icon_y + 8, 8, 1, LEONOS_UI_BLACK);
    leonos_ui_rect(&ui, icon_x + 14, icon_y + 9, 2, 2, LEONOS_UI_BLACK);

    if (taskbar_network_connected) {
        leonos_ui_rect(&ui, icon_x + 7, icon_y + 8, 6, 1, taskbar_network_color);
        leonos_ui_rect(&ui, icon_x + 10, icon_y + 6, 1, 5, taskbar_network_color);
    } else {
        leonos_ui_rect(&ui, icon_x + 8, icon_y + 5, 8, 2, taskbar_network_color);
        leonos_ui_rect(&ui, icon_x + 11, icon_y + 2, 2, 8, taskbar_network_color);
    }
    leonos_ui_rect(&ui, icon_x + 20, icon_y + 11, 4, 4, taskbar_network_color);
}

static void draw_taskbar_inputm(uint32_t tb_y)
{
    const char *label = "EN";
    uint32_t tray_w = desktop_tray_width();
    uint32_t x = fb_w() > tray_w ? fb_w() - tray_w : 0;
    for (uint32_t i = 0; i < desktop_inputm_entry_count; ++i) {
        if (text_eq(desktop_inputm_entries[i].id, desktop_inputm_state.active_id) &&
            desktop_inputm_entries[i].abbreviation[0]) {
            label = desktop_inputm_entries[i].abbreviation;
            break;
        }
    }
    draw_taskbar_plain_button(x + 4U, tb_y + 5U, TASKBAR_INPUTM_W - 6U,
                              label, desktop_inputm_menu_open);
}

static void wallpaper_draw_pixel(uint32_t x, uint32_t y,
                                 uint32_t source_x, uint32_t source_y)
{
    if (source_x >= wallpaper_width) {
        source_x = wallpaper_width - 1;
    }
    if (source_y >= wallpaper_height) {
        source_y = wallpaper_height - 1;
    }
    screen[(uint64_t)y * MAX_FB_W + x] =
        wallpaper_pixels[(uint64_t)source_y * WALLPAPER_MAX_W + source_x] & 0x00ffffffu;
}

static uint32_t wallpaper_scale_x[MAX_FB_W];
static uint32_t wallpaper_scale_y[MAX_FB_H];
static uint32_t wallpaper_map_src_w, wallpaper_map_src_h;
static uint32_t wallpaper_map_target_w, wallpaper_map_target_h;
static uint32_t wallpaper_map_src_x, wallpaper_map_src_y;

static void wallpaper_build_scale_map(uint32_t src_w, uint32_t src_h,
                                      uint32_t target_w, uint32_t target_h,
                                      uint32_t src_x_start, uint32_t src_y_start)
{
    uint32_t i;
    if (wallpaper_map_src_w == src_w && wallpaper_map_src_h == src_h &&
        wallpaper_map_target_w == target_w && wallpaper_map_target_h == target_h &&
        wallpaper_map_src_x == src_x_start && wallpaper_map_src_y == src_y_start) {
        return;
    }
    for (i = 0; i < target_w; ++i) {
        wallpaper_scale_x[i] = src_x_start +
            (uint32_t)(((uint64_t)i * src_w) / target_w);
    }
    for (i = 0; i < target_h; ++i) {
        wallpaper_scale_y[i] = src_y_start +
            (uint32_t)(((uint64_t)i * src_h) / target_h);
    }
    wallpaper_map_src_w = src_w;
    wallpaper_map_src_h = src_h;
    wallpaper_map_target_w = target_w;
    wallpaper_map_target_h = target_h;
    wallpaper_map_src_x = src_x_start;
    wallpaper_map_src_y = src_y_start;
}

static void draw_scaled_cropped_wallpaper_region(struct rect dirty,
                                                 uint32_t target_x, uint32_t target_y,
                                                 uint32_t target_w, uint32_t target_h,
                                                 uint32_t src_w, uint32_t src_h,
                                                 uint32_t src_x_start, uint32_t src_y_start)
{
    if (!target_w || !target_h || !src_w || !src_h ||
        !wallpaper_width || !wallpaper_height) {
        return;
    }
    wallpaper_build_scale_map(src_w, src_h, target_w, target_h,
                              src_x_start, src_y_start);
    int first_y = dirty.y > (int)target_y ? dirty.y : (int)target_y;
    int last_y = dirty.y + dirty.h < (int)(target_y + target_h) ?
                 dirty.y + dirty.h : (int)(target_y + target_h);
    int first_x = dirty.x > (int)target_x ? dirty.x : (int)target_x;
    int last_x = dirty.x + dirty.w < (int)(target_x + target_w) ?
                 dirty.x + dirty.w : (int)(target_x + target_w);
    if (first_y >= last_y || first_x >= last_x) {
        return;
    }
    for (int y = first_y; y < last_y; ++y) {
        uint32_t source_y = wallpaper_scale_y[(uint32_t)y - target_y];
        uint32_t *dst = screen + (uint64_t)y * MAX_FB_W + (uint32_t)first_x;
        for (int x = first_x; x < last_x; ++x) {
            uint32_t source_x = wallpaper_scale_x[(uint32_t)x - target_x];
            if (source_x >= wallpaper_width) {
                source_x = wallpaper_width - 1;
            }
            if (source_y >= wallpaper_height) {
                source_y = wallpaper_height - 1;
            }
            *dst++ = wallpaper_pixels[(uint64_t)source_y * WALLPAPER_MAX_W + source_x] &
                     0x00ffffffu;
        }
    }
}

static void draw_scaled_wallpaper_region(struct rect dirty,
                                         uint32_t target_x, uint32_t target_y,
                                         uint32_t target_w, uint32_t target_h)
{
    draw_scaled_cropped_wallpaper_region(dirty, target_x, target_y,
                                         target_w, target_h,
                                         wallpaper_width, wallpaper_height, 0, 0);
}

static void draw_wallpaper(struct rect dirty)
{
    uint32_t desktop_width = fb_w();
    uint32_t desktop_height = fb_h();
    if (!wallpaper_loaded || !wallpaper_width || !wallpaper_height ||
        !desktop_width || !desktop_height) {
        return;
    }
    if (desktop_wallpaper_mode == LEONOS_WALLPAPER_MODE_STRETCH) {
        draw_scaled_wallpaper_region(dirty, 0, 0, desktop_width, desktop_height);
        return;
    }
    if (desktop_wallpaper_mode == LEONOS_WALLPAPER_MODE_TILE) {
        for (int y = dirty.y; y < dirty.y + dirty.h; ++y) {
            uint32_t source_y = (uint32_t)y % wallpaper_height;
            for (int x = dirty.x; x < dirty.x + dirty.w; ++x) {
                wallpaper_draw_pixel((uint32_t)x, (uint32_t)y,
                                     (uint32_t)x % wallpaper_width, source_y);
            }
        }
        return;
    }
    if (desktop_wallpaper_mode == LEONOS_WALLPAPER_MODE_CENTER) {
        uint32_t target_w = wallpaper_width < desktop_width ? wallpaper_width : desktop_width;
        uint32_t target_h = wallpaper_height < desktop_height ? wallpaper_height : desktop_height;
        uint32_t target_x = desktop_width > target_w ? (desktop_width - target_w) / 2U : 0;
        uint32_t target_y = desktop_height > target_h ? (desktop_height - target_h) / 2U : 0;
        rect_fill((uint32_t)dirty.x, (uint32_t)dirty.y,
                  (uint32_t)dirty.w, (uint32_t)dirty.h, LEONOS_UI_DESKTOP);
        for (int y = dirty.y; y < dirty.y + dirty.h; ++y) {
            if ((uint32_t)y < target_y || (uint32_t)y >= target_y + target_h) {
                continue;
            }
            uint32_t source_y = (uint32_t)y - target_y;
            if (desktop_height < wallpaper_height) {
                source_y += (wallpaper_height - desktop_height) / 2U;
            }
            for (int x = dirty.x; x < dirty.x + dirty.w; ++x) {
                if ((uint32_t)x < target_x || (uint32_t)x >= target_x + target_w) {
                    continue;
                }
                uint32_t source_x = (uint32_t)x - target_x;
                if (desktop_width < wallpaper_width) {
                    source_x += (wallpaper_width - desktop_width) / 2U;
                }
                wallpaper_draw_pixel((uint32_t)x, (uint32_t)y, source_x, source_y);
            }
        }
        return;
    }

    if (desktop_wallpaper_mode == LEONOS_WALLPAPER_MODE_FIT) {
        uint32_t target_w = desktop_width;
        uint32_t target_h = (uint32_t)(((uint64_t)desktop_width * wallpaper_height) /
                                      wallpaper_width);
        if (!target_h) {
            target_h = 1;
        }
        if (target_h > desktop_height) {
            target_h = desktop_height;
            target_w = (uint32_t)(((uint64_t)desktop_height * wallpaper_width) /
                                  wallpaper_height);
            if (!target_w) {
                target_w = 1;
            }
        }
        rect_fill((uint32_t)dirty.x, (uint32_t)dirty.y,
                  (uint32_t)dirty.w, (uint32_t)dirty.h, LEONOS_UI_DESKTOP);
        draw_scaled_wallpaper_region(dirty,
                                     desktop_width > target_w ? (desktop_width - target_w) / 2U : 0,
                                     desktop_height > target_h ? (desktop_height - target_h) / 2U : 0,
                                     target_w, target_h);
        return;
    }

    uint32_t target_h = (uint32_t)(((uint64_t)desktop_width * wallpaper_height) /
                                  wallpaper_width);
    uint32_t source_x_start = 0;
    uint32_t source_y_start = 0;
    uint32_t source_w = wallpaper_width;
    uint32_t source_h = wallpaper_height;
    if (!target_h) {
        target_h = 1;
    }
    if (target_h < desktop_height) {
        source_w = (uint32_t)(((uint64_t)wallpaper_height * desktop_width) /
                              desktop_height);
        if (!source_w) {
            source_w = 1;
        }
        if (source_w < wallpaper_width) {
            source_x_start = (wallpaper_width - source_w) / 2U;
        }
    } else if (target_h > desktop_height) {
        source_h = (uint32_t)(((uint64_t)wallpaper_width * desktop_height) /
                              desktop_width);
        if (!source_h) {
            source_h = 1;
        }
        if (source_h < wallpaper_height) {
            source_y_start = (wallpaper_height - source_h) / 2U;
        }
    }
    draw_scaled_cropped_wallpaper_region(dirty, 0, 0,
                                         desktop_width, desktop_height,
                                         source_w, source_h,
                                         source_x_start, source_y_start);
}

#define put_pixel(x, y, color) cursor_put_pixel((int)(x), (int)(y), (color))

static void draw_custom_cursor(int x, int y)
{
    const uint32_t black = 0x00000000u;
    const uint32_t white = 0x00ffffffu;
    if (desktop_cursor_style == LEONOS_GUI_CURSOR_TEXT) {
        for (uint32_t row = 0; row < FALLBACK_CURSOR_H; ++row) {
            cursor_put_pixel((int)x + 7, (int)y + (int)row, black);
        }
        for (uint32_t col = 4; col < 11; ++col) {
            cursor_put_pixel((int)x + (int)col, (int)y, black);
            cursor_put_pixel((int)x + (int)col, (int)y + FALLBACK_CURSOR_H - 1, black);
        }
        return;
    }
    if (desktop_cursor_style == LEONOS_GUI_CURSOR_CROSSHAIR) {
        for (uint32_t i = 0; i < FALLBACK_CURSOR_H; ++i) {
            cursor_put_pixel((int)x + 7, (int)y + (int)i, black);
            cursor_put_pixel((int)x + (int)i, (int)y + 7, black);
        }
        cursor_put_pixel((int)x + 7, (int)y + 7, white);
        return;
    }
    if (desktop_cursor_style == LEONOS_GUI_CURSOR_MOVE) {
        for (uint32_t i = 3; i < 13; ++i) {
            put_pixel(x + 7, y + i, black);
            put_pixel(x + i, y + 7, black);
        }
        for (uint32_t i = 0; i < 4; ++i) {
            put_pixel(x + 7 - i, y + 3 + i, black);
            put_pixel(x + 7 + i, y + 3 + i, black);
            put_pixel(x + 7 - i, y + 11 - i, black);
            put_pixel(x + 7 + i, y + 11 - i, black);
        }
        return;
    }
    if (desktop_cursor_style == LEONOS_GUI_CURSOR_WAIT) {
        for (uint32_t row = 2; row < 14; ++row) {
            for (uint32_t col = 2; col < 14; ++col) {
                uint32_t dx = col > 7 ? col - 7 : 7 - col;
                uint32_t dy = row > 7 ? row - 7 : 7 - row;
                if ((dx == 5 && dy >= 2 && dy <= 4) ||
                    (dy == 5 && dx >= 2 && dx <= 4) ||
                    (dx == 4 && dy == 4)) {
                    put_pixel(x + col, y + row, black);
                }
            }
        }
        put_pixel(x + 10, y + 3, white);
        return;
    }
    if (desktop_cursor_style == LEONOS_GUI_CURSOR_HAND) {
        for (uint32_t row = 2; row < 11; ++row) {
            put_pixel(x + 7, y + row, black);
        }
        for (uint32_t row = 4; row < 10; ++row) {
            put_pixel(x + 9, y + row, black);
        }
        for (uint32_t row = 7; row < 11; ++row) {
            put_pixel(x + 5, y + row, black);
        }
        for (uint32_t col = 5; col < 11; ++col) {
            put_pixel(x + col, y + 11, black);
            put_pixel(x + col, y + 13, black);
        }
        put_pixel(x + 4, y + 12, black);
        put_pixel(x + 11, y + 12, black);
        return;
    }
    if (desktop_cursor_style == LEONOS_GUI_CURSOR_NO) {
        for (uint32_t i = 2; i < 14; ++i) {
            put_pixel(x + i, y + i, black);
            put_pixel(x + i, y + 15U - i, black);
            if (i > 2 && i < 13) {
                put_pixel(x + i - 1U, y + i, white);
                put_pixel(x + i - 1U, y + 15U - i, white);
            }
        }
        return;
    }
    if (desktop_cursor_style == LEONOS_GUI_CURSOR_HELP) {
        for (uint32_t i = 3; i < 13; ++i) {
            put_pixel(x + i, y + 2, black);
            put_pixel(x + i, y + 13, black);
        }
        for (uint32_t i = 2; i < 14; ++i) {
            put_pixel(x + 2, y + i, black);
            put_pixel(x + 13, y + i, black);
        }
        put_pixel(x + 7, y + 5, black);
        put_pixel(x + 8, y + 5, black);
        put_pixel(x + 7, y + 6, black);
        put_pixel(x + 7, y + 8, black);
        put_pixel(x + 7, y + 10, black);
        return;
    }
    if (desktop_cursor_style == LEONOS_GUI_CURSOR_PROGRESS ||
        desktop_cursor_style == LEONOS_GUI_CURSOR_APP_STARTING) {
        for (uint32_t i = 2; i < 14; ++i) {
            put_pixel(x + 7, y + i, black);
            put_pixel(x + i, y + 7, black);
        }
        put_pixel(x + 7, y + 2, white);
        put_pixel(x + 12, y + 7, white);
        return;
    }
    if (desktop_cursor_style == LEONOS_GUI_CURSOR_SIZE_NS ||
        desktop_cursor_style == LEONOS_GUI_CURSOR_SIZE_WE ||
        desktop_cursor_style == LEONOS_GUI_CURSOR_SIZE_NWSE ||
        desktop_cursor_style == LEONOS_GUI_CURSOR_SIZE_NESW) {
        for (uint32_t i = 3; i < 13; ++i) {
            if (desktop_cursor_style == LEONOS_GUI_CURSOR_SIZE_NS) {
                put_pixel(x + 7, y + i, black);
            } else if (desktop_cursor_style == LEONOS_GUI_CURSOR_SIZE_WE) {
                put_pixel(x + i, y + 7, black);
            } else if (desktop_cursor_style == LEONOS_GUI_CURSOR_SIZE_NWSE) {
                put_pixel(x + i, y + i, black);
            } else {
                put_pixel(x + i, y + 15U - i, black);
            }
        }
        if (desktop_cursor_style == LEONOS_GUI_CURSOR_SIZE_NS) {
            for (uint32_t i = 3; i < 7; ++i) {
                put_pixel(x + 7U - i + 3U, y + i, black);
                put_pixel(x + 7U + i - 3U, y + i, black);
                put_pixel(x + 7U - i + 3U, y + 15U - i, black);
                put_pixel(x + 7U + i - 3U, y + 15U - i, black);
            }
        } else if (desktop_cursor_style == LEONOS_GUI_CURSOR_SIZE_WE) {
            for (uint32_t i = 3; i < 7; ++i) {
                put_pixel(x + i, y + 7U - i + 3U, black);
                put_pixel(x + i, y + 7U + i - 3U, black);
                put_pixel(x + 15U - i, y + 7U - i + 3U, black);
                put_pixel(x + 15U - i, y + 7U + i - 3U, black);
            }
        }
        return;
    }
    if (desktop_cursor_style == LEONOS_GUI_CURSOR_UP) {
        for (uint32_t i = 3; i < 14; ++i) {
            put_pixel(x + 7, y + i, black);
        }
        for (uint32_t i = 3; i < 12; ++i) {
            put_pixel(x + i, y + 5, black);
        }
        return;
    }
}

#undef put_pixel

static void draw_cursor_shape_target(uint32_t x, uint32_t y)
{
    uint32_t style = desktop_cursor_style < CURSOR_STYLE_COUNT
                         ? desktop_cursor_style : LEONOS_GUI_CURSOR_ARROW;
    int draw_x = (int)x - cursor_hotspot_x[style];
    int draw_y = (int)y - cursor_hotspot_y[style];
    if (cursor_bitmap_loaded) {
        uint32_t tile_y = style * CURSOR_TILE_H;
        for (uint32_t row = 0; row < CURSOR_TILE_H; ++row) {
            for (uint32_t col = 0; col < CURSOR_TILE_W; ++col) {
                uint32_t px = cursor_pixels[(tile_y + row) * CURSOR_MAX_W + col];
                if ((px >> 24) != 0 && draw_x + (int)col >= 0 &&
                    draw_y + (int)row >= 0 &&
                    draw_x + (int)col < (int)fb_w() &&
                    draw_y + (int)row < (int)fb_h()) {
                    cursor_put_pixel(draw_x + (int)col, draw_y + (int)row,
                                     px & 0x00ffffffu);
                }
            }
        }
        return;
    }
    if (style != LEONOS_GUI_CURSOR_ARROW) {
        desktop_cursor_style = style;
        /* draw_x/draw_y already apply the style hotspot. Keep the fallback
         * shape anchored at that hotspot so every cursor style stays inside
         * the same compositing tile. */
        draw_custom_cursor(draw_x, draw_y);
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

void draw_cursor_shape(uint32_t x, uint32_t y)
{
    uint32_t *saved_target = cursor_target;
    int saved_target_x = cursor_target_x;
    int saved_target_y = cursor_target_y;
    uint32_t saved_target_width = cursor_target_width;
    uint32_t saved_target_height = cursor_target_height;
    uint32_t saved_target_stride = cursor_target_stride;

    cursor_target = screen;
    cursor_target_x = 0;
    cursor_target_y = 0;
    cursor_target_width = fb_w();
    cursor_target_height = fb_h();
    cursor_target_stride = MAX_FB_W;
    draw_cursor_shape_target(x, y);

    cursor_target = saved_target;
    cursor_target_x = saved_target_x;
    cursor_target_y = saved_target_y;
    cursor_target_width = saved_target_width;
    cursor_target_height = saved_target_height;
    cursor_target_stride = saved_target_stride;
}

void redraw_region(struct rect dirty)
{
    dirty = rect_clip(dirty);
    if (dirty.w <= 0 || dirty.h <= 0) {
        return;
    }
    leonos_ui_set_clip(&ui, dirty.x, dirty.y, (uint32_t)dirty.w, (uint32_t)dirty.h);

    if (wallpaper_loaded) {
        draw_wallpaper(dirty);
    } else {
        rect_fill((uint32_t)dirty.x, (uint32_t)dirty.y, (uint32_t)dirty.w, (uint32_t)dirty.h,
                  LEONOS_UI_DESKTOP);
    }

    if (active_window_is_fullscreen()) {
        if (rect_intersects(dirty, window_rect((uint8_t)active_window))) {
            draw_window((uint8_t)active_window);
        }
        leonos_ui_clear_clip(&ui);
        leonos_ui_cursor_clear(&ui);
        return;
    }

    draw_desktop_items(dirty);

    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (!windows[z_order[i]].anim && rect_intersects(dirty, window_rect(z_order[i]))) {
            draw_window(z_order[i]);
        }
    }

    draw_snap_preview();

    uint32_t tb_y = taskbar_y();
    if (desktop_taskbar_visible && (uint32_t)(dirty.y + dirty.h) >= tb_y) {
        struct rect start_rect = rect_make(6, (int)tb_y + 5, 86, LEONOS_UI_BUTTON_H);
        struct rect network_rect = rect_make(0, 0, 0, 0);
        struct rect clock_rect = rect_make(0, 0, 0, 0);
        struct rect inputm_rect = rect_make(0, 0, 0, 0);
        if (desktop_service_network_icon && fb_w() >= desktop_tray_width() + 8U) {
            uint32_t network_x = fb_w() -
                                  (desktop_service_rtc_clock ? TASKBAR_CLOCK_W : 0U) -
                                  TASKBAR_NET_W;
            network_rect = rect_make((int)network_x + 4, (int)tb_y + 5,
                                     TASKBAR_NET_W - 6, LEONOS_UI_BUTTON_H);
        }
        if (desktop_service_rtc_clock && fb_w() >= TASKBAR_CLOCK_W + 8U) {
            clock_rect = rect_make((int)fb_w() - TASKBAR_CLOCK_W + 4,
                                   (int)tb_y + 5, TASKBAR_CLOCK_W - 10,
                                   LEONOS_UI_BUTTON_H);
        }
        {
            uint32_t tray_w = desktop_tray_width();
            uint32_t inputm_x = fb_w() > tray_w ? fb_w() - tray_w : 0;
            inputm_rect = rect_make((int)inputm_x + 4, (int)tb_y + 5,
                                    TASKBAR_INPUTM_W - 6, LEONOS_UI_BUTTON_H);
        }
        leonos_ui_taskbar(&ui, tb_y, TASKBAR_H);
        if (rect_intersects(dirty, start_rect)) {
            draw_taskbar_plain_button(6, tb_y + 5, 86,
                                      leonos_i18n("Start", "开始"),
                                      start_menu_open);
        }
        uint32_t x = 106;
        uint32_t button_w = taskbar_button_width(running_window_count());
        for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
            if (windows[i].visible &&
                (windows[i].flags & LEONOS_GUI_WINDOW_HIDE_TASKBAR) == 0 &&
                button_w > 0) {
                struct rect button_rect = rect_make((int)x, (int)tb_y + 5,
                                                    button_w > 8 ? button_w - 8 : button_w,
                                                    LEONOS_UI_BUTTON_H);
                if (rect_intersects(dirty, button_rect)) {
                    draw_taskbar_button(i, x, tb_y + 5, button_w);
                }
                x += button_w;
            }
        }
        if (rect_intersects(dirty, network_rect)) {
            draw_taskbar_network_icon(tb_y);
        }
        if (rect_intersects(dirty, clock_rect)) {
            draw_taskbar_clock(tb_y);
        }
        if (rect_intersects(dirty, inputm_rect)) {
            draw_taskbar_inputm(tb_y);
        }
    }

    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[z_order[i]].anim) {
            draw_window(z_order[i]);
        }
    }

    draw_start_menu();
    draw_desktop_context_menu();
    draw_alt_tab_overlay();
    draw_power_confirm();
    draw_desktop_shortcut_input();
    draw_desktop_message();
    draw_inputm_overlay();
    leonos_ui_clear_clip(&ui);
    /* Desktop controls are drawn on the framebuffer, not submitted as a GUI window. */
    leonos_ui_cursor_clear(&ui);
}

void draw_power_confirm(void)
{
    enum { W = 360, H = 150 };
    uint32_t x;
    uint32_t y;
    const char *title;
    const char *message;
    if (!power_confirm_action) {
        return;
    }
    x = fb_w() > W ? (fb_w() - W) / 2 : 0;
    y = fb_h() > H ? (fb_h() - H) / 2 : 0;
    title = power_confirm_action == POWER_CONFIRM_REBOOT
                ? leonos_i18n("Confirm Restart", "确认重启")
                : leonos_i18n("Confirm Shut Down", "确认关机");
    message = power_confirm_action == POWER_CONFIRM_REBOOT
                  ? leonos_i18n("Restart LeonOS now?", "是否立即重启 LeonOS？")
                  : leonos_i18n("Shut down LeonOS now?", "是否立即关闭 LeonOS？");
    rect_fill_i((int)x + 5, (int)y + 5, W, H, 0x00404040);
    leonos_ui_dialog(&ui, x, y, W, H, title);
    leonos_ui_text_clipped(&ui, x + 20, y + 50, W - 40, message,
                           LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_button(&ui, x + W - 168, y + H - 38, 72, LEONOS_UI_BUTTON_H,
                     leonos_i18n("Yes", "是"), 0);
    leonos_ui_button(&ui, x + W - 88, y + H - 38, 72, LEONOS_UI_BUTTON_H,
                     leonos_i18n("No", "否"), 0);
}

static void flush_pixels(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                         uint32_t stride, const uint32_t *pixels)
{
    if (!width || !height || !pixels) {
        return;
    }
    if (desktop_scale <= 1) {
        leonos_fb_blit(x, y, width, height, stride, pixels);
        return;
    }
    uint32_t scale = desktop_scale;
    static uint32_t scaled[384 * 384];
    const uint32_t max_out_w = 384;
    const uint32_t max_out_h = 384;
    for (uint32_t chunk_y = 0; chunk_y < height;) {
        uint32_t logical_h = height - chunk_y;
        if (logical_h * scale > max_out_h) {
            logical_h = max_out_h / scale;
        }
        if (!logical_h) {
            logical_h = 1;
        }
        for (uint32_t chunk_x = 0; chunk_x < width;) {
            uint32_t logical_w = width - chunk_x;
            if (logical_w * scale > max_out_w) {
                logical_w = max_out_w / scale;
            }
            if (!logical_w) {
                logical_w = 1;
            }
            for (uint32_t y = 0; y < logical_h; ++y) {
                const uint32_t *src = pixels +
                    (uint64_t)(chunk_y + y) * stride + chunk_x;
                for (uint32_t sy = 0; sy < scale; ++sy) {
                    uint32_t *dst = scaled + (uint64_t)(y * scale + sy) * max_out_w;
                    for (uint32_t x = 0; x < logical_w; ++x) {
                        for (uint32_t sx = 0; sx < scale; ++sx) {
                            dst[x * scale + sx] = src[x];
                        }
                    }
                }
            }
            leonos_fb_blit((x + chunk_x) * scale,
                           (y + chunk_y) * scale,
                           logical_w * scale, logical_h * scale,
                           max_out_w, scaled);
            chunk_x += logical_w;
        }
        chunk_y += logical_h;
    }
}

static void flush_base_region(struct rect dirty)
{
    dirty = rect_clip(dirty);
    if (dirty.w <= 0 || dirty.h <= 0) {
        return;
    }
    flush_pixels((uint32_t)dirty.x, (uint32_t)dirty.y,
                 (uint32_t)dirty.w, (uint32_t)dirty.h, MAX_FB_W,
                 screen + (uint64_t)dirty.y * MAX_FB_W + dirty.x);
}

static int build_cursor_composite(struct rect *raw_out, struct rect *clip_out)
{
    struct rect raw;
    struct rect clip;

    if (!cursor_visible || leonos_gui_mouse_visible() <= 0) {
        return 0;
    }
    raw = cursor_rect_for_style(cursor_x, cursor_y, desktop_cursor_style);
    clip = rect_clip(raw);
    if (clip.w <= 0 || clip.h <= 0) {
        return 0;
    }
    for (uint32_t row = 0; row < CURSOR_TILE_H; ++row) {
        for (uint32_t col = 0; col < CURSOR_TILE_W; ++col) {
            int x = raw.x + (int)col;
            int y = raw.y + (int)row;
            cursor_composite[row * CURSOR_TILE_W + col] =
                (x >= 0 && y >= 0 && x < (int)fb_w() && y < (int)fb_h())
                    ? screen[(uint64_t)y * MAX_FB_W + (uint32_t)x]
                    : 0;
        }
    }
    cursor_target = cursor_composite;
    cursor_target_x = raw.x;
    cursor_target_y = raw.y;
    cursor_target_width = CURSOR_TILE_W;
    cursor_target_height = CURSOR_TILE_H;
    cursor_target_stride = CURSOR_TILE_W;
    draw_cursor_shape_target(cursor_x, cursor_y);
    cursor_target = 0;
    if (raw_out) {
        *raw_out = raw;
    }
    if (clip_out) {
        *clip_out = clip;
    }
    return 1;
}

static void flush_cursor_overlay(struct rect dirty)
{
    struct rect cursor_rect;
    struct rect cursor_clip;
    const uint32_t *source;

    if (!build_cursor_composite(&cursor_rect, &cursor_clip) ||
        !rect_intersects(dirty, cursor_rect)) {
        return;
    }
    source = cursor_composite +
             (uint32_t)(cursor_clip.y - cursor_rect.y) * CURSOR_TILE_W +
             (uint32_t)(cursor_clip.x - cursor_rect.x);
    flush_pixels((uint32_t)cursor_clip.x, (uint32_t)cursor_clip.y,
                 (uint32_t)cursor_clip.w, (uint32_t)cursor_clip.h,
                 CURSOR_TILE_W, source);
}

static int flush_small_composited_region(struct rect dirty)
{
    struct rect cursor_rect;
    uint32_t width = (uint32_t)dirty.w;
    uint32_t height = (uint32_t)dirty.h;

    if (width > CURSOR_FRAME_MAX_W || height > CURSOR_FRAME_MAX_H) {
        return 0;
    }
    for (uint32_t row = 0; row < height; ++row) {
        const uint32_t *source = screen +
            (uint64_t)(dirty.y + (int)row) * MAX_FB_W + dirty.x;
        for (uint32_t col = 0; col < width; ++col) {
            cursor_frame[row * CURSOR_FRAME_MAX_W + col] = source[col];
        }
    }
    if (cursor_visible && leonos_gui_mouse_visible() > 0) {
        cursor_rect = cursor_rect_for_style(cursor_x, cursor_y,
                                            desktop_cursor_style);
        if (rect_intersects(dirty, cursor_rect)) {
            cursor_target = cursor_frame;
            cursor_target_x = dirty.x;
            cursor_target_y = dirty.y;
            cursor_target_width = width;
            cursor_target_height = height;
            cursor_target_stride = CURSOR_FRAME_MAX_W;
            draw_cursor_shape_target(cursor_x, cursor_y);
            cursor_target = 0;
        }
    }
    flush_pixels((uint32_t)dirty.x, (uint32_t)dirty.y,
                 width, height, CURSOR_FRAME_MAX_W, cursor_frame);
    return 1;
}

void flush_region(struct rect dirty)
{
    dirty = rect_clip(dirty);
    if (dirty.w <= 0 || dirty.h <= 0) {
        return;
    }
    /* Small pointer and clock damages can be submitted in one composite
     * command, avoiding two synchronous VMware updates per mouse sample. */
    if (flush_small_composited_region(dirty)) {
        return;
    }
    /* Publish the clean base first, then overlay the current cursor. The
     * damage union contains the old cursor rectangle, so moving the pointer
     * naturally erases its previous pixels without a saved background. */
    flush_base_region(dirty);
    flush_cursor_overlay(dirty);
}

void repaint_and_flush(struct rect dirty)
{
    dirty = rect_clip(dirty);
    redraw_region(dirty);
    flush_region(dirty);
}

void repaint_cursor_and_flush(struct rect dirty)
{
    repaint_and_flush(dirty);
}

void desktop_queue_damage(struct rect dirty)
{
    /* Coalesce non-cursor updates until the event batch is drained. */
    dirty = rect_clip(dirty);
    if (dirty.w <= 0 || dirty.h <= 0) {
        return;
    }
    if (desktop_damage_pending) {
        desktop_damage_rect = rect_union(desktop_damage_rect, dirty);
    } else {
        desktop_damage_rect = dirty;
        desktop_damage_pending = 1;
    }
    desktop_damage_cursor_only = 0;
}

void desktop_queue_cursor_damage(struct rect dirty)
{
    dirty = rect_clip(dirty);
    if (dirty.w <= 0 || dirty.h <= 0) {
        return;
    }
    if (desktop_damage_pending) {
        desktop_damage_rect = rect_union(desktop_damage_rect, dirty);
    } else {
        desktop_damage_rect = dirty;
        desktop_damage_pending = 1;
        desktop_damage_cursor_only = 1;
    }
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
    desktop_damage_pending = 0;
    desktop_damage_cursor_only = 0;
    desktop_damage_rect = rect_make(0, 0, 0, 0);
    full_redraw_pending =
        (start_menu_animating || desktop_context_menu_animating ||
         desktop_window_animation_active()) ? 1 : 0;
}

int hit_window(uint32_t x, uint32_t y)
{
    /* The taskbar is composited above windows, so underlying windows must not
     * receive hover or mouse events through the taskbar area. */
    if (desktop_taskbar_visible && y >= taskbar_y()) {
        return -1;
    }
    for (int zi = MAX_WINDOWS - 1; zi >= 0; --zi) {
        uint8_t id = z_order[zi];
        struct desktop_window *w = &windows[id];
        if (w->visible && !w->minimized && w->anim != WINDOW_ANIM_CLOSE &&
            hit_rect(x, y, w->x, w->y, w->width, w->height)) {
            return id;
        }
    }
    return -1;
}
