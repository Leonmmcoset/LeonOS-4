#include <leonos/fs.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <leonos/layout.h>

static int init_block_signals(sigset_t *set)
{
    sigemptyset(set);
    sigaddset(set, SIGUSR1);
    sigaddset(set, SIGUSR2);
    sigaddset(set, SIGTERM);
    sigaddset(set, SIGCHLD);
    return sigprocmask(SIG_BLOCK, set, NULL);
}

static int init_dispatch_signal(int number)
{
    int command;
    /* Upstream BusyBox halt/poweroff/reboot signal PID 1 unless forced. */
    switch (number) {
    case SIGUSR1: command = RB_HALT_SYSTEM; break;
    case SIGUSR2: command = RB_POWER_OFF; break;
    case SIGTERM: command = RB_AUTOBOOT; break;
    default: return 0;
    }
    sync();
    return reboot(command);
}

int main(void)
{
    sigset_t signals;
    if (init_block_signals(&signals) < 0) {
        printf("[init.elf] cannot block init signals errno=%d\n", errno);
        return 1;
    }
    char cwd[LEONOS_FS_PATH_LEN];
    struct leonos_stat st;
    struct leonos_dir_entry entry;
    char conf[64];
    int fd;
    int ret;
    long got;
    puts("[init.elf] LeonOS 4 userland init started");
    printf("[init.elf] pid=%d\n", getpid());
    ret = chdir("/");
    printf("[init.elf] chdir root => %d\n", ret);
    /* Older installed images have no temporary directory. Preserve existing
     * metadata, including an administrator's explicit mode. */
    if (mkdir("/tmp", 01777) == 0 || errno == EEXIST) {
        if (chmod("/tmp", 01777) < 0)
            printf("[init.elf] chmod /tmp failed errno=%d\n", errno);
    } else {
        printf("[init.elf] mkdir /tmp failed errno=%d\n", errno);
    }
    /* /var/tmp shares /tmp's sticky writable lifecycle but persists across
     * reboots on the current ext2 root.  Missing parents are created first. */
    (void)mkdir("/var", 0755);
    (void)mkdir("/var/tmp", 01777);
    if (chmod("/var/tmp", 01777) < 0 && errno != ENOENT)
        printf("[init.elf] chmod /var/tmp failed errno=%d\n", errno);
    (void)mkdir("/run", 0755);
    if (mkdir("/run/leonos", 0755) == 0 || errno == EEXIST) {
        (void)chmod("/run/leonos", 0755);
    }
    printf("[init.elf] getcwd => %x\n", (unsigned int)(uintptr_t)getcwd(cwd, sizeof(cwd)));
    printf("[init.elf] cwd=%s\n", cwd);
    ret = leonos_stat_legacy(LEONOS_PATH_LEONOS_CONF, &st);
    printf("[init.elf] stat leonos.conf => %d type=%d size=%d\n", ret, (int)st.type, (int)st.size);
    ret = chdir(LEONOS_LAYOUT_ETC_LEONOS);
    printf("[init.elf] chdir system config => %d\n", ret);
    printf("[init.elf] getcwd after chdir => %x\n", (unsigned int)(uintptr_t)getcwd(cwd, sizeof(cwd)));
    printf("[init.elf] cwd after chdir=%s\n", cwd);
    fd = open(".", 0, 0);
    printf("[init.elf] open . => %d\n", fd);
    if (fd >= 0) {
        while (leonos_readdir(fd, &entry) > 0) {
            printf("[init.elf] dir entry %d %s\n", (int)entry.type, entry.name);
        }
        close(fd);
    }
    fd = open("leonos.conf", 0, 0);
    printf("[init.elf] open leonos.conf => %d\n", fd);
    if (fd >= 0) {
        ret = leonos_fstat_legacy(fd, &st);
        printf("[init.elf] fstat leonos.conf => %d type=%d size=%d\n", ret, (int)st.type, (int)st.size);
        got = read(fd, conf, sizeof(conf) - 1);
        printf("[init.elf] read leonos.conf => %d\n", (int)got);
        if (got >= 0) {
            conf[got] = 0;
            printf("[init.elf] read leonos.conf bytes=%d text=%s\n", (int)got, conf);
        }
        close(fd);
    }
    chdir("/");
    puts("[init.elf] startup services initialized");
    for (;;) {
        int status, reaped;
        while ((reaped = waitpid(-1, &status, WNOHANG)) > 0)
            printf("[init.elf] reaped child=%d status=%d\n", reaped, status);
        int number = sigwaitinfo(&signals, NULL);
        if (number < 0) {
            if (errno != EINTR) {
                printf("[init.elf] signal wait failed errno=%d\n", errno);
                sleep(1);
            }
            continue;
        }
        if (init_dispatch_signal(number) < 0)
            printf("[init.elf] power request failed signal=%d errno=%d\n", number, errno);
    }
}
