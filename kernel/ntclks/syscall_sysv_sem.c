/* Linux v6.12 ipc/sem.c semantics, serialized by the kernel execution lock. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>
#include <ntclks/futex.h>
#include <ntclks/heap.h>
#include <ntclks/time.h>
#include <ntclks/usercopy.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/errno.h>

#define SEM_ID_SLOTS 32768u
#define SEM_FAST_IO 256u

struct sysv_sem_wait_list { struct task_sysv_sem_state *first, *last; };
struct sysv_sem_value {
    int32_t value, pid;
    int64_t otime;
    struct sysv_sem_wait_list alter, constant;
};
struct sysv_sem_undo_list {
    uint32_t references;
    struct sysv_sem_undo *first;
};
struct sysv_sem_undo {
    struct sysv_sem_undo *task_next, *array_next;
    struct sysv_sem_undo_list *owner;
    struct sysv_sem_array *array;
    int16_t adjustments[];
};
struct sysv_sem_array {
    struct linux_semid64_ds stat;
    int32_t id;
    uint32_t references, complex_count;
    bool removed;
    struct sysv_sem_undo *undos;
    struct sysv_sem_wait_list alter, constant;
    struct sysv_sem_value values[];
};
static struct sysv_sem_array *sem_ids[SEM_ID_SLOTS];
static uint32_t sem_count, sem_used, sem_cursor, sem_sequence;
static int32_t sem_last_index = -1;

/** @brief Read realtime seconds for ctime/otime. */
static int64_t sem_wall_time(void)
{
    struct linux_timespec now = {0};
    time_clock_get(LINUX_CLOCK_REALTIME, &now);
    return now.tv_sec;
}

/** @brief Read monotonic nanoseconds, saturated to Linux's signed ktime range. */
static uint64_t sem_monotonic(void)
{
    struct linux_timespec now = {0};
    time_clock_get(LINUX_CLOCK_MONOTONIC, &now);
    if ((uint64_t)now.tv_sec > (uint64_t)INT64_MAX / 1000000000) return INT64_MAX;
    uint64_t ns = (uint64_t)now.tv_sec * 1000000000;
    return (uint64_t)now.tv_nsec > INT64_MAX - ns ? INT64_MAX : ns + now.tv_nsec;
}

/** @brief Import bytes in page order, rejecting wraparound before dereference. */
static int sem_copy_in(void *destination, uint64_t pointer, uint64_t length)
{
    if (length > UINT64_MAX - pointer) return -LINUX_EFAULT;
    unsigned char *out = destination;
    while (length) {
        uint64_t take = 4096 - (pointer & 4095);
        if (take > length) take = length;
        if (!user_range_ok(pointer, take)) return -LINUX_EFAULT;
        __builtin_memcpy(out, (const void *)(uintptr_t)pointer, take);
        out += take; pointer += take; length -= take;
    }
    return 0;
}

/** @brief Apply ipcperms using effective UID, fsgid and supplementary groups. */
static bool sem_permitted(const struct task *task, const struct sysv_sem_array *a, uint32_t flags)
{
    const struct linux_ipc64_perm *p = &a->stat.sem_perm;
    uint32_t mode = p->mode, requested = (flags | (flags >> 3) | (flags >> 6)) & 7;
    bool group = task->fsgid == p->gid || task->fsgid == p->cgid;
    if (task->groups) for (uint32_t i = 0; i < task->groups->count; ++i)
        if (task->groups->ids[i] == p->gid || task->groups->ids[i] == p->cgid) group = true;
    if (task->euid == p->uid || task->euid == p->cuid) mode >>= 6;
    else if (group) mode >>= 3;
    return !(requested & ~mode) || (task->cap_effective & (1ULL << CAP_IPC_OWNER));
}

