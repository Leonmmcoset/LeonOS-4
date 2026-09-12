#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[fd-exec] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
static const char *self;
static int descriptor;
static int use_libc;
static int execute(void)
{
    char number[24], filename[80];
    snprintf(number, sizeof(number), "%d", descriptor);
    snprintf(filename, sizeof(filename), "/dev/fd/%d", descriptor);
    char *args[] = {"fd-executed", "--check", number, filename, NULL};
    char *env[] = {NULL};
    if (use_libc) fexecve(descriptor, args, env);
    else syscall(SYS_execveat, descriptor, "", args, env, AT_EMPTY_PATH);
    return 1;
}
static int empty_vector(void)
{
    char *env[] = {"EMPTY_VECTOR=1", NULL};
    syscall(SYS_execve, self, NULL, env);
    return 1;
}
static int privileged(void)
{
    CHECK(setresgid(1000, 1000, 1000) == 0);
    CHECK(setresuid(1000, 1000, 1000) == 0);
    char *args[] = {"fd-executed", "--setid", NULL};
    syscall(SYS_execveat, descriptor, "", args, NULL, AT_EMPTY_PATH);
    return 1;
}
static int run(int (*test)(void))
{
    pid_t child = fork();
    if (!child) _exit(test());
    int status;
    CHECK(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    return 0;
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getenv("EMPTY_VECTOR")) {
        CHECK(argc == 1 && !argv[0][0] && !argv[1]);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--setid")) {
        uid_t real, effective, saved;
        CHECK(getresuid(&real, &effective, &saved) == 0);
        CHECK(real == 1000 && effective == 0 && saved == 0);
        CHECK(getauxval(AT_SECURE) == 1);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--check")) {
        CHECK(!strcmp(argv[0], "fd-executed"));
        CHECK(!strcmp((char *)getauxval(AT_EXECFN), argv[3]));
        CHECK(fcntl(atoi(argv[2]), F_GETFD) == -1 && errno == EBADF);
        return 0;
    }
    self = argv[0];
    puts("[fd-exec] BEGIN held unlinked ELF, O_PATH, CLOEXEC, AT_EXECFN and permissions");
    CHECK(run(empty_vector) == 0);
    int source = open(self, O_RDONLY), target = open("/tmp/fd-executable", O_CREAT | O_EXCL | O_WRONLY, 0755);
    CHECK(source >= 0 && target >= 0);
    char buffer[4096];
    ssize_t length;
    while ((length = read(source, buffer, sizeof(buffer))) > 0) CHECK(write(target, buffer, length) == length);
    CHECK(length == 0 && close(source) == 0 && close(target) == 0);
    descriptor = open("/tmp/fd-executable", O_PATH | O_CLOEXEC);
    CHECK(descriptor >= 0);
    char *args[] = {(char *)self, NULL};
    CHECK(chmod("/tmp/fd-executable", 0600) == 0);
    CHECK(syscall(SYS_execveat, descriptor, "", args, NULL, AT_EMPTY_PATH) == -1 && errno == EACCES);
    CHECK(chmod("/tmp/fd-executable", 04755) == 0);
    CHECK(unlink("/tmp/fd-executable") == 0);
    CHECK(run(privileged) == 0);
    CHECK(run(execute) == 0);
    use_libc = 1;
    CHECK(run(execute) == 0);
    CHECK(close(descriptor) == 0);
    descriptor = open(self, O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0 && run(execute) == 0 && close(descriptor) == 0);
    CHECK(syscall(SYS_execveat, -1, "", args, NULL, AT_EMPTY_PATH) == -1 && errno == EBADF);
    CHECK(syscall(SYS_execveat, -1, "", args, NULL, 0) == -1 && errno == ENOENT);
    CHECK(syscall(SYS_execveat, -1, self, args, NULL, 1) == -1 && errno == EINVAL);
    CHECK(symlink(self, "/tmp/fd-exec-link") == 0);
    CHECK(syscall(SYS_execveat, AT_FDCWD, "/tmp/fd-exec-link", args, NULL, AT_SYMLINK_NOFOLLOW) == -1 && errno == ELOOP);
    CHECK(unlink("/tmp/fd-exec-link") == 0);
    puts("[fd-exec] DONE failures=0");
    return 0;
}
