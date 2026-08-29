#include <leonos/boot_handoff.h>
#include <leonos/psf_font.h>
#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <ntclks/time.h>

#define CONSOLE_LOG_CAP 8192
#define CONSOLE_MAX_COLS 512u
#define CONSOLE_MAX_ROWS 192u

static char log_buffer[CONSOLE_LOG_CAP];
static size_t log_len;
static bool fb_console_enabled;
static bool vga_console_enabled;
static uint32_t fb_x;
static uint32_t fb_y;
static uint32_t fb_cols;
static uint32_t fb_rows;
static uint32_t fb_col;
static uint32_t fb_row;
static uint32_t console_ui_theme = 1u;
static bool console_line_start = true;
static bool console_runtime_quiet;
static uint64_t console_boot_uptime_us;
static bool console_presenting;
static uint32_t fb_saved_col;
static uint32_t fb_saved_row;
static uint32_t fb_console_fg_color;
static uint32_t fb_console_bg_color;
static bool fb_console_bold;
static bool fb_console_underline;
static bool fb_console_reverse;
static uint32_t fb_scroll_top;
static uint32_t fb_scroll_bottom;

static bool console_is_ntclks_format(const char *fmt)
{
    static const char prefix[] = "[ntclks]";
    if (!fmt) return false;
    for (uint32_t i = 0; i < sizeof(prefix) - 1u; ++i) {
        if (!fmt[i] || fmt[i] != prefix[i]) return false;
    }
    return true;
}

enum fb_ansi_state {
    FB_ANSI_NORMAL = 0,
    FB_ANSI_ESC,
    FB_ANSI_CSI,
    FB_ANSI_OSC,
    FB_ANSI_OSC_ESC,
    FB_ANSI_CHARSET,
};

static uint8_t fb_ansi_state;
static uint32_t fb_ansi_params[8];
static uint32_t fb_ansi_param_count;
static uint32_t fb_ansi_param_value;
static bool fb_ansi_param_active;
static bool fb_ansi_private;

static uint32_t console_panel(void)
{
    return console_ui_theme == 0u ? 0x00000000u : 0x001b2a3au;
}

static uint32_t console_fg(void)
{
    return console_ui_theme == 0u ? 0x0000ff00u : 0x00ffffffu;
}

static void fb_console_reset_attributes(void)
{
    fb_console_bold = false;
    fb_console_underline = false;
    fb_console_reverse = false;
    fb_console_fg_color = console_fg();
    fb_console_bg_color = console_panel();
}

static void fb_console_reset_ansi(void)
{
    fb_ansi_state = FB_ANSI_NORMAL;
    fb_ansi_param_count = 0;
    fb_ansi_param_value = 0;
    fb_ansi_param_active = false;
    fb_ansi_private = false;
}

static void log_store(char ch)
{
    if (log_len < CONSOLE_LOG_CAP) {
        log_buffer[log_len++] = ch;
    } else {
        for (size_t i = 1; i < CONSOLE_LOG_CAP; ++i) {
            log_buffer[i - 1] = log_buffer[i];
        }
        log_buffer[CONSOLE_LOG_CAP - 1] = ch;
    }
}

static void fb_console_clear_log_line(uint32_t row)
{
    const struct framebuffer *fb = framebuffer_get();
    if (!fb->available || !fb_cols || row >= fb_rows) {
        return;
    }
    framebuffer_rect(fb_x, fb_y + row * LEONOS_FONT_H,
                     fb_cols * LEONOS_FONT_W, LEONOS_FONT_H, fb_console_bg_color);
}

static void fb_console_clear_log(void)
{
    const struct framebuffer *fb = framebuffer_get();
    if (!fb->available || !fb_cols || !fb_rows) {
        return;
    }
    framebuffer_rect(fb_x, fb_y, fb_cols * LEONOS_FONT_W,
                     fb_rows * LEONOS_FONT_H, fb_console_bg_color);
}

