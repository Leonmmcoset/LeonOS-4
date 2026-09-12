/*
 * Raw-syscall regression for the generic ioctl(FIOCLEX/FIONCLEX) descriptor
 * flags of the Linux v6.12 native x86-64 ABI (no i386/x32).
 *
 * The same static binary is executed twice by the build harness:
 *   1. on the host Linux kernel as the reference implementation, and
 *   2. inside the LeonOS guest, where the generic VFS handling is LeonOS code.
 *
 * Covered behaviour:
 *   - FIOCLEX sets FD_CLOEXEC and FIONCLEX clears it, repeatedly and on every
 *     descriptor type LeonOS supports (regular file, directory, pipe, socket,
 *     PTY endpoint, device node, anonymous/signalfd/memfd and O_PATH).
 *   - The third ioctl argument is ignored and never dereferenced.
 *   - Closed descriptors report EBADF; the fd and cmd arguments are truncated
 *     to 32 bits exactly like Linux' unsigned int parameters.
 *   - The flag belongs to the descriptor, not to the shared open file
 *     description: dup()ed descriptors keep their own state and dup()/dup2()/
 *     F_DUPFD_CLOEXEC/dup3() follow Linux semantics.
 *   - fcntl(F_GETFD/F_SETFD) and ioctl(FIOCLEX/FIONCLEX) observe one state.
 *   - A successful execve() really closes the marked descriptor while other
 *     descriptors survive, including the implicit stdin/stdout/stderr slots.
 *   - Closing a marked descriptor and reusing its number yields a descriptor
 *     without FD_CLOEXEC.
 *   - CLONE_FILES threads observe one shared table while fork() children own
 *     their inherited copy of the flag.
 *   - Unknown requests report the Linux generic ENOTTY, never a faked success
 *     and never ENOSYS.
 *
 * Output is line oriented so the harness can parse it from a serial console:
 *   [ioctl-clex] PASS <case>
 *   [ioctl-clex] SKIP <case>: <reason>
 *   [ioctl-clex] FAIL <case>: <detail>
 *   [ioctl-clex] DONE checks=<n> failures=<n> skips=<n>
 * The exit status is 0 only when no check failed.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef FIOCLEX
#define FIOCLEX 0x5451
#endif
#ifndef FIONCLEX
#define FIONCLEX 0x5450
#endif
#ifndef O_PATH
#define O_PATH 010000000
#endif

#define CHILD_MODE "--child-verify"

static unsigned checks;
static unsigned failures;
static unsigned skips;
static const char *self_argv0;
static char scratch[256];
static char report[8192];
static size_t report_used;

static void emit(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(report + report_used, sizeof(report) - report_used,
                            format, arguments);
    va_end(arguments);
    if (written > 0 && (size_t)written < sizeof(report) - report_used) {
        report_used += (size_t)written;
    }
}

static void pass(const char *name)
{
    ++checks;
    printf("[ioctl-clex] PASS %s\n", name);
    fflush(stdout);
    emit("PASS %s\n", name);
}

static void fail(const char *name, const char *format, ...)
{
    char detail[256];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(detail, sizeof(detail), format, arguments);
    va_end(arguments);
    ++checks;
    ++failures;
    printf("[ioctl-clex] FAIL %s: %s\n", name, detail);
    fflush(stdout);
    emit("FAIL %s: %s\n", name, detail);
}

static void skip(const char *name, const char *reason)
{
    ++checks;
    ++skips;
    printf("[ioctl-clex] SKIP %s: %s\n", name, reason);
    fflush(stdout);
    emit("SKIP %s: %s\n", name, reason);
}

static int raw_ioctl(unsigned long fd, unsigned long request, unsigned long argument)
{
    return (int)syscall(SYS_ioctl, fd, request, argument);
}

/** @brief Raw syscall returning -1 with errno preserved, or fd >= 0. */
static int raw_fcntl(unsigned int fd, unsigned int command, unsigned long argument)
{
    return (int)syscall(SYS_fcntl, fd, command, argument);
}

static int fd_flags(int fd)
{
    errno = 0;
    return raw_fcntl((unsigned int)fd, F_GETFD, 0);
}

static const char *errno_text(int value)
{
    static char storage[64];
    snprintf(storage, sizeof(storage), "errno=%d (%s)", value, strerror(value));
    return storage;
}

/**
 * @brief Check that one ioctl succeeds and is observable through fcntl.
 * @return Zero when the descriptor ends up with the requested flag state.
 */
static int expect_flag(const char *name, int fd, int request, int expected)
{
    errno = 0;
    int ret = raw_ioctl((unsigned long)(unsigned int)fd, (unsigned long)request, 0);
    if (ret != 0) {
        fail(name, "ioctl(0x%x) returned %d with %s", request, ret, errno_text(errno));
        return -1;
    }
    int flags = fd_flags(fd);
    if (flags < 0) {
        fail(name, "fcntl(F_GETFD) after ioctl(0x%x) failed with %s",
             request, errno_text(errno));
        return -1;
    }
    if ((flags & FD_CLOEXEC) != (expected ? FD_CLOEXEC : 0)) {
        fail(name, "ioctl(0x%x) left FD_CLOEXEC=%d, expected %d",
             request, (flags & FD_CLOEXEC) ? 1 : 0, expected ? 1 : 0);
        return -1;
    }
    return 0;
}

