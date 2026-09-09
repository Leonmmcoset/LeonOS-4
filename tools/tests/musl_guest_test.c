#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/futex.h>
#include <linux/sched.h>
#include <limits.h>
#include <mimalloc.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <poll.h>
#include <pty.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <ucontext.h>
#include <stdint.h>

#ifndef PROBE_KIND
#define PROBE_KIND "dynamic"
#endif
#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1U << 2)
#endif
#ifndef MREMAP_MAYMOVE
#define MREMAP_MAYMOVE 1U
#endif
#ifndef PR_SET_NAME
#define PR_SET_NAME 15
#define PR_GET_NAME 16
#endif
static _Thread_local unsigned tls_value = 73;
static unsigned failed;
static volatile sig_atomic_t timer_callback_seen;

#define CHECK(c) do { if (!(c)) { \
    printf("[musl-abi:" PROBE_KIND "] FAIL %s:%d %s errno=%d\n", \
           __func__, __LINE__, #c, errno); fflush(stdout); return 1; } } while (0)

#define FUTEX2_EMBEDDED
#include "futex2_abi_test.c"
#define CLONE3_EMBEDDED
#include "clone3_abi_test.c"
#define PRCTL_EMBEDDED
#include "prctl_abi_test.c"
#define UTSNAME_EMBEDDED
#include "utsname_abi_test.c"
#define MEMBARRIER_EMBEDDED
#include "membarrier_abi_test.c"
#define OPENAT2_EMBEDDED
#include "openat2_abi_test.c"

static void timer_callback(union sigval value)
{
    (void)value;
    timer_callback_seen = 1;
}

static int timer_abi(void)
{
    timer_t timer;
    struct sigevent none = {.sigev_notify = SIGEV_NONE};
    struct itimerspec spec = {
        .it_value = {.tv_sec = 0, .tv_nsec = 20000000},
    };
    struct itimerspec old = {0}, current = {0};
    CHECK(timer_create(CLOCK_MONOTONIC, &none, &timer) == 0);
    CHECK(timer_settime(timer, 0, &spec, &old) == 0);
    CHECK(timer_gettime(timer, &current) == 0 && current.it_value.tv_sec >= 0);
    CHECK(timer_getoverrun(timer) == 0);
    CHECK(timer_delete(timer) == 0);

    timer_callback_seen = 0;
    struct sigevent threaded = {
        .sigev_notify = SIGEV_THREAD,
        .sigev_notify_function = timer_callback,
    };
    CHECK(timer_create(CLOCK_MONOTONIC, &threaded, &timer) == 0);
    CHECK(timer_settime(timer, 0, &spec, NULL) == 0);
    for (unsigned i = 0; i < 20 && !timer_callback_seen; ++i)
        usleep(10000);
    CHECK(timer_callback_seen);
    CHECK(timer_delete(timer) == 0);
    return 0;
}

static int startup(int argc, char **argv)
{
    CHECK(argc >= 1 && argv && argv[argc] == NULL);
    CHECK(getauxval(AT_PAGESZ) == 4096);
    CHECK(getauxval(AT_PHDR) && getauxval(AT_PHNUM) && getauxval(AT_RANDOM));
    uid_t ruid, euid, suid;
    gid_t rgid, egid, sgid;
    CHECK(syscall(SYS_getresuid, &ruid, &euid, &suid) == 0);
    CHECK(ruid == getuid() && euid == geteuid());
    CHECK(syscall(SYS_getresgid, &rgid, &egid, &sgid) == 0);
    CHECK(rgid == getgid() && egid == getegid());
    CHECK(syscall(SYS_setreuid, (unsigned)-1, (unsigned)euid) == 0);
    CHECK(syscall(SYS_setregid, (unsigned)-1, (unsigned)egid) == 0);
    CHECK(syscall(SYS_setresuid, (unsigned)-1, (unsigned)-1, (unsigned)-1) == 0);
    CHECK(syscall(SYS_setresgid, (unsigned)-1, (unsigned)-1, (unsigned)-1) == 0);
    CHECK(syscall(SYS_setfsuid, geteuid()) == geteuid());
    CHECK(syscall(SYS_setfsgid, getegid()) == getegid());
    CHECK(syscall(SYS_sched_get_priority_max, SCHED_OTHER) == 0);
    CHECK(syscall(SYS_sched_get_priority_min, SCHED_OTHER) == 0);
    CHECK(syscall(SYS_sched_get_priority_max, SCHED_FIFO) == 99);
    CHECK(syscall(SYS_sched_get_priority_min, SCHED_RR) == 1);
    CHECK(syscall(SYS_sched_get_priority_max, 4) == -1 && errno == EINVAL);
    struct sched_param sched_value = {.sched_priority = -1};
    CHECK(syscall(SYS_sched_getparam, 0, &sched_value) == 0 && sched_value.sched_priority == 0);
    CHECK(syscall(SYS_sched_getscheduler, 0) == SCHED_OTHER);
    sched_value.sched_priority = 0;
    CHECK(syscall(SYS_sched_setparam, 0, &sched_value) == 0);
    CHECK(syscall(SYS_sched_setscheduler, 0, SCHED_OTHER, &sched_value) == SCHED_OTHER);
    sched_value.sched_priority = 1;
    CHECK(syscall(SYS_sched_setparam, 0, &sched_value) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_sched_setscheduler, 0, SCHED_FIFO, &sched_value) == -1 && errno == EINVAL);
    struct timespec rr_interval;
    CHECK(syscall(SYS_sched_rr_get_interval, 0, &rr_interval) == 0 &&
          rr_interval.tv_sec == 0 && rr_interval.tv_nsec > 0 && rr_interval.tv_nsec <= 1000000000L);
    CHECK(syscall(SYS_sched_rr_get_interval, 0x7fffffff, &rr_interval) == -1 && errno == ESRCH);
    CHECK(syscall(SYS_personality, (unsigned long)-1) == 0);
    CHECK(syscall(SYS_personality, 0) == 0);
    CHECK(syscall(SYS_personality, 0x0040000UL) == -1 && errno == EINVAL);
    char task_name[16] = {0};
    CHECK(syscall(SYS_prctl, PR_SET_NAME, "abi-probe-name") == 0);
    CHECK(syscall(SYS_prctl, PR_GET_NAME, task_name) == 0 &&
          !strcmp(task_name, "abi-probe-name"));
    CHECK(run_utsname_abi_test() == 0);
    CHECK(run_membarrier_abi_test() == 0);
    CHECK(run_openat2_abi_test() == 0);
    CHECK(timer_abi() == 0);
    struct timespec invalid_clock = {.tv_sec = 0, .tv_nsec = 1000000000L};
    CHECK(syscall(SYS_clock_settime, CLOCK_REALTIME, &invalid_clock) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_sched_getparam, 0x7fffffff, &sched_value) == -1 && errno == ESRCH);
    unsigned char *remap = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(remap != MAP_FAILED);
    remap[0] = 0xa5;
    remap = (unsigned char *)syscall(SYS_mremap, remap, 4096, 8192, MREMAP_MAYMOVE);
    CHECK(remap != MAP_FAILED && remap[0] == 0xa5);
    CHECK(munmap(remap, 8192) == 0);
    unsigned char *locked = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(locked != MAP_FAILED);
    CHECK(syscall(SYS_mlock, locked, 4096) == 0);
    CHECK(syscall(SYS_munlock, locked, 4096) == 0);
    CHECK(syscall(SYS_mlock2, locked, 4096, 2) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_mlockall, MCL_CURRENT) == 0);
    CHECK(syscall(SYS_munlockall) == 0);
    CHECK(munmap(locked, 4096) == 0);
    _Alignas(4) uint32_t futex_word = 1;
    CHECK(syscall(SYS_futex_wake, &futex_word, UINT32_MAX, 1, FUTEX2_SIZE_U32) == 0);
    CHECK(syscall(SYS_futex_wait, &futex_word, 0, UINT32_MAX, FUTEX2_SIZE_U32,
                  NULL, CLOCK_MONOTONIC) == -1 && errno == EAGAIN);
    struct futex_waitv futex_requeue[2] = {
        {.val = 1, .uaddr = (uintptr_t)&futex_word, .flags = FUTEX2_SIZE_U32},
        {.val = 1, .uaddr = (uintptr_t)&futex_word, .flags = FUTEX2_SIZE_U32},
    };
    CHECK(syscall(SYS_futex_requeue, futex_requeue, 0, 0, 0) == 0);
    int readahead_fd = open("/system/tests/musl-abi-static.elf", O_RDONLY);
    CHECK(readahead_fd >= 0);
    CHECK(syscall(SYS_readahead, readahead_fd, 0, 4096) == 0);
    CHECK(close(readahead_fd) == 0);
    const char *path = (const char *)getauxval(AT_EXECFN);
    CHECK(path && path[0] == '/');
    CHECK(tls_value == 73);
    tls_value = 91;
    return 0;
}

static int heap(void)
{
    char *p = strdup("musl to mimalloc");
    CHECK(p && mi_is_in_heap_region(p));
    p = realloc(p, 32768);
    CHECK(p && !strcmp(p, "musl to mimalloc"));
    free(p);
    void *aligned = NULL;
    CHECK(posix_memalign(&aligned, 4096, 8192) == 0);
    CHECK(((uintptr_t)aligned & 4095) == 0 && mi_is_in_heap_region(aligned));
    free(aligned);
    unsigned char *zero = calloc(512, 1);
    CHECK(zero);
    for (unsigned i = 0; i < 512; ++i) CHECK(zero[i] == 0);
    free(zero);
    char *text = NULL;
    CHECK(asprintf(&text, "%.3f", 1.25) == 5 && !strcmp(text, "1.250"));
    CHECK(mi_is_in_heap_region(text));
    free(text);
    return 0;
}

static int memory(void)
{
    uint64_t initial_break = (uint64_t)syscall(SYS_brk, 0);
    CHECK(initial_break > 0 && initial_break < 0x08000000ULL);
    CHECK(syscall(SYS_brk, initial_break + 8193) == (long)(initial_break + 8193));
    ((volatile unsigned char *)(uintptr_t)initial_break)[4096] = 0x5a;
    CHECK(syscall(SYS_brk, initial_break + 1) == (long)(initial_break + 1));
    CHECK(syscall(SYS_brk, initial_break) == (long)initial_break);
    CHECK(syscall(SYS_brk, initial_break - 65536) == (long)initial_break);
    char *p = mmap(NULL, 8192, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(p != MAP_FAILED);
    CHECK(mprotect(p, 4096, PROT_READ | PROT_WRITE) == 0);
    p[0] = 42;
    p[4095] = 17;
    unsigned char residency[2] = {0, 0};
    CHECK(syscall(SYS_mincore, p, 8192, residency) == 0 && (residency[0] & 1));
    CHECK(syscall(SYS_madvise, p, 8192, MADV_NORMAL) == 0);
    CHECK(syscall(SYS_msync, p, 8192, MS_SYNC) == 0);
    CHECK(syscall(SYS_msync, p, 8192, MS_ASYNC | MS_SYNC) == -1 && errno == EINVAL);
    CHECK(mprotect(p, 4096, PROT_NONE) == 0);
    int fds[2];
    CHECK(pipe(fds) == 0);
    /* A syscall must reject inaccessible user input without crashing the kernel. */
    CHECK(write(fds[1], p, 1) == -1 && errno == EFAULT);
    CHECK(close(fds[0]) == 0 && close(fds[1]) == 0);
    CHECK(mprotect(p, 4096, PROT_READ | PROT_WRITE) == 0);
    CHECK(p[0] == 42 && p[4095] == 17);
    CHECK(munmap(p, 8192) == 0);
    CHECK(mprotect(NULL, 4096, PROT_READ) == -1 && errno == ENOMEM);
    CHECK(mprotect(NULL, 0, PROT_READ) == 0);
    int zero = open("/dev/zero", O_RDONLY);
    CHECK(zero >= 0);
    p = mmap(NULL, 4096, PROT_READ, MAP_SHARED, zero, 0);
    CHECK(p != MAP_FAILED && p[0] == 0);
    CHECK(mprotect(p, 4096, PROT_WRITE) == -1 && errno == EACCES);
    CHECK(munmap(p, 4096) == 0);
    p = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE, zero, 0);
    CHECK(p != MAP_FAILED);
    CHECK(mprotect(p, 4096, PROT_READ | PROT_WRITE) == 0);
    p[0] = 99;
    CHECK(munmap(p, 4096) == 0 && close(zero) == 0);
    /* Anonymous mappings ignore fd; shared RAM must retain identity at fork. */
    p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 123, 0);
    CHECK(p != MAP_FAILED);
    p[0] = 17;
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        if (p[0] != 17) _exit(2);
        if (mprotect(p, 4096, PROT_NONE) ||
            mprotect(p, 4096, PROT_READ | PROT_WRITE)) _exit(3);
        p[0] = 42;
        _exit(0);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(p[0] == 42);
    CHECK(munmap(p, 4096) == 0);
    return 0;
}

static int nonblocking(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
    CHECK((syscall(SYS_fcntl, fd, F_GETFL) & 0x800) != 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/musl-abi-%ld.sock", (long)getpid());
    unlink(addr.sun_path);
    CHECK(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    CHECK(listen(fd, 1) == 0);
    CHECK(accept(fd, NULL, NULL) == -1 && errno == EAGAIN);
    CHECK(close(fd) == 0);
    CHECK(unlink(addr.sun_path) == 0);
    return 0;
}

static int unix_streams(void)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX}, actual;
    snprintf(address.sun_path, sizeof(address.sun_path), "/tmp/musl-stream-%ld.sock", (long)getpid());
    socklen_t size = offsetof(struct sockaddr_un, sun_path) + strlen(address.sun_path) + 1;
    mode_t mask = umask(0027);
    int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    CHECK(listener >= 0 && (fcntl(listener, F_GETFL) & O_NONBLOCK) && (fcntl(listener, F_GETFD) & FD_CLOEXEC));
    CHECK(bind(listener, (struct sockaddr *)&address, size) == 0);
    umask(mask);
    struct stat st;
    CHECK(stat(address.sun_path, &st) == 0 && S_ISSOCK(st.st_mode) && (st.st_mode & 0777) == 0750);
    CHECK(chmod(address.sun_path, 0600) == 0);
    CHECK(stat(address.sun_path, &st) == 0 && S_ISSOCK(st.st_mode) && (st.st_mode & 0777) == 0600);
    CHECK(open(address.sun_path, O_RDONLY) == -1 && errno == ENXIO);
    CHECK(listen(listener, 2) == 0);
    int client = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(client >= 0 && bind(client, (struct sockaddr *)&address, size) == -1 && errno == EADDRINUSE);
    CHECK(connect(client, (struct sockaddr *)&address, size) == 0);
    socklen_t actual_size = sizeof(actual);
    int accepted = accept4(listener, (struct sockaddr *)&actual, &actual_size, SOCK_CLOEXEC);
    CHECK(accepted >= 0 && actual.sun_family == AF_UNIX && actual_size == 2);
    CHECK((fcntl(accepted, F_GETFL) & O_NONBLOCK) == 0 && (fcntl(accepted, F_GETFD) & FD_CLOEXEC));
    actual_size = sizeof(actual);
    CHECK(getsockname(accepted, (struct sockaddr *)&actual, &actual_size) == 0 && actual_size == size &&
          !strcmp(actual.sun_path, address.sun_path));
    actual_size = sizeof(actual);
    CHECK(getpeername(client, (struct sockaddr *)&actual, &actual_size) == 0 && actual_size == size);
    struct ucred peer;
    socklen_t peer_size = sizeof(peer);
    CHECK(getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) == 0 && peer.pid == getpid() && peer.uid == getuid());
    CHECK(unlink(address.sun_path) == 0 && stat(address.sun_path, &st) == -1 && errno == ENOENT);
    int rebound = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(rebound >= 0 && bind(rebound, (struct sockaddr *)&address, size) == 0 && listen(rebound, 1) == 0);
    CHECK(write(client, "abc", 3) == 3);
    char data[16];
    CHECK(read(accepted, data, 3) == 3 && !memcmp(data, "abc", 3));
    CHECK(shutdown(client, SHUT_WR) == 0 && read(accepted, data, 1) == 0);
    CHECK(write(accepted, "r", 1) == 1 && read(client, data, 1) == 1 && data[0] == 'r');
    CHECK(close(client) == 0 && close(accepted) == 0 && close(listener) == 0 && close(rebound) == 0);
    CHECK(stat(address.sun_path, &st) == 0 && S_ISSOCK(st.st_mode));
    client = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(client >= 0 && connect(client, (struct sockaddr *)&address, size) == -1 && errno == ECONNREFUSED);
    CHECK(close(client) == 0 && unlink(address.sun_path) == 0);
    DIR *dir = opendir("/tmp");
    CHECK(dir);
    CHECK(closedir(dir) == 0);
    int pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair) == 0);
    CHECK(read(pair[0], data, 0) == 0 && read(pair[0], data, 1) == -1 && errno == EAGAIN);
    char *large = malloc(65536);
    CHECK(large);
    memset(large, 'x', 65536);
    ssize_t sent = write(pair[0], large, 65536);
    CHECK(sent > 0 && sent <= 65536);
    size_t buffered = sent;
    for (unsigned i = 0; i < 64; ++i) {
        sent = write(pair[0], large, 65536);
        if (sent < 0) break;
        CHECK(sent > 0);
        buffered += sent;
    }
    CHECK(sent == -1 && errno == EAGAIN);
    struct pollfd ready = {pair[0], POLLOUT, 0};
    CHECK(poll(&ready, 1, 0) == 0);
    CHECK(write(pair[0], large, 1) == -1 && errno == EAGAIN);
    while (buffered) {
        ssize_t got = read(pair[1], large, buffered < 65536 ? buffered : 65536);
        CHECK(got > 0 && large[got - 1] == 'x');
        buffered -= got;
    }
    CHECK(poll(&ready, 1, 0) == 1 && (ready.revents & POLLOUT));
    CHECK(close(pair[0]) == 0 && read(pair[1], data, 1) == 0 && close(pair[1]) == 0);
    free(large);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path + 1, sizeof(address.sun_path) - 1, "musl-%ld", (long)getpid());
    size = 2 + 1 + strlen(address.sun_path + 1);
    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    client = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(listener >= 0 && client >= 0);
    CHECK(bind(listener, (struct sockaddr *)&address, size) == 0 && listen(listener, 1) == 0);
    CHECK(connect(client, (struct sockaddr *)&address, size) == 0);
    accepted = accept(listener, NULL, NULL);
    CHECK(accepted >= 0);
    CHECK(close(listener) == 0 && close(client) == 0 && close(accepted) == 0);
    return 0;
}

