#include "accounts.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <sys/random.h>

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

int authd_account_valid(const struct leonos_user_info *user)
{
    if (!authd_username_valid(user->username, sizeof(user->username)) ||
        user->uid >= 65534 || !strcmp(user->username, "nobody") ||
        user->home[0] != '/' || !memchr(user->home, 0, sizeof(user->home))) return 0;
    if (user->uid == 0) {
        if (strcmp(user->username, "root") || user->role != 2 ||
            strcmp(user->home, "/root")) return 0;
    } else if (!strcmp(user->username, "root") || user->role != 1) return 0;
    for (unsigned i = 0; i < sizeof(user->home); ++i) {
        unsigned char ch = (unsigned char)user->home[i];
        if (!ch) return 1;
        if (ch < ' ' || ch == 127 || ch == ':') return 0;
    }
    return 0;
}

static int write_all(int fd, const void *data, size_t length)
{
    const char *buffer = data;
    while (length) {
        ssize_t n = write(fd, buffer, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { if (!n) errno = EIO; return -1; }
        buffer += n;
        length -= (size_t)n;
    }
    return 0;
}

static int password_derive(const char *password, const unsigned char salt[16], unsigned char out[32])
{
    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    int error = mbedtls_md_setup(&context, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    if (!error) error = mbedtls_pkcs5_pbkdf2_hmac(&context, (const unsigned char *)password,
                       strlen(password), salt, 16, 100000, 32, out);
    mbedtls_md_free(&context);
    if (error) { errno = EIO; return -1; }
    return 0;
}

int authd_set_password(struct leonos_auth_record *record, const char *password)
{
    if (!leonos_auth_password_valid(password, LEONOS_AUTH_PASSWORD_LEN)) { errno = EINVAL; return -1; }
    static const char hex[] = "0123456789abcdef";
    unsigned char random[16], hash[32];
    size_t done = 0;
    while (done < sizeof(random)) {
        ssize_t n = getrandom(random + done, sizeof(random) - done, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { if (!n) errno = EIO; return -1; }
        done += (size_t)n;
    }
    if (password_derive(password, random, hash) < 0) return -1;
    memset(record->password_hash, 0, sizeof(record->password_hash));
    char *out = (char *)record->password_hash;
    strcpy(out, "$pbkdf2-sha256$100000$");
    out += strlen(out);
    for (unsigned i = 0; i < 16; ++i) { *out++ = hex[random[i] >> 4]; *out++ = hex[random[i] & 15]; }
    *out++ = '$';
    for (unsigned i = 0; i < 32; ++i) { *out++ = hex[hash[i] >> 4]; *out++ = hex[hash[i] & 15]; }
    explicit_bzero(hash, sizeof(hash));
    return 0;
}

int authd_check_password(const struct leonos_auth_record *record, const char *password)
{
    if (!leonos_auth_password_valid(password, LEONOS_AUTH_PASSWORD_LEN)) return 0;
    const char *stored = (const char *)record->password_hash;
    static const char prefix[] = "$pbkdf2-sha256$100000$";
    static const char hex[] = "0123456789abcdef";
    const size_t prefix_len = sizeof(prefix) - 1;
    if (!memchr(stored, 0, sizeof(record->password_hash)) ||
        strncmp(stored, prefix, prefix_len) || strlen(stored) != prefix_len + 97 ||
        stored[prefix_len + 32] != '$') return 0;
    unsigned char decoded[48], hash[32];
    for (unsigned i = 0; i < 48; ++i) {
        size_t pos = prefix_len + i * 2 + (i >= 16);
        const char *a = strchr(hex, stored[pos]), *b = strchr(hex, stored[pos + 1]);
        if (!a || !b) return 0;
        decoded[i] = (unsigned char)((a - hex) * 16 + (b - hex));
    }
    if (password_derive(password, decoded, hash) < 0) return 0;
    unsigned difference = 0;
    for (unsigned i = 0; i < 32; ++i) difference |= hash[i] ^ decoded[16 + i];
    explicit_bzero(hash, sizeof(hash));
    return !difference;
}

int authd_store_database(const char *path, const struct leonos_auth_record *records,
                         unsigned count)
{
    char temporary[512];
    uint32_t header[] = {LEONOS_AUTH_DB_MAGIC, count};
    if (count > LEONOS_AUTH_MAX_USERS || (count && !records)) { errno = EINVAL; return -1; }
    int n = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(temporary)) { errno = ENAMETOOLONG; return -1; }
    int fd = mkstemp(temporary);
    if (fd < 0) return -1;
    if (fchmod(fd, 0600) < 0 || write_all(fd, header, sizeof(header)) < 0 ||
        write_all(fd, records, count * sizeof(*records)) < 0 || fsync(fd) < 0) goto failed;
    if (close(fd) < 0) { fd = -1; goto failed; }
    fd = -1;
    if (rename(temporary, path) < 0) goto failed;
    return 0;
failed:;
    int error = errno;
    if (fd >= 0) close(fd);
    unlink(temporary);
    errno = error;
    return -1;
}

int authd_publish_session(const char *path, const struct leonos_user_info *user)
{
    if (!user) return unlink(path) == 0 || errno == ENOENT ? 0 : -1;
    char temporary[512], text[256];
    int n = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(temporary)) { errno = ENAMETOOLONG; return -1; }
    n = snprintf(text, sizeof(text), "%u\n%u\n%s\n%s\n", user->uid, user->role,
                 user->username, user->home);
    if (n < 0 || (size_t)n >= sizeof(text)) { errno = EOVERFLOW; return -1; }
    int fd = mkstemp(temporary);
    if (fd < 0) return -1;
    if (fchown(fd, 0, 0) < 0 || fchmod(fd, 0644) < 0 || write_all(fd, text, n) < 0 ||
        fsync(fd) < 0) goto failed;
    if (close(fd) < 0) { fd = -1; goto failed; }
    fd = -1;
    if (rename(temporary, path) < 0) goto failed;
    return 0;
failed:;
    int error = errno;
    if (fd >= 0) close(fd);
    unlink(temporary);
    errno = error;
    return -1;
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
        if (!authd_account_valid(&records[i].user)) { errno = EINVAL; return -1; }
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
        int t = snprintf(temporary[i], sizeof(temporary[i]), "%s/.%s.XXXXXX", directory, names[i]);
        if (n < 0 || (size_t)n >= sizeof(target[i]) || t < 0 || (size_t)t >= sizeof(temporary[i])) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }
    if (mkdir(directory, 0755) < 0 && errno != EEXIST) return -1;
    for (unsigned i = 0; i < 2; ++i) {
        descriptors[i] = mkstemp(temporary[i]);
        if (descriptors[i] < 0 || fchmod(descriptors[i], 0644) < 0) goto failed;
    }
    /* Live media has no login records; its root lookup entry stays locked. */
    int has_root = 0;
    for (unsigned i = 0; i < count; ++i) has_root |= records[i].user.uid == 0;
    const char *root_entry = has_root ? "root:x:0:0::/root:/bin/sh\n" : "root:!:0:0:root:/root:/bin/sh\n";
    static const char base_passwd[] = "nobody:!:65534:65534:nobody:/:/bin/false\n";
    static const char base_group[] = "root:x:0:\nnobody:x:65534:\n";
    if (write_all(descriptors[0], root_entry, strlen(root_entry)) < 0 ||
        write_all(descriptors[0], base_passwd, sizeof(base_passwd) - 1) < 0 ||
        write_all(descriptors[1], base_group, sizeof(base_group) - 1) < 0) goto failed;
    /* AUS2 remains authoritative. These files contain identity lookup data,
     * never password hashes; the primary group retains the existing UID. */
    for (unsigned i = 0; i < count; ++i) {
        const struct leonos_user_info *user = &records[i].user;
        if (!user->uid) continue;
        int n = snprintf(line, sizeof(line), "%s:x:%u:%u::%s:/bin/sh\n",
                         user->username, user->uid, user->uid, user->home);
        if (n < 0 || (size_t)n >= sizeof(line)) { errno = EOVERFLOW; goto failed; }
        if (write_all(descriptors[0], line, (size_t)n) < 0) goto failed;
        n = snprintf(line, sizeof(line), "%s:x:%u:\n", user->username, user->uid);
        if (n < 0 || (size_t)n >= sizeof(line)) { errno = EOVERFLOW; goto failed; }
        if (write_all(descriptors[1], line, (size_t)n) < 0) goto failed;
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (fsync(descriptors[i]) < 0) goto failed;
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
