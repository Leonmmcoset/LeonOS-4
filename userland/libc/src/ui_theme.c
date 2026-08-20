#include <leonos/fs.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define UI_THEME_CONFIG_PATH "0:/system/config/display.conf"

static uint32_t ui_current_theme = LEONOS_UI_THEME_METRO;
static uint32_t ui_metro_color_scheme = LEONOS_UI_COLOR_SCHEME_BLUE;
static uint32_t ui_win95_color_scheme = LEONOS_UI_COLOR_SCHEME_BLUE;
static uint8_t ui_theme_system_loaded;

struct ui_theme_palette {
    uint32_t accent;
    uint32_t inactive_title;
    uint32_t desktop;
    uint32_t border;
    uint32_t selection;
};

static const struct ui_theme_palette metro_palettes[LEONOS_UI_COLOR_SCHEME_COUNT] = {
    {0x000078d4u, 0x00707070u, 0x000078d4u, 0x00d0d0d0u, 0x00e7f0ffu},
    {0x00009988u, 0x006d807eu, 0x00006666u, 0x00c7dad7u, 0x00dff5f2u},
    {0x00108040u, 0x006d7f72u, 0x00107038u, 0x00c8dacbu, 0x00e2f4e8u},
    {0x006b3fa0u, 0x00756d82u, 0x00503380u, 0x00d2c8dfu, 0x00eee7f8u},
    {0x00b03030u, 0x00806d6du, 0x00802020u, 0x00dfc8c8u, 0x00f8e4e4u},
    {0x00505050u, 0x00707070u, 0x003a3a3au, 0x00c8c8c8u, 0x00e6e6e6u},
    /* Kawaii pink: bubblegum title, soft candy desktop, gentle selection. */
    {0x00ff6faeu, 0x00c98da9u, 0x00f7b6d2u, 0x00ffd7e7u, 0x00ffeaf3u},
};

static const struct ui_theme_palette win95_palettes[LEONOS_UI_COLOR_SCHEME_COUNT] = {
    {0x00000080u, 0x00808080u, 0x00008080u, 0x00000000u, 0x00d8d8ffu},
    {0x00008080u, 0x00808080u, 0x00006060u, 0x00000000u, 0x00d8f0f0u},
    {0x00008000u, 0x00808080u, 0x00006020u, 0x00000000u, 0x00d8efd8u},
    {0x00800080u, 0x00808080u, 0x00602060u, 0x00000000u, 0x00efd8efu},
    {0x00800000u, 0x00808080u, 0x00602020u, 0x00000000u, 0x00efd8d8u},
    {0x00404040u, 0x00808080u, 0x00606060u, 0x00000000u, 0x00d8d8d8u},
    /* Kawaii pink with a Win95-compatible darker title and pastel desktop. */
    {0x00c85a91u, 0x00a77b91u, 0x00e9a4c5u, 0x00000000u, 0x00ffe0eeu},
};

static int ui_config_key_value_eq(const char *text, uint32_t len,
                                  const char *key, const char *value)
{
    uint32_t key_len = 0;
    uint32_t value_len = 0;
    while (key && key[key_len]) {
        ++key_len;
    }
    while (value && value[value_len]) {
        ++value_len;
    }
    if (!key_len || !value_len) {
        return 0;
    }
    for (uint32_t index = 0; index + key_len + 1u + value_len <= len; ++index) {
        uint32_t matched = 0;
        if (index != 0 && text[index - 1] != '\n' && text[index - 1] != '\r') {
            continue;
        }
        while (matched < key_len && text[index + matched] == key[matched]) {
            ++matched;
        }
        if (matched != key_len || text[index + matched] != '=') {
            continue;
        }
        ++matched;
        uint32_t value_index = 0;
        while (value_index < value_len &&
               text[index + matched + value_index] == value[value_index]) {
            ++value_index;
        }
        if (value_index == value_len &&
            (index + matched + value_index == len ||
             text[index + matched + value_index] == '\n' ||
             text[index + matched + value_index] == '\r')) {
            return 1;
        }
    }
    return 0;
}

static uint32_t ui_color_scheme_from_config(const char *text, uint32_t len,
                                            const char *key, uint32_t fallback)
{
    static const char *names[LEONOS_UI_COLOR_SCHEME_COUNT] = {
        "blue", "teal", "green", "purple", "red", "graphite", "pink",
    };
    for (uint32_t i = 0; i < LEONOS_UI_COLOR_SCHEME_COUNT; ++i) {
        if (ui_config_key_value_eq(text, len, key, names[i])) {
            return i;
        }
    }
    return fallback < LEONOS_UI_COLOR_SCHEME_COUNT
               ? fallback
               : LEONOS_UI_COLOR_SCHEME_BLUE;
}

static uint32_t ui_theme_normalize_scheme(uint32_t scheme)
{
    return scheme < LEONOS_UI_COLOR_SCHEME_COUNT
               ? scheme
               : LEONOS_UI_COLOR_SCHEME_BLUE;
}

uint32_t leonos_ui_theme(void)
{
    return ui_current_theme;
}

int leonos_ui_theme_set(uint32_t theme)
{
    if (theme != LEONOS_UI_THEME_WIN95 && theme != LEONOS_UI_THEME_METRO) {
        return -1;
    }
    ui_current_theme = theme;
    return 0;
}

