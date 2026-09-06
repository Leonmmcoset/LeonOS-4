#ifndef LEONOS_PTY_H
#define LEONOS_PTY_H

#include <stdint.h>

/* Linux-compatible process-group TTY requests used by Picolibc. */

#define LEONOS_PTY_PATH_LEN 160U
#define LEONOS_PTY_NCCS 11U
#define LEONOS_PTY_IFLAG_ICRNL 0x0002U

struct leonos_pty_spawn {
    uint32_t pty_id;
    const char *path;
    char *const *argv;
    char *const *envp;
    int32_t stdin_fd;
    int32_t stdout_fd;
    int32_t stderr_fd;
};

struct leonos_pty_termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t c_cc[LEONOS_PTY_NCCS];
    uint8_t reserved;
    uint32_t c_ispeed;
    uint32_t c_ospeed;
};

struct leonos_pty_termios_request {
    uint32_t action;
    uint32_t reserved;
    struct leonos_pty_termios termios;
};

struct leonos_pty_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
};

#endif
