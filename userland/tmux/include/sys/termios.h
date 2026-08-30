#ifndef LEONOS_TMUX_SYS_TERMIOS_H
#define LEONOS_TMUX_SYS_TERMIOS_H

/* Picolibc's compact winsize has rows and columns only. Keep its termios
 * declarations, then expose the conventional pixel fields expected by tmux. */
#define winsize leonos_picolibc_winsize
#include_next <sys/termios.h>
#undef winsize

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#endif