static void fb_console_scroll(void)
{
    const struct framebuffer *fb = framebuffer_get();
    uint32_t top = fb_scroll_top < fb_rows ? fb_scroll_top : 0u;
    uint32_t bottom = fb_scroll_bottom < fb_rows ? fb_scroll_bottom : fb_rows - 1u;
    if (bottom <= top) {
        top = 0u;
        bottom = fb_rows - 1u;
    }
    if (!fb->available || bottom <= top || fb_x >= fb->width || fb_y >= fb->height) {
        fb_console_clear_log();
        fb_row = top;
        return;
    }

    uint32_t width = fb_cols * LEONOS_FONT_W;
    if (width > fb->width - fb_x) {
        width = fb->width - fb_x;
    }

    uint32_t src_y = fb_y + (top + 1u) * LEONOS_FONT_H;
    if (src_y >= fb->height) {
        fb_console_clear_log();
        fb_row = 0;
        return;
    }

    uint32_t copy_h = (bottom - top) * LEONOS_FONT_H;
    if (copy_h > fb->height - src_y) {
        copy_h = fb->height - src_y;
    }

    for (uint32_t y = 0; y < copy_h; ++y) {
        uint8_t *dst = (uint8_t *)fb->pixels + (uint64_t)(fb_y + top * LEONOS_FONT_H + y) * fb->pitch +
                       (uint64_t)fb_x * fb->bytes_per_pixel;
        const uint8_t *src = (const uint8_t *)fb->pixels +
                             (uint64_t)(src_y + y) * fb->pitch +
                             (uint64_t)fb_x * fb->bytes_per_pixel;
        for (uint32_t byte = 0; byte < width * fb->bytes_per_pixel; ++byte) {
            dst[byte] = src[byte];
        }
    }

    fb_row = bottom;
    fb_console_clear_log_line(fb_row);
}

static void fb_console_newline(void)
{
    fb_col = 0;
    uint32_t bottom = fb_scroll_bottom < fb_rows ? fb_scroll_bottom : fb_rows - 1u;
    if (fb_row < bottom) {
        ++fb_row;
    } else {
        fb_console_scroll();
    }
}

static void fb_console_putc(char ch)
{
    if (!fb_console_enabled || !framebuffer_get()->available || !fb_cols || !fb_rows) {
        return;
    }
    if (ch == '\r') {
        fb_col = 0;
        return;
    }
    if (ch == '\n') {
        fb_console_newline();
        return;
    }
    if (ch == '\b') {
        if (fb_col) {
            --fb_col;
            framebuffer_rect(fb_x + fb_col * LEONOS_FONT_W,
                             fb_y + fb_row * LEONOS_FONT_H,
                             LEONOS_FONT_W, LEONOS_FONT_H, fb_console_bg_color);
        }
        return;
    }
    if (ch == '\t') {
        uint32_t next = (fb_col + 8u) & ~7u;
        while (fb_col < next) {
            fb_console_putc(' ');
        }
        return;
    }
    if ((unsigned char)ch < 0x20u) {
        return;
    }
    if ((unsigned char)ch == 0x7fu) {
        return;
    }
    if (fb_col >= fb_cols) {
        fb_console_newline();
    }
    {
        uint32_t fg = fb_console_reverse ? fb_console_bg_color : fb_console_fg_color;
        uint32_t bg = fb_console_reverse ? fb_console_fg_color : fb_console_bg_color;
        uint32_t x = fb_x + fb_col * LEONOS_FONT_W;
        uint32_t y = fb_y + fb_row * LEONOS_FONT_H;
        framebuffer_rect(x, y, LEONOS_FONT_W, LEONOS_FONT_H, bg);
        framebuffer_text(x, y, (char[]){ch, 0}, fg, bg);
        if (fb_console_bold && x + 1u < framebuffer_get()->width) {
            framebuffer_text(x + 1u, y, (char[]){ch, 0}, fg, bg);
        }
        if (fb_console_underline) {
            framebuffer_rect(x, y + LEONOS_FONT_H - 2u, LEONOS_FONT_W, 1u, fg);
        }
    }
    ++fb_col;
}

static uint32_t fb_ansi_param(uint32_t index, uint32_t fallback)
{
    if (index >= fb_ansi_param_count) {
        return fallback;
    }
    return fb_ansi_params[index];
}

static void fb_console_clear_row_range(uint32_t row, uint32_t first, uint32_t last)
{
    const struct framebuffer *fb = framebuffer_get();
    if (!fb->available || row >= fb_rows || first >= fb_cols) {
        return;
    }
    if (last > fb_cols) {
        last = fb_cols;
    }
    if (last <= first) {
        return;
    }
    framebuffer_rect(fb_x + first * LEONOS_FONT_W,
                     fb_y + row * LEONOS_FONT_H,
                     (last - first) * LEONOS_FONT_W, LEONOS_FONT_H,
                     fb_console_bg_color);
}