/** @brief Look up a native ID, optionally ignoring its generation for SEM_STAT. */
static struct sysv_sem_array *sem_find(int32_t id, bool generation)
{
    if (id < 0) return NULL;
    struct sysv_sem_array *a = sem_ids[(uint32_t)id & (SEM_ID_SLOTS - 1)];
    return a && (!generation || a->id == id) ? a : NULL;
}

/** @brief Release a registry/wait reference after the object stops being accessible. */
static void sem_put(struct sysv_sem_array *a)
{
    if (!--a->references) kernel_free(a);
}

/** @brief Remove a wait node without changing the complex-operation count. */
static void sem_list_remove(struct task_sysv_sem_state *w)
{
    if (!w->list) return;
    if (w->previous) w->previous->next = w->next;
    else w->list->first = w->next;
    if (w->next) w->next->previous = w->previous;
    else w->list->last = w->previous;
    w->previous = w->next = NULL; w->list = NULL;
}

/** @brief Append a wait node in FIFO order. */
static void sem_list_append(struct sysv_sem_wait_list *list, struct task_sysv_sem_state *w)
{
    w->previous = list->last; w->next = NULL; w->list = list;
    if (list->last) list->last->next = w;
    else list->first = w;
    list->last = w;
}

/** @brief Remove a finished or interrupted operation from pending accounting. */
static void sem_unlink_wait(struct task_sysv_sem_state *w)
{
    if (!w->list) return;
    if (w->count > 1) --w->array->complex_count;
    sem_list_remove(w);
}

/** @brief Return simple alter operations to per-semaphore queues at the unlock boundary. */
static void sem_unmerge(struct sysv_sem_array *a)
{
    if (a->complex_count) return;
    while (a->alter.first) {
        struct task_sysv_sem_state *w = a->alter.first;
        sem_list_remove(w);
        sem_list_append(&a->values[w->ops[0].sem_num].alter, w);
    }
}

/** @brief Merge simple queues by Linux list_splice_init order before a complex waiter. */
static void sem_merge(struct sysv_sem_array *a)
{
    for (uint32_t i = 0; i < a->stat.sem_nsems; ++i) {
        struct sysv_sem_wait_list *list = &a->values[i].alter;
        if (!list->first) continue;
        for (struct task_sysv_sem_state *w = list->first; w; w = w->next) w->list = &a->alter;
        list->last->next = a->alter.first;
        if (a->alter.first) a->alter.first->previous = list->last;
        else a->alter.last = list->last;
        a->alter.first = list->first;
        *list = (struct sysv_sem_wait_list){0};
    }
}

/** @brief Execute the atomic vector with rollback of both values and undo adjustments. */
static int sem_perform(struct task_sysv_sem_state *w)
{
    struct sysv_sem_array *a = w->array;
    int result = 0;
    uint32_t i;
    for (i = 0; i < w->count; ++i) {
        struct linux_sembuf *op = &w->ops[i];
        struct sysv_sem_value *v = &a->values[op->sem_num];
        int next = v->value + op->sem_op;
        if ((!op->sem_op && v->value) || next < 0) {
            w->blocking = i;
            result = op->sem_flg & LINUX_IPC_NOWAIT ? -LINUX_EAGAIN : 1;
            break;
        }
        if (next > LINUX_SEMVMX) { result = -LINUX_ERANGE; break; }
        if (op->sem_flg & LINUX_SEM_UNDO) {
            int undo = w->undo->adjustments[op->sem_num] - op->sem_op;
            if (undo < -32768 || undo > 32767) { result = -LINUX_ERANGE; break; }
            w->undo->adjustments[op->sem_num] = undo;
        }
        v->value = next;
    }
    if (!result) {
        for (i = 0; i < w->count; ++i) a->values[w->ops[i].sem_num].pid = w->pid;
        return 0;
    }
    while (i) {
        struct linux_sembuf *op = &w->ops[--i];
        a->values[op->sem_num].value -= op->sem_op;
        if (op->sem_flg & LINUX_SEM_UNDO) w->undo->adjustments[op->sem_num] += op->sem_op;
    }
    return result;
}

