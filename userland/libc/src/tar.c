#include <leonos/fs.h>
#include <leonos/syscall.h>
#include <leonos/tar.h>
#include <stdint.h>
#include <string.h>

#define LEONOS_TAR_HEADER_NAME 0U
#define LEONOS_TAR_HEADER_MODE 100U
#define LEONOS_TAR_HEADER_SIZE 124U
#define LEONOS_TAR_HEADER_CHECKSUM 148U
#define LEONOS_TAR_HEADER_TYPEFLAG 156U
#define LEONOS_TAR_HEADER_END 512U

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
};

static uint32_t tar_compute_checksum(const unsigned char *header);

static uint32_t tar_field_len(const char *field, uint32_t capacity)
{
    uint32_t len = 0;
    if (!field) {
        return 0;
    }
    while (len < capacity && field[len]) {
        ++len;
    }
    return len;
}

static int tar_octal_to_uint_checked(const char *octal, uint32_t len, uint32_t *out)
{
    uint64_t value = 0;
    uint32_t seen = 0;
    uint32_t i;
    if (!octal || !out) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)octal[i];
        if (ch == 0) {
            break;
        }
        if (ch == ' ') {
            if (seen) {
                break;
            }
            continue;
        }
        if (ch < '0' || ch > '7') {
            return 0;
        }
        seen = 1;
        value = (value << 3) | (uint64_t)(ch - '0');
        if (value > 0xffffffffULL) {
            return 0;
        }
    }
    if (!seen) {
        *out = 0;
        return 1;
    }
    *out = (uint32_t)value;
    return 1;
}

static int tar_header_name_copy(const struct tar_header *hdr, char *out, uint32_t capacity)
{
    uint32_t name_len;
    uint32_t prefix_len;
    if (!hdr || !out || capacity == 0) {
        return 0;
    }
    name_len = tar_field_len(hdr->name, sizeof(hdr->name));
    prefix_len = tar_field_len(hdr->prefix, sizeof(hdr->prefix));
    if (name_len == 0) {
        return 0;
    }
    if (prefix_len) {
        if (prefix_len + 1U + name_len >= capacity) {
            return 0;
        }
        memcpy(out, hdr->prefix, prefix_len);
        out[prefix_len] = '/';
        memcpy(out + prefix_len + 1U, hdr->name, name_len);
        out[prefix_len + 1U + name_len] = 0;
        return 1;
    }
    if (name_len >= capacity) {
        return 0;
    }
    memcpy(out, hdr->name, name_len);
    out[name_len] = 0;
    return 1;
}

