#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <mimalloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

static _Thread_local unsigned tls_value = 73;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s (errno %d)\n", \
    __FILE__, __LINE__, #c, errno); return 1; } } while (0)

static void *thread_probe(void *unused)
{
    (void)unused;
    if (tls_value != 73) return (void *)1;
    tls_value = 19;
    for (size_t i = 1; i < 4096; i += 7) {
        void *p = calloc(i, 1);
        if (!p || !mi_is_in_heap_region(p)) return (void *)2;
        free(p);
    }
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(argc >= 1 && argv[argc] == NULL && argv[0][0]);
    CHECK(getauxval(AT_PAGESZ) == 4096 && getauxval(AT_PHDR) != 0);
    CHECK(getauxval(AT_PHNUM) != 0 && getauxval(AT_RANDOM) != 0);
    CHECK(tls_value == 73);
    tls_value = 91;
    char *s = strdup("musl allocation crossing into mimalloc free");
    CHECK(s && mi_is_in_heap_region(s));
    CHECK(malloc_usable_size(s) >= strlen(s) + 1);
    s = realloc(s, 32768);
    CHECK(s && strcmp(s, "musl allocation crossing into mimalloc free") == 0);
    free(s);
    void *aligned = NULL;
    CHECK(posix_memalign(&aligned, 4096, 8192) == 0);
    CHECK(((uintptr_t)aligned & 4095) == 0 && mi_is_in_heap_region(aligned));
    free(aligned);
    unsigned char *zero = calloc(512, 1);
    CHECK(zero);
    for (int i = 0; i < 512; ++i) CHECK(zero[i] == 0);
    free(zero);
    char *formatted = NULL;
    CHECK(asprintf(&formatted, "%.3f", 1.25) == 5);
    CHECK(strcmp(formatted, "1.250") == 0 && mi_is_in_heap_region(formatted));
    free(formatted);
    struct stat st;
    CHECK(fstat(STDOUT_FILENO, &st) == 0);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
    CHECK((syscall(SYS_fcntl, fd, F_GETFL) & 0x800) != 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    CHECK(bind(fd, (struct sockaddr *)&addr, sizeof(sa_family_t)) == 0);
    CHECK(listen(fd, 1) == 0);
    CHECK(accept(fd, NULL, NULL) == -1 && errno == EAGAIN);
    CHECK(close(fd) == 0);
    pthread_t thread;
    void *result = (void *)3;
    CHECK(pthread_create(&thread, NULL, thread_probe, NULL) == 0);
    CHECK(pthread_join(thread, &result) == 0 && result == 0);
    CHECK(tls_value == 91);
    puts("PASS musl+mimalloc: startup auxv TLS pthread heap stdio stat nonblocking socket");
    return 0;
}
