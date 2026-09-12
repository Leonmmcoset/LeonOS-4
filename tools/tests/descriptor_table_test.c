#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../kernel/ntclks/syscall.c"

static int fail_allocation;
static unsigned pty_references;
void *kernel_malloc(size_t size) { return fail_allocation ? NULL : malloc(size); }
void kernel_free(void *memory) { free(memory); }
/* This descriptor test uses anonymous objects, never storage-backed files. */
int storage_inode_put(struct storage_inode_ref *inode) { assert(!inode); return 0; }
void task_pipe_release(struct task_file *file) { (void)file; }
void task_socket_release(struct task_file *file) { (void)file; }
void task_inet_release(struct task_file *file) { (void)file; }
void task_shm_release(struct task_file *file) { (void)file; }
void task_socket_collect(void) {}
void input_evdev_release(uint32_t kind, uint64_t token, uint32_t pid)
{ (void)kind; (void)token; (void)pid; }
uint32_t smp_current_cpu(void) { return 0; }
void pty_reap_hungup(uint32_t id) { (void)id; }
int pty_is_active(uint32_t id) { return id == 7; }
int pty_slave_open_allowed(uint32_t id) { return id == 7; }
int pty_transfer_get(uint32_t id, uint32_t endpoint)
{ assert(id == 7 && endpoint == TASK_PTY_ENDPOINT_SLAVE); ++pty_references; return 0; }
void pty_transfer_put(uint32_t id, uint32_t endpoint)
{ assert(id == 7 && endpoint == TASK_PTY_ENDPOINT_SLAVE && pty_references); --pty_references; }
void kernel_spin_init(struct kernel_spinlock *lock) { lock->state = 0; }
void kernel_spin_lock_irqsave(struct kernel_spinlock *lock, uint64_t *flags)
{ assert(!lock->state); lock->state = 1; *flags = 0; }
void kernel_spin_unlock_irqrestore(struct kernel_spinlock *lock, uint64_t flags)
{ (void)flags; assert(lock->state == 1); lock->state = 0; }

static void test_flock_promotion(void)
{
    for (unsigned type = LEONOS_FLOCK_SH; type <= LEONOS_FLOCK_EX; ++type) {
        struct task_file source = {.used = 1, .flock_type = type};
        struct task_file duplicate = {0};
        struct leonos_flock_entry *entry = &leonos_flocks[0];
        *entry = (struct leonos_flock_entry){.used = 1, .type = type};
        kernel_wait_queue_init(&entry->waiters);
        if (type == LEONOS_FLOCK_EX) entry->owner = &source;
        else { entry->shared_count = 1; entry->shared_owners[0] = &source; }
        fail_allocation = 1;
        assert(task_file_reference(&duplicate, &source) == -LEONOS_ENOMEM);
        assert(!source.description && !duplicate.used);
        assert((type == LEONOS_FLOCK_EX ? entry->owner : entry->shared_owners[0]) == &source);
        fail_allocation = 0;
        assert(task_file_reference(&duplicate, &source) == 0);
        assert((type == LEONOS_FLOCK_EX ? entry->owner : entry->shared_owners[0]) == source.description);
        clear_task_file(&source);
        assert(entry->used);
        clear_task_file(&duplicate);
        assert(!entry->used);
    }
    puts("PASS flock owner promotion: exclusive/shared, ENOMEM rollback and final close");
}

