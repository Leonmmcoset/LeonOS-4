#include <leonos/fs.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define UI_THEME_CONFIG_PATH "0:/etc/display.conf"

static uint32_t ui_current_theme = LEONOS_UI_THEME_METRO;

static int ui_theme_line_is_win95(const char *text, uint32_t len)
{
    static const char key[] = "theme=win95";
    uint32_t key_len = sizeof(key) - 1u;
    for (uint32_t index = 0; index + key_len <= len; ++index) {
        uint32_t matched = 0;
        while (matched < key_len && text[index + matched] == key[matched]) {
            ++matched;
        }
        if (matched == key_len &&
            (index == 0 || text[index - 1] == '\n' || text[index - 1] == '\r') &&
            (index + key_len == len || text[index + key_len] == '\n' ||
             text[index + key_len] == '\r')) {
            return 1;
        }
    }
    return 0;
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

void leonos_ui_theme_load_system(void)
{
    char config[160];
    int fd = open(UI_THEME_CONFIG_PATH, LEONOS_O_RDONLY, 0);
    long got;
    ui_current_theme = LEONOS_UI_THEME_METRO;
    if (fd < 0) {
        return;
    }
    got = read(fd, config, sizeof(config));
    close(fd);
    if (got > 0 && ui_theme_line_is_win95(config, (uint32_t)got)) {
        ui_current_theme = LEONOS_UI_THEME_WIN95;
    }
}

int ui_theme_is_metro(void)
{
    return ui_current_theme == LEONOS_UI_THEME_METRO;
}

uint32_t leonos_ui_color(uint32_t role)
{
    if (ui_current_theme == LEONOS_UI_THEME_WIN95) {
        static const uint32_t win95[] = {
            0x00000000u, 0x00ffffffu, 0x00c0c0c0u, 0x00dfdfdfu,
            0x00808080u, 0x00000080u, 0x00808080u, 0x00008080u,
            0x00000000u, 0x00000080u,
        };
        return role < sizeof(win95) / sizeof(win95[0]) ? win95[role] : win95[0];
    }
    static const uint32_t metro[] = {
        0x00202020u, 0x00ffffffu, 0x00f3f3f3u, 0x00e5e5e5u,
        0x006b6b6bu, 0x000078d4u, 0x00707070u, 0x000078d4u,
        0x00d0d0d0u, 0x000078d4u,
    };
    return role < sizeof(metro) / sizeof(metro[0]) ? metro[role] : metro[0];
}
