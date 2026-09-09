#include "accounts.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int authd_username_valid(const char *name, unsigned capacity)
{
    if (!name || !capacity || !name[0]) return 0;
    for (unsigned i = 0; i < capacity; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (!ch) return strcmp(name, ".") && strcmp(name, "..");
        if (ch <= ' ' || ch == 127 || ch == ':' || ch == '/' || ch == '\\') return 0;
    }
    return 0;
}

static int account_valid(const struct leonos_user_info *user)
{
    if (!authd_username_valid(user->username, sizeof(user->username)) ||
        user->uid == UINT32_MAX || user->home[0] != '/') return 0;
    for (unsigned i = 0; i < sizeof(user->home); ++i) {
        unsigned char ch = (unsigned char)user->home[i];
        if (!ch) return 1;
        if (ch < ' ' || ch == 127 || ch == ':') return 0;
    }
    return 0;
}

static int write_all(int fd, const char *buffer, size_t length)
{
    while (length) {
        ssize_t n = write(fd, buffer, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { if (!n) errno = EIO; return -1; }
        buffer += n;
        length -= (size_t)n;
    }
    return 0;
}

int authd_export_accounts(const char *directory,
                          const struct leonos_auth_record *records, unsigned count)
{
    char target[2][256], temporary[2][256], line[256];
    const char *names[] = {"passwd", "group"};
    int descriptors[] = {-1, -1};
    if (!directory || count > LEONOS_AUTH_MAX_USERS || (count && !records)) {
        errno = EINVAL;
        return -1;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (!account_valid(&records[i].user)) { errno = EINVAL; return -1; }
        for (unsigned j = 0; j < i; ++j) {
            if (records[i].user.uid == records[j].user.uid ||
                !strcmp(records[i].user.username, records[j].user.username)) {
                errno = EINVAL;
                return -1;
            }
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        int n = snprintf(target[i], sizeof(target[i]), "%s/%s", directory, names[i]);
        int t = snprintf(temporary[i], sizeof(temporary[i]), "%s/.%s.authd.tmp", directory, names[i]);
        if (n < 0 || (size_t)n >= sizeof(target[i]) || t < 0 || (size_t)t >= sizeof(temporary[i])) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }
    if (mkdir(directory, 0755) < 0 && errno != EEXIST) return -1;
    for (unsigned i = 0; i < 2; ++i) {
        descriptors[i] = open(temporary[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (descriptors[i] < 0 || fchmod(descriptors[i], 0644) < 0) goto failed;
    }
    /* AUS1 remains authoritative. These files contain identity lookup data,
     * never password hashes; the primary group retains the existing UID. */
    for (unsigned i = 0; i < count; ++i) {
        const struct leonos_user_info *user = &records[i].user;
        int n = snprintf(line, sizeof(line), "%s:x:%u:%u::%s:/bin/sh\n",
                         user->username, user->uid, user->uid, user->home);
        if (n < 0 || (size_t)n >= sizeof(line)) { errno = EOVERFLOW; goto failed; }
        if (write_all(descriptors[0], line, (size_t)n) < 0) goto failed;
        n = snprintf(line, sizeof(line), "%s:x:%u:\n", user->username, user->uid);
        if (n < 0 || (size_t)n >= sizeof(line)) { errno = EOVERFLOW; goto failed; }
        if (write_all(descriptors[1], line, (size_t)n) < 0) goto failed;
    }
    for (unsigned i = 0; i < 2; ++i) {
        int ret = close(descriptors[i]);
        descriptors[i] = -1;
        if (ret < 0) goto failed;
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (rename(temporary[i], target[i]) < 0) goto failed;
    }
    return 0;

failed:;
    int error = errno;
    for (unsigned i = 0; i < 2; ++i) {
        if (descriptors[i] >= 0) close(descriptors[i]);
        unlink(temporary[i]);
    }
    errno = error;
    return -1;
}
