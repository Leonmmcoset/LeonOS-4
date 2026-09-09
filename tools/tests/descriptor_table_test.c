#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
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

int main(void)
{
    struct task *task = calloc(1, sizeof(*task));
    assert(task);
    sched_task_limits(task)->nofile.rlim_cur = 1024;
    struct task_file *slot;
    assert(task_allocate_fd(task, 0, &slot) == 3);
    slot->offset = 123;
    assert(task_duplicate_file_fd(task, 3, 100, 1) == 100);
    assert(task_dup2_fd(task, 100, 700) == 700);
    assert(task_file_for_fd(task, 100) == task_file_for_fd(task, 700));
    assert(task_file_for_fd(task, 700)->offset == 123);
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
    sched_task_limits(task)->nofile.rlim_cur = 4;
    assert(task_allocate_fd(task, 0, &slot) == -LEONOS_EMFILE);
    assert(task_dup2_fd(task, 900, 900) == 900);
    task_discard_file_fd(task, 0);
    assert(task_allocate_fd(task, 0, &slot) == 0);
    task_discard_file_fd(task, 0);
    assert(task_allocate_fd(task, 0, &slot) == 0);
    task_discard_file_fd(task, 0);
    for (int fd = 3; fd < 1024; ++fd) task_discard_file_fd(task, fd);
    sched_task_file_release(task);
    free(task);
    puts("PASS actual fd table: growth, shared OFD, failure rollback, stdio reuse, limit lowering");
}
