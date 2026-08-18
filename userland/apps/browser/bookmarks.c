#include "browser.h"

#define BROWSER_BOOKMARK_STORE_DIR "browser"
#define BROWSER_BOOKMARK_STORE_FILE "bookmarks.txt"
#define BROWSER_BOOKMARK_MENU_FIXED 4U

static char bookmark_store_path[LEONOS_FS_PATH_LEN];

static void bookmark_append_path(char *dst, uint32_t cap, const char *dir,
                                 const char *name)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, cap, dir);
    if (dir && dir[0] && dir[(uint32_t)strlen(dir) - 1U] != '/') {
        append_char(dst, &pos, cap, '/');
    }
    append_text(dst, &pos, cap, name);
}

static void bookmark_store_location(char *dst, uint32_t cap)
{
    struct leonos_user_info user;
    char dir[LEONOS_FS_PATH_LEN];
    if (leonos_auth_current(&user) == 0 && user.uid && user.home[0]) {
        bookmark_append_path(dir, sizeof(dir), user.home,
                             BROWSER_BOOKMARK_STORE_DIR);
        (void)mkdir(dir, 0);
    } else {
        (void)mkdir("0:/var", 0);
        bookmark_append_path(dir, sizeof(dir), "0:/var",
                             BROWSER_BOOKMARK_STORE_DIR);
        (void)mkdir(dir, 0);
    }
    bookmark_append_path(dst, cap, dir, BROWSER_BOOKMARK_STORE_FILE);
}

static void bookmark_clean(char *dst, uint32_t cap, const char *src)
{
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    for (uint32_t i = 0; src && src[i] && pos + 1U < cap; ++i) {
        char ch = src[i];
        if (ch != '\r' && ch != '\n' && ch != '\t') {
            dst[pos++] = ch;
        }
    }
    dst[pos] = 0;
}

static void bookmark_copy_range(char *dst, uint32_t cap, const char *src,
                                uint32_t len)
{
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    while (src && pos + 1U < cap && pos < len) {
        char ch = src[pos];
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            break;
        }
        dst[pos] = ch;
        ++pos;
    }
    dst[pos] = 0;
}

static void bookmark_save(void)
{
    char data[BROWSER_MAX_BOOKMARKS *
              (BROWSER_BOOKMARK_TITLE_CAP + BROWSER_URL_CAP + 2U) + 32U];
    uint32_t pos = 0;
    int fd;
    bookmark_store_location(bookmark_store_path, sizeof(bookmark_store_path));
    data[0] = 0;
    append_text(data, &pos, sizeof(data), "# LeonOS Browser bookmarks v1\n");
    for (uint32_t i = 0; i < browser_bookmark_count; ++i) {
        append_text(data, &pos, sizeof(data), browser_bookmarks[i].title);
        append_char(data, &pos, sizeof(data), '\t');
        append_text(data, &pos, sizeof(data), browser_bookmarks[i].url);
        append_char(data, &pos, sizeof(data), '\n');
    }
    fd = open(bookmark_store_path, LEONOS_O_WRONLY | LEONOS_O_CREAT |
              LEONOS_O_TRUNC, 0);
    if (fd >= 0) {
        (void)write(fd, data, pos);
        close(fd);
    }
}

static void bookmark_add(const char *title, const char *url)
{
    char clean_title[BROWSER_BOOKMARK_TITLE_CAP];
    char clean_url[BROWSER_URL_CAP];
    if (!url || !url[0]) {
        return;
    }
    bookmark_clean(clean_title, sizeof(clean_title), title);
    bookmark_clean(clean_url, sizeof(clean_url), url);
    if (!clean_title[0]) {
        copy_text(clean_title, sizeof(clean_title), clean_url);
    }
    for (uint32_t i = 0; i < browser_bookmark_count; ++i) {
        if (text_eq(browser_bookmarks[i].url, clean_url)) {
            copy_text(browser_bookmarks[i].title,
                      sizeof(browser_bookmarks[i].title), clean_title);
            bookmark_save();
            return;
        }
    }
    if (browser_bookmark_count >= BROWSER_MAX_BOOKMARKS) {
        for (uint32_t i = 1; i < BROWSER_MAX_BOOKMARKS; ++i) {
            browser_bookmarks[i - 1U] = browser_bookmarks[i];
        }
        browser_bookmark_count = BROWSER_MAX_BOOKMARKS - 1U;
    }
    copy_text(browser_bookmarks[browser_bookmark_count].title,
              sizeof(browser_bookmarks[browser_bookmark_count].title), clean_title);
    copy_text(browser_bookmarks[browser_bookmark_count].url,
              sizeof(browser_bookmarks[browser_bookmark_count].url), clean_url);
    ++browser_bookmark_count;
    bookmark_save();
}

