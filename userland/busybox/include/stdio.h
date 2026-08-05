#ifndef LEONOS_BUSYBOX_STDIO_H
#define LEONOS_BUSYBOX_STDIO_H

#include_next <stdio.h>

/* Picolibc's putchar_unlocked macro takes a stream argument despite exposing
 * the correct one-argument function declaration. Keep the function visible. */
#undef putchar_unlocked

static inline char *fgets_unlocked(char *buffer, int length, FILE *stream)
{
    return fgets(buffer, length, stream);
}

static inline int fputs_unlocked(const char *text, FILE *stream)
{
    return fputs(text, stream);
}

static inline int fileno_unlocked(FILE *stream)
{
    return fileno(stream);
}

#endif
