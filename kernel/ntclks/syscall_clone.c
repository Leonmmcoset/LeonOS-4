#include <ntclks/syscall.h>
#include <ntclks/sched.h>
#include <ntclks/arch.h>
#include <ntclks/usercopy.h>
#include <linux/sched.h>
#include <linux/errno.h>

/**
 * @brief Decode Linux 6.12 clone_args and delegate validated process creation.
 * @param frame Parent syscall frame; the scheduler supplies the child's zero return.
 * @param arguments User pointer to versioned clone_args with zero-only extensions.
 * @param size Bytes in the supplied structure, bounded by one page.
 * @return Child TID, Linux validation errno, or ENOSYS for missing backend features.
 */
int64_t syscall_clone3(const struct trap_frame *frame, uint64_t arguments, uint64_t size)
{
    if (size > 4096) return -LINUX_E2BIG;
    if (size < CLONE_ARGS_SIZE_VER0) return -LINUX_EINVAL;
    if (!user_range_ok(arguments, size)) return -LINUX_EFAULT;
    struct clone_args args = {0};
    uint64_t known_size = size < sizeof(args) ? size : sizeof(args);
    __builtin_memcpy(&args, (const void *)(uintptr_t)arguments, known_size);
    const unsigned char *bytes = (const unsigned char *)(uintptr_t)arguments;
    for (uint64_t i = sizeof(args); i < size; ++i)
        if (bytes[i]) return -LINUX_E2BIG;

    if (args.set_tid_size > 32 || (!args.set_tid && args.set_tid_size) ||
        (args.set_tid && !args.set_tid_size)) return -LINUX_EINVAL;
    if (args.exit_signal > 64) return -LINUX_EINVAL;
    if ((args.flags & CLONE_INTO_CGROUP) &&
        (args.cgroup > INT32_MAX || size < CLONE_ARGS_SIZE_VER2)) return -LINUX_EINVAL;
    if (args.set_tid && !user_range_ok(args.set_tid, args.set_tid_size * sizeof(uint32_t)))
        return -LINUX_EFAULT;
    if (args.flags & ~(0xffffffffULL | CLONE_CLEAR_SIGHAND | CLONE_INTO_CGROUP))
        return -LINUX_EINVAL;
    if (args.flags & (CLONE_DETACHED | (CSIGNAL & ~CLONE_NEWTIME))) return -LINUX_EINVAL;
    if ((args.flags & CLONE_SIGHAND) && (args.flags & CLONE_CLEAR_SIGHAND)) return -LINUX_EINVAL;
    if ((args.flags & (CLONE_THREAD | CLONE_PARENT)) && args.exit_signal) return -LINUX_EINVAL;
    if (!args.stack) {
        if (args.stack_size) return -LINUX_EINVAL;
    } else if (!args.stack_size || args.stack >= NTCLKS_USER_TLS_LIMIT ||
               args.stack_size > NTCLKS_USER_TLS_LIMIT - args.stack) return -LINUX_EINVAL;

    /* Do not fold CLONE_NEWTIME into clone's low-byte exit signal. */
    if (args.set_tid_size || (args.flags & (CLONE_NEWTIME | CLONE_INTO_CGROUP)))
        return -LINUX_ENOSYS;
    return sched_clone_current(frame, args.flags | args.exit_signal,
                               args.stack ? args.stack + args.stack_size : 0,
                               args.parent_tid, args.child_tid, args.tls);
}