void browser_bookmarks_load(void)
{
    char data[BROWSER_MAX_BOOKMARKS *
              (BROWSER_BOOKMARK_TITLE_CAP + BROWSER_URL_CAP + 2U) + 32U];
    uint32_t len = 0;
    int fd;
    browser_bookmark_count = 0;
    bookmark_store_location(bookmark_store_path, sizeof(bookmark_store_path));
    fd = open(bookmark_store_path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    while (len + 1U < sizeof(data)) {
        long got = read(fd, data + len, sizeof(data) - len - 1U);
        if (got <= 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    data[len] = 0;
    for (uint32_t start = 0; start < len &&
                              browser_bookmark_count < BROWSER_MAX_BOOKMARKS;) {
        uint32_t end = start;
        uint32_t tab = len;
        while (end < len && data[end] != '\r' && data[end] != '\n') {
            if (data[end] == '\t' && tab == len) {
                tab = end;
            }
            ++end;
        }
        if (tab > start && tab + 1U < end && data[start] != '#') {
            bookmark_copy_range(browser_bookmarks[browser_bookmark_count].title,
                                sizeof(browser_bookmarks[browser_bookmark_count].title),
                                data + start, tab - start);
            bookmark_copy_range(browser_bookmarks[browser_bookmark_count].url,
                                sizeof(browser_bookmarks[browser_bookmark_count].url),
                                data + tab + 1U, end - tab - 1U);
            if (browser_bookmarks[browser_bookmark_count].title[0] &&
                browser_bookmarks[browser_bookmark_count].url[0]) {
                ++browser_bookmark_count;
            }
        }
        while (end < len && (data[end] == '\r' || data[end] == '\n')) {
            ++end;
        }
        start = end;
    }
    for (uint32_t i = 0; i < browser_bookmark_count; ++i) {
        for (uint32_t j = i + 1U; j < browser_bookmark_count;) {
            if (text_eq(browser_bookmarks[i].url, browser_bookmarks[j].url)) {
                for (uint32_t k = j + 1U; k < browser_bookmark_count; ++k) {
                    browser_bookmarks[k - 1U] = browser_bookmarks[k];
                }
                --browser_bookmark_count;
            } else {
                ++j;
            }
        }
    }
}

void browser_bookmarks_add_current(void)
{
    char title[BROWSER_BOOKMARK_TITLE_CAP];
    if (!current_location[0] || starts_with_ignore_case(current_location, "about:")) {
        set_status(T("Only pages with an address can be bookmarked",
                     "只有带地址的页面可以加入书签"));
        return;
    }
    copy_text(title, sizeof(title), page_title);
    if (!leonos_ui_show_input_dialog(T("Add Bookmark", "添加书签"),
                                     T("Title:", "标题:"), title,
                                     sizeof(title))) {
        return;
    }
    bookmark_add(title, current_location);
    set_status(T("Bookmark saved", "书签已保存"));
}

void browser_bookmarks_build_menu(struct leonos_ui_context_menu_item *items,
                                  uint32_t capacity, uint32_t *out_count)
{
    uint32_t count = 0;
    if (!items || capacity < BROWSER_BOOKMARK_MENU_FIXED) {
        return;
    }
    items[count++] = (struct leonos_ui_context_menu_item){
        T("Add Current Page", "添加当前页面"), BROWSER_CMD_FAV_ADD, 0};
    items[count++] = (struct leonos_ui_context_menu_item){
        T("Manage Bookmarks...", "管理书签..."), BROWSER_CMD_FAV_MANAGE, 0};
    items[count++] = (struct leonos_ui_context_menu_item){"", 0,
        LEONOS_UI_MENU_SEPARATOR};
    for (uint32_t i = 0; i < browser_bookmark_count && count < capacity; ++i) {
        items[count++] = (struct leonos_ui_context_menu_item){
            browser_bookmarks[i].title, BROWSER_CMD_FAV_BOOKMARK_BASE + i, 0};
    }
    if (count == 3U && count < capacity) {
        items[count++] = (struct leonos_ui_context_menu_item){
            T("No saved bookmarks", "没有保存的书签"), 0,
            LEONOS_UI_MENU_DISABLED};
    }
    if (out_count) {
        *out_count = count;
    }
}

int browser_bookmarks_handle_command(uint32_t command, char *out_url,
                                     uint32_t out_cap)
{
    if (out_url && out_cap) {
        out_url[0] = 0;
    }
    if (command == BROWSER_CMD_FAV_ADD) {
        browser_bookmarks_add_current();
        return 1;
    }
    if (command == BROWSER_CMD_FAV_MANAGE) {
        return browser_show_bookmark_manager(out_url, out_cap);
    }
    if (command >= BROWSER_CMD_FAV_BOOKMARK_BASE &&
        command < BROWSER_CMD_FAV_BOOKMARK_BASE + browser_bookmark_count) {
        copy_text(out_url, out_cap,
                  browser_bookmarks[command - BROWSER_CMD_FAV_BOOKMARK_BASE].url);
        return 1;
    }
    return 0;
}

int browser_show_bookmark_manager(char *out_url, uint32_t out_cap)
{
    enum { W = 500, H = 330, ROW_H = 28, LIST_Y = 48, LIST_ROWS = 7 };
    static uint32_t pixels[W * H];
    struct leonos_ui_surface surface;
    struct leonos_gui_app_event event;
    int window_id;
    int32_t selected = browser_bookmark_count ? 0 : -1;
    int result = 0;
    if (out_url && out_cap) {
        out_url[0] = 0;
    }
    window_id = leonos_gui_create_app_window_ex(T("Bookmarks", "书签"),
                                                 T("Saved browser pages", "已保存的网页"),
                                                 W, H,
                                                 LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return 0;
    }
    leonos_ui_bind(&surface, pixels, W, H, W);
    for (;;) {
        leonos_ui_rect(&surface, 0, 0, W, H, LEONOS_UI_GRAY);
        leonos_ui_text(&surface, 18, 18, T("Bookmarks", "书签"),
                       LEONOS_UI_BLACK, LEONOS_UI_GRAY);
        leonos_ui_panel(&surface, 16, LIST_Y, W - 32, LIST_ROWS * ROW_H,
        LEONOS_UI_WHITE);
        for (uint32_t i = 0; i < browser_bookmark_count && i < LIST_ROWS; ++i) {
            uint32_t y = LIST_Y + i * ROW_H;
            uint32_t selected_row = selected == (int32_t)i;
            uint32_t bg = selected_row ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_WHITE;
            uint32_t fg = selected_row ? LEONOS_UI_WHITE : LEONOS_UI_BLACK;
            uint32_t detail_fg = selected_row ? LEONOS_UI_WHITE : LEONOS_UI_DARK;
            leonos_ui_rect(&surface, 18, y + 1U, W - 36, ROW_H - 2U, bg);
            leonos_ui_text_clipped(&surface, 28, y + 7U, 180,
                                   browser_bookmarks[i].title,
                                   fg, bg);
            leonos_ui_text_clipped(&surface, 216, y + 7U, W - 244,
                                   browser_bookmarks[i].url,
                                   detail_fg, bg);
        }
        if (!browser_bookmark_count) {
            leonos_ui_text(&surface, 28, LIST_Y + 10,
                           T("No saved bookmarks", "没有保存的书签"),
                           LEONOS_UI_DARK, LEONOS_UI_WHITE);
        }
        leonos_ui_button(&surface, 16, H - 42, 72, LEONOS_UI_BUTTON_H,
                         T("Open", "打开"), selected < 0 ? LEONOS_UI_BUTTON_DISABLED : 0);
        leonos_ui_button(&surface, 96, H - 42, 72, LEONOS_UI_BUTTON_H,
                         T("Add", "添加"), 0);
        leonos_ui_button(&surface, 176, H - 42, 72, LEONOS_UI_BUTTON_H,
                         T("Edit", "编辑"), selected < 0 ? LEONOS_UI_BUTTON_DISABLED : 0);
        leonos_ui_button(&surface, 256, H - 42, 72, LEONOS_UI_BUTTON_H,
                         T("Delete", "删除"), selected < 0 ? LEONOS_UI_BUTTON_DISABLED : 0);
        leonos_ui_button(&surface, W - 88, H - 42, 72, LEONOS_UI_BUTTON_H,
                         T("Close", "关闭"), 0);
        leonos_gui_present_window((uint32_t)window_id, W, H, W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) <= 0) {
            sleep_ms(10);
            continue;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_CLOSE ||
            (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed &&
             event.keycode == 1U)) {
            break;
        }
        if (event.type != LEONOS_GUI_APP_EVENT_MOUSE_BUTTON ||
            !(event.buttons & 1U)) {
            continue;
        }
        if (event.y >= LIST_Y && event.y < LIST_Y + LIST_ROWS * ROW_H) {
            uint32_t index = (uint32_t)(event.y - LIST_Y) / ROW_H;
            if (index < browser_bookmark_count) {
                selected = (int32_t)index;
            }
            continue;
        }
        if (event.y < H - 42 ||
            event.y >= (int32_t)(H - 42 + LEONOS_UI_BUTTON_H)) {
            continue;
        }
        if (event.x >= 16 && event.x < 88 && selected >= 0) {
            copy_text(out_url, out_cap, browser_bookmarks[(uint32_t)selected].url);
            result = 1;
            break;
        }
        if (event.x >= 96 && event.x < 168) {
            char title[BROWSER_BOOKMARK_TITLE_CAP] = "New Bookmark";
            char url[BROWSER_URL_CAP] = "http://";
            if (leonos_ui_show_input_dialog(T("Add Bookmark", "添加书签"),
                                            T("Title:", "标题:"), title, sizeof(title)) &&
                leonos_ui_show_input_dialog(T("Add Bookmark", "添加书签"),
                                            T("URL:", "地址:"), url, sizeof(url)) && url[0]) {
                bookmark_add(title, url);
                selected = (int32_t)browser_bookmark_count - 1;
            }
            continue;
        }
        if (event.x >= 176 && event.x < 248 && selected >= 0) {
            char title[BROWSER_BOOKMARK_TITLE_CAP];
            char url[BROWSER_URL_CAP];
            uint32_t index = (uint32_t)selected;
            copy_text(title, sizeof(title), browser_bookmarks[index].title);
            copy_text(url, sizeof(url), browser_bookmarks[index].url);
            if (leonos_ui_show_input_dialog(T("Edit Bookmark", "编辑书签"),
                                            T("Title:", "标题:"), title, sizeof(title)) &&
                leonos_ui_show_input_dialog(T("Edit Bookmark", "编辑书签"),
                                            T("URL:", "地址:"), url, sizeof(url)) && url[0]) {
                bookmark_clean(browser_bookmarks[index].title,
                               sizeof(browser_bookmarks[index].title), title);
                bookmark_clean(browser_bookmarks[index].url,
                               sizeof(browser_bookmarks[index].url), url);
                bookmark_save();
            }
            continue;
        }
        if (event.x >= 256 && event.x < 328 && selected >= 0) {
            uint32_t index = (uint32_t)selected;
            if (leonos_ui_show_confirm_dialog(T("Delete Bookmark", "删除书签"),
                                              browser_bookmarks[index].title, 0)) {
                for (uint32_t i = index + 1U; i < browser_bookmark_count; ++i) {
                    browser_bookmarks[i - 1U] = browser_bookmarks[i];
                }
                --browser_bookmark_count;
                if (selected >= (int32_t)browser_bookmark_count) {
                    selected = (int32_t)browser_bookmark_count - 1;
                }
                bookmark_save();
            }
            continue;
        }
        if (event.x >= W - 88) {
            break;
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return result;
}

static int find_in_source(uint32_t start, uint32_t *out_offset)
{
    uint32_t needle_len = (uint32_t)strlen(browser_find_query);
    uint32_t source_len = (uint32_t)strlen(page_source);
    if (!needle_len || start >= source_len || source_len < needle_len) {
        return 0;
    }
    for (uint32_t pos = start; pos + needle_len <= source_len; ++pos) {
        uint32_t i = 0;
        while (i < needle_len &&
               ascii_tolower(page_source[pos + i]) ==
                   ascii_tolower(browser_find_query[i])) {
            ++i;
        }
        if (i == needle_len) {
            *out_offset = pos;
            return 1;
        }
    }
    return 0;
}

void browser_find_next(void)
{
    uint32_t offset;
    uint32_t needle_len = (uint32_t)strlen(browser_find_query);
    uint32_t source_len = (uint32_t)strlen(page_source);
    if (!needle_len || !source_len) {
        return;
    }
    if (browser_find_start >= source_len) {
        browser_find_start = 0;
    }
    if (find_in_source(browser_find_start +
                       (browser_find_len ? browser_find_len : 0U), &offset) ||
        find_in_source(0, &offset)) {
            browser_find_row = -1;
            browser_find_start = offset;
            browser_find_len = needle_len;
            set_status(T("Text found", "已找到文本"));
            return;
    }
    browser_find_row = -1;
    browser_find_start = 0;
    browser_find_len = 0;
    set_status(T("Text not found", "未找到文本"));
}

void browser_find_prompt(void)
{
    char query[BROWSER_FIND_CAP];
    copy_text(query, sizeof(query), browser_find_query);
    if (!leonos_ui_show_input_dialog(T("Find in Page", "在页面中查找"),
                                     T("Find:", "查找:"), query,
                                     sizeof(query))) {
        return;
    }
    copy_text(browser_find_query, sizeof(browser_find_query), query);
    browser_find_row = -1;
    browser_find_start = 0;
    browser_find_len = 0;
    browser_find_next();
}