/** @brief Publish a result executed by another task before waking its original caller. */
static void sem_complete(struct task_sysv_sem_state *w, int result)
{
    sem_unlink_wait(w); w->result = result; w->completed = true;
    sched_wake_interruptible(w->task);
}

/** @brief Complete nonaltering operations whose complete zero-test vector is satisfied. */
static bool sem_wake_constant(struct sysv_sem_wait_list *list)
{
    bool completed = false;
    for (struct task_sysv_sem_state *w = list->first, *next; w; w = next) {
        next = w->next;
        int result = sem_perform(w);
        if (result > 0) continue;
        sem_complete(w, result);
        completed |= !result;
    }
    return completed;
}

/** @brief Run zero waiters for changed semaphores before processing alter waiters. */
static bool sem_wake_zero(struct sysv_sem_array *a, struct linux_sembuf *ops, uint32_t count)
{
    bool zero = false, completed = false;
    uint32_t n = ops ? count : a->stat.sem_nsems;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t index = ops ? ops[i].sem_num : i;
        if (!a->values[index].value) {
            zero = true;
            completed |= sem_wake_constant(&a->values[index].constant);
        }
    }
    if (zero) completed |= sem_wake_constant(&a->constant);
    return completed;
}

/** @brief Complete eligible alter operations with Linux's complex-queue rescan ordering. */
static bool sem_update_queue(struct sysv_sem_array *a, int32_t index)
{
    struct sysv_sem_wait_list *list = index < 0 ? &a->alter : &a->values[index].alter;
    bool completed = false;
    struct task_sysv_sem_state *w = list->first;
    while (w) {
        struct task_sysv_sem_state *next = w->next;
        if (index >= 0 && !a->values[index].value) break;
        int result = sem_perform(w);
        if (result <= 0) {
            sem_complete(w, result);
            if (!result) {
                completed = true;
                sem_wake_zero(a, w->ops, w->count);
                if (a->alter.first || w->count > 1) next = list->first;
            }
        }
        w = next;
    }
    return completed;
}

/** @brief Run Linux smart-update ordering and maintain per-semaphore operation timestamps. */
static void sem_update(struct sysv_sem_array *a, struct linux_sembuf *ops, uint32_t count, bool otime)
{
    otime |= sem_wake_zero(a, ops, count);
    if (a->alter.first) otime |= sem_update_queue(a, -1);
    else if (!ops) {
        for (uint32_t i = 0; i < a->stat.sem_nsems; ++i) otime |= sem_update_queue(a, i);
    } else {
        for (uint32_t i = 0; i < count; ++i)
            if (ops[i].sem_op > 0) otime |= sem_update_queue(a, ops[i].sem_num);
    }
    if (otime) a->values[ops ? ops[0].sem_num : 0].otime = sem_wall_time();
    sem_unmerge(a);
}

/** @brief Cancel the parked call while preserving persistent SEM_UNDO state across exec. */
void task_sysv_sem_cancel(struct task *task)
{
    if (!task) return;
    struct task_sysv_sem_state *w = &task->sysv_sem;
    sem_unlink_wait(w);
    if (w->array) { sem_unmerge(w->array); sem_put(w->array); }
    if (w->ops && w->ops != w->fast_ops) kernel_free(w->ops);
    *w = (struct task_sysv_sem_state){0};
}

/** @brief Allocate an undo owner before ID/permission checks, as find_alloc_undo does. */
static int sem_undo_owner(struct task *task)
{
    if (task->sysv_undo) return 0;
    struct sysv_sem_undo_list *list = kernel_malloc(sizeof(*list));
    if (!list) return -LINUX_ENOMEM;
    *list = (struct sysv_sem_undo_list){.references = 1};
    task->sysv_undo = list;
    return 0;
}