static int tar_header_is_zero(const unsigned char *header)
{
    uint32_t i;
    if (!header) {
        return 1;
    }
    for (i = 0; i < LEONOS_TAR_BLOCK_SIZE; ++i) {
        if (header[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void tar_strip_trailing_slashes(char *path)
{
    uint32_t len;
    if (!path) {
        return;
    }
    len = (uint32_t)strlen(path);
    while (len > 0 && path[len - 1U] == '/') {
        path[--len] = 0;
    }
}

static int tar_member_name_is_safe(const char *name)
{
    uint32_t i = 0;
    uint32_t part_start = 0;
    uint32_t part_len = 0;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') {
        return 0;
    }
    while (name[i]) {
        char ch = name[i];
        if (ch == ':' || ch == '\\' || (unsigned char)ch < 0x20) {
            return 0;
        }
        if (ch == '/') {
            if (part_len == 0) {
                return 0;
            }
            if ((part_len == 1 && name[part_start] == '.') ||
                (part_len == 2 && name[part_start] == '.' && name[part_start + 1U] == '.')) {
                return 0;
            }
            part_start = i + 1U;
            part_len = 0;
        } else {
            ++part_len;
        }
        ++i;
    }
    if (part_len == 0) {
        return 0;
    }
    if ((part_len == 1 && name[part_start] == '.') ||
        (part_len == 2 && name[part_start] == '.' && name[part_start + 1U] == '.')) {
        return 0;
    }
    return 1;
}

static int tar_is_root_path(const char *path)
{
    return path && path[0] == '0' && path[1] == ':' && path[2] == '/' && path[3] == 0;
}

static int tar_parent_path(const char *path, char *parent, uint32_t capacity)
{
    uint32_t len;
    uint32_t slash;
    if (!path || !path[0] || !parent || capacity == 0) {
        return 0;
    }
    len = (uint32_t)strlen(path);
    while (len > 0 && path[len - 1U] == '/') {
        --len;
    }
    if (len == 0) {
        return 0;
    }
    slash = len;
    while (slash > 0 && path[slash - 1U] != '/') {
        --slash;
    }
    if (slash == 0) {
        return 0;
    }
    if (slash >= capacity) {
        return 0;
    }
    memcpy(parent, path, slash);
    parent[slash] = 0;
    return 1;
}

static int tar_ensure_dir(const char *path)
{
    struct leonos_stat st;
    char parent[LEONOS_FS_PATH_LEN];
    if (!path || !path[0]) {
        return 0;
    }
    if (tar_is_root_path(path)) {
        return 1;
    }
    if (stat(path, &st) == 0) {
        return st.type == LEONOS_FS_TYPE_DIR ? 1 : 0;
    }
    if (tar_parent_path(path, parent, sizeof(parent))) {
        if (!tar_ensure_dir(parent)) {
            return 0;
        }
    }
    if (mkdir(path, 0) == 0) {
        return 1;
    }
    return stat(path, &st) == 0 && st.type == LEONOS_FS_TYPE_DIR;
}

static int tar_ensure_parent_dir(const char *path)
{
    char parent[LEONOS_FS_PATH_LEN];
    if (!tar_parent_path(path, parent, sizeof(parent))) {
        return 1;
    }
    return tar_ensure_dir(parent);
}

static int tar_join_path(char *out, uint32_t capacity, const char *base,
                         const char *name)
{
    uint32_t base_len;
    uint32_t name_len;
    if (!out || capacity == 0 || !base || !name) {
        return 0;
    }
    base_len = (uint32_t)strlen(base);
    name_len = (uint32_t)strlen(name);
    if (base_len == 0 || base_len + 1U + name_len >= capacity) {
        return 0;
    }
    memcpy(out, base, base_len);
    if (out[base_len - 1U] != '/') {
        out[base_len++] = '/';
    }
    if (base_len + name_len >= capacity) {
        return 0;
    }
    memcpy(out + base_len, name, name_len);
    out[base_len + name_len] = 0;
    return 1;
}

static int tar_header_parse(const unsigned char *raw, char *name, uint32_t name_cap,
                            uint32_t *size, char *typeflag)
{
    const struct tar_header *hdr = (const struct tar_header *)raw;
    uint32_t stored_checksum;
    uint32_t actual_checksum;
    if (!raw || !name || !size || !typeflag) {
        return 0;
    }
    if (!tar_octal_to_uint_checked(hdr->checksum, sizeof(hdr->checksum), &stored_checksum)) {
        return 0;
    }
    actual_checksum = tar_compute_checksum(raw);
    if (stored_checksum != actual_checksum) {
        return 0;
    }
    if (!tar_octal_to_uint_checked(hdr->size, sizeof(hdr->size), size)) {
        return 0;
    }
    if (*size > LEONOS_TAR_MAX_FILE_SIZE) {
        return 0;
    }
    if (!tar_header_name_copy(hdr, name, name_cap)) {
        return 0;
    }
    *typeflag = hdr->typeflag ? hdr->typeflag : LEONOS_TAR_TYPE_FILE;
    return 1;
}

static void tar_uint_to_octal(uint32_t value, char *out, uint32_t len)
{
    uint32_t pos = len;
    out[--pos] = 0;
    if (value == 0) {
        if (pos > 0) out[--pos] = '0';
    } else {
        while (pos > 0 && value > 0) {
            out[--pos] = (char)('0' + (value & 7));
            value >>= 3;
        }
    }
    for (uint32_t i = 0; i < pos; ++i) {
        out[i] = '0';
    }
    if (len > 1) {
        out[len - 2] = ' ';
    }
    out[len - 1] = 0;
}

static uint32_t tar_compute_checksum(const unsigned char *header)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < LEONOS_TAR_BLOCK_SIZE; ++i) {
        if (i >= 148 && i < 156) {
            sum += (uint32_t)' ';
        } else {
            sum += header[i];
        }
    }
    return sum;
}

static uint32_t tar_round_up(uint32_t size)
{
    uint32_t rem = size % LEONOS_TAR_BLOCK_SIZE;
    return rem ? size + LEONOS_TAR_BLOCK_SIZE - rem : size;
}

int leonos_tar_finalize(int fd)
{
    unsigned char zero[LEONOS_TAR_BLOCK_SIZE * 2U];
    long wrote;
    memset(zero, 0, sizeof(zero));
    wrote = write(fd, zero, sizeof(zero));
    return wrote == (long)sizeof(zero) ? 1 : 0;
}

static int tar_write_padding(int fd, uint32_t size)
{
    uint32_t padded = tar_round_up(size);
    if (padded > size) {
        unsigned char zero[LEONOS_TAR_BLOCK_SIZE];
        uint32_t pad = padded - size;
        memset(zero, 0, sizeof(zero));
        while (pad > 0) {
            uint32_t chunk = pad < sizeof(zero) ? pad : sizeof(zero);
            long wrote = write(fd, zero, chunk);
            if (wrote != (long)chunk) {
                return 0;
            }
            pad -= chunk;
        }
    }
    return 1;
}

int leonos_tar_create(const char *tar_path)
{
    int fd;
    if (!tar_path || !tar_path[0]) {
        return 0;
    }
    fd = open(tar_path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) {
        return 0;
    }
    close(fd);
    return 1;
}

int leonos_tar_pack_file_append(int tar_fd, const char *file_path,
                                const char *stored_name)
{
    int src_fd;
    struct leonos_stat st;
    struct tar_header header;
    unsigned char raw[LEONOS_TAR_BLOCK_SIZE];
    unsigned char buffer[LEONOS_TAR_BLOCK_SIZE];
    uint32_t name_len;
    uint32_t file_size;
    uint32_t remaining;
    long got;
    long wrote;
    if (!file_path || !file_path[0] || !stored_name || !stored_name[0]) {
        return 0;
    }
    if (!tar_member_name_is_safe(stored_name)) {
        return 0;
    }
    name_len = (uint32_t)strlen(stored_name);
    if (name_len >= LEONOS_TAR_NAME_LEN) {
        return 0;
    }
    if (stat(file_path, &st) != 0 || st.type != LEONOS_FS_TYPE_FILE) {
        return 0;
    }
    file_size = (uint32_t)st.size;
    if (file_size > LEONOS_TAR_MAX_FILE_SIZE) {
        return 0;
    }
    src_fd = open(file_path, LEONOS_O_RDONLY, 0);
    if (src_fd < 0) {
        return 0;
    }
    memset(&header, 0, sizeof(header));
    strcpy(header.name, stored_name);
    tar_uint_to_octal(0644, header.mode, sizeof(header.mode));
    tar_uint_to_octal(file_size, header.size, sizeof(header.size));
    tar_uint_to_octal(0, header.mtime, sizeof(header.mtime));
    header.typeflag = LEONOS_TAR_TYPE_FILE;
    strcpy(header.magic, "ustar");
    header.version[0] = '0';
    header.version[1] = '0';
    tar_uint_to_octal(0, header.checksum, sizeof(header.checksum));
    memset(raw, 0, sizeof(raw));
    memcpy(raw, &header, sizeof(header));
    tar_uint_to_octal(tar_compute_checksum(raw),
                      ((struct tar_header *)raw)->checksum,
                      sizeof(((struct tar_header *)raw)->checksum));
    wrote = write(tar_fd, raw, sizeof(raw));
    if (wrote != (long)sizeof(raw)) {
        close(src_fd);
        return 0;
    }
    remaining = file_size;
    while (remaining > 0) {
        uint32_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        got = read(src_fd, buffer, chunk);
        if (got <= 0) {
            break;
        }
        wrote = write(tar_fd, buffer, (size_t)got);
        if (wrote != got) {
            close(src_fd);
            return 0;
        }
        remaining -= (uint32_t)got;
    }
    close(src_fd);
    if (!tar_write_padding(tar_fd, file_size)) {
        return 0;
    }
    return remaining == 0 ? 1 : 0;
}

int leonos_tar_pack_file(const char *tar_path, const char *file_path,
                         const char *stored_name)
{
    int tar_fd;
    if (!tar_path || !tar_path[0]) {
        return 0;
    }
    tar_fd = open(tar_path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (tar_fd < 0) {
        return 0;
    }
    if (!leonos_tar_pack_file_append(tar_fd, file_path, stored_name)) {
        close(tar_fd);
        unlink(tar_path);
        return 0;
    }
    if (!leonos_tar_finalize(tar_fd)) {
        close(tar_fd);
        unlink(tar_path);
        return 0;
    }
    close(tar_fd);
    return 1;
}

static int tar_pack_dir_recursive_to_fd(int tar_fd, const char *dir_path,
                                        const char *arc_prefix)
{
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
    uint32_t count;
    uint32_t i;
    char full_path[LEONOS_FS_PATH_LEN];
    char stored_name[LEONOS_TAR_NAME_LEN];
    uint32_t dir_len;
    uint32_t prefix_len;
    if (!dir_path || !dir_path[0]) {
        return 0;
    }
    if (leonos_list_dir(dir_path, entries, LEONOS_FS_MAX_ENTRIES,
                        &count) != 0) {
        return 0;
    }
    dir_len = (uint32_t)strlen(dir_path);
    prefix_len = arc_prefix ? (uint32_t)strlen(arc_prefix) : 0;
    for (i = 0; i < count; ++i) {
        uint32_t name_len;
        name_len = (uint32_t)strlen(entries[i].name);
        if (dir_len + 1U + name_len >= sizeof(full_path)) {
            continue;
        }
        memcpy(full_path, dir_path, dir_len);
        full_path[dir_len] = '/';
        memcpy(full_path + dir_len + 1U, entries[i].name, name_len + 1U);
        if (entries[i].type == LEONOS_FS_TYPE_DIR) {
            if (prefix_len + name_len + 1U >= sizeof(stored_name)) {
                continue;
            }
            if (prefix_len) {
                memcpy(stored_name, arc_prefix, prefix_len);
            }
            memcpy(stored_name + prefix_len, entries[i].name, name_len);
            stored_name[prefix_len + name_len] = '/';
            stored_name[prefix_len + name_len + 1U] = 0;
            if (!tar_pack_dir_recursive_to_fd(tar_fd, full_path, stored_name)) {
                return 0;
            }
        } else if (entries[i].type == LEONOS_FS_TYPE_FILE) {
            if (prefix_len + name_len >= sizeof(stored_name)) {
                continue;
            }
            if (prefix_len) {
                memcpy(stored_name, arc_prefix, prefix_len);
            }
            memcpy(stored_name + prefix_len, entries[i].name, name_len + 1U);
            if (!leonos_tar_pack_file_append(tar_fd, full_path, stored_name)) {
                return 0;
            }
        }
    }
    return 1;
}

int leonos_tar_pack_dir(const char *tar_path, const char *dir_path)
{
    int tar_fd;
    if (!tar_path || !tar_path[0]) {
        return 0;
    }
    tar_fd = open(tar_path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (tar_fd < 0) {
        return 0;
    }
    if (!tar_pack_dir_recursive_to_fd(tar_fd, dir_path, 0)) {
        close(tar_fd);
        unlink(tar_path);
        return 0;
    }
    if (!leonos_tar_finalize(tar_fd)) {
        close(tar_fd);
        unlink(tar_path);
        return 0;
    }
    close(tar_fd);
    return 1;
}

int leonos_tar_pack_dir_append(int tar_fd, const char *dir_path)
{
    return tar_pack_dir_recursive_to_fd(tar_fd, dir_path, 0);
}

int leonos_tar_extract_file(const char *tar_path, const char *stored_name,
                            const char *dest_path)
{
    int tar_fd;
    int dest_fd;
    unsigned char header[LEONOS_TAR_BLOCK_SIZE];
    uint32_t file_size;
    uint32_t remaining;
    unsigned char buffer[LEONOS_TAR_BLOCK_SIZE];
    char name[LEONOS_FS_PATH_LEN];
    char typeflag;
    long got;
    long wrote;
    int found = 0;
    int ok = 0;
    if (!tar_path || !tar_path[0] || !stored_name || !stored_name[0] ||
        !dest_path || !dest_path[0]) {
        return 0;
    }
    if (!tar_member_name_is_safe(stored_name)) {
        return 0;
    }
    tar_fd = open(tar_path, LEONOS_O_RDONLY, 0);
    if (tar_fd < 0) {
        return 0;
    }
    for (;;) {
        got = read(tar_fd, header, sizeof(header));
        if (got != (long)sizeof(header)) {
            close(tar_fd);
            return 0;
        }
        if (tar_header_is_zero(header)) {
            close(tar_fd);
            return 0;
        }
        if (!tar_header_parse(header, name, sizeof(name), &file_size, &typeflag)) {
            close(tar_fd);
            return 0;
        }
        if (typeflag == LEONOS_TAR_TYPE_FILE && strcmp(name, stored_name) == 0) {
            found = 1;
            break;
        }
        if (lseek(tar_fd, (long)tar_round_up(file_size), LEONOS_SEEK_CUR) < 0) {
            close(tar_fd);
            return 0;
        }
    }
    if (!found) {
        close(tar_fd);
        return 0;
    }
    if (!tar_ensure_parent_dir(dest_path)) {
        close(tar_fd);
        return 0;
    }
    dest_fd = open(dest_path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC,
                   0);
    if (dest_fd < 0) {
        close(tar_fd);
        return 0;
    }
    remaining = file_size;
    while (remaining > 0) {
        uint32_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        got = read(tar_fd, buffer, chunk);
        if (got <= 0) {
            break;
        }
        wrote = write(dest_fd, buffer, (size_t)got);
        if (wrote != got) {
            break;
        }
        remaining -= (uint32_t)got;
    }
    close(dest_fd);
    close(tar_fd);
    ok = remaining == 0 ? 1 : 0;
    if (!ok) {
        unlink(dest_path);
    }
    return ok;
}

int leonos_tar_extract_all(const char *tar_path, const char *dest_dir)
{
    int tar_fd;
    int dest_fd;
    unsigned char header[LEONOS_TAR_BLOCK_SIZE];
    uint32_t file_size;
    uint32_t padded_size;
    uint32_t remaining;
    uint32_t count = 0;
    int file_ok;
    unsigned char buffer[LEONOS_TAR_BLOCK_SIZE];
    char dest_path[LEONOS_FS_PATH_LEN];
    uint32_t dest_dir_len;
    char name[LEONOS_FS_PATH_LEN];
    char typeflag;
    long got;
    long wrote;
    if (!tar_path || !tar_path[0] || !dest_dir || !dest_dir[0]) {
        return 0;
    }
    dest_dir_len = (uint32_t)strlen(dest_dir);
    if (dest_dir_len + 2U >= sizeof(dest_path)) {
        return 0;
    }
    tar_fd = open(tar_path, LEONOS_O_RDONLY, 0);
    if (tar_fd < 0) {
        return 0;
    }
    for (;;) {
        got = read(tar_fd, header, sizeof(header));
        if (got != (long)sizeof(header)) {
            close(tar_fd);
            return 0;
        }
        if (tar_header_is_zero(header)) {
            break;
        }
        if (!tar_header_parse(header, name, sizeof(name), &file_size, &typeflag)) {
            close(tar_fd);
            return 0;
        }
        padded_size = tar_round_up(file_size);
        if (typeflag == LEONOS_TAR_TYPE_DIR) {
            tar_strip_trailing_slashes(name);
            if (!tar_member_name_is_safe(name) || !tar_join_path(dest_path, sizeof(dest_path), dest_dir, name) ||
                !tar_ensure_dir(dest_path)) {
                close(tar_fd);
                return 0;
            }
            if (lseek(tar_fd, (long)padded_size, LEONOS_SEEK_CUR) < 0) {
                close(tar_fd);
                return 0;
            }
            ++count;
            continue;
        }
        if (typeflag != LEONOS_TAR_TYPE_FILE) {
            close(tar_fd);
            return 0;
        }
        if (!tar_member_name_is_safe(name) ||
            !tar_join_path(dest_path, sizeof(dest_path), dest_dir, name) ||
            !tar_ensure_parent_dir(dest_path)) {
            close(tar_fd);
            return 0;
        }
        dest_fd = open(dest_path,
                       LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
        if (dest_fd < 0) {
            close(tar_fd);
            return 0;
        }
        file_ok = 1;
        remaining = file_size;
        while (remaining > 0) {
            uint32_t chunk = remaining < sizeof(buffer) ? remaining
                                                         : sizeof(buffer);
            got = read(tar_fd, buffer, chunk);
            if (got <= 0) {
                file_ok = 0;
                break;
            }
            wrote = write(dest_fd, buffer, (size_t)got);
            if (wrote != got) {
                file_ok = 0;
                break;
            }
            remaining -= (uint32_t)got;
        }
        close(dest_fd);
        if (!file_ok || remaining != 0) {
            unlink(dest_path);
            close(tar_fd);
            return 0;
        }
        if (padded_size > file_size &&
            lseek(tar_fd, (long)(padded_size - file_size), LEONOS_SEEK_CUR) < 0) {
            close(tar_fd);
            return 0;
        }
        ++count;
    }
    close(tar_fd);
    return count > 0 ? 1 : 0;
}

int leonos_tar_list(const char *tar_path, char *output, uint32_t capacity)
{
    int tar_fd;
    unsigned char header[LEONOS_TAR_BLOCK_SIZE];
    uint32_t pos = 0;
    char name[LEONOS_FS_PATH_LEN];
    uint32_t file_size;
    char typeflag;
    long got;
    if (!tar_path || !tar_path[0] || !output || capacity == 0) {
        return 0;
    }
    output[0] = 0;
    tar_fd = open(tar_path, LEONOS_O_RDONLY, 0);
    if (tar_fd < 0) {
        return 0;
    }
    for (;;) {
        got = read(tar_fd, header, sizeof(header));
        if (got != (long)sizeof(header)) {
            close(tar_fd);
            return 0;
        }
        if (tar_header_is_zero(header)) {
            break;
        }
        if (pos + 64U >= capacity) {
            close(tar_fd);
            return 0;
        }
        if (!tar_header_parse(header, name, sizeof(name), &file_size, &typeflag)) {
            close(tar_fd);
            return 0;
        }
        {
            uint32_t name_len = (uint32_t)strlen(name);
            uint32_t display_size = file_size;
            char num[32];
            uint32_t num_len;
            uint32_t i;
            if (pos + name_len + 32U >= capacity) {
                close(tar_fd);
                return 0;
            }
            for (i = 0; i < name_len && pos < capacity - 1U; ++i) {
                output[pos++] = name[i];
            }
            if (pos < capacity - 1U) {
                output[pos++] = ' ';
            }
            if (pos < capacity - 1U) {
                output[pos++] = '(';
            }
            if (display_size >= 1024U * 1024U) {
                uint32_t mb = display_size / (1024U * 1024U);
                num_len = 0;
                if (mb >= 1000U) { num[num_len++] = '0' + (char)(mb / 1000U); mb %= 1000U; }
                if (mb >= 100U || num_len > 0) { num[num_len++] = '0' + (char)(mb / 100U); mb %= 100U; }
                if (mb >= 10U || num_len > 0) { num[num_len++] = '0' + (char)(mb / 10U); mb %= 10U; }
                num[num_len++] = '0' + (char)(mb);
                for (i = 0; i < num_len && pos < capacity - 1U; ++i) {
                    output[pos++] = num[i];
                }
                if (pos < capacity - 1U) output[pos++] = 'M';
            } else if (display_size >= 1024U) {
                uint32_t kb = display_size / 1024U;
                num_len = 0;
                if (kb >= 100U) { num[num_len++] = '0' + (char)(kb / 100U); kb %= 100U; }
                if (kb >= 10U || num_len > 0) { num[num_len++] = '0' + (char)(kb / 10U); kb %= 10U; }
                num[num_len++] = '0' + (char)(kb);
                for (i = 0; i < num_len && pos < capacity - 1U; ++i) {
                    output[pos++] = num[i];
                }
                if (pos < capacity - 1U) output[pos++] = 'K';
            } else {
                num_len = 0;
                if (display_size >= 100U) { num[num_len++] = '0' + (char)(display_size / 100U); display_size %= 100U; }
                if (display_size >= 10U || num_len > 0) { num[num_len++] = '0' + (char)(display_size / 10U); display_size %= 10U; }
                num[num_len++] = '0' + (char)(display_size);
                for (i = 0; i < num_len && pos < capacity - 1U; ++i) {
                    output[pos++] = num[i];
                }
            }
            if (pos < capacity - 1U) {
                output[pos++] = ')';
            }
            if (pos < capacity - 1U) {
                output[pos++] = '\n';
            }
        }
        {
            uint32_t padded = tar_round_up(file_size);
            if (lseek(tar_fd, (long)padded, LEONOS_SEEK_CUR) < 0) {
                close(tar_fd);
                return 0;
            }
        }
    }
    if (pos < capacity) {
        output[pos] = 0;
    } else {
        output[capacity - 1U] = 0;
    }
    close(tar_fd);
    return 1;
}