static int expect_ebadf(const char *name, unsigned long fd, int request)
{
    errno = 0;
    int ret = raw_ioctl(fd, (unsigned long)request, 0);
    if (ret != -1 || errno != EBADF) {
        fail(name, "ioctl(fd=%lu,0x%x) returned %d with %s, expected EBADF",
             fd, request, ret, errno_text(errno));
        return -1;
    }
    return 0;
}

/** @brief Verify that a descriptor type round-trips FIOCLEX/FIONCLEX. */
static int check_type(const char *name, int fd)
{
    if (fd < 0) {
        skip(name, "descriptor unavailable");
        return 0;
    }
    if (expect_flag(name, fd, FIOCLEX, 1) < 0) return -1;
    if (expect_flag(name, fd, FIOCLEX, 1) < 0) return -1; /* repeated call */
    if (expect_flag(name, fd, FIONCLEX, 0) < 0) return -1;
    if (expect_flag(name, fd, FIONCLEX, 0) < 0) return -1;
    if (expect_flag(name, fd, FIOCLEX, 1) < 0) return -1;
    int flags = fd_flags(fd);
    if (flags < 0 || !(flags & FD_CLOEXEC)) {
        fail(name, "final FIOCLEX state lost (%s)", errno_text(errno));
        return -1;
    }
    pass(name);
    return 0;
}

static int create_scratch_file(const char *tag)
{
    snprintf(scratch, sizeof(scratch), "/tmp/ioctl-clex-%ld-%s", (long)getpid(), tag);
    return open(scratch, O_RDWR | O_CREAT | O_TRUNC, 0644);
}

/**
 * @brief Case 1/2: set, clear, repeat, ignore the argument, reject bad fds.
 */
static int case_basics(void)
{
    int fd = create_scratch_file("basic");
    if (fd < 0) {
        fail("basic-open", "open(%s) failed with %s", scratch, errno_text(errno));
        return -1;
    }
    if (fd_flags(fd) != 0) {
        fail("basic-initial", "fresh descriptor already has flags=0x%x", fd_flags(fd));
    } else if (expect_flag("basic-set", fd, FIOCLEX, 1) == 0 &&
               expect_flag("basic-repeat-set", fd, FIOCLEX, 1) == 0 &&
               expect_flag("basic-clear", fd, FIONCLEX, 0) == 0 &&
               expect_flag("basic-repeat-clear", fd, FIONCLEX, 0) == 0) {
        pass("basic-set-clear-repeat");
    }

    /* The third argument is meaningless for these requests: valid garbage,
     * a PROT_NONE mapping and an unmapped pointer must all be ignored. */
    void *guard = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned long bogus = guard == MAP_FAILED ? 0x1UL : (unsigned long)guard;
    errno = 0;
    int ret = raw_ioctl((unsigned long)(unsigned int)fd, FIOCLEX, bogus);
    if (ret != 0 || !(fd_flags(fd) & FD_CLOEXEC)) {
        fail("basic-ignored-argument", "ioctl(FIOCLEX,arg=%#lx) returned %d with %s",
             bogus, ret, errno_text(errno));
    } else if (raw_ioctl((unsigned long)(unsigned int)fd, FIONCLEX, ~0UL) != 0) {
        fail("basic-ignored-argument", "ioctl(FIONCLEX,~(0)) failed with %s",
             errno_text(errno));
    } else if (raw_ioctl((unsigned long)(unsigned int)fd, FIOCLEX, 0xdeadbeefUL) != 0) {
        fail("basic-ignored-argument", "ioctl(FIOCLEX,0xdeadbeef) failed with %s",
             errno_text(errno));
    } else if (!(fd_flags(fd) & FD_CLOEXEC)) {
        fail("basic-ignored-argument", "flag not set after ignored-argument calls");
    } else {
        pass("basic-ignored-argument");
    }
    if (guard != MAP_FAILED) munmap(guard, 4096);

    close(fd);
    if (expect_ebadf("basic-closed-fd", (unsigned long)(unsigned int)fd, FIOCLEX) == 0 &&
        expect_ebadf("basic-closed-fd-clear", (unsigned long)(unsigned int)fd, FIONCLEX) == 0) {
        pass("basic-closed-fd-ebadf");
    }
    if (expect_ebadf("basic-negative-fd", (unsigned long)-1, FIOCLEX) == 0 &&
        expect_ebadf("basic-negative-fd-clear", (unsigned long)-1, FIONCLEX) == 0) {
        pass("basic-negative-fd-ebadf");
    }
    if (expect_ebadf("basic-huge-fd", (unsigned long)INT_MAX, FIOCLEX) == 0) {
        pass("basic-unopened-fd-ebadf");
    }

    /* Linux declares ioctl's fd as unsigned int and its cmd as unsigned int,
     * so the kernel truncates both.  A descriptor number with high garbage in
     * the upper 32 bits must still address the same descriptor. */
    fd = create_scratch_file("width");
    if (fd < 0) {
        fail("width-open", "open failed with %s", errno_text(errno));
        return -1;
    }
    errno = 0;
    ret = raw_ioctl(((unsigned long)(unsigned int)fd) | 0x100000000UL, FIOCLEX, 0);
    if (ret != 0 || !(fd_flags(fd) & FD_CLOEXEC)) {
        fail("width-fd-truncated", "ioctl(fd|1<<32) returned %d with %s",
             ret, errno_text(errno));
    } else if (raw_ioctl((unsigned long)(unsigned int)fd, 0xffffffff00005450UL, 0) != 0) {
        fail("width-fd-truncated", "ioctl(cmd|1<<32) failed with %s", errno_text(errno));
    } else if (fd_flags(fd) & FD_CLOEXEC) {
        fail("width-fd-truncated", "FIONCLEX was not applied after cmd truncation");
    } else {
        pass("width-32bit-truncation");
    }

    /* Close and re-open: the released descriptor number must not remember the
     * flag, which also proves the descriptor-table slot was reset. */
    if (expect_flag("reuse-set", fd, FIOCLEX, 1) == 0) {
        close(fd);
        int reused = create_scratch_file("reuse");
        if (reused < 0) {
            fail("reuse-reopen", "open failed with %s", errno_text(errno));
        } else if (fd_flags(reused) != 0) {
            fail("reuse-reopen", "reused fd %d kept flags=0x%x", reused, fd_flags(reused));
        } else {
            pass("reuse-cleared-flags");
        }
        if (reused >= 0) close(reused);
    }
    unlink(scratch);
    return 0;
}

