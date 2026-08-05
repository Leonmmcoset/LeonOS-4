#ifndef LEONOS_PTY_H
#define LEONOS_PTY_H

#include <stdint.h>

#define LEONOS_PTY_IOCTL_CREATE 0x4c505443UL
#define LEONOS_PTY_IOCTL_READ_OUTPUT 0x4c505452UL
#define LEONOS_PTY_IOCTL_WRITE_INPUT 0x4c505457UL
#define LEONOS_PTY_IOCTL_SPAWN 0x4c505453UL
#define LEONOS_PTY_IOCTL_SELF 0x4c505449UL
#define LEONOS_PTY_IOCTL_INPUT_AVAILABLE 0x4c505441UL
#define LEONOS_PTY_IOCTL_GET_ATTR 0x4c505447UL
#define LEONOS_PTY_IOCTL_SET_ATTR 0x4c505454UL
#define LEONOS_PTY_IOCTL_OWNER_GET_ATTR 0x4c50544dUL
#define LEONOS_PTY_IOCTL_OWNER_SET_ATTR 0x4c50544eUL
#define LEONOS_PTY_IOCTL_OWNER_GET_WINSIZE 0x4c50544fUL
#define LEONOS_PTY_IOCTL_OWNER_SET_WINSIZE 0x4c505450UL
/* Keep the standard small-TTY requests usable by portable applications. */
#define LEONOS_PTY_IOCTL_GET_WINSIZE 0x5401UL
#define LEONOS_PTY_IOCTL_SET_WINSIZE 0x5402UL

#define LEONOS_PTY_PATH_LEN 160U
#define LEONOS_PTY_NCCS 11U
#define LEONOS_PTY_IFLAG_ICRNL 0x0002U
#define LEONOS_PTY_LFLAG_ECHO 0x0001U
#define LEONOS_PTY_LFLAG_ECHONL 0x0008U
#define LEONOS_PTY_LFLAG_ICANON 0x0010U
#define LEONOS_PTY_LFLAG_IEXTEN 0x0020U
#define LEONOS_PTY_LFLAG_ISIG 0x0040U

#define LEONOS_PTY_CC_VEOF 0U
#define LEONOS_PTY_CC_VEOL 1U
#define LEONOS_PTY_CC_VERASE 2U
#define LEONOS_PTY_CC_VINTR 3U
#define LEONOS_PTY_CC_VKILL 4U
#define LEONOS_PTY_CC_VMIN 5U
#define LEONOS_PTY_CC_VQUIT 6U
#define LEONOS_PTY_CC_VSTART 7U
#define LEONOS_PTY_CC_VSTOP 8U
#define LEONOS_PTY_CC_VSUSP 9U
#define LEONOS_PTY_CC_VTIME 10U

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

/* This mirrors the Picolibc termios layout without exposing libc headers to
 * the kernel. The kernel currently consumes the flag and control-byte fields
 * that affect raw/cooked mode; baud and character flags are retained for
 * round-tripping portable programs. */
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

/* The terminal host owns a session but is not itself attached to it. Keep
 * host-side controls separate from the stdio-based child-process requests. */
struct leonos_pty_termios_io {
    uint32_t pty_id;
    uint32_t action;
    struct leonos_pty_termios termios;
};

struct leonos_pty_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
};

struct leonos_pty_winsize_io {
    uint32_t pty_id;
    struct leonos_pty_winsize winsize;
};

int leonos_pty_create(void);
int leonos_pty_read_output(uint32_t pty_id, char *buffer, uint32_t length);
int leonos_pty_write_input(uint32_t pty_id, const char *buffer, uint32_t length);
int leonos_pty_spawn(const char *path, uint32_t pty_id);
int leonos_pty_spawn_argv(const char *path, uint32_t pty_id,
                          char *const argv[], char *const envp[]);
int leonos_pty_self(void);
int leonos_pty_input_available(void);
int leonos_pty_get_termios(uint32_t pty_id, struct leonos_pty_termios *termios);
int leonos_pty_set_termios(uint32_t pty_id,
                           const struct leonos_pty_termios *termios);
int leonos_pty_get_winsize(uint32_t pty_id, struct leonos_pty_winsize *winsize);
int leonos_pty_set_winsize(uint32_t pty_id,
                            const struct leonos_pty_winsize *winsize);

#endif