/** @brief Locate or allocate per-array adjustments, keeping recently used arrays first. */
static struct sysv_sem_undo *sem_get_undo(struct task *task, int32_t id, int *error)
{
    *error = sem_undo_owner(task);
    if (*error) return NULL;
    struct sysv_sem_undo_list *owner = task->sysv_undo;
    struct sysv_sem_undo **link = &owner->first, *u;
    while ((u = *link)) {
        if (u->array->id == id) {
            *link = u->task_next; u->task_next = owner->first; owner->first = u;
            return u;
        }
        link = &u->task_next;
    }
    struct sysv_sem_array *a = sem_find(id, true);
    if (!a) { *error = -LINUX_EINVAL; return NULL; }
    size_t bytes = sizeof(*u) + a->stat.sem_nsems * sizeof(u->adjustments[0]);
    u = kernel_malloc(bytes);
    if (!u) { *error = -LINUX_ENOMEM; return NULL; }
    __builtin_memset(u, 0, bytes);
    u->array = a; u->owner = owner; u->task_next = owner->first; owner->first = u;
    u->array_next = a->undos; a->undos = u;
    return u;
}

/** @brief Remove an undo entry from both ownership lists before releasing it. */
static void sem_remove_undo(struct sysv_sem_undo *u)
{
    struct sysv_sem_undo **link = &u->owner->first;
    while (*link && *link != u) link = &(*link)->task_next;
    if (*link) *link = u->task_next;
    link = &u->array->undos;
    while (*link && *link != u) link = &(*link)->array_next;
    if (*link) *link = u->array_next;
    kernel_free(u);
}

/** @brief Implement copy_semundo, including lazy allocation before sharing an empty list. */
int task_sysv_sem_clone(struct task *parent, struct task *child, uint64_t flags)
{
    child->sysv_undo = NULL;
    if (!(flags & CLONE_SYSVSEM)) return 0;
    int error = sem_undo_owner(parent);
    if (error) return error;
    child->sysv_undo = parent->sysv_undo;
    ++child->sysv_undo->references;
    return 0;
}

/** @brief Apply final-owner adjustments, clamped to [0,SEMVMX], then wake eligible calls. */
void task_sysv_sem_exit(struct task *task)
{
    if (!task) return;
    task_sysv_sem_cancel(task);
    struct sysv_sem_undo_list *owner = task->sysv_undo;
    task->sysv_undo = NULL;
    if (!owner || --owner->references) return;
    while (owner->first) {
        struct sysv_sem_undo *u = owner->first;
        struct sysv_sem_array *a = u->array;
        for (uint32_t i = 0; i < a->stat.sem_nsems; ++i) if (u->adjustments[i]) {
            struct sysv_sem_value *v = &a->values[i];
            v->value += u->adjustments[i];
            if (v->value < 0) v->value = 0;
            if (v->value > LINUX_SEMVMX) v->value = LINUX_SEMVMX;
            v->pid = sched_task_tgid(task);
        }
        sem_remove_undo(u);
        sem_update(a, NULL, 0, true);
    }
    kernel_free(owner);
}

