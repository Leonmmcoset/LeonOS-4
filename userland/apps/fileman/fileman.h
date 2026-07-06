#ifndef LEONOS_FILEMAN_H
#define LEONOS_FILEMAN_H

#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/launch.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define FILEMAN_W 560
#define FILEMAN_H 360
#define FILEMAN_MAX_W 1264
#define FILEMAN_MAX_H 746
#define FILEMAN_MAX_ENTRIES LEONOS_FS_MAX_ENTRIES
#define TOOLBAR_Y 40
#define LIST_X 8
#define LIST_Y 82
#define ROW_H (LEONOS_FONT_H + 8)
#define STATUS_H 28
#define TREE_W 132
#define TREE_ROW_H 24
#define MENU_BAR_H 28
#define MENU_ITEM_H (LEONOS_FONT_H + 8)
#define FILEMAN_KEY_ESCAPE 1U
#define FILEMAN_KEY_UP 72U
#define FILEMAN_KEY_DOWN 80U
#define FILEMAN_CONTEXT_MENU_W 206
#define FILEMAN_CONTEXT_MENU_COUNT 10
#define FILEMAN_DETAILS_W 430
#define FILEMAN_DETAILS_H 220
#define FILEMAN_FOLDER_SIZE_MAX_DEPTH 16
#define FILEMAN_FOLDER_SIZE_MAX_ITEMS 2048
#define T(en, zh) leonos_i18n((en), (zh))

enum {
    FILEMAN_MENU_NONE = 0,
    FILEMAN_MENU_FILE = 1,
    FILEMAN_MENU_VIEW = 2,
};

enum {
    FILEMAN_ACTION_OPEN = 1,
    FILEMAN_ACTION_OPEN_WITH = 2,
    FILEMAN_ACTION_DEFAULT_PROGRAM = 3,
    FILEMAN_ACTION_DETAILS = 4,
    FILEMAN_ACTION_RENAME = 5,
    FILEMAN_ACTION_DELETE = 6,
    FILEMAN_ACTION_NEW_FOLDER = 7,
    FILEMAN_ACTION_UP = 8,
    FILEMAN_ACTION_REFRESH = 9,
    FILEMAN_ACTION_CREATE_SHORTCUT = 10,
    FILEMAN_ACTION_ROOT = 11,
    FILEMAN_ACTION_ABOUT = 12,
};

struct fileman_layout {
    uint32_t tree_x;
    uint32_t tree_y;
    uint32_t tree_w;
    uint32_t tree_h;
    uint32_t list_x;
    uint32_t list_y;
    uint32_t list_w;
    uint32_t list_h;
    uint32_t rows_y;
    uint32_t rows_h;
    uint32_t visible_rows;
    uint32_t scrollbar_x;
    uint32_t scrollbar_h;
};

struct folder_size_info {
    uint64_t bytes;
    uint32_t files;
    uint32_t folders;
    uint32_t visited;
    uint8_t partial;
};

extern uint32_t pixels[FILEMAN_MAX_W * FILEMAN_MAX_H];
extern uint32_t details_pixels[FILEMAN_DETAILS_W * FILEMAN_DETAILS_H];
extern struct leonos_dir_entry entries[FILEMAN_MAX_ENTRIES];
extern char current_path[LEONOS_FS_PATH_LEN];
extern char home_path[LEONOS_AUTH_HOME_LEN];
extern char status_text[96];
extern uint32_t entry_count;
extern struct leonos_ui_listview_state file_list;
extern int32_t last_click_index;
extern unsigned long last_click_ms;
extern uint8_t menu_open;
extern uint8_t context_menu_active;
extern uint8_t context_menu_animating;
extern uint8_t context_menu_opening;
extern unsigned long context_menu_anim_start;
extern uint32_t context_menu_x;
extern uint32_t context_menu_y;
extern uint32_t view_w;
extern uint32_t view_h;

struct fileman_layout current_layout(void);
void copy_text(char *dst, uint32_t dst_len, const char *src);
uint32_t text_len(const char *text);
int text_eq(const char *a, const char *b);
int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh);
int ends_with(const char *text, const char *suffix);
void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch);
void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text);
void append_dec(char *buf, uint32_t *pos, uint32_t cap, uint32_t value);
void append_size(char *buf, uint32_t *pos, uint32_t cap, uint64_t value);
int is_root_path(const char *path);
int selected_entry_valid(void);
int selected_entry_is_file(void);
int selected_entry_is_mutable(void);
int list_index_at(int32_t x, int32_t y);
void format_size_text(char *buf, uint32_t cap, uint64_t bytes);
void set_status(const char *text);
void set_status_code(const char *prefix, int value);
int permission_error(int value);
void set_status_error(const char *prefix, int value);
int refresh_home_path(void);
void context_menu_set_active(uint8_t active);
void build_child_path(char *dst, uint32_t dst_len, const char *name);
void build_path_join(char *dst, uint32_t dst_len, const char *parent, const char *name);
void build_parent_path(char *dst, uint32_t dst_len);
const char *path_basename(const char *path);
const char *entry_type_name(const struct leonos_dir_entry *entry);
void build_context_menu_items(struct leonos_ui_context_menu_item *items, uint32_t count);
void format_contains_text(char *buf, uint32_t cap, const struct folder_size_info *info);
int accumulate_folder_size(const char *path, struct folder_size_info *info, uint32_t depth);
void show_details_selected(void);
void show_open_with_for_path(const char *path, uint8_t set_default_only);
void show_open_with_selected(void);
void show_default_program_for_selected(void);
int reload_dir(void);
uint32_t build_tree_items(struct leonos_ui_tree_item *items, uint32_t cap);
const char *tree_path_for_id(uint32_t id);
int navigate_to_path(const char *path);
void draw_fileman(struct leonos_ui_surface *ui);
void open_selected_entry(void);
void navigate_up(void);
void navigate_root(void);
void create_new_folder(void);
void create_shortcut_for_selected(void);
void rename_selected_entry(void);
void delete_selected_entry(void);
void execute_action(uint32_t action);
int handle_menu_click(int32_t x, int32_t y);
int handle_context_menu_click(int32_t x, int32_t y);
void show_context_menu_at(int32_t x, int32_t y, int32_t target);
void handle_right_click(int32_t x, int32_t y);
void handle_click(int32_t x, int32_t y);
void handle_key(uint8_t keycode);
int handle_wheel(int32_t x, int32_t y, int32_t wheel);
void present_fileman(uint32_t window_id, struct leonos_ui_surface *ui);

#endif
