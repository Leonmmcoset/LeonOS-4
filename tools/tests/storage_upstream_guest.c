/* Target musl reference probe; writes only disposable guest files/mounts. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned failures;
static void check(int ok, const char *label)
{
    printf("[storage-upstream] %s %s\n", ok ? "PASS" : "FAIL", label);
    failures += !ok;
}
static int run_tool(const char *label, char *const argv[], const char *input, const char *expected)
{
    puts("[storage-upstream] BEGIN");
    printf("[storage-upstream] command=%s\n", label);
    int in = open("/tmp/storage-test-input", O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (in < 0) { check(0, "open input"); return -1; }
    if (input) write(in, input, strlen(input));
    lseek(in, 0, SEEK_SET);
    pid_t child = fork();
    if (child == 0) {
        int output = open("/tmp/storage-test-output", O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (output < 0 || dup2(in, 0) < 0 || dup2(output, 1) < 0 || dup2(output, 2) < 0) _exit(126);
        close(in); close(output);
        execv(argv[0], argv);
        perror("exec official tool");
        _exit(127);
    }
    close(in);
    int status = 0, ready = 0;
    for (unsigned n = 0; child > 0 && n < 200; ++n) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) { ready = 1; break; }
        if (result < 0 && errno != EINTR) break;
        usleep(100000);
    }
    if (child > 0 && !ready) { kill(child, SIGKILL); waitpid(child, &status, 0); }
    char buffer[8192];
    int output = open("/tmp/storage-test-output", O_RDONLY);
    ssize_t length = output < 0 ? -1 : read(output, buffer, sizeof(buffer) - 1);
    if (output >= 0) close(output);
    if (length < 0) length = 0;
    buffer[length] = 0;
    printf("%s\n[storage-upstream] status=%d ready=%d\n", buffer, status, ready);
    int ok = ready && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
             (!expected || strstr(buffer, expected));
    check(ok, label);
    return ok ? 0 : -1;
}
static int make_file(const char *path, off_t size)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    int ok = fd >= 0 && ftruncate(fd, size) == 0;
    if (fd >= 0) close(fd);
    check(ok, "create disposable guest image");
    return ok;
}
#define RUN(label, input, expected, ...) do { \
    char *args[] = {__VA_ARGS__, NULL}; run_tool(label, args, input, expected); \
} while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("LC_ALL", "C", 1);
    puts("[storage-upstream] START");
#ifdef PROBE_POWER_COMMAND
    /* The inventory slot starts concurrently with init's ELF/runtime startup.
     * Exercise commands after init has installed its power-signal mask. */
    int init_ready = 0;
    unsigned long long wanted = (1ULL << (SIGUSR1 - 1)) |
                                (1ULL << (SIGUSR2 - 1)) |
                                (1ULL << (SIGTERM - 1));
    for (unsigned attempt = 0; attempt < 200 && !init_ready; ++attempt) {
        FILE *status = fopen("/proc/1/stat", "r");
        if (status) {
            char line[2048];
            if (fgets(line, sizeof(line), status)) {
                char *end = strrchr(line, ')'), *save = NULL;
                char *field = end ? strtok_r(end + 1, " ", &save) : NULL;
                for (unsigned number = 3; field && number < 32; ++number)
                    field = strtok_r(NULL, " ", &save);
                if (field && (strtoull(field, NULL, 10) & wanted) == wanted)
                    init_ready = 1;
            }
            fclose(status);
        }
        if (!init_ready) usleep(100000);
    }
    if (!init_ready) {
        check(0, "init power signal mask ready");
        return 1;
    }
    printf("[storage-upstream] upstream BusyBox %s request\n", PROBE_POWER_COMMAND);
    execl("/bin/busybox", "busybox", PROBE_POWER_COMMAND, (char *)NULL);
    perror("exec BusyBox power command");
    return 1;