static int rename_replacement(void)
{
    char directory[96], source[128], target[128], left[128], right[128], child[160];
    snprintf(directory, sizeof(directory), "/tmp/musl-rename-%ld", (long)getpid());
    snprintf(source, sizeof(source), "%s/source-long-name", directory);
    snprintf(target, sizeof(target), "%s/target", directory);
    snprintf(left, sizeof(left), "%s/left", directory);
    snprintf(right, sizeof(right), "%s/right", directory);
    snprintf(child, sizeof(child), "%s/child", right);
    CHECK(mkdir(directory, 0700) == 0);
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0640);
    CHECK(fd >= 0 && write(fd, "new", 3) == 3 && close(fd) == 0);
    CHECK(chown(source, 12345, 23456) == 0);
    fd = open(target, O_CREAT | O_EXCL | O_WRONLY, 0600);
    CHECK(fd >= 0 && write(fd, "old data", 8) == 8 && close(fd) == 0);
    CHECK(rename(source, target) == 0);
    struct stat st;
    CHECK(stat(source, &st) == -1 && errno == ENOENT);
    CHECK(stat(target, &st) == 0 && st.st_uid == 12345 && st.st_gid == 23456 &&
          (st.st_mode & 0777) == 0640 && st.st_size == 3);
    char text[8] = {0};
    fd = open(target, O_RDONLY);
    CHECK(fd >= 0 && read(fd, text, sizeof(text)) == 3 && !strcmp(text, "new") && close(fd) == 0);
    CHECK(rename(target, target) == 0);
    CHECK(rename(source, source) == -1 && errno == ENOENT);
    CHECK(mkdir(left, 0700) == 0 && mkdir(right, 0700) == 0);
    CHECK(rename(target, left) == -1 && errno == EISDIR);
    CHECK(rename(left, target) == -1 && errno == ENOTDIR);
    fd = open(child, O_CREAT | O_EXCL | O_WRONLY, 0600);
    CHECK(fd >= 0 && close(fd) == 0);
    CHECK(rename(left, right) == -1 && errno == ENOTEMPTY);
    CHECK(unlink(child) == 0 && rename(left, right) == 0);
    CHECK(stat(left, &st) == -1 && errno == ENOENT);
    CHECK(stat(right, &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(rmdir(right) == 0 && unlink(target) == 0 && rmdir(directory) == 0);
    return 0;
}

static int permission_child(const char *path, unsigned uid, unsigned gid,
                            int can_read, int can_write, int inherited)
{
    CHECK(setgid(gid) == 0 && setuid(uid) == 0);
    uid_t ruid, euid, suid;
    gid_t rgid, egid, sgid;
    CHECK(syscall(SYS_getresuid, &ruid, &euid, &suid) == 0 &&
          ruid == uid && euid == uid && suid == uid);
    CHECK(syscall(SYS_getresgid, &rgid, &egid, &sgid) == 0 &&
          rgid == gid && egid == gid && sgid == gid);
    CHECK(syscall(SYS_setreuid, (unsigned)-1, (unsigned)uid) == 0);
    CHECK(syscall(SYS_setregid, (unsigned)-1, (unsigned)gid) == 0);
    CHECK(syscall(SYS_setreuid, (unsigned)(uid + 1000), (unsigned)-1) == -1 && errno == EPERM);
    CHECK(syscall(SYS_setregid, (unsigned)(gid + 1000), (unsigned)-1) == -1 && errno == EPERM);
    CHECK(syscall(SYS_setresuid, (unsigned)-1, (unsigned)-1, (unsigned)-1) == 0);
    CHECK(syscall(SYS_setresgid, (unsigned)-1, (unsigned)-1, (unsigned)-1) == 0);
    CHECK(setgroups(0, NULL) == -1 && errno == EPERM);
    int fd = open(path, O_RDONLY);
    CHECK(can_read ? fd >= 0 : fd == -1 && errno == EACCES);
    if (fd >= 0) CHECK(close(fd) == 0);
    fd = open(path, O_WRONLY);
    CHECK(can_write ? fd >= 0 : fd == -1 && errno == EACCES);
    if (fd >= 0) CHECK(close(fd) == 0);
    if (inherited >= 0) {
        CHECK(chmod(path, 0000) == 0);
        CHECK(open(path, O_RDONLY) == -1 && errno == EACCES);
        CHECK(write(inherited, "x", 1) == 1);
        CHECK(fchmod(inherited, 0640) == 0);
        CHECK(chown(path, uid + 1, (gid_t)-1) == -1 && errno == EPERM);
    } else CHECK(chmod(path, 0777) == -1 && errno == EPERM);
    return 0;
}

static int permissions(void)
{
    char directory[96], path[128];
    snprintf(directory, sizeof(directory), "/tmp/musl-perm-%ld", (long)getpid());
    snprintf(path, sizeof(path), "%s/file", directory);
    mode_t old_mask = umask(0);
    CHECK(mkdir(directory, 01777) == 0);
    CHECK(chmod(directory, 01777) == 0);
    umask(0027);
    int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0666);
    CHECK(fd >= 0);
    struct stat st;
    CHECK(fstat(fd, &st) == 0 && (st.st_mode & 0777) == 0640);
    struct statfs fs, fd_fs;
    CHECK(statfs(directory, &fs) == 0 && fstatfs(fd, &fd_fs) == 0);
    CHECK(fs.f_type == fd_fs.f_type && fs.f_bsize > 0 && fs.f_blocks > 0);
    CHECK(fs.f_bavail <= fs.f_bfree && fs.f_bfree <= fs.f_blocks);
    CHECK(st.st_uid == getuid() && st.st_gid == getgid());
    CHECK(fchown(fd, 10001, 20001) == 0);
    CHECK(stat(path, &st) == 0 && st.st_uid == 10001 && st.st_gid == 20001);
    int dirfd = open(directory, O_RDONLY | O_DIRECTORY);
    CHECK(dirfd >= 0);
    char cwd_before[256], cwd_after[256];
    CHECK(getcwd(cwd_before, sizeof(cwd_before)));
    int relative = openat(dirfd, "file", O_RDONLY);
    CHECK(relative >= 0 && close(relative) == 0);
    CHECK(getcwd(cwd_after, sizeof(cwd_after)) && !strcmp(cwd_before, cwd_after));
    CHECK(fstatat(dirfd, "file", &st, 0) == 0 && st.st_uid == 10001);
    CHECK(fstatat(-1, path, &st, 0) == 0);
    CHECK(fstatat(fd, "", &st, AT_EMPTY_PATH) == 0 && (st.st_mode & 0777) == 0640);
    struct statx sx;
    CHECK(syscall(SYS_statx, AT_FDCWD, path, 0,
                  STATX_TYPE | STATX_MODE | STATX_UID | STATX_GID |
                  STATX_INO | STATX_SIZE | STATX_BLOCKS, &sx) == 0 &&
          (sx.stx_mask & STATX_BASIC_STATS) ==
              (STATX_TYPE | STATX_MODE | STATX_UID | STATX_GID |
               STATX_INO | STATX_SIZE | STATX_BLOCKS) &&
          (sx.stx_mode & 0777) == 0640 && sx.stx_uid == 10001 && sx.stx_gid == 20001);
    CHECK(syscall(SYS_statx, AT_FDCWD, path, 0, 0, &sx) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_statx, fd, "", AT_EMPTY_PATH, STATX_BASIC_STATS, &sx) == 0 &&
          (sx.stx_mode & 0777) == 0640);
    CHECK(syscall(SYS_fchmodat, dirfd, "file", 0600) == 0);
    CHECK(fstat(fd, &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(syscall(SYS_fchmodat2, fd, "", 0640, AT_EMPTY_PATH) == 0);
    CHECK(syscall(SYS_fchmodat, -1, path, 0640) == 0);
    CHECK(syscall(SYS_fchmodat, dirfd, "", 0640) == -1 && errno == ENOENT);
    CHECK(syscall(SYS_fchmodat2, dirfd, "file", 0640, 1) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_fchownat, dirfd, "file", -1, 20002, 0) == 0);
    CHECK(fstat(fd, &st) == 0 && st.st_uid == 10001 && st.st_gid == 20002);
    CHECK(syscall(SYS_fchownat, fd, "", -1, 20001, AT_EMPTY_PATH) == 0);
    CHECK(syscall(SYS_faccessat, dirfd, "file", R_OK | W_OK) == 0);
    CHECK(syscall(SYS_faccessat2, dirfd, "file", R_OK, AT_EACCESS) == 0);
    CHECK(syscall(SYS_mkdirat, dirfd, "child", 0777) == 0);
    CHECK(fstatat(dirfd, "child", &st, 0) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0750);
    CHECK(syscall(SYS_unlinkat, dirfd, "child", AT_REMOVEDIR) == 0);
    CHECK(syscall(SYS_unlinkat, dirfd, "file", 1) == -1 && errno == EINVAL);
    CHECK(fstatat(dirfd, "missing/../file", &st, 0) == -1 && errno == ENOENT);
    CHECK(fstatat(dirfd, "file/../file", &st, 0) == -1 && errno == ENOTDIR);
    CHECK(syscall(SYS_mkdirat, dirfd, "private", 0700) == 0);
    CHECK(close(dirfd) == 0);
    CHECK(setgroups(0, NULL) == 0 && getgroups(0, NULL) == 0);
    for (unsigned i = 0; i < 4; ++i) {
        if (i == 3) {
            gid_t groups[] = {30001, 20001};
            CHECK(setgroups(2, groups) == 0 && getgroups(2, groups) == 2);
            CHECK(groups[0] == 20001 && groups[1] == 30001);
        }
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) _exit(permission_child(path, 10001 + i, i >= 2 ? 20002 : 20001,
                                         i != 2, i == 0, i == 0 ? fd : -1));
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK(setgroups(0, NULL) == 0);
    pid_t creator = fork();
    CHECK(creator >= 0);
    if (!creator) {
        CHECK(setgid(30001) == 0 && setuid(30001) == 0);
        CHECK(unlink(path) == -1 && errno == EPERM);
        char own_path[128];
        snprintf(own_path, sizeof(own_path), "%s/private/../file", directory);
        CHECK(stat(own_path, &st) == -1 && errno == EACCES);
        snprintf(own_path, sizeof(own_path), "%s/owned", directory);
        umask(0027);
        int own_fd = open(own_path, O_CREAT | O_EXCL | O_RDWR, 0666);
        CHECK(own_fd >= 0);
        CHECK(fstat(own_fd, &st) == 0 && (st.st_mode & 0777) == 0640 &&
              st.st_uid == 30001 && st.st_gid == 30001);
        CHECK(write(own_fd, "owner", 5) == 5);
        CHECK(chmod(own_path, 0600) == 0 && close(own_fd) == 0);
        CHECK(unlink(own_path) == 0);
        _exit(0);
    }
    int creator_status;
    CHECK(waitpid(creator, &creator_status, 0) == creator &&
          WIFEXITED(creator_status) && WEXITSTATUS(creator_status) == 0);
    char private_path[128];
    snprintf(private_path, sizeof(private_path), "%s/private", directory);
    CHECK(rmdir(private_path) == 0);
    CHECK(chmod("/dev/null", 0600) == 0);
    pid_t device_child = fork();
    CHECK(device_child >= 0);
    if (!device_child) {
        CHECK(setgid(10001) == 0 && setuid(10001) == 0);
        CHECK(open("/dev/null", O_WRONLY) == -1 && errno == EACCES);
        _exit(0);
    }
    int device_status;
    CHECK(waitpid(device_child, &device_status, 0) == device_child &&
          WIFEXITED(device_status) && WEXITSTATUS(device_status) == 0);
    CHECK(chmod("/dev/null", 0666) == 0);
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0640);
    CHECK(chown(path, (uid_t)-1, (gid_t)-1) == 0);
    CHECK(stat(path, &st) == 0 && st.st_uid == 10001 && st.st_gid == 20001);
    CHECK(close(fd) == 0 && unlink(path) == 0 && rmdir(directory) == 0);
    umask(old_mask);
    return 0;
}

static int pty(void)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0);
    int number = -1;
    CHECK(syscall(SYS_ioctl, master, 0xffffffff80045430ULL, &number) == 0);
    CHECK(number >= 0);
    CHECK(ioctl(master, TIOCGPTN, &number) == 0);
    CHECK(grantpt(master) == 0 && unlockpt(master) == 0);
    char name[128];
    CHECK(ptsname_r(master, name, sizeof(name)) == 0);
    int slave = open(name, O_RDWR | O_NOCTTY);
    CHECK(slave >= 0);
    struct termios original, raw, readback;
    CHECK(tcgetattr(slave, &original) == 0);
    raw = original;
    cfmakeraw(&raw);
    CHECK(tcsetattr(slave, TCSANOW, &raw) == 0);
    CHECK(tcgetattr(slave, &readback) == 0);
    CHECK(!(readback.c_lflag & (ICANON | ECHO | ISIG)));
    CHECK(readback.c_cc[VMIN] == 1 && readback.c_cc[VTIME] == 0);
    CHECK(write(master, "x", 1) == 1);
    char ch = 0;
    CHECK(read(slave, &ch, 1) == 1 && ch == 'x');
    CHECK(tcsetattr(slave, TCSANOW, &original) == 0);
    CHECK(close(slave) == 0 && close(master) == 0);
    return 0;
}

static volatile sig_atomic_t session_child_exited;
static void session_child_signal(int sig)
{
    if (sig == SIGCHLD) session_child_exited = 1;
}

static int process_sessions(void)
{
    pid_t self = getpid(), sid = getsid(0), group = getpgrp();
    /* Linux init's inherited session may be 0 before any setsid(). */
    CHECK(sid >= 0 && getsid(self) == sid && getpgid(self) == group);
    CHECK(syscall(SYS_getsid, 0x100000000ULL) == sid);
    CHECK(syscall(SYS_getpgid, 0x100000000ULL) == group);
    CHECK(syscall(SYS_getsid, 0x100000000ULL | (unsigned)self) == sid);
    CHECK(getsid(-1) == -1 && errno == ESRCH);
    CHECK(getpgid(-1) == -1 && errno == ESRCH);
    CHECK(getsid(0x7fffffff) == -1 && errno == ESRCH);
    CHECK(getpgid(0x7fffffff) == -1 && errno == ESRCH);
    int ready[2], release[2];
    CHECK(pipe(ready) == 0 && pipe(release) == 0);
    struct sigaction action = {.sa_handler = session_child_signal}, old;
    sigemptyset(&action.sa_mask);
    session_child_exited = 0;
    CHECK(sigaction(SIGCHLD, &action, &old) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        close(ready[0]); close(release[1]);
        if (setsid() != getpid()) _exit(51);
        if (geteuid() == 0 && setuid(65534)) _exit(52);
        /* Linux permits these identity queries across user/session boundaries. */
        if (getsid(self) != sid || getpgid(self) != group) _exit(53);
        if (write(ready[1], "r", 1) != 1) _exit(54);
        char byte;
        if (read(release[0], &byte, 1) != 1) _exit(55);
        _exit(0);
    }
    close(ready[1]); close(release[0]);
    char byte;
    CHECK(read(ready[0], &byte, 1) == 1);
    CHECK(getsid(child) == child && getpgid(child) == child);
    CHECK(write(release[1], "x", 1) == 1);
    for (unsigned i = 0; !session_child_exited && i < 3000; ++i) usleep(1000);
    CHECK(session_child_exited);
    /* The exited child retains its PID/session until wait reaps it. */
    CHECK(getsid(child) == child && getpgid(child) == child);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(getsid(child) == -1 && errno == ESRCH);
    CHECK(getpgid(child) == -1 && errno == ESRCH);
    CHECK(sigaction(SIGCHLD, &old, NULL) == 0);
    CHECK(close(ready[0]) == 0 && close(release[1]) == 0);
    return 0;
}

static int pty_fork(void)
{
    int master = -1;
    struct winsize size = {.ws_row = 25, .ws_col = 80};
    pid_t child = forkpty(&master, NULL, NULL, &size);
    CHECK(child >= 0);
    if (!child) {
        struct winsize got;
        if (!isatty(0) || getsid(0) != getpid() || tcgetpgrp(0) != getpgrp() ||
            ioctl(0, TIOCGWINSZ, &got) || got.ws_row != 25 || got.ws_col != 80)
            _exit(41);
        if (ioctl(0, TIOCSCTTY, 0)) _exit(42);
        int second, slave;
        if (openpty(&second, &slave, NULL, NULL, NULL)) _exit(43);
        if (ioctl(slave, TIOCSCTTY, 0) != -1 || errno != EPERM) _exit(44);
        close(slave); close(second);
        if (write(1, "forkpty-ready", 13) != 13) _exit(45);
        _exit(0);
    }
    struct pollfd ready = {.fd = master, .events = POLLIN};
    CHECK(poll(&ready, 1, 3000) > 0);
    char text[32] = {0};
    CHECK(read(master, text, sizeof(text)) == 13 && !strcmp(text, "forkpty-ready"));
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(close(master) == 0);
    return 0;
}