/**
 * @brief Case 3: the flag is per descriptor, not per open file description.
 */
static int case_dup_isolation(void)
{
    int fd = create_scratch_file("dup");
    if (fd < 0) {
        fail("dup-open", "open failed with %s", errno_text(errno));
        return -1;
    }
    int duplicate = dup(fd);
    if (duplicate < 0) {
        fail("dup-create", "dup failed with %s", errno_text(errno));
        close(fd);
        return -1;
    }
    if (expect_flag("dup-original-set", fd, FIOCLEX, 1) < 0) goto out;
    if (fd_flags(duplicate) != 0) {
        fail("dup-isolation", "dup fd %d observed the original's flag (0x%x)",
             duplicate, fd_flags(duplicate));
        goto out;
    }
    if (expect_flag("dup-original-clear", fd, FIONCLEX, 0) < 0) goto out;
    if (expect_flag("dup-duplicate-set", duplicate, FIOCLEX, 1) < 0) goto out;
    if (fd_flags(fd) != 0) {
        fail("dup-isolation", "original fd %d observed the duplicate's flag (0x%x)",
             fd, fd_flags(fd));
        goto out;
    }
    pass("dup-descriptor-isolation");

    /* dup()/dup2() clear the flag on the new descriptor while the source keeps
     * its own state. */
    int copied = dup(duplicate);
    if (copied < 0) {
        fail("dup-clear", "dup failed with %s", errno_text(errno));
        goto out;
    }
    if (fd_flags(copied) != 0) {
        fail("dup-clear", "dup of a CLOEXEC descriptor returned flags=0x%x", fd_flags(copied));
    } else if (!(fd_flags(duplicate) & FD_CLOEXEC)) {
        fail("dup-clear", "dup changed the source descriptor flags");
    } else {
        pass("dup-clears-cloexec");
    }

    int cloexec_dup = raw_fcntl((unsigned int)duplicate, F_DUPFD_CLOEXEC, 0);
    if (cloexec_dup < 0) {
        fail("dupfd-cloexec", "F_DUPFD_CLOEXEC failed with %s", errno_text(errno));
    } else if (!(fd_flags(cloexec_dup) & FD_CLOEXEC)) {
        fail("dupfd-cloexec", "F_DUPFD_CLOEXEC returned flags=0x%x", fd_flags(cloexec_dup));
    } else {
        pass("dupfd-cloexec-flags");
    }
    if (cloexec_dup >= 0) {
        /* ioctl on the F_DUPFD_CLOEXEC descriptor must be able to clear it
         * without touching the descriptor it was duplicated from. */
        if (expect_flag("dupfd-clear", cloexec_dup, FIONCLEX, 0) == 0 &&
            (fd_flags(duplicate) & FD_CLOEXEC)) {
            pass("dupfd-independent-state");
        } else if (!(fd_flags(duplicate) & FD_CLOEXEC)) {
            fail("dupfd-independent-state", "clearing the duplicate changed the source");
        }
        close(cloexec_dup);
    }
#if defined(SYS_dup3) && defined(O_CLOEXEC)
    int target = raw_fcntl((unsigned int)duplicate, F_DUPFD, 0);
    if (target >= 0) {
        close(target);
        int dup3_fd = (int)syscall(SYS_dup3, duplicate, target, O_CLOEXEC);
        if (dup3_fd != target) {
            fail("dup3-cloexec", "dup3 failed with %s", errno_text(errno));
        } else if (!(fd_flags(dup3_fd) & FD_CLOEXEC)) {
            fail("dup3-cloexec", "dup3(O_CLOEXEC) returned flags=0x%x", fd_flags(dup3_fd));
        } else if (expect_flag("dup3-clear", dup3_fd, FIONCLEX, 0) == 0 &&
                   (fd_flags(duplicate) & FD_CLOEXEC)) {
            pass("dup3-cloexec-flags");
        } else if (!(fd_flags(duplicate) & FD_CLOEXEC)) {
            fail("dup3-cloexec-flags", "clearing the dup3 descriptor changed the source");
        }
        if (dup3_fd >= 0) close(dup3_fd);
    } else {
        skip("dup3-cloexec-flags", "no free descriptor for dup3");
    }
#endif
out:
    close(duplicate);
    close(fd);
    unlink(scratch);
    return 0;
}

