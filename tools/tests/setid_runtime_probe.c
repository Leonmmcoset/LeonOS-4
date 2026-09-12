#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef SECURE_LOADER_TEST
#include <dlfcn.h>
extern int secure_loader_dependency(void);
#endif

#define CHECK(c) do { if (!(c)) { printf("[setid] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
static const char *binary = "/tmp/setid-probe";
static int report(const char *kind)
{
    unsigned r, e, s;
    int elevated = !strcmp(kind, "elevated");
    CHECK(syscall(SYS_getresuid, &r, &e, &s) == 0 && r == 1000 && e == (elevated ? 0 : 1000) && s == e);
    CHECK(syscall(SYS_setfsuid, -1) == e);
    CHECK(syscall(SYS_getresgid, &r, &e, &s) == 0 && r == 1000 && e == (elevated ? 123 : 1000) && s == e);
    CHECK(syscall(SYS_setfsgid, -1) == e);
    CHECK(getauxval(AT_UID) == 1000 && getauxval(AT_EUID) == (elevated ? 0 : 1000));
    CHECK(getauxval(AT_GID) == 1000 && getauxval(AT_EGID) == (elevated ? 123 : 1000));
    CHECK(getauxval(AT_SECURE) == elevated && prctl(PR_GET_DUMPABLE) == !elevated);
    unsigned group;
    CHECK(syscall(SYS_getgroups, 1, &group) == 1 && group == 456);
    struct __user_cap_header_struct h = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct d[2] = {{0}};
    CHECK(syscall(SYS_capget, &h, d) == 0);
    CHECK(!!(d[0].effective & (1U << CAP_SETUID)) == elevated);
#ifdef SECURE_LOADER_TEST
    CHECK(!!dlsym(RTLD_DEFAULT, "hostile_preloaded") == !elevated);
    CHECK(secure_loader_dependency() == (elevated ? 1 : 2));
#endif
    CHECK(syscall(SYS_setresuid, 1000, 1000, 1000) == 0);
    CHECK(syscall(SYS_setuid, 0) == -1 && errno == EPERM);
    return 0;
}
static int execute(const char *kind)
{
    unsigned group = 456;
    CHECK(syscall(SYS_setgroups, 1, &group) == 0);
    CHECK(syscall(SYS_setresgid, 1000, 1000, 1000) == 0);
    CHECK(syscall(SYS_setresuid, 1000, 1000, 1000) == 0);
    char *args[] = {(char *)binary, "--report", (char *)kind, NULL};
    char *environment[] = {"LD_PRELOAD=/tmp/hostile-preload.so", "LD_LIBRARY_PATH=/tmp", NULL};
    CHECK(syscall(SYS_execve, binary, args, environment) == 0);
    return 1;
}
static int normal(void) { return execute("elevated"); }
static int nnp(void)
{
    CHECK(prctl(PR_SET_NO_NEW_PRIVS, 1L, 0L, 0L, 0L) == 0);
    return execute("suppressed");
}
static int nosuid(void)
{
    CHECK(mount(NULL, "/", NULL, MS_REMOUNT | MS_NOSUID, NULL) == 0);
    return execute("suppressed");
}
static int noexec(void)
{
    CHECK(mount(NULL, "/", NULL, MS_REMOUNT | MS_NOEXEC, NULL) == 0);
    char *args[] = {(char *)binary, NULL};
    CHECK(syscall(SYS_execve, binary, args, NULL) == -1 && errno == EACCES);
    return 0;
}
static int denied_mount(void)
{
    CHECK(setuid(1000) == 0);
    CHECK(mount(NULL, "/", NULL, MS_REMOUNT | MS_NOSUID, NULL) == -1 && errno == EPERM);
    return 0;
}
static int copy_self(const char *self)
{
    int source = open(self, O_RDONLY), target = open(binary, O_WRONLY | O_CREAT | O_EXCL, 0700);
    CHECK(source >= 0 && target >= 0);
    char buffer[8192];
    ssize_t got;
    while ((got = read(source, buffer, sizeof(buffer))) > 0) {
        size_t done = 0;
        while (done < (size_t)got) {
            ssize_t written = write(target, buffer + done, got - done);
            CHECK(written > 0);
            done += written;
        }
    }
    CHECK(got == 0 && close(source) == 0 && close(target) == 0);
    CHECK(chown(binary, 0, 123) == 0 && chmod(binary, 06755) == 0);
    return 0;
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3 && !strcmp(argv[1], "--report")) return report(argv[2]);
    puts("[setid] BEGIN real ELF, saved/fs IDs, auxv, NNP and mount enforcement");
    CHECK(copy_self(argv[0]) == 0);
    int failures = 0;
    int (*cases[])(void) = {normal, nnp, nosuid, noexec, denied_mount};
    const char *names[] = {"set-ID ELF", "NNP", "nosuid", "noexec", "mount privilege"};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        pid_t child = fork();
        if (!child) _exit(cases[i]());
        int status;
        int ok = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status);
        if (i == 2 || i == 3) CHECK(mount(NULL, "/", NULL, MS_REMOUNT, NULL) == 0);
        printf("[setid] %s %s\n", ok ? "PASS" : "FAIL", names[i]);
        failures += !ok;
    }
    CHECK(unlink(binary) == 0);
    printf("[setid] DONE failures=%d\n", failures);
    return failures != 0;
}
