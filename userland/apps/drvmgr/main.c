#include <leonos/auth.h>
#include <leonos/devmgr_service.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define DRVMGR_W 820U
#define DRVMGR_H 480U
#define DRVMGR_MAX_W LEONOS_GUI_MAX_WINDOW_WIDTH
#define DRVMGR_MAX_H LEONOS_GUI_MAX_WINDOW_HEIGHT
#define DRVMGR_ROW_H 24U
#define DRVMGR_LIST_Y 92U
#define DRVMGR_STATUS_H 28U
#define DRVMGR_KEY_ESCAPE 1U
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[DRVMGR_MAX_W * DRVMGR_MAX_H];
static system_driver_info_t drivers[SYSTEM_DRIVER_MAX];
static struct leonos_ui_listview_state driver_list;
static struct leonos_user_info current_user;
static uint32_t driver_count;
static uint32_t view_w = DRVMGR_W;
static uint32_t view_h = DRVMGR_H;
static uint8_t can_manage;
static char status_text[160] = "Ready";

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t index = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[index] && index + 1U < cap) {
        dst[index] = src[index];
        ++index;
    }
    dst[index] = 0;
}

static void append_text(char *dst, uint32_t *pos, uint32_t cap, const char *src)
{
    while (src && *src && *pos + 1U < cap) {
        dst[(*pos)++] = *src++;
    }
    if (*pos < cap) {
        dst[*pos] = 0;
    }
}

