#include <errno.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

static int utsname_abi_test(void)
{
    struct utsname before = {0};
    if (syscall(SYS_uname, &before) != 0) return 1;
    if (strcmp(before.machine, "x86_64") != 0) return 2;

    if (geteuid() != 0) {
        errno = 0;
        if (syscall(SYS_sethostname, "abi-host", 8) != -1 || errno != EPERM) return 3;
        errno = 0;
        if (syscall(SYS_setdomainname, "abi-domain", 10) != -1 || errno != EPERM) return 4;
        return 0;
    }

    errno = 0;
    if (syscall(SYS_sethostname, NULL, 65) != -1 || errno != EINVAL) return 5;
    errno = 0;
    if (syscall(SYS_setdomainname, NULL, 65) != -1 || errno != EINVAL) return 6;
    errno = 0;
    if (syscall(SYS_sethostname, (void *)(uintptr_t)0x400000000000ULL, 1) != -1 || errno != EFAULT) return 7;
    errno = 0;
    if (syscall(SYS_setdomainname, (void *)(uintptr_t)0x400000000000ULL, 1) != -1 || errno != EFAULT) return 8;
    if (syscall(SYS_sethostname, "abi-host", 8) != 0) return 9;
    if (syscall(SYS_setdomainname, "abi-domain", 10) != 0) return 10;

    struct utsname after = {0};
    if (syscall(SYS_uname, &after) != 0) return 11;
    if (strcmp(after.nodename, "abi-host") != 0) return 12;
#ifdef _GNU_SOURCE
    if (strcmp(after.domainname, "abi-domain") != 0) return 13;
#else
    if (strcmp(after.__domainname, "abi-domain") != 0) return 13;
#endif
    if (syscall(SYS_sethostname, "", 0) != 0) return 14;
    if (syscall(SYS_setdomainname, "", 0) != 0) return 15;
    return 0;
}

#ifdef UTSNAME_EMBEDDED
static int run_utsname_abi_test(void) { return utsname_abi_test(); }
#else
int main(void) { return utsname_abi_test(); }
#endif
