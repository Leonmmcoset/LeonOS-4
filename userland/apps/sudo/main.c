/* Preserve historical application paths while using the complete upstream CLI. */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    (void)argc;
    execv("/usr/bin/sudo", argv);
    int error = errno;
    perror("sudo");
    return error == ENOENT ? 127 : 126;
}
