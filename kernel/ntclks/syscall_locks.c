/* Linux v6.12 fs/locks.c: POSIX byte-range locks owned by files_struct. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>
#include <ntclks/heap.h>
#include <ntclks/futex.h>
#include <ntclks/usercopy.h>
#include <ntclks/wait.h>
#include <linux/errno.h>
#include <linux/fcntl.h>

#define RECORD_LOCK_MAX 256u
#define LOCK_END 0x7fffffffffffffffLL
struct record_lock {
    uint64_t owner;
    uint32_t pid;
    struct storage_node node;
    int64_t start, end;
    int16_t type;
};
struct native_flock {
    int16_t type, whence;
    int32_t padding;
    int64_t start, length;
    int32_t pid, trailing_padding;
};
static struct record_lock records[RECORD_LOCK_MAX];
static struct kernel_wait_queue record_waiters;
static uint64_t next_lock_owner;
static struct {
    struct task *task;
    struct record_lock request;
} pending[KERNEL_WAIT_QUEUE_MAX];

static bool same_inode(const struct storage_node *a, const struct storage_node *b)
{
    return a->volume_id == b->volume_id && a->first_cluster == b->first_cluster && a->type == b->type;
}

uint64_t syscall_record_lock_owner(struct task *task)
{
    struct task_fd_table_state *table = sched_task_fds(task);
    if (!table->lock_owner) table->lock_owner = __atomic_add_fetch(&next_lock_owner, 1, __ATOMIC_RELAXED);
    return table->lock_owner;
}

void syscall_record_locks_close(struct task *task, struct task_file *descriptor)
{
    struct task_file *file = task_file_description(descriptor);
    uint64_t owner = task ? sched_task_fds(task)->lock_owner : 0;
    if (!file || !owner) return;
    for (unsigned i = 0; i < RECORD_LOCK_MAX; ++i)
        if (records[i].owner == owner && same_inode(&records[i].node, &file->node)) records[i].owner = 0;
    kernel_wait_queue_wake_all(&record_waiters);
}

static bool overlap(const struct record_lock *a, const struct record_lock *b)
{
    return a->start <= b->end && b->start <= a->end;
}

static bool conflicts(const struct record_lock *a, const struct record_lock *b)
{
    return a->owner && b->owner && a->owner != b->owner && same_inode(&a->node, &b->node) &&
        overlap(a, b) && (a->type == LINUX_F_WRLCK || b->type == LINUX_F_WRLCK);
}

void syscall_record_lock_cancel(struct task *task)
{
    for (unsigned i = 0; i < KERNEL_WAIT_QUEUE_MAX; ++i)
        if (pending[i].task == task) pending[i].task = NULL;
}

/* Linux bounds its POSIX lock deadlock walk to ten owners. */
static bool waits_for(uint64_t owner, uint64_t target, unsigned depth)
{
    if (owner == target) return true;
    if (!depth) return false;
    for (unsigned i = 0; i < KERNEL_WAIT_QUEUE_MAX; ++i) {
        if (!pending[i].task || pending[i].request.owner != owner) continue;
        for (unsigned j = 0; j < RECORD_LOCK_MAX; ++j)
            if (conflicts(&records[j], &pending[i].request) && waits_for(records[j].owner, target, depth - 1)) return true;
    }
    return false;
}

static int insert(struct record_lock *destination, unsigned *count, const struct record_lock *record)
{
    if (*count == RECORD_LOCK_MAX) return -LINUX_ENOLCK;
    destination[(*count)++] = *record;
    return 0;
}

static int replace_range(const struct record_lock *request)
{
    struct record_lock *next = kernel_malloc(sizeof(records));
    if (!next) return -LINUX_ENOLCK;
    unsigned count = 0;
    int ret = 0;
    for (unsigned i = 0; i < RECORD_LOCK_MAX && !ret; ++i) {
        struct record_lock part = records[i];
        if (!part.owner) continue;
        if (part.owner != request->owner || !same_inode(&part.node, &request->node) || !overlap(&part, request)) {
            ret = insert(next, &count, &part);
            continue;
        }
        if (part.start < request->start) {
            part.end = request->start - 1;
            ret = insert(next, &count, &part);
        }
        part = records[i];
        if (!ret && part.end > request->end) {
            part.start = request->end + 1;
            ret = insert(next, &count, &part);
        }
    }
    if (!ret && request->type != LINUX_F_UNLCK) ret = insert(next, &count, request);
    if (!ret) {
        for (unsigned i = 0; i < count; ++i) {
            if (!next[i].owner) continue;
            for (unsigned j = 0; j < count; ++j) {
                if (i == j || next[j].owner != next[i].owner || next[j].type != next[i].type ||
                    !same_inode(&next[j].node, &next[i].node)) continue;
                if ((next[i].end == LOCK_END || next[j].start <= next[i].end + 1) &&
                    (next[j].end == LOCK_END || next[i].start <= next[j].end + 1)) {
                    if (next[j].start < next[i].start) next[i].start = next[j].start;
                    if (next[j].end > next[i].end) next[i].end = next[j].end;
                    next[j].owner = 0;
                    j = (unsigned)-1;
                }
            }
        }
        for (unsigned i = 0; i < RECORD_LOCK_MAX; ++i)
            records[i] = i < count ? next[i] : (struct record_lock){0};
        kernel_wait_queue_wake_all(&record_waiters);
    }
    kernel_free(next);
    return ret;
}

