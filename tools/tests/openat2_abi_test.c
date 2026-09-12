#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int openat2_abi_test(void)
{
    struct open_how how = {.flags = O_RDONLY};
    int fd = syscall(SYS_openat2, AT_FDCWD, "/dev/null", &how, sizeof(how));
    if (fd < 0) return 1;
    if (close(fd) != 0) return 2;

    errno = 0;
    if (syscall(SYS_openat2, AT_FDCWD, "/dev/null", &how, 0) != -1 || errno != EINVAL) return 3;
    errno = 0;
    if (syscall(SYS_openat2, AT_FDCWD, "/dev/null", NULL, sizeof(how)) != -1 || errno != EFAULT) return 4;

    _Alignas(struct open_how) unsigned char extended[32] = {0};
    *(struct open_how *)extended = how;
    extended[24] = 1;
    errno = 0;
    if (syscall(SYS_openat2, AT_FDCWD, "/dev/null", extended, sizeof(extended)) != -1 || errno != E2BIG) return 5;

    how.flags = 1ULL << 63;
    errno = 0;
    if (syscall(SYS_openat2, AT_FDCWD, "/dev/null", &how, sizeof(how)) != -1 || errno != EINVAL) return 6;
    how.flags = O_RDONLY;
    how.mode = 0600;
    errno = 0;
    if (syscall(SYS_openat2, AT_FDCWD, "/dev/null", &how, sizeof(how)) != -1 || errno != EINVAL) return 7;
    how.mode = 0;
    how.resolve = 1ULL << 63;
    if (syscall(SYS_openat2, AT_FDCWD, "/dev/null", &how, sizeof(how)) != -1 || errno != EINVAL) return 8;
    how.resolve = RESOLVE_BENEATH | RESOLVE_IN_ROOT;
    if (syscall(SYS_openat2, AT_FDCWD, "/dev/null", &how, sizeof(how)) != -1 || errno != EINVAL) return 9;
    how.resolve = 0;
    how.flags = O_CREAT | O_DIRECTORY | O_RDONLY;
    if (syscall(SYS_openat2, AT_FDCWD, "/dev/null", &how, sizeof(how)) != -1 || errno != EINVAL) return 10;
    how.flags = O_RDONLY;
    extended[24] = 0;
    fd = syscall(SYS_openat2, -42, "/dev/null", extended, sizeof(extended));
    if (fd < 0 || close(fd)) return 11;
    if (syscall(SYS_openat2, AT_FDCWD, "/dev/null", &how, 4097) != -1 || errno != E2BIG) return 12;
    if (syscall(SYS_openat2, -42, "child", &how, sizeof(how)) != -1 || errno != EBADF) return 13;
    char directory[] = "/tmp/openat2-XXXXXX";
    if (!mkdtemp(directory)) return 14;
    int dirfd = open(directory, O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) return 15;
    how = (struct open_how){.flags = O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, .mode = 0600};
    fd = syscall(SYS_openat2, dirfd, "child", &how, sizeof(how));
    if (fd < 0) return 16;
    struct stat st;
    if (fstat(fd, &st) || (st.st_mode & 0777) != 0600 || fcntl(fd, F_GETFD) != FD_CLOEXEC) return 17;
    if (write(fd, "native", 6) != 6 || lseek(fd, 0, SEEK_SET)) return 18;
    char content[8] = {0};
    if (read(fd, content, sizeof(content)) != 6 || strcmp(content, "native")) return 19;
    if (close(fd)) return 20;
    if (syscall(SYS_openat2, dirfd, "child", &how, sizeof(how)) != -1 || errno != EEXIST) return 21;
    if (unlinkat(dirfd, "child", 0) || close(dirfd) || rmdir(directory)) return 22;
    return 0;
}

#ifdef OPENAT2_EMBEDDED
static int run_openat2_abi_test(void)
{
    int result = openat2_abi_test();
    if (result) printf("openat2 FAIL case=%d errno=%d\n", result, errno);
    return result;
}
#else
int main(void) { return openat2_abi_test(); }
#endif
