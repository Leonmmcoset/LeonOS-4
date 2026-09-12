#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[inode-lifetime] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
static const char *self;
static int held_unlink(void)
{
    int fd = open("/tmp/held-inode", O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0 && write(fd, "old", 3) == 3);
    struct stat before, after;
    CHECK(fstat(fd, &before) == 0 && unlink("/tmp/held-inode") == 0);
    int replacement = open("/tmp/held-inode", O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(replacement >= 0 && write(replacement, "new", 3) == 3);
    CHECK(fstat(fd, &after) == 0 && after.st_ino == before.st_ino && after.st_nlink == 0);
    char data[8] = {0};
    CHECK(pread(fd, data, 3, 0) == 3 && !memcmp(data, "old", 3));
    CHECK(pwrite(fd, "held", 4, 0) == 4 && ftruncate(fd, 2) == 0);
    CHECK(pread(fd, data, sizeof(data), 0) == 2 && !memcmp(data, "he", 2));
    CHECK(pread(replacement, data, sizeof(data), 0) == 3 && !memcmp(data, "new", 3));
    CHECK(close(fd) == 0 && close(replacement) == 0 && unlink("/tmp/held-inode") == 0);
    return 0;
}
static int held_replace(void)
{
    int old = open("/tmp/held-old", O_CREAT | O_EXCL | O_RDWR, 0600);
    int newer = open("/tmp/held-new", O_CREAT | O_EXCL | O_RDWR, 0644);
    CHECK(old >= 0 && newer >= 0 && write(old, "old", 3) == 3 && write(newer, "new", 3) == 3);
    CHECK(rename("/tmp/held-new", "/tmp/held-old") == 0 && fchmod(old, 0400) == 0);
    struct stat st;
    CHECK(fstat(old, &st) == 0 && st.st_nlink == 0 && (st.st_mode & 0777) == 0400);
    CHECK(stat("/tmp/held-old", &st) == 0 && (st.st_mode & 0777) == 0644);
    char data[4];
    CHECK(pread(old, data, 3, 0) == 3 && !memcmp(data, "old", 3));
    CHECK(close(old) == 0 && close(newer) == 0 && unlink("/tmp/held-old") == 0);
    return 0;
}
static int lazy_mapping(void)
{
    char page[4096];
    memset(page, 'A', sizeof(page));
    int fd = open("/tmp/held-map", O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0);
    for (unsigned i = 0; i < 3; ++i) CHECK(write(fd, page, sizeof(page)) == sizeof(page));
    char *mapping = mmap(NULL, 3 * sizeof(page), PROT_READ, MAP_PRIVATE, fd, 0);
    CHECK(mapping != MAP_FAILED && close(fd) == 0 && unlink("/tmp/held-map") == 0);
    memset(page, 'B', sizeof(page));
    fd = open("/tmp/held-map", O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0);
    for (unsigned i = 0; i < 3; ++i) CHECK(write(fd, page, sizeof(page)) == sizeof(page));
    CHECK(mprotect(mapping + sizeof(page), sizeof(page), PROT_READ | PROT_WRITE) == 0);
    CHECK(munmap(mapping + sizeof(page), sizeof(page)) == 0);
    CHECK(mapping[0] == 'A' && mapping[3 * sizeof(page) - 1] == 'A');
    CHECK(munmap(mapping, sizeof(page)) == 0 && munmap(mapping + 2 * sizeof(page), sizeof(page)) == 0);
    CHECK(close(fd) == 0 && unlink("/tmp/held-map") == 0);
    return 0;
}
static int executable_write_access(void)
{
    /* Linux v6.12 removed deny_write_access from executable file lifetime. */
    int fd = open(self, O_WRONLY);
    CHECK(fd >= 0 && close(fd) == 0);
    return 0;
}
int main(int argc, char **argv)
{
    (void)argc;
    self = argv[0];
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("[inode-lifetime] BEGIN open, unlink, rename, mmap and executable write access");
    int failures = 0;
    int (*cases[])(void) = {held_unlink, held_replace, lazy_mapping, executable_write_access};
    const char *names[] = {"held unlink", "held replacement", "lazy mapping", "v6.12 executable write access"};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        pid_t child = fork();
        if (!child) _exit(cases[i]());
        int status;
        int ok = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status);
        printf("[inode-lifetime] %s %s\n", ok ? "PASS" : "FAIL", names[i]);
        failures += !ok;
    }
    printf("[inode-lifetime] DONE failures=%d\n", failures);
    return failures != 0;
}
