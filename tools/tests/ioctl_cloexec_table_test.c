/*
 * Host unit test for the Linux ioctl close-on-exec descriptor flags.
 *
 * The test compiles and links the real kernel translation unit
 * (kernel/ntclks/syscall.c) so it exercises the actual descriptor helpers,
 * descriptor tables and error paths rather than a re-implementation.  It is
 * the host-side half of the ioctl(FIOCLEX/FIONCLEX) regression; the guest half
 * is tools/tests/linux_ioctl_cloexec_test.c, which runs the same Linux ABI on
 * the real LeonOS kernel and on the host Linux kernel as the reference.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/ntclks/syscall.c"

static int fail_allocation;
void *kernel_malloc(size_t size) { return fail_allocation ? NULL : malloc(size); }
void kernel_free(void *memory) { free(memory); }
void task_pipe_release(struct task_file *file) { (void)file; }
void task_socket_release(struct task_file *file) { (void)file; }
void task_inet_release(struct task_file *file) { (void)file; }
void task_shm_release(struct task_file *file) { (void)file; }
void task_socket_collect(void) {}
void input_evdev_release(uint32_t kind, uint64_t token, uint32_t pid)
{ (void)kind; (void)token; (void)pid; }
uint32_t smp_current_cpu(void) { return 0; }
void pty_reap_hungup(uint32_t id) { (void)id; }
void kernel_spin_init(struct kernel_spinlock *lock) { lock->state = 0; }
void kernel_spin_lock_irqsave(struct kernel_spinlock *lock, uint64_t *flags)
{ assert(!lock->state); lock->state = 1; *flags = 0; }
void kernel_spin_unlock_irqrestore(struct kernel_spinlock *lock, uint64_t flags)
{ (void)flags; assert(lock->state == 1); lock->state = 0; }

/* Linux v6.12 native x86-64 UAPI values the kernel must use verbatim. */
static_assert(FIOCLEX == 0x5451UL, "FIOCLEX must stay the Linux x86-64 value");
static_assert(FIONCLEX == 0x5450UL, "FIONCLEX must stay the Linux x86-64 value");
static_assert(LEONOS_FD_CLOEXEC == 1U, "FD_CLOEXEC must stay the Linux value");

/** @brief Allocate one file descriptor slot the way open() does. */
static int open_slot(struct task *task, uint32_t node_flags)
{
    struct task_file *slot;
    int fd = task_allocate_fd(task, 0, &slot);
    assert(fd >= 0);
    slot->node.type = LEONOS_FS_TYPE_FILE;
    slot->flags = node_flags;
    return fd;
}

