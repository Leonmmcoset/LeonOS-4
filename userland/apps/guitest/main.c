#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/mouse.h>
#include <leonos/stdio.h>
#include <leonos/ui.h>

#define T(en, zh) leonos_i18n((en), (zh))

#define GUI_TEST_INITIAL_W 640U
#define GUI_TEST_INITIAL_H 420U
#define GUI_TEST_MAX_W 720U
#define GUI_TEST_MAX_H 500U
#define GUI_TEST_BUTTON_H LEONOS_UI_BUTTON_H

static uint32_t pixels[GUI_TEST_MAX_W * GUI_TEST_MAX_H];

static int hit(int32_t x, int32_t y, uint32_t rx, uint32_t ry,
               uint32_t width, uint32_t height)
{
    return x >= (int32_t)rx && y >= (int32_t)ry &&
           x < (int32_t)(rx + width) && y < (int32_t)(ry + height);
}

static void set_status(char *status, uint32_t capacity, const char *text, int result)
{
    uint32_t pos = 0;
    if (!status || capacity == 0) {
        return;
    }
    status[0] = 0;
    while (text && *text && pos + 1U < capacity) {
        status[pos++] = *text++;
    }
    if (pos + 1U < capacity) {
        status[pos++] = result > 0 ? ':' : '!';
    }
    if (pos + 1U < capacity) {
        status[pos++] = result > 0 ? ' ' : ' ';
    }
    if (result > 0 && pos + 3U < capacity) {
        status[pos++] = 'o';
        status[pos++] = 'k';
    } else if (result <= 0 && pos + 6U < capacity) {
        status[pos++] = 'f';
        status[pos++] = 'a';
        status[pos++] = 'i';
        status[pos++] = 'l';
    }
    status[pos] = 0;
}

static const char *cursor_name(uint32_t style)
{
    switch (style) {
    case LEONOS_GUI_CURSOR_HAND:
        return T("Hand", "手形");
    case LEONOS_GUI_CURSOR_TEXT:
        return T("Text", "文本");
    case LEONOS_GUI_CURSOR_WAIT:
        return T("Wait", "等待");
    case LEONOS_GUI_CURSOR_CROSSHAIR:
        return T("Crosshair", "十字");
    case LEONOS_GUI_CURSOR_MOVE:
        return T("Move", "移动");
    default:
        return T("Arrow", "箭头");
    }
}

static void reset_desktop_state(uint32_t window_id)
{
    (void)leonos_gui_set_window_borderless(window_id, 0);
    (void)leonos_gui_set_window_taskbar_visible(window_id, 1);
    (void)leonos_gui_set_taskbar_visible(window_id, 1);
    (void)leonos_mouse_set_style(window_id, LEONOS_GUI_CURSOR_ARROW);
    (void)leonos_gui_set_window_title(window_id, T("GUI API Tester", "GUI API 测试"));
}