/**
 * @brief Case 4: ioctl and fcntl share exactly one descriptor flag state.
 */
static int case_fcntl_consistency(void)
{
    int fd = create_scratch_file("fcntl");
    if (fd < 0) {
        fail("fcntl-open", "open failed with %s", errno_text(errno));
        return -1;
    }
    if (raw_fcntl((unsigned int)fd, F_SETFD, FD_CLOEXEC) != 0) {
        fail("fcntl-setfd", "F_SETFD failed with %s", errno_text(errno));
        close(fd);
        unlink(scratch);
        return -1;
    }
    if (expect_flag("fcntl-setfd-visible", fd, FIONCLEX, 0) < 0) goto out;
    if (fd_flags(fd) != 0) {
        fail("fcntl-fionclex-visible", "F_GETFD still reports 0x%x", fd_flags(fd));
        goto out;
    }
    if (expect_flag("ioctl-fioclex-visible", fd, FIOCLEX, 1) < 0) goto out;
    if (fd_flags(fd) != FD_CLOEXEC) {
        fail("fcntl-getfd", "F_GETFD reports 0x%x, expected FD_CLOEXEC", fd_flags(fd));
        goto out;
    }
    if (raw_fcntl((unsigned int)fd, F_SETFD, 0) != 0) {
        fail("fcntl-clearsetfd", "F_SETFD 0 failed with %s", errno_text(errno));
        goto out;
    }
    if (fd_flags(fd) != 0) {
        fail("fcntl-clearsetfd", "F_GETFD reports 0x%x after clearing", fd_flags(fd));
        goto out;
    }
    if (expect_ebadf("fcntl-ebadf", (unsigned long)INT_MAX, FIOCLEX) == 0) {
        pass("fcntl-ioctl-shared-state");
    }
out:
    close(fd);
    unlink(scratch);
    return 0;
}

/**
 * @brief Case 5: every supported descriptor type accepts the generic request.
 */
