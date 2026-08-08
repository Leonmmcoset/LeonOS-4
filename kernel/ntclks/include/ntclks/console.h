#ifndef NTCLKS_CONSOLE_H
#define NTCLKS_CONSOLE_H

#include <ntclks/types.h>

struct leonos_boot_log_state;

void console_init(void);
void console_putc(char ch);
void console_write(const char *s);
void console_write_len(const char *s, size_t len);
void console_printf(const char *fmt, ...);
void console_enable_framebuffer(const struct leonos_boot_log_state *boot_log);
void console_disable_framebuffer(void);
void console_enable_vga_fallback(void);
void console_set_ui_theme(uint32_t theme);
void serial_init(void);
int serial_is_ready(void);
void serial_write(const char *s);
void vga_init(void);
void vga_putc(char ch);
void vga_write_at(uint8_t x, uint8_t y, const char *s);

#endif
