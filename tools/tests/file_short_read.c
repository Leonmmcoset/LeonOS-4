/* Reproduce the current kernel's bounded read on host Linux. */
#include <unistd.h>
#include <sys/syscall.h>
#include <stddef.h>

ssize_t read(int fd, void *buffer, size_t count)
{
    if (count > 32768) count = 32768;
    return syscall(SYS_read, fd, buffer, count);
}
