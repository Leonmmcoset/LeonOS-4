/* Native Linux v6.12 ipc/msg.c and ipc/util.c; one system IPC namespace. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>
#include <ntclks/futex.h>
#include <ntclks/heap.h>
#include <ntclks/time.h>
#include <ntclks/usercopy.h>
#include <linux/msg.h>
#include <linux/time.h>
#include <linux/capability.h>
#include <linux/errno.h>

#define MSG_ID_SLOTS 32768u

struct sysv_message {
    struct sysv_message *previous, *next;
    int64_t type;
    uint64_t size;
    unsigned char data[];
};

struct sysv_msg_queue {
    struct linux_msqid64_ds stat;
    int32_t id;
    uint32_t references;
    bool removed;
    struct sysv_message *first, *last;
    struct task_sysv_msg_state *senders, *senders_last, *receivers, *receivers_last;
};

/* All mutations and task cancellation use the existing kernel execution lock. */
static struct sysv_msg_queue *msg_ids[MSG_ID_SLOTS];
static uint32_t msg_queue_count, msg_cursor, msg_sequence;
static int32_t msg_last_index = -1;
static uint64_t msg_total_bytes, msg_total_headers;

/** @brief Read the wall clock used by SysV status timestamps. */
static int64_t msg_now(void)
{
    struct linux_timespec value = {0};
    time_clock_get(0, &value);
    return value.tv_sec;
}

/** @brief Import user bytes in page order, without accepting arithmetic wraparound. */
static int msg_copy_in(void *destination, uint64_t address, uint64_t size)
{
    if (size > UINT64_MAX - address) return -LINUX_EFAULT;
    unsigned char *out = destination;
    while (size) {
        uint64_t take = 4096 - (address & 4095);
        if (take > size) take = size;
        if (!user_range_ok(address, take)) return -LINUX_EFAULT;
        __builtin_memcpy(out, (const void *)(uintptr_t)address, take);
        out += take; address += take; size -= take;
    }
    return 0;
}

/** @brief Match Linux in_group_p using fsgid and supplementary groups. */
static bool msg_in_group(const struct task *task, uint32_t group)
{
    if (task->fsgid == group) return true;
    if (task->groups) for (uint32_t i = 0; i < task->groups->count; ++i)
        if (task->groups->ids[i] == group) return true;
    return false;
}

/** @brief Check ipcperms owner/creator, group and CAP_IPC_OWNER access. */
static bool msg_permitted(struct task *task, struct sysv_msg_queue *queue, uint32_t requested)
{
    struct linux_ipc64_perm *p = &queue->stat.msg_perm;
    uint32_t granted = p->mode;
    requested = (requested | (requested >> 3) | (requested >> 6)) & 7;
    if (task->euid == p->uid || task->euid == p->cuid) granted >>= 6;
    else if (msg_in_group(task, p->gid) || msg_in_group(task, p->cgid)) granted >>= 3;
    return !(requested & ~granted) || (task->cap_effective & (1ULL << CAP_IPC_OWNER));
}

/** @brief Resolve an IPC ID with optional sequence validation for STAT index commands. */
static struct sysv_msg_queue *msg_find(int32_t id, bool check_sequence)
{
    if (id < 0) return NULL;
    struct sysv_msg_queue *q = msg_ids[(uint32_t)id & (MSG_ID_SLOTS - 1)];
    return q && (!check_sequence || q->id == id) ? q : NULL;
}

/** @brief Drop the registry or a blocked task's ownership of a queue. */
static void msg_put_queue(struct sysv_msg_queue *q)
{
    if (!--q->references) kernel_free(q);
}

/** @brief Detach a wait record while retaining its operation and queue reference. */
static void msg_unlink_wait(struct task_sysv_msg_state *w)
{
    if (!w->linked) return;
    struct sysv_msg_queue *q = w->queue;
    bool sending = w->number == __NR_msgsnd;
    if (w->previous) w->previous->next = w->next;
    else if (sending) q->senders = w->next;
    else q->receivers = w->next;
    if (w->next) w->next->previous = w->previous;
    else if (sending) q->senders_last = w->previous;
    else q->receivers_last = w->previous;
    w->previous = w->next = NULL;
    w->linked = false;
}

/** @brief Append a sender or receiver in FIFO order without allocating a wait node. */
static void msg_link_wait(struct task_sysv_msg_state *w)
{
    struct sysv_msg_queue *q = w->queue;
    bool sending = w->number == __NR_msgsnd;
    struct task_sysv_msg_state **head = sending ? &q->senders : &q->receivers;
    struct task_sysv_msg_state **tail = sending ? &q->senders_last : &q->receivers_last;
    w->previous = *tail; w->next = NULL;
    if (*tail) (*tail)->next = w;
    else *head = w;
    *tail = w; w->linked = true;
}