/** @brief Create a zero-valued semaphore array or find an existing key. */
static int64_t sem_get(struct task *task, int32_t key, int32_t nsems, uint32_t flags)
{
    if (nsems < 0 || nsems > LINUX_SEMMSL) return -LINUX_EINVAL;
    if (key) {
        for (uint32_t i = 0; i < SEM_ID_SLOTS; ++i) {
            struct sysv_sem_array *a = sem_ids[i];
            if (!a || a->stat.sem_perm.key != key) continue;
            if ((flags & (LINUX_IPC_CREAT | LINUX_IPC_EXCL)) ==
                (LINUX_IPC_CREAT | LINUX_IPC_EXCL)) return -LINUX_EEXIST;
            if ((uint64_t)nsems > a->stat.sem_nsems) return -LINUX_EINVAL;
            return sem_permitted(task, a, flags) ? a->id : -LINUX_EACCES;
        }
        if (!(flags & LINUX_IPC_CREAT)) return -LINUX_ENOENT;
    }
    if (!nsems) return -LINUX_EINVAL;
    if (sem_used + (uint32_t)nsems > LINUX_SEMMNS) return -LINUX_ENOSPC;
    size_t bytes = sizeof(struct sysv_sem_array) + nsems * sizeof(struct sysv_sem_value);
    struct sysv_sem_array *a = kernel_malloc(bytes);
    if (!a) return -LINUX_ENOMEM;
    if (sem_count >= LINUX_SEMMNI) { kernel_free(a); return -LINUX_ENOSPC; }
    __builtin_memset(a, 0, bytes);
    uint32_t limit = sem_count * 3 / 2;
    if (limit < 64) limit = 64;
    if (limit > SEM_ID_SLOTS) limit = SEM_ID_SLOTS;
    uint32_t index = sem_cursor < limit ? sem_cursor : 0;
    while (sem_ids[index]) if (++index == limit) index = 0;
    sem_cursor = index + 1;
    if ((int32_t)index <= sem_last_index && ++sem_sequence >= (INT32_MAX >> 15)) sem_sequence = 0;
    sem_last_index = index;
    a->id = (int32_t)((sem_sequence << 15) | index); a->references = 1;
    a->stat.sem_perm = (struct linux_ipc64_perm){.key = key, .uid = task->euid, .cuid = task->euid,
        .gid = task->egid, .cgid = task->egid, .mode = flags & 0777, .seq = sem_sequence};
    a->stat.sem_ctime = sem_wall_time(); a->stat.sem_nsems = nsems;
    sem_ids[index] = a; ++sem_count; sem_used += nsems;
    return a->id;
}

/** @brief Park an already queued operation until a producer, signal or relative timeout. */
static int64_t sem_park(struct task_sysv_sem_state *w)
{
    uint64_t ticks = 0;
    if (w->timed) {
        uint64_t now = sem_monotonic();
        if (now >= w->deadline) { task_sysv_sem_cancel(w->task); return -LINUX_EAGAIN; }
        uint64_t remaining = w->deadline - now;
        uint64_t step = 1000000000 / NTCLKS_TICK_HZ;
        ticks = time_ticks() + (remaining + step - 1) / step;
    }
    sched_signal_wait_current(ticks);
    if (sched_task_pending(w->task) & ~w->task->blocked_signals) sched_wake_interruptible(w->task);
    return KERNEL_SYSCALL_BLOCKED;
}

/** @brief Preserve semop's completed result or timed-out EAGAIN before delivery of a handler. */
bool task_sysv_sem_ready(const struct task *task)
{
    const struct task_sysv_sem_state *w = &task->sysv_sem;
    return w->array && (w->completed || (w->timed && sem_monotonic() >= w->deadline));
}

/** @brief Preserve Linux's nonrestartable semop result across a default group stop. */
void task_sysv_sem_stop(struct task *task)
{
    struct task_sysv_sem_state *w = &task->sysv_sem;
    if (!w->array || w->completed) return;
    int result = w->timed && sem_monotonic() >= w->deadline ? -LINUX_EAGAIN : -LINUX_EINTR;
    sem_complete(w, result);
    sem_unmerge(w->array);
}

