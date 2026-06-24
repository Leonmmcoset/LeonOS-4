#ifndef LEONOS_UI_H
#define LEONOS_UI_H

#include <stdint.h>

#define LEONOS_UI_BLACK 0x00000000u
#define LEONOS_UI_WHITE 0x00ffffffu
#define LEONOS_UI_GRAY 0x00c0c0c0u
#define LEONOS_UI_LIGHT 0x00dfdfdfu
#define LEONOS_UI_DARK 0x00808080u
#define LEONOS_UI_ACTIVE_TITLE 0x00000080u
#define LEONOS_UI_INACTIVE_TITLE 0x00808080u
#define LEONOS_UI_DESKTOP 0x00008080u

#define LEONOS_UI_TITLEBAR_H 26u
#define LEONOS_UI_TASKBAR_H 34u
#define LEONOS_UI_BUTTON_H 24u
#define LEONOS_UI_WINDOW_BUTTON_W 18u
#define LEONOS_UI_WINDOW_BUTTON_H 20u

#define LEONOS_UI_BUTTON_PRESSED 0x01u
#define LEONOS_UI_BUTTON_ACTIVE 0x02u
#define LEONOS_UI_BUTTON_DISABLED 0x04u
#define LEONOS_UI_WINDOW_ACTIVE 0x01u
#define LEONOS_UI_MENU_SEPARATOR 0x01u
#define LEONOS_UI_MENU_SELECTED 0x02u

struct leonos_ui_surface {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

struct leonos_ui_rect {
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
};

struct leonos_ui_window_parts {
    struct leonos_ui_rect titlebar;
    struct leonos_ui_rect body;
    struct leonos_ui_rect minimize;
    struct leonos_ui_rect maximize;
    struct leonos_ui_rect close;
};

void leonos_ui_bind(struct leonos_ui_surface *surface, uint32_t *pixels,
                    uint32_t width, uint32_t height, uint32_t stride);
void leonos_ui_pixel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t color);
void leonos_ui_rect(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h, uint32_t color);
void leonos_ui_text(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    const char *text, uint32_t fg, uint32_t bg);
void leonos_ui_text_transparent(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                const char *text, uint32_t fg);
void leonos_ui_bevel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t fill, uint32_t flags);
void leonos_ui_inset(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t fill);
void leonos_ui_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, const char *label, uint32_t flags);
void leonos_ui_window_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                             char label, uint32_t flags);
void leonos_ui_window(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, const char *title, uint32_t flags,
                      struct leonos_ui_window_parts *parts);
void leonos_ui_window_ex(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h, const char *title, char maximize_label,
                         uint32_t flags, struct leonos_ui_window_parts *parts);
void leonos_ui_taskbar(struct leonos_ui_surface *surface, uint32_t y, uint32_t h);
void leonos_ui_taskbar_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                              uint32_t w, const char *label, uint32_t flags);
void leonos_ui_menu(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h);
void leonos_ui_menu_item(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t w, const char *label, uint32_t flags);
void leonos_ui_panel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t color);
void leonos_ui_checkbox(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        const char *label, int checked, uint32_t flags);
void leonos_ui_progress(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h, uint32_t value, uint32_t max);
void leonos_ui_text_field(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                          uint32_t w, const char *text, uint32_t flags);
void leonos_ui_list_header(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           uint32_t w, const char *label);
void leonos_ui_list_row(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, const char *text, uint32_t flags);

#endif
