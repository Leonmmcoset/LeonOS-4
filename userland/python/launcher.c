/* Relocatable Python launcher.
 *
 * Guest layout: /usr/bin/python3.14 (python/python3 are relative symlinks)
 * with the complete CPython tree under /opt/python.  The same derivation
 * keeps host package-tree tests working without a guest root.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

int main(int argc, char **argv)
{
    (void)argc;
    char exe[4096];
    char prefix[4096];
    char binary[4096];
    ssize_t length = readlink("/proc/self/exe", exe, sizeof(exe) - 1U);
    if (length < 0 || length >= (ssize_t)sizeof(exe) - 1U) {
        perror("python launcher: /proc/self/exe");
        return 126;
    }
    exe[length] = 0;

    static const char suffix[] = "/usr/bin/python3.14";
    size_t exe_length = strlen(exe);
    if (exe_length >= sizeof(suffix) - 1U &&
        strcmp(exe + exe_length - (sizeof(suffix) - 1U), suffix) == 0) {
        exe[exe_length - (sizeof(suffix) - 1U)] = 0;
    } else {
        fputs("python launcher: unexpected executable path\n", stderr);
        return 126;
    }
    if (snprintf(prefix, sizeof(prefix), "%s/opt/python", exe) >= (int)sizeof(prefix) ||
        snprintf(binary, sizeof(binary), "%s/bin/python3.14", prefix) >= (int)sizeof(binary)) {
        fputs("python launcher: path too long\n", stderr);
        return 126;
    }
    if (setenv("PYTHONHOME", prefix, 0) != 0) {
        perror("python launcher: PYTHONHOME");
        return 126;
    }
    argv[0] = binary;
    execve(binary, argv, environ);
    int error = errno;
    fprintf(stderr, "%s: %s\n", binary, strerror(error));
    return error == ENOENT ? 127 : 126;
}
