#ifndef LEONOS_SL_CURSES_H
#define LEONOS_SL_CURSES_H

/* Small curses surface used by the upstream sl animation. */
typedef unsigned long chtype;

typedef struct leonos_sl_window WINDOW;

extern WINDOW *stdscr;
extern int LINES;
extern int COLS;

#define OK 0
#define ERR (-1)
#define TRUE 1
#define FALSE 0

WINDOW *initscr(void);
int endwin(void);
int nodelay(WINDOW *window, int enabled);
int noecho(void);
int leaveok(WINDOW *window, int enabled);
int scrollok(WINDOW *window, int enabled);
int curs_set(int visibility);
int mvaddch(int y, int x, chtype character);
int refresh(void);
int getch(void);
int mvcur(int old_y, int old_x, int new_y, int new_x);

#endif
