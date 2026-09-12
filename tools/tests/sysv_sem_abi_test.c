#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sched.h>
#include <sys/sem.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define SEM_CHECK(expr) do { if (!(expr)) { \
    printf("SysV sem ABI line %d: %s errno=%d\n", __LINE__, #expr, errno); goto fail; \
} } while (0)

static volatile sig_atomic_t sem_alarm_count;
static int sem_alarm_id = -1;
static void sem_alarm_handler(int sig)
{
    (void)sig;
    if (++sem_alarm_count == 1) alarm(1);
    else if (sem_alarm_id >= 0) syscall(SYS_semctl, sem_alarm_id, 0, IPC_RMID, 0L);
}

static int sem_undo_child(void *arg)
{
    int id = *(int *)arg;
    struct sembuf op = {0, 1, SEM_UNDO};
    return syscall(SYS_semop, id, &op, 1) == 0 ? 0 : 1;
}

static int sem_stop_continue(int id, int timed)
{
    pid_t child = fork();
    if (child < 0) return 1;
    if (!child) {
        signal(SIGALRM, SIG_DFL); alarm(3);
        struct sembuf op = {0, -1, 0};
        struct timespec timeout = {10, 0};
        long result = timed ? syscall(SYS_semtimedop, id, &op, 1, &timeout)
                            : syscall(SYS_semop, id, &op, 1);
        _exit(result == -1 && errno == EINTR ? 0 : 1);
    }
    int waiting = 0, status;
    for (unsigned i = 0; i < 2000; ++i) {
        if (syscall(SYS_semctl, id, 0, GETNCNT, 0L) == 1) { waiting = 1; break; }
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    if (!waiting || kill(child, SIGSTOP) || waitpid(child, &status, WUNTRACED) != child ||
        !WIFSTOPPED(status) || syscall(SYS_semctl, id, 0, GETNCNT, 0L) != 0 || kill(child, SIGCONT)) {
        kill(child, SIGKILL); waitpid(child, &status, 0); return 1;
    }
    return waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status);
}

