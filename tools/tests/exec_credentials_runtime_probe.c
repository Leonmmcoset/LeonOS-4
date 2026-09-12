#define _GNU_SOURCE
#include <errno.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[exec-credentials] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
static const char *self;
static uint64_t permitted, effective, inheritable;
static int getcaps(void)
{
    struct __user_cap_header_struct h = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct d[2] = {{0}};
    int ret = syscall(SYS_capget, &h, d);
    permitted = d[0].permitted | ((uint64_t)d[1].permitted << 32);
    effective = d[0].effective | ((uint64_t)d[1].effective << 32);
    inheritable = d[0].inheritable | ((uint64_t)d[1].inheritable << 32);
    return ret;
}
static int setcaps(uint64_t p, uint64_t e, uint64_t i)
{
    struct __user_cap_header_struct h = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct d[2] = {
        {.permitted = p, .effective = e, .inheritable = i},
        {.permitted = p >> 32, .effective = e >> 32, .inheritable = i >> 32}};
    return syscall(SYS_capset, &h, d);
}
static int replace(const char *mode)
{
    char *args[] = {(char *)self, "--replaced", (char *)mode, NULL};
    CHECK(syscall(SYS_execve, self, args, NULL) == 0);
    return 1;
}
static int saved_ids(void)
{
    CHECK(syscall(SYS_setresgid, 1000, 1000, 0) == 0);
    CHECK(syscall(SYS_setresuid, 1000, 1000, 0) == 0);
    CHECK(syscall(SYS_setfsuid, 0) == 1000);
    CHECK(syscall(SYS_setfsgid, 0) == 1000);
    CHECK(prctl(PR_GET_DUMPABLE) == 0);
    return replace("saved");
}
static int ambient(void)
{
    CHECK(prctl(PR_SET_KEEPCAPS, 1L) == 0);
    CHECK(syscall(SYS_setresgid, 1000, 1000, 1000) == 0);
    CHECK(syscall(SYS_setresuid, 1000, 1000, 1000) == 0);
    CHECK(getcaps() == 0 && permitted && !effective);
    CHECK(setcaps(1ULL << CAP_KILL, 0, 1ULL << CAP_KILL) == 0);
    CHECK(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_KILL, 0L, 0L) == 0);
    CHECK(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_KILL, 0L, 0L) == 1);
    return replace("ambient");
}
static int bounding(void)
{
    CHECK(prctl(PR_CAPBSET_READ, CAP_CHOWN) == 1);
    CHECK(prctl(PR_CAPBSET_READ, 41L) == -1 && errno == EINVAL);
    CHECK(prctl(PR_CAPBSET_DROP, CAP_CHOWN) == 0);
    CHECK(prctl(PR_CAPBSET_DROP, CAP_CHOWN) == 0);
    CHECK(prctl(PR_CAPBSET_READ, CAP_CHOWN) == 0);
    CHECK(getcaps() == 0 && (permitted & (1ULL << CAP_CHOWN)));
    CHECK(setcaps(permitted, effective, 1ULL << CAP_CHOWN) == -1 && errno == EPERM);
    return replace("bounding");
}
static int securebits(void)
{
    unsigned bits = SECBIT_NOROOT | SECBIT_NOROOT_LOCKED |
                    SECBIT_NO_CAP_AMBIENT_RAISE | SECBIT_NO_CAP_AMBIENT_RAISE_LOCKED;
    CHECK(prctl(PR_SET_SECUREBITS, (unsigned long)bits) == 0);
    CHECK(prctl(PR_GET_SECUREBITS) == bits);
    CHECK(prctl(PR_SET_SECUREBITS, 0L) == -1 && errno == EPERM);
    CHECK(getcaps() == 0 && setcaps(permitted, effective, 1ULL << CAP_KILL) == 0);
    CHECK(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_KILL, 0L, 0L) == -1 && errno == EPERM);
    CHECK(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0L, 1L, 0L) == -1 && errno == EINVAL);
    return replace("securebits");
}
static int nnp(void)
{
    CHECK(prctl(PR_SET_NO_NEW_PRIVS, 1L, 0L, 0L, 0L) == 0);
    CHECK(setcaps(0, 0, 0) == 0);
    return replace("nnp");
}
static int ambient_lower(void)
{
    CHECK(getcaps() == 0 && setcaps(permitted, effective, 1ULL << CAP_KILL) == 0);
    CHECK(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_KILL, 0L, 0L) == 0);
    CHECK(setcaps(permitted, effective, 0) == 0);
    CHECK(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_KILL, 0L, 0L) == 0);
    CHECK(prctl(PR_SET_SECUREBITS, (unsigned long)SECBIT_NO_SETUID_FIXUP) == 0);
    CHECK(syscall(SYS_setresuid, 1000, 1000, 1000) == 0);
    CHECK(getcaps() == 0 && permitted && effective == permitted);
    CHECK(syscall(SYS_setfsuid, 0) == 1000);
    CHECK(syscall(SYS_setfsuid, 1000) == 0);
    uint64_t previous = effective;
    CHECK(getcaps() == 0 && effective == previous);
    return 0;
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    self = argv[0];
    if (argc == 3 && !strcmp(argv[1], "--replaced")) {
        CHECK(getcaps() == 0);
        if (!strcmp(argv[2], "saved") || !strcmp(argv[2], "ambient")) {
            unsigned r, e, s;
            CHECK(syscall(SYS_getresuid, &r, &e, &s) == 0 && r == 1000 && e == 1000 && s == 1000);
            CHECK(syscall(SYS_getresgid, &r, &e, &s) == 0 && r == 1000 && e == 1000 && s == 1000);
            CHECK(syscall(SYS_setfsuid, -1) == 1000 && syscall(SYS_setfsgid, -1) == 1000);
            uint64_t expected = !strcmp(argv[2], "ambient") ? 1ULL << CAP_KILL : 0;
            CHECK(permitted == expected && effective == expected);
            CHECK(getauxval(AT_SECURE) == 0);
            CHECK(prctl(PR_GET_DUMPABLE) == (!strcmp(argv[2], "ambient") ? 1 : 0));
            CHECK(prctl(PR_GET_KEEPCAPS) == 0);
            CHECK(syscall(SYS_setuid, 0) == -1 && errno == EPERM);
        } else if (!strcmp(argv[2], "bounding")) {
            CHECK(!(permitted & (1ULL << CAP_CHOWN)) && effective == permitted);
            CHECK(prctl(PR_CAPBSET_READ, CAP_CHOWN) == 0);
        } else {
            CHECK(!permitted && !effective && getuid() == 0 && geteuid() == 0);
            if (!strcmp(argv[2], "nnp")) CHECK(prctl(PR_GET_NO_NEW_PRIVS, 0L, 0L, 0L, 0L) == 1);
            else CHECK(prctl(PR_GET_SECUREBITS) & SECBIT_NOROOT_LOCKED);
        }
        return 0;
    }
    int failures = 0;
    int (*cases[])(void) = {saved_ids, ambient, bounding, securebits, nnp, ambient_lower};
    const char *names[] = {"saved/fs IDs", "ambient exec", "bounding exec", "securebits exec", "NNP caps", "fixups"};
    puts("[exec-credentials] BEGIN Linux native credential and capability lifecycle");
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        pid_t child = fork();
        if (!child) _exit(cases[i]());
        int status;
        int ok = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status);
        printf("[exec-credentials] %s %s\n", ok ? "PASS" : "FAIL", names[i]);
        failures += !ok;
    }
    printf("[exec-credentials] DONE failures=%d\n", failures);
    return failures != 0;
}
