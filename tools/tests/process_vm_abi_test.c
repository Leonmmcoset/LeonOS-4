#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#define PVM_CHECK(expr) do { if (!(expr)) { \
    printf("process_vm ABI line %d: %s errno=%d\n", __LINE__, #expr, errno); return 1; \
} } while (0)

static int process_vm_syscalls(void)
{
    char input[] = "0123456789abcdef", output[32] = {0};
    struct iovec local[2] = {{output, 3}, {output + 3, 13}};
    struct iovec remote[2] = {{input, 7}, {input + 7, 9}};
    pid_t pid = getpid();
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 2, remote, 2, 0) == 16);
    PVM_CHECK(!memcmp(output, input, 16));
    memcpy(output, "abcdefghijklmnop", 16);
    PVM_CHECK(syscall(SYS_process_vm_writev, pid, local, 2, remote, 2, 0) == 16);
    PVM_CHECK(!memcmp(output, input, 16));
    PVM_CHECK(syscall(SYS_process_vm_readv, -1, NULL, 0, NULL, -1L, 1) == -1 && errno == EINVAL);
    PVM_CHECK(syscall(SYS_process_vm_readv, -1, NULL, 0, (void *)1, -1L, 0) == 0);
    PVM_CHECK(syscall(SYS_process_vm_readv, -1, local, 2, NULL, 0, 0) == 0);
    PVM_CHECK(syscall(SYS_process_vm_readv, -1, local, 2, remote, 2, 0) == -1 && errno == ESRCH);
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 1025, remote, 2, 0) == -1 && errno == EINVAL);
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 2, remote, 1ULL << 32, 0) == -1 && errno == EINVAL);
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, (1ULL << 32) | 2, remote, 2, 0) == 16);
    PVM_CHECK(syscall(SYS_process_vm_readv, (1ULL << 32) | (uint32_t)pid, local, 2, remote, 2, 0) == 16);
    remote[0].iov_len = SIZE_MAX;
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 2, remote, 2, 0) == -1 && errno == EINVAL);
    remote[0] = (struct iovec){(void *)1, 1};
    PVM_CHECK(syscall(SYS_process_vm_readv, -1, local, 2, remote, 1, 0) == -1 && errno == ESRCH);
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 2, remote, 1, 0) == -1 && errno == EFAULT);
    remote[0] = (struct iovec){input, 16};
    local[0] = (struct iovec){output, INT64_MAX};
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 1, remote, 1, 0) == 16);
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 2, remote, 1, 0) == -1 && errno == EFAULT);
    size_t page = sysconf(_SC_PAGESIZE);
    unsigned char *guard = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    PVM_CHECK(guard != MAP_FAILED);
    memset(guard, 0x53, page);
    PVM_CHECK(mprotect(guard + page, page, PROT_NONE) == 0);
    local[0] = (struct iovec){output, 32};
    remote[0] = (struct iovec){guard + page - 16, 32};
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 1, remote, 1, 0) == 16);
    PVM_CHECK(output[0] == 0x53 && output[15] == 0x53);
    remote[0] = (struct iovec){guard, 32};
    local[0] = (struct iovec){guard + page - 16, 32};
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 1, remote, 1, 0) == 16);
    PVM_CHECK(syscall(SYS_process_vm_writev, pid, local, 1, remote, 1, 0) == 16);
    local[0] = (struct iovec){output, 16};
    PVM_CHECK(mprotect(guard, page, PROT_WRITE) == 0);
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 1, remote, 1, 0) == -1 && errno == EFAULT);
    PVM_CHECK(mprotect(guard, page, PROT_READ) == 0);
    PVM_CHECK(syscall(SYS_process_vm_writev, pid, local, 1, remote, 1, 0) == -1 && errno == EFAULT);
    PVM_CHECK(mprotect(guard, page, PROT_READ | PROT_WRITE) == 0);
    int old_dumpable = prctl(PR_GET_DUMPABLE);
    PVM_CHECK(old_dumpable >= 0 && prctl(PR_SET_DUMPABLE, 0) == 0);
    PVM_CHECK(syscall(SYS_process_vm_readv, pid, local, 1, remote, 1, 0) == 16);
    PVM_CHECK(prctl(PR_SET_DUMPABLE, old_dumpable) == 0);
    int command[2], response[2];
    PVM_CHECK(pipe(command) == 0 && pipe(response) == 0);
    pid_t child = fork();
    PVM_CHECK(child >= 0);
    if (!child) {
        close(command[1]); close(response[0]);
        unsigned char token = 0;
        int ok = write(response[1], "r", 1) == 1 && read(command[0], &token, 1) == 1;
        ok = ok && guard[0] == 0x62 && prctl(PR_SET_DUMPABLE, 0) == 0;
        token = ok ? 'd' : 'e';
        ok = write(response[1], &token, 1) == 1 && ok;
        ok = read(command[0], &token, 1) == 1 && ok;
        _exit(ok ? 0 : 1);
    }
    close(command[0]); close(response[1]);
    char token;
    PVM_CHECK(read(response[0], &token, 1) == 1 && token == 'r');
    remote[0] = (struct iovec){guard, 1};
    PVM_CHECK(syscall(SYS_process_vm_readv, child, local, 1, remote, 1, 0) == 1 && output[0] == 0x53);
    output[0] = 0x62;
    PVM_CHECK(syscall(SYS_process_vm_writev, child, local, 1, remote, 1, 0) == 1);
    PVM_CHECK(guard[0] == 0x53);
    PVM_CHECK(write(command[1], "w", 1) == 1 && read(response[0], &token, 1) == 1 && token == 'd');
    struct { uint32_t version; int32_t pid; } header = {0x20080522u, 0};
    struct { uint32_t effective, permitted, inheritable; } caps[2];
    PVM_CHECK(syscall(SYS_capget, &header, caps) == 0);
    if (!(caps[0].effective & (1u << 19)))
        PVM_CHECK(syscall(SYS_process_vm_readv, child, local, 1, remote, 1, 0) == -1 && errno == EPERM);
    PVM_CHECK(write(command[1], "x", 1) == 1);
    int status;
    PVM_CHECK(waitpid(child, &status, 0) == child && status == 0);
    PVM_CHECK(syscall(SYS_process_vm_readv, child, local, 1, remote, 1, 0) == -1 && errno == ESRCH);
    close(command[1]); close(response[0]);
    PVM_CHECK(munmap(guard, 2 * page) == 0);
    puts("PASS raw Linux process_vm: vectors, zero/errors, widths, partial faults, protection, fork COW and dumpability");
    return 0;
}

#ifndef PROCESS_VM_EMBEDDED
int main(void) { return process_vm_syscalls(); }
#endif
