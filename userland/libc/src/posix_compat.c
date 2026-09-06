/* Compatibility entry points needed by the Picolibc shared runtime.
 * LeonOS provides fork/exec, process groups and default signal actions;
 * application-defined signal handlers and TLS remain unsupported. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <fcntl.h>
#include <leonos/gui.h>
#include <leonos/pty.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include "../include/arpa/inet.h"
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* picolibc may expose htons/htonl as macros when its arpa header is
 * included indirectly.  The LeonOS libc exports real functions so callers
 * can take their address and link them normally. */
#ifdef htons
#undef htons
#endif
#ifdef ntohs
#undef ntohs
#endif
#ifdef htonl
#undef htonl
#endif
#ifdef ntohl
#undef ntohl
#endif

static int unsupported(void)
{
    errno = ENOSYS;
    return -1;
}

int mount(const char *source, const char *target, const char *filesystemtype,
          unsigned long mountflags, const void *data)
{
    long result = syscall6(SYS_mount, (long)source, (long)target,
                           (long)filesystemtype, (long)mountflags,
                           (long)data, 0);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

int umount2(const char *target, int flags)
{
    long result = syscall2(SYS_umount2, (long)target, flags);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

int umount(const char *target)
{
    return umount2(target, 0);
}

int getentropy(void *buffer, size_t length)
{
    (void)buffer;
    (void)length;
    return unsupported();
}

int nanosleep(const struct timespec *request, struct timespec *remaining)
{
    if (!request || request->tv_sec < 0 || request->tv_nsec < 0 ||
        request->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    if (remaining) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    return (int)syscall2(SYS_nanosleep, (long)request, 0);
}

int gettimeofday(struct timeval *value, void *timezone) __attribute__((weak))
{
    long result = syscall2(SYS_gettimeofday, (long)value, (long)timezone);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

int settimeofday(const struct timeval *value, const void *timezone) __attribute__((weak))
{
    long result = syscall2(SYS_settimeofday, (long)value, (long)timezone);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

clock_t times(struct tms *buffer)
{
    clock_t ticks = (clock_t)leonos_uptime_ms();
    if (buffer) {
        buffer->tms_utime = ticks;
        buffer->tms_stime = 0;
        buffer->tms_cutime = 0;
        buffer->tms_cstime = 0;
    }
    return ticks;
}

#define LEONOS_CLOCK_REALTIME 1
#define LEONOS_CLOCK_MONOTONIC 4

int clock_gettime(clockid_t clock_id, struct timespec *value)
{
    struct leonos_time_info info;
    uint64_t milliseconds;
    if (!value || (clock_id != LEONOS_CLOCK_REALTIME &&
                   clock_id != LEONOS_CLOCK_MONOTONIC)) {
        errno = EINVAL;
        return -1;
    }
    if (clock_id == LEONOS_CLOCK_REALTIME && leonos_time_info(&info) == 0 && info.valid) {
        value->tv_sec = (time_t)info.unix_seconds;
        value->tv_nsec = (long)((info.uptime_ms % 1000ULL) * 1000000ULL);
        return 0;
    }
    milliseconds = leonos_uptime_ms();
    value->tv_sec = (time_t)(milliseconds / 1000ULL);
    value->tv_nsec = (long)((milliseconds % 1000ULL) * 1000000ULL);
    return 0;
}

int clock_getres(clockid_t clock_id, struct timespec *value)
{
    if (clock_id != LEONOS_CLOCK_REALTIME && clock_id != LEONOS_CLOCK_MONOTONIC) {
        errno = EINVAL;
        return -1;
    }
    if (value) {
        value->tv_sec = 0;
        value->tv_nsec = 1000000L;
    }
    return 0;
}

int usleep(useconds_t microseconds)
{
    unsigned long milliseconds =
        ((unsigned long)microseconds + 999UL) / 1000UL;
    int result = sleep_ms(milliseconds);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return 0;
}

int poll(struct pollfd *fds, nfds_t count, int timeout_ms)
{
    unsigned long started = leonos_uptime_ms();
    if (!fds && count != 0) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        long result = syscall3(SYS_poll, (long)fds, (long)count, timeout_ms);
        if (result < 0 && result != -LEONOS_EAGAIN) {
            errno = (int)-result;
            return -1;
        }
        if (result > 0 || timeout_ms == 0) return (int)result;
        if (timeout_ms > 0 && leonos_uptime_ms() - started >= (unsigned long)timeout_ms) {
            return 0;
        }
        (void)sleep_ms(4);
    }
}

static int socket_result(long result)
{
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

int socket(int domain, int type, int protocol)
{
    return socket_result(syscall3(SYS_socket, domain, type, protocol));
}

int connect(int fd, const struct sockaddr *address, socklen_t length)
{
    return socket_result(syscall3(SYS_connect, fd, (long)address, length));
}

int bind(int fd, const struct sockaddr *address, socklen_t length)
{
    return socket_result(syscall3(SYS_bind, fd, (long)address, length));
}

int listen(int fd, int backlog)
{
    return socket_result(syscall2(SYS_listen, fd, backlog));
}

int accept(int fd, struct sockaddr *address, socklen_t *length)
{
    return socket_result(syscall3(SYS_accept, fd, (long)address, (long)length));
}

int socketpair(int domain, int type, int protocol, int socket_vector[2])
{
    if (!socket_vector) {
        errno = EFAULT;
        return -1;
    }
    return socket_result(syscall6(SYS_socketpair, domain, type, protocol,
                                  (long)socket_vector, 0, 0));
}

int accept4(int fd, struct sockaddr *address, socklen_t *length, int flags)
{
    return socket_result(syscall6(SYS_accept4, fd, (long)address, (long)length,
                                  flags, 0, 0));
}

ssize_t sendmsg(int fd, const struct msghdr *message, int flags)
{
    long result = syscall3(SYS_sendmsg, fd, (long)message, flags);
    if (result < 0) { errno = (int)-result; return -1; }
    return (ssize_t)result;
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags)
{
    long result = syscall3(SYS_recvmsg, fd, (long)message, flags);
    if (result < 0) { errno = (int)-result; return -1; }
    return (ssize_t)result;
}

int getsockname(int fd, struct sockaddr *address, socklen_t *length)
{
    return socket_result(syscall3(SYS_getsockname, fd, (long)address, (long)length));
}

int getsockopt(int fd, int level, int option, void *value, socklen_t *length)
{
    return socket_result(syscall6(SYS_getsockopt, fd, level, option,
                                  (long)value, (long)length, 0));
}

int setsockopt(int fd, int level, int option, const void *value, socklen_t length)
{
    return socket_result(syscall6(SYS_setsockopt, fd, level, option,
                                  (long)value, length, 0));
}

int shutdown(int fd, int how)
{
    return socket_result(syscall2(SYS_shutdown, fd, how));
}

ssize_t send(int fd, const void *buffer, size_t length, int flags)
{
    long result = syscall6(SYS_send, fd, (long)buffer, (long)length, flags, 0, 0);
    if (result < 0) { errno = (int)-result; return -1; }
    return (ssize_t)result;
}

ssize_t recv(int fd, void *buffer, size_t length, int flags)
{
    long result = syscall6(SYS_recv, fd, (long)buffer, (long)length, flags, 0, 0);
    if (result < 0) { errno = (int)-result; return -1; }
    return (ssize_t)result;
}

ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *destination, socklen_t destination_length)
{
    long result = syscall6(SYS_sendto, fd, (long)buffer, (long)length, flags,
                           (long)destination, destination_length);
    if (result < 0) { errno = (int)-result; return -1; }
    return (ssize_t)result;
}

ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *source, socklen_t *source_length)
{
    long result = syscall6(SYS_recvfrom, fd, (long)buffer, (long)length, flags,
                           (long)source, (long)source_length);
    if (result < 0) { errno = (int)-result; return -1; }
    return (ssize_t)result;
}

int chmod(const char *path, mode_t mode) __attribute__((weak))
{
    long result = syscall2(SYS_chmod, (long)path, mode);
    if (result < 0) { errno = (int)-result; return -1; }
    return 0;
}

int fchmod(int fd, mode_t mode) __attribute__((weak))
{
    long result = syscall2(SYS_fchmod, fd, mode);
    if (result < 0) { errno = (int)-result; return -1; }
    return 0;
}

int chown(const char *path, uid_t owner, gid_t group) __attribute__((weak))
{
    long result = syscall3(SYS_chown, (long)path, owner, group);
    if (result < 0) { errno = (int)-result; return -1; }
    return 0;
}

int fchown(int fd, uid_t owner, gid_t group) __attribute__((weak))
{
    long result = syscall3(SYS_fchown, fd, owner, group);
    if (result < 0) { errno = (int)-result; return -1; }
    return 0;
}

uint16_t htons(uint16_t value) { return (uint16_t)((value << 8) | (value >> 8)); }
uint16_t ntohs(uint16_t value) { return htons(value); }
uint32_t htonl(uint32_t value)
{
    return ((value & 0x000000ffU) << 24) | ((value & 0x0000ff00U) << 8) |
           ((value & 0x00ff0000U) >> 8) | ((value & 0xff000000U) >> 24);
}
uint32_t ntohl(uint32_t value) { return htonl(value); }

static int parse_ipv4(const char *text, uint32_t *network_value)
{
    uint32_t host = 0;
    if (!text || !network_value) return 0;
    for (int octet = 0; octet < 4; ++octet) {
        uint32_t value = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10u + (uint32_t)(*text - '0');
            if (value > 255u) return 0;
            ++text;
            ++digits;
        }
        if (!digits) return 0;
        host = (host << 8) | value;
        if (octet != 3) {
            if (*text != '.') return 0;
            ++text;
        }
    }
    if (*text != 0) return 0;
    *network_value = htonl(host);
    return 1;
}

in_addr_t inet_addr(const char *text)
{
    uint32_t value;
    return parse_ipv4(text, &value) ? value : (in_addr_t)0xffffffffU;
}

int inet_pton(int family, const char *source, void *destination)
{
    uint32_t value;
    if (family != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (!destination) {
        errno = EFAULT;
        return -1;
    }
    if (!parse_ipv4(source, &value)) return 0;
    *(in_addr_t *)destination = value;
    return 1;
}
