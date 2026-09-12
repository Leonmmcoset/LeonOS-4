/* Preserve historical application paths while using the complete upstream CLI. */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    (void)argc;
    execv("/bin/su", argv);
    int error = errno;
    perror("su");
    return error == ENOENT ? 127 : 126;
}
