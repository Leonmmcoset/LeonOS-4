/*
 * LeonOS kernel TUI interface.
 * Provides a small VT100/xterm renderer for framebuffer and serial output.
 */
#ifndef NTCLKS_OSTUI_H
#define NTCLKS_OSTUI_H

#include <ntclks/types.h>

void ostui_init(void);
void ostui_clear(void);
void ostui_write(const char *text);
void ostui_write_u64(uint64_t value);
int ostui_poll_key(void);

#endif
