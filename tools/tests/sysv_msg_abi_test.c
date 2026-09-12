#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Linux v6.12 include/uapi/linux/msg.h; musl does not expose this extension. */
#ifndef MSG_COPY
#define MSG_COPY 040000
#endif

#define SYSV_CHECK(expr) do { if (!(expr)) { \
    printf("SysV msg ABI line %d: %s errno=%d\n", __LINE__, #expr, errno); goto fail; \
} } while (0)

static volatile sig_atomic_t sysv_alarm_count;
static int sysv_alarm_queue = -1;
static void sysv_alarm_handler(int sig)
{
    (void)sig;
    if (++sysv_alarm_count == 1) alarm(1);
    else if (sysv_alarm_queue >= 0) syscall(SYS_msgctl, sysv_alarm_queue, IPC_RMID, NULL);
}

static int sysv_msg_syscalls(void)
{
    int q = -1;
    int restore_signal = 0;
    struct sigaction previous_action, action = {.sa_handler = sysv_alarm_handler, .sa_flags = SA_RESTART};
    sigemptyset(&action.sa_mask);
    void *guard = MAP_FAILED;
    size_t page = sysconf(_SC_PAGESIZE);
    struct { long type; char text[32]; } msg = {7, "original"}, out = {0};
    struct msqid_ds ds;
    _Static_assert(sizeof(struct msqid_ds) == 120, "native msqid_ds");
    _Static_assert(sizeof(struct ipc_perm) == 48, "native ipc_perm");
    SYSV_CHECK(syscall(SYS_msgsnd, -1, (void *)1, -1L, 0) == -1 && errno == EFAULT);
    SYSV_CHECK(syscall(SYS_msgsnd, -1, &msg, -1L, 0) == -1 && errno == EINVAL);
    q = syscall(SYS_msgget, IPC_PRIVATE, 0600);
    SYSV_CHECK(q >= 0);
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_STAT, &ds) == 0 && ds.msg_qbytes == 16384);
    SYSV_CHECK(ds.msg_perm.uid == geteuid() && ds.msg_perm.gid == getegid());
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_STAT | 0x100, &ds) == -1 && errno == EINVAL);
    SYSV_CHECK(syscall(SYS_msgrcv, q, (void *)1, 32L, 0L, IPC_NOWAIT) == -1 && errno == ENOMSG);
    SYSV_CHECK(syscall(SYS_msgsnd, (1ULL << 32) | (uint32_t)q, &msg, 8L, 1ULL << 32) == 0);
    SYSV_CHECK(syscall(SYS_msgrcv, q, &out, 7L, 0L, IPC_NOWAIT) == -1 && errno == E2BIG);
    SYSV_CHECK(syscall(SYS_msgrcv, q, &out, 32L, 0L, MSG_COPY | IPC_NOWAIT) == 8);
    SYSV_CHECK(out.type == 7 && !memcmp(out.text, msg.text, 8));
    SYSV_CHECK(syscall(SYS_msgrcv, q, &out, 7L, 0L, MSG_COPY | MSG_NOERROR | IPC_NOWAIT) == -1 && errno == EINVAL);
    SYSV_CHECK(syscall(SYS_msgrcv, q, (void *)1, 32L, 0L, 0) == -1 && errno == EFAULT);
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_STAT, &ds) == 0 && !ds.msg_qnum);
    SYSV_CHECK(sigaction(SIGALRM, &action, &previous_action) == 0); restore_signal = 1;
    sysv_alarm_queue = q; sysv_alarm_count = 0; alarm(1);
    SYSV_CHECK(syscall(SYS_msgrcv, q, &out, 32L, 0L, 0) == -1 && errno == EINTR);
    alarm(0); SYSV_CHECK(sysv_alarm_count == 1);
    ds.msg_qbytes = 0;
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_SET, &ds) == 0);
    sysv_alarm_count = 0; alarm(1);
    SYSV_CHECK(syscall(SYS_msgsnd, q, &msg, 0L, 0) == -1 && errno == EINTR);
    alarm(0); SYSV_CHECK(sysv_alarm_count == 1);
    SYSV_CHECK(sigaction(SIGALRM, &previous_action, NULL) == 0); restore_signal = 0;
    sysv_alarm_queue = -1;
    ds.msg_qbytes = 16384;
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_SET, &ds) == 0);
    const long types[] = {9, 3, 7, 3, LONG_MAX};
    for (unsigned i = 0; i < 5; ++i) {
        msg.type = types[i]; msg.text[0] = (char)i;
        SYSV_CHECK(syscall(SYS_msgsnd, q, &msg, 1L, 0) == 0);
    }
    SYSV_CHECK(syscall(SYS_msgrcv, q, &out, 32L, -8L, 0) == 1 && out.type == 3 && out.text[0] == 1);
    SYSV_CHECK(syscall(SYS_msgrcv, q, &out, 32L, 3L, MSG_EXCEPT) == 1 && out.type == 9);
    SYSV_CHECK(syscall(SYS_msgrcv, q, &out, 32L, LONG_MIN, 0) == 1 && out.type == 3 && out.text[0] == 3);
    SYSV_CHECK(syscall(SYS_msgrcv, q, &out, 32L, 0L, 0) == 1 && out.type == 7);
    SYSV_CHECK(syscall(SYS_msgrcv, q, &out, 32L, LONG_MIN, 0) == 1 && out.type == LONG_MAX);
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_STAT, &ds) == 0); ds.msg_qbytes = 1;
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_SET, &ds) == 0);
    SYSV_CHECK(syscall(SYS_msgsnd, q, &msg, 0L, IPC_NOWAIT) == 0);
    SYSV_CHECK(syscall(SYS_msgsnd, q, &msg, 0L, IPC_NOWAIT) == -1 && errno == EAGAIN);
    SYSV_CHECK(syscall(SYS_msgrcv, q, &out, 0L, 0L, 0) == 0 && out.type == LONG_MAX);
    /* msgctl_down truncates to int before comparison with the unsigned limit. */
    ds.msg_qbytes = (1ULL << 32) | 16384;
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_SET, &ds) == 0);
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_STAT, &ds) == 0 && ds.msg_qbytes == 16384);
    guard = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    SYSV_CHECK(guard != MAP_FAILED && mprotect((char *)guard + page, page, PROT_NONE) == 0);
    msg.type = 7;
    SYSV_CHECK(syscall(SYS_msgsnd, q, &msg, 32L, 0) == 0);
    char *boundary = (char *)guard + page - 16;
    SYSV_CHECK(syscall(SYS_msgrcv, q, boundary, 32L, 0L, 0) == -1 && errno == EFAULT);
    SYSV_CHECK(*(long *)boundary == 7 && !memcmp(boundary + 8, msg.text, 8));
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_STAT, &ds) == 0 && !ds.msg_qnum);
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_RMID, NULL) == 0);
    SYSV_CHECK(syscall(SYS_msgctl, q, IPC_STAT, &ds) == -1 && errno == EINVAL);
    q = -1;
    SYSV_CHECK(munmap(guard, 2 * page) == 0); guard = MAP_FAILED;
    puts("PASS raw Linux SysV queues: native layout/widths, selection, MSG_COPY, capacity, faults, EINTR and deletion");
    return 0;
fail:
    if (restore_signal) { alarm(0); sigaction(SIGALRM, &previous_action, NULL); }
    sysv_alarm_queue = -1;
    if (q >= 0) syscall(SYS_msgctl, q, IPC_RMID, NULL);
    if (guard != MAP_FAILED) munmap(guard, 2 * page);
    return 1;
}

#ifndef SYSV_MSG_EMBEDDED
int main(void) { return sysv_msg_syscalls(); }
#endif
