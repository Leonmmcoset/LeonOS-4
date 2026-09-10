#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define LINK_CHECK(expr) do { if (!(expr)) { \
    printf("symlink ABI: line %d: %s errno=%d\n", __LINE__, #expr, errno); return 1; \
} } while (0)

static int symlink_syscalls(void)
{
    char directory[] = "/tmp/linux-symlink-XXXXXX", path[128], text[1100];
    struct stat link_stat, file_stat;
    LINK_CHECK(mkdtemp(directory));
    int dirfd = syscall(SYS_openat, AT_FDCWD, directory, O_RDONLY | O_DIRECTORY, 0);
    LINK_CHECK(dirfd >= 0);
    int fd = syscall(SYS_openat, dirfd, "target", O_CREAT | O_EXCL | O_RDWR, 0600);
    LINK_CHECK(fd >= 0 && write(fd, "data", 4) == 4);
    LINK_CHECK(syscall(SYS_fstat, fd, &file_stat) == 0);
    mode_t mask = syscall(SYS_umask, 0777);
    LINK_CHECK(syscall(SYS_symlinkat, "target", dirfd, "link") == 0);
    syscall(SYS_umask, mask);
    LINK_CHECK(syscall(SYS_newfstatat, dirfd, "link", &link_stat, AT_SYMLINK_NOFOLLOW) == 0);
    LINK_CHECK(S_ISLNK(link_stat.st_mode) && (link_stat.st_mode & 0777) == 0777 && link_stat.st_size == 6);
    LINK_CHECK(link_stat.st_ino != file_stat.st_ino);
    LINK_CHECK(syscall(SYS_newfstatat, dirfd, "link", &link_stat, 0) == 0 && link_stat.st_ino == file_stat.st_ino);
    memset(text, '#', sizeof(text));
    LINK_CHECK(syscall(SYS_readlinkat, dirfd, "link", text, 3) == 3);
    LINK_CHECK(!memcmp(text, "tar#", 4));
    LINK_CHECK(syscall(SYS_readlinkat, dirfd, "link", text, 0) == -1 && errno == EINVAL);
    LINK_CHECK(syscall(SYS_readlinkat, dirfd, "target", text, sizeof(text)) == -1 && errno == EINVAL);
    LINK_CHECK(syscall(SYS_symlinkat, "", dirfd, "empty") == -1 && errno == ENOENT);
    LINK_CHECK(syscall(SYS_symlinkat, "target", -1, "relative") == -1 && errno == EBADF);
    LINK_CHECK(syscall(SYS_symlinkat, "target", fd, "relative") == -1 && errno == ENOTDIR);
    LINK_CHECK(syscall(SYS_symlinkat, "missing", dirfd, "dangling") == 0);
    LINK_CHECK(syscall(SYS_symlinkat, "other", dirfd, "dangling") == -1 && errno == EEXIST);
    LINK_CHECK(syscall(SYS_openat, dirfd, "dangling", O_CREAT | O_EXCL | O_RDWR, 0600) == -1 && errno == EEXIST);
    LINK_CHECK(syscall(SYS_openat, dirfd, "link", O_RDONLY | O_NOFOLLOW, 0) == -1 && errno == ELOOP);
    LINK_CHECK(syscall(SYS_linkat, dirfd, "link", dirfd, "hard-link", 0) == 0);
    LINK_CHECK(syscall(SYS_newfstatat, dirfd, "hard-link", &link_stat, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(link_stat.st_mode));
    LINK_CHECK(syscall(SYS_linkat, dirfd, "link", dirfd, "hard-target", AT_SYMLINK_FOLLOW) == 0);
    LINK_CHECK(syscall(SYS_newfstatat, dirfd, "hard-target", &link_stat, AT_SYMLINK_NOFOLLOW) == 0 && link_stat.st_ino == file_stat.st_ino);
    LINK_CHECK(syscall(SYS_mkdirat, dirfd, "child", 0700) == 0);
    LINK_CHECK(syscall(SYS_symlinkat, "child", dirfd, "jump") == 0);
    int via_parent = syscall(SYS_openat, dirfd, "jump/../target", O_RDONLY, 0);
    LINK_CHECK(via_parent >= 0 && read(via_parent, text, 4) == 4 && !memcmp(text, "data", 4));
    LINK_CHECK(close(via_parent) == 0);
    LINK_CHECK(syscall(SYS_unlinkat, dirfd, "jump/", 0) == -1 && errno == ENOTDIR);
    LINK_CHECK(syscall(SYS_newfstatat, dirfd, "child", &link_stat, 0) == 0 && S_ISDIR(link_stat.st_mode));
    LINK_CHECK(syscall(SYS_symlinkat, "loop", dirfd, "loop") == 0);
    LINK_CHECK(syscall(SYS_openat, dirfd, "loop", O_RDONLY, 0) == -1 && errno == ELOOP);
    LINK_CHECK(syscall(SYS_renameat, dirfd, "link", dirfd, "renamed") == 0);
    LINK_CHECK(syscall(SYS_unlinkat, dirfd, "renamed", 0) == 0);
    LINK_CHECK(syscall(SYS_readlinkat, dirfd, "hard-link", text, sizeof(text)) == 6);
    LINK_CHECK(!memcmp(text, "target", 6));
    snprintf(path, sizeof(path), "%s/absolute", directory);
    LINK_CHECK(syscall(SYS_symlink, "target", path) == 0);
    LINK_CHECK(syscall(SYS_lstat, path, &link_stat) == 0 && S_ISLNK(link_stat.st_mode));
    LINK_CHECK(syscall(SYS_stat, path, &link_stat) == 0 && link_stat.st_ino == file_stat.st_ino);
    LINK_CHECK(syscall(SYS_readlink, path, text, sizeof(text)) == 6 && !memcmp(text, "target", 6));
    for (unsigned length = 59; length <= 61; ++length) {
        memset(text, 't', length);
        text[length] = 0;
        LINK_CHECK(syscall(SYS_symlinkat, text, dirfd, "long-link") == 0);
        memset(text, '#', sizeof(text));
        LINK_CHECK(syscall(SYS_readlinkat, dirfd, "long-link", text, sizeof(text)) == length);
        LINK_CHECK(text[length] == '#');
        for (unsigned i = 0; i < length; ++i) LINK_CHECK(text[i] == 't');
        LINK_CHECK(syscall(SYS_unlinkat, dirfd, "long-link", 0) == 0);
    }
    const char *names[] = {"target", "hard-link", "hard-target", "dangling", "jump", "loop", "absolute"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        LINK_CHECK(syscall(SYS_unlinkat, dirfd, names[i], 0) == 0);
    LINK_CHECK(syscall(SYS_unlinkat, dirfd, "child", AT_REMOVEDIR) == 0);
    LINK_CHECK(close(fd) == 0 && close(dirfd) == 0);
    LINK_CHECK(rmdir(directory) == 0);
    puts("PASS raw Linux symlink/symlinkat/readlink, follow flags, metadata and lifetime");
    return 0;
}

#ifndef SYMLINK_EMBEDDED
int main(void) { return symlink_syscalls(); }
#endif