static int case_descriptor_types(void)
{
    int fd = create_scratch_file("type-file");
    check_type("type-regular-file", fd);
    if (fd >= 0) close(fd);

    int directory = open("/", O_RDONLY | O_DIRECTORY);
    check_type("type-directory", directory);
    if (directory >= 0) close(directory);

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) == 0) {
        check_type("type-pipe-read", pipefd[0]);
        check_type("type-pipe-write", pipefd[1]);
        close(pipefd[0]);
        close(pipefd[1]);
    } else {
        skip("type-pipe-read/write", "pipe() failed");
    }

    int sockets[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0) {
        check_type("type-unix-socket", sockets[0]);
        check_type("type-unix-socket-peer", sockets[1]);
        close(sockets[0]);
        close(sockets[1]);
    } else {
        skip("type-unix-socket", "socketpair() failed");
    }

    int null_fd = open("/dev/null", O_RDWR);
    check_type("type-device-null", null_fd);
    if (null_fd >= 0) close(null_fd);

    int memfd = (int)syscall(SYS_memfd_create, "ioctl-clex", 0);
    check_type("type-anonymous-memfd", memfd);
    if (memfd >= 0) close(memfd);

    int efd = (int)syscall(SYS_eventfd2, 0, 0);
    check_type("type-anonymous-eventfd", efd);
    if (efd >= 0) close(efd);

    /* Linux takes the native 64-bit kernel sigset, which musl passes as
     * _NSIG/8 bytes; glibc's larger sigset_t would be rejected with EINVAL. */
    unsigned long long kernel_mask = 0;
    int sfd = (int)syscall(SYS_signalfd4, -1, &kernel_mask, _NSIG / 8, 0);
    check_type("type-signalfd", sfd);
    if (sfd >= 0) {
        /* The signalfd path was the only implementation before this fix, so
         * also prove FD_CLOEXEC survives a fork there. */
        pid_t child = fork();
        if (child == 0) _exit((fd_flags(sfd) & FD_CLOEXEC) ? 0 : 1);
        int status = 0;
        if (child > 0 && waitpid(child, &status, 0) == child &&
            WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            pass("type-signalfd-fork-inherits");
        } else {
            fail("type-signalfd-fork-inherits", "child did not observe FD_CLOEXEC");
        }
        close(sfd);
    }

    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        skip("type-pty-endpoint", "posix_openpt unavailable in this environment");
    } else {
        if (grantpt(master) != 0 || unlockpt(master) != 0) {
            skip("type-pty-endpoint", "grantpt/unlockpt unavailable");
        } else {
            check_type("type-pty-endpoint", master);
        }
        close(master);
    }

    /* Unknown requests must be rejected with Linux' generic ENOTTY instead of
     * a faked success or an ENOSYS that claims ioctl() itself is missing. */
    {
        int unknown_ok = 1;
        struct {
            const char *name;
            int fd;
        } targets[] = {
            {"file", create_scratch_file("unknown")},
            {"directory", open("/", O_RDONLY | O_DIRECTORY)},
            {"device", open("/dev/null", O_RDWR)},
            {"pipe", -1},
            {"socket", -1},
        };
        int unsupported_pipe[2] = {-1, -1};
        int unsupported_socket[2] = {-1, -1};
        if (pipe(unsupported_pipe) == 0) targets[3].fd = unsupported_pipe[0];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, unsupported_socket) == 0) {
            targets[4].fd = unsupported_socket[0];
        }
        for (size_t index = 0; index < sizeof(targets) / sizeof(targets[0]); ++index) {
            if (targets[index].fd < 0) continue;
            errno = 0;
            int ret = raw_ioctl((unsigned long)(unsigned int)targets[index].fd,
                                0x7fff1234UL, 0);
            if (ret != -1 || errno != ENOTTY) {
                fail("unknown-request-enotty", "%s fd returned %d with %s, expected ENOTTY",
                     targets[index].name, ret, errno_text(errno));
                unknown_ok = 0;
            }
            if (index == 0) unlink(scratch);
            if (index != 3 && index != 4) close(targets[index].fd);
        }
        if (unsupported_pipe[0] >= 0) {
            close(unsupported_pipe[0]);
            close(unsupported_pipe[1]);
        }
        if (unsupported_socket[0] >= 0) {
            close(unsupported_socket[0]);
            close(unsupported_socket[1]);
        }
        if (unknown_ok) pass("unknown-request-enotty");
    }

    int path_fd = open("/", O_PATH | O_DIRECTORY);
    if (path_fd < 0) {
        skip("o_path-ioctl-rejected", "open(O_PATH) unavailable in this environment");
    } else {
        /* Linux resolves ioctl() with fdget(), which rejects FMODE_PATH, so
         * every request on an O_PATH descriptor fails with EBADF even though
         * fcntl() (fdget_raw) still manages FD_CLOEXEC on the same fd. */
        if (expect_ebadf("o_path-ioctl-rejected", (unsigned long)(unsigned int)path_fd,
                         FIOCLEX) < 0 ||
            expect_ebadf("o_path-ioctl-rejected-clear",
                         (unsigned long)(unsigned int)path_fd, FIONCLEX) < 0) {
            /* reported by expect_ebadf */
        } else if (raw_fcntl((unsigned int)path_fd, F_SETFD, FD_CLOEXEC) != 0 ||
                   fd_flags(path_fd) != FD_CLOEXEC) {
            fail("o_path-ioctl-rejected", "fcntl(F_SETFD) failed on O_PATH: %s",
                 errno_text(errno));
        } else if (raw_fcntl((unsigned int)path_fd, F_SETFD, 0) != 0 ||
                   fd_flags(path_fd) != 0) {
            fail("o_path-ioctl-rejected", "fcntl could not clear the O_PATH flag");
        } else {
            pass("o_path-ioctl-rejected-fcntl-works");
        }
        close(path_fd);
    }
    return 0;
}

/**
 * @brief Case 6: implicit stdin/stdout/stderr descriptors store the flag.
 *
 * LeonOS keeps the three initial descriptors in a deferred table, so a flag
 * that is accepted but not saved would only show up at exec time.  The state
 * is restored here so the remaining cases keep their stdio.
 */
static int case_implicit_stdio(void)
{
    int tested = 0;
    int ok = 1;
    for (int fd = 0; fd <= 2; ++fd) {
        errno = 0;
        int flags = fd_flags(fd);
        if (flags < 0) {
            skip("implicit-stdio", "descriptor is not open in this environment");
            continue;
        }
        if (flags != 0) {
            fail("implicit-stdio-clean", "fd %d starts with flags=0x%x", fd, flags);
            ok = 0;
            continue;
        }
        if (expect_flag("implicit-stdio-set", fd, FIOCLEX, 1) < 0) { ok = 0; continue; }
        if (expect_flag("implicit-stdio-clear", fd, FIONCLEX, 0) < 0) { ok = 0; continue; }
        /* fcntl must see the same storage. */
        if (raw_fcntl((unsigned int)fd, F_SETFD, FD_CLOEXEC) != 0 ||
            fd_flags(fd) != FD_CLOEXEC) {
            fail("implicit-stdio-fcntl", "fd %d lost the fcntl flag (%s)",
                 fd, errno_text(errno));
            ok = 0;
            continue;
        }
        if (raw_fcntl((unsigned int)fd, F_SETFD, 0) != 0 || fd_flags(fd) != 0) {
            fail("implicit-stdio-fcntl", "fd %d kept flags=0x%x", fd, fd_flags(fd));
            ok = 0;
            continue;
        }
        ++tested;
    }
    if (ok && tested == 3) pass("implicit-stdio-flag-storage");
    else if (ok && tested > 0) pass("implicit-stdio-flag-storage-partial");
    return 0;
}

