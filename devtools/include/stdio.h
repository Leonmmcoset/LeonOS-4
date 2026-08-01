#ifndef LEONOS_STDIO_COMPAT_H
#define LEONOS_STDIO_COMPAT_H

#include <stdarg.h>
#include <stddef.h>

typedef struct leonos_file FILE;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int puts(const char *text);
int snprintf(char *dst, size_t capacity, const char *fmt, ...);
int vsnprintf(char *dst, size_t capacity, const char *fmt, va_list args);
int snprintf(char *dst, size_t capacity, const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list args);
FILE *fopen(const char *path, const char *mode);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fclose(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int feof(FILE *stream);
char *fgets(char *buffer, int size, FILE *stream);
int fflush(FILE *stream);
int putchar(int ch);
int remove(const char *path);
int sscanf(const char *text, const char *format, ...);
int fileno(FILE *stream);

#endif