static int stdio_reopen_child(int slave)
{
    CHECK(setsid() >= 0);
    CHECK(dup2(slave, STDIN_FILENO) == STDIN_FILENO);
    CHECK(close(slave) == 0);
    CHECK(syscall(SYS_close, 0x100000000ULL) == 0);
    CHECK(syscall(SYS_close, STDIN_FILENO) == -1 && errno == EBADF);
    CHECK(syscall(SYS_open, "/dev/null", O_RDONLY, 0) == STDIN_FILENO);
    char byte = 0;
    CHECK(syscall(SYS_read, STDIN_FILENO, &byte, 1) == 0);
    CHECK(syscall(SYS_close, STDIN_FILENO) == 0);
    CHECK(syscall(SYS_close, STDIN_FILENO) == -1 && errno == EBADF);
    return 0;
}

static int stdio_reopen(void)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0);
    CHECK(grantpt(master) == 0 && unlockpt(master) == 0);
    char name[128];
    CHECK(ptsname_r(master, name, sizeof(name)) == 0);
    int slave = open(name, O_RDWR | O_NOCTTY);
    CHECK(slave >= 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(stdio_reopen_child(slave));
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(close(slave) == 0 && close(master) == 0);
    return 0;
}

static int proc_directories(void)
{
    char path[64], name[24];
    snprintf(name, sizeof(name), "%ld", (long)getpid());
    snprintf(path, sizeof(path), "/proc/%s", name);
    struct stat st;
    CHECK(stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    int root = open("/proc", O_RDONLY | O_DIRECTORY);
    CHECK(root >= 0);
    int task = openat(root, name, O_RDONLY | O_DIRECTORY);
    CHECK(task >= 0);
    int info = openat(task, "stat", O_RDONLY);
    CHECK(info >= 0);
    char content[512];
    CHECK(read(info, content, sizeof(content)) > 0 && close(info) == 0);
    _Static_assert(offsetof(struct dirent, d_name) == 19, "native x86-64 dirent64 prefix");
    _Alignas(8) char records[1024], comparison[1024];
    CHECK(syscall(SYS_getdents64, root, records, 1) == -1 && errno == EINVAL);
    void *readonly = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(readonly != MAP_FAILED);
    CHECK(syscall(SYS_getdents64, root, readonly, 128) == -1 && errno == EFAULT);
    CHECK(munmap(readonly, 4096) == 0);
    int fresh = open("/proc", O_RDONLY | O_DIRECTORY);
    CHECK(fresh >= 0);
    long first = syscall(SYS_getdents64, fresh, comparison, sizeof(comparison));
    CHECK(first > 0 && close(fresh) == 0);
    long length = syscall(SYS_getdents64, root, records, sizeof(records));
    CHECK(length > 0);
    CHECK(!strcmp(((struct dirent *)records)->d_name, ((struct dirent *)comparison)->d_name));
    int found = 0;
    do {
        for (long offset = 0; offset < length;) {
            struct dirent *entry = (struct dirent *)(records + offset);
            CHECK(entry->d_reclen >= 24 && !(entry->d_reclen & 7) && offset + entry->d_reclen <= length);
            CHECK(memchr(entry->d_name, 0, entry->d_reclen - 19));
            if (!strcmp(entry->d_name, name)) { CHECK(entry->d_type == DT_DIR); found = 1; }
            offset += entry->d_reclen;
        }
        length = syscall(SYS_getdents64, root, records, sizeof(records));
        CHECK(length >= 0);
    } while (length);
    CHECK(found);
    CHECK(syscall(SYS_getdents64, task, records, sizeof(records)) > 0);
    CHECK(close(task) == 0 && close(root) == 0);

    /* Exercise the old Linux getdents record and descriptor-relative rename
     * through raw syscall numbers, independently of libc wrappers. */
    const char *old_name = "/tmp/linux-abi-creat-old";
    const char *new_name = "/tmp/linux-abi-renameat-new";
    int created = syscall(SYS_creat, old_name, 0600);
    CHECK(created >= 0);
    CHECK(write(created, "x", 1) == 1 && close(created) == 0);
    int tmp = open("/tmp", O_RDONLY | O_DIRECTORY);
    CHECK(tmp >= 0);
    CHECK(syscall(SYS_renameat, AT_FDCWD, old_name, tmp, "linux-abi-renameat-new") == 0);
    _Alignas(8) char old_records[1024];
    int old_found = 0;
    long old_length = syscall(SYS_getdents, tmp, old_records, sizeof(old_records));
    CHECK(old_length > 0);
    for (long offset = 0; offset < old_length;) {
        unsigned long *ino = (unsigned long *)(old_records + offset);
        unsigned short *reclen = (unsigned short *)(old_records + offset + 16);
        CHECK(*ino != 0 && *reclen >= 24 && !(*reclen & 7) && offset + *reclen <= old_length);
        char *entry_name = old_records + offset + 18;
        CHECK(memchr(entry_name, 0, *reclen - 18));
        if (!strcmp(entry_name, "linux-abi-renameat-new")) old_found = 1;
        offset += *reclen;
    }
    CHECK(old_found && close(tmp) == 0);
    CHECK(unlink(new_name) == 0);
    return 0;
}

static int proc_status(void)
{
    int fd = open("/proc/self/status", O_RDONLY);
    CHECK(fd >= 0);
    char text[8192];
    ssize_t length = read(fd, text, sizeof(text) - 1);
    CHECK(length > 0 && close(fd) == 0);
    text[length] = 0;
    CHECK(!strncmp(text, "Name:\t", 6));
    char *value = strstr(text, "\nUid:\t");
    unsigned real, effective, saved, fs;
    CHECK(value && sscanf(value, "\nUid:\t%u%u%u%u", &real, &effective, &saved, &fs) == 4);
    CHECK(real == getuid() && effective == geteuid() && saved == real && fs == effective);
    value = strstr(text, "\nGid:\t");
    CHECK(value && sscanf(value, "\nGid:\t%u%u%u%u", &real, &effective, &saved, &fs) == 4);
    CHECK(real == getgid() && effective == getegid() && saved == real && fs == effective);
    value = strstr(text, "\nPid:\t");
    CHECK(value && sscanf(value, "\nPid:\t%u", &real) == 1 && real == (unsigned)getpid());
    value = strstr(text, "\nTgid:\t");
    CHECK(value && sscanf(value, "\nTgid:\t%u", &real) == 1 && real == (unsigned)getpid());
    value = strstr(text, "\nVmRSS:\t");
    unsigned long rss;
    CHECK(value && sscanf(value, "\nVmRSS:\t%lu kB", &rss) == 1 && rss > 0 && !(rss % 4));
    return 0;
}

static int descriptor_allocation_child(void)
{
    for (int fd = 3; fd < 1024; ++fd) syscall(SYS_close, fd);
    int fd = open("/dev/null", O_RDWR);
    CHECK(fd == 3 && dup(fd) == 4);
    CHECK(fcntl(fd, F_DUPFD_CLOEXEC, 100) == 100);
    CHECK(fcntl(100, F_GETFD) == FD_CLOEXEC);
    CHECK(dup2(100, 700) == 700 && fcntl(700, F_GETFD) == 0);
    CHECK(fcntl(100, F_SETFL, O_NONBLOCK) == 0);
    CHECK(fcntl(fd, F_GETFL) & O_NONBLOCK);
    CHECK(fcntl(700, F_GETFL) & O_NONBLOCK);
    CHECK(dup2(-1, 700) == -1 && errno == EBADF);
    CHECK(fcntl(700, F_GETFL) & O_NONBLOCK);
    CHECK(close(4) == 0);
    int pair[2];
    CHECK(pipe2(pair, O_CLOEXEC) == 0 && pair[0] == 4 && pair[1] == 5);
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0 && pair[0] == 4 && pair[1] == 5);
    CHECK(close(0) == 0 && dup(pair[0]) == 0);
    CHECK(write(pair[1], "s", 1) == 1);
    char ch;
    CHECK(read(0, &ch, 1) == 1 && ch == 's');
    CHECK(close(0) == 0 && close(3) == 0);
    int ends[2];
    CHECK(pipe(ends) == 0 && ends[0] == 0 && ends[1] == 3);
    CHECK(write(ends[1], "p", 1) == 1 && read(ends[0], &ch, 1) == 1 && ch == 'p');
    CHECK(close(ends[0]) == 0 && close(ends[1]) == 0);
    CHECK(socket(AF_UNIX, SOCK_DGRAM, 0) == 0);
    CHECK(close(0) == 0);
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master == 0);
    CHECK(grantpt(master) == 0 && unlockpt(master) == 0);
    CHECK(close(master) == 0);
    /* recvmsg must allocate SCM_RIGHTS from the same lowest-free namespace. */
    char control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec vec = {"r", 1};
    struct msghdr message = {.msg_iov = &vec, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control)};
    struct cmsghdr *hdr = CMSG_FIRSTHDR(&message);
    hdr->cmsg_level = SOL_SOCKET; hdr->cmsg_type = SCM_RIGHTS;
    hdr->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(hdr), &pair[0], sizeof(int));
    CHECK(sendmsg(pair[0], &message, 0) == 1);
    vec.iov_base = &ch;
    CHECK(recvmsg(pair[1], &message, MSG_CMSG_CLOEXEC) == 1 && ch == 'r');
    int received = -1;
    memcpy(&received, CMSG_DATA(CMSG_FIRSTHDR(&message)), sizeof(int));
    CHECK(received == 0 && fcntl(received, F_GETFD) == FD_CLOEXEC);
    return 0;
}

static int descriptor_limits_child(void)
{
    for (int fd = 3; fd < 1024; ++fd) syscall(SYS_close, fd);
    CHECK(open("/dev/null", O_RDWR) == 3 && dup2(3, 100) == 100);
    struct rlimit limit;
    CHECK(syscall(SYS_getrlimit, RLIMIT_NOFILE, &limit) == 0);
    limit.rlim_cur = 8;
    CHECK(syscall(SYS_setrlimit, RLIMIT_NOFILE, &limit) == 0);
    CHECK(dup2(100, 100) == 100);
    CHECK(dup2(3, 8) == -1 && errno == EBADF);
    CHECK(fcntl(3, F_DUPFD, 8) == -1 && errno == EINVAL);
    CHECK(fcntl(3, F_DUPFD, -1) == -1 && errno == EINVAL);
    CHECK(dup2(999, 999) == -1 && errno == EBADF);
    CHECK(open("/dev/null", O_RDWR) == 4);
    int pair[2];
    CHECK(pipe(pair) == 0 && pair[0] == 5 && pair[1] == 6);
    CHECK(socket(AF_UNIX, SOCK_DGRAM, 0) == 7);
    CHECK(socket(AF_UNIX, SOCK_DGRAM, 0) == -1 && errno == EMFILE);
    CHECK(open("/dev/null", O_RDWR) == -1 && errno == EMFILE);
    CHECK(close(3) == 0);
    pair[0] = 123; pair[1] = 456;
    CHECK(pipe(pair) == -1 && errno == EMFILE && pair[0] == 123 && pair[1] == 456);
    CHECK(open("/dev/null", O_RDWR) == 3);
    CHECK(close(0) == 0 && socket(AF_UNIX, SOCK_DGRAM, 0) == 0);
    CHECK(fcntl(100, F_GETFL) >= 0);
    return 0;
}

static int descriptor_allocation(void)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(descriptor_allocation_child());
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

static int descriptor_boundaries_child(void)
{
    for (int fd = 3; fd < 1024; ++fd) syscall(SYS_close, fd);
    void *readonly = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(readonly != MAP_FAILED);
    CHECK(syscall(SYS_pipe, readonly) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_pipe2, readonly, 0) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, readonly) == -1 && errno == EFAULT);
    CHECK(munmap(readonly, 4096) == 0);
    int fd = open("/dev/null", O_RDWR);
    CHECK(fd == 3);
    CHECK(syscall(SYS_fcntl, fd, 0x100000000ULL | F_GETFD, 0) == 0);
    CHECK(syscall(SYS_dup3, fd, 4, 0x100000000ULL | O_CLOEXEC) == 4);
    CHECK(fcntl(4, F_GETFD) == FD_CLOEXEC && close(4) == 0);
    int pair[2];
    CHECK(syscall(SYS_pipe2, pair, 0x100000000ULL | O_NONBLOCK) == 0);
    CHECK(pair[0] == 4 && pair[1] == 5 && (fcntl(4, F_GETFL) & O_NONBLOCK));
    CHECK(close(4) == 0 && close(5) == 0);
    CHECK(syscall(SYS_socket, 0x100000000ULL | AF_UNIX,
                  0x100000000ULL | SOCK_DGRAM, 0x100000000ULL) == 4);
    CHECK(close(4) == 0);
    CHECK(syscall(SYS_socketpair, 0x100000000ULL | AF_UNIX,
                  0x100000000ULL | SOCK_STREAM, 0x100000000ULL, pair) == 0);
    CHECK(pair[0] == 4 && pair[1] == 5);
    struct rlimit limit;
    CHECK(syscall(SYS_getrlimit, RLIMIT_NOFILE, &limit) == 0);
    limit.rlim_cur = 0;
    CHECK(syscall(SYS_setrlimit, RLIMIT_NOFILE, &limit) == 0);
    CHECK(dup(fd) == -1 && errno == EMFILE);
    CHECK(fcntl(fd, F_DUPFD, 0) == -1 && errno == EINVAL);
    CHECK(dup2(fd, fd) == fd);
    CHECK(dup2(fd, 0) == -1 && errno == EBADF);
    return 0;
}

static int descriptor_boundaries(void)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(descriptor_boundaries_child());
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

static int descriptor_limits(void)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(descriptor_limits_child());
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

struct rlimit_worker_state {
    pthread_barrier_t barrier;
    struct rlimit limit;
    pid_t tid;
    int result;
};

static void *rlimit_worker(void *argument)
{
    struct rlimit_worker_state *state = argument;
    state->tid = syscall(SYS_gettid);
    state->result = setrlimit(RLIMIT_NOFILE, &state->limit);
    pthread_barrier_wait(&state->barrier);
    pthread_barrier_wait(&state->barrier);
    struct rlimit observed;
    if (getrlimit(RLIMIT_NOFILE, &observed) || observed.rlim_cur != 64) state->result = 2;
    return NULL;
}

static int resource_limits_child(void)
{
    struct rlimit original, old, value;
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, NULL, &original) == 0);
    CHECK(original.rlim_cur >= 128 && original.rlim_max >= original.rlim_cur);
    CHECK(syscall(SYS_prlimit64, 0x100000000ULL, 0x100000007ULL, NULL, &old) == 0);
    CHECK(!memcmp(&old, &original, sizeof(old)));
    CHECK(syscall(SYS_prlimit64, -1, -1, (void *)1, NULL) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_prlimit64, -1, -1, NULL, (void *)1) == -1 && errno == ESRCH);
    CHECK(syscall(SYS_prlimit64, 0, -1, NULL, (void *)1) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_getrlimit, -1, (void *)1) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_setrlimit, -1, (void *)1) == -1 && errno == EFAULT);
    value = (struct rlimit){129,128};
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, &value, NULL) == -1 && errno == EINVAL);
    value = (struct rlimit){0,RLIM_INFINITY};
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, &value, NULL) == -1 && errno == EPERM);
    value = (struct rlimit){128,128};
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, &value, &old) == 0);
    CHECK(!memcmp(&old, &original, sizeof(old)));
    CHECK(syscall(SYS_getrlimit, 0x100000007ULL, &old) == 0 && old.rlim_cur == 128 && old.rlim_max == 128);
    struct rlimit *readonly = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(readonly != MAP_FAILED);
    CHECK(syscall(SYS_getrlimit, RLIMIT_NOFILE, readonly) == -1 && errno == EFAULT);
    value.rlim_cur = 100;
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, &value, readonly) == -1 && errno == EFAULT);
    CHECK(getrlimit(RLIMIT_NOFILE, &old) == 0 && old.rlim_cur == 100 && old.rlim_max == 128);
    CHECK(munmap(readonly, 4096) == 0);
    value.rlim_cur = 96;
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, &value, &value) == 0);
    CHECK(value.rlim_cur == 100 && value.rlim_max == 128);
    CHECK(getrlimit(RLIMIT_NOFILE, &old) == 0 && old.rlim_cur == 96);
    struct rlimit_worker_state state = {.limit = {80,128}};
    CHECK(pthread_barrier_init(&state.barrier, NULL, 2) == 0);
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, rlimit_worker, &state) == 0);
    pthread_barrier_wait(&state.barrier);
    CHECK(state.result == 0 && getrlimit(RLIMIT_NOFILE, &old) == 0 && old.rlim_cur == 80);
    value = (struct rlimit){64,128};
    CHECK(syscall(SYS_prlimit64, state.tid, RLIMIT_NOFILE, &value, &old) == 0 && old.rlim_cur == 80);
    pthread_barrier_wait(&state.barrier);
    CHECK(pthread_join(worker, NULL) == 0 && state.result == 0);
    CHECK(pthread_barrier_destroy(&state.barrier) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        if (getrlimit(RLIMIT_NOFILE, &old) || old.rlim_cur != 64 || old.rlim_max != 128) _exit(2);
        value.rlim_cur = 60;
        if (setrlimit(RLIMIT_NOFILE, &value)) _exit(3);
        const char *executable = (const char *)getauxval(AT_EXECFN);
        execl(executable, executable, "--rlimit-child", NULL);
        _exit(4);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(getrlimit(RLIMIT_NOFILE, &old) == 0 && old.rlim_cur == 64);
    CHECK(setuid(12345) == 0);
    value = (struct rlimit){64,129};
    CHECK(setrlimit(RLIMIT_NOFILE, &value) == -1 && errno == EPERM);
    CHECK(syscall(SYS_prlimit64, getppid(), RLIMIT_NOFILE, NULL, &old) == -1 && errno == EPERM);
    value = (struct rlimit){0,128};
    CHECK(setrlimit(RLIMIT_NOFILE, &value) == 0);
    CHECK(open("/dev/null", O_RDONLY) == -1 && errno == EMFILE);
    value.rlim_cur = 64;
    CHECK(setrlimit(RLIMIT_NOFILE, &value) == 0);
    CHECK(getrlimit(RLIMIT_AS, &old) == 0);
    value = (struct rlimit){0,old.rlim_max};
    CHECK(setrlimit(RLIMIT_AS, &value) == 0);
    CHECK(mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED && errno == ENOMEM);
    CHECK(setrlimit(RLIMIT_AS, &old) == 0);
    return 0;
}