int64_t syscall_record_lock(int fd, uint32_t command, uint64_t pointer)
{
    struct task *task = sched_current_task();
    syscall_record_lock_cancel(task);
    if (task && task->waiting_queue == &record_waiters) kernel_wait_queue_remove(&record_waiters, task);
    struct task_file *file = task_file_for_fd(task, fd);
    if (!file || (file->flags & TASK_FILE_FLAG_PATH)) return -LINUX_EBADF;
    if (!user_range_ok(pointer, sizeof(struct native_flock))) return -LINUX_EFAULT;
    struct native_flock user;
    __builtin_memcpy(&user, (const void *)(uintptr_t)pointer, sizeof(user));
    if (user.type < LINUX_F_RDLCK || user.type > LINUX_F_UNLCK ||
        (command == LINUX_F_GETLK && user.type == LINUX_F_UNLCK)) return -LINUX_EINVAL;
    int64_t start;
    if (user.whence == 0) start = 0;
    else if (user.whence == 1) start = file->offset;
    else if (user.whence == 2) {
        int ret = storage_inode_refresh(&file->node);
        if (ret < 0) return ret;
        start = file->node.size;
    } else return -LINUX_EINVAL;
    if (__builtin_add_overflow(start, user.start, &start)) return -LINUX_EOVERFLOW;
    if (start < 0) return -LINUX_EINVAL;
    int64_t end = LOCK_END;
    if (user.length > 0 && __builtin_add_overflow(start, user.length - 1, &end)) return -LINUX_EOVERFLOW;
    if (user.length < 0) {
        if (!start) return -LINUX_EINVAL;
        end = start - 1;
        if (__builtin_add_overflow(start, user.length, &start) || start < 0) return -LINUX_EINVAL;
    }
    if (file->node.type != LEONOS_FS_TYPE_FILE) return -LINUX_EBADF;
    if (command != LINUX_F_GETLK && ((user.type == LINUX_F_RDLCK && !file_can_read(file)) ||
        (user.type == LINUX_F_WRLCK && !file_can_write(file)))) return -LINUX_EBADF;
    struct record_lock request = {.owner = syscall_record_lock_owner(task), .pid = sched_task_tgid(task),
        .node = file->node, .type = user.type, .start = start, .end = end};
    for (unsigned i = 0; i < RECORD_LOCK_MAX && user.type != LINUX_F_UNLCK; ++i) {
        const struct record_lock *held = &records[i];
        if (!conflicts(held, &request)) continue;
        if (command == LINUX_F_GETLK) {
            user.type = held->type;
            user.whence = 0;
            user.start = held->start;
            user.length = held->end == LOCK_END ? 0 : held->end - held->start + 1;
            user.pid = held->pid;
            goto copy_result;
        }
        if (command == LINUX_F_SETLK) return -LINUX_EAGAIN;
        if (waits_for(held->owner, request.owner, 10)) return -LINUX_EDEADLK;
        unsigned slot;
        for (slot = 0; slot < KERNEL_WAIT_QUEUE_MAX && pending[slot].task; ++slot) {}
        if (slot == KERNEL_WAIT_QUEUE_MAX) return -LINUX_ENOLCK;
        pending[slot].task = task;
        pending[slot].request = request;
        kernel_wait_queue_block_current(&record_waiters);
        return KERNEL_SYSCALL_BLOCKED;
    }
    if (command != LINUX_F_GETLK) return replace_range(&request);
    user.type = LINUX_F_UNLCK;
copy_result:
    if (!user_range_writable(pointer, sizeof(user))) return -LINUX_EFAULT;
    __builtin_memcpy((void *)(uintptr_t)pointer, &user, sizeof(user));
    return 0;
}
