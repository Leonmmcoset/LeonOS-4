#include <leonos/psf_font.h>
#include <ntclks/console.h>
#include <ntclks/framebuffer.h>

#define CONSOLE_LOG_CAP 8192
#define CONSOLE_COLS 96
#define CONSOLE_ROWS 14

static char log_buffer[CONSOLE_LOG_CAP];
static size_t log_len;
static bool fb_console_enabled;
static bool vga_console_enabled;
static uint32_t fb_col;
static uint32_t fb_row;
static uint32_t console_ui_theme = 1u;

static uint32_t console_bg(void)
{
    return console_ui_theme == 0u ? 0x00000000u : 0x000078d4u;
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

static void fb_console_clear(void)
{
    const struct framebuffer *fb = framebuffer_get();
    if (!fb->available) {
        return;
    }
    uint32_t w = fb->width < 800 ? fb->width : 800;
    uint32_t h = fb->height < 240 ? fb->height : 240;
    framebuffer_rect(0, 0, w, h, console_bg());
    framebuffer_rect(0, h > 2 ? h - 2 : 0, w, 2,
                     console_ui_theme == 0u ? 0x00808080u : 0x005aa7e8u);
}

static void fb_console_newline(void)
{
    fb_col = 0;
    if (++fb_row >= CONSOLE_ROWS) {
        fb_console_clear();
        fb_row = 0;
    }
}

static void fb_console_putc(char ch)
{
    if (!fb_console_enabled || !framebuffer_get()->available) {
        return;
    }
    if (ch == '\r') {
        return;
    }
    if (ch == '\n') {
        fb_console_newline();
        return;
    }
    if (fb_col >= CONSOLE_COLS) {
        fb_console_newline();
    }
    framebuffer_rect(fb_col * LEONOS_FONT_W, fb_row * (LEONOS_FONT_H + 1),
                     LEONOS_FONT_W, LEONOS_FONT_H + 1, console_bg());
    framebuffer_text(fb_col * LEONOS_FONT_W, fb_row * (LEONOS_FONT_H + 1),
                     (char[]){ch, 0}, console_fg(), console_bg());
    ++fb_col;
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

void console_enable_framebuffer(void)
{
    if (!framebuffer_get()->available) {
        return;
    }
    fb_console_enabled = true;
    fb_col = 0;
    fb_row = 0;
    fb_console_clear();
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
