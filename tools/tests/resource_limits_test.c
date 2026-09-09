#include <assert.h>
#include <stdio.h>
#include "../../kernel/ntclks/syscall_process.c"

static struct task caller, target;
static uint64_t readonly;
struct task *sched_current_task(void) { return &caller; }
struct task *sched_find(uint32_t pid) { return pid == 7 ? &target : NULL; }
bool user_range_ok(uint64_t address, uint64_t length)
{ return address >= 4096 && address + length >= address; }
bool user_range_writable(uint64_t address, uint64_t length)
{ return user_range_ok(address, length) && address != readonly; }

int main(void)
{
    caller.limits.nofile = (struct linux_rlimit64){1024,1048576};
    target = caller;
    target.shared_limits = &caller.limits;
    struct linux_rlimit64 value = {128,256}, old;
    assert(process_resource_limit(LINUX_SYS_PRLIMIT64, 7, 7, (uintptr_t)&value, (uintptr_t)&old) == 0);
    assert(old.rlim_cur == 1024 && old.rlim_max == 1048576);
    assert(caller.limits.nofile.rlim_cur == 128 && caller.limits.nofile.rlim_max == 256);
    value = (struct linux_rlimit64){64,256};
    readonly = (uintptr_t)&old;
    assert(process_resource_limit(LINUX_SYS_PRLIMIT64, 7, 7, (uintptr_t)&value, readonly) == -LINUX_EFAULT);
    assert(caller.limits.nofile.rlim_cur == 64);
    readonly = 0;
    value = (struct linux_rlimit64){32,256};
    assert(process_resource_limit(LINUX_SYS_PRLIMIT64, 0x100000007ULL, 0x100000007ULL,
                                  (uintptr_t)&value, (uintptr_t)&value) == 0);
    assert(value.rlim_cur == 64 && caller.limits.nofile.rlim_cur == 32);
    caller.uid = caller.euid = caller.suid = target.uid = target.euid = target.suid = 1000;
    caller.gid = caller.egid = caller.sgid = target.gid = target.egid = target.sgid = 2000;
    assert(process_resource_limit(LINUX_SYS_PRLIMIT64, 7, 7, 0, (uintptr_t)&old) == 0);
    ++target.sgid;
    assert(process_resource_limit(LINUX_SYS_PRLIMIT64, 7, 7, 0, (uintptr_t)&old) == -LINUX_EPERM);
    value = (struct linux_rlimit64){32,257};
    assert(process_resource_limit(LINUX_SYS_SETRLIMIT, 7, (uintptr_t)&value, 0, 0) == -LINUX_EPERM);
    value = (struct linux_rlimit64){257,256};
    assert(process_resource_limit(LINUX_SYS_SETRLIMIT, 7, (uintptr_t)&value, 0, 0) == -LINUX_EINVAL);
    assert(process_resource_limit(LINUX_SYS_GETRLIMIT, 16, 1, 0, 0) == -LINUX_EINVAL);
    assert(process_resource_limit(LINUX_SYS_SETRLIMIT, 16, 1, 0, 0) == -LINUX_EFAULT);
    assert(process_resource_limit(LINUX_SYS_PRLIMIT64, -1, 16, 0, 1) == -LINUX_ESRCH);
    puts("PASS native rlimit helpers: shared state, hard limits, widths, ID checks, alias and output-fault commit");
}
