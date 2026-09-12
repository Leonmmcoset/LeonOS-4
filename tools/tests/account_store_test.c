#define _GNU_SOURCE
#include "../../userland/auth/account_store.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *names[] = {"passwd", "shadow", "group", "gshadow"};
static const char *old[] = {"root:x:0:0::/root:/bin/sh\n", "root:!:::::::\n",
                            "root:x:0:\n", "root:!::\n"};
static const char *next[] = {"root:x:0:0::/root:/bin/sh\nu:x:1000:1000::/home/u:/bin/sh\n",
    "root:!:::::::\nu:!:::::::\n", "root:x:0:\nu:x:1000:\n", "root:!::\nu:!::\n"};
static unsigned crash_at, operations;
static int fail_write, short_write, interrupt_write, fail_sync;
int __real_fsync(int fd);
ssize_t __real_write(int fd, const void *data, size_t size);
int __real_renameat(int olddir, const char *oldname, int newdir, const char *newname);

static void checkpoint(void)
{
    if (crash_at && ++operations == crash_at) _exit(99);
}
int __wrap_fsync(int fd)
{
    if (fail_sync) { fail_sync = 0; errno = EIO; return -1; }
    int result = __real_fsync(fd);
    if (!result) checkpoint();
    return result;
}
ssize_t __wrap_write(int fd, const void *data, size_t size)
{
    if (fail_write) { fail_write = 0; errno = ENOSPC; return -1; }
    if (interrupt_write) { interrupt_write = 0; errno = EINTR; return -1; }
    return __real_write(fd, data, short_write && size > 3 ? 3 : size);
}
int __wrap_renameat(int olddir, const char *oldname, int newdir, const char *newname)
{
    int result = __real_renameat(olddir, oldname, newdir, newname);
    if (!result) checkpoint();
    return result;
}

static int matches(int directory, const char *const contents[4])
{
    for (unsigned i = 0; i < 4; ++i) {
        int fd = openat(directory, names[i], O_RDONLY | O_NOFOLLOW);
        assert(fd >= 0);
        struct stat st;
        assert(fstat(fd, &st) == 0 && st.st_uid == 0);
        assert((st.st_mode & 0777) == (i % 2 ? 0600 : 0644));
        char text[256];
        ssize_t size = read(fd, text, sizeof(text));
        assert(close(fd) == 0 && size >= 0);
        if ((size_t)size != strlen(contents[i]) || memcmp(text, contents[i], (size_t)size)) return 0;
    }
    return 1;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(geteuid() == 0);
    puts("[account-store] BEGIN real lock, publication and crash recovery");
    char path[] = "/tmp/account-store-XXXXXX";
    assert(mkdtemp(path));
    int directory = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(directory >= 0);
    assert(leonos_account_store_recover(directory) == 0);
    assert(leonos_account_store_commit(directory, old) == 0 && matches(directory, old));
    fail_write = 1;
    assert(leonos_account_store_commit(directory, next) == -1 && errno == ENOSPC);
    assert(leonos_account_store_recover(directory) == 0 && matches(directory, old));
    fail_sync = 1;
    assert(leonos_account_store_commit(directory, next) == -1 && errno == EIO);
    assert(leonos_account_store_recover(directory) == 0 && matches(directory, old));
    short_write = interrupt_write = 1;
    assert(leonos_account_store_commit(directory, next) == 0 && matches(directory, next));
    short_write = 0;
    for (unsigned point = 1; point <= 24; ++point) {
        assert(leonos_account_store_commit(directory, old) == 0);
        pid_t child = fork();
        assert(child >= 0);
        if (!child) {
            crash_at = point;
            operations = 0;
            _exit(leonos_account_store_commit(directory, next) ? 1 : 0);
        }
        int status;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
        assert(WEXITSTATUS(status) == 99 || WEXITSTATUS(status) == 0);
        assert(leonos_account_store_recover(directory) == 0);
        assert(matches(directory, old) || matches(directory, next));
        assert(leonos_account_store_recover(directory) == 0);
    }
    pid_t workers[4];
    for (unsigned i = 0; i < 4; ++i) {
        workers[i] = fork();
        assert(workers[i] >= 0);
        if (!workers[i]) {
            for (unsigned n = 0; n < 8; ++n)
                if (leonos_account_store_commit(directory, (i + n) % 2 ? old : next)) _exit(1);
            _exit(0);
        }
    }
    for (unsigned i = 0; i < 4; ++i) {
        int status;
        assert(waitpid(workers[i], &status, 0) == workers[i] && WIFEXITED(status) && !WEXITSTATUS(status));
    }
    assert(leonos_account_store_recover(directory) == 0);
    assert(matches(directory, old) || matches(directory, next));
    assert(fchmod(directory, 0777) == 0);
    assert(leonos_account_store_commit(directory, next) == -1 && errno == EACCES);
    assert(fchmod(directory, 0700) == 0);
    assert(unlinkat(directory, ".pwd.lock", 0) == 0);
    assert(symlinkat("shadow", directory, ".pwd.lock") == 0);
    assert(leonos_account_store_commit(directory, next) == -1 && errno == ELOOP);
    assert(unlinkat(directory, ".pwd.lock", 0) == 0);
    for (unsigned i = 0; i < 4; ++i) assert(unlinkat(directory, names[i], 0) == 0);
    assert(close(directory) == 0 && rmdir(path) == 0);
    puts("[account-store] DONE failures=0");
    return 0;
}