static uint32_t fb_ansi_color(uint32_t code)
{
    static const uint32_t colors[8] = {
        0x00000000u, 0x00aa0000u, 0x0000aa00u, 0x00aa5500u,
        0x000000aau, 0x00aa00aau, 0x0000aaaaau, 0x00aaaaaau,
    };
    static const uint32_t bright[8] = {
        0x00555555u, 0x00ff5555u, 0x0055ff55u, 0x00ffff55u,
        0x005555ffu, 0x00ff55ffu, 0x0055ffffu, 0x00ffffffu,
    };
    if (code >= 90u && code <= 97u) {
        return bright[code - 90u];
    }
    if (code >= 30u && code <= 37u) {
        return colors[code - 30u];
    }
    return console_fg();
}

static uint32_t fb_ansi_palette(uint32_t index)
{
    static const uint32_t base[16] = {
        0x00000000u, 0x00aa0000u, 0x0000aa00u, 0x00aa5500u,
        0x000000aau, 0x00aa00aau, 0x0000aaaaau, 0x00aaaaaau,
        0x00555555u, 0x00ff5555u, 0x0055ff55u, 0x00ffff55u,
        0x005555ffu, 0x00ff55ffu, 0x0055ffffu, 0x00ffffffu,
    };
    if (index < 16u) return base[index];
    if (index >= 232u) {
        uint32_t level = 8u + (index - 232u) * 10u;
        return (level << 16) | (level << 8) | level;
    }
    index -= 16u;
    {
        uint32_t r = index / 36u;
        uint32_t g = (index / 6u) % 6u;
        uint32_t b = index % 6u;
        r = r ? 55u + r * 40u : 0u;
        g = g ? 55u + g * 40u : 0u;
        b = b ? 55u + b * 40u : 0u;
        return (r << 16) | (g << 8) | b;
    }
}

