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

static inline int tolower(int ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static inline int toupper(int ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
}

#endif