static int resource_limits(void)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(resource_limits_child());
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

static int resource_address_space_child(void)
{
    char *kept = mmap(NULL, 12288, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(kept != MAP_FAILED && munmap(kept + 8192, 4096) == 0);
    kept[0] = 73;
    struct rlimit original;
    CHECK(getrlimit(RLIMIT_AS, &original) == 0);
    /* Find the current VM-size boundary without assuming a loader's layout. */
    unsigned lower = 0, upper = 65536;
    while (lower < upper) {
        unsigned middle = lower + (upper - lower) / 2;
        struct rlimit value = {(rlim_t)middle * 4096, original.rlim_max};
        CHECK(setrlimit(RLIMIT_AS, &value) == 0);
        void *page = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED) { CHECK(errno == ENOMEM); lower = middle + 1; }
        else { CHECK(munmap(page, 4096) == 0); upper = middle; }
    }
    CHECK(lower > 1 && lower < 65536);
    struct rlimit value = {(rlim_t)(lower - 1) * 4096, original.rlim_max};
    CHECK(setrlimit(RLIMIT_AS, &value) == 0);
    CHECK(mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED && errno == ENOMEM);
    CHECK(mmap(kept, 8192, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == kept);
    CHECK(kept[0] == 0);
    kept[0] = 91;
    CHECK(mmap(kept, 12288, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED && errno == ENOMEM);
    CHECK(kept[0] == 91);
    value.rlim_cur = 0;
    CHECK(setrlimit(RLIMIT_AS, &value) == 0);
    CHECK(mmap(NULL, 0, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED && errno == EINVAL);
    CHECK(mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_SHARED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED && errno == EINVAL);
    CHECK(mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, -1, 0) == MAP_FAILED && errno == EBADF);
    CHECK(setrlimit(RLIMIT_AS, &original) == 0 && munmap(kept, 8192) == 0);
    return 0;
}

static int resource_address_space(void)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(resource_address_space_child());
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

struct affinity_worker_state {
    pthread_barrier_t barrier;
    cpu_set_t observed;
    int result;
};

static void *affinity_worker(void *argument)
{
    struct affinity_worker_state *state = argument;
    pthread_barrier_wait(&state->barrier);
    state->result = pthread_getaffinity_np(pthread_self(), sizeof(state->observed), &state->observed);
    return NULL;
}

static int thread_affinity(void)
{
    uint64_t raw[3] = {0, 0x123456789abcdef0ULL, 0xfedcba9876543210ULL};
    CHECK(syscall(SYS_sched_getaffinity, 0, sizeof(raw), raw) == 8 && raw[0]);
    CHECK(raw[1] == 0x123456789abcdef0ULL && raw[2] == 0xfedcba9876543210ULL);
    uint64_t original = raw[0], selected = original & -original;
    CHECK(syscall(SYS_sched_getaffinity, 0, 7, (void *)1) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_sched_getaffinity, 0, 9, raw) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_sched_getaffinity, -1, 8, (void *)1) == -1 && errno == ESRCH);
    CHECK(syscall(SYS_sched_getaffinity, 0, 8, (void *)1) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_sched_getaffinity, 0x100000000ULL, 0x100000008ULL, raw) == 8);
    CHECK(syscall(SYS_sched_setaffinity, 0, 0, NULL) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_sched_setaffinity, -1, 0, NULL) == -1 && errno == ESRCH);
    CHECK(syscall(SYS_sched_setaffinity, -1, 8, (void *)1) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_sched_setaffinity, 0, 1, &selected) == 0);
    char *memory = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(memory != MAP_FAILED && mprotect(memory + 4096, 4096, PROT_NONE) == 0);
    uint64_t *edge = (void *)(memory + 4096 - 8);
    *edge = selected;
    CHECK(syscall(SYS_sched_setaffinity, 0, 128, edge) == 0);
    CHECK(syscall(SYS_sched_getaffinity, 0, 128, edge) == 8 && *edge == selected);
    CHECK(mprotect(memory, 4096, PROT_READ) == 0);
    CHECK(syscall(SYS_sched_getaffinity, 0, 8, edge) == -1 && errno == EFAULT);
    CHECK(munmap(memory, 8192) == 0);
    cpu_set_t set;
    memset(&set, 0xff, sizeof(set));
    CHECK(pthread_getaffinity_np(pthread_self(), sizeof(set), &set) == 0);
    CHECK(CPU_COUNT(&set) == 1 && *(uint64_t *)&set == selected);
    struct affinity_worker_state state = {0};
    CHECK(pthread_barrier_init(&state.barrier, NULL, 2) == 0);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, affinity_worker, &state) == 0);
    CHECK(pthread_setaffinity_np(thread, sizeof(set), &set) == 0);
    CHECK(pthread_getaffinity_np(thread, sizeof(set), &set) == 0 && CPU_COUNT(&set) == 1);
    pthread_barrier_wait(&state.barrier);
    CHECK(pthread_join(thread, NULL) == 0 && state.result == 0 && CPU_EQUAL(&set, &state.observed));
    CHECK(pthread_barrier_destroy(&state.barrier) == 0);
    CHECK(syscall(SYS_sched_setaffinity, 0, 8, &original) == 0);
    pid_t parent = getpid(), child = fork();
    CHECK(child >= 0);
    if (!child) {
        if (setuid(12345)) _exit(2);
        if (syscall(SYS_sched_getaffinity, parent, 8, raw) != 8) _exit(3);
        if (syscall(SYS_sched_setaffinity, parent, 8, &selected) != -1 || errno != EPERM) _exit(4);
        if (syscall(SYS_sched_setaffinity, 0, 8, &selected)) _exit(5);
        _exit(0);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

/* No libc/TLS access may occur between setting an unmapped FS base and restoring it. */
static long temporary_fs_base(uint64_t base, uint64_t original)
{
    register long result __asm__("rax") = SYS_arch_prctl;
    register uint64_t saved __asm__("r9") = original;
    register long first __asm__("r8");
    register long option __asm__("rdi") = 0x1002;
    register uint64_t argument __asm__("rsi") = base;
    __asm__ volatile("syscall; mov %%rax, %%r8; mov %%r9, %%rsi; mov $158, %%eax; syscall"
        : "+a"(result), "=r"(first), "+D"(option), "+S"(argument)
        : "r"(saved) : "rcx", "r11", "memory", "cc");
    return first;
}

/* Fork-like native clone: the child restores its libc TLS before returning to C. */
static long clone_with_fs(uint64_t base, uint64_t original)
{
    register long result __asm__("rax") = SYS_clone;
    register long flags __asm__("rdi") = SIGCHLD | CLONE_SETTLS;
    register long stack __asm__("rsi") = 0;
    register long child_tid __asm__("r10") = 0;
    register uint64_t tls __asm__("r8") = base;
    register uint64_t saved __asm__("r9") = original;
    __asm__ volatile("syscall; test %%rax, %%rax; jnz 1f; mov $0x1002, %%edi; "
                     "mov %%r9, %%rsi; mov $158, %%eax; syscall; 1:"
        : "+a"(result), "+D"(flags), "+S"(stack)
        : "d"(0L), "r"(child_tid), "r"(tls), "r"(saved)
        : "rcx", "r11", "memory", "cc");
    return result;
}

static int thread_tls_control(void)
{
    uint64_t original = 0, observed = 0;
    CHECK(syscall(SYS_arch_prctl, 0x1003, &original) == 0 && original);
    CHECK(syscall(SYS_arch_prctl, 0x100001003ULL, &observed) == 0 && observed == original);
    CHECK(syscall(SYS_arch_prctl, -1, 0) == -1 && errno == EINVAL);
    const uint64_t invalid[] = {0x7ffffffff000ULL, 0x800000000000ULL,
                                0x100000000000000ULL, UINT64_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(syscall(SYS_arch_prctl, 0x1002, invalid[i]) == -1 && errno == EPERM);
        CHECK(syscall(SYS_arch_prctl, 0x1003, &observed) == 0 && observed == original);
        CHECK(clone_with_fs(invalid[i], original) == -EPERM);
    }
    CHECK(temporary_fs_base(0, original) == 0);
    CHECK(temporary_fs_base(0x100000000ULL, original) == 0);
    CHECK(temporary_fs_base(0x7fffffffefffULL, original) == 0);
    CHECK(syscall(SYS_arch_prctl, 0x1003, &observed) == 0 && observed == original);
    char *page = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(page != MAP_FAILED && mprotect(page + 4096, 4096, PROT_NONE) == 0);
    CHECK(syscall(SYS_arch_prctl, 0x1003, page + 4092) == -1 && errno == EFAULT);
    CHECK(mprotect(page, 4096, PROT_READ) == 0);
    CHECK(syscall(SYS_arch_prctl, 0x1003, page) == -1 && errno == EFAULT);
    CHECK(munmap(page, 8192) == 0);
    long child = clone_with_fs(0x100000000ULL, original);
    CHECK(child >= 0);
    if (!child) _exit(tls_value != 91);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(tls_value == 91);
    return 0;
}

static int thread_tid_registration(void)
{
    uint32_t *word = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK(word != MAP_FAILED);
    for (unsigned test = 0; test < 4; ++test) {
        *word = 0x12345678;
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) {
            void *pointer = test == 0 ? (void *)1 : test == 1 ? (void *)UINT64_MAX : word;
            if (test == 3 && mprotect(word, 4096, PROT_READ)) _exit(2);
            if (syscall(SYS_set_tid_address, pointer) != syscall(SYS_gettid)) _exit(3);
            /* Registration never writes; a private mm must not clear a shared mapping on exit. */
            if (*word != 0x12345678) _exit(4);
            _exit(0);
        }
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(*word == 0x12345678);
    }
    CHECK(munmap(word, 4096) == 0);
    return 0;
}

/* The child uses only raw syscalls on its private stack, without entering libc. */
static long clone_register_tid(void *top, void *pointer)
{
    register long result __asm__("rax") = SYS_clone;
    register long flags __asm__("rdi") = SIGCHLD | CLONE_VM;
    register void *stack __asm__("rsi") = top;
    register void *tid __asm__("r10") = pointer;
    register long tls __asm__("r8") = 0;
    __asm__ volatile("syscall; test %%rax, %%rax; jnz 1f; mov %%r10, %%rdi; "
                     "mov $218, %%eax; syscall; xor %%edi, %%edi; mov $60, %%eax; syscall; ud2; 1:"
        : "+a"(result), "+D"(flags), "+S"(stack)
        : "d"(0L), "r"(tid), "r"(tls) : "rcx", "r11", "memory", "cc");
    return result;
}

static int thread_tid_exit(void)
{
    char *stack = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    char *memory = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(stack != MAP_FAILED && memory != MAP_FAILED);
    for (unsigned test = 0; test < 5; ++test) {
        unsigned offset = test == 1 ? 1 : test == 2 ? 4094 : 0;
        uint32_t sentinel = 0x12345678, observed;
        memset(memory, 0xab, 8192);
        memcpy(memory + offset, &sentinel, sizeof(sentinel));
        if (test == 3) CHECK(mprotect(memory, 8192, PROT_READ) == 0);
        long child = clone_register_tid(stack + 16384, test == 4 ? (void *)1 : memory + offset);
        CHECK(child > 0);
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        memcpy(&observed, memory + offset, sizeof(observed));
        CHECK(observed == (test < 3 ? 0 : sentinel));
        if (offset) CHECK((unsigned char)memory[offset - 1] == 0xab);
        CHECK((unsigned char)memory[offset + 4] == 0xab);
        if (test == 3) CHECK(mprotect(memory, 8192, PROT_READ | PROT_WRITE) == 0);
    }
    CHECK(munmap(memory, 8192) == 0 && munmap(stack, 16384) == 0);
    return 0;
}

static void *thread_main(void *arg)
{
    (void)arg;
    if (tls_value != 73) return (void *)1;
    tls_value = 19;
    void *p = calloc(4096, 1);
    if (!p || !mi_is_in_heap_region(p)) return (void *)2;
    free(p);
    return NULL;
}

static int threads(void)
{
    pthread_t thread;
    int error = pthread_create(&thread, NULL, thread_main, NULL);
    if (error) errno = error;
    CHECK(error == 0);
    void *result = (void *)3;
    CHECK(pthread_join(thread, &result) == 0 && result == NULL);
    CHECK(tls_value == 91);
    return 0;
}

static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t counter_cond = PTHREAD_COND_INITIALIZER;
static pthread_once_t counter_once = PTHREAD_ONCE_INIT;
static pthread_key_t counter_key;
static unsigned counter_value, counter_started, counter_go, once_count;
static _Atomic unsigned destructor_count;
static pid_t process_id;
static int shared_fd;
static void once_function(void) { ++once_count; }
static void key_destructor(void *ptr) { free(ptr); atomic_fetch_add(&destructor_count, 1); }

static void *counter_thread(void *arg)
{
    uintptr_t id = (uintptr_t)arg;
    if (getpid() != process_id || syscall(SYS_gettid) == process_id || tls_value != 73) return (void *)1;
    tls_value = 100 + id;
    if (pthread_once(&counter_once, once_function)) return (void *)2;
    void *allocation = malloc(1024);
    if (!allocation || pthread_setspecific(counter_key, allocation)) return (void *)3;
    if (pthread_mutex_lock(&counter_mutex)) return (void *)4;
    ++counter_started;
    pthread_cond_broadcast(&counter_cond);
    while (!counter_go) if (pthread_cond_wait(&counter_cond, &counter_mutex)) return (void *)5;
    pthread_mutex_unlock(&counter_mutex);
    for (unsigned i = 0; i < 80; ++i) {
        if (pthread_mutex_lock(&counter_mutex)) return (void *)6;
        unsigned old = counter_value;
        if (!(i % 20)) usleep(1000);
        counter_value = old + 1;
        pthread_mutex_unlock(&counter_mutex);
        void *p = calloc(16, 128);
        if (!p || tls_value != 100 + id) return (void *)7;
        free(p);
    }
    if (!id) shared_fd = open("/dev/null", O_RDWR);
    return NULL;
}

static int thread_contention(void)
{
    pthread_t workers[4];
    process_id = getpid();
    shared_fd = -1;
    CHECK(pthread_key_create(&counter_key, key_destructor) == 0);
    for (unsigned i = 0; i < 4; ++i) CHECK(pthread_create(&workers[i], NULL, counter_thread, (void *)(uintptr_t)i) == 0);
    CHECK(pthread_mutex_lock(&counter_mutex) == 0);
    while (counter_started != 4) CHECK(pthread_cond_wait(&counter_cond, &counter_mutex) == 0);
    counter_go = 1;
    CHECK(pthread_cond_broadcast(&counter_cond) == 0 && pthread_mutex_unlock(&counter_mutex) == 0);
    for (unsigned i = 0; i < 4; ++i) {
        void *result = (void *)99;
        CHECK(pthread_join(workers[i], &result) == 0 && result == NULL);
    }
    CHECK(counter_value == 320 && once_count == 1 && atomic_load(&destructor_count) == 4 && tls_value == 91);
    CHECK(shared_fd >= 0 && write(shared_fd, "shared", 6) == 6 && close(shared_fd) == 0);
    CHECK(pthread_key_delete(counter_key) == 0);
    CHECK(pthread_mutex_lock(&counter_mutex) == 0);
    struct timespec timeout;
    CHECK(clock_gettime(CLOCK_REALTIME, &timeout) == 0);
    timeout.tv_nsec += 20000000;
    if (timeout.tv_nsec >= 1000000000) { ++timeout.tv_sec; timeout.tv_nsec -= 1000000000; }
    CHECK(pthread_cond_timedwait(&counter_cond, &counter_mutex, &timeout) == ETIMEDOUT);
    CHECK(pthread_mutex_unlock(&counter_mutex) == 0);
    return 0;
}

static int send_rights(int socket_fd, const int *fds, unsigned count, const char *text)
{
    char control[CMSG_SPACE(2 * sizeof(int))] = {0};
    struct iovec vectors[2] = {{(void *)text, 1}, {(void *)(text + 1), strlen(text) - 1}};
    struct msghdr message = {.msg_iov = vectors, .msg_iovlen = 2,
        .msg_control = control, .msg_controllen = CMSG_SPACE(count * sizeof(int))};
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(count * sizeof(int));
    memcpy(CMSG_DATA(header), fds, count * sizeof(int));
    return sendmsg(socket_fd, &message, MSG_NOSIGNAL);
}

static int receive_right(int socket_fd, unsigned flags, int *descriptor)
{
    char control[CMSG_SPACE(sizeof(int))] = {0}, data[2];
    struct iovec vectors[2] = {{data, 1}, {data + 1, 1}};
    struct msghdr message = {.msg_iov = vectors, .msg_iovlen = 2,
        .msg_control = control, .msg_controllen = sizeof(control)};
    CHECK(recvmsg(socket_fd, &message, flags) == 2);
    CHECK(data[0] == 'a' && data[1] == 'b' && !(message.msg_flags & MSG_CTRUNC));
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    CHECK(header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
          header->cmsg_len == CMSG_LEN(sizeof(int)) && message.msg_controllen >= CMSG_LEN(sizeof(int)));
    memcpy(descriptor, CMSG_DATA(header), sizeof(int));
    return 0;
}

