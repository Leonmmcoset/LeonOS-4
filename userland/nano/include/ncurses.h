#ifndef LEONOS_NANO_NCURSES_H
#define LEONOS_NANO_NCURSES_H

#include <stdarg.h>

typedef unsigned long chtype;
typedef struct leonos_curses_window WINDOW;

extern WINDOW *stdscr;
extern int LINES;
extern int COLS;

#define curscr stdscr

#define ERR (-1)
#define OK 0
#define TRUE 1
#define FALSE 0

#define A_NORMAL ((chtype)0)
#define A_BOLD ((chtype)0x01)
#define A_REVERSE ((chtype)0x02)
#define A_ITALIC ((chtype)0x04)
#define COLOR_PAIR(number) ((chtype)((number) << 8))

#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7

#define KEY_CODE_YES 0x100
#define KEY_DOWN 0x102
#define KEY_UP 0x103
#define KEY_LEFT 0x104
#define KEY_RIGHT 0x105
#define KEY_HOME 0x106
#define KEY_BACKSPACE 0x107
#define KEY_F0 0x108
#define KEY_F(number) (KEY_F0 + (number))
#define KEY_DC 0x14a
#define KEY_IC 0x14b
#define KEY_NPAGE 0x152
#define KEY_PPAGE 0x153
#define KEY_END 0x164
#define KEY_ENTER 0x157
#define KEY_BTAB 0x161
#define KEY_RESIZE 0x19a
#define KEY_MOUSE 0x199
#define KEY_A1 0x1a1
#define KEY_A3 0x1a3
#define KEY_B2 0x1b2
#define KEY_BEG 0x1c0
#define KEY_CANCEL 0x1c1
#define KEY_C1 0x1c2
#define KEY_C3 0x1c3
#define KEY_CENTER 0x1c4
#define KEY_EOL 0x1c5
#define KEY_SBEG 0x1c6
#define KEY_SCANCEL 0x1c7
#define KEY_SDC 0x1c8
#define KEY_SDOWN 0x1c9
#define KEY_SEND 0x1ca
#define KEY_SF 0x1cb
#define KEY_SHOME 0x1cc
#define KEY_SIC 0x1cd
#define KEY_SLEFT 0x1ce
#define KEY_SNEXT 0x1cf
#define KEY_SPREVIOUS 0x1d0
#define KEY_SR 0x1d1
#define KEY_SRIGHT 0x1d2
#define KEY_SSUSPEND 0x1d3
#define KEY_SUP 0x1d4
#define KEY_SUSPEND 0x1d5
#define KEY_ALT_L 0x1d6
#define KEY_ALT_R 0x1d7
#define KEY_CONTROL_L 0x1d8
#define KEY_CONTROL_R 0x1d9
#define KEY_SHIFT_L 0x1dc
#define KEY_SHIFT_R 0x1dd

WINDOW *initscr(void);
int endwin(void);
int isendwin(void);
WINDOW *newwin(int rows, int columns, int y, int x);
int delwin(WINDOW *window);
int keypad(WINDOW *window, int enabled);
int nodelay(WINDOW *window, int enabled);
int scrollok(WINDOW *window, int enabled);
int raw(void);
int noecho(void);
int halfdelay(int tenths);
int napms(int milliseconds);
int nonl(void);
int typeahead(int fd);
int curs_set(int visibility);
int beep(void);
int start_color(void);
int use_default_colors(void);
int init_pair(short pair, short foreground, short background);
int wattron(WINDOW *window, chtype attributes);
int wattroff(WINDOW *window, chtype attributes);
int wmove(WINDOW *window, int y, int x);
int waddch(WINDOW *window, const chtype character);
int waddnstr(WINDOW *window, const char *text, int length);
int waddstr(WINDOW *window, const char *text);
int mvwaddch(WINDOW *window, int y, int x, const chtype character);
int mvwaddnstr(WINDOW *window, int y, int x, const char *text, int length);
int mvwaddstr(WINDOW *window, int y, int x, const char *text);
int wprintw(WINDOW *window, const char *format, ...);
int mvwprintw(WINDOW *window, int y, int x, const char *format, ...);
int wclrtoeol(WINDOW *window);
int werase(WINDOW *window);
int wrefresh(WINDOW *window);
int wnoutrefresh(WINDOW *window);
int doupdate(void);
int wredrawln(WINDOW *window, int begin, int count);
int wscrl(WINDOW *window, int lines);
int wgetch(WINDOW *window);
int ungetch(int input);
int key_defined(const char *definition);

#endif
