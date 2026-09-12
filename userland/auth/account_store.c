#define _GNU_SOURCE
#include "account_store.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *names[] = {"passwd", "shadow", "group", "gshadow"};
static const char prepared[] = ".leonos-account-prepared";
static const char committed[] = ".leonos-account-committed";
static const char garbage[] = ".leonos-account-cleanup";
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int trusted(int fd, int directory)
{
    struct stat st;
    if (fstat(fd, &st) < 0) return -1;
    if (st.st_uid || (st.st_mode & 0022) ||
        (directory ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode))) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

static int open_directory(int parent, const char *name)
{
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0 && trusted(fd, 1) < 0) {
        int error = errno;
        close(fd);
        errno = error;
        return -1;
    }
    return fd;
}

static int lock_store(int directory)
{
    if (geteuid() != 0 || trusted(directory, 1) < 0) { errno = EACCES; return -1; }
    int fd = openat(directory, ".pwd.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    if (trusted(fd, 0) < 0) goto failed;
    struct stat st;
    if (fstat(fd, &st) < 0) goto failed;
    if (st.st_nlink != 1) { errno = EACCES; goto failed; }
    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) goto failed;
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    while (fcntl(fd, F_SETLK, &lock) < 0) {
        if (errno != EACCES && errno != EAGAIN && errno != EINTR) goto failed;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) goto failed;
        if (now.tv_sec - start.tv_sec >= 15) { errno = EAGAIN; goto failed; }
        struct timespec delay = {.tv_nsec = 10000000};
        if (nanosleep(&delay, NULL) < 0 && errno != EINTR) goto failed;
    }
    return fd;
failed:;
    int error = errno;
    close(fd);
    errno = error;
    return -1;
}

static int remove_stage(int parent, const char *name)
{
    int fd = open_directory(parent, name);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    int result = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (unlinkat(fd, names[i], 0) < 0 && errno != ENOENT) { result = -1; break; }
    if (!result && fsync(fd) < 0) result = -1;
    int error = errno;
    if (close(fd) < 0 && !result) return -1;
    if (result) { errno = error; return -1; }
    if (unlinkat(parent, name, AT_REMOVEDIR) < 0) return -1;
    return fsync(parent);
}

static int recover_locked(int directory)
{
    if (remove_stage(directory, garbage) < 0 || remove_stage(directory, prepared) < 0) return -1;
    int stage = open_directory(directory, committed);
    if (stage < 0) return errno == ENOENT ? 0 : -1;
    int result = -1;
    /* Keep every committed inode until all four names are durable. Replaying
     * links and replacements is idempotent even after a crash during publish. */
    for (unsigned i = 0; i < 4; ++i) {
        int file = openat(stage, names[i], O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (file < 0) goto out;
        int valid = trusted(file, 0);
        int error = errno;
        if (close(file) < 0 && !valid) goto out;
        if (valid < 0) { errno = error; goto out; }
    }
    for (unsigned i = 0; i < 4; ++i) {
        char temporary[64];
        snprintf(temporary, sizeof(temporary), ".leonos-account-%s", names[i]);
        if (unlinkat(directory, temporary, 0) < 0 && errno != ENOENT) goto out;
        if (linkat(stage, names[i], directory, temporary, 0) < 0 ||
            renameat(directory, temporary, directory, names[i]) < 0) goto out;
        /* rename of two names for the same inode leaves the source intact. */
        if (unlinkat(directory, temporary, 0) < 0 && errno != ENOENT) goto out;
    }
    if (fsync(directory) < 0 || renameat(directory, committed, directory, garbage) < 0 ||
        fsync(directory) < 0) goto out;
    result = remove_stage(directory, garbage);
out:;
    int error = errno;
    if (close(stage) < 0 && !result) return -1;
    errno = error;
    return result;
}

static int commit_locked(int directory, const char *const contents[4])
{
    if (!contents) { errno = EINVAL; return -1; }
    for (unsigned i = 0; i < 4; ++i)
        if (!contents[i]) { errno = EINVAL; return -1; }
    if (recover_locked(directory) < 0 || mkdirat(directory, prepared, 0700) < 0) return -1;
    int stage = open_directory(directory, prepared);
    if (stage < 0) return -1;
    int result = -1;
    for (unsigned i = 0; i < 4; ++i) {
        int file = openat(stage, names[i], O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (file < 0) goto out;
        int error = 0;
        const char *position = contents[i];
        size_t remaining = strlen(position);
        while (remaining) {
            ssize_t written = write(file, position, remaining);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { error = written ? errno : EIO; break; }
            position += written;
            remaining -= (size_t)written;
        }
        if (!error && (fchown(file, 0, 0) < 0 || fchmod(file, i % 2 ? 0600 : 0644) < 0 ||
                       fsync(file) < 0)) error = errno;
        if (close(file) < 0 && !error) error = errno;
        if (error) { errno = error; goto out; }
    }
    if (fsync(stage) < 0 || fsync(directory) < 0 ||
        renameat(directory, prepared, directory, committed) < 0 || fsync(directory) < 0) goto out;
    result = recover_locked(directory);
out:;
    int error = errno;
    if (close(stage) < 0 && !result) return -1;
    errno = error;
    return result;
}

static int operate(int directory, const char *const contents[4], int update)
{
    int error = pthread_mutex_lock(&mutex);
    if (error) { errno = error; return -1; }
    int lock = lock_store(directory);
    int result = lock < 0 ? -1 : update ? commit_locked(directory, contents) : recover_locked(directory);
    error = errno;
    if (lock >= 0 && close(lock) < 0 && !result) { error = errno; result = -1; }
    pthread_mutex_unlock(&mutex);
    errno = error;
    return result;
}

int leonos_account_store_commit(int directory, const char *const contents[4])
{
    return operate(directory, contents, 1);
}

int leonos_account_store_recover(int directory)
{
    return operate(directory, NULL, 0);
}