/** @brief Free retained input or a directly delivered message on task cancellation. */
void task_sysv_msg_cancel(struct task *task)
{
    if (!task || !task->sysv_msg.queue) return;
    struct task_sysv_msg_state *w = &task->sysv_msg;
    msg_unlink_wait(w);
    if (w->message) kernel_free(w->message);
    msg_put_queue(w->queue);
    *w = (struct task_sysv_msg_state){0};
}

/** @brief Wake an interruptible wait without resuming a job-control stopped task. */
static void msg_wake(struct task_sysv_msg_state *w)
{
    sched_wake_interruptible(w->task);
}

/** @brief Bound both message bytes and zero-length headers as Linux does. */
static bool msg_fits(struct sysv_msg_queue *q, uint64_t size)
{
    return size + q->stat.msg_cbytes <= q->stat.msg_qbytes &&
        1 + q->stat.msg_qnum <= q->stat.msg_qbytes;
}

/** @brief Wake eligible senders; rotate oversized requests behind smaller senders. */
static void msg_wake_senders(struct sysv_msg_queue *q)
{
    uint32_t count = 0;
    for (struct task_sysv_msg_state *w = q->senders; w; w = w->next) ++count;
    struct task_sysv_msg_state *w = q->senders;
    while (count--) {
        struct task_sysv_msg_state *next = w->next;
        if (msg_fits(q, w->size)) msg_wake(w);
        else { msg_unlink_wait(w); msg_link_wait(w); }
        w = next;
    }
}

/** @brief Publish blocking before rechecking pending signals to avoid a lost wakeup. */
static int64_t msg_park(struct task_sysv_msg_state *w)
{
    if (!w->linked) msg_link_wait(w);
    sched_signal_wait_current(0);
    if (sched_task_pending(w->task) & ~w->task->blocked_signals) msg_wake(w);
    return KERNEL_SYSCALL_BLOCKED;
}

/** @brief Pin an operation's arguments and queue across the parked syscall instruction. */
static void msg_begin_wait(struct task *task, struct sysv_msg_queue *q, uint32_t number,
                            struct sysv_message *message, uint64_t buffer, uint64_t size,
                            int64_t type, uint32_t flags)
{
    task->sysv_msg = (struct task_sysv_msg_state){.queue = q, .message = message,
        .task = task, .buffer = buffer, .size = size, .type = type, .flags = flags,
        .number = number, .result = -LINUX_EAGAIN};
    ++q->references;
}

/** @brief Create or look up a keyed queue using Linux's default ID cycling policy. */
static int64_t msg_get(struct task *task, int32_t key, uint32_t flags)
{
    if (key) {
        for (uint32_t i = 0; i < MSG_ID_SLOTS; ++i) {
            struct sysv_msg_queue *q = msg_ids[i];
            if (!q || q->stat.msg_perm.key != key) continue;
            if ((flags & (LINUX_IPC_CREAT | LINUX_IPC_EXCL)) ==
                (LINUX_IPC_CREAT | LINUX_IPC_EXCL)) return -LINUX_EEXIST;
            return msg_permitted(task, q, flags) ? q->id : -LINUX_EACCES;
        }
        if (!(flags & LINUX_IPC_CREAT)) return -LINUX_ENOENT;
    }
    struct sysv_msg_queue *q = kernel_malloc(sizeof(*q));
    if (!q) return -LINUX_ENOMEM;
    if (msg_queue_count >= LINUX_MSGMNI) { kernel_free(q); return -LINUX_ENOSPC; }
    uint32_t maximum = msg_queue_count * 3 / 2;
    if (maximum < 64) maximum = 64;
    if (maximum > MSG_ID_SLOTS) maximum = MSG_ID_SLOTS;
    uint32_t index = msg_cursor < maximum ? msg_cursor : 0;
    while (msg_ids[index]) if (++index == maximum) index = 0;
    msg_cursor = index + 1;
    if ((int32_t)index <= msg_last_index && ++msg_sequence >= (INT32_MAX >> 15)) msg_sequence = 0;
    msg_last_index = (int32_t)index;
    *q = (struct sysv_msg_queue){.id = (int32_t)((msg_sequence << 15) | index), .references = 1};
    q->stat.msg_perm = (struct linux_ipc64_perm){.key = key, .uid = task->euid, .cuid = task->euid,
        .gid = task->egid, .cgid = task->egid, .mode = flags & 0777, .seq = msg_sequence};
    q->stat.msg_ctime = msg_now(); q->stat.msg_qbytes = LINUX_MSGMNB;
    msg_ids[index] = q; ++msg_queue_count;
    return q->id;
}