static void test_pty_descriptions(void)
{
    struct task *parent = calloc(1, sizeof(*parent)), *child = calloc(1, sizeof(*child));
    assert(parent && child);
    parent->limits.nofile.rlim_cur = child->limits.nofile.rlim_cur = 1024;
    fail_allocation = 1;
    assert(task_pty_endpoint_fd(parent, 7, TASK_PTY_ENDPOINT_SLAVE, LEONOS_O_RDWR) == -LEONOS_ENOMEM);
    assert(!pty_references && !task_pty_fd_for_fd(parent, 3));
    fail_allocation = 0;
    int fd = task_pty_endpoint_fd(parent, 7, TASK_PTY_ENDPOINT_SLAVE, LEONOS_O_RDWR | LEONOS_O_CLOEXEC);
    assert(fd == 3 && pty_references == 1);
    struct task_pty_fd *source = task_pty_fd_for_fd(parent, fd);
    int duplicate = task_pty_duplicate_fd(parent, fd, 0, 0);
    assert(duplicate == 4 && source->description->references == 2 && pty_references == 1);
    source->description->status_flags |= LEONOS_O_NONBLOCK;
    assert(task_pty_status(task_pty_fd_for_fd(parent, duplicate)) & LEONOS_O_NONBLOCK);
    assert(task_fd_descriptor_flags(parent, fd) == LEONOS_FD_CLOEXEC &&
           task_fd_descriptor_flags(parent, duplicate) == 0);
    child->fd_table = parent->fd_table;
    assert(syscall_clone_task_files(parent, child) == 0);
    assert(source->description->references == 4);
    struct task_pty_fd queued;
    assert(task_pty_export_fd(parent, fd, &queued) == 0 && source->description->references == 5);
    task_pty_release_entry(source);
    task_pty_release_entry(task_pty_fd_for_fd(parent, duplicate));
    task_pty_release_entry(task_pty_fd_for_fd(child, fd));
    task_pty_release_entry(task_pty_fd_for_fd(child, duplicate));
    assert(queued.description->references == 1 && pty_references == 1);
    fd = task_pty_import_fd(parent, &queued, LEONOS_FD_CLOEXEC);
    assert(fd == 3 && queued.description->references == 2);
    task_pty_release_entry(&queued);
    source = task_pty_fd_for_fd(parent, fd);
    parent->syscall_pty = *source;
    ++source->description->references;
    parent->syscall_fd = fd;
    assert(task_pty_dup2_fd(parent, fd, 1) == 1 && parent->pty_id == 0);
    task_pty_release_entry(source);
    task_pty_release_entry(task_pty_fd_for_fd(parent, 1));
    assert(parent->syscall_pty.description->references == 1 && pty_references == 1);
    assert(task_pty_for_io(parent, fd) == &parent->syscall_pty);
    task_release_syscall_file(parent);
    assert(!pty_references && !parent->syscall_pty.used);
    fd = task_pty_endpoint_fd(parent, 7, TASK_PTY_ENDPOINT_SLAVE, LEONOS_O_RDWR);
    assert(fd >= 0);
    struct task_fd_table_state *shared = kernel_malloc(sizeof(*shared));
    assert(shared);
    clear_task_files(child);
    *shared = parent->fd_table;
    shared->references = 2;
    parent->shared_files = child->shared_files = shared;
    fail_allocation = 1;
    assert(syscall_unshare_task_files(parent) == -LEONOS_ENOMEM);
    assert(parent->shared_files == shared && shared->references == 2);
    fail_allocation = 0;
    assert(syscall_unshare_task_files(parent) == 0);
    source = task_pty_fd_for_fd(parent, fd);
    assert(parent->shared_files != shared && shared->references == 1);
    assert(source != task_pty_fd_for_fd(child, fd) && source->description->references == 2);
    source->description->status_flags |= LEONOS_O_APPEND;
    assert(task_pty_status(task_pty_fd_for_fd(child, fd)) & LEONOS_O_APPEND);
    task_pty_release_entry(source);
    task_pty_release_entry(task_pty_fd_for_fd(child, fd));
    assert(!pty_references);
    clear_task_files(parent);
    clear_task_files(child);
    kernel_free(parent->shared_files);
    kernel_free(shared);
    free(parent);
    free(child);
}

static void test_console_descriptions(void)
{
    struct task *parent = calloc(1, sizeof(*parent)), *child = calloc(1, sizeof(*child));
    assert(parent && child);
    parent->limits.nofile.rlim_cur = child->limits.nofile.rlim_cur = 64;
    assert(task_pty_materialize_stdio(parent) == 0);
    assert(task_file_for_fd(parent, 0) && task_file_for_fd(parent, 1) && task_file_for_fd(parent, 2));
    assert(task_device_is(task_file_for_fd(parent, 0), STORAGE_DEV_KIND_NULL));
    assert(task_device_is(task_file_for_fd(parent, 1), STORAGE_DEV_KIND_KMSG));
    assert(task_fd_set_descriptor_flags(parent, 2, LEONOS_FD_CLOEXEC) == 0);
    child->fd_table = parent->fd_table;
    assert(syscall_clone_task_files(parent, child) == 0);
    assert(task_file_for_fd(parent, 1) == task_file_for_fd(child, 1));
    int copy = task_duplicate_file_fd(child, 1, 0, 0);
    assert(copy == 3);
    task_file_for_fd(child, copy)->flags |= LEONOS_O_NONBLOCK;
    assert(task_file_for_fd(parent, 1)->flags & LEONOS_O_NONBLOCK);
    assert(task_fd_descriptor_flags(child, 2) == LEONOS_FD_CLOEXEC);
    task_discard_file_fd(child, 1);
    assert(task_pty_materialize_stdio(child) == 0 && !task_file_for_fd(child, 1));
    assert(task_file_for_fd(parent, 1));
    clear_task_files(child);
    clear_task_files(parent);
    free(child);
    free(parent);
}

