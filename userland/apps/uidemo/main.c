#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define DEMO_W 760
#define DEMO_H 520
#define DEMO_MENU_BAR_H 28
#define DEMO_MENU_ITEM_H (LEONOS_FONT_H + 8)

enum {
    DEMO_MENU_NONE = 0,
    DEMO_MENU_FILE = 1,
    DEMO_MENU_VIEW = 2,
    DEMO_MENU_HELP = 3,
};

static uint32_t pixels[DEMO_W * DEMO_H];

static const char *tab_labels[] = {"Controls", "Data", "Dialogs"};
static char sample_path[96] = "0:/userland/notepad.elf";
static char sample_text[160] = "Line one\nLine two\nLine three";
static struct leonos_ui_edit_state sample_edit;
static struct leonos_ui_text_area_state sample_area;
static struct leonos_ui_listview_state sample_list;
static uint8_t menu_open;

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void draw_controls_page(struct leonos_ui_surface *ui)
{
    leonos_ui_groupbox(ui, 12, 80, 230, 152, "Buttons and Inputs");
    leonos_ui_button(ui, 26, 106, 74, LEONOS_UI_BUTTON_H, "OK", 0);
    leonos_ui_button(ui, 108, 106, 92, LEONOS_UI_BUTTON_H, "Pressed", LEONOS_UI_BUTTON_PRESSED);
    leonos_ui_button(ui, 26, 138, 96, LEONOS_UI_BUTTON_H, "Disabled", LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_checkbox(ui, 26, 174, "Checked", 1, 0);
    leonos_ui_radio(ui, 26, 200, "Radio", 1, 0);

    leonos_ui_groupbox(ui, 258, 80, 246, 152, "Editors");
    leonos_ui_edit_state_draw(ui, 274, 106, 204, &sample_edit, 0);
    leonos_ui_combobox(ui, 274, 140, 204, "Normal priority", 0, 0);
    leonos_ui_text_area_state_draw(ui, 274, 174, 204, 44, &sample_area, 0);

    leonos_ui_groupbox(ui, 520, 80, 220, 152, "Progress");
    leonos_ui_progress(ui, 536, 110, 172, 18, 65, 100);
    leonos_ui_vscrollbar(ui, 710, 104, 18, 96, 4, 20, 8, 0);
    leonos_ui_hscrollbar(ui, 536, 204, 172, 18, 6, 20, 8, 0);
}

static void draw_data_page(struct leonos_ui_surface *ui)
{
    struct leonos_ui_list_column cols[] = {
        {"Name", 170},
        {"Type", 96},
        {"State", 120},
        {"Info", 240},
    };
    const char *row0[] = {"fileman.elf", "Program", "Running", "Uses toolbar, listview, statusbar"};
    const char *row1[] = {"notepad.elf", "Program", "Running", "Uses menubar, scroll view, statusbar"};
    const char *row2[] = {"taskmgr.elf", "Program", "Idle", "Uses listview and live status"};

    leonos_ui_toolbar(ui, 12, 82, DEMO_W - 24, 34);
    leonos_ui_toolbar_button(ui, 20, 87, 72, "New", 0);
    leonos_ui_toolbar_button(ui, 98, 87, 72, "Open", LEONOS_UI_BUTTON_PRESSED);
    leonos_ui_toolbar_button(ui, 176, 87, 90, "Refresh", 0);
    leonos_ui_splitter(ui, 278, 87, 4, 24, 1);
    leonos_ui_toolbar_button(ui, 292, 87, 92, "Disabled", LEONOS_UI_BUTTON_DISABLED);

    leonos_ui_scroll_view_frame(ui, 12, 128, DEMO_W - 42, 116);
    leonos_ui_listview_header(ui, 14, 130, DEMO_W - 46, cols, 4);
    const char *const *rows[] = {row0, row1, row2};
    for (uint32_t i = 0; i < 3; ++i) {
        leonos_ui_listview_row(ui, 14, 158 + i * 24, DEMO_W - 46, cols, rows[i], 4,
                               sample_list.selected == (int32_t)i ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(ui, DEMO_W - 28, 128, 18, 116, sample_list.scroll, 12, 4, 0);

    leonos_ui_groupbox(ui, 12, 268, 300, 92, "Tabs and Splitter");
    leonos_ui_tabs(ui, 28, 294, 268, tab_labels, 3, 1);
    leonos_ui_tab_body(ui, 28, 324, 268, 26);
    leonos_ui_text(ui, 36, 330, "Tab body frame", LEONOS_UI_DARK, LEONOS_UI_WHITE);

    leonos_ui_groupbox(ui, 332, 268, 300, 92, "Menus");
    leonos_ui_menubar(ui, 348, 294, 254);
    leonos_ui_menubar_item(ui, 354, 294, 52, "File", 1);
    leonos_ui_menubar_item(ui, 410, 294, 52, "Edit", 0);
    leonos_ui_menubar_item(ui, 466, 294, 52, "View", 0);
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

static void draw_demo(struct leonos_ui_surface *ui, uint32_t page)
{
    leonos_ui_rect(ui, 0, 0, DEMO_W, DEMO_H, LEONOS_UI_WHITE);
    leonos_ui_menubar(ui, 0, 0, DEMO_W);
    leonos_ui_menubar_item(ui, 8, 0, 54, "File", menu_open == DEMO_MENU_FILE);
    leonos_ui_menubar_item(ui, 64, 0, 54, "View", menu_open == DEMO_MENU_VIEW);
    leonos_ui_menubar_item(ui, 120, 0, 54, "Help", menu_open == DEMO_MENU_HELP);
    leonos_ui_text(ui, 12, 36, "LeonOS UI Component Library", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 12, 54, "Win32-style reusable drawing controls", LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_tabs(ui, 236, 36, 300, tab_labels, 3, page);
    if (page == 0) {
        draw_controls_page(ui);
    } else if (page == 1) {
        draw_data_page(ui);
    } else {
        draw_dialogs_page(ui);
    }
    leonos_ui_statusbar(ui, DEMO_H - 28, 28, "Click inside the top tabs to switch pages");

    if (menu_open == DEMO_MENU_FILE) {
        leonos_ui_menu(ui, 8, DEMO_MENU_BAR_H, 154, 60);
        leonos_ui_menu_item(ui, 42, DEMO_MENU_BAR_H + 8, 116, "Message", 0);
        leonos_ui_menu_item(ui, 42, DEMO_MENU_BAR_H + 34, 116, "Input", 0);
    } else if (menu_open == DEMO_MENU_VIEW) {
        leonos_ui_menu(ui, 64, DEMO_MENU_BAR_H, 154, 86);
        leonos_ui_menu_item(ui, 98, DEMO_MENU_BAR_H + 8, 116, "Controls", page == 0 ? LEONOS_UI_MENU_SELECTED : 0);
        leonos_ui_menu_item(ui, 98, DEMO_MENU_BAR_H + 34, 116, "Data", page == 1 ? LEONOS_UI_MENU_SELECTED : 0);
        leonos_ui_menu_item(ui, 98, DEMO_MENU_BAR_H + 60, 116, "Dialogs", page == 2 ? LEONOS_UI_MENU_SELECTED : 0);
    } else if (menu_open == DEMO_MENU_HELP) {
        leonos_ui_menu(ui, 120, DEMO_MENU_BAR_H, 154, 34);
        leonos_ui_menu_item(ui, 154, DEMO_MENU_BAR_H + 8, 116, "About", 0);
    }
}

static int handle_menu_click(int32_t x, int32_t y, uint32_t *page)
{
    if (y >= 0 && y < (int32_t)DEMO_MENU_BAR_H) {
        if (hit_rect_i(x, y, 8, 0, 54, (int32_t)DEMO_MENU_BAR_H)) {
            menu_open = menu_open == DEMO_MENU_FILE ? DEMO_MENU_NONE : DEMO_MENU_FILE;
            return 1;
        }
        if (hit_rect_i(x, y, 64, 0, 54, (int32_t)DEMO_MENU_BAR_H)) {
            menu_open = menu_open == DEMO_MENU_VIEW ? DEMO_MENU_NONE : DEMO_MENU_VIEW;
            return 1;
        }
        if (hit_rect_i(x, y, 120, 0, 54, (int32_t)DEMO_MENU_BAR_H)) {
            menu_open = menu_open == DEMO_MENU_HELP ? DEMO_MENU_NONE : DEMO_MENU_HELP;
            return 1;
        }
        menu_open = DEMO_MENU_NONE;
        return 1;
    }
    if (menu_open == DEMO_MENU_FILE) {
        if (hit_rect_i(x, y, 42, (int32_t)DEMO_MENU_BAR_H + 8, 116,
                       (int32_t)DEMO_MENU_ITEM_H)) {
            menu_open = DEMO_MENU_NONE;
            leonos_ui_show_message_box("UI Components", "This is a standalone dialog window.", "OK");
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)DEMO_MENU_BAR_H + 34, 116,
                       (int32_t)DEMO_MENU_ITEM_H)) {
            menu_open = DEMO_MENU_NONE;
            leonos_ui_show_input_dialog("Input", "Path:", sample_path, sizeof(sample_path));
            return 1;
        }
        menu_open = DEMO_MENU_NONE;
        return 1;
    }
    if (menu_open == DEMO_MENU_VIEW) {
        if (hit_rect_i(x, y, 98, (int32_t)DEMO_MENU_BAR_H + 8, 116,
                       (int32_t)DEMO_MENU_ITEM_H)) {
            *page = 0;
            menu_open = DEMO_MENU_NONE;
            return 1;
        }
        if (hit_rect_i(x, y, 98, (int32_t)DEMO_MENU_BAR_H + 34, 116,
                       (int32_t)DEMO_MENU_ITEM_H)) {
            *page = 1;
            menu_open = DEMO_MENU_NONE;
            return 1;
        }
        if (hit_rect_i(x, y, 98, (int32_t)DEMO_MENU_BAR_H + 60, 116,
                       (int32_t)DEMO_MENU_ITEM_H)) {
            *page = 2;
            menu_open = DEMO_MENU_NONE;
            return 1;
        }
        menu_open = DEMO_MENU_NONE;
        return 1;
    }
    if (menu_open == DEMO_MENU_HELP) {
        if (hit_rect_i(x, y, 154, (int32_t)DEMO_MENU_BAR_H + 8, 116,
                       (int32_t)DEMO_MENU_ITEM_H)) {
            menu_open = DEMO_MENU_NONE;
            leonos_ui_show_message_box("UI Components", "Gallery for LeonOS UI controls.", "OK");
            return 1;
        }
        menu_open = DEMO_MENU_NONE;
        return 1;
    }
    return 0;
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    uint32_t page = 0;
    puts("[uidemo.elf] UI component gallery starting");
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
    leonos_ui_listview_state_set_count(&sample_list, 3);
    sample_list.selected = 1;
    draw_demo(&ui, page);
    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                if (handle_menu_click(event.x, event.y, &page)) {
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                    continue;
                }
                menu_open = DEMO_MENU_NONE;
                int next = leonos_ui_tabs_hit(event.x, event.y, 236, 36, 300, tab_labels, 3);
                if (next >= 0) {
                    page = (uint32_t)next;
                    draw_demo(&ui, page);
                    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
                    continue;
                }
                if (page == 0) {
                    int changed = 0;
                    if (event.x >= 274 && event.x < 478 &&
                        event.y >= 106 && event.y < 106 + (int32_t)(LEONOS_FONT_H + 8)) {
                        sample_area.focused = 0;
                        changed |= leonos_ui_edit_state_handle_mouse(&sample_edit, event.x, event.y,
                                                                     274, 106, 204, event.buttons);
                    } else if (event.x >= 274 && event.x < 478 && event.y >= 174 && event.y < 218) {
                        sample_edit.focused = 0;
                        changed |= leonos_ui_text_area_state_handle_mouse(&sample_area, event.x, event.y,
                                                                          274, 174, 204, 44, event.buttons);
                    } else {
                        sample_edit.focused = 0;
                        sample_area.focused = 0;
                        changed = 1;
                    }
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
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                int changed = 0;
                uint32_t activate = 0;
                menu_open = DEMO_MENU_NONE;
                if (page == 0) {
                    changed |= leonos_ui_edit_state_handle_key(&sample_edit, event.keycode);
                    changed |= leonos_ui_text_area_state_handle_key(&sample_area, event.keycode, 204, 44);
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
            sleep_ms(10);
        }
    }
    return 0;
}
