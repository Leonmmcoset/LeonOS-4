#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <leonos/sudo.h>

static void run_case(int interrupted, int overflow)
{
    int master, slave;
    assert(openpty(&master, &slave, NULL, NULL, NULL) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(master);
        assert(setsid() >= 0 && ioctl(slave, TIOCSCTTY, 0) == 0);
        /* Password input must use /dev/tty even if stdin is a different file. */
        int nullfd = open("/dev/null", O_RDONLY);
        assert(dup2(nullfd, 0) == 0);
        char password[8];
        int result = leonos_read_password("PROMPT:", password, sizeof(password));
        if (overflow) assert(result == -1 && errno == EOVERFLOW && password[0] == 0);
        else assert(result == 0 && !strcmp(password, "secret"));
        _exit(0);
    }
    char output[256] = {0};
    size_t used = 0;
    while (!strstr(output, "PROMPT:")) {
        struct pollfd p = {.fd = master, .events = POLLIN};
        assert(poll(&p, 1, 5000) > 0);
        ssize_t n = read(master, output + used, sizeof(output) - used - 1);
        assert(n > 0);
        used += (size_t)n;
    }
    if (interrupted) assert(kill(child, SIGINT) == 0);
    else {
        const char *input = overflow ? "toolong-secret\n" : "secret\n";
        assert(write(master, input, strlen(input)) == (ssize_t)strlen(input));
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    if (interrupted) assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGINT);
    else assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    struct termios attributes;
    assert(tcgetattr(slave, &attributes) == 0 && (attributes.c_lflag & ECHO));
    close(master); close(slave);
}

int main(void)
{
    run_case(0, 0);
    run_case(0, 1);
    run_case(1, 0);
    puts("sudo password: controlling tty, overflow rejection and SIGINT echo restoration PASS");
    return 0;
}
