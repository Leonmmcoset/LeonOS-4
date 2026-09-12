#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PRCTL_EMBEDDED
#define CHECK(condition) do { if (!(condition)) { \
    printf("[prctl] FAIL line=%d %s errno=%d\n", __LINE__, #condition, errno); \
    return 1; } } while (0)
#endif

static void *prctl_dumpable_worker(void *unused)
{
    (void)unused;
    return syscall(SYS_prctl, PR_GET_DUMPABLE) != 0 ||
           syscall(SYS_prctl, PR_SET_DUMPABLE, 1) != 0 ? (void *)1 : NULL;
}

static int process_prctl(void)
{
    char original[16];
    CHECK(syscall(SYS_prctl, PR_GET_NAME, original) == 0);
    char *memory = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(memory != MAP_FAILED);
    CHECK(mprotect(memory + 4096, 4096, PROT_NONE) == 0);
    memory[4094] = 'x';
    memory[4095] = 0;
    CHECK(syscall(SYS_prctl, PR_SET_NAME, memory + 4094) == 0);
    struct { char name[16]; unsigned canary; } result = {{0}, 0x12345678};
    CHECK(syscall(SYS_prctl, PR_GET_NAME, result.name) == 0 && !strcmp(result.name, "x"));
    CHECK(result.canary == 0x12345678);
    CHECK(syscall(SYS_prctl, PR_SET_NAME, "12345678901234567890") == 0);
    CHECK(syscall(SYS_prctl, PR_GET_NAME, result.name) == 0 && !strcmp(result.name, "123456789012345"));
    CHECK(syscall(SYS_prctl, PR_SET_NAME, (void *)1) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_prctl, PR_GET_NAME, memory + 4094) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_prctl, PR_SET_NAME, original) == 0);
    CHECK(munmap(memory, 8192) == 0);
    long previous = syscall(SYS_prctl, PR_GET_DUMPABLE);
    CHECK(previous == 0 || previous == 1);
    CHECK(syscall(SYS_prctl, PR_SET_DUMPABLE, 0) == 0);
    CHECK(syscall(SYS_prctl, PR_GET_DUMPABLE) == 0);
    CHECK(syscall(SYS_prctl, PR_SET_DUMPABLE, 2) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_prctl, PR_SET_DUMPABLE, 1ULL << 32) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_prctl, PR_GET_DUMPABLE) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(syscall(SYS_prctl, PR_GET_DUMPABLE) != 0 ||
                     syscall(SYS_prctl, PR_SET_DUMPABLE, 1) != 0);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(syscall(SYS_prctl, PR_GET_DUMPABLE) == 0);
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, prctl_dumpable_worker, NULL) == 0);
    void *worker_result = (void *)1;
    CHECK(pthread_join(worker, &worker_result) == 0 && worker_result == NULL);
    CHECK(syscall(SYS_prctl, PR_GET_DUMPABLE) == 1);
    CHECK(syscall(SYS_prctl, PR_SET_DUMPABLE, previous) == 0);
    return 0;
}

#ifndef PRCTL_EMBEDDED
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int result = process_prctl();
    printf("[prctl] %s\n", result ? "FAIL" : "PASS");
    return result;
}
#endif
