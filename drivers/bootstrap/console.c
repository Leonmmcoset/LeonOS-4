#include <leonos/boot_handoff.h>
#include <leonos/psf_font.h>
#include <ntclks/console.h>
#include <ntclks/framebuffer.h>

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

static uint32_t console_panel(void)
{
    return console_ui_theme == 0u ? 0x00000000u : 0x001b2a3au;
}

static uint32_t console_fg(void)
{
    return console_ui_theme == 0u ? 0x0000ff00u : 0x00ffffffu;
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
                     fb_cols * LEONOS_FONT_W, LEONOS_FONT_H, console_panel());
}

static void fb_console_clear_log(void)
{
    const struct framebuffer *fb = framebuffer_get();
    if (!fb->available || !fb_cols || !fb_rows) {
        return;
    }
    framebuffer_rect(fb_x, fb_y, fb_cols * LEONOS_FONT_W,
                     fb_rows * LEONOS_FONT_H, console_panel());
}

static void fb_console_scroll(void)
{
    const struct framebuffer *fb = framebuffer_get();
    if (!fb->available || fb_rows < 2u || fb_x >= fb->width || fb_y >= fb->height) {
        fb_console_clear_log();
        fb_row = 0;
        return;
    }

    uint32_t width = fb_cols * LEONOS_FONT_W;
    if (width > fb->width - fb_x) {
        width = fb->width - fb_x;
    }

    uint32_t src_y = fb_y + LEONOS_FONT_H;
    if (src_y >= fb->height) {
        fb_console_clear_log();
        fb_row = 0;
        return;
    }

    uint32_t copy_h = (fb_rows - 1u) * LEONOS_FONT_H;
    if (copy_h > fb->height - src_y) {
        copy_h = fb->height - src_y;
    }

    for (uint32_t y = 0; y < copy_h; ++y) {
        uint8_t *dst = (uint8_t *)fb->pixels + (uint64_t)(fb_y + y) * fb->pitch +
                       (uint64_t)fb_x * fb->bytes_per_pixel;
        const uint8_t *src = (const uint8_t *)fb->pixels +
                             (uint64_t)(src_y + y) * fb->pitch +
                             (uint64_t)fb_x * fb->bytes_per_pixel;
        for (uint32_t byte = 0; byte < width * fb->bytes_per_pixel; ++byte) {
            dst[byte] = src[byte];
        }
    }

    fb_row = fb_rows - 1u;
    fb_console_clear_log_line(fb_row);
}

static void fb_console_newline(void)
{
    fb_col = 0;
    if (fb_row + 1u < fb_rows) {
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
        return;
    }
    if (ch == '\n') {
        fb_console_newline();
        return;
    }
    if (fb_col >= fb_cols) {
        fb_console_newline();
    }
    framebuffer_rect(fb_x + fb_col * LEONOS_FONT_W, fb_y + fb_row * LEONOS_FONT_H,
                     LEONOS_FONT_W, LEONOS_FONT_H, console_panel());
    framebuffer_text(fb_x + fb_col * LEONOS_FONT_W, fb_y + fb_row * LEONOS_FONT_H,
                     (char[]){ch, 0}, console_fg(), console_panel());
    ++fb_col;
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
    framebuffer_rect(0, 0, fb->width, fb->height, console_panel());
}

static void print_unsigned(uint64_t value, unsigned base, bool upper)
{
    char buf[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t i = 0;

    if (value == 0) {
        console_putc('0');
        return;
    }

    while (value && i < sizeof(buf)) {
        buf[i++] = digits[value % base];
        value /= base;
    }
    while (i) {
        console_putc(buf[--i]);
    }
}

void console_init(void)
{
    serial_init();
}

void console_set_ui_theme(uint32_t theme)
{
    console_ui_theme = theme == 0u ? 0u : 1u;
}

void console_putc(char ch)
{
    char s[2] = {ch, 0};
    log_store(ch);
    serial_write(s);
    if (vga_console_enabled) {
        vga_putc(ch);
    }
    fb_console_putc(ch);
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

void console_printf(const char *fmt, ...)
{
    va_list ap;
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
            print_unsigned((uint64_t)v, 10, false);
            break;
        }
        case 'u': {
            uint64_t v = long_long_flag ? va_arg(ap, unsigned long long)
                         : long_flag  ? va_arg(ap, unsigned long)
                                      : va_arg(ap, unsigned int);
            print_unsigned(v, 10, false);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v = long_long_flag ? va_arg(ap, unsigned long long)
                         : long_flag  ? va_arg(ap, unsigned long)
                                      : va_arg(ap, unsigned int);
            print_unsigned(v, 16, *p == 'X');
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            console_write("0x");
            print_unsigned(v, 16, false);
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
        fb_console_putc(log_buffer[i]);
    }
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