int main(void)
{
    test_flock_promotion();
    test_pty_descriptions();
    test_console_descriptions();
    struct task *task = calloc(1, sizeof(*task));
    assert(task);
    sched_task_limits(task)->nofile.rlim_cur = 1024;
    struct task_file *slot;
    assert(task_allocate_fd(task, 0, &slot) == 3);
    slot->offset = 123;
    slot->kind = TASK_FILE_KIND_SIGNALFD;
    slot->aux = 1ULL << 35;
    assert(task_duplicate_file_fd(task, 3, 100, 1) == 100);
    assert(task_dup2_fd(task, 100, 700) == 700);
    assert(task_file_for_fd(task, 100) == task_file_for_fd(task, 700));
    assert(task_file_for_fd(task, 700)->offset == 123);
    assert(task_file_for_fd(task, 700)->kind == TASK_FILE_KIND_SIGNALFD);
    task_file_for_fd(task, 100)->aux = 1ULL << 34;
    assert(task_file_for_fd(task, 700)->aux == (1ULL << 34));
    assert(task_descriptor_for_fd(task, 100)->fd_flags == 1);
    assert(task_descriptor_for_fd(task, 700)->fd_flags == 0);
    assert(task_file_for_fd(task, 3)->references == 3);
    /* Reject failed expansion without consuming an OFD reference or fd. */
    fail_allocation = 1;
    assert(task_dup2_fd(task, 100, 900) == -LEONOS_ENOMEM);
    assert(task_file_for_fd(task, 900) == NULL);
    assert(task_file_for_fd(task, 3)->references == 3);
    fail_allocation = 0;
    assert(task_dup2_fd(task, 100, 900) == 900);
    assert(task_dup2_fd(task, 999, 900) == -LEONOS_EBADF);
    assert(task_file_for_fd(task, 3)->references == 4);
    struct task waiter = {.pid = 777, .state = TASK_BLOCKED};
    leonos_flocks[0] = (struct leonos_flock_entry){.used = 1,
        .type = LEONOS_FLOCK_EX, .owner = task_file_for_fd(task, 3)};
    kernel_wait_queue_init(&leonos_flocks[0].waiters);
    assert(kernel_wait_queue_add(&leonos_flocks[0].waiters, &waiter) == 0);
    sched_task_limits(task)->nofile.rlim_cur = 4;
    assert(task_allocate_fd(task, 0, &slot) == -LEONOS_EMFILE);
    assert(task_dup2_fd(task, 900, 900) == 900);
    task_discard_file_fd(task, 0);
    assert(task_allocate_fd(task, 0, &slot) == 0);
    task_discard_file_fd(task, 0);
    assert(task_allocate_fd(task, 0, &slot) == 0);
    task_discard_file_fd(task, 0);
    for (int fd = 3; fd < 1024; ++fd) {
        task_discard_file_fd(task, fd);
        if (fd < 900) assert(leonos_flocks[0].used && waiter.waiting_queue);
    }
    assert(!leonos_flocks[0].used && !waiter.waiting_queue && !leonos_flocks[0].waiters.count);
    assert(task_allocate_fd(task, 0, &slot) == 0 && !slot->kind);
    task_discard_file_fd(task, 0);
    task->signalfd_vectors = kernel_malloc(9 * sizeof(struct iovec));
    assert(task->signalfd_vectors);
    task->signalfd_waiting = true;
    task->signalfd_wait_mask = UINT64_MAX;
    task_release_syscall_file(task);
    assert(!task->signalfd_vectors && !task->signalfd_waiting && !task->signalfd_wait_mask);
    sched_task_file_release(task);
    free(task);
    puts("PASS actual fd table: growth, shared OFD, failure rollback, stdio reuse, limit lowering");
}