static int unix_rights(void)
{
    char path[128], data[2];
    snprintf(path, sizeof(path), "/tmp/musl-ofd-%ld", (long)getpid());
    int original = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(original >= 0 && write(original, "1234", 4) == 4 && lseek(original, 0, SEEK_SET) == 0);
    int duplicate = dup(original);
    CHECK(duplicate >= 0 && fcntl(duplicate, F_SETFD, FD_CLOEXEC) == 0 && fcntl(original, F_GETFD) == 0);
    CHECK(fcntl(duplicate, F_SETFL, O_NONBLOCK) == 0 && (fcntl(original, F_GETFL) & O_NONBLOCK));
    int pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    int invalid = -1;
    CHECK(send_rights(pair[0], &invalid, 1, "ab") == -1 && errno == EBADF);
    CHECK(recv(pair[1], data, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    CHECK(send_rights(pair[0], &original, 1, "ab") == 2 && close(original) == 0);
    int peeked, received;
    CHECK(receive_right(pair[1], MSG_PEEK | MSG_CMSG_CLOEXEC, &peeked) == 0);
    CHECK((fcntl(peeked, F_GETFD) & FD_CLOEXEC) && read(peeked, data, 1) == 1 && data[0] == '1');
    CHECK(receive_right(pair[1], 0, &received) == 0 && received != peeked);
    CHECK(fcntl(received, F_GETFD) == 0 && read(received, data, 1) == 1 && data[0] == '2');
    CHECK(lseek(duplicate, 0, SEEK_CUR) == 2);
    CHECK(close(peeked) == 0 && close(received) == 0 && close(duplicate) == 0);
    CHECK(unlink(path) == 0);
    int pipe_fds[2];
    CHECK(pipe(pipe_fds) == 0 && send_rights(pair[0], &pipe_fds[0], 1, "ab") == 2 && close(pipe_fds[0]) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        int incoming;
        if (receive_right(pair[1], MSG_CMSG_CLOEXEC, &incoming)) _exit(1);
        if (read(incoming, data, 1) != 1 || data[0] != 'x') _exit(2);
        close(incoming);
        _exit(0);
    }
    CHECK(write(pipe_fds[1], "x", 1) == 1);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    struct pollfd pipe_status = {pipe_fds[1], POLLOUT, 0};
    CHECK(poll(&pipe_status, 1, 0) == 1 && (pipe_status.revents & POLLERR));
    CHECK(close(pipe_fds[1]) == 0 && close(pair[0]) == 0 && close(pair[1]) == 0);
    return 0;
}

static int unix_rights_boundaries(void)
{
    int original = open("/dev/null", O_RDONLY);
    CHECK(original >= 0);
    for (unsigned test = 0; test < 5; ++test) {
        int pair[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        CHECK(write(pair[0], "ab", 2) == 2);
        CHECK(send_rights(pair[0], &original, 1, "cde") == 3);
        CHECK(write(pair[0], "fg", 2) == 2);
        char data[8] = {0};
        union { struct cmsghdr align; char data[CMSG_SPACE(sizeof(int))]; } control;
        struct iovec vector = {data, 7};
        struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1,
            .msg_control = control.data, .msg_controllen = sizeof(control.data)};
        if (test == 3) {
            CHECK(read(pair[1], data, 7) == 5 && !memcmp(data, "abcde", 5));
        } else if (test == 4) {
            struct iovec vectors[2] = {{data, 3}, {data + 3, 4}};
            CHECK(readv(pair[1], vectors, 2) == 5 && !memcmp(data, "abcde", 5));
        } else {
            CHECK(recvmsg(pair[1], &message, test == 1 ? MSG_PEEK : test == 2 ? 0 : MSG_WAITALL) == 5);
            CHECK(!memcmp(data, "abcde", 5) && !(message.msg_flags & MSG_CTRUNC));
            struct cmsghdr *header = CMSG_FIRSTHDR(&message);
            CHECK(header && header->cmsg_type == SCM_RIGHTS && header->cmsg_level == SOL_SOCKET &&
                  header->cmsg_len == CMSG_LEN(sizeof(int)));
            int received;
            memcpy(&received, CMSG_DATA(header), sizeof(received));
            CHECK(received >= 0 && close(received) == 0);
            if (test == 1) {
                /* The peeked descriptor must not detach the queued reference. */
                vector.iov_len = 3;
                message.msg_controllen = sizeof(control.data);
                CHECK(recvmsg(pair[1], &message, 0) == 3 && !memcmp(data, "abc", 3));
                header = CMSG_FIRSTHDR(&message);
                CHECK(header && header->cmsg_type == SCM_RIGHTS);
                memcpy(&received, CMSG_DATA(header), sizeof(received));
                CHECK(close(received) == 0);
                CHECK(recv(pair[1], data, 4, MSG_WAITALL) == 4 && !memcmp(data, "defg", 4));
            }
        }
        if (test != 1) CHECK(recv(pair[1], data, 7, MSG_DONTWAIT) == 2 && !memcmp(data, "fg", 2));
        CHECK(recv(pair[1], data, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN);
        CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    }
    CHECK(close(original) == 0);
    return 0;
}

static void *last_thread(void *arg)
{
    _Atomic int *state = arg;
    usleep(30000);
    atomic_store(state, 42);
    return NULL;
}

static int thread_exit_lifecycle(void)
{
    struct robust_head { uintptr_t next; intptr_t offset; uintptr_t pending; };
    struct robust_node { uintptr_t next; uint32_t word; };
    unsigned char *shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK(shared != MAP_FAILED);
    struct robust_head *head = (void *)shared;
    struct robust_node *node = (void *)(shared + 128);
    head->next = (uintptr_t)node;
    head->offset = offsetof(struct robust_node, word);
    node->next = (uintptr_t)head;
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        node->word = (uint32_t)syscall(SYS_gettid) | 0x80000000U;
        if (syscall(SYS_set_robust_list, head, sizeof(*head))) _exit(3);
        uintptr_t actual = 0;
        size_t size = 0;
        if (syscall(SYS_get_robust_list, 0, &actual, &size) || actual != (uintptr_t)head || size != sizeof(*head)) _exit(4);
        syscall(SYS_exit, 0);
        __builtin_unreachable();
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(node->word == 0xc0000000U);
    _Atomic int *state = (void *)(shared + 256);
    child = fork();
    CHECK(child >= 0);
    if (!child) {
        pthread_t worker;
        if (pthread_create(&worker, NULL, last_thread, state)) _exit(5);
        pthread_exit(NULL);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(atomic_load(state) == 42);
    CHECK(munmap(shared, 4096) == 0);
    return 0;
}

static atomic_int cancel_ready, cancel_cleaned;
static void cancel_cleanup(void *arg)
{
    (void)arg;
    atomic_fetch_add(&cancel_cleaned, 1);
}

static void *cancel_worker(void *arg)
{
    int fd = *(int *)arg;
    pthread_cleanup_push(cancel_cleanup, NULL);
    atomic_store(&cancel_ready, 1);
    char byte;
    read(fd, &byte, 1);
    pthread_cleanup_pop(1);
    return (void *)1;
}

static int thread_cancellation(void)
{
    int pair[2];
    CHECK(pipe(pair) == 0);
    pthread_t worker;
    atomic_store(&cancel_ready, 0);
    atomic_store(&cancel_cleaned, 0);
    CHECK(pthread_create(&worker, NULL, cancel_worker, &pair[0]) == 0);
    while (!atomic_load(&cancel_ready)) sched_yield();
    usleep(20000);
    CHECK(pthread_cancel(worker) == 0);
    void *result;
    CHECK(pthread_join(worker, &result) == 0 && result == PTHREAD_CANCELED);
    CHECK(atomic_load(&cancel_cleaned) == 1);
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    return 0;
}

static volatile sig_atomic_t signal_seen, signal_errors, signal_change_return;
static uintptr_t signal_alt_low, signal_alt_high;
static void native_signal_handler(int sig, siginfo_t *info, void *opaque)
{
    ucontext_t *context = opaque;
    char local;
    if (info->si_signo != sig || !context || !context->uc_mcontext.fpregs) ++signal_errors;
    if (signal_alt_low && ((uintptr_t)&local < signal_alt_low || (uintptr_t)&local >= signal_alt_high))
        ++signal_errors;
    if (signal_change_return) {
        context->uc_mcontext.gregs[REG_RAX] = -EINTR;
        __asm__ volatile("pxor %%xmm15, %%xmm15" ::: "xmm15");
    }
    ++signal_seen;
}

static int native_signals(void)
{
    struct sigaction action = {.sa_sigaction = native_signal_handler,
        .sa_flags = SA_SIGINFO | SA_ONSTACK}, queried;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGRTMAX, &action, NULL) == 0);
    CHECK(sigaction(SIGRTMAX, NULL, &queried) == 0 && queried.sa_sigaction == action.sa_sigaction &&
          (queried.sa_flags & SA_SIGINFO));
    void *stack = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(stack != MAP_FAILED);
    stack_t alt = {.ss_sp = stack, .ss_size = 16384}, old;
    CHECK(sigaltstack(&alt, &old) == 0 && (old.ss_flags & SS_DISABLE));
    signal_alt_low = (uintptr_t)stack;
    signal_alt_high = signal_alt_low + 16384;
    signal_seen = signal_errors = 0;
    sigset_t blocked, before, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGRTMAX);
    CHECK(pthread_sigmask(SIG_BLOCK, &blocked, &before) == 0);
    CHECK(syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), SIGRTMAX) == 0);
    CHECK(signal_seen == 0 && sigpending(&pending) == 0 && sigismember(&pending, SIGRTMAX));
    CHECK(pthread_sigmask(SIG_SETMASK, &before, NULL) == 0 && signal_seen == 1 && !signal_errors);
    uint64_t expected[2] = {0x1234567890abcdefULL, 0xfedcba0987654321ULL}, actual[2];
    signal_change_return = 1;
    long result = SYS_tgkill;
    long pid = getpid(), tid = syscall(SYS_gettid);
    __asm__ volatile("movdqu %2, %%xmm15\n\tsyscall\n\tmovdqu %%xmm15, %1"
        : "+a"(result), "=m"(actual) : "m"(expected), "D"(pid), "S"(tid), "d"((long)SIGRTMAX)
        : "rcx", "r11", "xmm15", "memory");
    signal_change_return = 0;
    CHECK(result == -EINTR && !memcmp(expected, actual, sizeof(actual)) && signal_seen == 2 && !signal_errors);
    signal_alt_low = signal_alt_high = 0;
    alt.ss_flags = SS_DISABLE;
    CHECK(sigaltstack(&alt, NULL) == 0 && munmap(stack, 16384) == 0);
    action.sa_handler = SIG_DFL;
    action.sa_flags = 0;
    CHECK(sigaction(SIGRTMAX, &action, NULL) == 0);
    return 0;
}

static atomic_int interrupted_read_ready;
static void *interrupted_reader(void *arg)
{
    atomic_store(&interrupted_read_ready, 1);
    char byte;
    ssize_t count = read(*(int *)arg, &byte, 1);
    return (void *)(intptr_t)(count < 0 ? errno : 100 + count);
}

static int thread_signal_restart(void)
{
    struct sigaction action = {.sa_sigaction = native_signal_handler, .sa_flags = SA_SIGINFO};
    sigemptyset(&action.sa_mask);
    for (unsigned restart = 0; restart < 2; ++restart) {
        action.sa_flags = SA_SIGINFO | (restart ? SA_RESTART : 0);
        CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
        int pipe_fds[2];
        CHECK(pipe(pipe_fds) == 0);
        pthread_t worker;
        atomic_store(&interrupted_read_ready, 0);
        CHECK(pthread_create(&worker, NULL, interrupted_reader, &pipe_fds[0]) == 0);
        while (!atomic_load(&interrupted_read_ready)) sched_yield();
        usleep(20000);
        CHECK(pthread_kill(worker, SIGUSR1) == 0);
        usleep(20000);
        if (restart) CHECK(write(pipe_fds[1], "x", 1) == 1);
        void *result;
        CHECK(pthread_join(worker, &result) == 0 && (intptr_t)result == (restart ? 101 : EINTR));
        CHECK(close(pipe_fds[0]) == 0 && close(pipe_fds[1]) == 0);
    }
    action.sa_handler = SIG_DFL;
    action.sa_flags = 0;
    CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
    return 0;
}

static const char *probe_executable;
static void *exec_worker(void *arg)
{
    if (pthread_kill(pthread_self(), SIGUSR1) || alarm(4) != 0) _exit(95);
    char expected_pid[24], fd_text[24];
    snprintf(expected_pid, sizeof(expected_pid), "%ld", (long)getpid());
    snprintf(fd_text, sizeof(fd_text), "%d", *(int *)arg);
    char *arguments[] = {(char *)probe_executable, "--thread-exec", expected_pid, fd_text, NULL};
    execv(probe_executable, arguments);
    _exit(93);
}

static int thread_exec(void)
{
    int pair[2];
    CHECK(pipe2(pair, O_CLOEXEC) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        sigset_t blocked;
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGUSR1);
        sigaddset(&blocked, SIGUSR2);
        if (pthread_sigmask(SIG_BLOCK, &blocked, NULL) || kill(getpid(), SIGUSR2)) _exit(96);
        pthread_t worker;
        if (pthread_create(&worker, NULL, exec_worker, &pair[0])) _exit(94);
        for (;;) pause();
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(fcntl(pair[0], F_GETFD) == FD_CLOEXEC);
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    return 0;
}

static int unix_rights_cycles(void)
{
    for (unsigned iteration = 0; iteration < 80; ++iteration) {
        int pair[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
        CHECK(send_rights(pair[0], &pair[1], 1, "ab") == 2);
        CHECK(send_rights(pair[1], &pair[0], 1, "ab") == 2);
        CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    }
    return 0;
}

static int unix_packets(void)
{
    for (unsigned i = 0; i < 2; ++i) {
        int pair[2], type = i ? SOCK_SEQPACKET : SOCK_DGRAM;
        CHECK(socketpair(AF_UNIX, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair) == 0);
        char data[16];
        CHECK(send(pair[0], "abcdef", 6, 0) == 6 && send(pair[0], "xy", 2, 0) == 2);
        CHECK(recv(pair[1], data, 3, MSG_PEEK | MSG_TRUNC) == 6 && !memcmp(data, "abc", 3));
        CHECK(recv(pair[1], data, 3, 0) == 3 && !memcmp(data, "abc", 3));
        CHECK(recv(pair[1], data, sizeof(data), 0) == 2 && !memcmp(data, "xy", 2));
        CHECK(send(pair[0], "", 0, 0) == 0);
        struct pollfd event = {pair[1], POLLIN, 0};
        CHECK(poll(&event, 1, 0) == 1 && (event.revents & POLLIN));
        CHECK(recv(pair[1], data, sizeof(data), 0) == 0);
        CHECK(recv(pair[1], data, sizeof(data), 0) == -1 && errno == EAGAIN);
        int reported = 0;
        socklen_t length = sizeof(reported);
        CHECK(getsockopt(pair[0], SOL_SOCKET, SO_TYPE, &reported, &length) == 0 && reported == type);
        CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    }
    int left = socket(AF_UNIX, SOCK_DGRAM, 0), right = socket(AF_UNIX, SOCK_DGRAM, 0);
    CHECK(left >= 0 && right >= 0);
    struct sockaddr_un a = {.sun_family = AF_UNIX}, b = {.sun_family = AF_UNIX}, from;
    snprintf(a.sun_path + 1, sizeof(a.sun_path) - 1, "musl-a-%ld", (long)getpid());
    snprintf(b.sun_path + 1, sizeof(b.sun_path) - 1, "musl-b-%ld", (long)getpid());
    socklen_t alen = 3 + strlen(a.sun_path + 1), blen = 3 + strlen(b.sun_path + 1);
    CHECK(bind(left, (void *)&a, alen) == 0 && bind(right, (void *)&b, blen) == 0);
    CHECK(sendto(left, "named", 5, 0, (void *)&b, blen) == 5);
    char data[8];
    socklen_t length = sizeof(from);
    CHECK(recvfrom(right, data, sizeof(data), 0, (void *)&from, &length) == 5 && length == alen &&
          !memcmp(&from, &a, alen) && !memcmp(data, "named", 5));
    CHECK(close(left) == 0 && close(right) == 0);
    return 0;
}

struct waitall_sender { int fd; pthread_t receiver; int interrupt; };
static void *waitall_writer(void *arg)
{
    struct waitall_sender *sender = arg;
    if (sender->interrupt) {
        if (send(sender->fd, "short", 5, 0) != 5) return (void *)1;
        usleep(20000);
        return (void *)(intptr_t)pthread_kill(sender->receiver, SIGUSR1);
    }
    unsigned char bytes[3000];
    for (unsigned offset = 0; offset < 40000;) {
        unsigned count = 40000 - offset;
        if (count > sizeof(bytes)) count = sizeof(bytes);
        for (unsigned i = 0; i < count; ++i) bytes[i] = (unsigned char)(offset + i);
        ssize_t written = send(sender->fd, bytes, count, 0);
        if (written <= 0) return (void *)1;
        offset += (unsigned)written;
        usleep(10000);
    }
    return NULL;
}

static int unix_waitall(void)
{
    int pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    int enabled = 1;
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    struct waitall_sender sender = {pair[0], pthread_self(), 0};
    pthread_t writer;
    CHECK(pthread_create(&writer, NULL, waitall_writer, &sender) == 0);
    unsigned char *data = malloc(40000);
    CHECK(data);
    struct iovec vectors[2] = {{data, 13000}, {data + 13000, 27000}};
    union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(sizeof(struct ucred))]; } control;
    struct msghdr message = {.msg_iov = vectors, .msg_iovlen = 2,
        .msg_control = control.bytes, .msg_controllen = sizeof(control)};
    CHECK(recvmsg(pair[1], &message, MSG_WAITALL) == 40000);
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    CHECK(header && header->cmsg_type == SCM_CREDENTIALS && !(message.msg_flags & MSG_CTRUNC));
    for (unsigned i = 0; i < 40000; ++i) CHECK(data[i] == (unsigned char)i);
    void *result;
    CHECK(pthread_join(writer, &result) == 0 && !result);
    struct sigaction action = {.sa_sigaction = native_signal_handler, .sa_flags = SA_SIGINFO | SA_RESTART};
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
    sender.interrupt = 1;
    CHECK(pthread_create(&writer, NULL, waitall_writer, &sender) == 0);
    vectors[0] = (struct iovec){data, 12};
    message = (struct msghdr){.msg_iov = vectors, .msg_iovlen = 1,
        .msg_control = control.bytes, .msg_controllen = sizeof(control)};
    CHECK(recvmsg(pair[1], &message, MSG_WAITALL) == 5 && !memcmp(data, "short", 5));
    CHECK(message.msg_controllen == 0 && message.msg_flags == 0);
    CHECK(pthread_join(writer, &result) == 0 && !result);
    action.sa_handler = SIG_DFL;
    action.sa_flags = 0;
    CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    free(data);
    return 0;
}

