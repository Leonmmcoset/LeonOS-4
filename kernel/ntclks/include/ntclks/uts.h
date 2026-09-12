#ifndef NTCLKS_UTS_H
#define NTCLKS_UTS_H
#include <stdint.h>
/**
 * @brief Copy a coherent snapshot under the UTS spinlock.
 * @param hostname Writable 65-byte kernel buffer for the hostname.
 * @param domainname Writable 65-byte kernel buffer for the NIS domain.
 */
void linux_uts_names(char hostname[65], char domainname[65]);
/** @brief Set a captured kernel name (0..64 bytes); domain selects NIS domain.
 * @param name Captured kernel bytes, nullable only when length is zero.
 * @param length Byte count, at most 64; embedded NUL is preserved like Linux.
 * @param domain Nonzero selects the NIS domain, zero selects the hostname.
 * @return Zero or negative EINVAL. Caller performs capability/user-pointer checks.
 */
int linux_uts_set(const char *name, uint32_t length, int domain);
/** @brief Load /etc/hostname before userspace starts; absent config uses leonos.
 * @return Zero or a real I/O/configuration error.
 */
int linux_uts_load_hostname(void);
#endif
