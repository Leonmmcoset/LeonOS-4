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

#define CHECK(c) do { if (!(c)) { printf("[script-exec] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
static const char *script = "/tmp/script-file", *interpreter = "/tmp/script-interpreter";

static int write_script(const char *contents, mode_t mode)
{
    int fd = open(script, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    CHECK(fd >= 0);
    size_t length = strlen(contents);
    CHECK(write(fd, contents, length) == (ssize_t)length);
    CHECK(fchmod(fd, mode) == 0 && close(fd) == 0);
    return 0;
}

static int run_script(int fd, uid_t caller, uid_t expected)
{
    pid_t child = fork();
    if (!child) {
        char name[100], uid[40];
        snprintf(name, sizeof(name), "EXPECT_SCRIPT=/dev/fd/%d", fd);
        snprintf(uid, sizeof(uid), "EXPECT_UID=%u", expected);
        char *env[] = {fd < 0 ? "EXPECT_SCRIPT=/tmp/script-file" : name,
                       "EXPECT_ARG=first second", uid, NULL};
        char *args[] = {"ignored", "tail", NULL};
        if (setresgid(caller, caller, caller) || setresuid(caller, caller, caller)) _exit(2);
        if (fd < 0) syscall(SYS_execve, script, args, env);
        else syscall(SYS_execveat, fd, "", args, env, AT_EMPTY_PATH);
        dprintf(1, "[script-exec] child exec errno=%d\n", errno);
        _exit(3);
    }
    int status;
    CHECK(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getenv("EXPECT_SCRIPT")) {
        CHECK(argc == 4 && !strcmp(argv[0], interpreter));
        CHECK(!strcmp(argv[1], getenv("EXPECT_ARG")));
        CHECK(!strcmp(argv[2], getenv("EXPECT_SCRIPT")) && !strcmp(argv[3], "tail"));
        CHECK(!strcmp((char *)getauxval(AT_EXECFN), getenv("EXPECT_SCRIPT")));
        CHECK(getuid() == 1000 && geteuid() == (uid_t)atoi(getenv("EXPECT_UID")));
        CHECK(getauxval(AT_SECURE) == (geteuid() != getuid()));
        return 0;
    }
    puts("[script-exec] BEGIN shebang argv, script/interpreter set-ID, recursion and rejection");
    int source = open(argv[0], O_RDONLY), target = open(interpreter, O_WRONLY | O_CREAT | O_EXCL, 0755);
    CHECK(source >= 0 && target >= 0);
    char buffer[4096];
    ssize_t length;
    while ((length = read(source, buffer, sizeof(buffer))) > 0) CHECK(write(target, buffer, length) == length);
    CHECK(length == 0 && close(source) == 0 && close(target) == 0);
    CHECK(write_script("#!  /tmp/script-interpreter\t first second \t\n", 04755) == 0);
    CHECK(run_script(-1, 1000, 1000) == 0);
    CHECK(chmod(interpreter, 04755) == 0);
    CHECK(run_script(-1, 1000, 0) == 0);
    CHECK(chmod(interpreter, 0755) == 0);
    int fd = open(script, O_PATH | O_CLOEXEC);
    char *args[] = {"ignored", NULL};
    CHECK(fd >= 0);
    CHECK(syscall(SYS_execveat, fd, "", args, NULL, AT_EMPTY_PATH) == -1 && errno == ENOENT);
    CHECK(fcntl(fd, F_SETFD, 0) == 0 && run_script(fd, 1000, 1000) == 0 && close(fd) == 0);
    CHECK(write_script("#!/tmp/script-missing\n", 0755) == 0);
    CHECK(syscall(SYS_execve, script, args, NULL) == -1 && errno == ENOENT);
    CHECK(write_script("#! /tmp/script-interpreter\n", 0755) == 0 && chmod(interpreter, 0644) == 0);
    CHECK(syscall(SYS_execve, script, args, NULL) == -1 && errno == EACCES);
    CHECK(chmod(interpreter, 0755) == 0);
    CHECK(write_script("#! \t \n", 0755) == 0);
    CHECK(syscall(SYS_execve, script, args, NULL) == -1 && errno == ENOEXEC);
    CHECK(write_script("#!/tmp/script-file\n", 0755) == 0);
    CHECK(syscall(SYS_execve, script, args, NULL) == -1 && errno == ELOOP);
    CHECK(unlink(script) == 0 && unlink(interpreter) == 0);
    puts("[script-exec] DONE failures=0");
    return 0;
}
