#ifndef LEONOS_TMUX_TERM_H
#define LEONOS_TMUX_TERM_H

#include <stdarg.h>

typedef struct leonos_terminal TERMINAL;

extern TERMINAL *cur_term;

#ifndef OK
#define OK 0
#endif
#ifndef ERR
#define ERR (-1)
#endif

int setupterm(char *term, int fd, int *error);
int del_curterm(TERMINAL *term);
char *tigetstr(char *name);
int tigetnum(char *name);
int tigetflag(char *name);
char *tiparm(const char *capability, ...);
char *tparm(char *capability, long p1, long p2, long p3, long p4, long p5,
            long p6, long p7, long p8, long p9);

#endif
