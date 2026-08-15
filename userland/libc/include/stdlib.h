#ifndef LEONOS_STDLIB_H
#define LEONOS_STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
int abs(int value);
int atoi(const char *text);
double atof(const char *text);
double strtod(const char *text, char **end);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
unsigned long long strtoull(const char *text, char **end, int base);
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));
char *strdup(const char *text);
char *getenv(const char *name);
int system(const char *command);
int rename(const char *old_path, const char *new_path);
void exit(int code) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));

#endif
