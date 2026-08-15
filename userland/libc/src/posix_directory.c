/* POSIX directory helpers shared by userland ports. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <leonos/fs.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned char directory_entry_type(uint32_t type)
{
    if (type == LEONOS_FS_TYPE_DIR) {
        return DT_DIR;
    }
    if (type == LEONOS_FS_TYPE_DEVICE) {
        return DT_CHR;
    }
    return DT_REG;
}

DIR *opendir(const char *path)
{
    DIR *directory;
    int fd;

    if (!path || !path[0]) {
        errno = EINVAL;
        return NULL;
    }
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        errno = -fd;
        return NULL;
    }
    directory = calloc(1, sizeof(*directory));
    if (!directory) {
        (void)close(fd);
        errno = ENOMEM;
        return NULL;
    }
    directory->fd = fd;
    return directory;
}

struct dirent *readdir(DIR *directory)
{
    struct leonos_dir_entry entry;
    int result;
    size_t index;

    if (!directory) {
        errno = EINVAL;
        return NULL;
    }
    result = leonos_readdir(directory->fd, &entry);
    if (result < 0) {
        errno = -result;
        return NULL;
    }
    if (result == 0) {
        return NULL;
    }
    memset(&directory->dirent, 0, sizeof(directory->dirent));
    directory->dirent.d_type = directory_entry_type(entry.type);
    for (index = 0; index + 1U < sizeof(directory->dirent.d_name) && entry.name[index]; ++index) {
        directory->dirent.d_name[index] = entry.name[index];
    }
    return &directory->dirent;
}

int closedir(DIR *directory)
{
    int result;

    if (!directory) {
        errno = EINVAL;
        return -1;
    }
    result = close(directory->fd);
    free(directory);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return 0;
}

int dirfd(DIR *directory)
{
    if (!directory) {
        errno = EINVAL;
        return -1;
    }
    return directory->fd;
}

void rewinddir(DIR *directory)
{
    if (directory) {
        (void)lseek(directory->fd, 0, SEEK_SET);
    }
}
