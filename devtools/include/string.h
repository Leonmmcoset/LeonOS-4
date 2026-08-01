#ifndef LEONOS_STRING_H
#define LEONOS_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t len);
void *memmove(void *dst, const void *src, size_t len);
void *memset(void *dst, int value, size_t len);
int memcmp(const void *left, const void *right, size_t len);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t len);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t len);
char *strchr(const char *text, int value);
char *strstr(const char *text, const char *needle);
char *strrchr(const char *text, int value);
int strcasecmp(const char *left, const char *right);
int strncasecmp(const char *left, const char *right, size_t len);

#endif
