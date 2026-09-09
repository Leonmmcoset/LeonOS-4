#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <leonos/fs.h>
int fixture_readdir(int fd, struct leonos_dir_entry *entry);
int fixture_getpwuid_r(uid_t uid, struct passwd *record, char *buffer, size_t size,
                       struct passwd **result);
#define open fixture_open
#define read fixture_read
#define close fixture_close
#define getpwuid_r fixture_getpwuid_r
#define leonos_readdir fixture_readdir
#include "../../userland/libc/src/procsys.c"
#undef open
#undef read
#undef close
#undef getpwuid_r
#undef leonos_readdir

static unsigned cursor, offset, closes;
static int current_file, missing_status;
uint64_t leonos_uptime_ms(void) { return 123; }
int fixture_open(const char *path, int flags, ...)
{
    (void)flags;
    offset = 0;
    if (!strcmp(path, "/proc")) { cursor = 0; return 0; }
    if (!strcmp(path, "/proc/42/stat")) { current_file = 1; return 1; }
    assert(!strcmp(path, "/proc/42/status"));
    current_file = 2;
    return missing_status ? -1 : 1;
}
ssize_t fixture_read(int fd, void *buffer, size_t size)
{
    assert(fd == 1);
    const char *text = current_file == 1 ? "42 (test) 1 1 1 1 0 0 0 0 0 20 999 1 0\n" :
        "Name:\ttest\nUid:\t1001\t1001\t1001\t1001\nVmRSS:\t1234 kB\n";
    size_t length = strlen(text) - offset;
    if (length > size) length = size;
    if (length > 7) length = 7;
    memcpy(buffer, text + offset, length); offset += length;
    return length;
}
int fixture_close(int fd) { assert(fd == 0 || fd == 1); ++closes; return 0; }
int fixture_readdir(int fd, struct leonos_dir_entry *entry)
{
    assert(fd == 0);
    if (cursor++) return 0;
    strcpy(entry->name, "42");
    return 1;
}
int fixture_getpwuid_r(uid_t uid, struct passwd *record, char *buffer, size_t size,
                       struct passwd **result)
{
    assert(uid == (missing_status ? 999 : 1001) && size >= 6);
    strcpy(buffer, "admin"); record->pw_name = buffer; *result = record;
    return 0;
}
int main(void)
{
    struct leonos_task_info task;
    assert(leonos_task_snapshot(&task, 1, NULL) == 1);
    assert(task.pid == 42 && task.uid == 1001 && task.memory_kib == 1234);
    assert(!strcmp(task.username, "admin") && closes == 3);
    missing_status = 1;
    assert(leonos_task_snapshot(&task, 1, NULL) == 1);
    assert(task.uid == 999 && task.memory_kib == 0 && closes == 5);
    puts("PASS task snapshot: status UID/RSS, account resolution, partial reads and fd cleanup");
}