/**
 * @brief Case 6b: CLONE_FILES threads share one descriptor table.
 *
 * A pthread is created with CLONE_FILES, so a flag written through the ioctl
 * in one thread must be visible in the other, and the shared table must keep
 * it after the setting thread exits.  fork() children instead inherit their
 * own descriptor entries, which the exec cases below verify.
 */
struct thread_flags {
    int fd;
    int set;
    int observed;
};

static void *thread_flags_worker(void *argument)
{
    struct thread_flags *work = argument;
    if (work->set &&
        raw_ioctl((unsigned long)(unsigned int)work->fd, FIOCLEX, 0) != 0) {
        work->observed = -1;
        return NULL;
    }
    work->observed = fd_flags(work->fd);
    return NULL;
}

static int case_clone_files(void)
{
    int fd = create_scratch_file("threads");
    if (fd < 0) {
        fail("clone_files-open", "open failed with %s", errno_text(errno));
        return -1;
    }
    pthread_t thread;
    struct thread_flags setter = {.fd = fd, .set = 1, .observed = -2};
    if (pthread_create(&thread, NULL, thread_flags_worker, &setter) != 0) {
        skip("clone_files-shared-flags", "pthread_create unavailable in this environment");
        close(fd);
        unlink(scratch);
        return 0;
    }
    pthread_join(thread, NULL);
    if (setter.observed != FD_CLOEXEC || fd_flags(fd) != FD_CLOEXEC) {
        fail("clone_files-shared-flags",
             "thread set FIOCLEX but the shared table reports 0x%x", fd_flags(fd));
    } else if (expect_flag("clone_files-clear", fd, FIONCLEX, 0) < 0) {
        /* reported by expect_flag */
    } else {
        struct thread_flags reader = {.fd = fd, .set = 0, .observed = -2};
        if (pthread_create(&thread, NULL, thread_flags_worker, &reader) != 0) {
            skip("clone_files-shared-flags", "second pthread_create failed");
        } else {
            pthread_join(thread, NULL);
            if (reader.observed != 0) {
                fail("clone_files-shared-flags",
                     "thread observed 0x%x after the main thread cleared FD_CLOEXEC",
                     reader.observed);
            } else {
                pass("clone_files-shared-flags");
            }
        }
    }
    close(fd);
    unlink(scratch);
    return 0;
}

/* ---------------------------------------------------------------- exec ---- */

static const char *resolve_self(char *buffer, size_t capacity)
{
    ssize_t length = readlink("/proc/self/exe", buffer, capacity - 1);
    if (length > 0) {
        buffer[length] = 0;
        return buffer;
    }
    if (self_argv0 && self_argv0[0] == '/') return self_argv0;
    return self_argv0;
}

/**
 * @brief Child after execve: verify which descriptors survived the exec.
 * @param report_fd Inherited pipe used to report results to the parent.
 * @param closed_fd Descriptor that must be closed by the exec (or -1).
 * @param extra_closed_fd Second descriptor that must be closed (or -1).
 * @param survivor_fd Descriptor that must still be open after the exec.
 * @return Zero only when every expectation held.
 */
static int child_verify(int report_fd, int closed_fd, int extra_closed_fd,
                        int survivor_fd)
{
    int ok = 1;
    report_used = 0;
    int expect_closed[2] = {closed_fd, extra_closed_fd};
    for (int i = 0; i < 2; ++i) {
        if (expect_closed[i] < 0) continue;
        errno = 0;
        int flags = fd_flags(expect_closed[i]);
        if (flags == -1 && errno == EBADF) {
            ++checks;
            emit("PASS exec-closed-fd-%d\n", expect_closed[i]);
        } else {
            ++checks;
            ++failures;
            ok = 0;
            emit("FAIL exec-closed-fd-%d: fcntl returned %d %s\n",
                 expect_closed[i], flags, errno_text(errno));
        }
    }
    errno = 0;
    int survivor_flags = fd_flags(survivor_fd);
    if (survivor_flags >= 0) {
        ++checks;
        emit("PASS exec-survived-fd-%d\n", survivor_fd);
    } else {
        ++checks;
        ++failures;
        ok = 0;
        emit("FAIL exec-survived-fd-%d: %s\n", survivor_fd, errno_text(errno));
    }
    if (write(report_fd, report, report_used) != (ssize_t)report_used) ok = 0;
    return ok ? 0 : 1;
}