/** @brief Import native operations, execute atomically, and retain the full vector if blocked. */
static int64_t sem_operate(struct task *task, uint32_t number, int32_t id,
                            uint64_t pointer, uint32_t count, uint64_t timeout)
{
    struct task_sysv_sem_state *w = &task->sysv_sem;
    if (w->array) {
        if (w->completed) { int result = w->result; task_sysv_sem_cancel(task); return result; }
        return sem_park(w);
    }
    struct linux_timespec duration;
    if (timeout && sem_copy_in(&duration, timeout, sizeof(duration))) return -LINUX_EFAULT;
    if (count > LINUX_SEMOPM) return -LINUX_E2BIG;
    if (!count) return -LINUX_EINVAL;
    *w = (struct task_sysv_sem_state){.task = task, .number = number, .count = count,
        .timed = timeout != 0, .pid = sched_task_tgid(task)};
    w->ops = count <= 64 ? w->fast_ops : kernel_malloc(count * sizeof(*w->ops));
    if (!w->ops) return -LINUX_ENOMEM;
    int result = sem_copy_in(w->ops, pointer, count * sizeof(*w->ops));
    if (result) goto finish;
    if (id < 0) { result = -LINUX_EINVAL; goto finish; }
    if (timeout) {
        if (duration.tv_sec < 0 || (uint64_t)duration.tv_nsec >= 1000000000) {
            result = -LINUX_EINVAL; goto finish;
        }
        uint64_t now = sem_monotonic(), remaining;
        if ((uint64_t)duration.tv_sec > (uint64_t)INT64_MAX / 1000000000) remaining = INT64_MAX;
        else {
            remaining = (uint64_t)duration.tv_sec * 1000000000;
            remaining = (uint64_t)duration.tv_nsec > INT64_MAX - remaining ? INT64_MAX : remaining + duration.tv_nsec;
        }
        w->deadline = remaining > INT64_MAX - now ? INT64_MAX : now + remaining;
    }
    bool undo = false;
    uint32_t max = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (w->ops[i].sem_num > max) max = w->ops[i].sem_num;
        undo |= (w->ops[i].sem_flg & LINUX_SEM_UNDO) != 0;
        w->alter |= w->ops[i].sem_op != 0;
    }
    if (undo) { w->undo = sem_get_undo(task, id, &result); if (!w->undo) goto finish; }
    struct sysv_sem_array *a = sem_find(id, true);
    if (!a) { result = -LINUX_EINVAL; goto finish; }
    if (max >= a->stat.sem_nsems) { result = -LINUX_EFBIG; goto finish; }
    if (!sem_permitted(task, a, w->alter ? 0222 : 0444)) { result = -LINUX_EACCES; goto finish; }
    w->array = a; ++a->references;
    result = sem_perform(w);
    if (!result) {
        if (w->alter) sem_update(a, w->ops, count, true);
        else a->values[w->ops[0].sem_num].otime = sem_wall_time();
    } else if (result > 0) {
        struct sysv_sem_wait_list *list;
        if (count > 1) {
            if (!a->complex_count) sem_merge(a);
            ++a->complex_count;
            list = w->alter ? &a->alter : &a->constant;
        } else {
            struct sysv_sem_value *v = &a->values[w->ops[0].sem_num];
            list = w->alter ? (a->complex_count ? &a->alter : &v->alter) : &v->constant;
        }
        sem_list_append(list, w);
        return sem_park(w);
    }
finish:
    task_sysv_sem_cancel(task);
    return result;
}

/** @brief Remove the ID, invalidate undo entries, and complete all old waiters with EIDRM. */
static void sem_remove(struct sysv_sem_array *a)
{
    a->removed = true;
    while (a->constant.first) sem_complete(a->constant.first, -LINUX_EIDRM);
    while (a->alter.first) sem_complete(a->alter.first, -LINUX_EIDRM);
    for (uint32_t i = 0; i < a->stat.sem_nsems; ++i) {
        while (a->values[i].constant.first) sem_complete(a->values[i].constant.first, -LINUX_EIDRM);
        while (a->values[i].alter.first) sem_complete(a->values[i].alter.first, -LINUX_EIDRM);
    }
    while (a->undos) sem_remove_undo(a->undos);
    sem_ids[(uint32_t)a->id & (SEM_ID_SLOTS - 1)] = NULL;
    --sem_count; sem_used -= a->stat.sem_nsems;
    sem_put(a);
}