/** @brief Apply the FIFO, exact, exclusion or least-positive-type receiver filter. */
static bool msg_matches(struct sysv_message *m, int64_t type, uint32_t flags)
{
    if (flags & LINUX_MSG_COPY) return true;
    if (!type) return true;
    if (type < 0) return m->type <= (type == INT64_MIN ? INT64_MAX : -type);
    return flags & LINUX_MSG_EXCEPT ? m->type != type : m->type == type;
}

/** @brief Deliver to a blocked receiver, including oversize wakeups preceding a fit. */
static bool msg_pipeline(struct sysv_msg_queue *q, struct sysv_message *message)
{
    struct task_sysv_msg_state *w = q->receivers;
    while (w) {
        struct task_sysv_msg_state *next = w->next;
        if (msg_matches(message, w->type, w->flags)) {
            msg_unlink_wait(w);
            w->completed = true;
            w->result = -LINUX_E2BIG;
            if ((w->flags & LINUX_MSG_NOERROR) || w->size >= message->size) {
                w->message = message; w->result = 0;
                q->stat.msg_lrpid = w->task->pid; q->stat.msg_rtime = msg_now();
                msg_wake(w);
                return true;
            }
            msg_wake(w);
        }
        w = next;
    }
    return false;
}

/** @brief Import a message before ID/permission checks, retaining real payload bytes. */
static struct sysv_message *msg_load(uint64_t address, uint64_t size, int *error)
{
    struct sysv_message *m = kernel_malloc(sizeof(*m) + size);
    if (!m) { *error = -LINUX_ENOMEM; return NULL; }
    *m = (struct sysv_message){.size = size};
    *error = msg_copy_in(m->data, address, size);
    if (*error) { kernel_free(m); return NULL; }
    return m;
}

/** @brief Send, block with captured input, or return the real nonblocking EAGAIN. */
static int64_t msg_send(struct task *task, int32_t id, uint64_t buffer, uint64_t size, uint32_t flags)
{
    struct task_sysv_msg_state *w = &task->sysv_msg;
    struct sysv_msg_queue *q;
    struct sysv_message *m;
    int error = 0;
    if (w->queue) {
        q = w->queue; m = w->message; size = w->size; flags = w->flags;
        if (q->removed) { task_sysv_msg_cancel(task); return -LINUX_EIDRM; }
        if (sched_task_pending(task) & ~task->blocked_signals) return msg_park(w);
        msg_unlink_wait(w);
    } else {
        int64_t type;
        if (msg_copy_in(&type, buffer, 8)) return -LINUX_EFAULT;
        if (id < 0 || size > LINUX_MSGMAX || type < 1) return -LINUX_EINVAL;
        m = msg_load(buffer + 8, size, &error);
        if (!m) return error;
        m->type = type;
        q = msg_find(id, true);
        if (!q) { kernel_free(m); return -LINUX_EINVAL; }
    }
    if (!msg_permitted(task, q, 0222)) error = -LINUX_EACCES;
    else if (!msg_fits(q, size)) {
        if (flags & LINUX_IPC_NOWAIT) error = -LINUX_EAGAIN;
        else {
            if (!w->queue) msg_begin_wait(task, q, __NR_msgsnd, m, buffer, size, m->type, flags);
            return msg_park(w);
        }
    }
    if (!error) {
        q->stat.msg_lspid = sched_task_tgid(task); q->stat.msg_stime = msg_now();
        if (!msg_pipeline(q, m)) {
            m->previous = q->last;
            if (q->last) q->last->next = m;
            else q->first = m;
            q->last = m;
            q->stat.msg_cbytes += size; ++q->stat.msg_qnum;
            msg_total_bytes += size; ++msg_total_headers;
        }
    } else kernel_free(m);
    w->message = NULL;
    task_sysv_msg_cancel(task);
    return error;
}

/** @brief Copy a consumed message, preserving consumption and partial output on EFAULT. */
static int64_t msg_fill(struct task *task, struct sysv_message *m, uint64_t buffer, uint64_t size)
{
    if (size > m->size) size = m->size;
    int error = user_copy_to_task(task, buffer, &m->type, 8);
    if (!error) error = user_copy_to_task(task, buffer + 8, m->data, size);
    kernel_free(m);
    return error ? error : (int64_t)size;
}