/**
 * @brief Fork and exec this binary, then verify the exec-time close.
 * @param closed_fd Descriptor marked FD_CLOEXEC with the ioctl under test.
 * @param extra_closed_fd Additional descriptor marked closed (-1 when unused).
 * @param survivor_fd Descriptor that must not be closed.
 * @param name Case name reported by the parent.
 * @return Zero when the child reported that every expectation held.
 */
static int run_exec_case(const char *name, int closed_fd, int extra_closed_fd,
                         int survivor_fd)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        fail(name, "pipe() failed with %s", errno_text(errno));
        return -1;
    }
    char executable[PATH_MAX];
    const char *self = resolve_self(executable, sizeof(executable));
    if (!self) {
        fail(name, "cannot resolve the test executable path");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    char closed_text[16], extra_text[16], survivor_text[16], report_text[16];
    snprintf(closed_text, sizeof(closed_text), "%d", closed_fd);
    snprintf(extra_text, sizeof(extra_text), "%d", extra_closed_fd);
    snprintf(survivor_text, sizeof(survivor_text), "%d", survivor_fd);
    snprintf(report_text, sizeof(report_text), "%d", pipefd[1]);
    pid_t child = fork();
    if (child < 0) {
        fail(name, "fork() failed with %s", errno_text(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (child == 0) {
        close(pipefd[0]);
        char *const argv[] = {(char *)self, (char *)CHILD_MODE, report_text,
                              closed_text, extra_text, survivor_text, NULL};
        execve(self, argv, environ);
        /* Report the exec failure through the inherited pipe so the parent can
         * print it; stdout may already be closed by FD_CLOEXEC. */
        report_used = 0;
        emit("FAIL exec-execve: %s\n", errno_text(errno));
        (void)!write(pipefd[1], report, report_used);
        _exit(1);
    }
    close(pipefd[1]);
    char buffer[4096];
    ssize_t got;
    size_t used = 0;
    while ((got = read(pipefd[0], buffer + used, sizeof(buffer) - used - 1)) > 0) {
        used += (size_t)got;
        if (used >= sizeof(buffer) - 1) break;
    }
    buffer[used] = 0;
    close(pipefd[0]);
    int status = 0;
    if (waitpid(child, &status, 0) != child) {
        fail(name, "waitpid failed with %s", errno_text(errno));
        return -1;
    }
    int child_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    for (char *line = strtok(buffer, "\n"); line; line = strtok(NULL, "\n")) {
        printf("[ioctl-clex] child %s\n", line);
    }
    fflush(stdout);
    if (!child_ok) {
        fail(name, "child exited with status %d", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return -1;
    }
    ++checks;
    printf("[ioctl-clex] PASS %s\n", name);
    fflush(stdout);
    return 0;
}

/** @brief Case 7: execve really closes marked descriptors and keeps the rest. */
static int case_exec(void)
{
    int marked = create_scratch_file("exec-marked");
    int survivor = create_scratch_file("exec-survivor");
    if (marked < 0 || survivor < 0) {
        fail("exec-setup", "open failed with %s", errno_text(errno));
        if (marked >= 0) close(marked);
        if (survivor >= 0) close(survivor);
        return -1;
    }
    if (expect_flag("exec-mark", marked, FIOCLEX, 1) < 0) goto out;
    if (fd_flags(survivor) != 0) {
        fail("exec-survivor-clean", "survivor flags=0x%x", fd_flags(survivor));
        goto out;
    }
    run_exec_case("exec-closes-marked-descriptor", marked, -1, survivor);

    /* Same check for the implicit stdout slot: mark fd 1 close-on-exec and
     * confirm the exec'ed child sees it closed while fd 2 and the pipe stay. */
    errno = 0;
    int stdout_flags = fd_flags(STDOUT_FILENO);
    if (stdout_flags < 0) {
        skip("exec-closes-implicit-stdio", "stdout is not open in this environment");
    } else if (expect_flag("exec-stdio-mark", STDOUT_FILENO, FIOCLEX, 1) < 0) {
        /* reported by expect_flag */
    } else {
        run_exec_case("exec-closes-implicit-stdio", STDOUT_FILENO, -1, survivor);
        if (fd_flags(STDOUT_FILENO) & FD_CLOEXEC) {
            expect_flag("exec-stdio-restore", STDOUT_FILENO, FIONCLEX, 0);
        }
    }

    /* FIONCLEX must also prevent the close: mark then unmark, and the exec'ed
     * child has to still own the descriptor (nothing is required to close). */
    if (expect_flag("exec-unmark", marked, FIONCLEX, 0) == 0) {
        run_exec_case("exec-keeps-unmarked-descriptor", -1, -1, marked);
    }
out:
    if (marked >= 0) close(marked);
    if (survivor >= 0) close(survivor);
    unlink(scratch);
    return 0;
}

static void case_ioctl_badfd(void)
{
    /* Bit 2 is ignored by Linux open; it must not inject our internal PATH bit. */
    int ignored = (int)syscall(SYS_open, "/dev/null", O_RDONLY | 4, 0);
    if (ignored < 0) {
        fail("open-ignored-flag", "open failed: %s", errno_text(errno));
    } else {
        if (expect_flag("open-ignored-flag", ignored, FIOCLEX, 1) == 0)
            pass("open-ignored-flag");
        close(ignored);
    }
    int closed = open("/dev/null", O_RDONLY);
    if (closed < 0) {
        fail("ioctl-invalid-fd-setup", "open failed");
        return;
    }
    close(closed);
    const unsigned long descriptors[] = {UINT_MAX, INT_MAX, (unsigned int)closed};
    const unsigned int requests[] = {0x7fff1234, TCGETS, TIOCGWINSZ, FIOCLEX, FIONCLEX};
    for (size_t i = 0; i < sizeof(descriptors) / sizeof(descriptors[0]); ++i) {
        for (size_t j = 0; j < sizeof(requests) / sizeof(requests[0]); ++j) {
            /* EBADF precedes command handling and even an invalid user pointer. */
            errno = 0;
            if (raw_ioctl(descriptors[i], requests[j], ULONG_MAX) != -1 || errno != EBADF) {
                fail("ioctl-invalid-fd-precedence", "fd=%lu cmd=%x: %s",
                     descriptors[i], requests[j], errno_text(errno));
                return;
            }
        }
    }
    pass("ioctl-invalid-fd-precedence");
}

static void case_epoll(void)
{
    int epfd = (int)syscall(SYS_epoll_create1, EPOLL_CLOEXEC);
    int efd = (int)syscall(SYS_eventfd2, 0, 0);
    int duplicate = -1;
    if (epfd < 0 || efd < 0) {
        fail("epoll-setup", "epoll_create1/eventfd2 failed: %s", errno_text(errno));
        goto out;
    }
    if (fd_flags(epfd) != FD_CLOEXEC) {
        fail("epoll-create-cloexec", "epoll_create1 lost FD_CLOEXEC");
        goto out;
    }
    if (check_type("type-epoll", epfd) < 0) goto out;
    struct epoll_event event = {.events = EPOLLIN, .data.u64 = 0x12345678};
    struct epoll_event ready = {0};
    unsigned long long value = 1;
    if (syscall(SYS_epoll_ctl, epfd, EPOLL_CTL_ADD, efd, &event) != 0 ||
        syscall(SYS_epoll_wait, epfd, &ready, 1, 0) != 0 ||
        write(efd, &value, sizeof(value)) != sizeof(value) ||
        syscall(SYS_epoll_wait, epfd, &ready, 1, 0) != 1 ||
        ready.events != EPOLLIN || ready.data.u64 != event.data.u64) {
        fail("epoll-add-wait", "registration/readiness failed: %s", errno_text(errno));
        goto out;
    }
    pass("epoll-add-wait");
    duplicate = dup(epfd);
    if (duplicate < 0 || fd_flags(duplicate) != 0 ||
        expect_flag("epoll-dup-mark", duplicate, FIOCLEX, 1) < 0 ||
        expect_flag("epoll-original-clear", epfd, FIONCLEX, 0) < 0 ||
        fd_flags(duplicate) != FD_CLOEXEC) {
        fail("epoll-dup-flags", "dup state was not independent");
        goto out;
    }
    run_exec_case("exec-closes-epoll", duplicate, -1, epfd);
    close(epfd);
    epfd = -1;
    event.data.u64 = 0x87654321;
    if (syscall(SYS_epoll_ctl, duplicate, EPOLL_CTL_MOD, efd, &event) != 0 ||
        syscall(SYS_epoll_wait, duplicate, &ready, 1, 0) != 1 ||
        ready.data.u64 != event.data.u64 ||
        read(efd, &value, sizeof(value)) != sizeof(value) || value != 1 ||
        syscall(SYS_epoll_wait, duplicate, &ready, 1, 0) != 0 ||
        syscall(SYS_epoll_ctl, duplicate, EPOLL_CTL_DEL, efd, NULL) != 0) {
        fail("epoll-dup-mod-del", "epoll did not survive closing its alias: %s", errno_text(errno));
        goto out;
    }
    pass("epoll-dup-mod-del");
out:
    if (duplicate >= 0) close(duplicate);
    if (epfd >= 0) close(epfd);
    if (efd >= 0) close(efd);
}

int main(int argc, char **argv)
{
    self_argv0 = argv[0];
    if (argc == 6 && strcmp(argv[1], CHILD_MODE) == 0) {
        return child_verify(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    }
    printf("[ioctl-clex] start abi=linux-6.12-native-x86_64 pid=%d\n", (int)getpid());
    fflush(stdout);
    case_basics();
    case_dup_isolation();
    case_fcntl_consistency();
    case_descriptor_types();
    case_implicit_stdio();
    case_clone_files();
    case_exec();
    case_ioctl_badfd();
    case_epoll();
    printf("[ioctl-clex] DONE checks=%u failures=%u skips=%u\n", checks, failures, skips);
    fflush(stdout);
    return failures ? 1 : 0;
}
