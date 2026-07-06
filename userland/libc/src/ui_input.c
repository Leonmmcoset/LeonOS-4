#include <leonos/ui.h>

#include "ui_internal.h"

uint8_t ui_shift_down;

int ui_is_shift_key(uint8_t keycode)
{
    return keycode == LEONOS_KEY_LEFT_SHIFT || keycode == LEONOS_KEY_RIGHT_SHIFT;
}

int leonos_ui_keycode_to_char(uint8_t keycode, char *out)
{
    return leonos_ui_keycode_to_char_shift(keycode, ui_shift_down, out);
}

int leonos_ui_keycode_to_char_shift(uint8_t keycode, uint8_t shifted, char *out)
{
    if (!out) {
        return 0;
    }
    switch (keycode) {
    case LEONOS_KEY_BACKSPACE: *out = '\b'; return 1;
    case LEONOS_KEY_TAB: *out = '\t'; return 1;
    case LEONOS_KEY_ENTER: *out = '\n'; return 1;
    case 2: *out = shifted ? '!' : '1'; return 1;
    case 3: *out = shifted ? '@' : '2'; return 1;
    case 4: *out = shifted ? '#' : '3'; return 1;
    case 5: *out = shifted ? '$' : '4'; return 1;
    case 6: *out = shifted ? '%' : '5'; return 1;
    case 7: *out = shifted ? '^' : '6'; return 1;
    case 8: *out = shifted ? '&' : '7'; return 1;
    case 9: *out = shifted ? '*' : '8'; return 1;
    case 10: *out = shifted ? '(' : '9'; return 1;
    case 11: *out = shifted ? ')' : '0'; return 1;
    case 12: *out = shifted ? '_' : '-'; return 1;
    case 13: *out = shifted ? '+' : '='; return 1;
    case 16: *out = shifted ? 'Q' : 'q'; return 1;
    case 17: *out = shifted ? 'W' : 'w'; return 1;
    case 18: *out = shifted ? 'E' : 'e'; return 1;
    case 19: *out = shifted ? 'R' : 'r'; return 1;
    case 20: *out = shifted ? 'T' : 't'; return 1;
    case 21: *out = shifted ? 'Y' : 'y'; return 1;
    case 22: *out = shifted ? 'U' : 'u'; return 1;
    case 23: *out = shifted ? 'I' : 'i'; return 1;
    case 24: *out = shifted ? 'O' : 'o'; return 1;
    case 25: *out = shifted ? 'P' : 'p'; return 1;
    case 26: *out = shifted ? '{' : '['; return 1;
    case 27: *out = shifted ? '}' : ']'; return 1;
    case 30: *out = shifted ? 'A' : 'a'; return 1;
    case 31: *out = shifted ? 'S' : 's'; return 1;
    case 32: *out = shifted ? 'D' : 'd'; return 1;
    case 33: *out = shifted ? 'F' : 'f'; return 1;
    case 34: *out = shifted ? 'G' : 'g'; return 1;
    case 35: *out = shifted ? 'H' : 'h'; return 1;
    case 36: *out = shifted ? 'J' : 'j'; return 1;
    case 37: *out = shifted ? 'K' : 'k'; return 1;
    case 38: *out = shifted ? 'L' : 'l'; return 1;
    case 39: *out = shifted ? ':' : ';'; return 1;
    case 40: *out = shifted ? '"' : '\''; return 1;
    case 41: *out = shifted ? '~' : '`'; return 1;
    case 43: *out = shifted ? '|' : '\\'; return 1;
    case 44: *out = shifted ? 'Z' : 'z'; return 1;
    case 45: *out = shifted ? 'X' : 'x'; return 1;
    case 46: *out = shifted ? 'C' : 'c'; return 1;
    case 47: *out = shifted ? 'V' : 'v'; return 1;
    case 48: *out = shifted ? 'B' : 'b'; return 1;
    case 49: *out = shifted ? 'N' : 'n'; return 1;
    case 50: *out = shifted ? 'M' : 'm'; return 1;
    case 51: *out = shifted ? '<' : ','; return 1;
    case 52: *out = shifted ? '>' : '.'; return 1;
    case 53: *out = shifted ? '?' : '/'; return 1;
    case LEONOS_KEY_SPACE: *out = ' '; return 1;
    default:
        return 0;
    }
}
