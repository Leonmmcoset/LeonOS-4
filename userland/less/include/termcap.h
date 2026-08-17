/* Minimal termcap ABI used by the upstream less terminal renderer. */
#ifndef LEONOS_LESS_TERMCAP_H
#define LEONOS_LESS_TERMCAP_H

int tgetent(char *buffer, const char *term);
int tgetflag(const char *name);
int tgetnum(const char *name);
char *tgetstr(const char *name, char **area);
char *tgoto(const char *capability, int column, int row);
int tputs(const char *string, int affected_lines, int (*putc_function)(int));

#endif
