#define main existing_suite_main
#include "authd_sudo_test.c"
#undef main
#include <fcntl.h>

int main(void)
{
    int fd = open("/dev/null", O_RDONLY);
    assert(fd >= 0 && dup2(fd, 55) == 55);
    close(fd);
    char *argv[] = {"/bin/sh", "-c", "test ! -e /proc/self/fd/55", NULL};
    uint32_t pid;
    int status;
    assert(authd_sudo_spawn(NULL, argv[0], argv, "/", "tester", getuid(),
                            -1, &pid) == 0);
    assert(waitpid(pid, &status, 0) == (pid_t)pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(55);
    int input[2], output[2], errors[2];
    assert(pipe(input) == 0 && pipe(output) == 0 && pipe(errors) == 0);
    assert(write(input[1], "separate-input\n", 15) == 15);
    close(input[1]);
    struct leonos_authd_run request = {0};
    strcpy(request.cwd, "/tmp");
    strcpy(request.term, "xterm-256color");
    struct authd_run_context context = {
        .stdio = {input[0], output[1], errors[1]}, .request = &request
    };
    char *streams[] = {"sh", "-c",
        "read text; test \"$text\" = separate-input && test \"$PWD\" = /tmp && "
        "test \"$TERM\" = xterm-256color || exit 2; printf stdout; printf stderr >&2", NULL};
    assert(authd_sudo_spawn(&context, streams[0], streams, "/", "tester", getuid(), -1, &pid) == 0);
    close(input[0]); close(output[1]); close(errors[1]);
    assert(waitpid(pid, &status, 0) == (pid_t)pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    char text[16] = {0};
    assert(read(output[0], text, sizeof(text)) == 6 && !memcmp(text, "stdout", 6));
    assert(read(errors[0], text, sizeof(text)) == 6 && !memcmp(text, "stderr", 6));
    close(output[0]); close(errors[0]);
    argv[0] = "/definitely-missing-sudo-command";
    assert(authd_sudo_spawn(NULL, argv[0], argv, "/", "tester", getuid(),
                            -1, &pid) == -1 && errno == ENOENT);
    puts("sudo spawn: fd isolation, three streams, cwd, TERM, PATH and exec errors PASS");
    return 0;
}