static int sysv_sem_syscalls(void)
{
    int id = -1, restore_signal = 0;
    struct sigaction previous_action, action = {.sa_handler = sem_alarm_handler, .sa_flags = SA_RESTART};
    sigemptyset(&action.sa_mask);
    void *stack = MAP_FAILED;
    struct semid_ds ds;
    struct sembuf ops[3] = {{0, -1, 0}, {1, 1, 0}, {0, 1, 0}};
    struct timespec invalid = {-1, 0}, zero = {0};
    _Static_assert(sizeof(struct semid_ds) == 104, "x86-64 semid_ds");
    _Static_assert(offsetof(struct semid_ds, sem_ctime) == 64, "x86-64 sem_ctime");
    _Static_assert(offsetof(struct semid_ds, sem_nsems) == 80, "x86-64 sem_nsems");
    _Static_assert(sizeof(struct sembuf) == 6, "native sembuf");
    SEM_CHECK(syscall(SYS_semget, IPC_PRIVATE, 0, 0600) == -1 && errno == EINVAL);
    SEM_CHECK(syscall(SYS_semop, -1, (void *)1, 1) == -1 && errno == EFAULT);
    SEM_CHECK(syscall(SYS_semtimedop, -1, (void *)1, 0, (void *)1) == -1 && errno == EFAULT);
    SEM_CHECK(syscall(SYS_semtimedop, -1, (void *)1, 501, &invalid) == -1 && errno == E2BIG);
    SEM_CHECK(syscall(SYS_semtimedop, -1, (void *)1, 1, &invalid) == -1 && errno == EFAULT);
    SEM_CHECK(syscall(SYS_semctl, -1, 0, SETVAL, 65535L) == -1 && errno == EINVAL);
    id = syscall(SYS_semget, IPC_PRIVATE, 2, 0600);
    SEM_CHECK(id >= 0);
    SEM_CHECK(syscall(SYS_semctl, id, -1, IPC_STAT, &ds) == 0 && ds.sem_nsems == 2 && !ds.sem_otime);
    SEM_CHECK(ds.sem_perm.uid == geteuid() && ds.sem_perm.gid == getegid());
    SEM_CHECK(syscall(SYS_semctl, id, 0, IPC_STAT | 0x100, &ds) == -1 && errno == EINVAL);
    SEM_CHECK(syscall(SYS_semctl, id, 0, SETVAL, 1ULL << 32 | 1) == 0);
    SEM_CHECK(syscall(SYS_semctl, id, 1, SETVAL, 32767L) == 0);
    SEM_CHECK(syscall(SYS_semop, id, ops, 2) == -1 && errno == ERANGE);
    SEM_CHECK(syscall(SYS_semctl, id, 0, GETVAL, 0L) == 1);
    SEM_CHECK(syscall(SYS_semctl, id, 1, SETVAL, 0L) == 0);
    SEM_CHECK(syscall(SYS_semop, (1ULL << 32) | (uint32_t)id, ops, (1ULL << 32) | 3) == 0);
    SEM_CHECK(syscall(SYS_semctl, id, 0, GETVAL, 0L) == 1);
    SEM_CHECK(syscall(SYS_semctl, id, 1, GETVAL, 0L) == 1);
    SEM_CHECK(syscall(SYS_semctl, id, 0, GETPID, 0L) == getpid());
    SEM_CHECK(syscall(SYS_semctl, id, 0, IPC_STAT, &ds) == 0 && ds.sem_otime > 0);
    unsigned short values[] = {3, 4}, output[2] = {0};
    SEM_CHECK(syscall(SYS_semctl, id, -1, SETALL, values) == 0);
    SEM_CHECK(syscall(SYS_semctl, id, -1, GETALL, output) == 0 && !memcmp(values, output, sizeof(values)));
    values[1] = 32768;
    SEM_CHECK(syscall(SYS_semctl, id, -1, SETALL, values) == -1 && errno == ERANGE);
    SEM_CHECK(syscall(SYS_semctl, id, 0, GETVAL, 0L) == 3);
    ops[0] = (struct sembuf){0, -4, IPC_NOWAIT};
    SEM_CHECK(syscall(SYS_semop, id, ops, 1) == -1 && errno == EAGAIN);
    ops[0].sem_flg = 0;
    SEM_CHECK(syscall(SYS_semtimedop, id, ops, 1, &zero) == -1 && errno == EAGAIN);
    SEM_CHECK(syscall(SYS_semtimedop, id, ops, 1, &invalid) == -1 && errno == EINVAL);
    ops[0].sem_num = 2;
    SEM_CHECK(syscall(SYS_semop, id, ops, 1) == -1 && errno == EFBIG);
    SEM_CHECK(syscall(SYS_semctl, id, 0, SETVAL, 0L) == 0);
    pid_t child = fork();
    SEM_CHECK(child >= 0);
    if (!child) _exit(sem_undo_child(&id));
    int status;
    SEM_CHECK(waitpid(child, &status, 0) == child && !status);
    SEM_CHECK(syscall(SYS_semctl, id, 0, GETVAL, 0L) == 0);
    stack = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    SEM_CHECK(stack != MAP_FAILED);
    child = clone(sem_undo_child, (char *)stack + 65536, CLONE_SYSVSEM | SIGCHLD, &id);
    SEM_CHECK(child >= 0 && waitpid(child, &status, 0) == child && !status);
    SEM_CHECK(syscall(SYS_semctl, id, 0, GETVAL, 0L) == 1);
    SEM_CHECK(syscall(SYS_semctl, id, 0, SETVAL, 0L) == 0);
    SEM_CHECK(munmap(stack, 65536) == 0); stack = MAP_FAILED;
    SEM_CHECK(sigaction(SIGALRM, &action, &previous_action) == 0); restore_signal = 1;
    sem_alarm_id = id; sem_alarm_count = 0; alarm(1);
    ops[0] = (struct sembuf){0, -1, 0};
    SEM_CHECK(syscall(SYS_semop, id, ops, 1) == -1 && errno == EINTR);
    alarm(0); SEM_CHECK(sem_alarm_count == 1);
    sem_alarm_count = 0; alarm(1);
    struct timespec duration = {10, 123};
    SEM_CHECK(syscall(SYS_semtimedop, id, ops, 1, &duration) == -1 && errno == EINTR);
    alarm(0); SEM_CHECK(sem_alarm_count == 1 && duration.tv_sec == 10 && duration.tv_nsec == 123);
    SEM_CHECK(sigaction(SIGALRM, &previous_action, NULL) == 0); restore_signal = 0; sem_alarm_id = -1;
    SEM_CHECK(sem_stop_continue(id, 0) == 0);
    SEM_CHECK(sem_stop_continue(id, 1) == 0);
    SEM_CHECK(syscall(SYS_semctl, id, 0, IPC_RMID, 0L) == 0);
    SEM_CHECK(syscall(SYS_semctl, id, 0, GETVAL, 0L) == -1 && errno == EINVAL); id = -1;
    puts("PASS raw Linux semaphores: layouts, widths/errors, atomic operations, commands, SEM_UNDO clone/fork, EINTR and stop/continue");
    return 0;
fail:
    if (restore_signal) { alarm(0); sigaction(SIGALRM, &previous_action, NULL); }
    sem_alarm_id = -1;
    if (id >= 0) syscall(SYS_semctl, id, 0, IPC_RMID, 0L);
    if (stack != MAP_FAILED) munmap(stack, 65536);
    return 1;
}

#ifndef SYSV_SEM_EMBEDDED
int main(void) { return sysv_sem_syscalls(); }
#endif
