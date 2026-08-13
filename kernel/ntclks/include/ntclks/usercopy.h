/*
 * LeonOS user-copy interface: declares checked Ring-3 memory access helpers.
 * Safely transfers buffers and strings between user and kernel address spaces.
 */
#ifndef NTCLKS_USERCOPY_H
#define NTCLKS_USERCOPY_H

#include <ntclks/types.h>

/**
 * @brief Coordinates the user range ok operation.
 * @param ptr Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
bool user_range_ok(uint64_t ptr, uint64_t len);
/**
 * @brief Coordinates the user strlen operation.
 * @param s Input or output value used by this operation.
 * @param max Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
size_t user_strlen(const char *s, size_t max);

#endif
