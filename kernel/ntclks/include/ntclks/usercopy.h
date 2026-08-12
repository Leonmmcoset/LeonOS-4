/*
 * LeonOS user-copy interface: declares checked Ring-3 memory access helpers.
 * Safely transfers buffers and strings between user and kernel address spaces.
 */
#ifndef NTCLKS_USERCOPY_H
#define NTCLKS_USERCOPY_H

#include <ntclks/types.h>

bool user_range_ok(uint64_t ptr, uint64_t len);
size_t user_strlen(const char *s, size_t max);

#endif
