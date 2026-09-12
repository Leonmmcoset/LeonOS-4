#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <limits.h>
#include <linux/capability.h>
#include <linux/prctl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[credentials] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
#define RAW(n, ...) syscall(SYS_##n, __VA_ARGS__)
static const unsigned unchanged = UINT_MAX;
static const char *executable;

static int ids(unsigned r, unsigned e, unsigned s, int group)
{
    unsigned actual[3];
    long result = group ? RAW(getresgid, &actual[0], &actual[1], &actual[2]) :
                          RAW(getresuid, &actual[0], &actual[1], &actual[2]);
    return result == 0 && actual[0] == r && actual[1] == e && actual[2] == s;
}

static int getcaps(struct __user_cap_data_struct data[2])
{
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    return RAW(capget, &header, data);
}

static int setcaps(struct __user_cap_data_struct data[2])
{
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    return RAW(capset, &header, data);
}

static int saved_ids(void)
{
    unsigned groups[] = {701, 700};
    CHECK(RAW(setgroups, 2, groups) == 0);
    CHECK(RAW(setresgid, 200, 201, 202) == 0);
    CHECK(RAW(setresuid, 100, 101, 102) == 0);
    CHECK(ids(100, 101, 102, 0) && ids(200, 201, 202, 1));
    CHECK(RAW(getgroups, 2, groups) == 2 && groups[0] == 700 && groups[1] == 701);
    CHECK(RAW(setgroups, 0, NULL) == -1 && errno == EPERM);
    CHECK(RAW(setreuid, unchanged, 101) == 0 && ids(100, 101, 101, 0));
    CHECK(RAW(setregid, unchanged, 201) == 0 && ids(200, 201, 201, 1));
    CHECK(RAW(setuid, 100) == 0 && ids(100, 100, 101, 0));
    CHECK(RAW(setuid, 101) == 0 && ids(100, 101, 101, 0));
    CHECK(RAW(setuid, 0) == -1 && errno == EPERM);
    CHECK(RAW(setresuid, 0, 0, 0) == -1 && errno == EPERM);
    CHECK(RAW(setresgid, 0, 0, 0) == -1 && errno == EPERM);
    CHECK(RAW(setuid, unchanged) == -1 && errno == EINVAL);
    CHECK(RAW(setgid, unchanged) == -1 && errno == EINVAL);
    struct __user_cap_data_struct caps[2];
    CHECK(getcaps(caps) == 0 && !caps[0].effective && !caps[1].effective &&
          !caps[0].permitted && !caps[1].permitted);
    return 0;
}

static int fs_ids(void)
{
    struct __user_cap_data_struct original[2], current[2];
    CHECK(getcaps(original) == 0 && (original[0].effective & (1u << CAP_SETUID)));
    CHECK(RAW(setfsuid, unchanged) == 0 && RAW(setfsgid, unchanged) == 0);
    CHECK(RAW(setfsuid, 400) == 0 && RAW(setfsgid, 500) == 0);
    CHECK(RAW(setfsuid, unchanged) == 400 && RAW(setfsgid, unchanged) == 500);
    CHECK(getcaps(current) == 0 && !(current[0].effective & (1u << CAP_DAC_OVERRIDE)));
    CHECK(RAW(setresuid, unchanged, unchanged, unchanged) == 0);
    CHECK(RAW(setresgid, unchanged, unchanged, unchanged) == 0);
    CHECK(RAW(setfsuid, unchanged) == 400 && RAW(setfsgid, unchanged) == 500);
    CHECK(RAW(setfsuid, 0) == 400 && RAW(setfsgid, 0) == 500);
    CHECK(getcaps(current) == 0 && current[0].effective == original[0].effective &&
          current[1].effective == original[1].effective);
    return 0;
}

static int capability_drop(void)
{
    struct __user_cap_data_struct original[2], empty[2] = {{0}};
    CHECK(getcaps(original) == 0 && (original[0].permitted & (1u << CAP_SETUID)));
    CHECK(setcaps(empty) == 0);
    CHECK(setcaps(original) == -1 && errno == EPERM);
    CHECK(RAW(setuid, 100) == -1 && errno == EPERM);
    CHECK(RAW(setgid, 100) == -1 && errno == EPERM);
    CHECK(RAW(setgroups, 0, NULL) == -1 && errno == EPERM);
    CHECK(ids(0, 0, 0, 0) && ids(0, 0, 0, 1));
    return 0;
}

static int reboot_capability(void)
{
    /* Invalid magic cannot reboot either kernel. Linux checks capability first. */
    CHECK(RAW(prctl, PR_SET_KEEPCAPS, 1UL, 0UL, 0UL, 0UL) == 0);
    CHECK(RAW(setresuid, 1000, 1000, 1000) == 0);
    struct __user_cap_data_struct caps[2] = {{0}};
    caps[0].effective = caps[0].permitted = 1u << CAP_SYS_BOOT;
    CHECK(setcaps(caps) == 0);
    CHECK(RAW(reboot, 0, 0, 0, NULL) == -1 && errno == EINVAL);
    caps[0].effective = 0;
    CHECK(setcaps(caps) == 0);
    CHECK(RAW(reboot, 0, 0, 0, NULL) == -1 && errno == EPERM);
    return 0;
}

