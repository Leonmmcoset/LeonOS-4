#include <leonos/fs.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>

int main(void)
{
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
    printf("[init.elf] getcwd => %x\n", (unsigned int)(uintptr_t)getcwd(cwd, sizeof(cwd)));
    printf("[init.elf] cwd=%s\n", cwd);
    ret = leonos_stat_legacy("/system/config/leonos.conf", &st);
    printf("[init.elf] stat leonos.conf => %d type=%d size=%d\n", ret, (int)st.type, (int)st.size);
    ret = chdir("/system/config");
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
    int status = 0;
    int reaped = wait4(2, &status, 0, 0);
    printf("[init.elf] initial child reap result=%d status=%d\n", reaped, status);
    return 0;
}
