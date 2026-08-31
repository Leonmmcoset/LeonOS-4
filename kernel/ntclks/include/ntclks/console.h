/*
 * LeonOS kernel console interface: declares early and diagnostic output.
 * Provides formatted console logging used before and during userland startup.
 */
#ifndef NTCLKS_CONSOLE_H
#define NTCLKS_CONSOLE_H

#include <ntclks/types.h>

struct leonos_boot_log_state;

/**
 * @brief Set up the kernel console backend before any output is written.
 */
void console_init(void);
/**
 * @brief Record the loader's elapsed boot time so boot-log timestamps start from zero.
 */
void console_set_boot_uptime_us(uint64_t uptime_us);
/**
 * @brief Write a single character to the active console backend.
 */
void console_putc(char ch);
/**
 * @brief Write a NUL-terminated string to the console.
 */
void console_write(const char *s);
/**
 * @brief Write exactly len bytes of s to the console; s need not be NUL-terminated.
 */
void console_write_len(const char *s, size_t len);
/**
 * @brief Write terminal data without adding diagnostic timestamps.
 */
void console_write_tty_len(const char *s, size_t len);
/**
 * @brief Enter the TTY runtime: hide kernel diagnostics and reset the visible console.
 */
void console_enter_tty_runtime(void);
/**
 * @brief Format and write a message to the console, printf-style.
 */
void console_printf(const char *fmt, ...);
/**
 * @brief Route console output to the framebuffer, using the boot log state.
 */
void console_enable_framebuffer(const struct leonos_boot_log_state *boot_log);
/**
 * @brief Turn off framebuffer output and fall back to another backend.
 */
void console_disable_framebuffer(void);
/**
 * @brief Route console output to the legacy VGA text buffer.
 */
void console_enable_vga_fallback(void);
/**
 * @brief Select the UI color theme used by the framebuffer console.
 */
void console_set_ui_theme(uint32_t theme);
/**
 * @brief Initialize the COM1 serial port for early diagnostic output.
 */
void serial_init(void);
/**
 * @brief Return non-zero when the serial backend is available for output.
 */
int serial_is_ready(void);
/**
 * @brief Write a string to the serial backend (or the early COM1 fallback).
 */
void serial_write(const char *s);
/**
 * @brief Initialize the legacy VGA text-mode console.
 */
void vga_init(void);
/**
 * @brief Write one character to the VGA text buffer, handling newlines and scrolling.
 */
void vga_putc(char ch);
/**
 * @brief Write a string into the VGA text buffer at column x, row y.
 */
void vga_write_at(uint8_t x, uint8_t y, const char *s);

#endif
