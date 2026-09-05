/*
 * LeonOS ostui implementation.
 * Parses the common VT100/xterm control sequences needed by kernel diagnostics
 * while mirroring the original byte stream to the serial console.
 */
#include <leonos/psf_font.h>
#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <ntclks/input.h>
#include <ntclks/ostui.h>

#define OSTUI_MAX_PARAMS 16U
#define OSTUI_ESC 1U
#define OSTUI_CSI 2U
#define OSTUI_OSC 3U

struct ostui_state {
    uint32_t row;
    uint32_t col;
    uint32_t saved_row;
    uint32_t saved_col;
    uint32_t fg;
    uint32_t bg;
    uint32_t params[OSTUI_MAX_PARAMS];
    uint32_t param_count;
    uint32_t param_value;
    uint8_t param_active;
    uint8_t parser;
    uint8_t private_mode;
};

static struct ostui_state state;

static const uint32_t palette[16] = {
    0x00000000u, 0x00aa0000u, 0x0000aa00u, 0x00aaaa00u,
    0x000000aau, 0x00aa00aau, 0x0000aaaau, 0x00aaaaaau,
    0x00555555u, 0x00ff5555u, 0x0055ff55u, 0x00ffff55u,
    0x005555ffu, 0x00ff55ffu, 0x0055ffffu, 0x00ffffffu,
};

static uint32_t ostui_cols(void)
{
    const struct framebuffer *fb = framebuffer_get();
    return fb->available && LEONOS_FONT_W ? fb->width / LEONOS_FONT_W : 80U;
}

static uint32_t ostui_rows(void)
{
    const struct framebuffer *fb = framebuffer_get();
    return fb->available && LEONOS_FONT_H ? fb->height / LEONOS_FONT_H : 25U;
}

static uint32_t color_index(uint32_t value)
{
    if (value < 16U) return palette[value];
    if (value >= 232U && value <= 255U) {
        uint32_t level = 8U + (value - 232U) * 10U;
        return (level << 16) | (level << 8) | level;
    }
    if (value >= 16U && value <= 231U) {
        uint32_t cube = value - 16U;
        uint32_t red = cube / 36U;
        uint32_t green = (cube / 6U) % 6U;
        uint32_t blue = cube % 6U;
        red = red ? 55U + red * 40U : 0U;
        green = green ? 55U + green * 40U : 0U;
        blue = blue ? 55U + blue * 40U : 0U;
        return (red << 16) | (green << 8) | blue;
    }
    return palette[7];
}

static void ostui_scroll(void)
{
    const struct framebuffer *fb = framebuffer_get();
    uint32_t rows = ostui_rows();
    if (!fb->available || rows < 2U) {
        state.row = 0;
        state.col = 0;
        return;
    }
    uint32_t copy_h = (rows - 1U) * LEONOS_FONT_H;
    for (uint32_t y = 0; y < copy_h; ++y) {
        uint8_t *dst = (uint8_t *)fb->pixels + (uint64_t)y * fb->pitch;
        const uint8_t *src = (const uint8_t *)fb->pixels +
                             (uint64_t)(y + LEONOS_FONT_H) * fb->pitch;
        for (uint32_t x = 0; x < fb->width * fb->bytes_per_pixel; ++x) {
            dst[x] = src[x];
        }
    }
    framebuffer_rect(0, copy_h, fb->width, LEONOS_FONT_H, state.bg);
    state.row = rows - 1U;
}

static void ostui_newline(void)
{
    state.col = 0;
    if (state.row + 1U < ostui_rows()) {
        ++state.row;
    } else {
        ostui_scroll();
    }
}

static void ostui_put_visible(char ch)
{
    const struct framebuffer *fb = framebuffer_get();
    char text[2] = {ch, 0};
    if (ch == '\r') {
        state.col = 0;
        return;
    }
    if (ch == '\n') {
        ostui_newline();
        return;
    }
    if (ch == '\t') {
        uint32_t next = (state.col + 8U) & ~7U;
        while (state.col < next) {
            ostui_put_visible(' ');
        }
        return;
    }
    if (!fb->available) {
        return;
    }
    if (state.col >= ostui_cols()) {
        ostui_newline();
    }
    framebuffer_rect(state.col * LEONOS_FONT_W, state.row * LEONOS_FONT_H,
                     LEONOS_FONT_W, LEONOS_FONT_H, state.bg);
    framebuffer_text(state.col * LEONOS_FONT_W, state.row * LEONOS_FONT_H,
                     text, state.fg, state.bg);
    ++state.col;
}

static uint32_t param_or(uint32_t index, uint32_t fallback)
{
    if (index >= state.param_count || state.params[index] == 0U) {
        return fallback;
    }
    return state.params[index];
}

static void ostui_apply_sgr(void)
{
    uint32_t count = state.param_count ? state.param_count : 1U;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t p = state.param_count ? state.params[i] : 0U;
        if (p == 0U) {
            state.fg = palette[7];
            state.bg = palette[0];
        } else if (p >= 30U && p <= 37U) {
            state.fg = palette[p - 30U];
        } else if (p >= 40U && p <= 47U) {
            state.bg = palette[p - 40U];
        } else if (p >= 90U && p <= 97U) {
            state.fg = palette[p - 90U + 8U];
        } else if (p >= 100U && p <= 107U) {
            state.bg = palette[p - 100U + 8U];
        } else if (p == 39U) {
            state.fg = palette[7];
        } else if (p == 49U) {
            state.bg = palette[0];
        } else if (p == 38U || p == 48U) {
            /* Accept 256-color and true-color forms while retaining a small
             * framebuffer palette: map them to a stable nearest shade. */
            uint32_t mode = i + 1U < count ? state.params[i + 1U] : 0U;
            uint32_t value = i + 2U < count ? state.params[i + 2U] : 7U;
            if (mode == 5U) {
                if (p == 38U) state.fg = color_index(value);
                else state.bg = color_index(value);
                i += 2U;
            } else if (mode == 2U) {
                uint32_t green = i + 3U < count ? state.params[i + 3U] : 0U;
                uint32_t blue = i + 4U < count ? state.params[i + 4U] : 0U;
                uint32_t rgb = ((value > 255U ? 255U : value) << 16) |
                               ((green > 255U ? 255U : green) << 8) |
                               (blue > 255U ? 255U : blue);
                if (p == 38U) state.fg = rgb;
                else state.bg = rgb;
                i += 4U;
            }
        }
    }
}

