#ifndef LEONOS_TMUX_CONFIG_H
#define LEONOS_TMUX_CONFIG_H

#define _GNU_SOURCE 1

#define HAVE_EVENT2_EVENT_H 1
#define HAVE_STDINT_H 1
#define HAVE_PTY_H 1
#define HAVE_FORKPTY 1
#define HAVE_SIGACTION 1
#define HAVE_SETENV 1
#define HAVE_STRLCPY 1
#define HAVE_STRLCAT 1
#define HAVE_STRNLEN 1
#define HAVE_STRNDUP 1
#define HAVE_MEMMEM 1
#define HAVE_ASPRINTF 1
#define HAVE_REALLOCARRAY 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_FLOCK 1
#define HAVE_NCURSES_H 1
#define HAVE_SYSCONF 1

#define TMUX_VERSION "3.5a"
#define TMUX_TERM "screen-256color"
#define TMUX_CONF "/system/etc/tmux.conf:~/.tmux.conf"
#define TMUX_SOCK "$TMUX_TMPDIR:/tmp"

#ifndef _POSIX_VDISABLE
#define _POSIX_VDISABLE 0
#endif

/* Optional BSD/Linux local-mode flags absent from the LeonOS termios ABI. */
#ifndef ECHOCTL
#define ECHOCTL 0
#endif
#ifndef ECHOKE
#define ECHOKE 0
#endif

#endif
