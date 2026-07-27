#include <leonos/gui.h>
#include <leonos/fs.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define DEMO_W 760
#define DEMO_H 520
#define DEMO_MENU_BAR_H 28
#define DEMO_MENU_ITEM_H (LEONOS_FONT_H + 8)
#define DEMO_CONTEXT_MENU_W 148
#define DEMO_TREE_ROW_H 24
#define DEMO_DROPDOWN_ROW_H 24
#define DEMO_DROPDOWN_ANIM_MS 140UL
#define DEMO_CONTEXT_ANIM_MS 120UL

enum {
    DEMO_MENU_NONE = 0,
    DEMO_MENU_FILE = 1,
    DEMO_MENU_VIEW = 2,
    DEMO_MENU_HELP = 3,
};

enum {
    DEMO_CTX_OPEN = 1,
    DEMO_CTX_RENAME = 2,
    DEMO_CTX_DISABLED = 3,
};

enum {
    DEMO_DROP_NORMAL = 1,
    DEMO_DROP_HIGH = 2,
    DEMO_DROP_LOW = 3,
    DEMO_DROP_DISABLED = 4,
};

enum {
    DEMO_FILE_MESSAGE = 101,
    DEMO_FILE_INPUT = 102,
    DEMO_VIEW_CONTROLS = 201,
    DEMO_VIEW_DATA = 202,
    DEMO_VIEW_DIALOGS = 203,
    DEMO_VIEW_ADVANCED = 204,
    DEMO_HELP_ABOUT = 301,
};

static uint32_t pixels[DEMO_W * DEMO_H];