static void draw_test_window(struct leonos_ui_surface *ui, uint32_t width,
                             uint32_t height, const char *status,
                             uint32_t title_index, uint8_t borderless,
                             uint8_t taskbar_list_visible,
                             uint8_t desktop_taskbar_visible,
                             uint32_t cursor_style)
{
    uint32_t left = 24U;
    uint32_t gap = 20U;
    uint32_t button_w = width > left * 2U + gap
                            ? (width - left * 2U - gap) / 2U
                            : 120U;
    uint32_t right = left + button_w + gap;
    uint32_t rows[] = {104U, 148U, 192U, 236U};
    char detail[128];
    uint32_t pos = 0;
    (void)height;
    leonos_ui_rect(ui, 0, 0, width, height, LEONOS_UI_WHITE);
    leonos_ui_toolbar(ui, 0, 0, width, 42U);
    leonos_ui_text(ui, 20, 13, T("LeonOS GUI API Tester", "LeonOS GUI API 测试器"),
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text_clipped(ui, 24, 62, width > 48U ? width - 48U : width,
                           status, LEONOS_UI_DARK, LEONOS_UI_WHITE);

    pos = 0;
    detail[0] = 0;
    while (T("Cursor style: ", "光标样式: ")[pos] && pos + 1U < sizeof(detail)) {
        detail[pos] = T("Cursor style: ", "光标样式: ")[pos];
        ++pos;
    }
    {
        uint32_t style_pos = 0;
        const char *name = cursor_name(cursor_style);
        while (name[style_pos] && pos + 1U < sizeof(detail)) {
            detail[pos++] = name[style_pos++];
        }
    }
    detail[pos] = 0;
    leonos_ui_text_clipped(ui, 24, 82, width > 48U ? width - 48U : width,
                           detail, LEONOS_UI_DARK, LEONOS_UI_WHITE);

    leonos_ui_button(ui, left, rows[0], button_w, GUI_TEST_BUTTON_H,
                     title_index ? T("Set alternate title", "设置备用标题")
                                 : T("Set test title", "设置测试标题"), 0);
    leonos_ui_button(ui, right, rows[0], button_w, GUI_TEST_BUTTON_H,
                     borderless ? T("Restore borders", "恢复边框")
                                : T("Toggle borderless", "切换无边框"), 0);
    leonos_ui_button(ui, left, rows[1], button_w, GUI_TEST_BUTTON_H,
                     taskbar_list_visible ? T("Hide window from taskbar", "从任务栏隐藏窗口")
                                          : T("Show window in taskbar", "在任务栏显示窗口"), 0);
    leonos_ui_button(ui, right, rows[1], button_w, GUI_TEST_BUTTON_H,
                     desktop_taskbar_visible ? T("Hide desktop taskbar", "隐藏桌面任务栏")
                                             : T("Show desktop taskbar", "显示桌面任务栏"), 0);
    leonos_ui_button(ui, left, rows[2], button_w, GUI_TEST_BUTTON_H,
                     T("Move mouse to 320, 240", "移动鼠标到 320, 240"), 0);
    leonos_ui_button(ui, right, rows[2], button_w, GUI_TEST_BUTTON_H,
                     T("Next cursor style", "下一个光标样式"), 0);
    leonos_ui_button(ui, left, rows[3], button_w, GUI_TEST_BUTTON_H,
                     T("Reset all", "重置全部"), 0);
    leonos_ui_button(ui, right, rows[3], button_w, GUI_TEST_BUTTON_H,
                     T("Close", "关闭"), 0);
}

int main(void)
{
    struct leonos_gui_app_event event;
    struct leonos_ui_surface ui;
    const char *titles[] = {
        T("GUI API Tester", "GUI API 测试"),
        T("GUI API title update passed", "GUI API 标题更新成功"),
    };
    char status[96] = "Ready";
    uint32_t view_w = GUI_TEST_INITIAL_W;
    uint32_t view_h = GUI_TEST_INITIAL_H;
    uint32_t title_index = 0;
    uint32_t cursor_style = LEONOS_GUI_CURSOR_ARROW;
    uint8_t borderless = 0;
    uint8_t taskbar_list_visible = 1;
    uint8_t desktop_taskbar_visible = 1;
    int window_id = leonos_gui_create_app_window_ex(
        T("GUI API Tester", "GUI API 测试"),
        T("GUI API Tester", "GUI API 测试"),
        GUI_TEST_INITIAL_W, GUI_TEST_INITIAL_H, 0);

    if (window_id <= 0) {
        printf("[guitest.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, view_w, view_h, GUI_TEST_MAX_W);
    for (;;) {
        draw_test_window(&ui, view_w, view_h, status, title_index, borderless,
                         taskbar_list_visible, desktop_taskbar_visible, cursor_style);
        (void)leonos_gui_present_window((uint32_t)window_id, view_w, view_h,
                                        GUI_TEST_MAX_W, pixels);

        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) <= 0) {
            continue;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
            break;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
            if (event.width >= 240U && event.width <= GUI_TEST_MAX_W) {
                view_w = event.width;
            }
            if (event.height >= 180U && event.height <= GUI_TEST_MAX_H) {
                view_h = event.height;
            }
            leonos_ui_bind(&ui, pixels, view_w, view_h, GUI_TEST_MAX_W);
            continue;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.keycode == 1U) {
            break;
        }
        if (event.type != LEONOS_GUI_APP_EVENT_MOUSE_BUTTON ||
            !(event.buttons & 1U)) {
            continue;
        }
        {
            uint32_t left = 24U;
            uint32_t gap = 20U;
            uint32_t button_w = view_w > left * 2U + gap
                                    ? (view_w - left * 2U - gap) / 2U
                                    : 120U;
            uint32_t right = left + button_w + gap;
            int result = 0;
            if (hit(event.x, event.y, left, 104U, button_w, GUI_TEST_BUTTON_H)) {
                uint32_t next_title_index = title_index ? 0U : 1U;
                result = leonos_gui_set_window_title((uint32_t)window_id,
                                                     titles[next_title_index]);
                if (result > 0) {
                    title_index = next_title_index;
                }
                set_status(status, sizeof(status), "window title", result);
            } else if (hit(event.x, event.y, right, 104U, button_w, GUI_TEST_BUTTON_H)) {
                uint8_t next_borderless = borderless ? 0U : 1U;
                result = leonos_gui_set_window_borderless((uint32_t)window_id, next_borderless);
                if (result > 0) {
                    borderless = next_borderless;
                }
                set_status(status, sizeof(status), "borderless window", result);
            } else if (hit(event.x, event.y, left, 148U, button_w, GUI_TEST_BUTTON_H)) {
                uint8_t next_taskbar_list_visible = taskbar_list_visible ? 0U : 1U;
                result = leonos_gui_set_window_taskbar_visible((uint32_t)window_id,
                                                               next_taskbar_list_visible);
                if (result > 0) {
                    taskbar_list_visible = next_taskbar_list_visible;
                }
                set_status(status, sizeof(status), "window taskbar entry", result);
            } else if (hit(event.x, event.y, right, 148U, button_w, GUI_TEST_BUTTON_H)) {
                uint8_t next_desktop_taskbar_visible = desktop_taskbar_visible ? 0U : 1U;
                result = leonos_gui_set_taskbar_visible((uint32_t)window_id,
                                                        next_desktop_taskbar_visible);
                if (result > 0) {
                    desktop_taskbar_visible = next_desktop_taskbar_visible;
                }
                set_status(status, sizeof(status), "desktop taskbar", result);
            } else if (hit(event.x, event.y, left, 192U, button_w, GUI_TEST_BUTTON_H)) {
                result = leonos_mouse_set_position((uint32_t)window_id,
                                                   320, 240);
                set_status(status, sizeof(status), "mouse position", result);
            } else if (hit(event.x, event.y, right, 192U, button_w, GUI_TEST_BUTTON_H)) {
                uint32_t next_cursor_style = (cursor_style + 1U) % LEONOS_GUI_CURSOR_STYLE_COUNT;
                result = leonos_mouse_set_style((uint32_t)window_id, next_cursor_style);
                if (result > 0) {
                    cursor_style = next_cursor_style;
                }
                set_status(status, sizeof(status), "mouse style", result);
            } else if (hit(event.x, event.y, left, 236U, button_w, GUI_TEST_BUTTON_H)) {
                reset_desktop_state((uint32_t)window_id);
                borderless = 0;
                taskbar_list_visible = 1;
                desktop_taskbar_visible = 1;
                cursor_style = LEONOS_GUI_CURSOR_ARROW;
                title_index = 0;
                set_status(status, sizeof(status), "reset", 1);
            } else if (hit(event.x, event.y, right, 236U, button_w, GUI_TEST_BUTTON_H)) {
                break;
            }
        }
    }
    reset_desktop_state((uint32_t)window_id);
    (void)leonos_gui_destroy_app_window((uint32_t)window_id);
    return 0;
}