/** @brief Count only the first unsatisfied operation in each waiting vector. */
static int sem_wait_count(struct sysv_sem_wait_list *list, uint32_t index, bool zero)
{
    int n = 0;
    for (struct task_sysv_sem_state *w = list->first; w; w = w->next) {
        struct linux_sembuf *op = &w->ops[w->blocking];
        if (op->sem_num == index && (zero ? op->sem_op == 0 : op->sem_op < 0)) ++n;
    }
    return n;
}

/** @brief Read/set semaphore arrays, preserving atomic input validation and undo resets. */
static int sem_all(struct task *task, struct sysv_sem_array *a, uint64_t pointer, bool setting)
{
    uint16_t fast[SEM_FAST_IO], *values = fast;
    uint32_t n = a->stat.sem_nsems;
    if (n > SEM_FAST_IO) { values = kernel_malloc(n * sizeof(*values)); if (!values) return -LINUX_ENOMEM; }
    int error = 0;
    if (setting) {
        error = sem_copy_in(values, pointer, n * sizeof(*values));
        if (error) goto out;
        for (uint32_t i = 0; i < n; ++i) if (values[i] > LINUX_SEMVMX) { error = -LINUX_ERANGE; goto out; }
        for (uint32_t i = 0; i < n; ++i) {
            a->values[i].value = values[i]; a->values[i].pid = sched_task_tgid(task);
        }
        for (struct sysv_sem_undo *u = a->undos; u; u = u->array_next)
            __builtin_memset(u->adjustments, 0, n * sizeof(u->adjustments[0]));
        a->stat.sem_ctime = sem_wall_time(); sem_update(a, NULL, 0, false);
    } else {
        for (uint32_t i = 0; i < n; ++i) values[i] = a->values[i].value;
        error = user_copy_to_task(task, pointer, values, n * sizeof(*values));
    }
out:
    if (values != fast) kernel_free(values);
    return error;
}

