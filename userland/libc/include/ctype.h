#ifndef LEONOS_CTYPE_H
#define LEONOS_CTYPE_H

static inline int isspace(int ch)
{
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

static inline int isdigit(int ch)
{
    return ch >= '0' && ch <= '9';
}

static inline int isalpha(int ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static inline int isascii(int ch)
{
    return (unsigned int)ch <= 0x7fU;
}

static inline int isalnum(int ch)
{
    return isalpha(ch) || isdigit(ch);
}

static inline int isupper(int ch)
{
    return ch >= 'A' && ch <= 'Z';
}

static inline int islower(int ch)
{
    return ch >= 'a' && ch <= 'z';
}

static inline int isprint(int ch)
{
    return ch >= 0x20 && ch <= 0x7e;
}

static inline int tolower(int ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static inline int toupper(int ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
}

#endif