static volatile sig_atomic_t socket_sigpipe_count;
static void socket_sigpipe(int signo) { socket_sigpipe_count += signo == SIGPIPE; }

static int unix_credentials(void)
{
    int pair[2], enabled = 1;
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    unsigned char value[32];
    memset(value, 0xa5, sizeof(value));
    socklen_t size = 1;
    CHECK(getsockopt(pair[0], SOL_SOCKET, SO_TYPE, value, &size) == 0);
    CHECK(size == 1 && value[0] == SOCK_STREAM && value[1] == 0xa5);
    size = 0;
    CHECK(getsockopt(pair[0], SOL_SOCKET, SO_TYPE, NULL, &size) == 0 && !size);
    size = (socklen_t)-1;
    CHECK(getsockopt(pair[0], SOL_SOCKET, SO_TYPE, value, &size) == -1 && errno == EINVAL);
    size = 3;
    CHECK(getsockopt(pair[0], SOL_SOCKET, SO_PEERCRED, value, &size) == 0 && size == 3);
    CHECK(getsockopt(pair[0], SOL_SOCKET, 0x7fff, value, &size) == -1 && errno == ENOPROTOOPT);
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &enabled, 3) == -1 && errno == EINVAL);
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    size = sizeof(enabled);
    int state = 0;
    CHECK(getsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &state, &size) == 0 && state == 1);
    char byte;
    union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(sizeof(int))]; } control;
    struct iovec vector = {&byte, 1};
    struct msghdr message;
    CHECK(write(pair[0], "ab", 2) == 2);
    for (unsigned i = 0; i < 2; ++i) {
        message = (struct msghdr){.msg_iov = &vector, .msg_iovlen = 1,
            .msg_control = control.bytes, .msg_controllen = sizeof(control)};
        CHECK(recvmsg(pair[1], &message, 0) == 1 && byte == "ab"[i]);
        struct cmsghdr *header = CMSG_FIRSTHDR(&message);
        CHECK(header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_CREDENTIALS);
        struct ucred credentials;
        memcpy(&credentials, CMSG_DATA(header), sizeof(credentials));
        CHECK(credentials.pid == getpid() && credentials.uid == getuid() && credentials.gid == getgid());
    }
    pid_t child = fork();
    CHECK(child >= 0);
    uid_t child_uid = geteuid() == 0 ? 1234 : getuid();
    gid_t child_gid = geteuid() == 0 ? 1234 : getgid();
    if (!child) {
        close(pair[1]);
        if (geteuid() == 0 && (setgid(child_gid) || setuid(child_uid))) _exit(20);
        struct ucred credentials = {getpid() + 100000, getuid(), getgid()};
        message = (struct msghdr){.msg_iov = &vector, .msg_iovlen = 1,
            .msg_control = control.bytes, .msg_controllen = CMSG_SPACE(sizeof(credentials))};
        struct cmsghdr *header = CMSG_FIRSTHDR(&message);
        *header = (struct cmsghdr){.cmsg_len = CMSG_LEN(sizeof(credentials)),
            .cmsg_level = SOL_SOCKET, .cmsg_type = SCM_CREDENTIALS};
        memcpy(CMSG_DATA(header), &credentials, sizeof(credentials));
        if (sendmsg(pair[0], &message, MSG_NOSIGNAL) != -1 || errno != EPERM) _exit(21);
        credentials.pid = getpid();
        memcpy(CMSG_DATA(header), &credentials, sizeof(credentials));
        if (sendmsg(pair[0], &message, MSG_NOSIGNAL) != 1) _exit(22);
        _exit(0);
    }
    message = (struct msghdr){.msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control.bytes, .msg_controllen = sizeof(control)};
    CHECK(recvmsg(pair[1], &message, 0) == 1);
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    CHECK(header && header->cmsg_type == SCM_CREDENTIALS);
    struct ucred credentials;
    memcpy(&credentials, CMSG_DATA(header), sizeof(credentials));
    CHECK(credentials.pid == child && credentials.uid == child_uid && credentials.gid == child_gid);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    int source = open("/dev/null", O_RDWR);
    CHECK(source >= 0);
    message = (struct msghdr){.msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control.bytes, .msg_controllen = CMSG_SPACE(sizeof(source))};
    header = CMSG_FIRSTHDR(&message);
    *header = (struct cmsghdr){.cmsg_len = CMSG_LEN(sizeof(source)), .cmsg_level = SOL_SOCKET, .cmsg_type = SCM_RIGHTS};
    memcpy(CMSG_DATA(header), &source, sizeof(source));
    CHECK(sendmsg(pair[0], &message, MSG_NOSIGNAL) == 1);
    message.msg_controllen = sizeof(control);
    CHECK(recvmsg(pair[1], &message, MSG_CMSG_CLOEXEC) == 1 && !(message.msg_flags & MSG_CTRUNC));
    header = CMSG_FIRSTHDR(&message);
    CHECK(header && header->cmsg_type == SCM_CREDENTIALS);
    header = CMSG_NXTHDR(&message, header);
    CHECK(header && header->cmsg_type == SCM_RIGHTS);
    int received;
    memcpy(&received, CMSG_DATA(header), sizeof(received));
    CHECK((fcntl(received, F_GETFD) & FD_CLOEXEC) != 0);
    CHECK(close(received) == 0 && close(source) == 0);
    CHECK(close(pair[1]) == 0);
    struct sigaction action = {.sa_handler = socket_sigpipe}, old;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGPIPE, &action, &old) == 0);
    CHECK(write(pair[0], "x", 1) == -1 && errno == EPIPE && socket_sigpipe_count == 1);
    CHECK(send(pair[0], "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE && socket_sigpipe_count == 1);
    CHECK(sigaction(SIGPIPE, &old, NULL) == 0 && close(pair[0]) == 0);
    CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0);
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    CHECK(send(pair[0], NULL, 0, 0) == 0);
    message = (struct msghdr){.msg_control = control.bytes, .msg_controllen = sizeof(control)};
    CHECK(recvmsg(pair[1], &message, 0) == 0);
    header = CMSG_FIRSTHDR(&message);
    CHECK(header && header->cmsg_type == SCM_CREDENTIALS);
    memcpy(&credentials, CMSG_DATA(header), sizeof(credentials));
    CHECK(credentials.pid == getpid() && credentials.uid == getuid());
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    return 0;
}

static void *timed_signal_sender(void *arg)
{
    struct timespec delay = {0, 30000000};
    if (nanosleep(&delay, NULL) || pthread_kill(*(pthread_t *)arg, SIGUSR1)) return (void *)1;
    return NULL;
}

static int poll_interrupts(void)
{
    struct sigaction action = {.sa_handler = socket_sigpipe, .sa_flags = SA_RESTART}, old;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, &old) == 0);
    pthread_t self = pthread_self(), sender;
    const unsigned long timeouts[] = {-1UL, -7UL, 0xffffffffUL, 0x1ffffffffUL, 500};
    for (unsigned i = 0; i < sizeof(timeouts) / sizeof(timeouts[0]); ++i) {
        CHECK(pthread_create(&sender, NULL, timed_signal_sender, &self) == 0);
        int result = syscall(SYS_poll, NULL, 0, timeouts[i]);
        int error = errno;
        void *joined;
        CHECK(pthread_join(sender, &joined) == 0 && !joined);
        CHECK(result == -1 && error == EINTR);
    }
    CHECK(pthread_create(&sender, NULL, timed_signal_sender, &self) == 0);
    int immediate = syscall(SYS_poll, NULL, 0, 1ULL << 32);
    void *joined;
    CHECK(pthread_join(sender, &joined) == 0 && !joined);
    CHECK(immediate == 0);
    CHECK(sigaction(SIGUSR1, &old, NULL) == 0);
    CHECK(syscall(SYS_poll, NULL, 1ULL << 32, 0) == 0);
    return 0;
}

static int poll_files(void)
{
    char path[96], sendfile_path[112], copy_range_path[112];
    snprintf(path, sizeof(path), "/tmp/poll-%s-%ld", PROBE_KIND, (long)getpid());
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    CHECK(fd >= 0);
    const short all = POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM | POLLRDHUP | POLLPRI;
    struct pollfd p = {fd, all, -1};
    CHECK(poll(&p, 1, 0) == 1 && p.revents == (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
    CHECK(close(fd) == 0);
    fd = open(path, O_RDONLY);
    CHECK(fd >= 0);
    p.fd = fd;
    CHECK(poll(&p, 1, 0) == 1 && p.revents == (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
    struct timespec ppoll_zero = {0, 0};
    p.revents = 0;
    CHECK(syscall(SYS_ppoll, &p, 1, &ppoll_zero, NULL, 0) == 1 &&
          p.revents == (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
    ppoll_zero.tv_nsec = 1000000000;
    CHECK(syscall(SYS_ppoll, &p, 1, &ppoll_zero, NULL, 0) == -1 && errno == EINVAL);
    ppoll_zero.tv_nsec = 0;
    uint64_t empty_mask = 0;
    CHECK(syscall(SYS_ppoll, &p, 1, &ppoll_zero, &empty_mask, sizeof(empty_mask)) == -1 &&
          errno == EOPNOTSUPP);
    fd_set readfds, writefds, exceptfds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    FD_SET(fd, &readfds);
    FD_SET(fd, &writefds);
    struct timeval select_zero = {0, 0};
    CHECK(syscall(SYS_select, fd + 1, &readfds, &writefds, &exceptfds, &select_zero) == 1 &&
          FD_ISSET(fd, &readfds) && FD_ISSET(fd, &writefds) && !FD_ISSET(fd, &exceptfds) &&
          select_zero.tv_sec == 0 && select_zero.tv_usec == 0);
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    struct timespec pselect_zero = {0, 0};
    struct { const sigset_t *ss; size_t ss_len; } pselect_arg = {NULL, sizeof(uint64_t)};
    CHECK(syscall(SYS_pselect6, fd + 1, &readfds, NULL, NULL, &pselect_zero, &pselect_arg) == 1 &&
          FD_ISSET(fd, &readfds));
    struct pollfd *readonly = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(readonly != MAP_FAILED);
    *readonly = p;
    CHECK(mprotect(readonly, 4096, PROT_READ) == 0);
    CHECK(syscall(SYS_poll, readonly, 1, 0) == -1 && errno == EFAULT);
    CHECK(munmap(readonly, 4096) == 0 && close(fd) == 0 && unlink(path) == 0);
    struct rlimit limit;
    CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0);
    CHECK(syscall(SYS_poll, NULL, limit.rlim_cur + 1, 0) == -1 && errno == EINVAL);
    p = (struct pollfd){-1, all, -1};
    CHECK(poll(&p, 1, 20) == 0 && p.revents == 0);
    p = (struct pollfd){1234567, 0, -1};
    CHECK(poll(&p, 1, 0) == 1 && p.revents == POLLNVAL);
    struct rusage usage;
    CHECK(syscall(SYS_getrusage, RUSAGE_SELF, &usage) == 0 && usage.ru_utime.tv_sec >= 0 &&
          usage.ru_maxrss >= 0);
    struct sysinfo info;
    CHECK(syscall(SYS_sysinfo, &info) == 0 && info.mem_unit == 1024 && info.totalram >= info.freeram &&
          info.procs > 0);
    struct tms process_times;
    long ticks = syscall(SYS_times, &process_times);
    CHECK(ticks >= 0 && process_times.tms_utime >= 0 && process_times.tms_stime >= 0 &&
          process_times.tms_utime <= ticks);
    fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    CHECK(fd >= 0);
    CHECK(syscall(SYS_fallocate, fd, 0, 0, 4096) == 0);
    struct stat allocated;
    CHECK(fstat(fd, &allocated) == 0 && allocated.st_size >= 4096);
    CHECK(syscall(SYS_fallocate, fd, 1, 0, 4096) == -1 && errno == EOPNOTSUPP);
    CHECK(syscall(SYS_fallocate, fd, 0, -1LL, 1) == -1 && errno == EINVAL);
    CHECK(close(fd) == 0);
    fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    CHECK(fd >= 0 && write(fd, "abcdef", 6) == 6);
    snprintf(sendfile_path, sizeof(sendfile_path), "%s.sendfile", path);
    int send_source = open(path, O_RDONLY);
    int send_target = open(sendfile_path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    CHECK(send_source >= 0 && send_target >= 0);
    off_t send_position = 1;
    CHECK(syscall(SYS_sendfile, send_target, send_source, &send_position, 3) == 3 &&
          send_position == 4 && lseek(send_source, 0, SEEK_CUR) == 0);
    CHECK(lseek(send_target, 0, SEEK_SET) == 0);
    char send_data[4] = {0};
    CHECK(read(send_target, send_data, 3) == 3 && !memcmp(send_data, "bcd", 3));
    CHECK(close(send_source) == 0 && close(send_target) == 0 && unlink(sendfile_path) == 0);
    snprintf(copy_range_path, sizeof(copy_range_path), "%s.copy", path);
    int copy_source = open(path, O_RDONLY);
    int copy_target = open(copy_range_path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    CHECK(copy_source >= 0 && copy_target >= 0);
    off_t copy_in = 2, copy_out = 0;
    CHECK(syscall(SYS_copy_file_range, copy_source, &copy_in, copy_target, &copy_out, 2, 0) == 2 &&
          copy_in == 4 && copy_out == 2 && lseek(copy_source, 0, SEEK_CUR) == 0 &&
          lseek(copy_target, 0, SEEK_CUR) == 0);
    CHECK(lseek(copy_target, 0, SEEK_SET) == 0);
    char copy_data[3] = {0};
    CHECK(read(copy_target, copy_data, 2) == 2 && !memcmp(copy_data, "cd", 2));
    CHECK(close(copy_source) == 0 && close(copy_target) == 0 && unlink(copy_range_path) == 0);
    int event_fd = eventfd(0, EFD_NONBLOCK);
    CHECK(event_fd >= 0);
    uint64_t event_value = 3;
    CHECK(write(event_fd, &event_value, sizeof(event_value)) == (ssize_t)sizeof(event_value));
    event_value = 0;
    CHECK(read(event_fd, &event_value, sizeof(event_value)) == (ssize_t)sizeof(event_value) && event_value == 3);
    CHECK(read(event_fd, &event_value, sizeof(event_value)) == -1 && errno == EAGAIN);
    struct pollfd event_poll = {event_fd, POLLIN | POLLOUT, 0};
    CHECK(poll(&event_poll, 1, 0) == 1 && !(event_poll.revents & POLLIN) && (event_poll.revents & POLLOUT));
    CHECK(syscall(SYS_eventfd2, 0, 0x40000000u) == -1 && errno == EINVAL);
    CHECK(close(event_fd) == 0);
    struct iovec vector = {(void *)"XYZ", 3};
    char positioned[4] = {0};
    struct iovec read_vector = {positioned, 3};
    CHECK(syscall(SYS_preadv, fd, &read_vector, 1, 1, 0) == 3 &&
          !memcmp(positioned, "bcd", 3));
    CHECK(lseek(fd, 0, SEEK_CUR) == 6);
    CHECK(syscall(SYS_pwritev, fd, &vector, 1, 2, 0) == 3);
    CHECK(lseek(fd, 0, SEEK_CUR) == 6);
    memset(positioned, 0, sizeof(positioned));
    CHECK(syscall(SYS_preadv2, fd, &read_vector, 1, 2, 0, 0) == 3 &&
          !memcmp(positioned, "XYZ", 3));
    CHECK(syscall(SYS_pwritev2, fd, &vector, 1, 0, 0, 1) == -1 && errno == EOPNOTSUPP);
    CHECK(syscall(SYS_sync_file_range, fd, 0, 0, 0) == 0);
    CHECK(syscall(SYS_sync_file_range, fd, 0, 0, 8) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_sync_file_range, fd, -1LL, 0, 0) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_syncfs, fd) == 0);
    int vector_pipe[2];
    CHECK(pipe(vector_pipe) == 0);
    CHECK(syscall(SYS_preadv, vector_pipe[0], &read_vector, 1, 0, 0) == -1 && errno == ESPIPE);
    CHECK(close(vector_pipe[0]) == 0 && close(vector_pipe[1]) == 0);
    CHECK(close(fd) == 0);
    int64_t now = 0;
    long time_result = syscall(SYS_time, &now);
    CHECK(time_result >= 0 && now == time_result);
    CHECK(syscall(SYS_time, (void *)1) == -1 && errno == EFAULT);
    unsigned cpu = 0, node = 0;
    CHECK(syscall(SYS_getcpu, &cpu, &node, NULL) == 0);
    CHECK(syscall(SYS_getcpu, (void *)1, NULL, NULL) == -1 && errno == EFAULT);
    fd = open("/dev/null", O_RDONLY);
    CHECK(fd >= 0 && syscall(SYS_close_range, fd, fd, 0) == 0 &&
          fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    fd = open("/dev/null", O_RDONLY);
    CHECK(fd >= 0 && syscall(SYS_close_range, fd, UINT_MAX, 0) == 0 &&
          fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    fd = open("/dev/null", O_RDONLY);
    CHECK(fd >= 0 && syscall(SYS_close_range, fd, fd, CLOSE_RANGE_CLOEXEC) == 0 &&
          (fcntl(fd, F_GETFD) & FD_CLOEXEC) && close(fd) == 0);
    CHECK(syscall(SYS_close_range, 4, 3, 0) == -1 && errno == EINVAL);
    return 0;
}

static int thread_timed_interrupts(void)
{
    struct timespec request = {0, 200000000}, remaining = {77, 88};
    CHECK(syscall(SYS_nanosleep, NULL, NULL) == -1 && errno == EFAULT);
    struct timespec invalid = {-1, 0};
    CHECK(syscall(SYS_nanosleep, &invalid, NULL) == -1 && errno == EINVAL);
    invalid = (struct timespec){0, 1000000000};
    CHECK(syscall(SYS_nanosleep, &invalid, NULL) == -1 && errno == EINVAL);
    struct timespec zero = {0, 0};
    CHECK(nanosleep(&zero, &remaining) == 0 && remaining.tv_sec == 77 && remaining.tv_nsec == 88);
    CHECK(clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &zero, &remaining) == 0);
    struct sigaction action = {.sa_handler = socket_sigpipe, .sa_flags = SA_RESTART}, old;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, &old) == 0);
    pthread_t self = pthread_self(), sender;
    void *result;
    for (unsigned mode = 0; mode < 4; ++mode) {
        CHECK(pthread_create(&sender, NULL, timed_signal_sender, &self) == 0);
        remaining = (struct timespec){77, 88};
        if (mode == 0) {
            CHECK(nanosleep(&request, &remaining) == -1 && errno == EINTR);
        } else if (mode == 1) {
            CHECK(clock_nanosleep(CLOCK_MONOTONIC, 0, &request, &remaining) == EINTR);
        } else if (mode == 2) {
            uint32_t word = 0;
            CHECK(syscall(SYS_futex, &word, 128 /* FUTEX_WAIT_PRIVATE */, 0,
                          &request, NULL, 0) == -1 && errno == EINTR);
        } else {
            struct timespec target;
            CHECK(clock_gettime(CLOCK_REALTIME, &target) == 0);
            target.tv_nsec += request.tv_nsec;
            if (target.tv_nsec >= 1000000000) { ++target.tv_sec; target.tv_nsec -= 1000000000; }
            CHECK(clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &target, &remaining) == EINTR);
        }
        if (mode < 2) CHECK(remaining.tv_sec == 0 && remaining.tv_nsec > 0 && remaining.tv_nsec <= request.tv_nsec);
        else CHECK(remaining.tv_sec == 77 && remaining.tv_nsec == 88);
        CHECK(pthread_join(sender, &result) == 0 && !result);
    }
    CHECK(sigaction(SIGUSR1, &old, NULL) == 0);
    return 0;
}

struct closing_reader {
    int fd, operation;
    atomic_int ready;
    ssize_t count;
    char byte;
};

static void *read_while_closed(void *arg)
{
    struct closing_reader *reader = arg;
    atomic_store(&reader->ready, 1);
    if (!reader->operation) reader->count = read(reader->fd, &reader->byte, 1);
    else if (reader->operation == 1) reader->count = recv(reader->fd, &reader->byte, 1, 0);
    else {
        struct iovec vector = {&reader->byte, 1};
        reader->count = readv(reader->fd, &vector, 1);
    }
    return NULL;
}

static int unix_close_during_io(void)
{
    for (int operation = 0; operation < 3; ++operation) {
        int original[2], replacement[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, original) == 0);
        struct closing_reader reader = {.fd = original[1], .operation = operation};
        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, read_while_closed, &reader) == 0);
        while (!atomic_load(&reader.ready)) sched_yield();
        struct timespec delay = {0, 30000000};
        CHECK(nanosleep(&delay, NULL) == 0);
        CHECK(close(original[1]) == 0);
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, replacement) == 0);
        CHECK(replacement[0] == original[1]);
        CHECK(send(original[0], "O", 1, MSG_NOSIGNAL) == 1);
        CHECK(pthread_join(thread, NULL) == 0 && reader.count == 1 && reader.byte == 'O');
        char byte;
        struct iovec vector = {&byte, 1};
        CHECK(readv(replacement[0], &vector, 1) == -1 && errno == EAGAIN);
        CHECK(write(replacement[1], "N", 1) == 1);
        CHECK(read(replacement[0], &byte, 1) == 1 && byte == 'N');
        CHECK(close(original[0]) == 0 && close(replacement[0]) == 0 && close(replacement[1]) == 0);
    }
    return 0;
}