#endif
    RUN("BusyBox pipeline", NULL, "3", "/bin/busybox", "sh", "-c", "printf abc | wc -c");
    RUN("file ELF and text without magic warnings", NULL, "FILE_OK", "/bin/sh", "-c",
        "set -e; printf 'Hello from LeonOS\\n' >/tmp/file-text.txt; "
        "/usr/bin/file /bin/busybox /tmp/file-text.txt >/tmp/file-result.txt 2>/tmp/file-errors.txt; "
        "cat /tmp/file-result.txt; cat /tmp/file-errors.txt; "
        "test ! -s /tmp/file-errors.txt; "
        "grep -q 'ELF 64-bit' /tmp/file-result.txt; "
        "grep -q 'ASCII text' /tmp/file-result.txt; echo FILE_OK");
    RUN("file missing input rejected", NULL, "FILE_MISSING_OK", "/bin/sh", "-c",
        "if /usr/bin/file -E /tmp/absent-file-input; then exit 1; "
        "else echo FILE_MISSING_OK; fi");
    RUN("boot payload copy", NULL, "COPY_OK", "/bin/sh", "-c",
        "set -e; s=/tmp/storage-copy-source; d='/tmp/storage copy target'; "
        "mkdir -p \"$s/EFI/BOOT\" \"$s/leonos\" \"$s/grub/fonts\" \"$d\"; "
        "for f in EFI/BOOT/BOOTX64.EFI loader.elf leonos/kernel.sys leonos/middlelayer.sys grub/fonts/test.pf2; "
        "do printf '%s' \"$f\" > \"$s/$f\"; done; "
        "/usr/sbin/leonos-grub-installer --source \"$s\" \"$d\"; "
        "for f in EFI/BOOT/BOOTX64.EFI loader.elf leonos/kernel.sys leonos/middlelayer.sys grub/fonts/test.pf2; "
        "do test \"$(cat \"$d/$f\")\" = \"$f\"; done; echo COPY_OK");
    RUN("boot missing payload rejected", NULL, "REJECT_OK", "/bin/sh", "-c",
        "if /usr/sbin/leonos-grub-installer --source /tmp/missing-boot-payload '/tmp/storage copy target'; "
        "then exit 1; else echo REJECT_OK; fi");
    RUN("fdisk version", NULL, "util-linux 2.41.6", "/usr/sbin/fdisk", "--version");
    RUN("mount version", NULL, "util-linux 2.41.6", "/bin/mount", "--version");
    RUN("mount listing", NULL, " on /", "/bin/mount");
    RUN("lsblk inventory", NULL, "blockdevices", "/bin/lsblk", "--json", "--output", "NAME,TYPE");
    RUN("fsck dispatcher", NULL, "fsck.ext2", "/usr/sbin/fsck", "-N", "-t", "ext2", "/tmp/storage-test.img");
    if (make_file("/tmp/storage-test.img", 64 << 20)) {
        RUN("fdisk GPT write", "g\nn\n1\n\n+16M\nw\n", NULL, "/usr/sbin/fdisk", "/tmp/storage-test.img");
        RUN("fdisk GPT read", NULL, "gpt", "/usr/sbin/fdisk", "-l", "/tmp/storage-test.img");
    }
    if (make_file("/tmp/storage-test.img", 64 << 20)) {
        RUN("mkfs ext2", NULL, NULL, "/usr/sbin/mkfs.ext2", "-F", "/tmp/storage-test.img");
        RUN("fsck ext2", NULL, NULL, "/usr/sbin/fsck.ext2", "-n", "/tmp/storage-test.img");
        RUN("blkid ext2", NULL, "ext2", "/usr/sbin/blkid", "-p", "-o", "value", "-s", "TYPE", "/tmp/storage-test.img");
    }
    if (make_file("/tmp/storage-test.img", 64 << 20)) {
        RUN("mkfs FAT32", NULL, NULL, "/usr/sbin/mkfs.fat", "-F", "32", "/tmp/storage-test.img");
        RUN("fsck FAT32", NULL, NULL, "/usr/sbin/fsck.fat", "-n", "/tmp/storage-test.img");
        RUN("blkid FAT32", NULL, "vfat", "/usr/sbin/blkid", "-p", "-o", "value", "-s", "TYPE", "/tmp/storage-test.img");
    }
    if (make_file("/tmp/storage-test.img", 64 << 20)) {
        RUN("mkfs exFAT", NULL, NULL, "/usr/sbin/mkfs.exfat", "/tmp/storage-test.img");
        RUN("fsck exFAT", NULL, NULL, "/usr/sbin/fsck.exfat", "-n", "/tmp/storage-test.img");
        RUN("blkid exFAT", NULL, "exfat", "/usr/sbin/blkid", "-p", "-o", "value", "-s", "TYPE", "/tmp/storage-test.img");
    }
    unlink("/tmp/storage-test.img");
    mkdir("/tmp/storage-mount", 0700);
    RUN("mount tmpfs", NULL, NULL, "/bin/mount", "-t", "tmpfs", "tmpfs", "/tmp/storage-mount");
    RUN("umount tmpfs", NULL, NULL, "/bin/umount", "/tmp/storage-mount");
    printf("[storage-upstream] DONE failures=%u\n", failures);
    return failures ? 1 : 0;
}
