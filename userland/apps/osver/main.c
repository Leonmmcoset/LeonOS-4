#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/png.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define OSVER_W 720
#define OSVER_H 460
#define OSVER_LOGO_PATH "0:/system/resources/logo.png"
#define OSVER_LOGO_BOX 196U
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[OSVER_W * OSVER_H];
static struct leonos_system_info info;
static char status_text[96];
static uint32_t *logo_pixels;
static uint32_t logo_width;
static uint32_t logo_height;
static uint32_t logo_clicks;
static unsigned long logo_click_window_ms;

static void copy_text(char *dst, uint32_t cap, const char *src);

static int osver_logo_rect(uint32_t *left, uint32_t *top,
                           uint32_t *width, uint32_t *height)
{
    const uint32_t box_x = 16U + 35U;
    const uint32_t box_y = 88U + 58U;
    const uint32_t box_size = OSVER_LOGO_BOX - 16U;
    uint32_t draw_w;
    uint32_t draw_h;
    if (!logo_pixels || !logo_width || !logo_height) return 0;
    draw_w = box_size;
    draw_h = (uint32_t)(((uint64_t)box_size * logo_height) / logo_width);
    if (draw_h > box_size) {
        draw_h = box_size;
        draw_w = (uint32_t)(((uint64_t)box_size * logo_width) / logo_height);
    }
    if (!draw_w || !draw_h) return 0;
    if (left) *left = box_x + (box_size - draw_w) / 2U;
    if (top) *top = box_y + (box_size - draw_h) / 2U;
    if (width) *width = draw_w;
    if (height) *height = draw_h;
    return 1;
}

static void osver_kernel_debug_click(uint32_t x, uint32_t y)
{
    uint32_t left;
    uint32_t top;
    uint32_t width;
    uint32_t height;
    const uint32_t now = (uint32_t)leonos_uptime_ms();
    uint32_t flags = 0;
    if (!osver_logo_rect(&left, &top, &width, &height) ||
        x < left || y < top || x >= left + width || y >= top + height) {
        logo_clicks = 0;
        logo_click_window_ms = now;
        return;
    }
    if (logo_clicks == 0U || now - logo_click_window_ms > 2000U) {
        logo_clicks = 0U;
        logo_click_window_ms = now;
    }
    ++logo_clicks;
    if (logo_clicks < 5U) return;
    logo_clicks = 0U;
    if (leonos_kernel_debug_get_state(&flags) < 0 ||
        (flags & LEONOS_KERNEL_DEBUG_STATE_ENABLED) == 0U) {
        if (leonos_kernel_debug_set_enabled(1) == 0) {
            copy_text(status_text, sizeof(status_text),
                      T("Kernel debug mode enabled", "内核调试模式已启用"));
            (void)leonos_ui_show_message_box(
                T("Kernel debug mode", "内核调试模式"),
                T("Kernel debug mode enabled. Use Start > Power to reboot into it.",
                  "内核调试模式已启用。请从开始菜单的电源页面重启进入。"),
                T("OK", "确定"));
        } else {
            (void)leonos_ui_show_message_box(
                T("Kernel debug mode", "内核调试模式"),
                T("Could not persist kernel debug mode.", "无法保存内核调试模式状态。"),
                T("OK", "确定"));
        }
    }
}

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

static void draw_logo(struct leonos_ui_surface *ui, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h)
{
    uint32_t draw_w;
    uint32_t draw_h;
    uint32_t draw_x;
    uint32_t draw_y;

    if (!logo_pixels || !logo_width || !logo_height || !w || !h) {
        leonos_ui_rect(ui, x, y, w, h, LEONOS_UI_WHITE);
        leonos_ui_text_resized_clipped(ui, x + 12, y + h / 2U - 8U,
                                       w > 24U ? w - 24U : w,
                                       T("Logo unavailable", "Logo 不可用"),
                                       LEONOS_UI_DARK, LEONOS_UI_WHITE,
                                       LEONOS_FONT_W, LEONOS_FONT_H);
        return;
    }

    draw_w = w;
    draw_h = (uint32_t)(((uint64_t)w * logo_height) / logo_width);
    if (draw_h > h) {
        draw_h = h;
        draw_w = (uint32_t)(((uint64_t)h * logo_width) / logo_height);
    }
    if (!draw_w || !draw_h) {
        return;
    }
    draw_x = x + (w - draw_w) / 2U;
    draw_y = y + (h - draw_h) / 2U;
    for (uint32_t dy = 0; dy < draw_h; ++dy) {
        uint32_t sy = (uint32_t)(((uint64_t)dy * logo_height) / draw_h);
        if (sy >= logo_height) {
            sy = logo_height - 1U;
        }
        for (uint32_t dx = 0; dx < draw_w; ++dx) {
            uint32_t sx = (uint32_t)(((uint64_t)dx * logo_width) / draw_w);
            if (sx >= logo_width) {
                sx = logo_width - 1U;
            }
            if (draw_x + dx < ui->width && draw_y + dy < ui->height) {
                ui->pixels[(draw_y + dy) * ui->stride + draw_x + dx] =
                    logo_pixels[sy * logo_width + sx];
            }
        }
    }
}

