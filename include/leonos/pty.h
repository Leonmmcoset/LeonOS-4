#ifndef LEONOS_PTY_H
#define LEONOS_PTY_H

#include <stdint.h>

/* Keep the standard small-TTY requests usable by portable applications. */
/* Linux-compatible process-group TTY requests used by Picolibc. */

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
struct leonos_pty_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
};

#endif
