#include "desktop.h"

#define DESKTOP_INPUTM_CONFIG_MAX 2048U

static char desktop_inputm_default_id[LEONOS_INPUTM_ID_LEN] = "en";
static char desktop_inputm_pending_id[LEONOS_INPUTM_ID_LEN];
static char desktop_inputm_hotkey[16] = "win-space";
static uint32_t desktop_inputm_config_generation;

static uint32_t inputm_text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static int inputm_state_changed(const struct leonos_inputm_state *left,
                                const struct leonos_inputm_state *right)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (uint32_t i = 0; i < sizeof(*left); ++i) {
        if (a[i] != b[i]) {
            return 1;
        }
    }
    return 0;
}

static int inputm_config_path(char *path, uint32_t capacity, uint32_t *out_uid)
{
    struct leonos_user_info user = {0};
    uint32_t len;
    if (!path || capacity == 0 || leonos_auth_current(&user) < 0 ||
        !user.uid || !user.home[0]) {
        return 0;
    }
    len = inputm_text_len(user.home);
    if (len + 1U + sizeof(DESKTOP_INPUTM_CONFIG_NAME) > capacity) {
        return 0;
    }
    copy_text(path, capacity, user.home);
    path[len++] = '/';
    copy_text(path + len, capacity - len, DESKTOP_INPUTM_CONFIG_NAME);
    if (out_uid) {
        *out_uid = user.uid;
    }
    return 1;
}

static int inputm_config_get(const char *text, const char *key,
                             char *value, uint32_t capacity)
{
    uint32_t key_len = inputm_text_len(key);
    uint32_t pos = 0;
    uint8_t found = 0;
    if (!text || !key || !key_len || !value || capacity == 0) {
        return 0;
    }
    value[0] = 0;
    while (text[pos]) {
        uint32_t start = pos;
        uint32_t end;
        uint32_t out = 0;
        while (text[pos] && text[pos] != '\n' && text[pos] != '\r') {
            ++pos;
        }
        end = pos;
        while (text[pos] == '\n' || text[pos] == '\r') {
            ++pos;
        }
        if (end <= start + key_len || text[start + key_len] != '=') {
            continue;
        }
        uint8_t matched = 1;
        for (uint32_t i = 0; i < key_len; ++i) {
            if (text[start + i] != key[i]) {
                matched = 0;
                break;
            }
        }
        if (!matched) {
            continue;
        }
        start += key_len + 1U;
        while (start < end && out + 1U < capacity) {
            value[out++] = text[start++];
        }
        value[out] = 0;
        found = 1;
    }
    return found;
}

static uint32_t inputm_parse_u32(const char *text, uint32_t fallback)
{
    uint32_t value = 0;
    uint32_t digits = 0;
    while (text && *text >= '0' && *text <= '9') {
        value = value * 10U + (uint32_t)(*text - '0');
        ++text;
        ++digits;
    }
    return digits ? value : fallback;
}

static void inputm_config_key(char *key, uint32_t capacity, uint32_t index,
                              const char *suffix)
{
    uint32_t pos = 0;
    key[0] = 0;
    append_text(key, &pos, capacity, "provider");
    append_dec(key, &pos, capacity, index);
    append_char(key, &pos, capacity, '_');
    append_text(key, &pos, capacity, suffix);
}

static void desktop_inputm_reset(void)
{
    desktop_inputm_entry_count = 1;
    desktop_inputm_entries[0] = (struct desktop_inputm_entry){0};
    copy_text(desktop_inputm_entries[0].id, sizeof(desktop_inputm_entries[0].id), "en");
    copy_text(desktop_inputm_entries[0].abbreviation,
              sizeof(desktop_inputm_entries[0].abbreviation), "EN");
    desktop_inputm_entries[0].enabled = 1;
    desktop_inputm_entries[0].running = 1;
    desktop_inputm_entries[0].startup_mode = LEONOS_INPUTM_START_MANUAL;
    desktop_inputm_entries[0].order = 0;
    desktop_inputm_state = (struct leonos_inputm_state){0};
    copy_text(desktop_inputm_state.active_id, sizeof(desktop_inputm_state.active_id), "en");
    copy_text(desktop_inputm_default_id, sizeof(desktop_inputm_default_id), "en");
    copy_text(desktop_inputm_hotkey, sizeof(desktop_inputm_hotkey), "win-space");
    desktop_inputm_pending_id[0] = 0;
    desktop_inputm_menu_open = 0;
    desktop_inputm_status[0] = 0;
}

