#ifndef INSTALLER_DIRECTORY_H
#define INSTALLER_DIRECTORY_H
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <leonos/fs.h>

/* Installation payloads can contain thousands of files in one directory. */
static int installer_list_dir(const char *path, struct leonos_dir_entry **entries,
                              uint32_t *count)
{
    DIR *dir = opendir(path);
    uint32_t capacity = 0;
    int error = 0;
    *entries = NULL;
    *count = 0;
    if (!dir) return -errno;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) { error = errno; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (strlen(entry->d_name) >= sizeof((*entries)[0].name)) { error = ENAMETOOLONG; break; }
        if (*count == capacity) {
            if (capacity > UINT32_MAX / 2) { error = EOVERFLOW; break; }
            uint32_t next = capacity ? capacity * 2 : 64;
            void *grown = realloc(*entries, (size_t)next * sizeof(**entries));
            if (!grown) { error = ENOMEM; break; }
            *entries = grown;
            capacity = next;
        }
        struct stat status;
        if (fstatat(dirfd(dir), entry->d_name, &status, 0) < 0) { error = errno; break; }
        struct leonos_dir_entry *out = &(*entries)[(*count)++];
        memset(out, 0, sizeof(*out));
        strcpy(out->name, entry->d_name);
        out->type = S_ISDIR(status.st_mode) ? LEONOS_FS_TYPE_DIR :
                    S_ISREG(status.st_mode) ? LEONOS_FS_TYPE_FILE : LEONOS_FS_TYPE_DEVICE;
    }
    if (closedir(dir) < 0 && !error) error = errno;
    if (error) { free(*entries); *entries = NULL; *count = 0; }
    return -error;
}
#endif