static void fb_console_execute_csi(char final)
{
    uint32_t n = fb_ansi_param_count;
    uint32_t p0 = fb_ansi_param(0, (final == 'J' || final == 'K') ? 0u : 1u);
    uint32_t p1 = fb_ansi_param(1, 1);
    uint32_t movement = p0 ? p0 : 1u;
    if (p0 == 0u && (final == 'G' || final == 'H' || final == 'f')) {
        p0 = 1u;
    }
    if (p1 == 0u && (final == 'H' || final == 'f')) {
        p1 = 1u;
    }

    switch (final) {
    case 'A':
        fb_row = fb_row >= movement ? fb_row - movement : 0;
        break;
    case 'B':
        fb_row = fb_row + movement < fb_rows ? fb_row + movement : fb_rows - 1u;
        break;
    case 'C':
        fb_col = fb_col + movement < fb_cols ? fb_col + movement : fb_cols - 1u;
        break;
    case 'D':
        fb_col = fb_col >= movement ? fb_col - movement : 0;
        break;
    case 'G':
        fb_col = p0 > fb_cols ? fb_cols - 1u : p0 - 1u;
        break;
    case 'H':
    case 'f':
        fb_row = p0 > fb_rows ? fb_rows - 1u : p0 - 1u;
        fb_col = p1 > fb_cols ? fb_cols - 1u : p1 - 1u;
        break;
    case 'J':
        if (p0 == 2u || p0 == 3u) {
            fb_console_clear_log();
        } else if (p0 == 1u) {
            for (uint32_t row = 0; row < fb_row; ++row) {
                fb_console_clear_row_range(row, 0, fb_cols);
            }
            fb_console_clear_row_range(fb_row, 0, fb_col + 1u);
        } else {
            fb_console_clear_row_range(fb_row, fb_col, fb_cols);
            for (uint32_t row = fb_row + 1u; row < fb_rows; ++row) {
                fb_console_clear_row_range(row, 0, fb_cols);
            }
        }
        break;
    case 'K':
        if (p0 == 2u) {
            fb_console_clear_row_range(fb_row, 0, fb_cols);
        } else if (p0 == 1u) {
            fb_console_clear_row_range(fb_row, 0, fb_col + 1u);
        } else {
            fb_console_clear_row_range(fb_row, fb_col, fb_cols);
        }
        break;
    case 'E':
        fb_row += movement;
        if (fb_row >= fb_rows) fb_row = fb_rows - 1u;
        fb_col = 0;
        break;
    case 'F':
        fb_row = fb_row >= movement ? fb_row - movement : 0u;
        fb_col = 0;
        break;
    case 'd':
        fb_row = p0 > fb_rows ? fb_rows - 1u : p0 - 1u;
        break;
    case 'e':
        fb_row += movement;
        if (fb_row >= fb_rows) fb_row = fb_rows - 1u;
        break;
    case 'h':
    case 'l':
        /* Accept cursor visibility, bracketed paste and other mode toggles. */
        break;
    case 'r':
        if (n >= 2u && p0 >= 1u && p1 >= p0 && p1 <= fb_rows) {
            fb_scroll_top = p0 - 1u;
            fb_scroll_bottom = p1 - 1u;
            fb_row = fb_scroll_top;
            fb_col = 0;
        }
        break;
    case 'S':
        for (uint32_t i = 0; i < movement; ++i) fb_console_scroll();
        break;
    case 'X':
    case '@':
    case 'P':
        fb_console_clear_row_range(fb_row, fb_col,
                                   fb_col + movement > fb_cols ? fb_cols : fb_col + movement);
        break;
    case 'L':
    case 'M':
        fb_console_clear_row_range(fb_row, 0, fb_cols);
        break;
    case 'm':
        if (!n) {
            fb_console_reset_attributes();
            break;
        }
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t code = fb_ansi_params[i];
            if (code == 0u) {
                fb_console_reset_attributes();
            } else if (code == 1u) {
                fb_console_bold = true;
            } else if (code == 2u) {
                fb_console_bold = false;
            } else if (code == 4u) {
                fb_console_underline = true;
            } else if (code == 7u) {
                fb_console_reverse = true;
            } else if (code == 22u) {
                fb_console_bold = false;
            } else if (code == 24u) {
                fb_console_underline = false;
            } else if (code == 27u) {
                fb_console_reverse = false;
            } else if ((code >= 30u && code <= 37u) || (code >= 90u && code <= 97u)) {
                fb_console_fg_color = fb_ansi_color(code);
            } else if (code == 39u) {
                fb_console_fg_color = console_fg();
            } else if (code >= 40u && code <= 47u) {
                fb_console_bg_color = fb_ansi_color(code - 10u);
            } else if (code >= 100u && code <= 107u) {
                fb_console_bg_color = fb_ansi_color(code - 10u);
            } else if (code == 49u) {
                fb_console_bg_color = console_panel();
            } else if (code == 38u || code == 48u) {
                bool background = code == 48u;
                if (i + 2u < n && fb_ansi_params[i + 1u] == 5u) {
                    uint32_t color = fb_ansi_palette(fb_ansi_params[i + 2u] > 255u
                                                         ? 255u : fb_ansi_params[i + 2u]);
                    if (background) fb_console_bg_color = color;
                    else fb_console_fg_color = color;
                    i += 2u;
                } else if (i + 4u < n && fb_ansi_params[i + 1u] == 2u) {
                    uint32_t r = fb_ansi_params[i + 2u] > 255u ? 255u : fb_ansi_params[i + 2u];
                    uint32_t g = fb_ansi_params[i + 3u] > 255u ? 255u : fb_ansi_params[i + 3u];
                    uint32_t b = fb_ansi_params[i + 4u] > 255u ? 255u : fb_ansi_params[i + 4u];
                    uint32_t color = (r << 16) | (g << 8) | b;
                    if (background) fb_console_bg_color = color;
                    else fb_console_fg_color = color;
                    i += 4u;
                }
            }
        }
        break;
    case 's':
        fb_saved_col = fb_col;
        fb_saved_row = fb_row;
        break;
    case 'u':
        fb_col = fb_saved_col < fb_cols ? fb_saved_col : 0u;
        fb_row = fb_saved_row < fb_rows ? fb_saved_row : 0u;
        break;
    default:
        break;
    }
}

