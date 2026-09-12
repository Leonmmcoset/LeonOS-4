#ifndef LEONOS_PTY_H
#define LEONOS_PTY_H

#include <stdint.h>
#include <linux/termios.h>

#define LEONOS_PTY_PATH_LEN 160U
#define LEONOS_PTY_NCCS LINUX_NCCS
#define LEONOS_PTY_IFLAG_ICRNL LINUX_ICRNL
#define LEONOS_PTY_LFLAG_ECHO LINUX_ECHO
#define LEONOS_PTY_LFLAG_ECHONL LINUX_ECHONL
#define LEONOS_PTY_LFLAG_ICANON LINUX_ICANON
#define LEONOS_PTY_LFLAG_IEXTEN LINUX_IEXTEN
#define LEONOS_PTY_LFLAG_ISIG LINUX_ISIG

#define LEONOS_PTY_CC_VEOF LINUX_VEOF
#define LEONOS_PTY_CC_VEOL LINUX_VEOL
#define LEONOS_PTY_CC_VERASE LINUX_VERASE
#define LEONOS_PTY_CC_VINTR LINUX_VINTR
#define LEONOS_PTY_CC_VKILL LINUX_VKILL
#define LEONOS_PTY_CC_VMIN LINUX_VMIN
#define LEONOS_PTY_CC_VQUIT LINUX_VQUIT
#define LEONOS_PTY_CC_VSTART LINUX_VSTART
#define LEONOS_PTY_CC_VSTOP LINUX_VSTOP
#define LEONOS_PTY_CC_VSUSP LINUX_VSUSP
#define LEONOS_PTY_CC_VTIME LINUX_VTIME

/* Source compatibility only: old Picolibc wire layouts require rebuilding. */
#define leonos_pty_termios linux_termios2

/* The terminal host owns a session but is not itself attached to it. Keep
 * host-side controls separate from the stdio-based child-process requests. */
struct leonos_pty_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
};

#endif
