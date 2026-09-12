#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <leonos/admin.h>
#include <leonos/sudo.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* An existing process cannot acquire identity from a password cache.
 * Relaunch its exact command through sudo; privileged work runs in that process. */
int leonos_admin_elevate(void)
{
    if (geteuid() == 0) return 1;
    size_t size = 256;
    char *path = NULL;
    for (;;) {
        char *grown = realloc(path, size);
        if (!grown) { free(path); return 0; }
        path = grown;
        ssize_t n = readlink("/proc/self/exe", path, size - 1);
        if (n < 0) { free(path); return 0; }
        if ((size_t)n < size - 1) { path[n] = 0; break; }
        if (size > SIZE_MAX / 2) { free(path); errno = E2BIG; return 0; }
        size *= 2;
    }
    int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0) { free(path); return 0; }
    char *data = NULL;
    size_t used = 0;
    for (;;) {
        if (used > SIZE_MAX - 4096) { errno = E2BIG; break; }
        char *grown = realloc(data, used + 4096);
        if (!grown) break;
        data = grown;
        ssize_t n = read(fd, data + used, 4096);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) break;
        if (!n) {
            if (!used || data[used - 1]) { errno = EIO; break; }
            size_t count = 0;
            for (size_t i = 0; i < used; ++i) if (!data[i]) ++count;
            char **args = calloc(count + 1, sizeof(*args));
            if (!args) break;
            size_t index = 0;
            for (size_t i = 0; i < used;) {
                args[index++] = data + i;
                i += strlen(data + i) + 1;
            }
            args[0] = path;
            uint32_t pid;
            if (leonos_sudo_run(NULL, NULL, args, &pid) == 0) errno = EINPROGRESS;
            free(args);
            break;
        }
        used += (size_t)n;
    }
    int error = errno;
    close(fd); free(data); free(path); errno = error;
    return 0;
}
