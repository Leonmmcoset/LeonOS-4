#ifndef LEONOS_POSIX_H
#define LEONOS_POSIX_H

struct stat;

/* Deprecated port aliases. Standard applications should call stat/fstat/lstat
 * from <sys/stat.h>; these names remain for older standalone ports. */
int leonos_posix_stat(const char *path, struct stat *status);
int leonos_posix_fstat(int fd, struct stat *status);
int leonos_posix_lstat(const char *path, struct stat *status);

#endif
