/* Linux 6.12 net/socket.c sendmmsg/recvmmsg iteration and restart state. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>
#include <ntclks/futex.h>
#include <ntclks/usercopy.h>
#include <ntclks/time.h>
#include <linux/socket.h>
#include <linux/errno.h>

/** @brief Update recvmmsg's remaining timeout after a successfully copied message. */
static bool mmsg_update_timeout(struct task_mmsg_state *state)
{
    struct linux_timespec now;
    time_clock_get(LINUX_CLOCK_MONOTONIC, &now);
    int64_t seconds = state->deadline.tv_sec - now.tv_sec;
    int64_t ns = state->deadline.tv_nsec - now.tv_nsec;
    if (ns < 0) { --seconds; ns += 1000000000; }
    if (seconds < 0) seconds = ns = 0;
    state->remaining = (struct linux_timespec){seconds, ns};
    return seconds == 0 && ns == 0;
}

/** @brief Return completed messages, writing the timeout only for positive results. */
static int64_t mmsg_finish(struct task *task, struct task_file *file, int error)
{
    struct task_mmsg_state state = task->mmsg;
    task->mmsg = (struct task_mmsg_state){0};
    if (!state.count) return error;
    if (state.receiving && error && error != -LINUX_EAGAIN)
        task_socket_message_error(file, -error, true);
    if (state.receiving && state.timeout_pointer &&
        user_copy_to_task(task, state.timeout_pointer, &state.remaining, sizeof(state.remaining)) < 0)
        return -LINUX_EFAULT;
    return state.count;
}

/**
 * @brief Run a Linux message batch, preserving the current slot during blocking.
 * @param receiving True for recvmmsg, false for sendmmsg.
 * @param fd Native signed descriptor, pinned by syscall_dispatch_frame.
 * @param vector Native 64-byte mmsghdr array in user memory.
 * @param length Unsigned message count; only sendmmsg caps it at UIO_MAXIOV.
 * @param flags Native unsigned MSG flags.
 * @param timeout recvmmsg timespec pointer; zero for no batch timeout.
 * @return Completed message count, negative errno, or KERNEL_SYSCALL_BLOCKED.
 */
int64_t syscall_socket_mmsg(bool receiving, int fd, uint64_t vector,
                            uint32_t length, uint32_t flags, uint64_t timeout)
{
    struct task *task = sched_current_task();
    if (flags & MSG_CMSG_COMPAT) return -LINUX_EINVAL;
    if (!task) return -LINUX_ESRCH;
    struct task_file *file = task_file_for_io(task, fd);
    struct task_mmsg_state *state = &task->mmsg;
    if (!state->active) {
        struct task_mmsg_state initial = {.active = true, .receiving = receiving,
            .vector = vector, .length = !receiving && length > 1024 ? 1024 : length,
            .flags = flags, .timeout_pointer = receiving ? timeout : 0};
        if (initial.timeout_pointer) {
            if (!user_range_ok(timeout, sizeof(initial.remaining))) return -LINUX_EFAULT;
            __builtin_memcpy(&initial.remaining, (const void *)(uintptr_t)timeout, sizeof(initial.remaining));
            if (initial.remaining.tv_sec < 0 || (uint64_t)initial.remaining.tv_nsec >= 1000000000)
                return -LINUX_EINVAL;
            if (initial.remaining.tv_sec || initial.remaining.tv_nsec) {
                struct linux_timespec now;
                time_clock_get(LINUX_CLOCK_MONOTONIC, &now);
                int64_t carry = (now.tv_nsec + initial.remaining.tv_nsec) / 1000000000;
                if (initial.remaining.tv_sec > INT64_MAX - now.tv_sec - carry)
                    initial.deadline = (struct linux_timespec){INT64_MAX, 999999999};
                else initial.deadline = (struct linux_timespec){
                    now.tv_sec + initial.remaining.tv_sec + carry,
                    (now.tv_nsec + initial.remaining.tv_nsec) % 1000000000};
            }
        }
        if (!file) return -LINUX_EBADF;
        if (!(file->flags & TASK_FILE_FLAG_SOCKET)) return -LINUX_ENOTSOCK;
        if (receiving && !(flags & MSG_ERRQUEUE)) {
            int error = task_socket_message_error(file, 0, false);
            if (error) return -error;
        }
        *state = initial;
    }
    int error = 0;
    while (state->count < state->length) {
        uint64_t offset = (uint64_t)state->count * sizeof(struct mmsghdr);
        if (state->vector > UINT64_MAX - offset - sizeof(struct mmsghdr)) {
            error = -LINUX_EFAULT;
            break;
        }
        uint64_t message = state->vector + offset;
        uint32_t message_flags = state->flags;
        if (receiving) {
            message_flags &= ~MSG_WAITFORONE;
            if (state->count && (state->flags & MSG_WAITFORONE)) message_flags |= MSG_DONTWAIT;
        } else if (state->count + 1 < state->length) message_flags |= MSG_BATCH;
        struct socket_message_result output = {0};
        int64_t result = task_socket_message(task, file, message, message_flags, receiving, true, &output);
        if (result == KERNEL_SYSCALL_BLOCKED) return result;
        if (result < 0) { error = (int)result; break; }
        uint32_t message_length = (uint32_t)result;
        /* The operation precedes this store: EFAULT must not undo sent data,
         * consumed data or transferred SCM_RIGHTS descriptors. */
        uint64_t length_pointer = message + __builtin_offsetof(struct mmsghdr, msg_len);
        if (!user_range_writable(length_pointer, sizeof(message_length))) {
            error = -LINUX_EFAULT;
            break;
        }
        __builtin_memcpy((void *)(uintptr_t)length_pointer, &message_length, sizeof(message_length));
        ++state->count;
        task->socket_io_deadline = 0;
        task->socket_io_timed = false;
        if (!receiving && (uint64_t)result < output.requested) break;
        /* Linux checks this timeout after receiving each message, not while
         * blocked inside recvmsg. SO_RCVTIMEO remains an independent timeout. */
        if (receiving && state->timeout_pointer && mmsg_update_timeout(state)) break;
        if (receiving && (output.flags & MSG_OOB)) break;
    }
    task_socket_cancel_receive(task);
    return mmsg_finish(task, file, error);
}

