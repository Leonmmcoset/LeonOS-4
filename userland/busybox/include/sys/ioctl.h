#ifndef _SYS_IOCTL_H_
#define _SYS_IOCTL_H_

/*
 * BusyBox is compiled against LeonOS' POSIX libc, but terminal ioctl request
 * numbers must use the Linux UAPI values.  Picolibc's fallback header uses
 * the historical ('T' << 8) | 1 value for TIOCGWINSZ, which is TCGETS on
 * Linux and makes lineedit pass a winsize buffer to the termios handler.
 */
#include <linux/ioctl.h>

int ioctl(int fd, unsigned long request, void *arg);

#define TIOCGPGRP  0x540fUL
#define TIOCSPGRP  0x5410UL
#define TIOCGWINSZ 0x5413UL
#define TIOCSWINSZ 0x5414UL

#define TCGETS     0x5401UL
#define TCSETS     0x5402UL
#define TCSETSW    0x5403UL
#define TCSETSF    0x5404UL

#endif
