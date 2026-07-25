#ifndef LEONOS_STDIO_COMPAT_H
#define LEONOS_STDIO_COMPAT_H

#include <stdarg.h>
#include <stddef.h>

typedef struct leonos_file FILE;

int printf(const char *fmt, ...);
int puts(const char *text);
int snprintf(char *dst, size_t capacity, const char *fmt, ...);
int vsnprintf(char *dst, size_t capacity, const char *fmt, va_list args);
int fprintf(FILE *stream, const char *fmt, ...);
FILE *fopen(const char *path, const char *mode);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
int fclose(FILE *stream);

#endif