static long nnp(unsigned command, unsigned long value)
{
    return RAW(prctl, command, value, 0UL, 0UL, 0UL);
}

static void *nnp_thread(void *unused)
{
    (void)unused;
    return (void *)(uintptr_t)(nnp(PR_GET_NO_NEW_PRIVS, 0) != 0 ||
        nnp(PR_SET_NO_NEW_PRIVS, 1) != 0 || nnp(PR_GET_NO_NEW_PRIVS, 0) != 1);
}

static int no_new_privileges(void)
{
    CHECK(nnp(PR_GET_NO_NEW_PRIVS, 0) == 0);
    pthread_t thread;
    void *answer;
    CHECK(pthread_create(&thread, NULL, nnp_thread, NULL) == 0);
    CHECK(pthread_join(thread, &answer) == 0 && answer == NULL);
    CHECK(nnp(PR_GET_NO_NEW_PRIVS, 0) == 0);
    for (unsigned index = 0; index < 5; ++index) {
        unsigned long args[] = {1, 0, 0, 0};
        if (index < 4) args[index] = index ? 1 : 0;
        else args[0] = 1UL << 32;
        CHECK(RAW(prctl, PR_SET_NO_NEW_PRIVS, args[0], args[1], args[2], args[3]) == -1 && errno == EINVAL);
        CHECK(nnp(PR_GET_NO_NEW_PRIVS, 0) == 0);
    }
    CHECK(nnp(PR_SET_NO_NEW_PRIVS, 1) == 0 && nnp(PR_GET_NO_NEW_PRIVS, 0) == 1);
    CHECK(nnp(PR_SET_NO_NEW_PRIVS, 0) == -1 && errno == EINVAL);
    for (unsigned index = 0; index < 4; ++index) {
        unsigned long args[] = {0, 0, 0, 0};
        args[index] = 1;
        CHECK(RAW(prctl, PR_GET_NO_NEW_PRIVS, args[0], args[1], args[2], args[3]) == -1 && errno == EINVAL);
    }
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        if (nnp(PR_GET_NO_NEW_PRIVS, 0) != 1) _exit(1);
        execl(executable, executable, "--nnp-exec", (char *)NULL);
        _exit(1);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

static atomic_int ready, proceed;
static int thread_mode;
static void *identity_thread(void *unused)
{
    (void)unused;
    if (!thread_mode && RAW(setresuid, 1100, 1100, 1100) < 0) {
        atomic_store(&ready, -1);
        return (void *)1;
    }
    atomic_store(&ready, 1);
    while (!atomic_load(&proceed)) usleep(1000);
    return (void *)(uintptr_t)(RAW(getuid, 0) != 1100);
}

static int thread_ids(void)
{
    for (thread_mode = 0; thread_mode < 2; ++thread_mode) {
        pthread_t thread;
        atomic_store(&ready, 0);
        atomic_store(&proceed, 0);
        CHECK(pthread_create(&thread, NULL, identity_thread, NULL) == 0);
        while (!atomic_load(&ready)) usleep(1000);
        int status = atomic_load(&ready);
        int own_identity = RAW(getuid, 0);
        int result = thread_mode ? setuid(1100) : 0;
        atomic_store(&proceed, 1);
        void *answer;
        CHECK(pthread_join(thread, &answer) == 0);
        CHECK(status == 1 && own_identity == 0 && result == 0 && answer == NULL);
        CHECK(RAW(getuid, 0) == (thread_mode ? 1100 : 0));
    }
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    executable = argv[0];
    if (argc == 2 && !strcmp(argv[1], "--nnp-exec")) {
        CHECK(nnp(PR_GET_NO_NEW_PRIVS, 0) == 1);
        CHECK(nnp(PR_SET_NO_NEW_PRIVS, 0) == -1 && errno == EINVAL);
        return 0;
    }
    puts("[credentials] BEGIN raw Linux syscall and musl setxid contracts");
    struct { const char *name; int (*run)(void); } cases[] = {
        {"saved IDs and groups", saved_ids}, {"filesystem IDs", fs_ids},
        {"root capability drop", capability_drop}, {"thread credentials", thread_ids},
        {"reboot requires effective CAP_SYS_BOOT", reboot_capability},
        {"no_new_privs thread/fork/exec and arguments", no_new_privileges},
    };
    unsigned failures = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        pid_t child = fork();
        if (child == 0) _exit(cases[i].run());
        int status;
        int ok = child > 0 && waitpid(child, &status, 0) == child &&
                 WIFEXITED(status) && WEXITSTATUS(status) == 0;
        printf("[credentials] %s %s\n", ok ? "PASS" : "FAIL", cases[i].name);
        failures += !ok;
    }
    printf("[credentials] DONE failures=%u\n", failures);
    return failures != 0;
}