static int desktop_inputm_entry_for_id(const char *id)
{
    for (uint32_t i = 0; i < desktop_inputm_entry_count; ++i) {
        if (text_eq(desktop_inputm_entries[i].id, id)) {
            return (int)i;
        }
    }
    return -1;
}

static int desktop_inputm_append_provider(const char *id)
{
    struct desktop_inputm_entry *entry;
    if (!id || !id[0] || desktop_inputm_entry_count >= LEONOS_INPUTM_MAX_PROVIDERS + 1U) {
        return -1;
    }
    entry = &desktop_inputm_entries[desktop_inputm_entry_count];
    *entry = (struct desktop_inputm_entry){0};
    copy_text(entry->id, sizeof(entry->id), id);
    copy_text(entry->abbreviation, sizeof(entry->abbreviation), "?");
    entry->enabled = 1;
    entry->startup_mode = LEONOS_INPUTM_START_ON_DEMAND;
    entry->order = desktop_inputm_entry_count;
    ++desktop_inputm_entry_count;
    return (int)(desktop_inputm_entry_count - 1U);
}

static void desktop_inputm_sort_entries(void)
{
    for (uint32_t i = 1; i < desktop_inputm_entry_count; ++i) {
        for (uint32_t j = i + 1U; j < desktop_inputm_entry_count; ++j) {
            if (desktop_inputm_entries[j].order < desktop_inputm_entries[i].order) {
                struct desktop_inputm_entry temp = desktop_inputm_entries[i];
                desktop_inputm_entries[i] = desktop_inputm_entries[j];
                desktop_inputm_entries[j] = temp;
            }
        }
    }
}