static void fb_console_ansi_feed(char ch)
{
    unsigned char byte = (unsigned char)ch;
    if (!fb_console_enabled) {
        return;
    }
    if (fb_ansi_state == FB_ANSI_NORMAL) {
        if (byte == 0x07u || byte == 0x7fu || byte == 0x00u || byte == 0x0eu || byte == 0x0fu) {
            return;
        }
        if (byte == 0x1bU) {
            fb_ansi_state = FB_ANSI_ESC;
            return;
        }
        fb_console_putc(ch);
        return;
    }
    if (fb_ansi_state == FB_ANSI_ESC) {
        if (ch == '[') {
            fb_ansi_state = FB_ANSI_CSI;
            fb_ansi_param_count = 0;
            fb_ansi_param_value = 0;
            fb_ansi_param_active = false;
            return;
        }
        if (ch == '7') {
            fb_saved_col = fb_col;
            fb_saved_row = fb_row;
            fb_console_reset_ansi();
            return;
        }
        if (ch == 'c') {
            fb_col = 0;
            fb_row = 0;
            fb_scroll_top = 0;
            fb_scroll_bottom = fb_rows ? fb_rows - 1u : 0u;
            fb_console_reset_attributes();
            fb_console_clear_log();
            fb_console_reset_ansi();
            return;
        }
        if (ch == 'D') {
            fb_console_newline();
            fb_console_reset_ansi();
            return;
        }
        if (ch == 'M') {
            if (fb_row > fb_scroll_top) --fb_row;
            fb_console_reset_ansi();
            return;
        }
        if (ch == 'E') {
            fb_console_newline();
            fb_col = 0;
            fb_console_reset_ansi();
            return;
        }
        if (ch == '(' || ch == ')') {
            fb_ansi_state = FB_ANSI_CHARSET;
            return;
        }
        if (ch == ']' || ch == 'P' || ch == '^' || ch == '_' || ch == 'X') {
            fb_ansi_state = FB_ANSI_OSC;
            return;
        }
        if (ch == '\\' || ch == '=' || ch == '>') {
            fb_console_reset_ansi();
            return;
        }
        if (ch == '8') {
            fb_col = fb_saved_col < fb_cols ? fb_saved_col : 0u;
            fb_row = fb_saved_row < fb_rows ? fb_saved_row : 0u;
            fb_console_reset_ansi();
            return;
        }
        /* Unknown ESC sequences are control traffic; never render their
         * final byte as user-visible text. */
        fb_console_reset_ansi();
        return;
    }
    if (fb_ansi_state == FB_ANSI_CHARSET) {
        fb_console_reset_ansi();
        return;
    }
    if (fb_ansi_state == FB_ANSI_OSC) {
        if (byte == 0x07u) {
            fb_console_reset_ansi();
        } else if (byte == 0x1bu) {
            fb_ansi_state = FB_ANSI_OSC_ESC;
        }
        return;
    }
    if (fb_ansi_state == FB_ANSI_OSC_ESC) {
        fb_console_reset_ansi();
        return;
    }
    if ((ch == '?' || ch == '>') && !fb_ansi_param_count &&
        !fb_ansi_param_active) {
        fb_ansi_private = true;
        return;
    }
    if (byte >= '0' && byte <= '9') {
        if (fb_ansi_param_value < 10000u) {
            fb_ansi_param_value = fb_ansi_param_value * 10u + (byte - '0');
        }
        fb_ansi_param_active = true;
        return;
    }
    if (ch == ';') {
        if (fb_ansi_param_count < 8u) {
            fb_ansi_params[fb_ansi_param_count++] =
                fb_ansi_param_active ? fb_ansi_param_value : 0u;
        }
        fb_ansi_param_value = 0;
        fb_ansi_param_active = false;
        return;
    }
    if (byte >= 0x40u && byte <= 0x7eu) {
        if (fb_ansi_param_count < 8u && (fb_ansi_param_active || fb_ansi_param_count)) {
            fb_ansi_params[fb_ansi_param_count++] =
                fb_ansi_param_active ? fb_ansi_param_value : 0u;
        }
        if (!fb_ansi_private) {
            fb_console_execute_csi(ch);
        }
        fb_console_reset_ansi();
        return;
    }
    fb_console_reset_ansi();
}

