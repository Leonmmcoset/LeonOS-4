#ifndef LEONOS_UAPI_LINUX_TTY_H
#define LEONOS_UAPI_LINUX_TTY_H

#include <stdint.h>

struct linux_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

#define TIOCGWINSZ 0x5413UL
#define TIOCSWINSZ 0x5414UL
#define TIOCSCTTY  0x540eUL
#define TIOCGPGRP  0x540fUL
#define TIOCSPGRP  0x5410UL

/* Linux native x86-64 ioctl encodings; TCGETS transfers 36 bytes. */
#define TCGETS     0x5401UL
#define TCSETS     0x5402UL
#define TCSETSW    0x5403UL
#define TCSETSF    0x5404UL
#define LINUX_TCGETS2  0x802c542aUL
#define LINUX_TCSETS2  0x402c542bUL
#define LINUX_TCSETSW2 0x402c542cUL
#define LINUX_TCSETSF2 0x402c542dUL
#define TIOCOUTQ   0x5411UL
#define FIONREAD   0x541BUL
#define FIONBIO    0x5421UL
#define FIONCLEX   0x5450UL
#define FIOCLEX    0x5451UL

/* Unix98 PTY helpers. */
#define TIOCGPTN   0x80045430UL
#define TIOCSPTLCK 0x40045431UL
#define TIOCGPTLCK 0x8004542eUL

#endif
