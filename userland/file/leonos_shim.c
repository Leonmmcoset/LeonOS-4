/* POSIX ABI adapters required by upstream libmagic. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

ssize_t pread(int fd, void *buffer, size_t length, off_t offset)
{
    long previous;
    long result;
    int saved_errno;

    previous = lseek(fd, 0, SEEK_CUR);
    if (previous < 0 || lseek(fd, offset, SEEK_SET) < 0) {
        return -1;
    }
    result = read(fd, buffer, length);
    saved_errno = errno;
    (void)lseek(fd, previous, SEEK_SET);
    errno = saved_errno;
    return (ssize_t)result;
}

/*
 * libmagic normally obtains this helper from compress.c.  LeonOS deliberately
 * leaves that source out because its compression filters require pipes and
 * child processes.  magic.c still uses sread() when it reads a regular file
 * descriptor, for which a single native read has the required semantics.
 *
 * file.h declares sread as file_protected, so this definition must have hidden
 * visibility too; otherwise ld.lld rejects the hidden reference on clean CI
 * builds.
 */
__attribute__((visibility("hidden")))
ssize_t sread(int fd, void *buffer, size_t length, int can_be_pipe)
{
    (void)can_be_pipe;
    return (ssize_t)read(fd, buffer, length);
}

ssize_t readlink(const char *path, char *buffer, size_t capacity)
{
    (void)path;
    (void)buffer;
    (void)capacity;
    errno = EINVAL;
    return -1;
}