static void append_u32(char *dst, uint32_t *pos, uint32_t cap, uint32_t value)
{
    char digits[16];
    uint32_t count = 0;
    if (value == 0) {
        append_text(dst, pos, cap, "0");
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (count) {
        char text[2] = {digits[--count], 0};
        append_text(dst, pos, cap, text);
    }
}

static int hit_rect(int32_t x, int32_t y, int32_t rx, int32_t ry,
                    int32_t width, int32_t height)
{
    return x >= rx && y >= ry && x < rx + width && y < ry + height;
}

static const char *driver_state_name(uint32_t state)
{
    switch (state) {
    case SYSTEM_DRIVER_STATE_LOADING:
        return T("Loading", "加载中");
    case SYSTEM_DRIVER_STATE_LOADED:
        return T("Loaded", "已加载");
    case SYSTEM_DRIVER_STATE_DISABLED:
        return T("Disabled", "已禁用");
    case SYSTEM_DRIVER_STATE_FAILED:
        return T("Failed", "失败");
    default:
        return T("Unloaded", "未加载");
    }
}

static void set_status_code(const char *prefix, int code)
{
    uint32_t pos = 0;
    copy_text(status_text, sizeof(status_text), prefix);
    while (status_text[pos]) {
        ++pos;
    }
    append_text(status_text, &pos, sizeof(status_text), " (");
    if (code < 0) {
        append_text(status_text, &pos, sizeof(status_text), "-");
        append_u32(status_text, &pos, sizeof(status_text), (uint32_t)(-code));
    } else {
        append_u32(status_text, &pos, sizeof(status_text), (uint32_t)code);
    }
    append_text(status_text, &pos, sizeof(status_text), ")");
}

static void refresh_user(void)
{
    current_user = (struct leonos_user_info){0};
    can_manage = 0;
    if (leonos_auth_current(&current_user) == 0 &&
        current_user.role == LEONOS_AUTH_ROLE_ADMIN) {
        can_manage = 1;
    }
}

static uint32_t visible_rows(void)
{
    uint32_t bottom = view_h > DRVMGR_STATUS_H + 12U ? view_h - DRVMGR_STATUS_H - 12U : view_h;
    uint32_t rows = bottom > DRVMGR_LIST_Y ? (bottom - DRVMGR_LIST_Y) / DRVMGR_ROW_H : 1U;
    return rows ? rows : 1U;
}

static uint32_t list_height(void)
{
    uint32_t bottom = view_h > DRVMGR_STATUS_H + 12U ? view_h - DRVMGR_STATUS_H - 12U : view_h;
    return bottom > DRVMGR_LIST_Y ? bottom - DRVMGR_LIST_Y : DRVMGR_ROW_H;
}

static void refresh_drivers(void)
{
    uint32_t count = SYSTEM_DRIVER_MAX;
    int ret;
    refresh_user();
    ret = system_driver_list(drivers, SYSTEM_DRIVER_MAX, &count);
    if (ret < 0) {
        driver_count = 0;
        driver_list.selected = -1;
        leonos_ui_listview_state_set_count(&driver_list, 0);
        set_status_code(T("Driver refresh failed", "驱动刷新失败"), ret);
        return;
    }
    driver_count = count > SYSTEM_DRIVER_MAX ? SYSTEM_DRIVER_MAX : count;
    leonos_ui_listview_state_set_count(&driver_list, driver_count);
    if (driver_count && driver_list.selected < 0) {
        driver_list.selected = 0;
    }
    if ((uint32_t)driver_list.selected >= driver_count) {
        driver_list.selected = driver_count ? (int32_t)(driver_count - 1U) : -1;
    }
    copy_text(status_text, sizeof(status_text),
              can_manage ? T("Administrator controls enabled", "管理员控制已启用")
                         : T("Read-only: administrator required", "只读：需要管理员权限"));
}

static const system_driver_info_t *selected_driver(void)
{
    if (driver_list.selected < 0 || (uint32_t)driver_list.selected >= driver_count) {
        return 0;
    }
    return &drivers[driver_list.selected];
}

static void draw_drvmgr(struct leonos_ui_surface *ui)
{
    uint32_t list_w = view_w > 52U ? view_w - 52U : 668U;
    uint32_t rows = driver_count > driver_list.visible_rows ? driver_list.visible_rows : driver_count;
    struct leonos_ui_list_column columns[] = {
        {T("File", "文件"), 132U},
        {T("Driver", "驱动"), 108U},
        {T("State", "状态"), 96U},
        {T("ABI", "ABI"), 54U},
        {T("Details", "详情"), list_w > 390U ? list_w - 390U : 120U},
    };
    leonos_ui_rect(ui, 0, 0, view_w, view_h, LEONOS_UI_GRAY);
    leonos_ui_toolbar(ui, 8, 8, view_w > 16U ? view_w - 16U : view_w, 70U);
    leonos_ui_toolbar_button(ui, 18, 16, 82, T("Refresh", "刷新"), 0);
    leonos_ui_toolbar_button(ui, 108, 16, 72, T("Load", "加载"), 0);
    leonos_ui_toolbar_button(ui, 188, 16, 72, T("Unload", "卸载"), 0);
    leonos_ui_toolbar_button(ui, 268, 16, 96, T("Force stop", "强制卸载"), 0);
    leonos_ui_toolbar_button(ui, 372, 16, 98, T("Disable boot", "开机禁用"), 0);
    leonos_ui_toolbar_button(ui, 478, 16, 94, T("Enable boot", "开机启用"), 0);
    leonos_ui_text(ui, 18, 48,
                   can_manage ? T("Modules run in Ring 0. Changes take effect immediately.", "模块运行于 Ring 0，修改立即生效。")
                              : T("You can inspect loaded modules, but cannot change them.", "可以查看模块，但不能修改它们。"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);

    leonos_ui_scroll_view_frame(ui, 12, DRVMGR_LIST_Y - 4U,
                                view_w > 24U ? view_w - 24U : view_w, list_height());
    leonos_ui_listview_header(ui, 14, DRVMGR_LIST_Y - 2U, list_w, columns, 5U);
    for (uint32_t row = 0; row < rows; ++row) {
        uint32_t index = driver_list.scroll + row;
        const char *cells[5];
        char abi[16];
        if (index >= driver_count) {
            break;
        }
        abi[0] = 0;
        {
            uint32_t pos = 0;
            append_u32(abi, &pos, sizeof(abi), drivers[index].abi_version);
        }
        cells[0] = drivers[index].file;
        cells[1] = drivers[index].name[0] ? drivers[index].name : "-";
        cells[2] = driver_state_name(drivers[index].state);
        cells[3] = abi;
        cells[4] = drivers[index].error[0] ? drivers[index].error
                                           : (drivers[index].flags & SYSTEM_DRIVER_FLAG_DISABLED
                                                  ? T("Skipped at boot", "启动时跳过")
                                                  : T("Available", "可用"));
        leonos_ui_listview_row(ui, 14, DRVMGR_LIST_Y + 26U + row * DRVMGR_ROW_H,
                               list_w, columns, cells, 5U,
                               driver_list.selected == (int32_t)index ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(ui, view_w > 30U ? view_w - 30U : 690U, DRVMGR_LIST_Y - 2U,
                         18U, list_height() > 26U ? list_height() - 26U : 24U,
                         driver_list.scroll,
                         driver_count > driver_list.visible_rows ? driver_count : driver_list.visible_rows,
                         driver_list.visible_rows,
                         driver_count <= driver_list.visible_rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    leonos_ui_statusbar(ui, view_h - DRVMGR_STATUS_H, DRVMGR_STATUS_H, status_text);
}

static void present(struct leonos_ui_surface *ui, uint32_t window_id)
{
    driver_list.visible_rows = visible_rows();
    leonos_ui_listview_state_set_count(&driver_list, driver_count);
    draw_drvmgr(ui);
    leonos_gui_present_window(window_id, view_w, view_h, DRVMGR_MAX_W, pixels);
}

static void control_selected(uint32_t action)
{
    const system_driver_info_t *driver = selected_driver();
    int ret;
    if (!can_manage) {
        copy_text(status_text, sizeof(status_text), T("Administrator permission required", "需要管理员权限"));
        return;
    }
    if (!driver) {
        copy_text(status_text, sizeof(status_text), T("Select a driver first", "请先选择驱动"));
        return;
    }
    ret = system_driver_control(action, driver->file);
    if (ret < 0) {
        set_status_code(T("Driver operation failed", "驱动操作失败"), ret);
    } else {
        copy_text(status_text, sizeof(status_text), T("Driver operation completed", "驱动操作已完成"));
    }
    refresh_drivers();
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    puts("[drvmgr.elf] driver manager starting");
    window_id = leonos_gui_create_app_window_ex(T("Driver Manager", "驱动管理器"),
                                                T("Kernel driver modules", "内核驱动模块"),
                                                DRVMGR_W, DRVMGR_H, 0);
    if (window_id <= 0) {
        printf("[drvmgr.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, view_w, view_h, DRVMGR_MAX_W);
    leonos_ui_listview_state_init(&driver_list, visible_rows(), DRVMGR_ROW_H);
    driver_list.focused = 1;
    refresh_drivers();
    present(&ui, (uint32_t)window_id);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1U)) {
                if (hit_rect(event.x, event.y, 18, 16, 82, LEONOS_UI_BUTTON_H)) {
                    refresh_drivers();
                } else if (hit_rect(event.x, event.y, 108, 16, 72, LEONOS_UI_BUTTON_H)) {
                    control_selected(SYSTEM_DRIVER_CONTROL_LOAD);
                } else if (hit_rect(event.x, event.y, 188, 16, 72, LEONOS_UI_BUTTON_H)) {
                    control_selected(SYSTEM_DRIVER_CONTROL_UNLOAD);
                } else if (hit_rect(event.x, event.y, 268, 16, 96, LEONOS_UI_BUTTON_H)) {
                    control_selected(SYSTEM_DRIVER_CONTROL_FORCE_UNLOAD);
                } else if (hit_rect(event.x, event.y, 372, 16, 98, LEONOS_UI_BUTTON_H)) {
                    control_selected(SYSTEM_DRIVER_CONTROL_DISABLE_BOOT);
                } else if (hit_rect(event.x, event.y, 478, 16, 94, LEONOS_UI_BUTTON_H)) {
                    control_selected(SYSTEM_DRIVER_CONTROL_ENABLE_BOOT);
                } else if (event.x >= (int32_t)(view_w > 30U ? view_w - 30U : 690U) &&
                           event.y >= (int32_t)(DRVMGR_LIST_Y - 2U)) {
                    leonos_ui_vscrollbar_handle_mouse(&driver_list.scroll,
                                                      driver_count > driver_list.visible_rows
                                                          ? driver_count : driver_list.visible_rows,
                                                      driver_list.visible_rows,
                                                      view_w > 30U ? view_w - 30U : 690U,
                                                      DRVMGR_LIST_Y - 2U, 18U,
                                                      list_height() > 26U ? list_height() - 26U : 24U,
                                                      event.x, event.y);
                } else {
                    uint32_t list_w = view_w > 52U ? view_w - 52U : 668U;
                    uint32_t activate = 0;
                    leonos_ui_listview_state_handle_mouse(&driver_list, event.x, event.y,
                                                          14, DRVMGR_LIST_Y + 26U, list_w,
                                                          &activate);
                    (void)activate;
                }
                present(&ui, (uint32_t)window_id);
            } else if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (leonos_ui_listview_state_handle_wheel(&driver_list, event.dy)) {
                    present(&ui, (uint32_t)window_id);
                }
            } else if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                uint32_t activate = 0;
                if (event.keycode == DRVMGR_KEY_ESCAPE) {
                    return 0;
                }
                if (leonos_ui_listview_state_handle_key(&driver_list, event.keycode, &activate)) {
                    present(&ui, (uint32_t)window_id);
                }
            } else if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                       event.type == LEONOS_GUI_APP_EVENT_FOCUS ||
                       event.type == LEONOS_GUI_APP_EVENT_THEME_CHANGED) {
                if (event.width >= DRVMGR_W) {
                    view_w = event.width > DRVMGR_MAX_W ? DRVMGR_MAX_W : event.width;
                }
                if (event.height >= DRVMGR_H) {
                    view_h = event.height > DRVMGR_MAX_H ? DRVMGR_MAX_H : event.height;
                }
                leonos_ui_bind(&ui, pixels, view_w, view_h, DRVMGR_MAX_W);
                present(&ui, (uint32_t)window_id);
            }
        }
    }
}
