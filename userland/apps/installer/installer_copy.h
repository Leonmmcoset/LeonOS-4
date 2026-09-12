#ifndef INSTALLER_COPY_H
#define INSTALLER_COPY_H

#include <errno.h>
#include <sched.h>
#include <unistd.h>

static ssize_t installer_read_chunk(int fd, void *buffer, size_t size)
{
    size_t used = 0;
    while (used < size) {
        ssize_t got = read(fd, (unsigned char *)buffer + used, size - used);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) return -1;
        if (!got) break;
        used += (size_t)got;
        if (used < size) (void)sched_yield();
    }
    return (ssize_t)used;
}

#endif