/** @brief Receive with type ordering, Linux MSG_COPY validation and retained wait state. */
static int64_t msg_receive(struct task *task, int32_t id, uint64_t buffer, uint64_t size,
                            int64_t type, uint32_t flags)
{
    struct task_sysv_msg_state *w = &task->sysv_msg;
    struct sysv_msg_queue *q;
    struct sysv_message *copy = NULL, *found = NULL;
    int error = 0;
    if (w->queue) {
        q = w->queue; buffer = w->buffer; size = w->size; type = w->type; flags = w->flags;
        if (w->completed) {
            struct sysv_message *m = w->message; error = w->result; w->message = NULL;
            task_sysv_msg_cancel(task);
            return error ? error : msg_fill(task, m, buffer, size);
        }
        if (sched_task_pending(task) & ~task->blocked_signals) return msg_park(w);
        msg_unlink_wait(w);
    } else {
        if (id < 0 || (int64_t)size < 0) return -LINUX_EINVAL;
        if (flags & LINUX_MSG_COPY) {
            if ((flags & LINUX_MSG_EXCEPT) || !(flags & LINUX_IPC_NOWAIT)) return -LINUX_EINVAL;
            copy = msg_load(buffer, size < LINUX_MSGMAX ? size : LINUX_MSGMAX, &error);
            if (!copy) return error;
        }
        q = msg_find(id, true);
        if (!q) { if (copy) kernel_free(copy); return -LINUX_EINVAL; }
    }
    if (!msg_permitted(task, q, 0444)) error = -LINUX_EACCES;
    else {
        int64_t index = 0;
        for (struct sysv_message *m = q->first; m; m = m->next, ++index) {
            if (!msg_matches(m, type, flags)) continue;
            if (flags & LINUX_MSG_COPY) { if (type == index) { found = m; break; } }
            else if (type < 0) { if (!found || m->type < found->type) found = m; }
            else { found = m; break; }
        }
        if (found) {
            if (size < found->size && !(flags & LINUX_MSG_NOERROR)) error = -LINUX_E2BIG;
            else if (copy) {
                if (copy->size < found->size) error = -LINUX_EINVAL;
                else {
                    copy->type = found->type; copy->size = found->size;
                    __builtin_memcpy(copy->data, found->data, found->size);
                    found = copy; copy = NULL;
                }
            } else {
                if (found->previous) found->previous->next = found->next;
                else q->first = found->next;
                if (found->next) found->next->previous = found->previous;
                else q->last = found->previous;
                --q->stat.msg_qnum; q->stat.msg_cbytes -= found->size;
                --msg_total_headers; msg_total_bytes -= found->size;
                q->stat.msg_lrpid = sched_task_tgid(task); q->stat.msg_rtime = msg_now();
                msg_wake_senders(q);
            }
        } else if (flags & LINUX_IPC_NOWAIT) error = -LINUX_ENOMSG;
        else {
            if (!w->queue) msg_begin_wait(task, q, __NR_msgrcv, NULL, buffer, size, type, flags);
            return msg_park(w);
        }
    }
    if (copy) kernel_free(copy);
    task_sysv_msg_cancel(task);
    return error ? error : msg_fill(task, found, buffer, size);
}

/** @brief Delete an ID and wake its waiters, retaining objects already handed to receivers. */
static void msg_remove(struct sysv_msg_queue *q)
{
    q->removed = true;
    msg_ids[(uint32_t)q->id & (MSG_ID_SLOTS - 1)] = NULL;
    --msg_queue_count;
    while (q->senders || q->receivers) {
        struct task_sysv_msg_state *w = q->senders ? q->senders : q->receivers;
        msg_unlink_wait(w); w->completed = true; w->result = -LINUX_EIDRM;
        msg_wake(w);
    }
    while (q->first) {
        struct sysv_message *m = q->first; q->first = m->next; kernel_free(m);
    }
    msg_total_bytes -= q->stat.msg_cbytes; msg_total_headers -= q->stat.msg_qnum;
    q->last = NULL; q->stat.msg_cbytes = q->stat.msg_qnum = 0;
    msg_put_queue(q);
}