static void ostui_finish_csi(char final)
{
    uint32_t n = param_or(0, 1U);
    if (final == 'A') state.row = state.row > n ? state.row - n : 0U;
    else if (final == 'B') state.row += n < ostui_rows() - state.row ? n : ostui_rows() - state.row - 1U;
    else if (final == 'C') state.col += n < ostui_cols() - state.col ? n : ostui_cols() - state.col - 1U;
    else if (final == 'D') state.col = state.col > n ? state.col - n : 0U;
    else if (final == 'G') state.col = n > 0U ? n - 1U : 0U;
    else if (final == 'd') state.row = n > 0U ? n - 1U : 0U;
    else if (final == 'H' || final == 'f') {
        state.row = param_or(0, 1U) - 1U;
        state.col = param_or(1, 1U) - 1U;
        if (state.row >= ostui_rows()) state.row = ostui_rows() - 1U;
        if (state.col >= ostui_cols()) state.col = ostui_cols() - 1U;
    } else if (final == 'J') {
        ostui_clear();
    } else if (final == 'K') {
        const struct framebuffer *fb = framebuffer_get();
        if (fb->available) {
            framebuffer_rect(state.col * LEONOS_FONT_W, state.row * LEONOS_FONT_H,
                             fb->width - state.col * LEONOS_FONT_W, LEONOS_FONT_H, state.bg);
        }
    } else if (final == 'm') {
        ostui_apply_sgr();
    } else if (final == 's') {
        state.saved_row = state.row;
        state.saved_col = state.col;
    } else if (final == 'u') {
        state.row = state.saved_row;
        state.col = state.saved_col;
    } else if (final == 'h' || final == 'l') {
        /**
 * @brief Alternate-screen mode is represented by a full clear.
 */
        if (state.private_mode && state.param_count && state.params[0] == 1049U) {
            ostui_clear();
        }
    }
    state.parser = 0;
    state.param_count = 0;
    state.param_value = 0;
    state.param_active = 0;
    state.private_mode = 0;
}

static void ostui_feed(char ch)
{
    if (state.parser == OSTUI_ESC) {
        if (ch == '[') {
            state.parser = OSTUI_CSI;
            state.param_count = 0;
            state.param_value = 0;
            state.param_active = 0;
            return;
        }
        if (ch == ']') {
            state.parser = OSTUI_OSC;
            return;
        }
        if (ch == '7') state.saved_row = state.row, state.saved_col = state.col;
        else if (ch == '8') state.row = state.saved_row, state.col = state.saved_col;
        state.parser = 0;
        return;
    }
    if (state.parser == OSTUI_OSC) {
        if (ch == 7 || ch == '\\') state.parser = 0;
        return;
    }
    if (state.parser == OSTUI_CSI) {
        if (ch == '?') {
            state.private_mode = 1;
            return;
        }
        if (ch >= '0' && ch <= '9') {
            state.param_value = state.param_value * 10U + (uint32_t)(ch - '0');
            state.param_active = 1;
            return;
        }
        if (ch == ';') {
            if (state.param_count < OSTUI_MAX_PARAMS) state.params[state.param_count++] = state.param_active ? state.param_value : 0U;
            state.param_value = 0;
            state.param_active = 0;
            return;
        }
        if (state.param_count < OSTUI_MAX_PARAMS && (state.param_active || state.param_count == 0U)) {
            state.params[state.param_count++] = state.param_active ? state.param_value : 0U;
        }
        ostui_finish_csi(ch);
        return;
    }
    if ((unsigned char)ch == 0x1bU) {
        state.parser = OSTUI_ESC;
        return;
    }
    ostui_put_visible(ch);
}

void ostui_clear(void)
{
    const struct framebuffer *fb = framebuffer_get();
    state.row = state.col = 0;
    if (fb->available) {
        framebuffer_clear(state.bg);
        framebuffer_present();
    }
}

void ostui_init(void)
{
    state = (struct ostui_state){.fg = palette[15], .bg = palette[0]};
    serial_write("\033[?1049h\033[2J\033[H");
    ostui_clear();
}

void ostui_write(const char *text)
{
    if (!text) return;
    serial_write(text);
    while (*text) ostui_feed(*text++);
    framebuffer_present();
}

void ostui_write_u64(uint64_t value)
{
    char buffer[24];
    uint32_t pos = sizeof(buffer);
    buffer[--pos] = 0;
    do { buffer[--pos] = (char)('0' + value % 10U); value /= 10U; } while (value && pos);
    ostui_write(buffer + pos);
}

int ostui_poll_key(void)
{
    struct input_raw_event event;
    while (input_pop(&event)) {
        if (event.type == INPUT_EVENT_KEYBOARD && event.pressed) return event.keycode;
    }
    return 0;
}