static int unix_timeouts(void)
{
    int pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    struct timeval timeout = {0, 40000}, observed;
    socklen_t size = sizeof(observed);
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    CHECK(getsockopt(pair[1], SOL_SOCKET, SO_RCVTIMEO, &observed, &size) == 0);
    CHECK(size == sizeof(observed) && observed.tv_sec == 0 && observed.tv_usec >= 40000 && observed.tv_usec <= 50000);
    struct timeval invalid = {0, 1000000};
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_RCVTIMEO, &invalid, sizeof(invalid)) == -1 && errno == EDOM);
    char buffer[8];
    struct timespec before, after;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
    CHECK(recv(pair[1], buffer, sizeof(buffer), 0) == -1 && errno == EAGAIN);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    long elapsed = (after.tv_sec - before.tv_sec) * 1000000000L + after.tv_nsec - before.tv_nsec;
    CHECK(elapsed >= 30000000L && elapsed < 1000000000L);
    CHECK(write(pair[0], "abc", 3) == 3);
    CHECK(recv(pair[1], buffer, sizeof(buffer), MSG_WAITALL) == 3 && !memcmp(buffer, "abc", 3));
    struct sigaction action = {.sa_handler = socket_sigpipe, .sa_flags = SA_RESTART}, old;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, &old) == 0);
    timeout.tv_usec = 200000;
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    pthread_t self = pthread_self(), sender;
    void *result;
    CHECK(pthread_create(&sender, NULL, timed_signal_sender, &self) == 0);
    CHECK(recv(pair[1], buffer, sizeof(buffer), 0) == -1 && errno == EINTR);
    CHECK(pthread_join(sender, &result) == 0 && !result);
    CHECK(sigaction(SIGUSR1, &old, NULL) == 0);
    timeout.tv_usec = 40000;
    CHECK(setsockopt(pair[0], SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0);
    char fill[4096] = {0};
    while (send(pair[0], fill, sizeof(fill), MSG_DONTWAIT | MSG_NOSIGNAL) > 0) {}
    CHECK(errno == EAGAIN);
    CHECK(send(pair[0], "x", 1, MSG_NOSIGNAL) == -1 && errno == EAGAIN);
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);

    CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0);
    /* Native x86-64 OLD and NEW timeout records are both two 64-bit fields. */
    CHECK(setsockopt(pair[1], SOL_SOCKET, 66 /* SO_RCVTIMEO_NEW */, &timeout, sizeof(timeout)) == 0);
    CHECK(recv(pair[1], buffer, sizeof(buffer), 0) == -1 && errno == EAGAIN);
    invalid = (struct timeval){-1, 0};
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_RCVTIMEO, &invalid, sizeof(invalid)) == 0);
    CHECK(recv(pair[1], buffer, sizeof(buffer), 0) == -1 && errno == EAGAIN);
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    int namespace_gap = socket(AF_UNIX, SOCK_STREAM, 0);
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    CHECK(listener >= 0 && bind(listener, (struct sockaddr *)&address, sizeof(sa_family_t)) == 0);
    size = sizeof(address);
    CHECK(getsockname(listener, (struct sockaddr *)&address, &size) == 0 && listen(listener, 0) == 0);
    CHECK(setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    CHECK(accept(listener, NULL, NULL) == -1 && errno == EAGAIN);
    int client = socket(AF_UNIX, SOCK_STREAM, 0), waiting = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(client >= 0 && waiting >= 0 && namespace_gap >= 0);
    /* An accepted endpoint can reuse an object slot before its listener. */
    CHECK(close(namespace_gap) == 0 && connect(client, (struct sockaddr *)&address, size) == 0);
    CHECK(setsockopt(waiting, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0);
    CHECK(connect(waiting, (struct sockaddr *)&address, size) == -1 && errno == EAGAIN);
    int enabled = 1;
    CHECK(setsockopt(listener, SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    int accepted = accept(listener, NULL, NULL);
    CHECK(accepted >= 0);
    size = sizeof(observed);
    CHECK(getsockopt(accepted, SOL_SOCKET, SO_RCVTIMEO, &observed, &size) == 0);
    CHECK(observed.tv_sec == 0 && observed.tv_usec == 0);
    size = sizeof(enabled);
    enabled = 0;
    CHECK(getsockopt(accepted, SOL_SOCKET, SO_PASSCRED, &enabled, &size) == 0 && enabled == 1);
    CHECK(close(accepted) == 0 && close(client) == 0 && close(waiting) == 0 && close(listener) == 0);
    return 0;
}

static int unix_vectors(void)
{
    int pair[2];
    char first[4] = {0}, second[4] = {0}, joined[8] = {0};
    struct iovec out[] = {{"abc", 3}, {"defg", 4}};
    struct iovec in[] = {{first, 3}, {second, 4}};
    for (int type = SOCK_STREAM; type <= SOCK_SEQPACKET; ++type) {
        if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_SEQPACKET) continue;
        CHECK(socketpair(AF_UNIX, type | SOCK_NONBLOCK, 0, pair) == 0);
        CHECK(syscall(SYS_readv, pair[1], NULL, 0) == 0);
        CHECK(syscall(SYS_writev, pair[0], NULL, 0) == 0);
        CHECK(syscall(SYS_readv, -1, NULL, 0) == -1 && errno == EBADF);
        CHECK(syscall(SYS_writev, pair[0], NULL, 1025) == -1 && errno == EINVAL);
        CHECK(syscall(SYS_readv, pair[1], NULL, -1) == -1 && errno == EINVAL);
        CHECK(writev(pair[0], out, 2) == 7);
        CHECK(recv(pair[1], joined, sizeof(joined), 0) == 7 && !memcmp(joined, "abcdefg", 7));
        CHECK(recv(pair[1], joined, 1, 0) == -1 && errno == EAGAIN);
        CHECK(send(pair[0], "ABCDEFG", 7, 0) == 7);
        CHECK(readv(pair[1], in, 2) == 7 && !memcmp(first, "ABC", 3) && !memcmp(second, "DEFG", 4));
        CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    }
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    CHECK(write(pair[0], "abc", 3) == 3);
    /* Available bytes end exactly at an iovec boundary. The read must return. */
    CHECK(readv(pair[1], in, 2) == 3 && !memcmp(first, "abc", 3));
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    return 0;
}

static int futex_park_word;

static atomic_int process_signal_stage, process_signal_count, process_signal_tid;

static int unix_nonblock_ioctl(void)
{
    const int types[] = {SOCK_STREAM, SOCK_SEQPACKET, SOCK_DGRAM};
    for (unsigned i = 0; i < 3; ++i) {
        int pair[2];
        CHECK(socketpair(AF_UNIX, types[i], 0, pair) == 0);
        int alias = dup(pair[0]);
        CHECK(alias >= 0);
        int value = 7;
        CHECK(syscall(SYS_ioctl, alias, FIONBIO, &value) == 0);
        CHECK(fcntl(pair[0], F_GETFL) & O_NONBLOCK);
        char byte;
        CHECK(recv(pair[0], &byte, 1, 0) == -1 && errno == EAGAIN);
        CHECK(syscall(SYS_ioctl, alias, FIONBIO, (void *)1) == -1 && errno == EFAULT);
        CHECK(fcntl(pair[0], F_GETFL) & O_NONBLOCK);
        value = 0;
        CHECK(syscall(SYS_ioctl, pair[0], FIONBIO, &value) == 0);
        CHECK(!(fcntl(alias, F_GETFL) & O_NONBLOCK));
        CHECK(send(pair[1], "x", 1, MSG_NOSIGNAL) == 1 && recv(alias, &byte, 1, 0) == 1 && byte == 'x');
        CHECK(close(alias) == 0 && close(pair[0]) == 0 && close(pair[1]) == 0);
    }
    return 0;
}

static int unix_shutdown_poll(void)
{
    const int types[] = {SOCK_STREAM, SOCK_SEQPACKET, SOCK_DGRAM};
    const short output = POLLOUT | POLLWRNORM | POLLWRBAND;
    const short input = POLLIN | POLLRDNORM | POLLRDHUP;
    const short events = output | input;
    for (unsigned i = 0; i < 3; ++i) {
        int fd = socket(AF_UNIX, types[i] | SOCK_NONBLOCK, 0);
        CHECK(fd >= 0);
        struct pollfd p = {fd, events, 0};
        CHECK(poll(&p, 1, 0) == 1 && p.revents == (output | (types[i] == SOCK_DGRAM ? 0 : POLLHUP)));
        CHECK(shutdown(fd, -1) == -1 && errno == EINVAL);
        CHECK(shutdown(fd, SHUT_RDWR) == 0);
        CHECK(poll(&p, 1, 0) == 1 && p.revents == (events | POLLHUP));
        CHECK(close(fd) == 0);
        int pair[2];
        CHECK(socketpair(AF_UNIX, types[i] | SOCK_NONBLOCK, 0, pair) == 0);
        CHECK(send(pair[1], "abc", 3, MSG_NOSIGNAL) == 3);
        CHECK(shutdown(pair[0], SHUT_RD) == 0);
        p = (struct pollfd){pair[0], events, 0};
        CHECK(poll(&p, 1, 0) == 1 && p.revents == events);
        char data[8] = {0};
        CHECK(recv(pair[0], data, sizeof(data), 0) == 3 && !memcmp(data, "abc", 3));
        if (types[i] != SOCK_DGRAM) {
            CHECK(recv(pair[0], data, sizeof(data), 0) == 0);
            CHECK(send(pair[1], "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE);
        }
        p = (struct pollfd){pair[1], events, 0};
        CHECK(poll(&p, 1, 0) == 1 && p.revents == output);
        CHECK(shutdown(pair[0], SHUT_RDWR) == 0);
        CHECK(poll(&p, 1, 0) == 1 && p.revents ==
              (types[i] == SOCK_DGRAM ? output : (events | POLLHUP)));
        CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    }
    return 0;
}

static int unix_inq(void)
{
    const int types[] = {SOCK_STREAM, SOCK_SEQPACKET, SOCK_DGRAM};
    for (unsigned i = 0; i < 3; ++i) {
        int pair[2], amount = -1;
        CHECK(socketpair(AF_UNIX, types[i] | SOCK_NONBLOCK, 0, pair) == 0);
        struct { int count; unsigned canary; } result = {-1, 0xaabbccdd};
        CHECK(syscall(SYS_ioctl, pair[0], FIONREAD, &result.count) == 0 && !result.count && result.canary == 0xaabbccdd);
        CHECK(syscall(SYS_ioctl, pair[0], FIONREAD, (void *)1) == -1 && errno == EFAULT);
        CHECK(send(pair[1], "abc", 3, MSG_NOSIGNAL) == 3 && send(pair[1], "defgh", 5, MSG_NOSIGNAL) == 5);
        CHECK(syscall(SYS_ioctl, pair[0], FIONREAD, &amount) == 0 && amount == (types[i] == SOCK_DGRAM ? 3 : 8));
        char data[8];
        CHECK(recv(pair[0], data, 2, MSG_PEEK) == 2);
        CHECK(syscall(SYS_ioctl, pair[0], FIONREAD, &amount) == 0 && amount == (types[i] == SOCK_DGRAM ? 3 : 8));
        CHECK(recv(pair[0], data, 2, 0) == 2);
        CHECK(syscall(SYS_ioctl, pair[0], FIONREAD, &amount) == 0 && amount == (types[i] == SOCK_STREAM ? 6 : 5));
        CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path + 1, sizeof(address.sun_path) - 1, "inq-%ld", (long)getpid());
    CHECK(bind(fd, (struct sockaddr *)&address, 2 + 1 + strlen(address.sun_path + 1)) == 0 && listen(fd, 1) == 0);
    int amount;
    CHECK(syscall(SYS_ioctl, fd, FIONREAD, &amount) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_ioctl, fd, FIONREAD, (void *)1) == -1 && errno == EINVAL);
    CHECK(close(fd) == 0);
    return 0;
}

static int unix_reset(void)
{
    const int types[] = {SOCK_STREAM, SOCK_SEQPACKET};
    for (unsigned type = 0; type < 2; ++type) {
        for (unsigned consume = 0; consume < 2; ++consume) {
            int pair[2];
            CHECK(socketpair(AF_UNIX, types[type] | SOCK_NONBLOCK, 0, pair) == 0);
            CHECK(send(pair[0], "unread", 6, MSG_NOSIGNAL) == 6);
            CHECK(close(pair[1]) == 0);
            struct pollfd p = {pair[0], POLLIN | POLLOUT | POLLRDHUP, 0};
            CHECK(poll(&p, 1, 0) == 1 && p.revents == (p.events | POLLHUP | POLLERR));
            int error = 0;
            socklen_t length = sizeof(error);
            char byte;
            if (consume) {
                CHECK(getsockopt(pair[0], SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == ECONNRESET);
            } else {
                CHECK(recv(pair[0], &byte, 1, 0) == -1 && errno == ECONNRESET);
            }
            CHECK(getsockopt(pair[0], SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0);
            CHECK(recv(pair[0], &byte, 1, 0) == 0);
            CHECK(poll(&p, 1, 0) == 1 && p.revents == (p.events | POLLHUP));
            CHECK(close(pair[0]) == 0);
        }
    }
    return 0;
}

static atomic_int alarm_delivered, alarm_tid;
static void alarm_handler(int sig)
{
    if (sig == SIGALRM) {
        atomic_store(&alarm_tid, (int)syscall(SYS_gettid));
        atomic_fetch_add(&alarm_delivered, 1);
    }
}

static void *alarm_worker(void *unused)
{
    (void)unused;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) || alarm(1)) return (void *)1;
    for (unsigned i = 0; i < 300 && !atomic_load(&alarm_delivered); ++i) usleep(10000);
    return atomic_load(&alarm_delivered) == 1 && atomic_load(&alarm_tid) == syscall(SYS_gettid)
        ? NULL : (void *)2;
}

static int thread_alarm(void)
{
    CHECK(alarm(0) == 0);
    CHECK(syscall(SYS_alarm, 2) == 0 && alarm(0) == 2);
    struct itimerval timer = {.it_value.tv_sec = 4}, observed;
    CHECK(syscall(SYS_getitimer, ITIMER_REAL, NULL) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_getitimer, 99, NULL) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_setitimer, ITIMER_REAL, &timer, (void *)1) == -1 && errno == EFAULT);
    CHECK(getitimer(ITIMER_REAL, &observed) == 0 && observed.it_value.tv_sec >= 3);
    timer.it_value.tv_usec = 1000000;
    CHECK(setitimer(ITIMER_REAL, &timer, NULL) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_setitimer, ITIMER_REAL, NULL, &observed) == 0 && observed.it_value.tv_sec >= 3);
    CHECK(alarm(4) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(alarm(0) == 0 ? 0 : 1);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    unsigned remaining = alarm(0);
    CHECK(remaining > 0 && remaining <= 4);
    struct sigaction action = {.sa_handler = alarm_handler}, old;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGALRM, &action, &old) == 0);
    sigset_t set, previous;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    CHECK(pthread_sigmask(SIG_BLOCK, &set, &previous) == 0);
    atomic_store(&alarm_delivered, 0);
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, alarm_worker, NULL) == 0);
    void *result = (void *)1;
    CHECK(pthread_join(worker, &result) == 0 && result == NULL);
    CHECK(alarm(0) == 0);
    atomic_store(&alarm_delivered, 0);
    timer = (struct itimerval){.it_interval.tv_usec = 40000, .it_value.tv_usec = 10000};
    CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0);
    usleep(90000);
    sigset_t pending;
    CHECK(sigpending(&pending) == 0 && sigismember(&pending, SIGALRM));
    CHECK(getitimer(ITIMER_REAL, &observed) == 0 && observed.it_interval.tv_usec == 40000 &&
          observed.it_value.tv_sec == 0 && observed.it_value.tv_usec == 0);
    CHECK(pthread_sigmask(SIG_UNBLOCK, &set, NULL) == 0 && atomic_load(&alarm_delivered) == 1);
    for (unsigned i = 0; i < 100 && atomic_load(&alarm_delivered) < 3; ++i) usleep(10000);
    CHECK(atomic_load(&alarm_delivered) >= 3);
    timer = (struct itimerval){0};
    CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0);
    CHECK(getitimer(ITIMER_REAL, &observed) == 0 && observed.it_interval.tv_usec == 0 &&
          observed.it_value.tv_sec == 0 && observed.it_value.tv_usec == 0);
    CHECK(pthread_sigmask(SIG_SETMASK, &previous, NULL) == 0);
    CHECK(sigaction(SIGALRM, &old, NULL) == 0);
    return 0;
}
static void process_signal_handler(int signal_number)
{
    if (signal_number == SIGUSR2) {
        atomic_store(&process_signal_tid, (int)syscall(SYS_gettid));
        atomic_fetch_add(&process_signal_count, 1);
    }
}