/** @brief Implement native msgctl layouts, permissions, configuration and live accounting. */
static int64_t msg_control(struct task *task, int32_t id, int32_t command, uint64_t buffer)
{
    if (id < 0 || command < 0) return -LINUX_EINVAL;
    if (command == LINUX_IPC_INFO || command == LINUX_MSG_INFO) {
        struct linux_msginfo info = {.msgpool = LINUX_MSGMNI * LINUX_MSGMNB / 1024,
            .msgmap = LINUX_MSGMNB, .msgmax = LINUX_MSGMAX, .msgmnb = LINUX_MSGMNB,
            .msgmni = LINUX_MSGMNI, .msgssz = 16, .msgtql = LINUX_MSGMNB, .msgseg = 65535};
        if (command == LINUX_MSG_INFO) {
            info.msgpool = msg_queue_count; info.msgmap = (int32_t)msg_total_headers;
            info.msgtql = (int32_t)msg_total_bytes;
        }
        int32_t maximum = MSG_ID_SLOTS - 1;
        while (maximum > 0 && !msg_ids[maximum]) --maximum;
        int error = user_copy_to_task(task, buffer, &info, sizeof(info));
        return error ? error : maximum;
    }
    if (command != LINUX_IPC_STAT && command != LINUX_MSG_STAT && command != LINUX_MSG_STAT_ANY &&
        command != LINUX_IPC_SET && command != LINUX_IPC_RMID) return -LINUX_EINVAL;
    struct linux_msqid64_ds input;
    if (command == LINUX_IPC_SET && msg_copy_in(&input, buffer, sizeof(input))) return -LINUX_EFAULT;
    struct sysv_msg_queue *q = msg_find(id, command != LINUX_MSG_STAT && command != LINUX_MSG_STAT_ANY);
    if (!q) return -LINUX_EINVAL;
    if (command == LINUX_IPC_STAT || command == LINUX_MSG_STAT || command == LINUX_MSG_STAT_ANY) {
        if (command != LINUX_MSG_STAT_ANY && !msg_permitted(task, q, 0444)) return -LINUX_EACCES;
        int error = user_copy_to_task(task, buffer, &q->stat, sizeof(q->stat));
        return error ? error : command == LINUX_IPC_STAT ? 0 : q->id;
    }
    struct linux_ipc64_perm *p = &q->stat.msg_perm;
    if (task->euid != p->uid && task->euid != p->cuid &&
        !(task->cap_effective & (1ULL << CAP_SYS_ADMIN))) return -LINUX_EPERM;
    if (command == LINUX_IPC_RMID) { msg_remove(q); return 0; }
    /* msgctl_down takes int even though the native input field is unsigned long. */
    int32_t bytes = (int32_t)input.msg_qbytes;
    if ((uint32_t)bytes > LINUX_MSGMNB && !(task->cap_effective & (1ULL << CAP_SYS_RESOURCE))) return -LINUX_EPERM;
    if (input.msg_perm.uid == UINT32_MAX || input.msg_perm.gid == UINT32_MAX) return -LINUX_EINVAL;
    p->uid = input.msg_perm.uid; p->gid = input.msg_perm.gid; p->mode = input.msg_perm.mode & 0777;
    q->stat.msg_qbytes = (uint64_t)(int64_t)bytes; q->stat.msg_ctime = msg_now();
    for (struct task_sysv_msg_state *w = q->receivers; w; w = w->next) msg_wake(w);
    msg_wake_senders(q);
    return 0;
}

/**
 * @brief Dispatch Linux 68-71, retaining native size/type widths and truncating int arguments.
 * @param number Native syscall number.
 * @param a0 Key or IPC identifier.
 * @param a1 Flags, message pointer or msgctl command.
 * @param a2 Message size or control pointer.
 * @param a3 Send flags or signed 64-bit receive selector.
 * @param a4 Receive flags.
 * @return Native value/-errno, or the internal parked-syscall marker.
 */
int64_t syscall_sysv_msg(uint64_t number, uint64_t a0, uint64_t a1,
                         uint64_t a2, uint64_t a3, uint64_t a4)
{
    struct task *task = sched_current_task();
    if (!task) return -LINUX_ESRCH;
    if (task->sysv_msg.queue && task->sysv_msg.number != number) task_sysv_msg_cancel(task);
    switch (number) {
    case __NR_msgget: return msg_get(task, (int32_t)a0, (uint32_t)a1);
    case __NR_msgsnd: return msg_send(task, (int32_t)a0, a1, a2, (uint32_t)a3);
    case __NR_msgrcv: return msg_receive(task, (int32_t)a0, a1, a2, (int64_t)a3, (uint32_t)a4);
    case __NR_msgctl: return msg_control(task, (int32_t)a0, (int32_t)a1, a2);
    default: return -LINUX_ENOSYS;
    }
}
