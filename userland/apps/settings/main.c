#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define SETTINGS_W 480
#define SETTINGS_H 300
#define SETTINGS_DROPDOWN_ROW_H 28
#define SETTINGS_MODE_COUNT 5
#define SETTINGS_SCALE_COUNT 3
#define T(en, zh) leonos_i18n((en), (zh))

enum {
    DROP_NONE = 0,
    DROP_RESOLUTION = 1,
    DROP_SCALE = 2,
    DROP_LANGUAGE = 3,
};

static uint32_t pixels[SETTINGS_W * SETTINGS_H];
static struct leonos_display_state display_state;
static uint8_t active_drop;
static char status_text[128] = "Ready";

static const char *mode_labels[SETTINGS_MODE_COUNT] = {
    "1920 x 1080",
    "1600 x 900",
    "1280 x 800",
    "1280 x 720",
    "1024 x 768",
};

static const uint32_t mode_widths[SETTINGS_MODE_COUNT] = {1920, 1600, 1280, 1280, 1024};
static const uint32_t mode_heights[SETTINGS_MODE_COUNT] = {1080, 900, 800, 720, 768};
static const uint32_t scale_values[SETTINGS_SCALE_COUNT] = {1, 2, 3};
static const char *scale_labels[SETTINGS_SCALE_COUNT] = {"1x", "2x", "3x"};

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (*pos + 1 < cap) {
        buf[(*pos)++] = ch;
        buf[*pos] = 0;
    }
}

static void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        append_char(buf, pos, cap, *text++);
    }
}

static void append_dec(char *buf, uint32_t *pos, uint32_t cap, uint32_t value)
{
    char tmp[16];
    uint32_t n = 0;
    if (value == 0) {
        append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) {
        append_char(buf, pos, cap, tmp[--n]);
    }
}

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static int mode_supported(uint32_t mode, uint32_t scale_index)
{
    uint32_t scale;
    if (mode >= SETTINGS_MODE_COUNT || scale_index >= SETTINGS_SCALE_COUNT) {
        return 0;
    }
    scale = scale_values[scale_index];
    return mode_widths[mode] * scale <= display_state.fb_width &&
           mode_heights[mode] * scale <= display_state.fb_height;
}

static int scale_supported(uint32_t mode, uint32_t scale_index)
{
    return mode_supported(mode, scale_index);
}

static void refresh_display_state(void)
{
    int ret = leonos_display_get_state(&display_state);
    if (ret <= 0) {
        display_state.fb_width = 1920;
        display_state.fb_height = 1080;
        display_state.logical_width = 1280;
        display_state.logical_height = 800;
        display_state.scale = 1;
        display_state.mode_index = 2;
        display_state.scale_index = 0;
        display_state.pending_confirm = 0;
        display_state.confirm_remaining_ms = 0;
    }
}

static void request_display(uint32_t action, uint32_t mode, uint32_t scale)
{
    struct leonos_display_request request;
    request.action = action;
    request.mode_index = mode;
    request.scale_index = scale;
    (void)leonos_display_request(&request);
}

static void set_status_mode(void)
{
    uint32_t pos = 0;
    status_text[0] = 0;
    append_text(status_text, &pos, sizeof(status_text), T("Display ", "显示 "));
    append_dec(status_text, &pos, sizeof(status_text), display_state.logical_width);
    append_char(status_text, &pos, sizeof(status_text), 'x');
    append_dec(status_text, &pos, sizeof(status_text), display_state.logical_height);
    append_text(status_text, &pos, sizeof(status_text), " @ ");
    append_dec(status_text, &pos, sizeof(status_text), display_state.scale);
    append_char(status_text, &pos, sizeof(status_text), 'x');
}

static const char *mode_label(void)
{
    return display_state.mode_index < SETTINGS_MODE_COUNT
               ? mode_labels[display_state.mode_index]
               : mode_labels[0];
}

static const char *scale_label(void)
{
    return display_state.scale_index < SETTINGS_SCALE_COUNT
               ? scale_labels[display_state.scale_index]
               : scale_labels[0];
}

static const char *language_label(void)
{
    return leonos_i18n_language() == LEONOS_LANG_ZH ? "中文" : "English";
}

