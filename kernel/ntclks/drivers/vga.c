#include <ntclks/console.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile uint16_t *const vga = (volatile uint16_t *)0xb8000;
static uint8_t row;
static uint8_t col;
static uint8_t color = 0x1f;
static int initialized;

static uint16_t entry(char ch)
{
    return (uint16_t)ch | ((uint16_t)color << 8);
}

static void scroll(void)
{
    for (uint32_t y = 1; y < VGA_HEIGHT; ++y) {
        for (uint32_t x = 0; x < VGA_WIDTH; ++x) {
            vga[(y - 1) * VGA_WIDTH + x] = vga[y * VGA_WIDTH + x];
        }
    }
    for (uint32_t x = 0; x < VGA_WIDTH; ++x) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = entry(' ');
    }
    row = VGA_HEIGHT - 1;
}

void vga_init(void)
{
    row = 0;
    col = 0;
    initialized = 1;
    for (uint32_t y = 0; y < VGA_HEIGHT; ++y) {
        for (uint32_t x = 0; x < VGA_WIDTH; ++x) {
            vga[y * VGA_WIDTH + x] = entry(' ');
        }
    }
}

void vga_putc(char ch)
{
    if (!initialized) {
        return;
    }
    if (ch == '\r') {
        return;
    }
    if (ch == '\n') {
        col = 0;
        if (++row == VGA_HEIGHT) {
            scroll();
        }
        return;
    }
    vga[row * VGA_WIDTH + col] = entry(ch);
    if (++col == VGA_WIDTH) {
        col = 0;
        if (++row == VGA_HEIGHT) {
            scroll();
        }
    }
}

void vga_write_at(uint8_t x, uint8_t y, const char *s)
{
    if (!initialized || x >= VGA_WIDTH || y >= VGA_HEIGHT) {
        return;
    }
    uint32_t pos = y * VGA_WIDTH + x;
    for (uint32_t i = 0; s && s[i] && x + i < VGA_WIDTH; ++i) {
        vga[pos + i] = entry(s[i]);
    }
}