static const struct leonos_ui_tab_item demo_tabs[] = {
    {"Controls", 0, 0},
    {"Data", 1, 0},
    {"Dialogs", 2, 0},
    {"Advanced", 3, 0},
};
static const struct leonos_ui_tab_item preview_tabs[] = {
    {"Overview", 0, 0},
    {"Details", 1, 0},
    {"History", 2, 0},
};
static const struct leonos_ui_dropdown_item priority_items[] = {
    {"Normal priority", DEMO_DROP_NORMAL, 0},
    {"High priority", DEMO_DROP_HIGH, 0},
    {"Low priority", DEMO_DROP_LOW, 0},
    {"Unavailable", DEMO_DROP_DISABLED, LEONOS_UI_MENU_DISABLED},
};
static const char *const data_rows[][4] = {
    {"fileman.elf", "Program", "Running", "Uses toolbar, listview, statusbar"},
    {"notepad.elf", "Program", "Running", "Uses menubar, text area, scrollbar"},
    {"taskmgr.elf", "Program", "Idle", "Uses listview and live status"},
    {"terminal.elf", "Program", "Running", "Uses ANSI text rendering"},
    {"uidemo.elf", "Program", "Running", "Exercises reusable controls"},
    {"minesweeper.elf", "Program", "Idle", "Resizable game window"},
    {"calc.elf", "Program", "Idle", "Keyboard input and buttons"},
    {"osver.elf", "Program", "Idle", "System version dialog"},
};
static char sample_path[96] = "0:/programs/notepad/notepad.elf";
static char sample_text[160] = "Line one\n你好，LeonOS 4。中文显示测试。\nLine three";
static char demo_status[96] = "Click inside the top tabs to switch pages";
static struct leonos_ui_edit_state sample_edit;
static struct leonos_ui_text_area_state sample_area;
static struct leonos_ui_listview_state sample_list;
static struct leonos_ui_tab_state demo_tab_state;
static struct leonos_ui_tab_state preview_tab_state;
static struct leonos_ui_color_input_state sample_color;
static struct leonos_ui_date_input_state sample_date;
static uint32_t tree_selected_id = 12;
static unsigned long anim_start_ms;
static unsigned long last_anim_redraw_ms;
static uint8_t context_menu_open;
static uint8_t context_menu_animating;
static uint8_t context_menu_opening;
static unsigned long context_menu_anim_start_ms;
static uint32_t context_menu_x = 540;
static uint32_t context_menu_y = 188;
static uint8_t dropdown_open;
static uint8_t dropdown_animating;
static uint8_t dropdown_opening;
static unsigned long dropdown_anim_start_ms;
static uint32_t dropdown_selected = DEMO_DROP_NORMAL;
static uint8_t menu_open;
static uint32_t demo_slider_value = 65;
static int32_t demo_stepper_value = 2;
static struct leonos_ui_toast_state demo_toast;

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || capacity == 0) {
        return;
    }
    if (src) {
        while (i + 1 < capacity && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

static void run_unicode_file_test(void)
{
    const char *dir = "0:/测试目录";
    const char *path = "0:/测试目录/你好.txt";
    const char *content = "你好，LeonOS 4。中文显示测试。\n";
    struct leonos_stat st;
    struct leonos_dir_entry entry;
    int ret = mkdir(dir, 0);
    printf("[uidemo.elf] unicode mkdir %s ret=%d\n", dir, ret);
    int fd = open(path, LEONOS_O_CREAT | LEONOS_O_TRUNC | LEONOS_O_WRONLY, 0);
    printf("[uidemo.elf] unicode open-write %s fd=%d\n", path, fd);
    if (fd >= 0) {
        long wrote = write(fd, content, strlen(content));
        close(fd);
        printf("[uidemo.elf] unicode write bytes=%d\n", (int)wrote);
    }
    ret = stat(path, &st);
    printf("[uidemo.elf] unicode stat %s ret=%d type=%d size=%d\n",
           path, ret, ret == 0 ? (int)st.type : -1, ret == 0 ? (int)st.size : -1);
    fd = open(dir, LEONOS_O_RDONLY, 0);
    printf("[uidemo.elf] unicode open-dir %s fd=%d\n", dir, fd);
    if (fd >= 0) {
        while ((ret = leonos_readdir(fd, &entry)) > 0) {
            printf("[uidemo.elf] unicode readdir name=%s type=%d\n",
                   entry.name, (int)entry.type);
        }
        close(fd);
        printf("[uidemo.elf] unicode readdir done ret=%d\n", ret);
    }
}

static const char *dropdown_selected_label(void)
{
    for (uint32_t i = 0; i < sizeof(priority_items) / sizeof(priority_items[0]); ++i) {
        if (priority_items[i].id == dropdown_selected) {
            return priority_items[i].label;
        }
    }
    return "Normal priority";
}

static void dropdown_set_open(uint8_t open)
{
    if (dropdown_open == open && !dropdown_animating) {
        return;
    }
    dropdown_open = open;
    dropdown_opening = open;
    dropdown_animating = 1;
    dropdown_anim_start_ms = leonos_uptime_ms();
}

static uint32_t dropdown_progress(void)
{
    uint32_t raw;
    if (!dropdown_animating) {
        return dropdown_open ? 1000 : 0;
    }
    raw = leonos_ui_anim_progress(leonos_uptime_ms(), dropdown_anim_start_ms,
                                  DEMO_DROPDOWN_ANIM_MS);
    if (raw >= 1000) {
        dropdown_animating = 0;
        return dropdown_open ? 1000 : 0;
    }
    return dropdown_opening ? raw : 1000 - raw;
}

static void context_menu_set_open(uint8_t open)
{
    if (context_menu_open == open && !context_menu_animating) {
        return;
    }
    context_menu_open = open;
    context_menu_opening = open;
    context_menu_animating = 1;
    context_menu_anim_start_ms = leonos_uptime_ms();
}

static uint32_t context_menu_progress(void)
{
    uint32_t raw;
    if (!context_menu_animating) {
        return context_menu_open ? 1000 : 0;
    }
    raw = leonos_ui_anim_progress(leonos_uptime_ms(), context_menu_anim_start_ms,
                                  DEMO_CONTEXT_ANIM_MS);
    if (raw >= 1000) {
        context_menu_animating = 0;
        return context_menu_open ? 1000 : 0;
    }
    return context_menu_opening ? raw : 1000 - raw;
}

static void draw_controls_page(struct leonos_ui_surface *ui)
{
    uint32_t drop_progress = dropdown_progress();
    struct leonos_ui_property_item props[] = {
        {"Menu Bar", "shared dropdown hit logic", 0},
        {"Split Pane", "first / splitter / second", 0},
        {"Toast", "non-blocking message", 0},
    };
    leonos_ui_groupbox(ui, 12, 80, 230, 152, "Buttons and Inputs");
    leonos_ui_button(ui, 26, 106, 74, LEONOS_UI_BUTTON_H, "OK", 0);
    leonos_ui_button(ui, 108, 106, 92, LEONOS_UI_BUTTON_H, "Pressed", LEONOS_UI_BUTTON_PRESSED);
    leonos_ui_button(ui, 26, 138, 96, LEONOS_UI_BUTTON_H, "Disabled", LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_checkbox(ui, 26, 174, "Checked", 1, 0);
    leonos_ui_radio(ui, 26, 200, "Radio", 1, 0);

    leonos_ui_groupbox(ui, 258, 80, 246, 152, "Editors");
    leonos_ui_edit_state_draw(ui, 274, 106, 204, &sample_edit, 0);
    leonos_ui_combobox(ui, 274, 140, 204, dropdown_selected_label(),
                       drop_progress > 0, 0);
    leonos_ui_text_area_state_draw(ui, 274, 174, 204, 44, &sample_area, 0);
    leonos_ui_dropdown(ui, 274, 164, 204, priority_items,
                       sizeof(priority_items) / sizeof(priority_items[0]),
                       dropdown_selected, DEMO_DROPDOWN_ROW_H, drop_progress);

    leonos_ui_groupbox(ui, 520, 80, 220, 152, "Progress");
    leonos_ui_progress(ui, 536, 110, 172, 18, demo_slider_value, 100);
    leonos_ui_slider(ui, 536, 142, 172, 22, demo_slider_value, 100, 0);
    leonos_ui_stepper(ui, 536, 176, 126, LEONOS_UI_BUTTON_H,
                      demo_stepper_value, 1, 5, 0);
    leonos_ui_vscrollbar(ui, 710, 104, 18, 96, 4, 20, 8, 0);
    leonos_ui_hscrollbar(ui, 536, 204, 172, 18, 6, 20, 8, 0);

    leonos_ui_property_grid(ui, 12, 250, 360, props,
                            sizeof(props) / sizeof(props[0]), 100, 24);
    leonos_ui_groupbox(ui, 400, 250, 340, 230, "Color and Date Inputs");
    leonos_ui_text(ui, 416, 270, "Date", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_date_input(ui, 416, 286, 300, &sample_date, 0);
    leonos_ui_text(ui, 416, 360, "Color", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_color_input(ui, 416, 376, 300, &sample_color, 0);
}

static void draw_data_page(struct leonos_ui_surface *ui)
{
    struct leonos_ui_list_column cols[] = {
        {"Name", 170},
        {"Type", 96},
        {"State", 120},
        {"Info", 240},
    };
    struct leonos_ui_menubar_item menu_items[] = {
        {"File", DEMO_MENU_FILE, 52, 0},
        {"Edit", 99, 52, LEONOS_UI_MENU_DISABLED},
        {"View", DEMO_MENU_VIEW, 52, 0},
    };
    struct leonos_ui_split_pane_state split;
    uint32_t row_count = sizeof(data_rows) / sizeof(data_rows[0]);

    leonos_ui_toolbar(ui, 12, 82, DEMO_W - 24, 34);
    leonos_ui_toolbar_button(ui, 20, 87, 72, "New", 0);
    leonos_ui_toolbar_button(ui, 98, 87, 72, "Open", LEONOS_UI_BUTTON_PRESSED);
    leonos_ui_toolbar_button(ui, 176, 87, 90, "Refresh", 0);
    leonos_ui_splitter(ui, 278, 87, 4, 24, 1);
    leonos_ui_toolbar_button(ui, 292, 87, 92, "Disabled", LEONOS_UI_BUTTON_DISABLED);

    leonos_ui_scroll_view_frame(ui, 12, 128, DEMO_W - 42, 116);
    leonos_ui_listview_header(ui, 14, 130, DEMO_W - 46, cols, 4);
    for (uint32_t row = 0; row < sample_list.visible_rows; ++row) {
        uint32_t i = sample_list.scroll + row;
        if (i >= row_count) {
            break;
        }
        leonos_ui_listview_row(ui, 14, 158 + row * 24, DEMO_W - 46, cols, data_rows[i], 4,
                               sample_list.selected == (int32_t)i ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(ui, DEMO_W - 28, 128, 18, 116, sample_list.scroll,
                         row_count, sample_list.visible_rows, 0);

    leonos_ui_groupbox(ui, 12, 268, 300, 92, "Tabs and Splitter");
    leonos_ui_tab_control(ui, 28, 294, 268, preview_tabs, 3, &preview_tab_state);
    leonos_ui_tab_body(ui, 28, 324, 268, 26);
    leonos_ui_text(ui, 36, 330, "Tab body frame", LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_split_pane_init(&split, LEONOS_UI_SPLIT_VERTICAL, 118, 64, 64);
    leonos_ui_split_pane_layout(&split, 30, 366, 266, 34);
    leonos_ui_inset(ui, (uint32_t)split.first.x, (uint32_t)split.first.y,
                    split.first.w, split.first.h, LEONOS_UI_WHITE);
    leonos_ui_inset(ui, (uint32_t)split.second.x, (uint32_t)split.second.y,
                    split.second.w, split.second.h, LEONOS_UI_WHITE);
    leonos_ui_split_pane_draw(ui, &split);

    leonos_ui_groupbox(ui, 332, 268, 300, 92, "Menus");
    leonos_ui_menubar_draw(ui, 348, 294, 254, menu_items,
                           sizeof(menu_items) / sizeof(menu_items[0]),
                           DEMO_MENU_FILE);
}

static void draw_dialogs_page(struct leonos_ui_surface *ui)
{
    leonos_ui_message_box(ui, 22, 92, 220, 132, "Message", "Operation completed.", "OK");
    leonos_ui_confirm_dialog(ui, 270, 92, 240, 132, "Confirm", "Close this window?", 1);
    leonos_ui_input_dialog(ui, 530, 92, 210, 152, "Input", "File name:", "notes.txt", LEONOS_UI_EDIT_FOCUSED);
    leonos_ui_toolbar_button(ui, 22, 252, 116, "Message Box", 0);
    leonos_ui_toolbar_button(ui, 148, 252, 96, "Confirm", 0);
    leonos_ui_toolbar_button(ui, 254, 252, 86, "Input", 0);
    leonos_ui_statusbar(ui, DEMO_H - 28, 28, "Statusbar: UI gallery shows every control in libc/ui.c");
}

static void draw_advanced_page(struct leonos_ui_surface *ui)
{
    struct leonos_ui_layout layout;
    struct leonos_ui_rect rect;
    uint32_t progress = leonos_ui_anim_progress(leonos_uptime_ms(), anim_start_ms, 1200);
    uint32_t eased = leonos_ui_anim_ease_out(progress);
    uint32_t width = leonos_ui_anim_lerp(48, 172, eased);
    struct leonos_ui_tree_item tree_items[] = {
        {"0:/", 10, 0, LEONOS_UI_TREE_EXPANDED},
        {"system", 11, 1, LEONOS_UI_TREE_EXPANDED},
        {"fonts", 12, 2, LEONOS_UI_TREE_LEAF},
        {"resources", 13, 2, LEONOS_UI_TREE_LEAF},
        {"userland", 20, 1, LEONOS_UI_TREE_EXPANDED},
        {"fileman.elf", 21, 2, LEONOS_UI_TREE_LEAF},
        {"uidemo.elf", 22, 2, LEONOS_UI_TREE_LEAF},
    };
    struct leonos_ui_context_menu_item context_items[] = {
        {"Open", DEMO_CTX_OPEN, 0},
        {"Rename", DEMO_CTX_RENAME, 0},
        {"Disabled item", DEMO_CTX_DISABLED, LEONOS_UI_MENU_DISABLED},
    };

    for (uint32_t i = 0; i < sizeof(tree_items) / sizeof(tree_items[0]); ++i) {
        if (tree_items[i].id == tree_selected_id) {
            tree_items[i].flags |= LEONOS_UI_TREE_SELECTED;
        }
    }

    leonos_ui_groupbox(ui, 12, 80, 336, 164, "Automatic Layout");
    leonos_ui_layout_begin(&layout, 30, 108, 296, 104, 8);
    rect = leonos_ui_layout_next(&layout, 86, LEONOS_UI_BUTTON_H);
    leonos_ui_button(ui, (uint32_t)rect.x, (uint32_t)rect.y, rect.w, rect.h, "Alpha", 0);
    rect = leonos_ui_layout_next(&layout, 112, LEONOS_UI_BUTTON_H);
    leonos_ui_button(ui, (uint32_t)rect.x, (uint32_t)rect.y, rect.w, rect.h, "Long Button", 0);
    rect = leonos_ui_layout_next(&layout, 92, LEONOS_UI_BUTTON_H);
    leonos_ui_button(ui, (uint32_t)rect.x, (uint32_t)rect.y, rect.w, rect.h, "Wrap", 0);
    rect = leonos_ui_layout_next(&layout, 132, LEONOS_UI_BUTTON_H);
    leonos_ui_combobox(ui, (uint32_t)rect.x, (uint32_t)rect.y, rect.w, "Auto row", 0, 0);
    rect = leonos_ui_layout_next(&layout, 120, LEONOS_UI_BUTTON_H);
    leonos_ui_text_field(ui, (uint32_t)rect.x, (uint32_t)rect.y, rect.w, "Field", 0);
    rect = leonos_ui_layout_next(&layout, 72, LEONOS_UI_BUTTON_H);
    leonos_ui_button(ui, (uint32_t)rect.x, (uint32_t)rect.y, rect.w, rect.h, "End", 0);

    leonos_ui_groupbox(ui, 370, 80, 364, 164, "Animation and Context Menu");
    leonos_ui_text(ui, 388, 110, "Animation:", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_activity_bar(ui, 476, 106, 220, 20, progress);
    leonos_ui_bevel(ui, 388, 144, 172, 18, LEONOS_UI_WHITE, 0);
    leonos_ui_rect(ui, 390, 146, width > 4 ? width - 4 : width, 14, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_button(ui, 388, 184, 128, LEONOS_UI_BUTTON_H, "Right Click Area", 0);
    leonos_ui_text(ui, 388, 216, "Right-click here to open a reusable menu.",
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);

    leonos_ui_groupbox(ui, 12, 268, 336, 154, "Tree Selection");
    leonos_ui_scroll_view_frame(ui, 30, 296, 296, 112);
    leonos_ui_tree(ui, 32, 298, 292, tree_items,
                   sizeof(tree_items) / sizeof(tree_items[0]), DEMO_TREE_ROW_H);

    leonos_ui_groupbox(ui, 370, 268, 364, 154, "Reusable Right-click Menu");
    leonos_ui_context_menu(ui, 392, 300, 180, context_items,
                           sizeof(context_items) / sizeof(context_items[0]));
    leonos_ui_text(ui, 592, 304, "The same component is used by Fileman.",
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);

    if (context_menu_open || context_menu_animating) {
        leonos_ui_context_menu_animated(ui, context_menu_x, context_menu_y, DEMO_CONTEXT_MENU_W,
                                        context_items,
                                        sizeof(context_items) / sizeof(context_items[0]),
                                        context_menu_progress());
    }
}

static void draw_demo(struct leonos_ui_surface *ui, uint32_t page)
{
    struct leonos_ui_menubar_item top_items[] = {
        {"File", DEMO_MENU_FILE, 54, 0},
        {"View", DEMO_MENU_VIEW, 54, 0},
        {"Help", DEMO_MENU_HELP, 54, 0},
    };
    leonos_ui_rect(ui, 0, 0, DEMO_W, DEMO_H, LEONOS_UI_WHITE);
    leonos_ui_menubar_draw(ui, 0, 0, DEMO_W, top_items,
                           sizeof(top_items) / sizeof(top_items[0]),
                           menu_open);
    leonos_ui_text(ui, 12, 36, "LeonOS UI Component Library", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 12, 54,
                   leonos_ui_theme() == LEONOS_UI_THEME_METRO
                       ? "Metro flat reusable drawing controls"
                       : "Win95 reusable drawing controls",
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 420, 54, "你好，LeonOS 4。中文显示测试。", LEONOS_UI_DARK, LEONOS_UI_WHITE);
    demo_tab_state.selected_id = page;
    leonos_ui_tab_control(ui, 236, 36, 410, demo_tabs, 4, &demo_tab_state);
    if (page == 0) {
        draw_controls_page(ui);
    } else if (page == 1) {
        draw_data_page(ui);
    } else if (page == 2) {
        draw_dialogs_page(ui);
    } else {
        draw_advanced_page(ui);
    }
    leonos_ui_statusbar(ui, DEMO_H - 28, 28, demo_status);

    if (menu_open == DEMO_MENU_FILE) {
        struct leonos_ui_context_menu_item items[] = {
            {"Message", DEMO_FILE_MESSAGE, 0},
            {"Input", DEMO_FILE_INPUT, 0},
        };
        struct leonos_ui_rect r;
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    DEMO_MENU_FILE, &r);
        leonos_ui_menu_popup(ui, (uint32_t)r.x, DEMO_MENU_BAR_H, 154,
                             items, sizeof(items) / sizeof(items[0]), 0);
    } else if (menu_open == DEMO_MENU_VIEW) {
        struct leonos_ui_context_menu_item items[] = {
            {"Controls", DEMO_VIEW_CONTROLS, 0},
            {"Data", DEMO_VIEW_DATA, 0},
            {"Dialogs", DEMO_VIEW_DIALOGS, 0},
            {"Advanced", DEMO_VIEW_ADVANCED, 0},
        };
        struct leonos_ui_rect r;
        uint32_t selected = page == 0 ? DEMO_VIEW_CONTROLS
                          : page == 1 ? DEMO_VIEW_DATA
                          : page == 2 ? DEMO_VIEW_DIALOGS
                                      : DEMO_VIEW_ADVANCED;
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    DEMO_MENU_VIEW, &r);
        leonos_ui_menu_popup(ui, (uint32_t)r.x, DEMO_MENU_BAR_H, 154,
                             items, sizeof(items) / sizeof(items[0]), selected);
    } else if (menu_open == DEMO_MENU_HELP) {
        struct leonos_ui_context_menu_item items[] = {
            {"About", DEMO_HELP_ABOUT, 0},
        };
        struct leonos_ui_rect r;
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    DEMO_MENU_HELP, &r);
        leonos_ui_menu_popup(ui, (uint32_t)r.x, DEMO_MENU_BAR_H, 154,
                             items, sizeof(items) / sizeof(items[0]), 0);
    }
    leonos_ui_toast_draw(ui, &demo_toast, leonos_uptime_ms());
}

static int handle_menu_click(int32_t x, int32_t y, uint32_t *page)
{
    struct leonos_ui_menubar_item top_items[] = {
        {"File", DEMO_MENU_FILE, 54, 0},
        {"View", DEMO_MENU_VIEW, 54, 0},
        {"Help", DEMO_MENU_HELP, 54, 0},
    };
    uint32_t id = 0;
    if (leonos_ui_menubar_hit(x, y, 0, 0, top_items,
                              sizeof(top_items) / sizeof(top_items[0]), &id)) {
        if (id) {
            menu_open = menu_open == id ? DEMO_MENU_NONE : (uint8_t)id;
            return 1;
        }
        menu_open = DEMO_MENU_NONE;
        return 1;
    }
    if (menu_open == DEMO_MENU_FILE) {
        struct leonos_ui_context_menu_item items[] = {
            {"Message", DEMO_FILE_MESSAGE, 0},
            {"Input", DEMO_FILE_INPUT, 0},
        };
        struct leonos_ui_rect r;
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    DEMO_MENU_FILE, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, DEMO_MENU_BAR_H,
                                     154, items,
                                     sizeof(items) / sizeof(items[0]), &id)) {
            menu_open = DEMO_MENU_NONE;
            if (id == DEMO_FILE_MESSAGE) {
                leonos_ui_show_message_box("UI Components", "This is a standalone dialog window.", "OK");
            } else if (id == DEMO_FILE_INPUT) {
                leonos_ui_show_input_dialog("Input", "Path:", sample_path, sizeof(sample_path));
            }
            return 1;
        }
        menu_open = DEMO_MENU_NONE;
        return 1;
    }
    if (menu_open == DEMO_MENU_VIEW) {
        struct leonos_ui_context_menu_item items[] = {
            {"Controls", DEMO_VIEW_CONTROLS, 0},
            {"Data", DEMO_VIEW_DATA, 0},
            {"Dialogs", DEMO_VIEW_DIALOGS, 0},
            {"Advanced", DEMO_VIEW_ADVANCED, 0},
        };
        struct leonos_ui_rect r;
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    DEMO_MENU_VIEW, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, DEMO_MENU_BAR_H,
                                     154, items,
                                     sizeof(items) / sizeof(items[0]), &id)) {
            if (id >= DEMO_VIEW_CONTROLS && id <= DEMO_VIEW_ADVANCED) {
                *page = id - DEMO_VIEW_CONTROLS;
            }
            menu_open = DEMO_MENU_NONE;
            return 1;
        }
        menu_open = DEMO_MENU_NONE;
        return 1;
    }
    if (menu_open == DEMO_MENU_HELP) {
        struct leonos_ui_context_menu_item items[] = {
            {"About", DEMO_HELP_ABOUT, 0},
        };
        struct leonos_ui_rect r;
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    DEMO_MENU_HELP, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, DEMO_MENU_BAR_H,
                                     154, items,
                                     sizeof(items) / sizeof(items[0]), &id)) {
            menu_open = DEMO_MENU_NONE;
            if (id == DEMO_HELP_ABOUT) {
                leonos_ui_show_message_box("UI Components", "Gallery for LeonOS UI controls.", "OK");
            }
            return 1;
        }
        menu_open = DEMO_MENU_NONE;
        return 1;
    }
    return 0;
}

static int handle_advanced_mouse(int32_t x, int32_t y, uint32_t buttons)
{
    struct leonos_ui_tree_item tree_items[] = {
        {"0:/", 10, 0, LEONOS_UI_TREE_EXPANDED},
        {"system", 11, 1, LEONOS_UI_TREE_EXPANDED},
        {"fonts", 12, 2, LEONOS_UI_TREE_LEAF},
        {"resources", 13, 2, LEONOS_UI_TREE_LEAF},
        {"userland", 20, 1, LEONOS_UI_TREE_EXPANDED},
        {"fileman.elf", 21, 2, LEONOS_UI_TREE_LEAF},
        {"uidemo.elf", 22, 2, LEONOS_UI_TREE_LEAF},
    };
    struct leonos_ui_context_menu_item context_items[] = {
        {"Open", DEMO_CTX_OPEN, 0},
        {"Rename", DEMO_CTX_RENAME, 0},
        {"Disabled item", DEMO_CTX_DISABLED, LEONOS_UI_MENU_DISABLED},
    };
    uint32_t id = 0;
    if ((buttons & 1u) && context_menu_open) {
        if (leonos_ui_context_menu_hit(x, y, context_menu_x, context_menu_y,
                                       DEMO_CONTEXT_MENU_W, context_items,
                                       sizeof(context_items) / sizeof(context_items[0]), &id)) {
            context_menu_set_open(0);
            if (id == DEMO_CTX_OPEN) {
                copy_text(demo_status, sizeof(demo_status), "Context menu command: Open");
            } else if (id == DEMO_CTX_RENAME) {
                copy_text(demo_status, sizeof(demo_status), "Context menu command: Rename");
            }
            return 1;
        }
        context_menu_set_open(0);
        return 1;
    }
    if (buttons & 2u) {
        if (x < 0) {
            x = 0;
        }
        if (y < 0) {
            y = 0;
        }
        context_menu_x = (uint32_t)x;
        context_menu_y = (uint32_t)y;
        if (context_menu_x + DEMO_CONTEXT_MENU_W > DEMO_W) {
            context_menu_x = DEMO_W - DEMO_CONTEXT_MENU_W;
        }
        if (context_menu_y + leonos_ui_context_menu_height(sizeof(context_items) / sizeof(context_items[0])) >
            DEMO_H - 28) {
            context_menu_y = DEMO_H - 28 -
                leonos_ui_context_menu_height(sizeof(context_items) / sizeof(context_items[0]));
        }
        context_menu_set_open(1);
        copy_text(demo_status, sizeof(demo_status), "Reusable context menu opened");
        return 1;
    }
    if (buttons & 1u) {
        if (leonos_ui_tree_hit(x, y, 32, 298, 292, tree_items,
                               sizeof(tree_items) / sizeof(tree_items[0]),
                               DEMO_TREE_ROW_H, &id)) {
            if (id) {
                tree_selected_id = id;
                copy_text(demo_status, sizeof(demo_status), "Tree selection changed");
            }
            context_menu_set_open(0);
            return 1;
        }
        context_menu_set_open(0);
    }
    return 0;
}

static int handle_controls_mouse(int32_t x, int32_t y, uint32_t buttons)
{
    int changed = 0;
    uint32_t id = 0;
    if (!(buttons & 1u)) {
        return 0;
    }
    if (leonos_ui_date_input_handle_mouse(&sample_date, x, y, 416, 286, 300, 0)) {
        sample_color.focused = 0;
        sample_edit.focused = 0;
        sample_area.focused = 0;
        copy_text(demo_status, sizeof(demo_status), "Date input changed");
        return 1;
    }
    if (leonos_ui_color_input_handle_mouse(&sample_color, x, y, 416, 376, 300, 0)) {
        sample_date.focused = 0;
        sample_edit.focused = 0;
        sample_area.focused = 0;
        copy_text(demo_status, sizeof(demo_status), "Color input changed");
        return 1;
    }
    if (dropdown_open || dropdown_animating) {
        if (leonos_ui_dropdown_hit(x, y, 274, 164, 204, priority_items,
                                   sizeof(priority_items) / sizeof(priority_items[0]),
                                   DEMO_DROPDOWN_ROW_H, dropdown_progress(), &id)) {
            if (id) {
                dropdown_selected = id;
                copy_text(demo_status, sizeof(demo_status), "Dropdown selection changed");
            }
            dropdown_set_open(0);
            return 1;
        }
    }
    if (hit_rect_i(x, y, 274, 140, 204, (int32_t)(LEONOS_FONT_H + 8))) {
        sample_edit.focused = 0;
        sample_area.focused = 0;
        dropdown_set_open(dropdown_open ? 0 : 1);
        copy_text(demo_status, sizeof(demo_status),
                  dropdown_open ? "Dropdown opening" : "Dropdown closing");
        return 1;
    }
    if (leonos_ui_slider_handle_mouse(&demo_slider_value, 100, 536, 142, 172, 22, x, y)) {
        copy_text(demo_status, sizeof(demo_status), "Slider changed");
        leonos_ui_toast_show(&demo_toast, "Slider value updated",
                             leonos_uptime_ms(), 1800, LEONOS_UI_TOAST_INFO);
        return 1;
    }
    if (leonos_ui_stepper_handle_mouse(&demo_stepper_value, 1, 5, 1,
                                       536, 176, 126, LEONOS_UI_BUTTON_H,
                                       x, y)) {
        copy_text(demo_status, sizeof(demo_status), "Stepper changed");
        leonos_ui_toast_show(&demo_toast, "Stepper changed",
                             leonos_uptime_ms(), 1800, LEONOS_UI_TOAST_SUCCESS);
        return 1;
    }
    dropdown_set_open(0);
    if (x >= 274 && x < 478 &&
        y >= 106 && y < 106 + (int32_t)(LEONOS_FONT_H + 8)) {
        sample_area.focused = 0;
        changed |= leonos_ui_edit_state_handle_mouse(&sample_edit, x, y,
                                                     274, 106, 204, buttons);
    } else if (x >= 274 && x < 478 && y >= 174 && y < 218) {
        sample_edit.focused = 0;
        changed |= leonos_ui_text_area_state_handle_mouse(&sample_area, x, y,
                                                          274, 174, 204, 44, buttons);
    } else {
        sample_edit.focused = 0;
        sample_area.focused = 0;
        changed = 1;
    }
    return changed;
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    uint32_t page = 0;
    puts("[uidemo.elf] UI component gallery starting");
    run_unicode_file_test();
    printf("[uidemo.elf] pid=%d creating UI Components window\n", getpid());
    window_id = leonos_gui_create_app_window_ex("UI Components", "LeonOS UI component gallery",
                                                DEMO_W, DEMO_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[uidemo.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, DEMO_W, DEMO_H, DEMO_W);
    leonos_ui_edit_state_init(&sample_edit, sample_path, sizeof(sample_path));
    sample_edit.focused = 1;
    leonos_ui_text_area_state_init(&sample_area, sample_text, sizeof(sample_text));
    leonos_ui_listview_state_init(&sample_list, 3, 24);
    leonos_ui_listview_state_set_count(&sample_list, sizeof(data_rows) / sizeof(data_rows[0]));
    sample_list.selected = 1;
    leonos_ui_tab_state_init(&demo_tab_state, 0);
    leonos_ui_tab_state_init(&preview_tab_state, 1);
    leonos_ui_color_input_state_init(&sample_color, 0x0070a0e0U);
    leonos_ui_date_input_state_init(&sample_date, 2026, 7, 25);
    anim_start_ms = leonos_uptime_ms();
    draw_demo(&ui, page);
    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, 20U) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 3u)) {
                if (handle_menu_click(event.x, event.y, &page)) {
                    context_menu_set_open(0);
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                    continue;
                }
                if (page == 3 && handle_advanced_mouse(event.x, event.y, event.buttons)) {
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                    continue;
                }
                menu_open = DEMO_MENU_NONE;
                if (leonos_ui_tab_control_handle_mouse(&demo_tab_state, event.x, event.y,
                                                       236, 36, 410, demo_tabs, 4)) {
                    page = demo_tab_state.selected_id;
                    context_menu_set_open(0);
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                    continue;
                }
                context_menu_set_open(0);
                if (page == 0) {
                    int changed = handle_controls_mouse(event.x, event.y, event.buttons);
                    if (changed) {
                        draw_demo(&ui, page);
                        leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                    }
                } else if (page == 1) {
                    uint32_t activate = 0;
                    if (leonos_ui_listview_state_handle_mouse(&sample_list, event.x, event.y,
                                                              14, 158, DEMO_W - 46, &activate)) {
                        draw_demo(&ui, page);
                        leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                    }
                } else if (page == 2) {
                    if (event.x >= 22 && event.x < 138 && event.y >= 252 &&
                        event.y < 252 + (int32_t)LEONOS_UI_BUTTON_H) {
                        leonos_ui_show_message_box("Message", "Standalone message box.", "OK");
                    } else if (event.x >= 148 && event.x < 244 && event.y >= 252 &&
                               event.y < 252 + (int32_t)LEONOS_UI_BUTTON_H) {
                        leonos_ui_show_confirm_dialog("Confirm", "This is a dialog window.", 1);
                    } else if (event.x >= 254 && event.x < 340 && event.y >= 252 &&
                               event.y < 252 + (int32_t)LEONOS_UI_BUTTON_H) {
                        leonos_ui_show_input_dialog("Input", "Path:", sample_path, sizeof(sample_path));
                    }
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                } else if (page == 3) {
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_MOVE) {
                int changed = 0;
                if (page == 0 && (event.buttons & 1u)) {
                    changed = handle_controls_mouse(event.x, event.y, event.buttons);
                } else if (page == 0) {
                    if (sample_edit.selecting) {
                        changed |= leonos_ui_edit_state_handle_mouse(&sample_edit, event.x, event.y,
                                                                     274, 106, 204, 0);
                    }
                    if (sample_area.selecting) {
                        changed |= leonos_ui_text_area_state_handle_mouse(&sample_area, event.x, event.y,
                                                                          274, 174, 204, 44, 0);
                    }
                }
                if (changed) {
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                int changed = 0;
                if (page == 0 &&
                    (sample_area.focused ||
                     hit_rect_i(event.x, event.y, 274, 174, 204, 44))) {
                    changed = leonos_ui_vscrollbar_handle_wheel(&sample_area.scroll_line,
                                                               sample_area.line_count, 2,
                                                               event.dy);
                    if (changed) {
                        sample_area.focused = 1;
                    }
                } else if (page == 1) {
                    changed = leonos_ui_listview_state_handle_wheel(&sample_list, event.dy);
                }
                if (changed) {
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN || event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                int changed = 0;
                uint32_t activate = 0;
                menu_open = DEMO_MENU_NONE;
                if (page == 0) {
                    changed |= leonos_ui_edit_state_handle_key(&sample_edit, event.keycode, event.pressed);
                    changed |= leonos_ui_text_area_state_handle_key(&sample_area, event.keycode, event.pressed, 204, 44);
                    if (sample_color.focused) {
                        changed |= leonos_ui_color_input_handle_key(&sample_color, event.keycode, 0);
                    }
                    if (sample_date.focused) {
                        changed |= leonos_ui_date_input_handle_key(&sample_date, event.keycode, 0);
                    }
                } else if (page == 1) {
                    changed |= leonos_ui_listview_state_handle_key(&sample_list, event.keycode, &activate);
                    (void)activate;
                }
                if (changed) {
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE || event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                draw_demo(&ui, page);
                leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
            }
        } else {
            if (page == 3 || dropdown_animating || context_menu_animating || demo_toast.active) {
                unsigned long now = leonos_uptime_ms();
                if (now - last_anim_redraw_ms >= 50) {
                    last_anim_redraw_ms = now;
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                    if (now - anim_start_ms >= 1200) {
                        anim_start_ms = now;
                    }
                }
            }
            sleep_ms(10);
        }
    }
    return 0;
}