void desktop_inputm_load_config(void)
{
    char path[LEONOS_FS_PATH_LEN];
    char config[DESKTOP_INPUTM_CONFIG_MAX];
    char value[LEONOS_FS_PATH_LEN];
    int fd;
    long got;
    uint32_t uid;
    desktop_inputm_reset();
    if (!inputm_config_path(path, sizeof(path), &uid)) {
        return;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    got = read(fd, config, sizeof(config) - 1U);
    close(fd);
    if (got <= 0) {
        return;
    }
    config[got] = 0;
    if (inputm_config_get(config, "default", value, sizeof(value)) && value[0]) {
        copy_text(desktop_inputm_default_id, sizeof(desktop_inputm_default_id), value);
    }
    if (inputm_config_get(config, "inputm_hotkey", value, sizeof(value)) && value[0]) {
        copy_text(desktop_inputm_hotkey, sizeof(desktop_inputm_hotkey), value);
    }
    uint32_t count = 0;
    if (inputm_config_get(config, "provider_count", value, sizeof(value))) {
        count = inputm_parse_u32(value, 0);
    }
    if (count > LEONOS_INPUTM_MAX_PROVIDERS) {
        count = LEONOS_INPUTM_MAX_PROVIDERS;
    }
    for (uint32_t i = 0; i < count; ++i) {
        char key[48];
        int index;
        inputm_config_key(key, sizeof(key), i, "id");
        if (!inputm_config_get(config, key, value, sizeof(value)) || !value[0] ||
            text_eq(value, "en")) {
            continue;
        }
        index = desktop_inputm_append_provider(value);
        if (index < 0) {
            continue;
        }
        desktop_inputm_entries[index].order = i + 1U;
        inputm_config_key(key, sizeof(key), i, "path");
        if (inputm_config_get(config, key, value, sizeof(value))) {
            copy_text(desktop_inputm_entries[index].path,
                      sizeof(desktop_inputm_entries[index].path), value);
        }
        inputm_config_key(key, sizeof(key), i, "enabled");
        if (inputm_config_get(config, key, value, sizeof(value))) {
            desktop_inputm_entries[index].enabled = inputm_parse_u32(value, 1) ? 1 : 0;
        }
        inputm_config_key(key, sizeof(key), i, "startup");
        if (inputm_config_get(config, key, value, sizeof(value))) {
            uint32_t startup = inputm_parse_u32(value, LEONOS_INPUTM_START_ON_DEMAND);
            desktop_inputm_entries[index].startup_mode =
                startup <= LEONOS_INPUTM_START_ON_DEMAND ? startup : LEONOS_INPUTM_START_ON_DEMAND;
        }
        inputm_config_key(key, sizeof(key), i, "order");
        if (inputm_config_get(config, key, value, sizeof(value))) {
            uint32_t order = inputm_parse_u32(value, i + 1U);
            desktop_inputm_entries[index].order = order ? order : i + 1U;
        }
    }
    desktop_inputm_sort_entries();
    (void)uid;
}

int desktop_inputm_hotkey_is_alt_shift(void)
{
    return text_eq(desktop_inputm_hotkey, "alt-shift");
}

static void desktop_inputm_refresh_registered(uint32_t uid)
{
    struct leonos_inputm_provider providers[LEONOS_INPUTM_MAX_PROVIDERS + 1U];
    uint32_t count = 0;
    if (leonos_inputm_list(uid, providers, LEONOS_INPUTM_MAX_PROVIDERS + 1U, &count) < 0) {
        return;
    }
    for (uint32_t i = 0; i < desktop_inputm_entry_count; ++i) {
        desktop_inputm_entries[i].running = text_eq(desktop_inputm_entries[i].id, "en") ? 1 : 0;
    }
    for (uint32_t i = 0; i < count && i < LEONOS_INPUTM_MAX_PROVIDERS + 1U; ++i) {
        int index = desktop_inputm_entry_for_id(providers[i].id);
        uint8_t configured = index >= 0;
        if (index < 0) {
            index = desktop_inputm_append_provider(providers[i].id);
        }
        if (index < 0) {
            continue;
        }
        desktop_inputm_entries[index].running = 1;
        if (!configured) {
            desktop_inputm_entries[index].enabled = providers[i].enabled ? 1 : 0;
            desktop_inputm_entries[index].startup_mode = providers[i].startup_mode;
        }
        copy_text(desktop_inputm_entries[index].abbreviation,
                  sizeof(desktop_inputm_entries[index].abbreviation),
                  providers[i].abbreviation);
    }
}

static void desktop_inputm_start_entry(uint32_t index)
{
    if (index >= desktop_inputm_entry_count || !desktop_inputm_entries[index].path[0] ||
        desktop_inputm_entries[index].running) {
        return;
    }
    if (spawn_program_path(desktop_inputm_entries[index].path) > 0) {
        copy_text(desktop_inputm_status, sizeof(desktop_inputm_status),
                  leonos_i18n("Starting input method", "正在启动输入法"));
    } else {
        copy_text(desktop_inputm_status, sizeof(desktop_inputm_status),
                  leonos_i18n("Input method could not start", "输入法无法启动"));
    }
}

static void desktop_inputm_activate_index(uint32_t index)
{
    char config_path[LEONOS_FS_PATH_LEN];
    uint32_t uid;
    if (index >= desktop_inputm_entry_count || !desktop_inputm_entries[index].enabled ||
        !inputm_config_path(config_path, sizeof(config_path), &uid)) {
        return;
    }
    if (!desktop_inputm_entries[index].running && !text_eq(desktop_inputm_entries[index].id, "en")) {
        copy_text(desktop_inputm_pending_id, sizeof(desktop_inputm_pending_id),
                  desktop_inputm_entries[index].id);
        desktop_inputm_start_entry(index);
        return;
    }
    if (leonos_inputm_set_active(uid, desktop_inputm_entries[index].id) > 0) {
        copy_text(desktop_inputm_status, sizeof(desktop_inputm_status),
                  desktop_inputm_entries[index].id);
    }
}

void desktop_inputm_refresh(void)
{
    char path[LEONOS_FS_PATH_LEN];
    uint32_t uid;
    struct leonos_inputm_state previous = desktop_inputm_state;
    if (!inputm_config_path(path, sizeof(path), &uid)) {
        return;
    }
    desktop_inputm_refresh_registered(uid);
    if (desktop_inputm_pending_id[0]) {
        int pending = desktop_inputm_entry_for_id(desktop_inputm_pending_id);
        if (pending >= 0 && desktop_inputm_entries[pending].running) {
            desktop_inputm_activate_index((uint32_t)pending);
            desktop_inputm_pending_id[0] = 0;
        }
    } else if (!text_eq(desktop_inputm_default_id, "en")) {
        int default_index = desktop_inputm_entry_for_id(desktop_inputm_default_id);
        if (default_index >= 0 && desktop_inputm_entries[default_index].enabled) {
            if (desktop_inputm_entries[default_index].running) {
                (void)leonos_inputm_set_active(uid, desktop_inputm_default_id);
                copy_text(desktop_inputm_default_id, sizeof(desktop_inputm_default_id), "en");
            } else {
                copy_text(desktop_inputm_pending_id, sizeof(desktop_inputm_pending_id),
                          desktop_inputm_default_id);
                desktop_inputm_start_entry((uint32_t)default_index);
            }
        }
    }
    if (leonos_inputm_get_state(uid, &desktop_inputm_state) > 0) {
        if (desktop_inputm_state.config_generation != desktop_inputm_config_generation) {
            struct leonos_inputm_state observed = desktop_inputm_state;
            desktop_inputm_config_generation = observed.config_generation;
            desktop_inputm_load_config();
            desktop_inputm_state = observed;
            desktop_inputm_refresh_registered(uid);
        }
        if (!text_eq(previous.active_id, "en") &&
            text_eq(desktop_inputm_state.active_id, "en")) {
            copy_text(desktop_inputm_status, sizeof(desktop_inputm_status),
                      leonos_i18n("Input method stopped; English restored", "输入法已停止，已恢复英文"));
        }
        if (inputm_state_changed(&previous, &desktop_inputm_state)) {
            full_redraw_pending = 1;
        }
    }
}

void desktop_inputm_launch_login_providers(void)
{
    /* Login-start providers are launched exclusively by the approved startup list. */
}

void desktop_inputm_cycle(void)
{
    int current = desktop_inputm_entry_for_id(desktop_inputm_state.active_id);
    if (desktop_inputm_entry_count < 2U) {
        copy_text(desktop_inputm_status, sizeof(desktop_inputm_status),
                  leonos_i18n("Only English input is available", "当前仅有英文输入法可用"));
        full_redraw_pending = 1;
        return;
    }
    for (uint32_t step = 1; step <= desktop_inputm_entry_count; ++step) {
        uint32_t index = (uint32_t)((current < 0 ? 0 : current) + (int)step) %
                         desktop_inputm_entry_count;
        if (desktop_inputm_entries[index].enabled) {
            desktop_inputm_activate_index(index);
            desktop_inputm_menu_open = 0;
            full_redraw_pending = 1;
            return;
        }
    }
}

int desktop_inputm_handle_click(uint32_t x, uint32_t y)
{
    uint32_t tb_y = taskbar_y();
    uint32_t tray_w = desktop_tray_width();
    uint32_t icon_x = fb_w() > tray_w ? fb_w() - tray_w : 0;
    uint32_t rows = desktop_inputm_entry_count + 1U;
    uint32_t menu_h = 8U + rows * DESKTOP_INPUTM_MENU_ROW_H + 8U;
    uint32_t menu_x = fb_w() > DESKTOP_INPUTM_MENU_W + 4U
                          ? fb_w() - DESKTOP_INPUTM_MENU_W - 4U : 0U;
    uint32_t menu_y = tb_y > menu_h + 4U ? tb_y - menu_h - 4U : 0U;
    if (hit_rect(x, y, (int)icon_x, (int)tb_y + 5, TASKBAR_INPUTM_W - 4U,
                 LEONOS_UI_BUTTON_H)) {
        desktop_inputm_menu_open = desktop_inputm_menu_open ? 0 : 1;
        full_redraw_pending = 1;
        return 1;
    }
    if (!desktop_inputm_menu_open) {
        return 0;
    }
    if (hit_rect(x, y, (int)menu_x, (int)menu_y, DESKTOP_INPUTM_MENU_W, menu_h)) {
        if (y >= menu_y + 8U && y < menu_y + 8U +
            desktop_inputm_entry_count * DESKTOP_INPUTM_MENU_ROW_H) {
            uint32_t index = (y - menu_y - 8U) / DESKTOP_INPUTM_MENU_ROW_H;
            if (index < desktop_inputm_entry_count) {
                desktop_inputm_activate_index(index);
            }
        } else if (y >= menu_y + 8U +
                   desktop_inputm_entry_count * DESKTOP_INPUTM_MENU_ROW_H) {
            spawn_program_path("0:/system/apps/settings/settings.elf");
        }
        desktop_inputm_menu_open = 0;
        full_redraw_pending = 1;
        return 1;
    }
    desktop_inputm_menu_open = 0;
    full_redraw_pending = 1;
    return 1;
}

void draw_inputm_overlay(void)
{
    uint32_t rows;
    uint32_t menu_h;
    uint32_t menu_x;
    uint32_t menu_y;
    if (desktop_inputm_menu_open) {
        rows = desktop_inputm_entry_count + 1U;
        menu_h = 8U + rows * DESKTOP_INPUTM_MENU_ROW_H + 8U;
        menu_x = fb_w() > DESKTOP_INPUTM_MENU_W + 4U
                     ? fb_w() - DESKTOP_INPUTM_MENU_W - 4U : 0U;
        menu_y = taskbar_y() > menu_h + 4U ? taskbar_y() - menu_h - 4U : 0U;
        leonos_ui_panel(&ui, menu_x, menu_y, DESKTOP_INPUTM_MENU_W, menu_h,
                        LEONOS_UI_LIGHT);
        for (uint32_t i = 0; i < desktop_inputm_entry_count; ++i) {
            char line[96];
            uint32_t pos = 0;
            line[0] = 0;
            append_text(line, &pos, sizeof(line), desktop_inputm_entries[i].abbreviation);
            append_text(line, &pos, sizeof(line), "  ");
            append_text(line, &pos, sizeof(line), desktop_inputm_entries[i].id);
            if (!desktop_inputm_entries[i].running &&
                !text_eq(desktop_inputm_entries[i].id, "en")) {
                append_text(line, &pos, sizeof(line), "  (start)");
            }
            leonos_ui_menu_item(&ui, menu_x + 6U,
                                menu_y + 8U + i * DESKTOP_INPUTM_MENU_ROW_H,
                                DESKTOP_INPUTM_MENU_W - 12U, line,
                                text_eq(desktop_inputm_entries[i].id,
                                        desktop_inputm_state.active_id)
                                    ? LEONOS_UI_MENU_SELECTED
                                    : (!desktop_inputm_entries[i].enabled
                                           ? LEONOS_UI_MENU_DISABLED : 0));
        }
        leonos_ui_menu_item(&ui, menu_x + 6U,
                            menu_y + 8U + desktop_inputm_entry_count *
                                DESKTOP_INPUTM_MENU_ROW_H,
                            DESKTOP_INPUTM_MENU_W - 12U,
                            leonos_i18n("Input method settings", "输入法设置"), 0);
    }
    if (desktop_inputm_state.composition[0] && !desktop_inputm_menu_open &&
        !(desktop_inputm_state.render_flags & LEONOS_INPUTM_RENDER_PIXELS)) {
        uint32_t width = 320U;
        uint32_t height = 34U + desktop_inputm_state.candidate_count *
                          (LEONOS_FONT_H + 4U);
        uint32_t x = cursor_x + 16U;
        uint32_t y = cursor_y + 18U;
        for (uint32_t i = 0; i < MAX_WINDOWS; ++i) {
            if (windows[i].visible && windows[i].window_id == desktop_inputm_state.window_id) {
                x = (uint32_t)(windows[i].x > 0 ? windows[i].x : 0) +
                    (desktop_inputm_state.caret_x > 0 ?
                         (uint32_t)desktop_inputm_state.caret_x : 0U);
                y = (uint32_t)(windows[i].y > 0 ? windows[i].y : 0) + TITLEBAR_H +
                    (desktop_inputm_state.caret_y > 0 ?
                         (uint32_t)desktop_inputm_state.caret_y : 0U) +
                    desktop_inputm_state.caret_h;
                break;
            }
        }
        if (width > fb_w()) {
            width = fb_w();
        }
        if (x + width > fb_w()) {
            x = fb_w() > width ? fb_w() - width : 0;
        }
        if (y + height > taskbar_y()) {
            y = taskbar_y() > height ? taskbar_y() - height : 0;
        }
        leonos_ui_panel(&ui, x, y, width, height, LEONOS_UI_LIGHT);
        leonos_ui_text_clipped(&ui, x + 8U, y + 7U, width > 16U ? width - 16U : width,
                               desktop_inputm_state.composition,
                               LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
        for (uint32_t i = 0; i < desktop_inputm_state.candidate_count &&
                             i < LEONOS_INPUTM_MAX_CANDIDATES; ++i) {
            char line[LEONOS_INPUTM_TEXT_LEN + 8U];
            uint32_t pos = 0;
            line[0] = 0;
            append_dec(line, &pos, sizeof(line), i + 1U);
            append_text(line, &pos, sizeof(line), ". ");
            append_text(line, &pos, sizeof(line), desktop_inputm_state.candidates[i]);
            leonos_ui_menu_item(&ui, x + 6U, y + 28U + i * (LEONOS_FONT_H + 4U),
                                width > 12U ? width - 12U : width, line,
                                i == desktop_inputm_state.selected_candidate
                                    ? LEONOS_UI_MENU_SELECTED : 0);
        }
    }
}