static void *process_signal_worker(void *argument)
{
    int directed = *(int *)argument;
    while (!atomic_load(&process_signal_stage)) usleep(1000);
    sigset_t pending, set;
    if (sigpending(&pending) || sigismember(&pending, SIGUSR2) != !directed) return (void *)1;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    if (pthread_sigmask(SIG_UNBLOCK, &set, NULL)) return (void *)2;
    if (!directed) {
        for (unsigned n = 0; n < 1000 && !atomic_load(&process_signal_count); ++n) usleep(1000);
        if (atomic_load(&process_signal_count) != 1 ||
            atomic_load(&process_signal_tid) != syscall(SYS_gettid)) return (void *)3;
    } else if (atomic_load(&process_signal_count)) return (void *)4;
    return NULL;
}

static int thread_process_signals(void)
{
    struct sigaction action = {.sa_handler = process_signal_handler}, old;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR2, &action, &old) == 0);
    sigset_t set, previous, pending;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    CHECK(pthread_sigmask(SIG_BLOCK, &set, &previous) == 0);
    for (int directed = 0; directed <= 1; ++directed) {
        atomic_store(&process_signal_stage, 0);
        atomic_store(&process_signal_count, 0);
        atomic_store(&process_signal_tid, 0);
        pthread_t worker;
        CHECK(pthread_create(&worker, NULL, process_signal_worker, &directed) == 0);
        if (directed) CHECK(pthread_kill(pthread_self(), SIGUSR2) == 0);
        else CHECK(kill(getpid(), SIGUSR2) == 0);
        CHECK(sigpending(&pending) == 0 && sigismember(&pending, SIGUSR2) == 1);
        atomic_store(&process_signal_stage, 1);
        void *result = (void *)1;
        CHECK(pthread_join(worker, &result) == 0 && result == NULL);
        CHECK(sigpending(&pending) == 0 && sigismember(&pending, SIGUSR2) == directed);
        if (directed) {
            CHECK(pthread_sigmask(SIG_UNBLOCK, &set, NULL) == 0);
            CHECK(atomic_load(&process_signal_count) == 1 && atomic_load(&process_signal_tid) == syscall(SYS_gettid));
        }
    }
    CHECK(pthread_sigmask(SIG_SETMASK, &previous, NULL) == 0);
    CHECK(sigaction(SIGUSR2, &old, NULL) == 0);
    return 0;
}

static void *unix_lowwater_sender(void *value)
{
    int fd = *(int *)value;
    if (write(fd, "abc", 3) != 3) return (void *)1;
    usleep(50000);
    return write(fd, "def", 3) == 3 ? NULL : (void *)1;
}

static int unix_lowwater(void)
{
    int pair[2], value;
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    socklen_t size = sizeof(value);
    CHECK(getsockopt(pair[1], SOL_SOCKET, SO_RCVLOWAT, &value, &size) == 0 && value == 1);
    value = -1;
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_RCVLOWAT, &value, sizeof(value)) == 0);
    CHECK(getsockopt(pair[1], SOL_SOCKET, SO_RCVLOWAT, &value, &size) == 0 && value == INT32_MAX);
    value = 0;
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_RCVLOWAT, &value, sizeof(value)) == 0);
    CHECK(getsockopt(pair[1], SOL_SOCKET, SO_RCVLOWAT, &value, &size) == 0 && value == 1);
    CHECK(getsockopt(pair[1], SOL_SOCKET, SO_SNDLOWAT, &value, &size) == 0 && value == 1);
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_SNDLOWAT, &value, sizeof(value)) == -1 && errno == ENOPROTOOPT);
    value = 6;
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_RCVLOWAT, &value, sizeof(value)) == 0);
    char data[8];
    for (int mode = 0; mode < 4; ++mode) {
        pthread_t sender;
        CHECK(pthread_create(&sender, NULL, unix_lowwater_sender, &pair[0]) == 0);
        memset(data, 0, sizeof(data));
        struct iovec vectors[] = {{data, 3}, {data + 3, 5}};
        struct msghdr message = {.msg_iov = vectors, .msg_iovlen = 2};
        ssize_t result;
        if (mode == 0) result = read(pair[1], data, sizeof(data));
        else if (mode == 1) result = readv(pair[1], vectors, 2);
        else if (mode == 2) result = recvmsg(pair[1], &message, 0);
        else result = recv(pair[1], data, sizeof(data), 0);
        CHECK(result == 6 && !memcmp(data, "abcdef", 6));
        void *sent = (void *)1;
        CHECK(pthread_join(sender, &sent) == 0 && sent == NULL);
    }
    CHECK(write(pair[0], "abc", 3) == 3);
    CHECK(recv(pair[1], data, sizeof(data), MSG_PEEK) == 3 && !memcmp(data, "abc", 3));
    CHECK(recv(pair[1], data, sizeof(data), MSG_PEEK | MSG_WAITALL) == 3);
    struct pollfd readable = {.fd = pair[1], .events = POLLIN};
    /* AF_UNIX poll reports queued data even below SO_RCVLOWAT in Linux v6.12. */
    CHECK(poll(&readable, 1, 0) == 1 && (readable.revents & POLLIN));
    CHECK(recv(pair[1], data, sizeof(data), MSG_DONTWAIT) == 3);
    struct timeval timeout = {0, 40000};
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    CHECK(write(pair[0], "abc", 3) == 3);
    struct timespec before, after;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
    CHECK(read(pair[1], data, sizeof(data)) == 3 && !memcmp(data, "abc", 3));
    CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    CHECK((after.tv_sec - before.tv_sec) * 1000000000LL + after.tv_nsec - before.tv_nsec >= 20000000);
    timeout = (struct timeval){0};
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    struct sigaction action = {.sa_handler = socket_sigpipe, .sa_flags = SA_RESTART}, old;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, &old) == 0);
    pthread_t self = pthread_self(), sender;
    for (int mode = 0; mode < 4; ++mode) {
        CHECK(write(pair[0], "abc", 3) == 3);
        CHECK(pthread_create(&sender, NULL, timed_signal_sender, &self) == 0);
        struct iovec vectors[] = {{data, 2}, {data + 2, 6}};
        struct msghdr message = {.msg_iov = vectors, .msg_iovlen = 2};
        ssize_t result;
        if (mode == 0) result = read(pair[1], data, sizeof(data));
        else if (mode == 1) result = readv(pair[1], vectors, 2);
        else if (mode == 2) result = recvmsg(pair[1], &message, 0);
        else result = recv(pair[1], data, sizeof(data), 0);
        CHECK(result == 3 && !memcmp(data, "abc", 3));
        void *sent = (void *)1;
        CHECK(pthread_join(sender, &sent) == 0 && sent == NULL);
    }
    CHECK(sigaction(SIGUSR1, &old, NULL) == 0);
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    return 0;
}

static void *futex_park(void *unused)
{
    (void)unused;
    struct timespec limit = {2, 0};
    return (void *)(intptr_t)syscall(SYS_futex, &futex_park_word,
                                    FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0, &limit, 0, 0);
}

static int thread_futex_ops(void)
{
    int word = 0, other = 0;
    const struct { unsigned op; int arg, before, after; } updates[] = {
        {FUTEX_OP_SET, -1, 7, -1}, {FUTEX_OP_ADD, -3, 7, 4},
        {FUTEX_OP_OR, 3, 8, 11}, {FUTEX_OP_ANDN, 3, 11, 8},
        {FUTEX_OP_XOR, 3, 11, 8},
        {FUTEX_OP_SET | FUTEX_OP_OPARG_SHIFT, 31, 0, INT32_MIN},
        {FUTEX_OP_SET | FUTEX_OP_OPARG_SHIFT, 33, 0, 2},
        {FUTEX_OP_SET | FUTEX_OP_OPARG_SHIFT, -1, 0, INT32_MIN},
    };
    for (unsigned i = 0; i < sizeof(updates) / sizeof(updates[0]); ++i) {
        other = updates[i].before;
        CHECK(syscall(SYS_futex, &word, FUTEX_WAKE_OP | FUTEX_PRIVATE_FLAG, 0, 0,
                      &other, FUTEX_OP(updates[i].op, updates[i].arg, FUTEX_OP_CMP_EQ, 0)) == 0);
        CHECK(other == updates[i].after);
    }
    other = 7;
    CHECK(syscall(SYS_futex, &word, FUTEX_WAKE_OP, 1, 1, &other,
                  FUTEX_OP(7, 9, 0, 0)) == -1 && errno == ENOSYS && other == 7);
    CHECK(syscall(SYS_futex, &word, FUTEX_WAKE_OP, 1, 1, &other,
                  FUTEX_OP(FUTEX_OP_SET, 9, 7, 0)) == -1 && errno == ENOSYS && other == 9);
    int *read_only = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(read_only != MAP_FAILED);
    CHECK(syscall(SYS_futex, &word, FUTEX_WAKE_OP, 1, 1, read_only,
                  FUTEX_OP(FUTEX_OP_SET, 0, 0, 0)) == -1 && errno == EFAULT);
    CHECK(munmap(read_only, 4096) == 0);

    for (int cmp = FUTEX_OP_CMP_EQ; cmp <= FUTEX_OP_CMP_GE; ++cmp) {
        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, futex_park, NULL) == 0);
        /* Requeue proves the worker has entered the kernel wait before testing wake-op. */
        long parked = 0;
        for (int attempt = 0; attempt < 1000 && !parked; ++attempt) {
            parked = syscall(SYS_futex, &futex_park_word,
                             FUTEX_CMP_REQUEUE | FUTEX_PRIVATE_FLAG, 0, 1, &other, 0);
            CHECK(parked >= 0);
            if (!parked) usleep(1000);
        }
        CHECK(parked == 1);
        const int old[] = {-1, -2, -2, -1, 0, -1};
        other = old[cmp];
        CHECK(syscall(SYS_futex, &word, FUTEX_WAKE_OP | FUTEX_PRIVATE_FLAG, 0, 0, &other,
                      FUTEX_OP(FUTEX_OP_SET, 8, cmp, -1)) == 1);
        void *result = (void *)1;
        CHECK(pthread_join(thread, &result) == 0 && result == NULL && other == 8);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--membarrier-exec")) return membarrier_exec_check();
    if (argc == 2 && !strcmp(argv[1], "--rlimit-child")) {
        struct rlimit value;
        return getrlimit(RLIMIT_NOFILE, &value) || value.rlim_cur != 60 || value.rlim_max != 128;
    }
    probe_executable = (const char *)getauxval(AT_EXECFN);
    if (argc == 4 && !strcmp(argv[1], "--thread-exec")) {
        if (getpid() != atol(argv[2]) || syscall(SYS_gettid) != getpid()) return 90;
        if (fcntl(atoi(argv[3]), F_GETFD) != -1 || errno != EBADF) return 91;
        sigset_t pending;
        if (sigpending(&pending) || !sigismember(&pending, SIGUSR1) || !sigismember(&pending, SIGUSR2)) return 97;
        unsigned remaining = alarm(0);
        if (!remaining || remaining > 4) return 98;
        return 0;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[musl-abi:" PROBE_KIND "] BEGIN\n");
    int status = startup(argc, argv);
    failed += status;
    printf("[musl-abi:" PROBE_KIND "] startup %s\n", status ? "FAIL" : "PASS");
    const struct { const char *name; int (*run)(void); } tests[] = {
        {"heap", heap}, {"memory", memory}, {"nonblocking", nonblocking},
        {"pty", pty}, {"pty_fork", pty_fork}, {"process_sessions", process_sessions},
        {"stdio_reopen", stdio_reopen},
        {"descriptor_allocation", descriptor_allocation},
        {"descriptor_limits", descriptor_limits},
        {"descriptor_boundaries", descriptor_boundaries},
        {"thread_affinity", thread_affinity},
        {"resource_limits", resource_limits},
        {"resource_address_space", resource_address_space},
        {"thread_tls_control", thread_tls_control},
        {"thread_tid_registration", thread_tid_registration},
        {"thread_tid_exit", thread_tid_exit},
        {"permissions", permissions}, {"threads", threads},
        {"proc_directories", proc_directories},
        {"proc_status", proc_status},
        {"rename_replacement", rename_replacement},
        {"unix_streams", unix_streams}, {"thread_contention", thread_contention},
        {"unix_rights", unix_rights}, {"thread_exit_lifecycle", thread_exit_lifecycle},
        {"unix_rights_boundaries", unix_rights_boundaries},
        {"thread_cancellation", thread_cancellation},
        {"native_signals", native_signals}, {"thread_signal_restart", thread_signal_restart},
        {"thread_exec", thread_exec}, {"unix_rights_cycles", unix_rights_cycles},
        {"unix_packets", unix_packets},
        {"unix_waitall", unix_waitall},
        {"unix_credentials", unix_credentials},
        {"thread_timed_interrupts", thread_timed_interrupts},
        {"unix_close_during_io", unix_close_during_io},
        {"unix_vectors", unix_vectors},
        {"unix_timeouts", unix_timeouts},
        {"thread_futex_ops", thread_futex_ops},
        {"thread_futex2", thread_futex2},
        {"thread_clone3", thread_clone3},
        {"process_prctl", process_prctl},
        {"unix_lowwater", unix_lowwater},
        {"thread_process_signals", thread_process_signals},
        {"thread_alarm", thread_alarm},
        {"unix_shutdown_poll", unix_shutdown_poll},
        {"unix_inq", unix_inq},
        {"unix_reset", unix_reset},
        {"poll_interrupts", poll_interrupts},
        {"poll_files", poll_files},
        {"unix_nonblock_ioctl", unix_nonblock_ioctl},
    };
    unsigned selected = 0;
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (argc == 3 && !strcmp(argv[1], "--case") && strcmp(argv[2], tests[i].name)) continue;
        ++selected;
        status = tests[i].run();
        failed += status;
        printf("[musl-abi:" PROBE_KIND "] %s %s\n", tests[i].name, status ? "FAIL" : "PASS");
    }
    if (!selected) { printf("[musl-abi:" PROBE_KIND "] unknown test case\n"); return 2; }
    printf("[musl-abi:" PROBE_KIND "] END failed=%u\n", failed);
    return failed != 0;
}
