/* Shared UTS state for uname, hostname syscalls, and procfs. */
#include <ntclks/uts.h>
#include <ntclks/lock.h>
#include <ntclks/storage.h>
#include <leonos/fs.h>

static struct kernel_spinlock uts_lock = KERNEL_SPINLOCK_INIT;
static char uts_hostname[65] = "leonos";
static char uts_domainname[65] = "(none)";

/** @brief Snapshot both names under a short lock.
 * @param hostname Writable 65-byte kernel buffer.
 * @param domainname Writable 65-byte kernel buffer.
 */
void linux_uts_names(char hostname[65], char domainname[65])
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&uts_lock, &flags);
    __builtin_memcpy(hostname, uts_hostname, 65);
    __builtin_memcpy(domainname, uts_domainname, 65);
    kernel_spin_unlock_irqrestore(&uts_lock, flags);
}

/** @brief Atomically replace a captured kernel name without touching user memory under the lock.
 * @param name Kernel input bytes, nullable when length is zero.
 * @param length Length in bytes, zero through 64.
 * @param domain Nonzero selects NIS domain, otherwise hostname.
 * @return Zero or negative EINVAL; rejected input leaves the previous name intact.
 */
int linux_uts_set(const char *name, uint32_t length, int domain)
{
    char value[65] = {0};
    uint64_t flags;
    if (length > 64 || (!name && length)) return -22;
    if (length) __builtin_memcpy(value, name, length);
    kernel_spin_lock_irqsave(&uts_lock, &flags);
    __builtin_memcpy(domain ? uts_domainname : uts_hostname, value, sizeof(value));
    kernel_spin_unlock_irqrestore(&uts_lock, flags);
    return 0;
}

/** @brief Load a bounded single-line hostname before normal or installer processes start.
 * @return Zero (including absent configuration), EINVAL for invalid content, or storage errno.
 */
int linux_uts_load_hostname(void)
{
    struct storage_node node;
    char value[66];
    uint32_t length = 0;
    int ret = storage_lookup_path("/etc/hostname", &node);
    if (ret == -2) return 0;
    if (ret < 0) return ret;
    if (node.type != LEONOS_FS_TYPE_FILE || node.size > sizeof(value)) return -22;
    ret = storage_read_node(&node, 0, value, sizeof(value), &length);
    if (ret < 0) return ret;
    if (length != node.size) return -5;
    while (length && (value[length - 1] == '\n' || value[length - 1] == '\r')) --length;
    if (!length || length > 64) return -22;
    for (uint32_t i = 0; i < length; ++i)
        if ((unsigned char)value[i] <= ' ' || (unsigned char)value[i] >= 127) return -22;
    return linux_uts_set(value, length, 0);
}