/**
 * @brief Finish message counts before constructing a signal handler frame.
 * @param task Interrupted task, pinned under the kernel execution lock.
 * @param received Bytes already copied in the current WAITALL receive.
 * @return Batch count or error; all output goes through the target page tables.
 */
int64_t task_socket_mmsg_interrupt(struct task *task, uint64_t received)
{
    struct task_mmsg_state *state = &task->mmsg;
    if (!state->active) return -LINUX_EINTR;
    /* sock_intr_errno distinguishes SO_RCVTIMEO from an infinite wait. */
    int error = task->socket_io_timed ? -LINUX_EINTR : -512;
    if (state->receiving && received) {
        uint64_t message = state->vector + (uint64_t)state->count * sizeof(struct mmsghdr);
        uint32_t flags = task->socket_receive_flags;
        uint64_t no_control = 0;
        if (task->socket_receive_name) {
            uint32_t length = task->socket_receive_path_length;
            uint32_t copied = length < task->socket_receive_name_capacity ? length : task->socket_receive_name_capacity;
            if ((copied && user_copy_to_task(task, task->socket_receive_name, task->socket_receive_path, copied) < 0) ||
                user_copy_to_task(task, message + __builtin_offsetof(struct msghdr, msg_namelen), &length, sizeof(length)) < 0)
                return mmsg_finish(task, task->syscall_file, -LINUX_EFAULT);
        }
        if (user_copy_to_task(task, message + __builtin_offsetof(struct msghdr, msg_flags), &flags, sizeof(flags)) < 0 ||
            user_copy_to_task(task, message + __builtin_offsetof(struct msghdr, msg_controllen), &no_control, sizeof(no_control)) < 0)
            return mmsg_finish(task, task->syscall_file, -LINUX_EFAULT);
        uint64_t pointer = state->vector + (uint64_t)state->count * sizeof(struct mmsghdr) +
            __builtin_offsetof(struct mmsghdr, msg_len);
        uint32_t length = (uint32_t)received;
        if (user_copy_to_task(task, pointer, &length, sizeof(length)) < 0) error = -LINUX_EFAULT;
        else {
            ++state->count;
            if ((state->flags & (MSG_WAITFORONE | MSG_DONTWAIT)) ||
                (task->syscall_file && (task->syscall_file->flags & LEONOS_O_NONBLOCK)))
                error = -LINUX_EAGAIN;
            if (state->timeout_pointer && mmsg_update_timeout(state)) error = 0;
            if (state->count == state->length) error = 0;
        }
    }
    int64_t result = mmsg_finish(task, task->syscall_file, error);
    return result == -512 ? -LINUX_EINTR : result;
}
