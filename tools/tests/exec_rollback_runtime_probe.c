#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[exec-rollback] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
static atomic_int ticks, stop;
static const char *self;
static void *worker(void *unused)
{
    (void)unused;
    while (!atomic_load(&stop)) {
        atomic_fetch_add(&ticks, 1);
        usleep(1000);
    }
    return NULL;
}

static int failure_case(void)
{
    const char *path = "/tmp/exec-invalid";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    CHECK(fd >= 0 && write(fd, "not an executable\n", 18) == 18 && close(fd) == 0);
    int preserved = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(preserved >= 0);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);
    while (!atomic_load(&ticks)) usleep(1000);
    int before = atomic_load(&ticks);
    char *args[] = {(char *)path, NULL};
    for (unsigned i = 0; i < 16; ++i) {
        CHECK(syscall(SYS_execve, path, args, NULL) == -1 && errno == ENOEXEC);
        CHECK(getuid() == 0 && geteuid() == 0 && fcntl(preserved, F_GETFD) == FD_CLOEXEC);
    }
    for (unsigned i = 0; i < 1000 && atomic_load(&ticks) == before; ++i) usleep(1000);
    CHECK(atomic_load(&ticks) > before);
    atomic_store(&stop, 1);
    CHECK(pthread_join(thread, NULL) == 0 && close(preserved) == 0 && unlink(path) == 0);
    return 0;
}

static int valid_case(void)
{
    int fd = open(self, O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    char number[32];
    snprintf(number, sizeof(number), "%d", fd);
    char *args[] = {(char *)self, "--replaced", number, NULL};
    CHECK(setenv("EXEC_ROLLBACK_VALUE", "preserved", 1) == 0);
    execv(self, args);
    return 1;
}

static int interpreter_failures(void)
{
    unsigned char image[4096] = {0};
    const char *path = "/tmp/exec-interp-main";
    const char *interpreter = "/tmp/exec-test-interpreter";
    Elf64_Ehdr header = {.e_type = ET_DYN, .e_machine = EM_X86_64, .e_version = EV_CURRENT,
        .e_entry = 0x200, .e_phoff = sizeof(Elf64_Ehdr), .e_ehsize = sizeof(Elf64_Ehdr),
        .e_phentsize = sizeof(Elf64_Phdr), .e_phnum = 3};
    memcpy(header.e_ident, ELFMAG, SELFMAG);
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    Elf64_Phdr headers[] = {
        {.p_type = PT_LOAD, .p_flags = PF_R | PF_X, .p_filesz = sizeof(image),
         .p_memsz = sizeof(image), .p_align = 4096},
        {.p_type = PT_INTERP, .p_offset = 0x180, .p_filesz = strlen(interpreter) + 1},
        {.p_type = PT_DYNAMIC, .p_offset = 0x1c0, .p_vaddr = 0x1c0, .p_filesz = sizeof(Elf64_Dyn)},
    };
    memcpy(image, &header, sizeof(header));
    memcpy(image + header.e_phoff, headers, sizeof(headers));
    memcpy(image + 0x180, interpreter, strlen(interpreter) + 1);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0755);
    CHECK(fd >= 0 && write(fd, image, sizeof(image)) == sizeof(image) && close(fd) == 0);
    char *args[] = {(char *)path, NULL};
    CHECK(syscall(SYS_execve, path, args, NULL) == -1 && errno == ENOENT);
    fd = open(interpreter, O_WRONLY | O_CREAT | O_EXCL, 0755);
    CHECK(fd >= 0 && write(fd, image, 64) == 64 && close(fd) == 0);
    CHECK(syscall(SYS_execve, path, args, NULL) == -1 && errno == ELIBBAD);
    CHECK(chmod(interpreter, 0644) == 0);
    CHECK(syscall(SYS_execve, path, args, NULL) == -1 && errno == EACCES);
    CHECK(unlink(interpreter) == 0 && unlink(path) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    self = argv[0];
    if (argc == 3 && !strcmp(argv[1], "--replaced")) {
        CHECK(fcntl(atoi(argv[2]), F_GETFD) == -1 && errno == EBADF);
        CHECK(getenv("EXEC_ROLLBACK_VALUE") && !strcmp(getenv("EXEC_ROLLBACK_VALUE"), "preserved"));
        return 0;
    }
    puts("[exec-rollback] BEGIN failed ELF leaves threads, credentials and CLOEXEC descriptors intact");
    int failures = 0;
    int (*cases[])(void) = {failure_case, valid_case, interpreter_failures};
    const char *names[] = {"failure rollback", "valid exec", "interpreter failures"};
    for (unsigned i = 0; i < 3; ++i) {
        pid_t child = fork();
        if (!child) _exit(cases[i]());
        int status;
        int ok = child > 0 && waitpid(child, &status, 0) == child &&
                 WIFEXITED(status) && WEXITSTATUS(status) == 0;
        printf("[exec-rollback] %s %s\n", ok ? "PASS" : "FAIL", names[i]);
        failures += !ok;
    }
    printf("[exec-rollback] DONE failures=%d\n", failures);
    return failures != 0;
}