/** @brief Implement native semctl command, width, permission and copy ordering. */
static int64_t sem_control(struct task *task, int32_t id, int32_t index, int32_t cmd, uint64_t arg)
{
    if (id < 0) return -LINUX_EINVAL;
    if (cmd == LINUX_IPC_INFO || cmd == LINUX_SEM_INFO) {
        struct linux_seminfo info = {.semmap = LINUX_SEMMNS, .semmni = LINUX_SEMMNI,
            .semmns = LINUX_SEMMNS, .semmnu = LINUX_SEMMNS, .semmsl = LINUX_SEMMSL,
            .semopm = LINUX_SEMOPM, .semume = LINUX_SEMOPM,
            .semusz = cmd == LINUX_SEM_INFO ? (int32_t)sem_count : 20,
            .semvmx = LINUX_SEMVMX, .semaem = cmd == LINUX_SEM_INFO ? (int32_t)sem_used : LINUX_SEMVMX};
        int32_t maximum = SEM_ID_SLOTS - 1;
        while (maximum > 0 && !sem_ids[maximum]) --maximum;
        int error = user_copy_to_task(task, arg, &info, sizeof(info));
        return error ? error : maximum;
    }
    bool stat = cmd == LINUX_IPC_STAT || cmd == LINUX_SEM_STAT || cmd == LINUX_SEM_STAT_ANY;
    bool owner = cmd == LINUX_IPC_SET || cmd == LINUX_IPC_RMID;
    if (!stat && !owner && (cmd < LINUX_GETPID || cmd > LINUX_SETALL)) return -LINUX_EINVAL;
    struct linux_semid64_ds input;
    if (cmd == LINUX_IPC_SET && sem_copy_in(&input, arg, sizeof(input))) return -LINUX_EFAULT;
    int32_t value = (int32_t)arg;
    if (cmd == LINUX_SETVAL && (value < 0 || value > LINUX_SEMVMX)) return -LINUX_ERANGE;
    struct sysv_sem_array *a = sem_find(id, cmd != LINUX_SEM_STAT && cmd != LINUX_SEM_STAT_ANY);
    if (!a) return -LINUX_EINVAL;
    if (cmd == LINUX_SETVAL && (index < 0 || (uint32_t)index >= a->stat.sem_nsems)) return -LINUX_EINVAL;
    struct linux_ipc64_perm *p = &a->stat.sem_perm;
    if (owner) {
        if (task->euid != p->uid && task->euid != p->cuid &&
            !(task->cap_effective & (1ULL << CAP_SYS_ADMIN))) return -LINUX_EPERM;
        if (cmd == LINUX_IPC_RMID) { sem_remove(a); return 0; }
        if (input.sem_perm.uid == UINT32_MAX || input.sem_perm.gid == UINT32_MAX) return -LINUX_EINVAL;
        p->uid = input.sem_perm.uid; p->gid = input.sem_perm.gid; p->mode = input.sem_perm.mode & 0777;
        a->stat.sem_ctime = sem_wall_time();
        return 0;
    }
    if (cmd != LINUX_SEM_STAT_ANY && !sem_permitted(task, a,
        cmd == LINUX_SETALL || cmd == LINUX_SETVAL ? 0222 : 0444)) return -LINUX_EACCES;
    if (stat) {
        struct linux_semid64_ds output = a->stat;
        output.sem_otime = a->values[0].otime;
        for (uint32_t i = 1; i < output.sem_nsems; ++i)
            if (a->values[i].otime > output.sem_otime) output.sem_otime = a->values[i].otime;
        int error = user_copy_to_task(task, arg, &output, sizeof(output));
        return error ? error : cmd == LINUX_IPC_STAT ? 0 : a->id;
    }
    if (cmd == LINUX_GETALL || cmd == LINUX_SETALL) return sem_all(task, a, arg, cmd == LINUX_SETALL);
    if (index < 0 || (uint32_t)index >= a->stat.sem_nsems) return -LINUX_EINVAL;
    struct sysv_sem_value *v = &a->values[index];
    if (cmd == LINUX_GETVAL) return v->value;
    if (cmd == LINUX_GETPID) return v->pid;
    if (cmd == LINUX_GETNCNT || cmd == LINUX_GETZCNT) {
        bool zero = cmd == LINUX_GETZCNT;
        return sem_wait_count(&a->alter, index, zero) +
            (zero ? sem_wait_count(&a->constant, index, true) : 0) +
            sem_wait_count(zero ? &v->constant : &v->alter, index, zero);
    }
    for (struct sysv_sem_undo *u = a->undos; u; u = u->array_next) u->adjustments[index] = 0;
    v->value = value; v->pid = sched_task_tgid(task); a->stat.sem_ctime = sem_wall_time();
    sem_update(a, NULL, 0, false);
    return 0;
}

/**
 * @brief Dispatch Linux semget/semop/semctl/semtimedop with native argument widths.
 * @param number Native syscall number.
 * @param a0 Key or semaphore identifier (int32).
 * @param a1 Array size, semaphore index or operation pointer.
 * @param a2 Flags, command or unsigned 32-bit operation count.
 * @param a3 semctl argument or semtimedop timeout pointer.
 * @return Linux value/-errno, or the internal parked-syscall marker.
 */
int64_t syscall_sysv_sem(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    struct task *task = sched_current_task();
    if (!task) return -LINUX_ESRCH;
    if (task->sysv_sem.ops && task->sysv_sem.number != number) task_sysv_sem_cancel(task);
    switch (number) {
    case __NR_semget: return sem_get(task, (int32_t)a0, (int32_t)a1, (uint32_t)a2);
    case __NR_semctl: return sem_control(task, (int32_t)a0, (int32_t)a1, (int32_t)a2, a3);
    case __NR_semop: case __NR_semtimedop:
        return sem_operate(task, number, (int32_t)a0, a1, (uint32_t)a2, number == __NR_semtimedop ? a3 : 0);
    default: return -LINUX_ENOSYS;
    }
}
