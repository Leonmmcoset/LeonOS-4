#ifndef LEONOS_UAPI_LINUX_TTY_H
#define LEONOS_UAPI_LINUX_TTY_H

#include <stdint.h>

#ifndef _SYS_TERMIOS_H_
struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};
#endif

#define TIOCGWINSZ 0x5413UL
#define TIOCSWINSZ 0x5414UL
#define TIOCGPGRP  0x540fUL
#define TIOCSPGRP  0x5410UL

/* Linux termios requests (the BSD-compatible 0x5401/0x5402 aliases are
 * accepted by the kernel as well). */
#define TCGETS     0x5401UL
#define TCSETS     0x5402UL
#define TCSETSW    0x5403UL
#define TCSETSF    0x5404UL
#define TIOCOUTQ   0x5411UL
#define FIONREAD   0x541BUL

/* Unix98 PTY helpers. */
#define TIOCGPTN   0x80045430UL
#define TIOCSPTLCK 0x40045431UL

#endif
