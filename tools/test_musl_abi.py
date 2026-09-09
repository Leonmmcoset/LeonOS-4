#!/usr/bin/env python3
"""Compile against actual musl headers and run Linux ABI probes on the host."""

import argparse
from pathlib import Path
import subprocess
import tempfile

from generate_linux_syscalls import syscall_entries

ROOT = Path(__file__).resolve().parents[1]


def run(args):
    subprocess.run([str(arg) for arg in args], check=True, timeout=60)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=ROOT / "build/musl/sysroot")
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    resource = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
    cc = ["clang", "--target=x86_64-linux-musl", "-O2", "-fPIC", "-nostdinc",
          "-isystem", prefix / "include", "-isystem", Path(resource) / "include"]
    with tempfile.TemporaryDirectory(prefix="leonos-musl-") as directory:
        temp = Path(directory)
        checks = ["#define _GNU_SOURCE", "#include <fcntl.h>", "#include <sys/stat.h>",
                  "#include <sys/syscall.h>", "#include <stddef.h>",
                  "#include <sys/mman.h>", "#include <linux/mman.h>",
                  "#include <termios.h>", "#include <linux/termios.h>",
                  "#include <linux/fcntl.h>", "#include <linux/stat.h>"]
        checks += ["#include <sys/statfs.h>", "#include <linux/statfs.h>",
                   '_Static_assert(sizeof(struct statfs) == sizeof(struct linux_statfs_abi), "statfs size");']
        checks += ["#include <sys/resource.h>", "#include <linux/resource.h>",
                   '_Static_assert(sizeof(struct rlimit) == sizeof(struct linux_rlimit64), "rlimit size");',
                   '_Static_assert(offsetof(struct rlimit, rlim_max) == offsetof(struct linux_rlimit64, rlim_max), "rlimit max offset");',
                   '_Static_assert(RLIM_INFINITY == LINUX_RLIM_INFINITY, "rlimit infinity");']
        for name in ("CPU", "FSIZE", "DATA", "STACK", "CORE", "RSS", "NPROC", "NOFILE",
                     "MEMLOCK", "AS", "LOCKS", "SIGPENDING", "MSGQUEUE", "NICE", "RTPRIO", "RTTIME"):
            checks += [f'_Static_assert(RLIMIT_{name} == LINUX_RLIMIT_{name}, "RLIMIT_{name} drift");']
        checks += ["#include <signal.h>", "#include <ucontext.h>", "#include <linux/signal.h>"]
        checks += ["#include <time.h>", "#include <sys/time.h>", "#include <linux/time.h>",
                   '_Static_assert(sizeof(struct timespec) == sizeof(struct linux_timespec), "timespec size");',
                   '_Static_assert(sizeof(struct timeval) == sizeof(struct linux_timeval), "timeval size");',
                   '_Static_assert(offsetof(struct timespec, tv_nsec) == offsetof(struct linux_timespec, tv_nsec), "nanoseconds offset");']
        for name in ("CLOCK_REALTIME", "CLOCK_MONOTONIC", "CLOCK_PROCESS_CPUTIME_ID", "CLOCK_THREAD_CPUTIME_ID",
                     "CLOCK_MONOTONIC_RAW", "CLOCK_REALTIME_COARSE", "CLOCK_MONOTONIC_COARSE", "CLOCK_BOOTTIME", "TIMER_ABSTIME"):
            checks += [f'_Static_assert({name} == LINUX_{name}, "{name} drift");']
        for name in ("SA_NOCLDSTOP", "SA_NOCLDWAIT", "SA_SIGINFO", "SA_ONSTACK",
                     "SA_RESTART", "SA_NODEFER", "SA_RESETHAND"):
            checks += [f'_Static_assert({name} == LINUX_{name}, "{name} drift");']
        for libc, raw in (("uc_flags", "flags"), ("uc_link", "link"),
                          ("uc_stack", "stack"), ("uc_mcontext", "context"),
                          ("uc_sigmask", "mask")):
            checks += [f'_Static_assert(offsetof(ucontext_t, {libc}) == '
                       f'offsetof(struct linux_ucontext, {raw}), "{libc} offset");']
        checks += ['_Static_assert(sizeof(stack_t) == sizeof(struct linux_sigaltstack), "altstack size");',
                   '_Static_assert(sizeof(mcontext_t) == sizeof(struct linux_sigcontext), "mcontext size");',
                   '_Static_assert(sizeof(siginfo_t) == sizeof(struct linux_siginfo), "siginfo size");',
                   '_Static_assert(offsetof(mcontext_t, fpregs) == offsetof(struct linux_sigcontext, fpstate), "fpstate offset");',
                   '_Static_assert(offsetof(siginfo_t, si_pid) == offsetof(struct linux_siginfo, fields.sender.pid), "sender pid offset");',
                   '_Static_assert(offsetof(siginfo_t, si_uid) == offsetof(struct linux_siginfo, fields.sender.uid), "sender uid offset");']
        for field in ("r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rdi", "rsi",
                      "rbp", "rbx", "rdx", "rax", "rcx", "rsp", "rip"):
            checks += [f'_Static_assert(offsetof(mcontext_t, gregs) + REG_{field.upper()} * sizeof(greg_t) == '
                       f'offsetof(struct linux_sigcontext, {field}), "{field} signal offset");']
        for field in ("f_type", "f_bsize", "f_blocks", "f_bfree", "f_bavail", "f_files", "f_ffree",
                      "f_fsid", "f_namelen", "f_frsize", "f_flags"):
            checks += [f'_Static_assert(offsetof(struct statfs, {field}) == offsetof(struct linux_statfs_abi, {field}), "statfs {field}");']
        # Capture libc errno values before adding the UAPI include: libc must
        # provide the independent expectation even when errno.h aliases it.
        checks += ["#include <errno.h>"]
        errno_names = ["EPERM", "ENOENT", "ESRCH", "EINTR", "EIO", "ENXIO", "EBADF",
                       "EAGAIN", "ENOMEM", "EACCES", "EFAULT", "EBUSY", "EEXIST",
                       "EXDEV", "ENODEV", "ENOTDIR", "EISDIR", "EINVAL", "ENFILE",
                       "EMFILE", "ENOTTY", "EFBIG", "ENOSPC", "ESPIPE", "EROFS",
                       "EPIPE", "ERANGE", "ENAMETOOLONG", "ENOSYS", "ENOTEMPTY",
                       "ELOOP", "EOVERFLOW", "ENOTSOCK", "EOPNOTSUPP", "ETIMEDOUT",
                       "ECONNREFUSED", "EINPROGRESS", "ECANCELED", "EOWNERDEAD", "ENOTRECOVERABLE"]
        checks += [f"enum {{ MUSL_{name} = {name} }};" for name in errno_names]
        checks += ["#include <linux/errno.h>"]
        checks += [f'_Static_assert(MUSL_{name} == LINUX_{name}, "{name} drift");' for name in errno_names]
        flags = ["O_RDONLY", "O_WRONLY", "O_RDWR", "O_CREAT", "O_EXCL",
                 "O_NOCTTY", "O_TRUNC", "O_APPEND", "O_NONBLOCK", "O_DSYNC", "O_ASYNC",
                 "O_DIRECT", "O_DIRECTORY", "O_NOFOLLOW", "O_NOATIME", "O_CLOEXEC",
                 "O_SYNC", "O_PATH", "O_TMPFILE", "F_DUPFD", "F_GETFD", "F_SETFD",
                 "F_GETFL", "F_SETFL", "F_GETLK", "F_SETLK", "F_SETLKW", "F_SETOWN",
                 "F_GETOWN", "F_DUPFD_CLOEXEC", "FD_CLOEXEC", "AT_FDCWD",
                 "AT_SYMLINK_NOFOLLOW", "AT_REMOVEDIR", "AT_EACCESS",
                 "AT_SYMLINK_FOLLOW", "AT_EMPTY_PATH", "PROT_NONE", "PROT_READ",
                 "PROT_WRITE", "PROT_EXEC", "MAP_SHARED", "MAP_PRIVATE",
                 "MAP_SHARED_VALIDATE", "MAP_FIXED", "MAP_ANONYMOUS",
                 "MAP_NORESERVE", "MAP_FIXED_NOREPLACE"]
        flags += ["S_IFMT", "S_IFSOCK", "S_IFLNK", "S_IFREG", "S_IFBLK", "S_IFDIR",
                  "S_IFCHR", "S_IFIFO", "S_ISUID", "S_ISGID", "S_ISVTX",
                  "S_IRWXU", "S_IRWXG", "S_IRWXO"]
        flags += ["VINTR", "VQUIT", "VERASE", "VKILL", "VEOF", "VTIME", "VMIN",
                  "VSTART", "VSTOP", "VSUSP", "VEOL", "VREPRINT", "VDISCARD",
                  "VWERASE", "VLNEXT", "VEOL2", "IGNBRK", "BRKINT", "IGNPAR",
                  "PARMRK", "INPCK", "ISTRIP", "INLCR", "IGNCR", "ICRNL", "IUCLC",
                  "IXON", "IXANY", "IXOFF", "IMAXBEL", "IUTF8", "OPOST", "OLCUC",
                  "ONLCR", "OCRNL", "ONOCR", "ONLRET", "OFILL", "OFDEL", "NLDLY",
                  "CRDLY", "CR1", "CR2", "TABDLY", "TAB1", "TAB2", "BSDLY",
                  "VTDLY", "FFDLY", "CSIZE", "CS5", "CS6", "CS7", "CS8", "CSTOPB",
                  "CREAD", "PARENB", "PARODD", "HUPCL", "CLOCAL", "CBAUD", "CIBAUD",
                  "CMSPAR", "CRTSCTS", "B115200", "B57600", "B38400", "ISIG",
                  "ICANON", "ECHO", "ECHOE", "ECHOK", "ECHONL", "NOFLSH", "TOSTOP",
                  "ECHOCTL", "ECHOPRT", "ECHOKE", "FLUSHO", "PENDIN", "IEXTEN", "EXTPROC"]
        checks += [f'_Static_assert({name} == LINUX_{name}, "{name} drift");' for name in flags]
        # musl fcntl.h includes POSIX O_SEARCH in its API classification mask.
        # This is not a wire flag passed separately to open(2).
        checks += ['_Static_assert(O_ACCMODE == (LINUX_O_ACCMODE | O_SEARCH), "access mask");',
                   '_Static_assert(O_SEARCH == LINUX_O_PATH, "O_SEARCH encoding");']
        # Numeric expectations come from the unmodified Linux table, not our header.
        checks += [f'_Static_assert(SYS_{name} == {number}, "{name} number drift");'
                   for number, name in syscall_entries()]
        checks += ['_Static_assert(sizeof(struct stat) == 144, "native stat size");',
                   '_Static_assert(sizeof(struct linux_termios) == 36, "TCGETS size");',
                   '_Static_assert(sizeof(struct linux_termios2) == 44, "TCGETS2 size");',
                   '_Static_assert(offsetof(struct termios, c_cc) == 17, "musl termios prefix");',
                   '_Static_assert(offsetof(struct linux_termios, c_cc) == 17, "kernel termios prefix");',
                   '_Static_assert(sizeof(struct linux_stat_abi) == 144, "UAPI stat size");']
        for field in ("st_dev", "st_ino", "st_nlink", "st_mode", "st_uid", "st_gid",
                      "st_rdev", "st_size", "st_blksize", "st_blocks"):
            checks += [f'_Static_assert(offsetof(struct stat, {field}) == '
                       f'offsetof(struct linux_stat_abi, {field}), "{field} offset");']
        for libc, raw in (("st_atim", "atime_sec"), ("st_mtim", "mtime_sec"),
                          ("st_ctim", "ctime_sec")):
            checks += [f'_Static_assert(offsetof(struct stat, {libc}) == '
                       f'offsetof(struct linux_stat_abi, {raw}), "{libc} offset");']
        source = temp / "uapi.c"
        source.write_text("\n".join(checks) + "\n")
        run([*cc, "-I", ROOT / "include/uapi", "-Werror", "-fsyntax-only", source])
        print("PASS musl installed headers vs Linux v6.12 numbers and shared UAPI", flush=True)
        obj = temp / "runtime.o"
        run([*cc, "-c", ROOT / "tools/tests/musl_runtime_test.c", "-o", obj])
        executable = temp / "dynamic"
        run(["ld.lld", "-pie", "--dynamic-linker", "/lib/ld-musl-x86_64.so.1",
             "-o", executable, prefix / "lib/Scrt1.o", prefix / "lib/crti.o", obj,
             "-L", prefix / "lib", "-l:libmimalloc.so.3", "-lc", prefix / "lib/crtn.o"])
        dynamic = subprocess.check_output(["readelf", "-d", executable], text=True)
        assert "[libmimalloc.so.3]" in dynamic and "[libc.so]" in dynamic
        assert "sysroot" not in dynamic, "build path leaked into DT_NEEDED"
        run([prefix / "lib/libc.so", "--library-path", prefix / "lib", executable])
        executable = temp / "static"
        run(["ld.lld", "-static", "-o", executable, prefix / "lib/crt1.o",
             prefix / "lib/crti.o", obj, prefix / "lib/mimalloc.o",
             prefix / "lib/libc.a", prefix / "lib/crtn.o"])
        run([executable])


if __name__ == "__main__":
    main()