uint32_t leonos_ui_theme_color_scheme(uint32_t theme)
{
    if (theme == LEONOS_UI_THEME_WIN95) {
        return ui_win95_color_scheme;
    }
    return ui_metro_color_scheme;
}

uint32_t leonos_ui_theme_active_color_scheme(void)
{
    return leonos_ui_theme_color_scheme(ui_current_theme);
}

uint32_t leonos_ui_theme_scheme_accent(uint32_t theme, uint32_t scheme)
{
    scheme = ui_theme_normalize_scheme(scheme);
    if (theme == LEONOS_UI_THEME_WIN95) {
        return win95_palettes[scheme].accent;
    }
    return metro_palettes[scheme].accent;
}

int leonos_ui_theme_set_color_scheme(uint32_t theme, uint32_t scheme)
{
    if (scheme >= LEONOS_UI_COLOR_SCHEME_COUNT) {
        return -1;
    }
    if (theme == LEONOS_UI_THEME_WIN95) {
        ui_win95_color_scheme = scheme;
        return 0;
    }
    if (theme == LEONOS_UI_THEME_METRO) {
        ui_metro_color_scheme = scheme;
        return 0;
    }
    return -1;
}

int leonos_ui_theme_set_appearance(uint32_t theme,
                                   uint32_t metro_color_scheme,
                                   uint32_t win95_color_scheme)
{
    if (theme != LEONOS_UI_THEME_WIN95 && theme != LEONOS_UI_THEME_METRO) {
        return -1;
    }
    if (metro_color_scheme >= LEONOS_UI_COLOR_SCHEME_COUNT ||
        win95_color_scheme >= LEONOS_UI_COLOR_SCHEME_COUNT) {
        return -1;
    }
    ui_current_theme = theme;
    ui_metro_color_scheme = metro_color_scheme;
    ui_win95_color_scheme = win95_color_scheme;
    return 0;
}

void leonos_ui_theme_load_system(void)
{
    char config[160];
    int fd = open(UI_THEME_CONFIG_PATH, LEONOS_O_RDONLY, 0);
    long got;
    ui_theme_system_loaded = 1;
    ui_current_theme = LEONOS_UI_THEME_METRO;
    ui_metro_color_scheme = LEONOS_UI_COLOR_SCHEME_BLUE;
    ui_win95_color_scheme = LEONOS_UI_COLOR_SCHEME_BLUE;
    if (fd < 0) {
        return;
    }
    got = read(fd, config, sizeof(config));
    close(fd);
    if (got > 0) {
        uint32_t len = (uint32_t)got;
        if (ui_config_key_value_eq(config, len, "theme", "win95")) {
            ui_current_theme = LEONOS_UI_THEME_WIN95;
        }
        ui_metro_color_scheme =
            ui_color_scheme_from_config(config, len, "metro.color",
                                        ui_metro_color_scheme);
        ui_win95_color_scheme =
            ui_color_scheme_from_config(config, len, "win95.color",
                                        ui_win95_color_scheme);
    }
}

void ui_theme_ensure_loaded(void)
{
    if (!ui_theme_system_loaded) {
        leonos_ui_theme_load_system();
    }
}

int ui_theme_is_metro(void)
{
    return ui_current_theme == LEONOS_UI_THEME_METRO;
}

uint32_t leonos_ui_color(uint32_t role)
{
    if (ui_current_theme == LEONOS_UI_THEME_WIN95) {
        const struct ui_theme_palette *palette =
            &win95_palettes[ui_theme_normalize_scheme(ui_win95_color_scheme)];
        static const uint32_t win95[] = {
            0x00000000u, 0x00ffffffu, 0x00c0c0c0u, 0x00dfdfdfu,
            0x00808080u, 0x00000080u, 0x00808080u, 0x00008080u,
            0x00000000u, 0x00000080u,
        };
        switch (role) {
        case LEONOS_UI_COLOR_ACCENT:
            return palette->accent;
        case LEONOS_UI_COLOR_TITLE_INACTIVE:
            return palette->inactive_title;
        case LEONOS_UI_COLOR_DESKTOP:
            return palette->desktop;
        case LEONOS_UI_COLOR_BORDER:
            return palette->border;
        case LEONOS_UI_COLOR_SELECTION:
            return palette->selection;
        default:
            return role < sizeof(win95) / sizeof(win95[0]) ? win95[role] : win95[0];
        }
    }
    const struct ui_theme_palette *palette =
        &metro_palettes[ui_theme_normalize_scheme(ui_metro_color_scheme)];
    static const uint32_t metro[] = {
        0x00202020u, 0x00ffffffu, 0x00f3f3f3u, 0x00e5e5e5u,
        0x006b6b6bu, 0x000078d4u, 0x00707070u, 0x000078d4u,
        0x00d0d0d0u, 0x000078d4u,
    };
    switch (role) {
    case LEONOS_UI_COLOR_ACCENT:
        return palette->accent;
    case LEONOS_UI_COLOR_TITLE_INACTIVE:
        return palette->inactive_title;
    case LEONOS_UI_COLOR_DESKTOP:
        return palette->desktop;
    case LEONOS_UI_COLOR_BORDER:
        return palette->border;
    case LEONOS_UI_COLOR_SELECTION:
        return palette->selection;
    default:
        return role < sizeof(metro) / sizeof(metro[0]) ? metro[role] : metro[0];
    }
}