static bool fb_console_use_handoff_state(const struct leonos_boot_log_state *boot_log)
{
    const struct framebuffer *fb = framebuffer_get();
    if (!boot_log || !fb->available || !boot_log->columns || !boot_log->rows ||
        boot_log->columns > CONSOLE_MAX_COLS || boot_log->rows > CONSOLE_MAX_ROWS ||
        boot_log->log_x >= fb->width || boot_log->log_y >= fb->height) {
        return false;
    }

    fb_x = boot_log->log_x;
    fb_y = boot_log->log_y;
    fb_cols = boot_log->columns;
    fb_rows = boot_log->rows;
    if (fb_cols * LEONOS_FONT_W > fb->width - fb_x) {
        fb_cols = (fb->width - fb_x) / LEONOS_FONT_W;
    }
    if (fb_rows * LEONOS_FONT_H > fb->height - fb_y) {
        fb_rows = (fb->height - fb_y) / LEONOS_FONT_H;
    }
    if (!fb_cols || !fb_rows) {
        return false;
    }

    fb_col = boot_log->column < fb_cols ? boot_log->column : 0u;
    fb_row = boot_log->row < fb_rows ? boot_log->row : fb_rows - 1u;
    fb_scroll_top = 0u;
    fb_scroll_bottom = fb_rows - 1u;
    fb_saved_col = fb_col;
    fb_saved_row = fb_row;
    fb_console_reset_attributes();
    fb_console_reset_ansi();
    return true;
}

static void fb_console_initialize_fullscreen(void)
{
    const struct framebuffer *fb = framebuffer_get();
    if (!fb->available) {
        return;
    }
    fb_x = 0;
    fb_y = 0;
    fb_cols = fb->width / LEONOS_FONT_W;
    fb_rows = fb->height / LEONOS_FONT_H;
    if (fb_cols > CONSOLE_MAX_COLS) {
        fb_cols = CONSOLE_MAX_COLS;
    }
    if (fb_rows > CONSOLE_MAX_ROWS) {
        fb_rows = CONSOLE_MAX_ROWS;
    }
    fb_col = 0;
    fb_row = 0;
    fb_saved_col = 0;
    fb_saved_row = 0;
    fb_scroll_top = 0u;
    fb_scroll_bottom = fb_rows ? fb_rows - 1u : 0u;
    fb_console_reset_attributes();
    fb_console_reset_ansi();
    framebuffer_rect(0, 0, fb->width, fb->height, console_panel());
}

static void console_emit_raw(char ch)
{
    char s[2] = {ch, 0};
    log_store(ch);
    serial_write(s);
    if (vga_console_enabled) {
        vga_putc(ch);
    }
    fb_console_putc(ch);
    console_line_start = ch == '\n';
}

static void console_present(void)
{
    if (console_presenting || !fb_console_enabled || !framebuffer_get()->available) {
        return;
    }
    console_presenting = true;
    if (fb_cols && fb_rows) {
        framebuffer_present_region(fb_x, fb_y,
                                   fb_cols * LEONOS_FONT_W,
                                   fb_rows * LEONOS_FONT_H);
    }
    console_presenting = false;
}

static void print_unsigned_raw(uint64_t value, unsigned base, bool upper)
{
    char buf[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t i = 0;

    if (value == 0) {
        console_emit_raw('0');
        return;
    }

    while (value && i < sizeof(buf)) {
        buf[i++] = digits[value % base];
        value /= base;
    }
    while (i) {
        console_emit_raw(buf[--i]);
    }
}

static void console_emit_timestamp(void)
{
    uint64_t uptime_us = console_boot_uptime_us + time_uptime_ms() * 1000ULL;
    uint64_t seconds = uptime_us / 1000000ULL;
    uint64_t micros = uptime_us % 1000000ULL;
    uint64_t divisor = 1ULL;
    uint32_t digits = 1U;

    while (seconds >= divisor * 10ULL && digits < 20U) {
        divisor *= 10ULL;
        ++digits;
    }
    console_emit_raw('[');
    for (uint32_t i = digits; i < 5U; ++i) {
        console_emit_raw(' ');
    }
    print_unsigned_raw(seconds, 10, false);
    console_emit_raw('.');
    console_emit_raw((char)('0' + (micros / 100000ULL) % 10ULL));
    console_emit_raw((char)('0' + (micros / 10000ULL) % 10ULL));
    console_emit_raw((char)('0' + (micros / 1000ULL) % 10ULL));
    console_emit_raw((char)('0' + (micros / 100ULL) % 10ULL));
    console_emit_raw((char)('0' + (micros / 10ULL) % 10ULL));
    console_emit_raw((char)('0' + micros % 10ULL));
    console_emit_raw(']');
    console_emit_raw(' ');
}

void console_init(void)
{
    serial_init();
}

void console_set_boot_uptime_us(uint64_t uptime_us)
{
    console_boot_uptime_us = uptime_us;
}

void console_set_ui_theme(uint32_t theme)
{
    console_ui_theme = theme == 0u ? 0u : 1u;
}

void console_putc(char ch)
{
    if (console_line_start) {
        console_emit_timestamp();
    }
    console_emit_raw(ch);
}

void console_write(const char *s)
{
    while (s && *s) {
        console_putc(*s++);
    }
}

void console_write_len(const char *s, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        console_putc(s[i]);
    }
}

