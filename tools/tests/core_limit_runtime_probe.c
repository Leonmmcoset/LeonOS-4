#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "[core-limit] FAIL line=%d errno=%d: %s\n", \
    __LINE__, errno, #c); exit(1); } } while (0)

static struct rlimit current(void)
{
    struct rlimit limit;
    CHECK(syscall(SYS_getrlimit, RLIMIT_CORE, &limit) == 0);
    return limit;
}

static void *change_thread(void *argument)
{
    (void)argument;
    struct rlimit limit = {2048, 4096};
    CHECK(syscall(SYS_setrlimit, RLIMIT_CORE, &limit) == 0);
    return NULL;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    struct rlimit limit = current();
    if (argc == 2 && !strcmp(argv[1], "--sudo-child")) {
        CHECK(getuid() == 0 && geteuid() == 0);
        CHECK(limit.rlim_cur == 0 && limit.rlim_max == 0);
        puts("CORE_SUDO_OK uid=0 core=0,0");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--exec-child")) {
        CHECK(limit.rlim_cur == 512 && limit.rlim_max == 1024);
        puts("[core-limit] exec inheritance PASS");
        return 0;
    }
    CHECK(geteuid() != 0); /* Test unprivileged hard-limit enforcement. */
    CHECK(limit.rlim_max >= 4096);
    struct rlimit old, next = {1024, 4096};
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_CORE, &next, &old) == 0);
    CHECK(old.rlim_cur == limit.rlim_cur && old.rlim_max == limit.rlim_max);
    next = (struct rlimit){4097, 4096};
    CHECK(syscall(SYS_setrlimit, RLIMIT_CORE, &next) == -1 && errno == EINVAL);
    next = (struct rlimit){1024, 4097};
    CHECK(syscall(SYS_setrlimit, RLIMIT_CORE, &next) == -1 && errno == EPERM);
    CHECK(syscall(SYS_setrlimit, RLIMIT_CORE, NULL) == -1 && errno == EFAULT);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, change_thread, NULL) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    limit = current();
    CHECK(limit.rlim_cur == 2048 && limit.rlim_max == 4096);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        limit = current();
        CHECK(limit.rlim_cur == 2048 && limit.rlim_max == 4096);
        next = (struct rlimit){512, 1024};
        CHECK(syscall(SYS_setrlimit, RLIMIT_CORE, &next) == 0);
        execl(argv[0], argv[0], "--exec-child", (char *)NULL);
        _exit(127);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    limit = current();
    CHECK(limit.rlim_cur == 2048 && limit.rlim_max == 4096);
    puts("[core-limit] DONE failures=0");
    return 0;
}
