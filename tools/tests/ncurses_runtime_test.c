#define _XOPEN_SOURCE_EXTENDED 1
#include <assert.h>
#include <curses.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <term.h>
#include <wchar.h>

int main(void)
{
    assert(setlocale(LC_ALL, "C.UTF-8"));
    FILE *input = tmpfile();
    FILE *output = tmpfile();
    assert(input && output);
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen);
    assert(tigetnum("colors") == 256);
    char *cursor = tigetstr("cup");
    assert(cursor && cursor != (char *)-1);
    assert(strstr(tparm(cursor, 2L, 3L), "3;4") != NULL);
    WINDOW *window = newwin(4, 30, 0, 0);
    assert(window);
    assert(waddwstr(window, L"ncurses \u4e2d\u6587") != ERR);
    assert(wmove(window, 0, 8) != ERR);
    cchar_t cell;
    wchar_t characters[CCHARW_MAX];
    attr_t attributes;
    short pair;
    assert(win_wch(window, &cell) != ERR);
    assert(getcchar(&cell, characters, &attributes, &pair, NULL) != ERR);
    assert(characters[0] == L'\u4e2d');
    assert(wnoutrefresh(window) != ERR);
    assert(doupdate() != ERR);
    assert(delwin(window) != ERR);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    puts("PASS ncurses: terminfo, cursor expansion, wide characters, window refresh");
    return 0;
}