void console_write_tty_len(const char *s, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        char byte[2] = {s[i], 0};
        log_store(s[i]);
        serial_write(byte);
        if (vga_console_enabled) {
            vga_putc(s[i]);
        }
        fb_console_ansi_feed(s[i]);
        console_line_start = s[i] == '\n';
    }
    console_present();
}

void console_enter_tty_runtime(void)
{
    console_runtime_quiet = true;
    console_line_start = true;
    if (fb_console_enabled) {
        fb_col = 0;
        fb_row = 0;
        fb_scroll_top = 0;
        fb_scroll_bottom = fb_rows ? fb_rows - 1u : 0u;
        fb_console_reset_attributes();
        fb_console_reset_ansi();
        fb_console_clear_log();
        console_present();
    } else if (vga_console_enabled) {
        vga_init();
    }
}

void console_printf(const char *fmt, ...)
{
    va_list ap;

    if (console_runtime_quiet && console_is_ntclks_format(fmt)) {
        return;
    }

    if (fmt && fmt[0] && console_line_start) {
        console_emit_timestamp();
    }
    va_start(ap, fmt);

    for (const char *p = fmt; *p; ++p) {
        if (*p != '%') {
            console_putc(*p);
            continue;
        }

        ++p;
        bool long_flag = false;
        bool long_long_flag = false;
        if (*p == 'l') {
            long_flag = true;
            ++p;
            if (*p == 'l') {
                long_long_flag = true;
                ++p;
            }
        }

        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            console_write(s ? s : "(null)");
            break;
        }
        case 'c':
            console_putc((char)va_arg(ap, int));
            break;
        case 'd':
        case 'i': {
            int64_t v = long_long_flag ? va_arg(ap, long long)
                        : long_flag  ? va_arg(ap, long)
                                     : va_arg(ap, int);
            if (v < 0) {
                console_putc('-');
                v = -v;
            }
            print_unsigned_raw((uint64_t)v, 10, false);
            break;
        }
        case 'u': {
            uint64_t v = long_long_flag ? va_arg(ap, unsigned long long)
                         : long_flag  ? va_arg(ap, unsigned long)
                                      : va_arg(ap, unsigned int);
            print_unsigned_raw(v, 10, false);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v = long_long_flag ? va_arg(ap, unsigned long long)
                         : long_flag  ? va_arg(ap, unsigned long)
                                      : va_arg(ap, unsigned int);
            print_unsigned_raw(v, 16, *p == 'X');
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            console_write("0x");
            print_unsigned_raw(v, 16, false);
            break;
        }
        case '%':
            console_putc('%');
            break;
        default:
            console_putc('%');
            console_putc(*p);
            break;
        }
    }

    va_end(ap);
    console_present();
}

void console_enable_framebuffer(const struct leonos_boot_log_state *boot_log)
{
    if (!framebuffer_get()->available) {
        return;
    }
    fb_console_enabled = true;
    if (!fb_console_use_handoff_state(boot_log)) {
        fb_console_initialize_fullscreen();
        fb_console_clear_log();
    }
    for (size_t i = 0; i < log_len; ++i) {
        fb_console_ansi_feed(log_buffer[i]);
    }
    console_present();
}

void console_disable_framebuffer(void)
{
    fb_console_enabled = false;
}

void console_enable_vga_fallback(void)
{
    if (framebuffer_get()->available) {
        return;
    }
    vga_init();
    vga_console_enabled = true;
    for (size_t i = 0; i < log_len; ++i) {
        vga_putc(log_buffer[i]);
    }
}
