#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[random] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); ++failures; } } while (0)

int main(void)
{
    unsigned failures = 0;
    unsigned char blocks[3][256] = {{0}}, zero[256] = {0}, next[256];
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("[random] BEGIN entropy device and raw syscall contracts");
    for (unsigned i = 0; i < 2; ++i) {
        int fd = open(i ? "/dev/urandom" : "/dev/random", O_RDONLY | O_CLOEXEC);
        CHECK(fd >= 0);
        if (fd < 0) continue;
        CHECK(read(fd, blocks[i], sizeof(blocks[i])) == sizeof(blocks[i]));
        CHECK(read(fd, next, sizeof(next)) == sizeof(next));
        CHECK(memcmp(blocks[i], zero, sizeof(zero)) != 0);
        CHECK(memcmp(blocks[i], next, sizeof(next)) != 0);
        CHECK(close(fd) == 0);
    }
    CHECK(syscall(SYS_getrandom, blocks[2], sizeof(blocks[2]), 0) == sizeof(blocks[2]));
    CHECK(memcmp(blocks[2], zero, sizeof(zero)) != 0);
    CHECK(memcmp(blocks[0], blocks[1], sizeof(zero)) != 0);
    CHECK(memcmp(blocks[1], blocks[2], sizeof(zero)) != 0);
    CHECK(syscall(SYS_getrandom, NULL, 0, 0) == 0);
    CHECK(syscall(SYS_getrandom, next, sizeof(next), 6) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_getrandom, next, sizeof(next), 8) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_getrandom, next, sizeof(next), 1ULL << 32) == sizeof(next));
    void *readonly = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(readonly != MAP_FAILED);
    if (readonly != MAP_FAILED) {
        CHECK(syscall(SYS_getrandom, readonly, 8, 0) == -1 && errno == EFAULT);
        CHECK(munmap(readonly, 4096) == 0);
    }
    puts("[random] Byte checks detect zero/repeated data; they do not establish entropy quality");
    printf("[random] DONE failures=%u\n", failures);
    return failures != 0;
}
