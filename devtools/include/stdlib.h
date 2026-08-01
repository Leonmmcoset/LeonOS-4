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
char *strdup(const char *text);
char *getenv(const char *name);
int system(const char *command);
int rename(const char *old_path, const char *new_path);
void exit(int code) __attribute__((noreturn));

#endif
