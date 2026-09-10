#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "../../userland/apps/installer/installer_directory.h"

int main(void)
{
    char path[] = "/tmp/installer-directory-XXXXXX";
    assert(mkdtemp(path));
    int dir = open(path, O_DIRECTORY | O_RDONLY);
    assert(dir >= 0);
    for (int i = 0; i < 800; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "header-%04d.h", i);
        int fd = openat(dir, name, O_CREAT | O_WRONLY, 0644);
        assert(fd >= 0 && !close(fd));
    }
    struct leonos_dir_entry *entries = NULL;
    uint32_t count = 0;
    assert(installer_list_dir(path, &entries, &count) == 0 && count == 800);
    for (uint32_t i = 0; i < count; ++i) {
        assert(entries[i].type == LEONOS_FS_TYPE_FILE);
        assert(!unlinkat(dir, entries[i].name, 0));
    }
    free(entries);
    assert(!close(dir) && !rmdir(path));
    assert(installer_list_dir(path, &entries, &count) == -ENOENT && !entries && !count);
    puts("PASS installer directory enumeration: 800 files and missing-directory error");
}
