/*
 * LeonOS user-copy interface: declares checked Ring-3 memory access helpers.
 * Safely transfers buffers and strings between user and kernel address spaces.
 */
#ifndef NTCLKS_USERCOPY_H
#define NTCLKS_USERCOPY_H

#include <ntclks/types.h>
struct task;
/**
 * @brief Copy bytes into a pinned task's address space, including across COW pages.
 * @param task Destination task whose mappings are stable under the execution lock.
 * @param address Destination user virtual address.
 * @param source Kernel source bytes.
 * @param size Byte count to copy.
 * @return Zero on success or -EFAULT, with earlier pages possibly already copied.
 */
int user_copy_to_task(struct task *task, uint64_t address, const void *source, uint64_t size);

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
