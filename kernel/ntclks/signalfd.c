/* Linux 6.12 fs/signalfd.c: masks belong to the OFD, pending signals to the reader. */
#include <ntclks/syscall_internal.h>
#include <ntclks/futex.h>
#include <ntclks/usercopy.h>
#include <ntclks/heap.h>
#include <linux/signalfd.h>
#include <linux/socket.h>
#include <linux/poll.h>
#include <linux/errno.h>

/** @brief Check Linux access_ok without consuming signals or faulting pages. */
static bool signalfd_access(uint64_t address, uint64_t size)
{
    const uint64_t limit = (1ULL << 47) - 4096;
    return address <= limit && size <= limit - address;
}

/**
 * @brief Create a signal fd or replace the mask of an existing shared description.
 * @param fd Minus one for creation; otherwise an existing signalfd descriptor.
 * @param pointer User pointer to the native eight-byte mask.
 * @param size Native sigset width; libc's larger sigset_t is not accepted here.
 * @param flags SFD_NONBLOCK/CLOEXEC, effective only during creation.
 * @return Descriptor number, or a negative Linux errno.
 */
int64_t syscall_signalfd(int32_t fd, uint64_t pointer, uint64_t size, uint32_t flags)
{
    if (size != sizeof(uint64_t)) return -LINUX_EINVAL;
    if (!user_range_ok(pointer, size)) return -LINUX_EFAULT;
    uint64_t mask;
    __builtin_memcpy(&mask, (const void *)(uintptr_t)pointer, sizeof(mask));
    if (flags & ~(LINUX_SFD_NONBLOCK | LINUX_SFD_CLOEXEC)) return -LINUX_EINVAL;
    mask &= ~((1ULL << 8) | (1ULL << 18));
    struct task *task = sched_current_task();
    if (!task) return -LINUX_ESRCH;
    struct task_file *file;
    if (fd != -1) {
        file = task_file_for_fd(task, fd);
        if (!file) return -LINUX_EBADF;
        if (file->kind != TASK_FILE_KIND_SIGNALFD) return -LINUX_EINVAL;
        file->aux = mask;
        sched_signalfd_reconfigure(task);
        return fd;
    }
    fd = task_allocate_fd(task, 0, &file);
    if (fd < 0) return fd;
    file->kind = TASK_FILE_KIND_SIGNALFD;
    file->flags = LINUX_O_RDWR | (flags & LINUX_O_NONBLOCK);
    file->fd_flags = flags & LINUX_O_CLOEXEC ? LINUX_FD_CLOEXEC : 0;
    file->aux = mask;
    file->node = (struct storage_node){.type = LEONOS_FS_TYPE_DEVICE};
    return fd;
}

/** @brief Query this reader's private and process queues without consuming them. */
short task_signalfd_poll(struct task *task, const struct task_file *file)
{
    return (sched_task_pending(task) & file->aux) ? POLLIN : 0;
}

/** @brief Translate native siginfo_layout to the stable 128-byte signalfd record. */
static struct linux_signalfd_siginfo signalfd_info(const struct linux_siginfo *info)
{
    struct linux_signalfd_siginfo out = {.signo = info->signo, .error = info->error, .code = info->code};
    int sig = info->signo, code = info->code;
    if (code == LINUX_SI_TIMER) {
        out.tid = info->fields.timer.id;
        out.overrun = info->fields.timer.overrun;
        out.value_ptr = info->fields.timer.value;
        out.value_int = (int32_t)info->fields.timer.value;
    } else if (code == -5) { /* SI_SIGIO */
        out.band = (uint32_t)info->fields.poll.band;
        out.fd = info->fields.poll.fd;
    } else if (code < 0) {
        out.pid = info->fields.realtime.pid;
        out.uid = info->fields.realtime.uid;
        out.value_ptr = info->fields.realtime.value;
        out.value_int = (int32_t)info->fields.realtime.value;
    } else if (code > 0 && code < LINUX_SI_KERNEL) {
        int limit = sig == 4 ? 11 : sig == 8 ? 15 : sig == 11 ? 10 :
                    sig == 7 ? 5 : sig == 31 ? 2 : (sig == 5 || sig == 17 || sig == 29) ? 6 : 0;
        if (code <= limit && (sig == 4 || sig == 8 || sig == 11 || sig == 7 || sig == 5)) {
            out.address = info->fields.fault.address;
            if (sig == 7 && (code == 4 || code == 5)) out.address_lsb = info->fields.fault.address_lsb;
        } else if (sig == 17 && code <= limit) {
            out.pid = info->fields.child.pid;
            out.uid = info->fields.child.uid;
            out.status = info->fields.child.status;
            out.utime = info->fields.child.utime;
            out.stime = info->fields.child.stime;
        } else if (sig == 31 && code <= limit) {
            out.call_address = info->fields.sys.call_address;
            out.syscall = info->fields.sys.syscall;
            out.arch = info->fields.sys.arch;
        } else if (code <= 6) {
            out.band = (uint32_t)info->fields.poll.band;
            out.fd = info->fields.poll.fd;
        } else {
            out.pid = info->fields.sender.pid;
            out.uid = info->fields.sender.uid;
        }
    } else {
        out.pid = info->fields.sender.pid;
        out.uid = info->fields.sender.uid;
    }
    return out;
}