int main(int argc, char **argv)
{
    struct task *task = calloc(1, sizeof(*task));
    assert(task);
    sched_task_limits(task)->nofile.rlim_cur = 1024;

    if (argc == 2 && strcmp(argv[1], "--invalid-fd") == 0) {
        assert(syscall_ioctl_resolve_fd(task, UINT32_MAX) == -LEONOS_EBADF);
        assert(syscall_ioctl_resolve_fd(task, INT32_MAX) == -LEONOS_EBADF);
        sched_task_fds(task)->closed_stdio_mask |= 1u;
        assert(syscall_ioctl_resolve_fd(task, 0) == -LEONOS_EBADF);
        free(task);
        puts("PASS ioctl rejects invalid descriptors before request dispatch");
        return 0;
    }

    /* epoll_create passes internal flags through this same allocator. */
    struct storage_node epoll_node = {.type = LEONOS_FS_TYPE_DEVICE};
    int epfd = alloc_task_fd(task, &epoll_node, TASK_FILE_FLAG_EPOLL | LEONOS_O_RDWR, NULL);
    assert(epfd >= 0);
    struct task_file *epfile = task_file_for_fd(task, epfd);
    struct task_epoll *epoll = kernel_malloc(sizeof(*epoll));
    assert(epoll);
    *epoll = (struct task_epoll){0};
    epfile->aux = (uint64_t)(uintptr_t)epoll;
    assert(task_epoll_for_fd(task, epfd) == epoll);
    assert(syscall_ioctl_resolve_fd(task, epfd) == 0);
    task_discard_file_fd(task, epfd);
    /* LeakSanitizer also checks that closing the descriptor frees epoll. */

    /* --- FIOCLEX sets and FIONCLEX clears one descriptor. ---------------- */
    int fd = open_slot(task, LEONOS_O_RDWR);
    assert(task_fd_descriptor_flags(task, fd) == 0);
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)fd, FIOCLEX) == 0);
    assert(task_fd_descriptor_flags(task, fd) == LEONOS_FD_CLOEXEC);
    assert(task_descriptor_for_fd(task, fd)->fd_flags == LEONOS_FD_CLOEXEC);
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)fd, FIOCLEX) == 0);
    assert(task_fd_descriptor_flags(task, fd) == LEONOS_FD_CLOEXEC);
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)fd, FIONCLEX) == 0);
    assert(task_fd_descriptor_flags(task, fd) == 0);
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)fd, FIONCLEX) == 0);

    /* The request is 32 bit: an fd with garbage in the upper half still names
     * the descriptor, and unknown requests must not be reported as success. */
    assert(syscall_ioctl_descriptor_flags(task, 0x100000000ULL | (uint32_t)fd,
                                          FIOCLEX) == 0);
    assert(task_fd_descriptor_flags(task, fd) == LEONOS_FD_CLOEXEC);
    /* The dispatcher truncates the request to 32 bits before dispatch; this
     * helper therefore only ever sees the native value. */
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)fd, (uint32_t)FIONCLEX) == 0);
    assert(task_fd_descriptor_flags(task, fd) == 0);
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)fd, TCGETS) == -LEONOS_ENOTTY);
    assert(task_fd_descriptor_flags(task, fd) == 0);

    /* --- Closed, negative and out-of-range descriptors report EBADF. ----- */
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)-1, FIOCLEX) == -LEONOS_EBADF);
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)-1, FIONCLEX) == -LEONOS_EBADF);
    assert(syscall_ioctl_descriptor_flags(task, 0x7fffffffULL, FIOCLEX) == -LEONOS_EBADF);
    assert(task_fd_descriptor_flags(task, (uint32_t)-1) == -LEONOS_EBADF);
    assert(task_fd_set_descriptor_flags(task, (uint32_t)-1, LEONOS_FD_CLOEXEC) == -LEONOS_EBADF);

    /* --- The flag belongs to the descriptor, not the shared description. - */
    int duplicate = task_duplicate_file_fd(task, fd, 0, 0);
    assert(duplicate >= 0);
    assert(task_file_for_fd(task, fd) == task_file_for_fd(task, duplicate));
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)fd, FIOCLEX) == 0);
    assert(task_fd_descriptor_flags(task, fd) == LEONOS_FD_CLOEXEC);
    assert(task_fd_descriptor_flags(task, duplicate) == 0);
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)duplicate, FIOCLEX) == 0);
    assert(syscall_ioctl_descriptor_flags(task, (uint32_t)fd, FIONCLEX) == 0);
    assert(task_fd_descriptor_flags(task, fd) == 0);
    assert(task_fd_descriptor_flags(task, duplicate) == LEONOS_FD_CLOEXEC);
    /* F_DUPFD_CLOEXEC and dup2 keep their Linux semantics on the same table. */
    int cloexec_dup = task_duplicate_file_fd(task, duplicate, 0, LEONOS_FD_CLOEXEC);
    assert(cloexec_dup >= 0 && task_fd_descriptor_flags(task, cloexec_dup) == LEONOS_FD_CLOEXEC);
    assert(task_dup2_fd(task, duplicate, cloexec_dup) == cloexec_dup);
    assert(task_fd_descriptor_flags(task, cloexec_dup) == 0);
    assert(task_fd_descriptor_flags(task, duplicate) == LEONOS_FD_CLOEXEC);

    /* --- Explicit PTY aliases keep their own descriptor flag word. ------- */
    struct task_pty_fd *alias = &sched_task_fds(task)->pty_fds[0];
    *alias = (struct task_pty_fd){.used = 1, .fd = 42, .stream = 0,
                                  .status_flags = LEONOS_O_RDONLY};
    assert(task_fd_descriptor_flags(task, 42) == 0);
    assert(syscall_ioctl_descriptor_flags(task, 42, FIOCLEX) == 0);
    assert(alias->flags == LEONOS_FD_CLOEXEC);
    assert(task_fd_descriptor_flags(task, 42) == LEONOS_FD_CLOEXEC);
    assert(syscall_ioctl_descriptor_flags(task, 42, FIONCLEX) == 0);
    assert(alias->flags == 0);
    *alias = (struct task_pty_fd){0};

    /* --- Implicit stdin/stdout/stderr descriptors really store the flag. - */
    for (int stdio = 0; stdio < 3; ++stdio) {
        assert(task_fd_descriptor_flags(task, stdio) == 0);
        assert(syscall_ioctl_descriptor_flags(task, (uint32_t)stdio, FIOCLEX) == 0);
        assert(sched_task_fds(task)->cloexec_stdio_mask & (1u << stdio));
        assert(task_fd_descriptor_flags(task, stdio) == LEONOS_FD_CLOEXEC);
        assert(task_fd_set_descriptor_flags(task, stdio, 0) == 0);
        assert(!(sched_task_fds(task)->cloexec_stdio_mask & (1u << stdio)));
        assert(task_fd_descriptor_flags(task, stdio) == 0);
    }
    /* A closed implicit slot is EBADF and must reject the flag write. */
    sched_task_fds(task)->closed_stdio_mask |= 1u << 2;
    assert(syscall_ioctl_descriptor_flags(task, 2, FIOCLEX) == -LEONOS_EBADF);
    assert(task_fd_descriptor_flags(task, 2) == -LEONOS_EBADF);
    sched_task_fds(task)->closed_stdio_mask &= ~(1u << 2);
    assert(syscall_ioctl_descriptor_flags(task, 2, FIOCLEX) == 0);
    /* Allocating a new descriptor into that slot clears the stale flag. */
    assert(task_fd_set_descriptor_flags(task, 2, LEONOS_FD_CLOEXEC) == 0);
    task_discard_file_fd(task, 2);
    assert(!(sched_task_fds(task)->cloexec_stdio_mask & (1u << 2)));
    struct task_file *reused;
    assert(task_allocate_fd(task, 0, &reused) == 2);
    assert(task_fd_descriptor_flags(task, 2) == 0);
    task_discard_file_fd(task, 2);

    /* dup2() onto an implicit stdio slot clears FD_CLOEXEC like Linux. */
    assert(task_fd_set_descriptor_flags(task, 1, LEONOS_FD_CLOEXEC) == 0);
    assert(task_dup2_fd(task, fd, 1) == 1);
    assert(task_fd_descriptor_flags(task, 1) == 0);
    assert(!(sched_task_fds(task)->cloexec_stdio_mask & (1u << 1)));
    task_discard_file_fd(task, 1);

    /* --- O_PATH descriptors are rejected exactly like Linux fdget(). ----- */
    /* The allocator receives internal flags. The raw-syscall probe checks
     * translation of O_PATH at the open() boundary. */
    assert((LINUX_O_PATH & TASK_FILE_FLAG_EPOLL) != 0);
    assert((TASK_FILE_FLAG_PATH & TASK_FILE_FLAG_EPOLL) == 0);
    struct storage_node path_node = {.type = LEONOS_FS_TYPE_FILE};
    int path_fd = alloc_task_fd(task, &path_node, TASK_FILE_FLAG_PATH | LEONOS_O_CLOEXEC,
                                "/tmp/opath");
    assert(path_fd >= 0);
    struct task_file *path_file = task_file_for_fd(task, path_fd);
    assert(path_file && (path_file->flags & TASK_FILE_FLAG_PATH));
    assert(!(path_file->flags & TASK_FILE_FLAG_EPOLL));
    assert(path_file->flags & LEONOS_O_CLOEXEC);
    assert(task_descriptor_for_fd(task, path_fd)->fd_flags == LEONOS_FD_CLOEXEC);
    assert(syscall_ioctl_resolve_fd(task, (uint32_t)path_fd) == -LEONOS_EBADF);
    assert(syscall_ioctl_resolve_fd(task, (uint32_t)fd) == 0);
    assert(syscall_ioctl_resolve_fd(task, (uint32_t)-1) == -LEONOS_EBADF);
    /* fcntl() resolves the same descriptor with fdget_raw() semantics. */
    assert(task_fd_set_descriptor_flags(task, path_fd, 0) == 0);
    assert(task_fd_descriptor_flags(task, path_fd) == 0);
    assert(task_fd_set_descriptor_flags(task, path_fd, LEONOS_FD_CLOEXEC) == 0);
    assert(task_fd_descriptor_flags(task, path_fd) == LEONOS_FD_CLOEXEC);

    /* --- O_PATH flags survive descriptor duplication as FMODE_PATH does. - */
    int path_dup = task_duplicate_file_fd(task, path_fd, 0, 0);
    assert(path_dup >= 0);
    assert(task_file_for_fd(task, path_dup)->flags & TASK_FILE_FLAG_PATH);
    assert(syscall_ioctl_resolve_fd(task, (uint32_t)path_dup) == -LEONOS_EBADF);

    /* Release every descriptor so the promoted open file descriptions are
     * freed and the sanitizer build stays leak-clean. */
    for (int index = 0; index < 1024; ++index) task_discard_file_fd(task, index);
    sched_task_file_release(task);
    free(task);
    puts("PASS kernel fd-flags table: FIOCLEX/FIONCLEX, dup isolation, implicit stdio, "
         "fd reuse, O_PATH EBADF, cmd/fd truncation");
    return 0;
}
