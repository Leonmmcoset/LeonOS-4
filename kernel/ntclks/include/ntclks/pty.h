#ifndef NTCLKS_PTY_H
#define NTCLKS_PTY_H

#include <ntclks/types.h>
#include <leonos/pty.h>

void pty_init(void);
int32_t pty_create(uint32_t owner_pid);
int pty_is_owner(uint32_t pty_id, uint32_t owner_pid);
int pty_is_active(uint32_t pty_id);
int64_t pty_read_output(uint32_t owner_pid, uint32_t pty_id, char *buffer, uint32_t length);
int64_t pty_write_input(uint32_t owner_pid, uint32_t pty_id, const char *buffer, uint32_t length);
int64_t pty_read_input(uint32_t pty_id, char *buffer, uint32_t length);
uint32_t pty_input_available(uint32_t pty_id);
int64_t pty_write_output(uint32_t pty_id, const char *buffer, uint32_t length);
int pty_get_termios(uint32_t pty_id, struct leonos_pty_termios *termios);
int pty_set_termios(uint32_t pty_id, const struct leonos_pty_termios *termios);
int pty_get_winsize(uint32_t pty_id, struct leonos_pty_winsize *winsize);
int pty_set_winsize(uint32_t pty_id, const struct leonos_pty_winsize *winsize);
void pty_process_exit(uint32_t pid);

#endif
