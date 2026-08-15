#ifndef LEONOS_POSIX_H
#define LEONOS_POSIX_H

struct stat;

/*
 * LeonOS still exposes a compact native file-status syscall structure to
 * first-party system code. These adapters convert it to Picolibc's POSIX
 * struct stat and are the common targets used by third-party build ports.
 */
int leonos_posix_stat(const char *path, struct stat *status);
int leonos_posix_fstat(int fd, struct stat *status);
int leonos_posix_lstat(const char *path, struct stat *status);

#endif
