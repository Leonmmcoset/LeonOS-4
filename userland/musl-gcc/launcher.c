/* Relocatable command aliases; the upstream compiler and binutils stay intact. */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

int main(int argc, char **argv)
{
    char root[PATH_MAX], program[PATH_MAX], sysroot[PATH_MAX];
    const char *name = strrchr(argv[0], '/');
    name = name ? name + 1 : argv[0];
    if (!strncmp(name, "x86_64-linux-musl-", 18)) name += 18;
    if (!strcmp(name, "musl-gcc") || !strcmp(name, "cc") ||
        !strcmp(name, "leonos-musl-cc")) name = "gcc";
    if (!strcmp(name, "musl-g++")) name = "g++";
    ssize_t size = readlink("/proc/self/exe", root, sizeof(root) - 1);
    if (size < 0 || size >= (ssize_t)sizeof(root) - 1) {
        perror("musl toolchain: /proc/self/exe");
        return 126;
    }
    root[size] = 0;
    char *bin = strrchr(root, '/');
    if (!bin) return 126;
    *bin = 0;
    bin = strrchr(root, '/');
    if (!bin || strcmp(bin, "/bin")) return 126;
    *bin = 0;
    if (snprintf(program, sizeof(program), "%s/gcc-musl/bin/x86_64-linux-musl-%s",
                 root, name) >= (int)sizeof(program) ||
        snprintf(sysroot, sizeof(sysroot), "--sysroot=%s/gcc-musl/x86_64-linux-musl",
                 root) >= (int)sizeof(sysroot)) return 126;
    int use_sysroot = !strcmp(name, "gcc") || !strcmp(name, "gcc-15.1.0") ||
        !strcmp(name, "g++") || !strcmp(name, "c++") || !strcmp(name, "cpp") ||
        !strcmp(name, "ld") || !strcmp(name, "ld.bfd");
    char **args = calloc((size_t)argc + 2, sizeof(*args));
    if (!args) { perror("musl toolchain"); return 126; }
    args[0] = program;
    int next = 1;
    if (use_sysroot) args[next++] = sysroot;
    for (int i = 1; i < argc; ++i) args[next++] = argv[i];
    execve(program, args, environ);
    int error = errno;
    fprintf(stderr, "%s: %s\n", program, strerror(error));
    free(args);
    return error == ENOENT ? 127 : 126;
}