static void draw_osver(struct leonos_ui_surface *ui)
{
    struct leonos_ui_property_item props[] = {
        {T("Kernel", "内核"), info.kernel_name, 0},
        {T("Kernel version", "内核版本"), info.kernel_version, 0},
        {T("Middle layer", "中间层"), info.middlelayer_name, 0},
        {T("Build time", "构建时间"), info.build_time, 0},
        {T("Copyright", "版权"), info.copyright, 0},
    };
    const uint32_t hero_x = 16U;
    const uint32_t hero_y = 88U;
    const uint32_t hero_w = 250U;
    const uint32_t content_h = 328U;
    const uint32_t info_x = 282U;
    const uint32_t info_w = OSVER_W - info_x - 16U;

    leonos_ui_rect(ui, 0, 0, OSVER_W, OSVER_H, LEONOS_UI_GRAY);
    leonos_ui_toolbar(ui, 0, 0, OSVER_W, 68U);
    leonos_ui_rect(ui, 0, 0, 8U, 68U, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text_resized_clipped(ui, 28U, 12U, 300U, "LeonOS 4",
                                   LEONOS_UI_BLACK, LEONOS_UI_GRAY, 12U, 24U);
    leonos_ui_text(ui, 29U, 43U,
                   T("About this operating system", "关于此操作系统"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);

    leonos_ui_panel(ui, hero_x, hero_y, hero_w, content_h, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, hero_x + 16U, hero_y + 14U,
                   T("LeonOS 4", "LeonOS 4"), LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_rect(ui, hero_x + 16U, hero_y + 38U, hero_w - 32U, 1U,
                   LEONOS_UI_WHITE);
    leonos_ui_panel(ui, hero_x + 27U, hero_y + 50U, OSVER_LOGO_BOX,
                    OSVER_LOGO_BOX, LEONOS_UI_WHITE);
    draw_logo(ui, hero_x + 35U, hero_y + 58U, OSVER_LOGO_BOX - 16U,
              OSVER_LOGO_BOX - 16U);
    leonos_ui_text_resized_clipped(ui, hero_x + 16U, hero_y + 260U,
                                   hero_w - 32U, "LeonOS 4", LEONOS_UI_BLACK,
                                   LEONOS_UI_LIGHT, 10U, 20U);
    leonos_ui_text(ui, hero_x + 16U, hero_y + 287U,
                   T("A compact desktop OS", "简洁的桌面操作系统"),
                   LEONOS_UI_DARK, LEONOS_UI_LIGHT);

    leonos_ui_panel(ui, info_x, hero_y, info_w, content_h, LEONOS_UI_LIGHT);
    leonos_ui_rect(ui, info_x + 1U, hero_y + 1U, info_w - 2U, 54U,
                   LEONOS_UI_WHITE);
    leonos_ui_text(ui, info_x + 16U, hero_y + 10U,
                   T("System information", "系统信息"), LEONOS_UI_BLACK,
                   LEONOS_UI_WHITE);
    leonos_ui_text(ui, info_x + 16U, hero_y + 31U,
                   T("Build and runtime components", "构建和运行时组件"),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_property_grid(ui, info_x + 12U, hero_y + 68U, info_w - 24U,
                            props, sizeof(props) / sizeof(props[0]), 122U, 28U);
    leonos_ui_text(ui, info_x + 16U, hero_y + 260U,
                   T("This window reports the version embedded in the running kernel.",
                     "此窗口显示当前运行内核中嵌入的版本信息。"),
                   LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, info_x + 16U, hero_y + 282U,
                   T("LeonOS is free software for learning and experimentation.",
                     "LeonOS 是用于学习和实验的自由软件。"),
                   LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    leonos_ui_statusbar(ui, OSVER_H - 28, 28, status_text);
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    int ret;

    puts("[osver.elf] system version viewer starting");
    (void)leonos_png_decode_file(OSVER_LOGO_PATH, &logo_pixels, &logo_width,
                                 &logo_height);
    if (!logo_pixels) {
        copy_text(status_text, sizeof(status_text),
                  T("System information (logo unavailable)",
                    "系统版本信息（Logo 不可用）"));
    }
    ret = leonos_system_info(&info);
    if (ret < 0) {
        copy_text(status_text, sizeof(status_text), T("Could not read system version information", "无法读取系统版本信息"));
        copy_text(info.kernel_name, sizeof(info.kernel_name), "unknown");
        copy_text(info.kernel_version, sizeof(info.kernel_version), "0.0.0-0000");
        copy_text(info.middlelayer_name, sizeof(info.middlelayer_name), "unknown");
        copy_text(info.build_time, sizeof(info.build_time), "unknown");
        copy_text(info.copyright, sizeof(info.copyright),
                  "Copyright LeonMMcoset 2021-2026. All rights reserved.");
    }

    if (!status_text[0]) {
        copy_text(status_text, sizeof(status_text), T("System version information", "系统版本信息"));
    }
    {
        uint32_t debug_flags = 0;
        if (leonos_kernel_debug_get_state(&debug_flags) == 0 &&
            (debug_flags & LEONOS_KERNEL_DEBUG_STATE_ENABLED) != 0U) {
            copy_text(status_text, sizeof(status_text),
                      T("Kernel debug mode enabled", "内核调试模式已启用"));
        }
    }
    window_id = leonos_gui_create_app_window_ex(T("About LeonOS", "关于 LeonOS"), T("System version", "系统版本"),
                                                OSVER_W, OSVER_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[osver.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, OSVER_W, OSVER_H, OSVER_W);
    draw_osver(&ui);
    leonos_gui_present_window((uint32_t)window_id, OSVER_W, OSVER_H, OSVER_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                leonos_png_free(logo_pixels);
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed && event.keycode == 1) {
                leonos_png_free(logo_pixels);
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1U)) {
                osver_kernel_debug_click((uint32_t)event.x, (uint32_t)event.y);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                draw_osver(&ui);
                leonos_gui_present_window((uint32_t)window_id, OSVER_W, OSVER_H, OSVER_W, pixels);
            }
        }
        sleep_ms(20);
    }
}
