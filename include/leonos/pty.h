#ifndef LEONOS_PTY_H
#define LEONOS_PTY_H

#include <stdint.h>

#define LEONOS_PTY_IOCTL_CREATE 0x4c505443UL
#define LEONOS_PTY_IOCTL_READ_OUTPUT 0x4c505452UL
#define LEONOS_PTY_IOCTL_WRITE_INPUT 0x4c505457UL
#define LEONOS_PTY_IOCTL_SPAWN 0x4c505453UL
#define LEONOS_PTY_IOCTL_SELF 0x4c505449UL

#define LEONOS_PTY_PATH_LEN 160U

struct leonos_pty_io {
    uint32_t pty_id;
    uint32_t length;
    char *buffer;
};

struct leonos_pty_spawn {
    uint32_t pty_id;
    const char *path;
    char *const *argv;
    char *const *envp;
};

int leonos_pty_create(void);
int leonos_pty_read_output(uint32_t pty_id, char *buffer, uint32_t length);
int leonos_pty_write_input(uint32_t pty_id, const char *buffer, uint32_t length);
int leonos_pty_spawn(const char *path, uint32_t pty_id);
int leonos_pty_spawn_argv(const char *path, uint32_t pty_id,
                          char *const argv[], char *const envp[]);
int leonos_pty_self(void);

#endif
