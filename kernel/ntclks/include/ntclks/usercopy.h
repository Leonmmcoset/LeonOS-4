/*
 * LeonOS user-copy interface: declares checked Ring-3 memory access helpers.
 * Safely transfers buffers and strings between user and kernel address spaces.
 */
#ifndef NTCLKS_USERCOPY_H
#define NTCLKS_USERCOPY_H

#include <ntclks/types.h>

/**
 * @brief Return true when the user range [ptr, ptr+len) is mapped and accessible.
 */
bool user_range_ok(uint64_t ptr, uint64_t len);
/**
 * @brief Prepare writable user pages for copy-out, faulting in and resolving COW.
 * @param ptr Start of the current user's output range.
 * @param len Range length in bytes; zero succeeds for a current user task.
 * @return False for unmapped/read-only memory, overflow, or failed COW allocation.
 */
bool user_range_writable(uint64_t ptr, uint64_t len);
/**
 * @brief Return the length of the NUL-terminated user string s, up to max bytes.
 */
size_t user_strlen(const char *s, size_t max);

#endif
