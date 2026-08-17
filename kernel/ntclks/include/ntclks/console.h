/*
 * LeonOS kernel console interface: declares early and diagnostic output.
 * Provides formatted console logging used before and during userland startup.
 */
#ifndef NTCLKS_CONSOLE_H
#define NTCLKS_CONSOLE_H

#include <ntclks/types.h>

struct leonos_boot_log_state;

/**
 * @brief Coordinates the console init operation.
 */
void console_init(void);
/**
 * @brief Sets the loader-provided uptime origin for boot log timestamps.
 * @param uptime_us Elapsed boot time in microseconds at kernel entry.
 */
void console_set_boot_uptime_us(uint64_t uptime_us);
/**
 * @brief Coordinates the console putc operation.
 * @param ch Input or output value used by this operation.
 */
void console_putc(char ch);
/**
 * @brief Coordinates the console write operation.
 * @param s Input or output value used by this operation.
 */
void console_write(const char *s);
/**
 * @brief Coordinates the console write len operation.
 * @param s Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 */
void console_write_len(const char *s, size_t len);
/**
 * @brief Coordinates the console printf operation.
 * @param fmt Input or output value used by this operation.
 */
void console_printf(const char *fmt, ...);
/**
 * @brief Coordinates the console enable framebuffer operation.
 * @param boot_log Input or output value used by this operation.
 */
void console_enable_framebuffer(const struct leonos_boot_log_state *boot_log);
/**
 * @brief Coordinates the console disable framebuffer operation.
 */
void console_disable_framebuffer(void);
/**
 * @brief Coordinates the console enable vga fallback operation.
 */
void console_enable_vga_fallback(void);
/**
 * @brief Coordinates the console set ui theme operation.
 * @param theme Input or output value used by this operation.
 */
void console_set_ui_theme(uint32_t theme);
/**
 * @brief Coordinates the serial init operation.
 */
void serial_init(void);
/**
 * @brief Coordinates the serial is ready operation.
 * @return Result, status, or value defined by this API.
 */
int serial_is_ready(void);
/**
 * @brief Coordinates the serial write operation.
 * @param s Input or output value used by this operation.
 */
void serial_write(const char *s);
/**
 * @brief Coordinates the vga init operation.
 */
void vga_init(void);
/**
 * @brief Coordinates the vga putc operation.
 * @param ch Input or output value used by this operation.
 */
void vga_putc(char ch);
/**
 * @brief Coordinates the vga write at operation.
 * @param x Input or output value used by this operation.
 * @param y Input or output value used by this operation.
 * @param s Input or output value used by this operation.
 */
void vga_write_at(uint8_t x, uint8_t y, const char *s);

#endif
