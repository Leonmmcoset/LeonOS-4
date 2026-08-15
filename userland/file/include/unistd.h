#ifndef LEONOS_FILE_UNISTD_H
#define LEONOS_FILE_UNISTD_H

#include <sys/types.h>
#include <leonos/posix.h>

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int open(const char *path, int flags, ...);
long read(int fd, void *buffer, size_t length);
long write(int fd, const void *buffer, size_t length);
int close(int fd);
long lseek(int fd, long offset, int whence);
int unlink(const char *path);
int access(const char *path, int mode);
int isatty(int fd);
int ftruncate(int fd, off_t length);
int dup2(int old_fd, int new_fd);
int pipe(int fds[2]);
ssize_t pread(int fd, void *buffer, size_t length, off_t offset);
ssize_t readlink(const char *path, char *buffer, size_t capacity);
#endif