/** @brief Copy a record over the imported vectors, retaining Linux partial-copy side effects. */
static int signalfd_copy(struct task *task, const struct iovec *vectors, uint32_t count,
                         uint64_t position, const struct linux_signalfd_siginfo *info)
{
    uint64_t copied = 0;
    for (uint32_t i = 0; i < count && copied < sizeof(*info); ++i) {
        if (position >= vectors[i].iov_len) { position -= vectors[i].iov_len; continue; }
        uint64_t take = vectors[i].iov_len - position;
        if (take > sizeof(*info) - copied) take = sizeof(*info) - copied;
        if (user_copy_to_task(task, (uintptr_t)vectors[i].iov_base + position,
                              (const uint8_t *)info + copied, take) < 0) return -LINUX_EFAULT;
        copied += take;
        position = 0;
    }
    return copied == sizeof(*info) ? 0 : -LINUX_EFAULT;
}

/** @brief Dequeue complete records; only the first record may block. */
static int64_t signalfd_read_iter(struct task *task, struct task_file *file,
                                 const struct iovec *vectors, uint32_t count,
                                 uint64_t bytes, bool nowait)
{
    task->signalfd_waiting = false;
    task->signalfd_wait_mask = 0;
    if (bytes < sizeof(struct linux_signalfd_siginfo)) return -LINUX_EINVAL;
    uint64_t total = 0;
    while (bytes - total >= sizeof(struct linux_signalfd_siginfo)) {
        struct linux_siginfo info;
        int sig = kernel_signal_dequeue(task, file->aux, &info);
        if (!sig) {
            if (total) return total;
            if (nowait || (file->flags & LINUX_O_NONBLOCK)) return -LINUX_EAGAIN;
            task->signalfd_wait_mask = file->aux;
            task->signalfd_waiting = true;
            sched_signal_wait_current(0);
            return KERNEL_SYSCALL_BLOCKED;
        }
        struct linux_signalfd_siginfo output = signalfd_info(&info);
        int result = signalfd_copy(task, vectors, count, total, &output);
        if (result < 0) return total ? (int64_t)total : result;
        total += sizeof(output);
    }
    return total;
}

/** @brief Read records without pre-faulting output, as Linux vfs_read does. */
int64_t task_signalfd_read(struct task *task, struct task_file *file, uint64_t buffer, uint64_t count)
{
    if (!signalfd_access(buffer, count)) return -LINUX_EFAULT;
    if ((int64_t)count < 0) return -LINUX_EINVAL;
    if (count > 0x7ffff000ULL) count = 0x7ffff000ULL;
    struct iovec vector = {(void *)(uintptr_t)buffer, count};
    return signalfd_read_iter(task, file, &vector, 1, count, false);
}

/** @brief Import readv/preadv2 vectors as one byte stream, including split records. */
int64_t task_signalfd_readv(struct task *task, struct task_file *file, uint64_t pointer,
                           uint64_t count, uint32_t flags)
{
    count = (uint32_t)count; /* import_iovec takes an unsigned native nr_segs. */
    if (task->signalfd_vectors) {
        int64_t result = signalfd_read_iter(task, file, task->signalfd_vectors,
            task->signalfd_vector_count, task->signalfd_vector_bytes, task->signalfd_read_flags & 8u);
        if (result != KERNEL_SYSCALL_BLOCKED) {
            if (task->signalfd_vectors != task->signalfd_fast_vectors) kernel_free(task->signalfd_vectors);
            task->signalfd_vectors = NULL;
        }
        return result;
    }
    if (count > 1024) return -LINUX_EINVAL;
    struct iovec *vectors = task->signalfd_fast_vectors;
    uint64_t size = count * sizeof(*vectors), total = 0;
    if (count > 8) {
        vectors = kernel_malloc(size);
        if (!vectors) return -LINUX_ENOMEM;
    }
    int64_t result = -LINUX_EFAULT;
    if (size && !user_range_ok(pointer, size)) goto done;
    if (size) __builtin_memcpy(vectors, (const void *)(uintptr_t)pointer, size);
    result = -LINUX_EINVAL;
    for (uint64_t i = 0; i < count; ++i) if (vectors[i].iov_len > INT64_MAX) goto done;
    /* Linux imports a single vector as ITER_UBUF, clamping before access_ok. */
    if (count == 1 && vectors[0].iov_len > 0x7ffff000ULL) vectors[0].iov_len = 0x7ffff000ULL;
    result = -LINUX_EFAULT;
    for (uint64_t i = 0; i < count; ++i) {
        if (!signalfd_access((uintptr_t)vectors[i].iov_base, vectors[i].iov_len)) goto done;
        if (vectors[i].iov_len > 0x7ffff000ULL - total) vectors[i].iov_len = 0x7ffff000ULL - total;
        total += vectors[i].iov_len;
    }
    result = 0;
    if (!total) goto done;
    result = -LINUX_EOPNOTSUPP;
    if (flags & ~0x3fu) goto done; /* RWF_ATOMIC is not supported for reads. */
    result = -LINUX_EINVAL;
    if ((flags & 0x30u) == 0x30u) goto done; /* APPEND and NOAPPEND conflict. */
    result = signalfd_read_iter(task, file, vectors, (uint32_t)count, total, flags & 8u);
    if (result == KERNEL_SYSCALL_BLOCKED) {
        task->signalfd_vectors = vectors;
        task->signalfd_vector_count = (uint32_t)count;
        task->signalfd_vector_bytes = total;
        task->signalfd_read_flags = flags;
        return result;
    }
done:
    if (vectors != task->signalfd_fast_vectors) kernel_free(vectors);
    return result;
}
