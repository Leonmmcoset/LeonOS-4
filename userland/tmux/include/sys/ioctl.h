#ifndef LEONOS_TMUX_SYS_IOCTL_H
#define LEONOS_TMUX_SYS_IOCTL_H

#include_next <sys/ioctl.h>

/* Linux-compatible request used by tmux to drain a pane before cleanup. */
#ifndef FIONREAD
#define FIONREAD 0x541bUL
#endif

#endif
