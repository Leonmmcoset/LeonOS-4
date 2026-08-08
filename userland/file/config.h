#ifndef LEONOS_FILE_CONFIG_H
#define LEONOS_FILE_CONFIG_H

/* Cross-configuration for libmagic on LeonOS's Picolibc userland. */
#define PACKAGE "file"
#define PACKAGE_NAME "file"
#define PACKAGE_TARNAME "file"
#define PACKAGE_VERSION "5.48"
#define VERSION "5.48"

#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_FCNTL_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_GETOPT_H 1
#define HAVE_STRUCT_OPTION 1
#define HAVE_LOCALE_H 1
#define HAVE_REGEX_H 1
#define HAVE_CTYPE_H 1

/* LeonOS has no native symlinks, pread, or mmap-backed file loading.
 * leonos_shim.c provides the safe stat and positional-read adapters. */
#define HAVE_PREAD 1

/* The unsupported process/spawn plumbing is guarded with numeric #if tests. */
#define HAVE_FORK 0

#define HAVE_VISIBILITY 1

#endif