static void draw_settings(struct leonos_ui_surface *ui)
{
    char line[128];
    uint32_t pos = 0;
    struct leonos_ui_dropdown_item mode_items[SETTINGS_MODE_COUNT];
    struct leonos_ui_dropdown_item scale_items[SETTINGS_SCALE_COUNT];
    struct leonos_ui_dropdown_item lang_items[2];

    for (uint32_t i = 0; i < SETTINGS_MODE_COUNT; ++i) {
        mode_items[i].label = mode_labels[i];
        mode_items[i].id = i;
        mode_items[i].flags = mode_supported(i, display_state.scale_index)
                                  ? 0
                                  : LEONOS_UI_MENU_DISABLED;
    }
    for (uint32_t i = 0; i < SETTINGS_SCALE_COUNT; ++i) {
        scale_items[i].label = scale_labels[i];
        scale_items[i].id = i;
        scale_items[i].flags = scale_supported(display_state.mode_index, i)
                                   ? 0
                                   : LEONOS_UI_MENU_DISABLED;
    }
    lang_items[0] = (struct leonos_ui_dropdown_item){"English", LEONOS_LANG_EN, 0};
    lang_items[1] = (struct leonos_ui_dropdown_item){"中文", LEONOS_LANG_ZH, 0};

    leonos_ui_rect(ui, 0, 0, SETTINGS_W, SETTINGS_H, LEONOS_UI_WHITE);
    leonos_ui_dialog(ui, 0, 0, SETTINGS_W, SETTINGS_H, T("Settings", "设置"));
    leonos_ui_text(ui, 18, 42, T("Display", "显示"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);

    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("Framebuffer ", "帧缓冲 "));
    append_dec(line, &pos, sizeof(line), display_state.fb_width);
    append_char(line, &pos, sizeof(line), 'x');
    append_dec(line, &pos, sizeof(line), display_state.fb_height);
    append_text(line, &pos, sizeof(line), "  ");
    append_text(line, &pos, sizeof(line), T("Desktop ", "桌面 "));
    append_dec(line, &pos, sizeof(line), display_state.logical_width);
    append_char(line, &pos, sizeof(line), 'x');
    append_dec(line, &pos, sizeof(line), display_state.logical_height);
    leonos_ui_text_clipped(ui, 18, 66, SETTINGS_W - 36, line, LEONOS_UI_DARK, LEONOS_UI_GRAY);

    leonos_ui_text(ui, 24, 102, T("Resolution", "分辨率"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 132, 96, 180, mode_label(), active_drop == DROP_RESOLUTION, 0);
    leonos_ui_text(ui, 24, 138, T("Scale", "缩放"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 132, 132, 180, scale_label(), active_drop == DROP_SCALE, 0);
    leonos_ui_text(ui, 24, 174, T("Language", "语言"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 132, 168, 180, language_label(), active_drop == DROP_LANGUAGE, 0);

    if (display_state.pending_confirm) {
        uint32_t seconds = (display_state.confirm_remaining_ms + 999) / 1000;
        pos = 0;
        line[0] = 0;
        append_text(line, &pos, sizeof(line), T("Keep these display settings? Reverting in ",
                                                "保留这些显示设置？将在 "));
        append_dec(line, &pos, sizeof(line), seconds);
        append_text(line, &pos, sizeof(line), T("s", " 秒后还原"));
        leonos_ui_panel(ui, 24, 214, SETTINGS_W - 48, 54, LEONOS_UI_LIGHT);
        leonos_ui_text_clipped(ui, 34, 224, SETTINGS_W - 68, line, LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
        leonos_ui_button(ui, 34, 244, 82, LEONOS_UI_BUTTON_H, T("Keep", "保留"), 0);
        leonos_ui_button(ui, 126, 244, 82, LEONOS_UI_BUTTON_H, T("Revert", "还原"), 0);
    } else {
        leonos_ui_text_clipped(ui, 24, 224, SETTINGS_W - 48,
                               T("Display changes apply temporarily until kept.",
                                 "显示更改会临时应用，保留后保存。"),
                               LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }

    leonos_ui_statusbar(ui, SETTINGS_H - 28, 28, status_text);

    if (active_drop == DROP_LANGUAGE) {
        leonos_ui_dropdown(ui, 132, 192, 180, lang_items, 2,
                           (uint32_t)leonos_i18n_language(), SETTINGS_DROPDOWN_ROW_H, 1000);
    } else if (active_drop == DROP_SCALE) {
        leonos_ui_dropdown(ui, 132, 156, 180, scale_items, SETTINGS_SCALE_COUNT,
                           display_state.scale_index, SETTINGS_DROPDOWN_ROW_H, 1000);
    } else if (active_drop == DROP_RESOLUTION) {
        leonos_ui_dropdown(ui, 132, 120, 180, mode_items, SETTINGS_MODE_COUNT,
                           display_state.mode_index, SETTINGS_DROPDOWN_ROW_H, 1000);
    }
}

static int handle_open_dropdown_hit(int32_t x, int32_t y)
{
    uint32_t id = 0;
    struct leonos_ui_dropdown_item mode_items[SETTINGS_MODE_COUNT];
    struct leonos_ui_dropdown_item scale_items[SETTINGS_SCALE_COUNT];
    struct leonos_ui_dropdown_item lang_items[2];
    for (uint32_t i = 0; i < SETTINGS_MODE_COUNT; ++i) {
        mode_items[i].label = mode_labels[i];
        mode_items[i].id = i;
        mode_items[i].flags = mode_supported(i, display_state.scale_index)
                                  ? 0
                                  : LEONOS_UI_MENU_DISABLED;
    }
    for (uint32_t i = 0; i < SETTINGS_SCALE_COUNT; ++i) {
        scale_items[i].label = scale_labels[i];
        scale_items[i].id = i;
        scale_items[i].flags = scale_supported(display_state.mode_index, i)
                                   ? 0
                                   : LEONOS_UI_MENU_DISABLED;
    }
    lang_items[0] = (struct leonos_ui_dropdown_item){"English", LEONOS_LANG_EN, 0};
    lang_items[1] = (struct leonos_ui_dropdown_item){"中文", LEONOS_LANG_ZH, 0};

    if (active_drop == DROP_RESOLUTION &&
        leonos_ui_dropdown_hit(x, y, 132, 120, 180, mode_items, SETTINGS_MODE_COUNT,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (id < SETTINGS_MODE_COUNT && mode_supported(id, display_state.scale_index)) {
            request_display(LEONOS_DISPLAY_REQUEST_APPLY, id, display_state.scale_index);
            copy_text(status_text, sizeof(status_text), T("Resolution changed", "分辨率已更改"));
        }
        return 1;
    }
    if (active_drop == DROP_SCALE &&
        leonos_ui_dropdown_hit(x, y, 132, 156, 180, scale_items, SETTINGS_SCALE_COUNT,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (id < SETTINGS_SCALE_COUNT && scale_supported(display_state.mode_index, id)) {
            request_display(LEONOS_DISPLAY_REQUEST_APPLY, display_state.mode_index, id);
            copy_text(status_text, sizeof(status_text), T("Scale changed", "缩放已更改"));
        }
        return 1;
    }
    if (active_drop == DROP_LANGUAGE &&
        leonos_ui_dropdown_hit(x, y, 132, 192, 180, lang_items, 2,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (id == LEONOS_LANG_EN || id == LEONOS_LANG_ZH) {
            (void)leonos_i18n_set_language((int)id);
            request_display(LEONOS_DISPLAY_REQUEST_REFRESH, display_state.mode_index,
                            display_state.scale_index);
            copy_text(status_text, sizeof(status_text), T("Language changed", "语言已更改"));
        }
        return 1;
    }
    return 0;
}

static void handle_click(int32_t x, int32_t y)
{
    if (active_drop && handle_open_dropdown_hit(x, y)) {
        return;
    }
    active_drop = DROP_NONE;
    if (hit_rect_i(x, y, 132, 96, 180, LEONOS_FONT_H + 8)) {
        active_drop = DROP_RESOLUTION;
        return;
    }
    if (hit_rect_i(x, y, 132, 132, 180, LEONOS_FONT_H + 8)) {
        active_drop = DROP_SCALE;
        return;
    }
    if (hit_rect_i(x, y, 132, 168, 180, LEONOS_FONT_H + 8)) {
        active_drop = DROP_LANGUAGE;
        return;
    }
    if (display_state.pending_confirm && hit_rect_i(x, y, 34, 244, 82, LEONOS_UI_BUTTON_H)) {
        request_display(LEONOS_DISPLAY_REQUEST_KEEP, display_state.mode_index, display_state.scale_index);
        copy_text(status_text, sizeof(status_text), T("Display settings saved", "显示设置已保存"));
        return;
    }
    if (display_state.pending_confirm && hit_rect_i(x, y, 126, 244, 82, LEONOS_UI_BUTTON_H)) {
        request_display(LEONOS_DISPLAY_REQUEST_REVERT, display_state.mode_index, display_state.scale_index);
        copy_text(status_text, sizeof(status_text), T("Display settings reverted", "显示设置已还原"));
    }
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    unsigned long last_refresh = 0;

    puts("[settings.elf] settings starting");
    window_id = leonos_gui_create_app_window_ex(T("Settings", "设置"),
                                                T("System settings", "系统设置"),
                                                SETTINGS_W, SETTINGS_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[settings.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, SETTINGS_W, SETTINGS_H, SETTINGS_W);
    refresh_display_state();
    set_status_mode();
    draw_settings(&ui);
    leonos_gui_present_window((uint32_t)window_id, SETTINGS_W, SETTINGS_H, SETTINGS_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                handle_click(event.x, event.y);
                refresh_display_state();
                draw_settings(&ui);
                leonos_gui_present_window((uint32_t)window_id, SETTINGS_W, SETTINGS_H, SETTINGS_W, pixels);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed && event.keycode == 1) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_FOCUS || event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                refresh_display_state();
                draw_settings(&ui);
                leonos_gui_present_window((uint32_t)window_id, SETTINGS_W, SETTINGS_H, SETTINGS_W, pixels);
            }
        } else {
            unsigned long now = leonos_uptime_ms();
            if (now - last_refresh >= 250) {
                refresh_display_state();
                draw_settings(&ui);
                leonos_gui_present_window((uint32_t)window_id, SETTINGS_W, SETTINGS_H, SETTINGS_W, pixels);
                last_refresh = now;
            }
            sleep_ms(20);
        }
    }
}
