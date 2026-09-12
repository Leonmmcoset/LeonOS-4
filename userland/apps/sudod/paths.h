#ifndef LEONOS_FILEOP_PATHS_H
#define LEONOS_FILEOP_PATHS_H
#include <leonos/sudo.h>
#include <string.h>
#include <sys/stat.h>
#define SUDO_FILEOP_LIST LEONOS_FILEOP_LIST
#define SUDO_FILEOP_MKDIR LEONOS_FILEOP_MKDIR
#define SUDO_FILEOP_RENAME LEONOS_FILEOP_RENAME
#define SUDO_FILEOP_UNLINK LEONOS_FILEOP_UNLINK
#define SUDO_DELETE_CONFIRM LEONOS_FILEOP_CONFIRM
/* Reject any path the privileged worker must never touch. ".." is refused
 * outright so the worker never relies on the kernel folding it after the
 * check, and the kernel-managed trees are refused by path prefix (a plain
 * substring match would wrongly reject "/process"). */
static inline int sudo_path_allowed(const char *path)
{
    static const char *const denied[] = {"/proc", "/sys", "/dev"};
    if (!path || path[0] != '/') {
        return 0;
    }
    for (const char *scan = path; *scan; ++scan) {
        /* Require canonical spelling; do not let redundant components bypass
         * the protected-tree check below. The worker also resolves by dirfd. */
        if (scan[0] == '/' && (scan[1] == '/' ||
            (scan[1] == '.' && (scan[2] == '/' || scan[2] == 0)))) {
            return 0;
        }
        if (scan[0] == '.' && scan[1] == '.' &&
            (scan == path || scan[-1] == '/') &&
            (scan[2] == 0 || scan[2] == '/')) {
            return 0;
        }
    }
    for (unsigned i = 0; i < sizeof(denied) / sizeof(denied[0]); ++i) {
        uint32_t length = 0;
        while (denied[i][length]) {
            ++length;
        }
        /* strncmp bounds the comparison at the prefix, so a short path can
         * never read past its own terminator. */
        if (strncmp(path, denied[i], length) != 0) {
            continue;
        }
        if (path[length] == 0 || path[length] == '/') {
            return 0;
        }
    }
    return 1;
}

/* Validate one privileged file operation without touching the filesystem.
 * The op values are the LEONOS_FILEOP_* wire constants. */
static inline int sudo_fileop_check(uint32_t op, const char *path1,
                                    const char *path2)
{
    switch (op) {
    case SUDO_FILEOP_LIST:
        return sudo_path_allowed(path1) ? 0 : -1;
    case SUDO_FILEOP_MKDIR:
        if (!sudo_path_allowed(path1) || !path2 || !path2[0]) {
            return -1;
        }
        if (path2[0] == '/' || strchr(path2, '/')) {
            return -1;
        }
        if (strcmp(path2, ".") == 0 || strcmp(path2, "..") == 0) {
            return -1;
        }
        return 0;
    case SUDO_FILEOP_RENAME:
        return sudo_path_allowed(path1) && sudo_path_allowed(path2) ? 0 : -1;
    case SUDO_FILEOP_UNLINK:
        if (!sudo_path_allowed(path1)) {
            return -1;
        }
        if (path2 && path2[0] && strcmp(path2, SUDO_DELETE_CONFIRM) != 0) {
            return -1;
        }
        return 0;
    default:
        return -1;
    }
}

typedef int (*sudo_stat_fn)(const char *path, struct stat *st, void *context);

/* Decide whether an UNLINK needs the confirmation word.
 *
 * The directory type comes from the worker's own stat, not from the client:
 * if the client could assert "this is a file", one click would silently become
 * a recursive delete. A target that cannot be inspected still demands
 * confirmation, so the answer never depends on the target being observable. */
static inline int sudo_delete_needs_confirm(uint32_t op, const char *path,
                                            sudo_stat_fn stat_fn, void *context)
{
    struct stat st;
    if (op != SUDO_FILEOP_UNLINK) {
        return 0;
    }
    if (!stat_fn || !path) {
        return 1;
    }
    if (stat_fn(path, &st, context) < 0) {
        return 1;
    }
    return (st.st_mode & S_IFMT) == S_IFDIR ? 1 : 0;
}

#endif
