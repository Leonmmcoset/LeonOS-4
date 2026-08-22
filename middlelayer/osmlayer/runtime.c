/*
 * LeonOS osmlayer runtime: provides the C ABI and syscall bridge for Rust.
 * Implements storage, identity, IPC, device, and policy callbacks.
 */
#include <stddef.h>
#include <stdint.h>
#include <leonos/auth.h>
#include <leonos/boot_handoff.h>
#include <leonos/fs.h>

#define OSMLAYER_VFS_OP_RESOLVE_PATH 1u
#define OSMLAYER_FS_NAME_LEN 128u
#define OSMLAYER_FS_PATH_LEN 256u

#define OSMLAYER_DEVICE_MAX 24u
#define OSMLAYER_DEVICE_NAME_LEN 32u
#define OSMLAYER_DEVICE_STATUS_LEN 32u
#define OSMLAYER_DEVICE_DETAIL_LEN 96u

#define OSMLAYER_DEVICE_CLASS_SYSTEM 1u
#define OSMLAYER_DEVICE_CLASS_INPUT 2u
#define OSMLAYER_DEVICE_CLASS_DISPLAY 3u
#define OSMLAYER_DEVICE_CLASS_STORAGE 4u
#define OSMLAYER_DEVICE_CLASS_SERIAL 5u
#define OSMLAYER_DEVICE_CLASS_NETWORK 6u
#define OSMLAYER_DEVICE_CLASS_AUDIO 7u

#define OSMLAYER_DEVICE_FLAG_PRESENT 0x00000001u
#define OSMLAYER_DEVICE_FLAG_ACTIVE 0x00000002u
#define OSMLAYER_DEVICE_FLAG_BOOT 0x00000004u

#define OSMLAYER_NET_CONFIG_SOURCE_DHCP 2u

#define OSMLAYER_RAW_DEVICE_KIND_RTC 1u
#define OSMLAYER_RAW_DEVICE_KIND_KEYBOARD 2u
#define OSMLAYER_RAW_DEVICE_KIND_MOUSE 3u
#define OSMLAYER_RAW_DEVICE_KIND_FRAMEBUFFER 4u
#define OSMLAYER_RAW_DEVICE_KIND_AHCI 5u
#define OSMLAYER_RAW_DEVICE_KIND_DISK 6u
#define OSMLAYER_RAW_DEVICE_KIND_SERIAL 7u
#define OSMLAYER_RAW_DEVICE_KIND_E1000 8u
#define OSMLAYER_RAW_DEVICE_KIND_AC97 9u

struct osmlayer_vfs_resolve_path {
    const char *cwd;
    const char *input;
    char *out;
    uint32_t capacity;
    uint32_t node_kind;
    uint32_t flags;
    uint32_t reserved;
};

struct osmlayer_raw_device_info {
    uint32_t kind;
    uint32_t flags;
    uint32_t aux0;
    uint32_t aux1;
    uint64_t value0;
    uint64_t value1;
};

struct osmlayer_device_info {
    uint32_t id;
    uint32_t device_class;
    uint32_t flags;
    uint32_t reserved;
    uint64_t value0;
    uint64_t value1;
    char name[OSMLAYER_DEVICE_NAME_LEN];
    char status[OSMLAYER_DEVICE_STATUS_LEN];
    char detail[OSMLAYER_DEVICE_DETAIL_LEN];
};

struct osmlayer_device_catalog_query {
    const struct osmlayer_raw_device_info *raw;
    uint32_t raw_count;
    uint32_t capacity;
    struct osmlayer_device_info *devices;
    uint32_t count;
    uint32_t reserved;
};

struct osmlayer_account {
    uint32_t used;
    uint32_t uid;
    uint32_t role;
    uint32_t flags;
    char username[LEONOS_AUTH_USERNAME_LEN];
    uint8_t salt[8];
    uint8_t hash[32];
};

#define OSMLAYER_ACCOUNTS_PATH "/system/state/accounts.db"
#define OSMLAYER_ACCOUNT_DB_MAX 8192u

static const struct leonos_kernel_services *osmlayer_services;
static struct osmlayer_account osmlayer_auth_accounts[LEONOS_AUTH_MAX_USERS];

/**
 * @brief Fill `len` bytes at `dst` with the low byte of `value`, returning `dst`.
 */
void *memset(void *dst, int value, size_t len)
{
    unsigned char *p = (unsigned char *)dst;
    while (len--) {
        *p++ = (unsigned char)value;
    }
    return dst;
}

/**
 * @brief Copy `len` bytes from `src` to `dst`, returning `dst`.
 */
void *memcpy(void *dst, const void *src, size_t len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (len--) {
        *d++ = *s++;
    }
    return dst;
}

/**
 * @brief Bytewise-compare `len` bytes; returns the first differing byte delta, or 0 when equal.
 */
int memcmp(const void *a, const void *b, size_t len)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (size_t i = 0; i < len; ++i) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}

/**
 * @brief True when `a` and `b` are non-NULL and equal as NUL-terminated strings.
 */
static int osmlayer_text_eq(const char *a, const char *b)
{
    while (a && b && *a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return a && b && *a == 0 && *b == 0;
}

/**
 * @brief True when `text` begins with `prefix` (both non-NULL).
 */
static int osmlayer_text_starts_with(const char *text, const char *prefix)
{
    uint32_t i = 0;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return 1;
}

/**
 * @brief True when `path` equals `base` or is a descendant, matching at a `/` boundary.
 */
static int osmlayer_path_under(const char *path, const char *base)
{
    uint32_t n;
    if (!path || !base || !base[0]) {
        return 0;
    }
    if (osmlayer_text_eq(path, base)) {
        return 1;
    }
    n = 0;
    while (base[n]) {
        ++n;
    }
    return osmlayer_text_starts_with(path, base) && path[n] == '/';
}

/**
 * @brief Returns the length of the NUL-terminated string `s` (0 for NULL).
 */
static uint32_t osmlayer_strlen(const char *s)
{
    uint32_t len = 0;
    while (s && s[len]) {
        ++len;
    }
    return len;
}

/**
 * @brief Copies `src` into `dst`, leaving room for a NUL terminator and truncating when needed.
 */
static void osmlayer_copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

/**
 * @brief Appends `ch` at `*pos` and keeps the buffer NUL-terminated, respecting `cap`.
 */
static void osmlayer_append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (buf && pos && *pos + 1 < cap) {
        buf[(*pos)++] = ch;
        buf[*pos] = 0;
    }
}

/**
 * @brief Appends `text` at `*pos`, stopping at the NUL terminator or the buffer capacity.
 */
static void osmlayer_append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        osmlayer_append_char(buf, pos, cap, *text++);
    }
}

/**
 * @brief Appends the decimal digits of `value` at `*pos`.
 */
static void osmlayer_append_u64(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    char tmp[24];
    uint32_t n = 0;
    if (value == 0) {
        osmlayer_append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) {
        osmlayer_append_char(buf, pos, cap, tmp[--n]);
    }
}

/**
 * @brief Stores the kernel service table for later file/mkdir callbacks.
 */
void osmlayer_c_bind_services(const struct leonos_kernel_services *services)
{
    osmlayer_services = services;
}

/**
 * @brief Delegates file reads to the kernel service; returns -ENOSYS when it is unavailable.
 */
static int osmlayer_service_read_file(const char *path, void *buf,
                                      uint32_t capacity, uint32_t *out_len)
{
    if (!osmlayer_services || !osmlayer_services->read_file) {
        return -38;
    }
    return osmlayer_services->read_file(path, buf, capacity, out_len);
}

/**
 * @brief Delegates file writes to the kernel service; returns -ENOSYS when it is unavailable.
 */
static int osmlayer_service_write_file(const char *path, const void *buf, uint32_t len)
{
    if (!osmlayer_services || !osmlayer_services->write_file) {
        return -38;
    }
    return osmlayer_services->write_file(path, buf, len);
}

/**
 * @brief Creates a directory via the kernel service, treating an existing dir (-EEXIST) as success.
 */
static int osmlayer_service_mkdir(const char *path)
{
    int ret;
    if (!osmlayer_services || !osmlayer_services->mkdir) {
        return -38;
    }
    ret = osmlayer_services->mkdir(path);
    return ret == -17 ? 0 : ret;
}

/**
 * @brief Appends a signed decimal at `*pos`, prefixing '-' when negative.
 */
static void osmlayer_append_i32(char *buf, uint32_t *pos, uint32_t cap, int32_t value)
{
    if (value < 0) {
        osmlayer_append_char(buf, pos, cap, '-');
        value = -value;
    }
    osmlayer_append_u64(buf, pos, cap, (uint32_t)value);
}

/**
 * @brief Appends `ip` as dotted-decimal IPv4 (a.b.c.d).
 */
static void osmlayer_append_ipv4(char *buf, uint32_t *pos, uint32_t cap, uint32_t ip)
{
    osmlayer_append_u64(buf, pos, cap, (ip >> 24) & 0xffu);
    osmlayer_append_char(buf, pos, cap, '.');
    osmlayer_append_u64(buf, pos, cap, (ip >> 16) & 0xffu);
    osmlayer_append_char(buf, pos, cap, '.');
    osmlayer_append_u64(buf, pos, cap, (ip >> 8) & 0xffu);
    osmlayer_append_char(buf, pos, cap, '.');
    osmlayer_append_u64(buf, pos, cap, ip & 0xffu);
}

/**
 * @brief True when `path` is a canonical Unix absolute path with no ':' separator.
 */
static int osmlayer_abs_path(const char *path)
{
    uint32_t i = 0;
    if (!path || path[0] != '/') {
        return 0;
    }
    while (path[i]) {
        if (path[i++] == ':') {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Splits `source` on '/' into normalized parts (resolving '.' and '..'); returns -EINVAL on overflow.
 */
static int osmlayer_push_path_parts(char parts[16][OSMLAYER_FS_NAME_LEN],
                                    uint32_t *part_count, const char *source)
{
    char token[OSMLAYER_FS_NAME_LEN];
    uint32_t pos = 0;
    const char *p = source ? source : "";
    for (;;) {
        char ch = *p;
        if (ch == '/' || ch == 0) {
            token[pos] = 0;
            if (pos != 0) {
                if (osmlayer_text_eq(token, ".")) {
                } else if (osmlayer_text_eq(token, "..")) {
                    if (*part_count) {
                        --*part_count;
                    }
                } else if (*part_count < 16) {
                    osmlayer_copy_text(parts[*part_count], OSMLAYER_FS_NAME_LEN, token);
                    ++*part_count;
                } else {
                    return -22;
                }
            }
            pos = 0;
            if (ch == 0) {
                break;
            }
            ++p;
            continue;
        }
        if (pos + 1 >= sizeof(token)) {
            return -22;
        }
        token[pos++] = ch;
        ++p;
    }
    return 0;
}

/**
 * @brief Joins cwd + input (or an absolute input) into a normalized path and classifies its node kind.
 */
static int osmlayer_resolve_path(struct osmlayer_vfs_resolve_path *query)
{
    char parts[16][OSMLAYER_FS_NAME_LEN];
    uint32_t part_count = 0;
    const char *sources[2];
    uint32_t source_count;
    uint32_t out_pos = 0;

    if (!query || !query->input || !query->out || query->capacity < 2) {
        return -22;
    }
    for (uint32_t i = 0; query->input[i]; ++i) {
        if (query->input[i] == ':') {
            return -22;
        }
    }
    if (query->input[0] == '/') {
        sources[0] = query->input + 1;
        source_count = 1;
    } else {
        if (!osmlayer_abs_path(query->cwd)) {
            query->cwd = "/";
        }
        sources[0] = query->cwd + 1;
        sources[1] = query->input;
        source_count = 2;
    }
    for (uint32_t i = 0; i < source_count; ++i) {
        if (osmlayer_push_path_parts(parts, &part_count, sources[i]) < 0) {
            return -22;
        }
    }

    query->out[out_pos++] = '/';
    query->out[out_pos] = 0;
    for (uint32_t i = 0; i < part_count; ++i) {
        uint32_t len = osmlayer_strlen(parts[i]);
        if (out_pos + len + 1 >= query->capacity) {
            return -22;
        }
        if (out_pos > 1) {
            query->out[out_pos++] = '/';
        }
        for (uint32_t j = 0; parts[i][j]; ++j) {
            query->out[out_pos++] = parts[i][j];
        }
        query->out[out_pos] = 0;
    }
    query->node_kind = osmlayer_text_eq(query->out, "/dev") ? 1u :
                       (query->out[0] == '/' && query->out[1] == 'd' &&
                        query->out[2] == 'e' && query->out[3] == 'v' &&
                        query->out[4] == '/') ? 3u : 2u;
    return 0;
}

/**
 * @brief Dispatches VFS ops; supports resolve-path and returns -ENOSYS for anything else.
 */
int osmlayer_c_vfs_op(uint32_t op, void *arg)
{
    if (op == OSMLAYER_VFS_OP_RESOLVE_PATH) {
        return osmlayer_resolve_path((struct osmlayer_vfs_resolve_path *)arg);
    }
    return -38;
}

struct osmlayer_sha256_ctx {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
};

static const uint32_t osmlayer_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

/**
 * @brief Rotates `value` right by `bits` (SHA-256 helper).
 */
static uint32_t osmlayer_rotr32(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32u - bits));
}

/**
 * @brief Processes one 64-byte block into the SHA-256 state.
 */
static void osmlayer_sha256_transform(struct osmlayer_sha256_ctx *ctx,
                                      const uint8_t data[64])
{
    uint32_t m[64];
    uint32_t a, b, c, d, e, f, g, h;
    for (uint32_t i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) |
               ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) |
               (uint32_t)data[j + 3];
    }
    for (uint32_t i = 16; i < 64; ++i) {
        uint32_t s0 = osmlayer_rotr32(m[i - 15], 7) ^
                      osmlayer_rotr32(m[i - 15], 18) ^
                      (m[i - 15] >> 3);
        uint32_t s1 = osmlayer_rotr32(m[i - 2], 17) ^
                      osmlayer_rotr32(m[i - 2], 19) ^
                      (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t s1 = osmlayer_rotr32(e, 6) ^ osmlayer_rotr32(e, 11) ^
                      osmlayer_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + osmlayer_sha256_k[i] + m[i];
        uint32_t s0 = osmlayer_rotr32(a, 2) ^ osmlayer_rotr32(a, 13) ^
                      osmlayer_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/**
 * @brief Resets the SHA-256 context to the standard IV and an empty buffer.
 */
static void osmlayer_sha256_init(struct osmlayer_sha256_ctx *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
}

/**
 * @brief Adds a byte range to an incremental SHA-256 calculation.
 *
 * Complete 64-byte blocks are compressed immediately; the final partial block
 * remains in the context until `osmlayer_sha256_final` is called.
 * @param ctx Hash state to update.
 * @param input Bytes to hash.
 * @param len Number of bytes at `input`.
 */
static void osmlayer_sha256_update(struct osmlayer_sha256_ctx *ctx,
                                   const void *input, uint32_t len)
{
    const uint8_t *data = (const uint8_t *)input;
    for (uint32_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            osmlayer_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

/**
 * @brief Pads and finalizes a SHA-256 calculation.
 * @param ctx Hash state containing all bytes supplied so far.
 * @param hash 32-byte buffer receiving the digest in big-endian order.
 */
static void osmlayer_sha256_final(struct osmlayer_sha256_ctx *ctx, uint8_t hash[32])
{
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80u;
        while (i < 56) {
            ctx->data[i++] = 0;
        }
    } else {
        ctx->data[i++] = 0x80u;
        while (i < 64) {
            ctx->data[i++] = 0;
        }
        osmlayer_sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8u;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    osmlayer_sha256_transform(ctx, ctx->data);
    for (i = 0; i < 4; ++i) {
        hash[i] = (uint8_t)(ctx->state[0] >> (24u - i * 8u));
        hash[i + 4u] = (uint8_t)(ctx->state[1] >> (24u - i * 8u));
        hash[i + 8u] = (uint8_t)(ctx->state[2] >> (24u - i * 8u));
        hash[i + 12u] = (uint8_t)(ctx->state[3] >> (24u - i * 8u));
        hash[i + 16u] = (uint8_t)(ctx->state[4] >> (24u - i * 8u));
        hash[i + 20u] = (uint8_t)(ctx->state[5] >> (24u - i * 8u));
        hash[i + 24u] = (uint8_t)(ctx->state[6] >> (24u - i * 8u));
        hash[i + 28u] = (uint8_t)(ctx->state[7] >> (24u - i * 8u));
    }
}

/**
 * @brief Checks the password length accepted by the authentication ABI.
 * @param password NUL-terminated password to validate.
 * @return Non-zero when the password is non-empty and fits the ABI field.
 */
static int osmlayer_password_valid(const char *password)
{
    uint32_t len = osmlayer_strlen(password);
    return len > 0 && len < LEONOS_AUTH_PASSWORD_LEN;
}

/**
 * @brief Validates an account name.
 *
 * LeonOS account names use lower-case ASCII letters, digits, and underscores.
 * @param username NUL-terminated account name.
 * @return Non-zero when the name is valid and fits the ABI field.
 */
static int osmlayer_username_valid(const char *username)
{
    uint32_t len = osmlayer_strlen(username);
    if (len == 0 || len >= LEONOS_AUTH_USERNAME_LEN) {
        return 0;
    }
    for (uint32_t i = 0; i < len; ++i) {
        char ch = username[i];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_')) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Builds the home path for an account.
 * @param home Destination buffer.
 * @param cap Capacity of `home`, including its terminator.
 * @param username Account name appended after `/users/`.
 */
static void osmlayer_home_for_user(char *home, uint32_t cap, const char *username)
{
    uint32_t pos = 0;
    if (!home || cap == 0) {
        return;
    }
    home[0] = 0;
    osmlayer_append_text(home, &pos, cap, "/users/");
    osmlayer_append_text(home, &pos, cap, username);
}

/**
 * @brief Converts an internal account record to the public user structure.
 * @param info Destination user information structure.
 * @param account Account record to expose.
 */
static void osmlayer_fill_user_info(struct leonos_user_info *info,
                                    const struct osmlayer_account *account)
{
    if (!info || !account) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->uid = account->uid;
    info->role = account->role;
    info->flags = account->flags;
    osmlayer_copy_text(info->username, sizeof(info->username), account->username);
    osmlayer_home_for_user(info->home, sizeof(info->home), account->username);
}

/**
 * @brief Parses an unsigned decimal integer without libc dependencies.
 * @param text NUL-terminated decimal text.
 * @param ok Optional result flag, set only when the entire string is valid.
 * @return Parsed value, or zero for invalid input.
 */
static uint32_t osmlayer_parse_u32(const char *text, int *ok)
{
    uint32_t value = 0;
    uint32_t i = 0;
    if (ok) {
        *ok = 0;
    }
    if (!text || !text[0]) {
        return 0;
    }
    while (text[i]) {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        value = value * 10u + (uint32_t)(text[i] - '0');
        ++i;
    }
    if (ok) {
        *ok = 1;
    }
    return value;
}

/**
 * @brief Converts one hexadecimal digit to its numeric value.
 * @param ch ASCII hexadecimal character.
 * @return A value from 0 through 15, or -1 when `ch` is not hexadecimal.
 */
static int osmlayer_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

/**
 * @brief Decodes an exact-length hexadecimal string into bytes.
 * @param text Hexadecimal input containing exactly `bytes * 2` characters.
 * @param out Destination byte array.
 * @param bytes Number of bytes to decode.
 * @return 0 on success, or -EINVAL for malformed/extra input.
 */
static int osmlayer_parse_hex(const char *text, uint8_t *out, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; ++i) {
        int hi = osmlayer_hex_value(text ? text[i * 2u] : 0);
        int lo = osmlayer_hex_value(text ? text[i * 2u + 1u] : 0);
        if (hi < 0 || lo < 0) {
            return -22;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return text[bytes * 2u] == 0 ? 0 : -22;
}

/**
 * @brief Appends `count` bytes as lower-case hexadecimal text.
 * @param buf Destination string buffer.
 * @param pos Current output offset, advanced after each emitted digit.
 * @param cap Capacity of `buf`.
 * @param bytes Bytes to encode.
 * @param count Number of bytes in `bytes`.
 */
static void osmlayer_append_hex(char *buf, uint32_t *pos, uint32_t cap,
                                const uint8_t *bytes, uint32_t count)
{
    static const char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < count; ++i) {
        osmlayer_append_char(buf, pos, cap, hex[(bytes[i] >> 4) & 0xfu]);
        osmlayer_append_char(buf, pos, cap, hex[bytes[i] & 0xfu]);
    }
}

/**
 * @brief Derives deterministic seed material for password hashing.
 * @param username Account name.
 * @param password Password text.
 * @param uid Numeric user ID mixed into the seed.
 * @return Non-zero seed value used by the salt generator.
 */
static uint64_t osmlayer_hash_seed(const char *username, const char *password, uint32_t uid)
{
    uint64_t h = 1469598103934665603ULL ^ uid;
    while (username && *username) {
        h ^= (uint8_t)*username++;
        h *= 1099511628211ULL;
    }
    while (password && *password) {
        h ^= (uint8_t)*password++;
        h *= 1099511628211ULL;
    }
    return h;
}

/**
 * @brief Generates the eight-byte per-account password salt.
 * @param salt Destination salt buffer.
 * @param username Account name used as entropy input.
 * @param password Password text used as entropy input.
 * @param uid User ID used as entropy input.
 */
static void osmlayer_make_salt(uint8_t salt[8], const char *username,
                               const char *password, uint32_t uid)
{
    uint64_t seed = osmlayer_hash_seed(username, password, uid);
    for (uint32_t i = 0; i < 8; ++i) {
        seed ^= seed >> 12;
        seed ^= seed << 25;
        seed ^= seed >> 27;
        salt[i] = (uint8_t)((seed * 2685821657736338717ULL) >> 56);
    }
}

/**
 * @brief Computes the stored password digest from a salt and password.
 * @param salt Eight-byte account salt.
 * @param password NUL-terminated password.
 * @param hash Destination 32-byte digest.
 */
static void osmlayer_hash_password(const uint8_t salt[8], const char *password,
                                   uint8_t hash[32])
{
    struct osmlayer_sha256_ctx ctx;
    uint32_t len = osmlayer_strlen(password);
    osmlayer_sha256_init(&ctx);
    osmlayer_sha256_update(&ctx, salt, 8);
    osmlayer_sha256_update(&ctx, password, len);
    osmlayer_sha256_final(&ctx, hash);
}

/**
 * @brief Splits an account database line in place at `|` separators.
 * @param line Mutable line buffer; separators are replaced with NUL bytes.
 * @param fields Output pointers into `line`.
 * @param max_fields Capacity of `fields`.
 * @return Number of fields found, or zero for invalid arguments.
 */
static int osmlayer_split_fields(char *line, char *fields[], uint32_t max_fields)
{
    uint32_t count = 0;
    if (!line || !fields || max_fields == 0) {
        return 0;
    }
    fields[count++] = line;
    for (uint32_t i = 0; line[i]; ++i) {
        if (line[i] == '|') {
            line[i] = 0;
            if (count < max_fields) {
                fields[count++] = &line[i + 1u];
            }
        }
    }
    return (int)count;
}

/**
 * @brief Loads and validates the persistent account database.
 * @param accounts Destination array for at most `LEONOS_AUTH_MAX_USERS` records.
 * @param out_count Optional number of records successfully loaded.
 * @return Zero on success (including a missing database), or a storage error.
 */
static int osmlayer_accounts_load(struct osmlayer_account accounts[LEONOS_AUTH_MAX_USERS],
                                  uint32_t *out_count)
{
    static char db[OSMLAYER_ACCOUNT_DB_MAX];
    uint32_t len = 0;
    uint32_t count = 0;
    int ret;
    memset(accounts, 0, sizeof(struct osmlayer_account) * LEONOS_AUTH_MAX_USERS);
    if (out_count) {
        *out_count = 0;
    }
    ret = osmlayer_service_read_file(OSMLAYER_ACCOUNTS_PATH, db, sizeof(db) - 1u, &len);
    if (ret == -2) {
        return 0;
    }
    if (ret < 0) {
        return ret;
    }
    db[len] = 0;
    uint32_t pos = 0;
    while (pos < len && count < LEONOS_AUTH_MAX_USERS) {
        char line[192];
        char *fields[6];
        uint32_t line_len = 0;
        while (pos < len && db[pos] != '\n' && line_len + 1u < sizeof(line)) {
            char ch = db[pos++];
            if (ch != '\r') {
                line[line_len++] = ch;
            }
        }
        while (pos < len && (db[pos] == '\n' || db[pos] == '\r')) {
            ++pos;
        }
        line[line_len] = 0;
        if (!line[0] || line[0] == '#') {
            continue;
        }
        if (osmlayer_split_fields(line, fields, 6) != 6) {
            continue;
        }
        int ok_uid = 0;
        int ok_role = 0;
        int ok_flags = 0;
        struct osmlayer_account *account = &accounts[count];
        account->uid = osmlayer_parse_u32(fields[0], &ok_uid);
        account->role = osmlayer_parse_u32(fields[1], &ok_role);
        account->flags = osmlayer_parse_u32(fields[2], &ok_flags);
        if (!ok_uid || !ok_role || !ok_flags ||
            !osmlayer_username_valid(fields[3]) ||
            osmlayer_parse_hex(fields[4], account->salt, 8) < 0 ||
            osmlayer_parse_hex(fields[5], account->hash, 32) < 0) {
            continue;
        }
        account->used = 1;
        osmlayer_copy_text(account->username, sizeof(account->username), fields[3]);
        ++count;
    }
    if (out_count) {
        *out_count = count;
    }
    return 0;
}

/**
 * @brief Appends an unsigned decimal value to a bounded string.
 * @param buf Destination string buffer.
 * @param pos Current output offset.
 * @param cap Capacity of `buf`.
 * @param value Number to append.
 */
static void osmlayer_append_dec(char *buf, uint32_t *pos, uint32_t cap, uint32_t value)
{
    osmlayer_append_u64(buf, pos, cap, value);
}

/**
 * @brief Serializes account records and writes the authentication database.
 * @param accounts Records to persist.
 * @param count Number of entries in `accounts`.
 * @return Zero on success, or a capacity/storage error.
 */
static int osmlayer_accounts_save(const struct osmlayer_account accounts[LEONOS_AUTH_MAX_USERS],
                                  uint32_t count)
{
    static char db[OSMLAYER_ACCOUNT_DB_MAX];
    uint32_t pos = 0;
    osmlayer_append_text(db, &pos, sizeof(db), "# leonos-accounts-v1\n");
    for (uint32_t i = 0; i < count; ++i) {
        if (!accounts[i].used) {
            continue;
        }
        osmlayer_append_dec(db, &pos, sizeof(db), accounts[i].uid);
        osmlayer_append_char(db, &pos, sizeof(db), '|');
        osmlayer_append_dec(db, &pos, sizeof(db), accounts[i].role);
        osmlayer_append_char(db, &pos, sizeof(db), '|');
        osmlayer_append_dec(db, &pos, sizeof(db), accounts[i].flags);
        osmlayer_append_char(db, &pos, sizeof(db), '|');
        osmlayer_append_text(db, &pos, sizeof(db), accounts[i].username);
        osmlayer_append_char(db, &pos, sizeof(db), '|');
        osmlayer_append_hex(db, &pos, sizeof(db), accounts[i].salt, 8);
        osmlayer_append_char(db, &pos, sizeof(db), '|');
        osmlayer_append_hex(db, &pos, sizeof(db), accounts[i].hash, 32);
        osmlayer_append_char(db, &pos, sizeof(db), '\n');
        if (pos + 96u >= sizeof(db)) {
            return -7;
        }
    }
    return osmlayer_service_write_file(OSMLAYER_ACCOUNTS_PATH, db, pos);
}

/**
 * @brief Counts enabled administrator accounts.
 * @param accounts Account records to inspect.
 * @param count Number of entries in `accounts`.
 * @return Number of records with the administrator role and no disabled flag.
 */
static int osmlayer_account_enabled_admin_count(const struct osmlayer_account *accounts,
                                                uint32_t count)
{
    uint32_t admins = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (accounts[i].used && accounts[i].role == LEONOS_AUTH_ROLE_ADMIN &&
            !(accounts[i].flags & LEONOS_AUTH_USER_DISABLED)) {
            ++admins;
        }
    }
    return (int)admins;
}

/**
 * @brief Finds an account by its case-sensitive username.
 * @param accounts Account records to search.
 * @param count Number of entries in `accounts`.
 * @param username Name to find.
 * @return Array index, or -1 when no matching used record exists.
 */
static int osmlayer_find_account_by_name(struct osmlayer_account *accounts,
                                         uint32_t count, const char *username)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (accounts[i].used && osmlayer_text_eq(accounts[i].username, username)) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Finds an account by numeric user ID.
 * @param accounts Account records to search.
 * @param count Number of entries in `accounts`.
 * @param uid User ID to find.
 * @return Array index, or -1 when no matching used record exists.
 */
static int osmlayer_find_account_by_uid(struct osmlayer_account *accounts,
                                        uint32_t count, uint32_t uid)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (accounts[i].used && accounts[i].uid == uid) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Tests whether a path names the protected account database.
 * @param path Path to compare with `OSMLAYER_ACCOUNTS_PATH`.
 * @return Non-zero when the path is the account database.
 */
static int osmlayer_path_is_accounts_db(const char *path);

#define OSMLAYER_ACL_FILE_NAME "LEONACL.SYS"
#define OSMLAYER_ACL_MAGIC 0x4c43414cU
#define OSMLAYER_ACL_MAX_BYTES 8192U
#define OSMLAYER_ACL_MAX_RECORDS 64U
#define OSMLAYER_ACL_TLV_RECORD 1U
#define OSMLAYER_ACL_LEGACY_ACE_DENY 0x00000001U

struct osmlayer_acl_record {
    char name[LEONOS_FS_NAME_LEN];
    uint32_t owner_uid;
    uint32_t flags;
    uint32_t ace_count;
    struct leonos_fs_acl_ace aces[LEONOS_FS_ACL_MAX_ACE];
};

struct osmlayer_acl_dir {
    uint32_t count;
    uint32_t corrupt;
    struct osmlayer_acl_record records[OSMLAYER_ACL_MAX_RECORDS];
};

static char osmlayer_acl_buf[OSMLAYER_ACL_MAX_BYTES];

/**
 * @brief Converts one ASCII upper-case letter to lower case.
 * @param ch Character to convert.
 * @return Lower-case equivalent, or `ch` when it is not an upper-case letter.
 */
static char osmlayer_lower(char ch)
{
    return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
}

/**
 * @brief Compares two NUL-terminated strings case-insensitively.
 * @param a First string.
 * @param b Second string.
 * @return The value or status produced by the operation.
 */
static int osmlayer_text_eq_ci(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && osmlayer_lower(*a) == osmlayer_lower(*b)) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

/**
 * @brief Tests whether a path is an ACL sidecar file.
 * @param path Path to inspect.
 * @return Non-zero when the final component is `LEONACL.SYS`.
 */
static int osmlayer_path_is_acl_file(const char *path)
{
    const char *base = path;
    if (!path) {
        return 0;
    }
    for (uint32_t i = 0; path[i]; ++i) {
        if (path[i] == '/') {
            base = path + i + 1u;
        }
    }
    return osmlayer_text_eq_ci(base, OSMLAYER_ACL_FILE_NAME);
}

/**
 * @brief Reads a little-endian 16-bit integer from a byte buffer.
 * @param p Pointer to at least two readable bytes.
 * @return Decoded unsigned value.
 */
static uint16_t osmlayer_get_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

/**
 * @brief Reads a little-endian 32-bit integer from a byte buffer.
 * @param p Pointer to at least four readable bytes.
 * @return Decoded unsigned value.
 */
static uint32_t osmlayer_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/**
 * @brief Stores a 16-bit integer in little-endian order.
 * @param p Destination byte buffer.
 * @param value Value to store.
 */
static void osmlayer_put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
}

/**
 * @brief Stores a 32-bit integer in little-endian order.
 * @param p Destination byte buffer.
 * @param value Value to store.
 */
static void osmlayer_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
}

/**
 * @brief Computes the checksum stored in an ACL directory header.
 * @param buf Serialized ACL bytes.
 * @param len Number of bytes covered by the checksum.
 * @return 32-bit checksum value.
 */
static uint32_t osmlayer_acl_checksum(const uint8_t *buf, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 16; i < len; ++i) {
        h ^= buf[i];
        h *= 16777619u;
    }
    return h;
}

/**
 * @brief Splits a path into its parent directory and final component.
 * @param path Absolute path to split.
 * @param parent Destination parent buffer.
 * @param parent_cap Capacity of `parent`.
 * @param name Destination basename buffer.
 * @param name_cap Capacity of `name`.
 * @return Zero on success, or -EINVAL/-ENAMETOOLONG for invalid input.
 */
static int osmlayer_path_parent_name(const char *path, char *parent,
                                     uint32_t parent_cap, char *name,
                                     uint32_t name_cap)
{
    uint32_t len = osmlayer_strlen(path);
    uint32_t slash = 0;
    uint32_t pos = 0;
    if (!path || !osmlayer_abs_path(path) || !parent || !name ||
        parent_cap < 2 || name_cap == 0) {
        return -22;
    }
    if (len == 1) {
        osmlayer_copy_text(parent, parent_cap, "/");
        osmlayer_copy_text(name, name_cap, ".");
        return 0;
    }
    for (uint32_t i = 1; path[i]; ++i) {
        if (path[i] == '/') {
            slash = i;
        }
    }
    if (slash == 0) {
        osmlayer_copy_text(parent, parent_cap, "/");
        slash = 0;
    } else {
        if (slash + 1u > parent_cap) {
            return -22;
        }
        for (uint32_t i = 0; i < slash; ++i) {
            parent[i] = path[i];
        }
        parent[slash] = 0;
    }
    for (uint32_t i = slash + 1u; path[i]; ++i) {
        if (pos + 1u >= name_cap) {
            return -22;
        }
        name[pos++] = path[i];
    }
    name[pos] = 0;
    return name[0] ? 0 : -22;
}

/**
 * @brief Appends the ACL sidecar filename to a directory path.
 * @param dir Directory path; NULL or empty means `/`.
 * @param out Destination path buffer.
 * @param cap Capacity of `out`.
 */
static void osmlayer_acl_file_path(const char *dir, char *out, uint32_t cap)
{
    uint32_t pos = 0;
    if (!out || cap == 0) {
        return;
    }
    out[0] = 0;
    osmlayer_append_text(out, &pos, cap, dir && dir[0] ? dir : "/");
    if (!osmlayer_text_eq(out, "/")) {
        osmlayer_append_char(out, &pos, cap, '/');
    }
    osmlayer_append_text(out, &pos, cap, OSMLAYER_ACL_FILE_NAME);
}

/**
 * Osmlayer acl find record.
 * @param dir Value supplied by the caller.
 * @param name NUL-terminated text supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_acl_find_record(struct osmlayer_acl_dir *dir, const char *name)
{
    if (!dir || !name) {
        return -1;
    }
    for (uint32_t i = 0; i < dir->count; ++i) {
        if (osmlayer_text_eq_ci(dir->records[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * Osmlayer acl load dir.
 * @param dir_path NUL-terminated text supplied by the caller.
 * @param dir Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_acl_load_dir(const char *dir_path, struct osmlayer_acl_dir *dir)
{
    char acl_path[LEONOS_FS_PATH_LEN];
    uint32_t len = 0;
    int ret;
    if (!dir) {
        return -22;
    }
    memset(dir, 0, sizeof(*dir));
    osmlayer_acl_file_path(dir_path, acl_path, sizeof(acl_path));
    ret = osmlayer_service_read_file(acl_path, osmlayer_acl_buf,
                                     sizeof(osmlayer_acl_buf), &len);
    if (ret == -2) {
        return 0;
    }
    if (ret < 0) {
        return ret;
    }
    if (len < 16 ||
        osmlayer_get_u32((const uint8_t *)osmlayer_acl_buf) != OSMLAYER_ACL_MAGIC ||
        osmlayer_get_u32((const uint8_t *)osmlayer_acl_buf + 4) != LEONOS_FS_ACL_VERSION ||
        osmlayer_get_u32((const uint8_t *)osmlayer_acl_buf + 12) !=
            osmlayer_acl_checksum((const uint8_t *)osmlayer_acl_buf, len)) {
        dir->corrupt = 1;
        return 0;
    }
    uint32_t pos = 16;
    while (pos + 4u <= len && dir->count < OSMLAYER_ACL_MAX_RECORDS) {
        uint16_t type = osmlayer_get_u16((const uint8_t *)osmlayer_acl_buf + pos);
        uint16_t tlv_len = osmlayer_get_u16((const uint8_t *)osmlayer_acl_buf + pos + 2u);
        uint32_t end = pos + 4u + tlv_len;
        if (tlv_len < 12u || end > len) {
            dir->corrupt = 1;
            return 0;
        }
        if (type == OSMLAYER_ACL_TLV_RECORD) {
            const uint8_t *p = (const uint8_t *)osmlayer_acl_buf + pos + 4u;
            uint16_t name_len = osmlayer_get_u16(p);
            uint16_t ace_count = osmlayer_get_u16(p + 2u);
            uint32_t need = 12u + (uint32_t)name_len + (uint32_t)ace_count * 12u;
            if (name_len == 0 || name_len >= LEONOS_FS_NAME_LEN ||
                ace_count > LEONOS_FS_ACL_MAX_ACE || need > tlv_len) {
                dir->corrupt = 1;
                return 0;
            }
            struct osmlayer_acl_record *rec = &dir->records[dir->count++];
            rec->owner_uid = osmlayer_get_u32(p + 4u);
            rec->flags = osmlayer_get_u32(p + 8u);
            rec->ace_count = ace_count;
            for (uint32_t i = 0; i < name_len; ++i) {
                rec->name[i] = (char)p[12u + i];
            }
            rec->name[name_len] = 0;
            const uint8_t *ace = p + 12u + name_len;
            for (uint32_t i = 0; i < ace_count; ++i) {
                uint32_t legacy_flags = osmlayer_get_u32(ace + i * 12u + 4u);
                rec->aces[i].principal = osmlayer_get_u32(ace + i * 12u);
                rec->aces[i].flags = 0;
                rec->aces[i].permissions =
                    (legacy_flags & OSMLAYER_ACL_LEGACY_ACE_DENY)
                        ? 0
                        : osmlayer_get_u32(ace + i * 12u + 8u);
                rec->aces[i].reserved = 0;
            }
        }
        pos = end;
    }
    return 0;
}

/**
 * Osmlayer acl save dir.
 * @param dir_path NUL-terminated text supplied by the caller.
 * @param dir Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_acl_save_dir(const char *dir_path, const struct osmlayer_acl_dir *dir)
{
    char acl_path[LEONOS_FS_PATH_LEN];
    uint8_t *buf = (uint8_t *)osmlayer_acl_buf;
    uint32_t pos = 16;
    if (!dir) {
        return -22;
    }
    memset(osmlayer_acl_buf, 0, sizeof(osmlayer_acl_buf));
    osmlayer_put_u32(buf, OSMLAYER_ACL_MAGIC);
    osmlayer_put_u32(buf + 4u, LEONOS_FS_ACL_VERSION);
    osmlayer_put_u32(buf + 8u, dir->count);
    for (uint32_t r = 0; r < dir->count; ++r) {
        const struct osmlayer_acl_record *rec = &dir->records[r];
        uint32_t name_len = osmlayer_strlen(rec->name);
        uint32_t payload_len = 12u + name_len + rec->ace_count * 12u;
        if (!name_len || name_len >= LEONOS_FS_NAME_LEN ||
            rec->ace_count > LEONOS_FS_ACL_MAX_ACE ||
            pos + 4u + payload_len > sizeof(osmlayer_acl_buf)) {
            return -7;
        }
        osmlayer_put_u16(buf + pos, OSMLAYER_ACL_TLV_RECORD);
        osmlayer_put_u16(buf + pos + 2u, (uint16_t)payload_len);
        pos += 4u;
        osmlayer_put_u16(buf + pos, (uint16_t)name_len);
        osmlayer_put_u16(buf + pos + 2u, (uint16_t)rec->ace_count);
        osmlayer_put_u32(buf + pos + 4u, rec->owner_uid);
        osmlayer_put_u32(buf + pos + 8u, rec->flags);
        pos += 12u;
        for (uint32_t i = 0; i < name_len; ++i) {
            buf[pos++] = (uint8_t)rec->name[i];
        }
        for (uint32_t i = 0; i < rec->ace_count; ++i) {
            osmlayer_put_u32(buf + pos, rec->aces[i].principal);
            osmlayer_put_u32(buf + pos + 4u, 0);
            osmlayer_put_u32(buf + pos + 8u,
                             rec->aces[i].permissions & LEONOS_FS_PERM_FULL);
            pos += 12u;
        }
    }
    osmlayer_put_u32(buf + 12u, osmlayer_acl_checksum(buf, pos));
    osmlayer_acl_file_path(dir_path, acl_path, sizeof(acl_path));
    return osmlayer_service_write_file(acl_path, osmlayer_acl_buf, pos);
}

/**
 * Osmlayer acl add ace.
 * @param acl ACL structure whose next ACE is appended.
 * @param principal Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @param permissions Identifier or flags controlling the operation.
 */
static void osmlayer_acl_add_ace(struct leonos_fs_acl *acl, uint32_t principal,
                                 uint32_t flags, uint32_t permissions)
{
    if (!acl || !permissions || acl->ace_count >= LEONOS_FS_ACL_MAX_ACE) {
        return;
    }
    acl->aces[acl->ace_count++] = (struct leonos_fs_acl_ace){
        .principal = principal,
        .flags = flags,
        .permissions = permissions & LEONOS_FS_PERM_FULL,
        .reserved = 0,
    };
}

/**
 * Osmlayer owner for path.
 * @param path NUL-terminated text supplied by the caller.
 * @param accounts Loaded account records used to determine ownership.
 * @param count Output storage updated by the function.
 * @return The value or status produced by the operation.
 */
static uint32_t osmlayer_owner_for_path(const char *path,
                                        struct osmlayer_account *accounts,
                                        uint32_t count)
{
    char prefix[LEONOS_AUTH_HOME_LEN];
    for (uint32_t i = 0; i < count; ++i) {
        if (!accounts[i].used) {
            continue;
        }
        osmlayer_home_for_user(prefix, sizeof(prefix), accounts[i].username);
        if (osmlayer_text_eq(path, prefix) || osmlayer_path_under(path, prefix)) {
            return accounts[i].uid;
        }
    }
    return 0;
}

/**
 * Osmlayer path is system tree.
 * @param path NUL-terminated text supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_path_is_system_tree(const char *path)
{
    return osmlayer_text_eq(path, "/") ||
           osmlayer_text_eq(path, "/boot") || osmlayer_path_under(path, "/boot") ||
           osmlayer_text_eq(path, "/docs") || osmlayer_path_under(path, "/docs") ||
           osmlayer_text_eq(path, "/system") || osmlayer_path_under(path, "/system") ||
           osmlayer_text_eq(path, "/programs") || osmlayer_path_under(path, "/programs") ||
           osmlayer_text_eq(path, "/users") ||
           osmlayer_text_eq(path, "/var") || osmlayer_path_under(path, "/var") ||
           osmlayer_text_eq(path, "/dev") || osmlayer_path_under(path, "/dev");
}

/**
 * Osmlayer acl default for path.
 * @param path NUL-terminated text supplied by the caller.
 * @param accounts Loaded account records used for default ownership.
 * @param count Output storage updated by the function.
 * @param acl Destination ACL receiving synthesized rules.
 */
static void osmlayer_acl_default_for_path(const char *path,
                                          struct osmlayer_account *accounts,
                                          uint32_t count,
                                          struct leonos_fs_acl *acl)
{
    uint32_t owner = osmlayer_owner_for_path(path, accounts, count);
    memset(acl, 0, sizeof(*acl));
    acl->version = LEONOS_FS_ACL_VERSION;
    acl->owner_uid = owner;
    acl->flags = LEONOS_FS_ACL_FLAG_SYNTHETIC;
    osmlayer_acl_add_ace(acl, LEONOS_FS_ACL_PRINCIPAL_SYSTEM, 0, LEONOS_FS_PERM_FULL);
    osmlayer_acl_add_ace(acl, LEONOS_FS_ACL_PRINCIPAL_ADMINISTRATORS, 0, LEONOS_FS_PERM_FULL);
    if (owner != 0) {
        osmlayer_acl_add_ace(acl, LEONOS_FS_ACL_PRINCIPAL_OWNER, 0, LEONOS_FS_PERM_FULL);
        return;
    }
    if (osmlayer_text_eq(path, "/tmp") || osmlayer_path_under(path, "/tmp")) {
        osmlayer_acl_add_ace(acl, LEONOS_FS_ACL_PRINCIPAL_USERS, 0, LEONOS_FS_PERM_FULL);
        return;
    }
    if (osmlayer_text_eq(path, "/system/config/display.conf") ||
        osmlayer_text_eq(path, "/system/config/locale.conf") ||
        osmlayer_text_eq(path, "/system/state/oobe.done")) {
        osmlayer_acl_add_ace(acl, LEONOS_FS_ACL_PRINCIPAL_USERS, 0,
                             LEONOS_FS_PERM_READ | LEONOS_FS_PERM_WRITE);
        return;
    }
    if (osmlayer_path_is_system_tree(path)) {
        osmlayer_acl_add_ace(acl, LEONOS_FS_ACL_PRINCIPAL_USERS, 0,
                             LEONOS_FS_PERM_READ | LEONOS_FS_PERM_EXEC);
    }
}

/**
 * Osmlayer acl get explicit or default.
 * @param path NUL-terminated text supplied by the caller.
 * @param accounts Account records used to resolve the owner.
 * @param count Output storage updated by the function.
 * @param acl Destination ACL receiving the explicit or synthesized rules.
 * @return The value or status produced by the operation.
 */
static int osmlayer_acl_get_explicit_or_default(const char *path,
                                                struct osmlayer_account *accounts,
                                                uint32_t count,
                                                struct leonos_fs_acl *acl)
{
    char parent[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct osmlayer_acl_dir dir;
    int ret;
    if (osmlayer_path_parent_name(path, parent, sizeof(parent), name, sizeof(name)) < 0) {
        return -22;
    }
    ret = osmlayer_acl_load_dir(parent, &dir);
    if (ret < 0) {
        return ret;
    }
    if (dir.corrupt) {
        osmlayer_acl_default_for_path(path, accounts, count, acl);
        acl->flags |= LEONOS_FS_ACL_FLAG_CORRUPT;
        return 0;
    }
    int idx = osmlayer_acl_find_record(&dir, name);
    if (idx >= 0) {
        const struct osmlayer_acl_record *rec = &dir.records[idx];
        memset(acl, 0, sizeof(*acl));
        acl->version = LEONOS_FS_ACL_VERSION;
        acl->owner_uid = rec->owner_uid;
        acl->flags = rec->flags;
        acl->ace_count = rec->ace_count;
        for (uint32_t i = 0; i < rec->ace_count; ++i) {
            acl->aces[i] = rec->aces[i];
        }
        return 0;
    }
    osmlayer_acl_default_for_path(path, accounts, count, acl);
    return 0;
}

/**
 * Osmlayer acl store.
 * @param path NUL-terminated text supplied by the caller.
 * @param acl ACL to serialize and store.
 * @return The value or status produced by the operation.
 */
static int osmlayer_acl_store(const char *path, const struct leonos_fs_acl *acl)
{
    char parent[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct osmlayer_acl_dir dir;
    int ret;
    int idx;
    if (!acl || acl->version != LEONOS_FS_ACL_VERSION ||
        acl->ace_count > LEONOS_FS_ACL_MAX_ACE ||
        osmlayer_path_parent_name(path, parent, sizeof(parent), name, sizeof(name)) < 0) {
        return -22;
    }
    ret = osmlayer_acl_load_dir(parent, &dir);
    if (ret < 0) {
        return ret;
    }
    if (dir.corrupt) {
        memset(&dir, 0, sizeof(dir));
    }
    idx = osmlayer_acl_find_record(&dir, name);
    if (idx < 0) {
        if (dir.count >= OSMLAYER_ACL_MAX_RECORDS) {
            return -28;
        }
        idx = (int)dir.count++;
    }
    struct osmlayer_acl_record *rec = &dir.records[idx];
    memset(rec, 0, sizeof(*rec));
    osmlayer_copy_text(rec->name, sizeof(rec->name), name);
    rec->owner_uid = acl->owner_uid;
    rec->flags = acl->flags & ~(LEONOS_FS_ACL_FLAG_CORRUPT | LEONOS_FS_ACL_FLAG_SYNTHETIC);
    rec->ace_count = acl->ace_count;
    for (uint32_t i = 0; i < rec->ace_count; ++i) {
        rec->aces[i] = acl->aces[i];
        rec->aces[i].flags = 0;
        rec->aces[i].permissions &= LEONOS_FS_PERM_FULL;
    }
    return osmlayer_acl_save_dir(parent, &dir);
}

/**
 * Osmlayer acl remove.
 * @param path NUL-terminated text supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_acl_remove(const char *path)
{
    char parent[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct osmlayer_acl_dir dir;
    int ret;
    int idx;
    if (osmlayer_path_parent_name(path, parent, sizeof(parent), name, sizeof(name)) < 0) {
        return -22;
    }
    ret = osmlayer_acl_load_dir(parent, &dir);
    if (ret < 0 || dir.corrupt) {
        return ret;
    }
    idx = osmlayer_acl_find_record(&dir, name);
    if (idx < 0) {
        return 0;
    }
    for (uint32_t i = (uint32_t)idx; i + 1u < dir.count; ++i) {
        dir.records[i] = dir.records[i + 1u];
    }
    --dir.count;
    return osmlayer_acl_save_dir(parent, &dir);
}

/**
 * Osmlayer acl rename.
 * @param old_path NUL-terminated text supplied by the caller.
 * @param new_path NUL-terminated text supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_acl_rename(const char *old_path, const char *new_path)
{
    char old_parent[LEONOS_FS_PATH_LEN];
    char new_parent[LEONOS_FS_PATH_LEN];
    char old_name[LEONOS_FS_NAME_LEN];
    char new_name[LEONOS_FS_NAME_LEN];
    struct osmlayer_acl_dir dir;
    int ret;
    int idx;
    if (osmlayer_path_parent_name(old_path, old_parent, sizeof(old_parent), old_name, sizeof(old_name)) < 0 ||
        osmlayer_path_parent_name(new_path, new_parent, sizeof(new_parent), new_name, sizeof(new_name)) < 0 ||
        !osmlayer_text_eq_ci(old_parent, new_parent)) {
        return 0;
    }
    ret = osmlayer_acl_load_dir(old_parent, &dir);
    if (ret < 0 || dir.corrupt) {
        return ret;
    }
    idx = osmlayer_acl_find_record(&dir, old_name);
    if (idx < 0) {
        return 0;
    }
    osmlayer_copy_text(dir.records[idx].name, sizeof(dir.records[idx].name), new_name);
    return osmlayer_acl_save_dir(old_parent, &dir);
}

/**
 * Osmlayer acl principal matches.
 * @param ace Access-control entry to match.
 * @param actor_uid Value supplied by the caller.
 * @param actor_role Value supplied by the caller.
 * @param actor_flags Value supplied by the caller.
 * @param owner_uid Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_acl_principal_matches(const struct leonos_fs_acl_ace *ace,
                                          uint32_t actor_uid, uint32_t actor_role,
                                          uint32_t actor_flags, uint32_t owner_uid)
{
    if (!ace) {
        return 0;
    }
    switch (ace->principal) {
    case LEONOS_FS_ACL_PRINCIPAL_OWNER:
        return actor_uid != 0 && actor_uid == owner_uid;
    case LEONOS_FS_ACL_PRINCIPAL_SYSTEM:
        return actor_uid == 0 || (actor_flags & LEONOS_AUTHZ_ACTOR_SERVICE);
    case LEONOS_FS_ACL_PRINCIPAL_ADMINISTRATORS:
        return actor_role == LEONOS_AUTH_ROLE_ADMIN;
    case LEONOS_FS_ACL_PRINCIPAL_USERS:
        return actor_role == LEONOS_AUTH_ROLE_USER;
    case LEONOS_FS_ACL_PRINCIPAL_EVERYONE:
        return 1;
    default:
        return 0;
    }
}

/**
 * Osmlayer acl actor permissions.
 * @param acl ACL whose entries are evaluated.
 * @param actor_uid Value supplied by the caller.
 * @param actor_role Value supplied by the caller.
 * @param actor_flags Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static uint32_t osmlayer_acl_actor_permissions(const struct leonos_fs_acl *acl,
                                               uint32_t actor_uid,
                                               uint32_t actor_role,
                                               uint32_t actor_flags)
{
    uint32_t allow = 0;
    if (!acl) {
        return 0;
    }
    for (uint32_t i = 0; i < acl->ace_count && i < LEONOS_FS_ACL_MAX_ACE; ++i) {
        const struct leonos_fs_acl_ace *ace = &acl->aces[i];
        if (!osmlayer_acl_principal_matches(ace, actor_uid, actor_role,
                                            actor_flags, acl->owner_uid)) {
            continue;
        }
        allow |= ace->permissions;
    }
    if (actor_uid != 0 && actor_uid == acl->owner_uid) {
        allow |= LEONOS_FS_PERM_MANAGE;
    }
    return allow;
}

/**
 * Osmlayer acl actor allow.
 * @param acl ACL whose entries are evaluated.
 * @param actor_uid Value supplied by the caller.
 * @param actor_role Value supplied by the caller.
 * @param actor_flags Value supplied by the caller.
 * @param allow Output storage updated by the function.
 */
static void osmlayer_acl_actor_allow(const struct leonos_fs_acl *acl,
                                     uint32_t actor_uid, uint32_t actor_role,
                                     uint32_t actor_flags, uint32_t *allow)
{
    if (!acl || !allow) {
        return;
    }
    for (uint32_t i = 0; i < acl->ace_count && i < LEONOS_FS_ACL_MAX_ACE; ++i) {
        const struct leonos_fs_acl_ace *ace = &acl->aces[i];
        if (!osmlayer_acl_principal_matches(ace, actor_uid, actor_role,
                                            actor_flags, acl->owner_uid)) {
            continue;
        }
        *allow |= ace->permissions;
    }
    if (actor_uid != 0 && actor_uid == acl->owner_uid) {
        *allow |= LEONOS_FS_PERM_MANAGE;
    }
}

/**
 * Osmlayer authz permission bit.
 * @param op Identifier or flags controlling the operation.
 * @return The value or status produced by the operation.
 */
static uint32_t osmlayer_authz_permission_bit(uint32_t op)
{
    switch (op) {
    case LEONOS_AUTHZ_READ:
        return LEONOS_FS_PERM_READ;
    case LEONOS_AUTHZ_WRITE:
        return LEONOS_FS_PERM_WRITE;
    case LEONOS_AUTHZ_EXEC:
        return LEONOS_FS_PERM_EXEC;
    case LEONOS_AUTHZ_DELETE:
        return LEONOS_FS_PERM_DELETE;
    case LEONOS_AUTHZ_MANAGE:
        return LEONOS_FS_PERM_MANAGE;
    default:
        return 0;
    }
}

/**
 * Osmlayer acl path has permission.
 * @param path NUL-terminated text supplied by the caller.
 * @param accounts Account records used for owner and role checks.
 * @param count Output storage updated by the function.
 * @param req Authorization request containing path and required permissions.
 * @param needed Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_acl_path_has_permission(const char *path,
                                            struct osmlayer_account *accounts,
                                            uint32_t count,
                                            const struct leonos_authz_request *req,
                                            uint32_t needed)
{
    char parts[16][OSMLAYER_FS_NAME_LEN];
    char current[LEONOS_FS_PATH_LEN];
    uint32_t part_count = 0;
    uint32_t pos = 1;
    uint32_t inherited_allow = 0;
    if (!path || !req || !needed) {
        return 0;
    }
    if (osmlayer_push_path_parts(parts, &part_count, path + 1) < 0) {
        return 0;
    }
    osmlayer_copy_text(current, sizeof(current), "/");
    for (uint32_t i = 0; i < part_count; ++i) {
        struct leonos_fs_acl acl;
        if (pos > 1) {
            osmlayer_append_char(current, &pos, sizeof(current), '/');
        }
        osmlayer_append_text(current, &pos, sizeof(current), parts[i]);
        {
            int acl_ret = osmlayer_acl_get_explicit_or_default(current, accounts,
                                                               count, &acl);
            if (acl_ret < 0) {
                return acl_ret;
            }
        }
        if (acl.flags & LEONOS_FS_ACL_FLAG_CORRUPT) {
            return req->role == LEONOS_AUTH_ROLE_ADMIN && needed == LEONOS_FS_PERM_MANAGE;
        }
        if (i + 1u == part_count || !(acl.flags & LEONOS_FS_ACL_FLAG_SYNTHETIC)) {
            osmlayer_acl_actor_allow(&acl, req->uid, req->role, req->actor_flags,
                                     &inherited_allow);
        }
        if (i + 1u != part_count &&
            (osmlayer_acl_actor_permissions(&acl, req->uid, req->role,
                                            req->actor_flags) & LEONOS_FS_PERM_EXEC) !=
                LEONOS_FS_PERM_EXEC) {
            return 0;
        }
    }
    if (part_count == 0) {
        struct leonos_fs_acl acl;
        {
            int acl_ret = osmlayer_acl_get_explicit_or_default("/", accounts,
                                                               count, &acl);
            if (acl_ret < 0) {
                return acl_ret;
            }
        }
        if (acl.flags & LEONOS_FS_ACL_FLAG_CORRUPT) {
            return req->role == LEONOS_AUTH_ROLE_ADMIN && needed == LEONOS_FS_PERM_MANAGE;
        }
        return (osmlayer_acl_actor_permissions(&acl, req->uid, req->role,
                                               req->actor_flags) & needed) == needed;
    }
    return (inherited_allow & needed) == needed;
}

/**
 * Osmlayer fsacl authorize.
 * @param req Caller-owned structure read or updated by the function.
 * @return The value or status produced by the operation.
 */
static int osmlayer_fsacl_authorize(struct leonos_authz_request *req)
{
    struct osmlayer_account *accounts = osmlayer_auth_accounts;
    uint32_t count = 0;
    uint32_t needed;
    int accounts_ret;
    if (!req || !req->path[0]) {
        return 0;
    }
    if (osmlayer_path_is_accounts_db(req->path) || osmlayer_path_is_acl_file(req->path)) {
        req->allowed = 0;
        return 0;
    }
    accounts_ret = osmlayer_accounts_load(accounts, &count);
    if (accounts_ret < 0) {
        req->allowed = 0;
        /* Do not turn a transient storage read failure into EACCES. The
         * kernel syscall restart path can retry EAGAIN, while a real I/O
         * error must remain visible to the caller instead of looking like a
         * permissions problem. */
        return accounts_ret;
    }
    needed = osmlayer_authz_permission_bit(req->op);
    {
        int permission = osmlayer_acl_path_has_permission(req->path, accounts, count,
                                                          req, needed);
        if (permission < 0) {
            req->allowed = 0;
            return permission;
        }
        req->allowed = permission ? 1u : 0u;
    }
    return 0;
}

/**
 * Osmlayer fsacl handle.
 * @param req Caller-owned structure read or updated by the function.
 * @return The value or status produced by the operation.
 */
static int osmlayer_fsacl_handle(struct leonos_fs_acl_request *req)
{
    struct osmlayer_account *accounts = osmlayer_auth_accounts;
    uint32_t count = 0;
    struct leonos_fs_acl acl;
    if (!req || !req->path[0]) {
        return -22;
    }
    int ret = osmlayer_accounts_load(accounts, &count);
    if (ret < 0) {
        return ret;
    }
    if (req->action == LEONOS_FS_ACL_ACTION_GET) {
        ret = osmlayer_acl_get_explicit_or_default(req->path, accounts, count, &req->acl);
        if (ret < 0) {
            return ret;
        }
        if ((req->acl.flags & LEONOS_FS_ACL_FLAG_CORRUPT) &&
            req->actor_role != LEONOS_AUTH_ROLE_ADMIN) {
            return -13;
        }
        return 0;
    }
    if (req->action == LEONOS_FS_ACL_ACTION_SET) {
        req->acl.version = LEONOS_FS_ACL_VERSION;
        return osmlayer_acl_store(req->path, &req->acl);
    }
    if (req->action == LEONOS_FS_ACL_ACTION_TAKE_OWNERSHIP) {
        if (req->actor_role != LEONOS_AUTH_ROLE_ADMIN || req->actor_uid == 0) {
            return -1;
        }
        ret = osmlayer_acl_get_explicit_or_default(req->path, accounts, count, &acl);
        if (ret < 0) {
            return ret;
        }
        acl.version = LEONOS_FS_ACL_VERSION;
        acl.owner_uid = req->actor_uid;
        acl.flags &= ~(LEONOS_FS_ACL_FLAG_CORRUPT | LEONOS_FS_ACL_FLAG_SYNTHETIC);
        osmlayer_acl_add_ace(&acl, LEONOS_FS_ACL_PRINCIPAL_OWNER, 0, LEONOS_FS_PERM_FULL);
        ret = osmlayer_acl_store(req->path, &acl);
        if (ret == 0) {
            req->acl = acl;
        }
        return ret;
    }
    if (req->action == LEONOS_FS_ACL_ACTION_REPAIR) {
        if (req->actor_role != LEONOS_AUTH_ROLE_ADMIN) {
            return -1;
        }
        osmlayer_acl_default_for_path(req->path, accounts, count, &acl);
        acl.flags = 0;
        ret = osmlayer_acl_store(req->path, &acl);
        if (ret == 0) {
            req->acl = acl;
        }
        return ret;
    }
    if (req->action == LEONOS_FS_ACL_ACTION_NOTE_CREATE) {
        ret = osmlayer_acl_get_explicit_or_default(req->path, accounts, count, &acl);
        if (ret < 0) {
            return ret;
        }
        if (acl.flags & LEONOS_FS_ACL_FLAG_SYNTHETIC) {
            acl.flags = 0;
            if (req->actor_uid != 0) {
                acl.owner_uid = req->actor_uid;
            }
            return osmlayer_acl_store(req->path, &acl);
        }
        return 0;
    }
    if (req->action == LEONOS_FS_ACL_ACTION_NOTE_DELETE) {
        return osmlayer_acl_remove(req->path);
    }
    if (req->action == LEONOS_FS_ACL_ACTION_NOTE_RENAME) {
        return req->path2[0] ? osmlayer_acl_rename(req->path, req->path2) : 0;
    }
    return -38;
}

/**
 * Osmlayer ensure user dirs.
 * @param username NUL-terminated text supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_ensure_user_dirs(const char *username)
{
    char home[LEONOS_AUTH_HOME_LEN];
    char child[LEONOS_AUTH_HOME_LEN + 16];
    uint32_t pos;
    int ret;
    if (!osmlayer_username_valid(username)) {
        return -22;
    }
    (void)osmlayer_service_mkdir("/users");
    (void)osmlayer_service_mkdir("/tmp");
    osmlayer_home_for_user(home, sizeof(home), username);
    ret = osmlayer_service_mkdir(home);
    if (ret < 0) {
        return ret;
    }
    const char *subs[] = {"desktop", "documents", "downloads"};
    for (uint32_t i = 0; i < 3; ++i) {
        pos = 0;
        child[0] = 0;
        osmlayer_append_text(child, &pos, sizeof(child), home);
        osmlayer_append_char(child, &pos, sizeof(child), '/');
        osmlayer_append_text(child, &pos, sizeof(child), subs[i]);
        ret = osmlayer_service_mkdir(child);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

/**
 * Osmlayer write desktop shortcut.
 * @param home Value supplied by the caller.
 * @param name NUL-terminated text supplied by the caller.
 * @param target Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_write_desktop_shortcut(const char *home,
                                           const char *name,
                                           const char *target)
{
    char path[LEONOS_AUTH_HOME_LEN + 48];
    char body[160];
    uint32_t pos = 0;
    uint32_t body_pos = 0;
    if (!home || !home[0] || !name || !name[0] || !target || !target[0]) {
        return -22;
    }
    path[0] = 0;
    osmlayer_append_text(path, &pos, sizeof(path), home);
    osmlayer_append_text(path, &pos, sizeof(path), "/desktop/");
    osmlayer_append_text(path, &pos, sizeof(path), name);
    body[0] = 0;
    osmlayer_append_text(body, &body_pos, sizeof(body), "# LeonOS shortcut\n");
    osmlayer_append_text(body, &body_pos, sizeof(body), "target=");
    osmlayer_append_text(body, &body_pos, sizeof(body), target);
    osmlayer_append_char(body, &body_pos, sizeof(body), '\n');
    return osmlayer_service_write_file(path, body, body_pos);
}

/**
 * @brief Reads the system language used for newly-created user resources.
 * @return Non-zero when the system locale is Chinese; English is the fallback.
 */
static int osmlayer_system_language_is_chinese(void)
{
    char locale[64];
    uint32_t length = 0;
    int ret = osmlayer_service_read_file("/system/config/locale.conf",
                                         locale, sizeof(locale) - 1u, &length);
    if (ret < 0 || length == 0) {
        return 0;
    }
    locale[length < sizeof(locale) ? length : sizeof(locale) - 1u] = 0;
    for (uint32_t i = 0; i + 6u < length; ++i) {
        if ((locale[i] == 'l' || locale[i] == 'L') &&
            locale[i + 1] == 'a' && locale[i + 2] == 'n' &&
            locale[i + 3] == 'g' && locale[i + 4] == '=' &&
            locale[i + 5] == 'z' && locale[i + 6] == 'h') {
            return 1;
        }
    }
    return 0;
}

/**
 * Osmlayer seed desktop shortcuts.
 * @param username NUL-terminated text supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_seed_desktop_shortcuts(const char *username)
{
    char home[LEONOS_AUTH_HOME_LEN];
    int chinese = osmlayer_system_language_is_chinese();
    static const struct {
        const char *name_en;
        const char *name_zh;
        const char *target;
    } shortcuts[] = {
        {"File Manager.lnk", "文件管理器.lnk", "/system/apps/fileman/fileman.elf"},
        {"Task Manager.lnk", "任务管理器.lnk", "/system/apps/taskmgr/taskmgr.elf"},
        {"Settings.lnk", "设置.lnk", "/system/apps/settings/settings.elf"},
        {"Browser.lnk", "浏览器.lnk", "/programs/browser/browser.elf"},
    };
    if (!osmlayer_username_valid(username)) {
        return -22;
    }
    osmlayer_home_for_user(home, sizeof(home), username);
    for (uint32_t i = 0; i < sizeof(shortcuts) / sizeof(shortcuts[0]); ++i) {
        int ret = osmlayer_write_desktop_shortcut(home,
                                                  chinese ? shortcuts[i].name_zh
                                                          : shortcuts[i].name_en,
                                                  shortcuts[i].target);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

/**
 * @brief Reports whether accounts and an enabled administrator exist.
 * @param status Optional destination for the account summary.
 * @return Zero on success, or the account database read error.
 */
static int osmlayer_auth_status(struct leonos_auth_status *status)
{
    struct osmlayer_account *accounts = osmlayer_auth_accounts;
    uint32_t count = 0;
    int ret = osmlayer_accounts_load(accounts, &count);
    if (ret < 0) {
        return ret;
    }
    (void)osmlayer_service_mkdir("/system/state");
    (void)osmlayer_service_mkdir("/users");
    (void)osmlayer_service_mkdir("/tmp");
    if (status) {
        status->user_count = count;
        status->has_admin = osmlayer_account_enabled_admin_count(accounts, count) > 0 ? 1u : 0u;
        status->reserved0 = 0;
        status->reserved1 = 0;
    }
    return 0;
}

/**
 * Osmlayer auth list.
 * @param list Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_auth_list(struct leonos_user_list *list)
{
    struct osmlayer_account *accounts = osmlayer_auth_accounts;
    uint32_t count = 0;
    uint32_t out = 0;
    int ret = osmlayer_accounts_load(accounts, &count);
    if (ret < 0) {
        return ret;
    }
    if (!list) {
        return -22;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!accounts[i].used) {
            continue;
        }
        if ((accounts[i].flags & LEONOS_AUTH_USER_DISABLED) &&
            !(list->actor_role == LEONOS_AUTH_ROLE_ADMIN && list->include_disabled)) {
            continue;
        }
        if (list->users && out < list->capacity) {
            osmlayer_fill_user_info(&list->users[out], &accounts[i]);
        }
        ++out;
    }
    list->count = out;
    return 0;
}

/**
 * Osmlayer auth login.
 * @param login Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_auth_login(struct leonos_auth_login *login)
{
    struct osmlayer_account *accounts = osmlayer_auth_accounts;
    uint32_t count = 0;
    uint8_t hash[32];
    int ret;
    int index;
    if (!login || !osmlayer_username_valid(login->username) ||
        !osmlayer_password_valid(login->password)) {
        return -22;
    }
    ret = osmlayer_accounts_load(accounts, &count);
    if (ret < 0) {
        return ret;
    }
    index = osmlayer_find_account_by_name(accounts, count, login->username);
    if (index < 0 || (accounts[index].flags & LEONOS_AUTH_USER_DISABLED)) {
        return -13;
    }
    osmlayer_hash_password(accounts[index].salt, login->password, hash);
    if (memcmp(hash, accounts[index].hash, sizeof(hash)) != 0) {
        return -13;
    }
    ret = osmlayer_ensure_user_dirs(accounts[index].username);
    if (ret < 0) {
        return ret;
    }
    osmlayer_fill_user_info(&login->user, &accounts[index]);
    return 0;
}

/**
 * Osmlayer auth create.
 * @param create Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_auth_create(struct leonos_auth_create *create)
{
    struct osmlayer_account *accounts = osmlayer_auth_accounts;
    uint32_t count = 0;
    uint32_t next_uid = 1;
    int ret;
    int has_admin;
    if (!create || !osmlayer_username_valid(create->username) ||
        !osmlayer_password_valid(create->password) ||
        (create->role != LEONOS_AUTH_ROLE_ADMIN &&
         create->role != LEONOS_AUTH_ROLE_USER)) {
        return -22;
    }
    ret = osmlayer_accounts_load(accounts, &count);
    if (ret < 0) {
        return ret;
    }
    has_admin = osmlayer_account_enabled_admin_count(accounts, count) > 0;
    if (has_admin && create->actor_role != LEONOS_AUTH_ROLE_ADMIN) {
        return -1;
    }
    if (!has_admin && create->role != LEONOS_AUTH_ROLE_ADMIN) {
        return -1;
    }
    if (count >= LEONOS_AUTH_MAX_USERS ||
        osmlayer_find_account_by_name(accounts, count, create->username) >= 0) {
        return -17;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (accounts[i].uid >= next_uid) {
            next_uid = accounts[i].uid + 1u;
        }
    }
    struct osmlayer_account *account = &accounts[count];
    memset(account, 0, sizeof(*account));
    account->used = 1;
    account->uid = next_uid;
    account->role = create->role;
    account->flags = 0;
    osmlayer_copy_text(account->username, sizeof(account->username), create->username);
    osmlayer_make_salt(account->salt, create->username, create->password, account->uid);
    osmlayer_hash_password(account->salt, create->password, account->hash);
    ret = osmlayer_ensure_user_dirs(account->username);
    if (ret < 0) {
        return ret;
    }
    ret = osmlayer_seed_desktop_shortcuts(account->username);
    if (ret < 0) {
        return ret;
    }
    ret = osmlayer_accounts_save(accounts, count + 1u);
    if (ret < 0) {
        return ret;
    }
    osmlayer_fill_user_info(&create->user, account);
    return 0;
}

/**
 * Osmlayer auth update.
 * @param update Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_auth_update(struct leonos_auth_update *update)
{
    struct osmlayer_account *accounts = osmlayer_auth_accounts;
    uint32_t count = 0;
    uint32_t new_role;
    uint32_t new_flags;
    int ret;
    int index;
    int admin_count;
    if (!update || update->actor_role != LEONOS_AUTH_ROLE_ADMIN) {
        return -1;
    }
    ret = osmlayer_accounts_load(accounts, &count);
    if (ret < 0) {
        return ret;
    }
    index = osmlayer_find_account_by_uid(accounts, count, update->uid);
    if (index < 0) {
        return -2;
    }
    new_role = (update->mask & LEONOS_AUTH_UPDATE_ROLE) ? update->role : accounts[index].role;
    new_flags = (update->mask & LEONOS_AUTH_UPDATE_FLAGS) ? update->flags : accounts[index].flags;
    if (new_role != LEONOS_AUTH_ROLE_ADMIN && new_role != LEONOS_AUTH_ROLE_USER) {
        return -22;
    }
    if (update->uid == update->actor_uid &&
        (new_role != LEONOS_AUTH_ROLE_ADMIN || (new_flags & LEONOS_AUTH_USER_DISABLED))) {
        return -1;
    }
    admin_count = osmlayer_account_enabled_admin_count(accounts, count);
    if (accounts[index].role == LEONOS_AUTH_ROLE_ADMIN &&
        !(accounts[index].flags & LEONOS_AUTH_USER_DISABLED) &&
        (new_role != LEONOS_AUTH_ROLE_ADMIN || (new_flags & LEONOS_AUTH_USER_DISABLED)) &&
        admin_count <= 1) {
        return -1;
    }
    accounts[index].role = new_role;
    accounts[index].flags = new_flags & LEONOS_AUTH_USER_DISABLED;
    return osmlayer_accounts_save(accounts, count);
}

/**
 * Osmlayer auth change password.
 * @param password NUL-terminated text supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_auth_change_password(struct leonos_auth_password *password)
{
    struct osmlayer_account *accounts = osmlayer_auth_accounts;
    uint32_t count = 0;
    uint8_t old_hash[32];
    int ret;
    int index;
    if (!password || !osmlayer_password_valid(password->new_password)) {
        return -22;
    }
    ret = osmlayer_accounts_load(accounts, &count);
    if (ret < 0) {
        return ret;
    }
    index = osmlayer_find_account_by_uid(accounts, count, password->uid);
    if (index < 0) {
        return -2;
    }
    if (password->actor_role != LEONOS_AUTH_ROLE_ADMIN ||
        password->actor_uid == password->uid) {
        if (password->actor_uid != password->uid ||
            !osmlayer_password_valid(password->old_password)) {
            return -1;
        }
        osmlayer_hash_password(accounts[index].salt, password->old_password, old_hash);
        if (memcmp(old_hash, accounts[index].hash, sizeof(old_hash)) != 0) {
            return -13;
        }
    }
    osmlayer_make_salt(accounts[index].salt, accounts[index].username,
                       password->new_password, accounts[index].uid);
    osmlayer_hash_password(accounts[index].salt, password->new_password,
                           accounts[index].hash);
    return osmlayer_accounts_save(accounts, count);
}

/**
 * Osmlayer path is accounts db.
 * @param path NUL-terminated text supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int osmlayer_path_is_accounts_db(const char *path)
{
    return osmlayer_text_eq(path, OSMLAYER_ACCOUNTS_PATH);
}

/**
 * Osmlayer auth authorize.
 * @param req Caller-owned structure read or updated by the function.
 * @return The value or status produced by the operation.
 */
static int osmlayer_auth_authorize(struct leonos_authz_request *req)
{
    if (!req) {
        return -22;
    }
    req->allowed = 0;
    if (req->op == LEONOS_AUTHZ_USER_ADMIN || req->op == LEONOS_AUTHZ_INSTALL) {
        req->allowed = req->role == LEONOS_AUTH_ROLE_ADMIN;
        return 0;
    }
    if (req->op == LEONOS_AUTHZ_KILL_TASK) {
        req->allowed = req->role == LEONOS_AUTH_ROLE_ADMIN ||
                       (req->uid != 0 && req->target_uid == req->uid);
        return 0;
    }
    if (req->op == LEONOS_AUTHZ_READ ||
        req->op == LEONOS_AUTHZ_WRITE ||
        req->op == LEONOS_AUTHZ_EXEC ||
        req->op == LEONOS_AUTHZ_DELETE ||
        req->op == LEONOS_AUTHZ_MANAGE) {
        return osmlayer_fsacl_authorize(req);
    }
    return 0;
}

/**
 * Osmlayer c auth op.
 * @param op Identifier or flags controlling the operation.
 * @param arg Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
int osmlayer_c_auth_op(uint32_t op, void *arg)
{
    switch (op) {
    case LEONOS_AUTH_OP_STATUS:
        return osmlayer_auth_status((struct leonos_auth_status *)arg);
    case LEONOS_AUTH_OP_LIST_USERS:
        return osmlayer_auth_list((struct leonos_user_list *)arg);
    case LEONOS_AUTH_OP_LOGIN:
        return osmlayer_auth_login((struct leonos_auth_login *)arg);
    case LEONOS_AUTH_OP_CREATE_USER:
        return osmlayer_auth_create((struct leonos_auth_create *)arg);
    case LEONOS_AUTH_OP_UPDATE_USER:
        return osmlayer_auth_update((struct leonos_auth_update *)arg);
    case LEONOS_AUTH_OP_CHANGE_PASSWORD:
        return osmlayer_auth_change_password((struct leonos_auth_password *)arg);
    case LEONOS_AUTH_OP_AUTHORIZE:
        return osmlayer_auth_authorize((struct leonos_authz_request *)arg);
    case LEONOS_AUTH_OP_FSPERM:
        return osmlayer_fsacl_handle((struct leonos_fs_acl_request *)arg);
    default:
        return -38;
    }
}

/**
 * @brief Tests whether a device is present and currently active.
 * @param flags Device state flags.
 * @return Non-zero when both the present and active bits are set.
 */
static int osmlayer_active(uint32_t flags)
{
    return (flags & (OSMLAYER_DEVICE_FLAG_PRESENT | OSMLAYER_DEVICE_FLAG_ACTIVE)) ==
           (OSMLAYER_DEVICE_FLAG_PRESENT | OSMLAYER_DEVICE_FLAG_ACTIVE);
}

/**
 * Osmlayer catalog add.
 * @param query Caller-owned structure read or updated by the function.
 * @param device_class Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @param name NUL-terminated text supplied by the caller.
 * @param status Output storage updated by the function.
 * @param detail Value supplied by the caller.
 * @param value0 Value supplied by the caller.
 * @param value1 Value supplied by the caller.
 */
static void osmlayer_catalog_add(struct osmlayer_device_catalog_query *query,
                                 uint32_t device_class, uint32_t flags,
                                 const char *name, const char *status,
                                 const char *detail, uint64_t value0,
                                 uint64_t value1)
{
    uint32_t id = query->count++;
    if (!query->devices || id >= query->capacity) {
        return;
    }
    struct osmlayer_device_info *dev = &query->devices[id];
    memset(dev, 0, sizeof(*dev));
    dev->id = id;
    dev->device_class = device_class;
    dev->flags = flags;
    dev->value0 = value0;
    dev->value1 = value1;
    osmlayer_copy_text(dev->name, sizeof(dev->name), name);
    osmlayer_copy_text(dev->status, sizeof(dev->status), status);
    osmlayer_copy_text(dev->detail, sizeof(dev->detail), detail);
}

/**
 * Osmlayer catalog raw.
 * @param query Caller-owned structure read or updated by the function.
 * @param raw Caller-owned structure read or updated by the function.
 */
static void osmlayer_catalog_raw(struct osmlayer_device_catalog_query *query,
                                 const struct osmlayer_raw_device_info *raw)
{
    char name[OSMLAYER_DEVICE_NAME_LEN];
    char detail[OSMLAYER_DEVICE_DETAIL_LEN];
    uint32_t pos;
    switch (raw->kind) {
    case OSMLAYER_RAW_DEVICE_KIND_RTC: {
        pos = 0;
        detail[0] = 0;
        if (osmlayer_active(raw->flags)) {
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux0 >> 16);
            osmlayer_append_char(detail, &pos, sizeof(detail), '-');
            osmlayer_append_u64(detail, &pos, sizeof(detail), (raw->aux0 >> 8) & 0xffu);
            osmlayer_append_char(detail, &pos, sizeof(detail), '-');
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux0 & 0xffu);
            osmlayer_append_char(detail, &pos, sizeof(detail), ' ');
            osmlayer_append_u64(detail, &pos, sizeof(detail), (raw->aux1 >> 16) & 0xffu);
            osmlayer_append_char(detail, &pos, sizeof(detail), ':');
            osmlayer_append_u64(detail, &pos, sizeof(detail), (raw->aux1 >> 8) & 0xffu);
            osmlayer_append_char(detail, &pos, sizeof(detail), ':');
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux1 & 0xffu);
        } else {
            osmlayer_copy_text(detail, sizeof(detail), "CMOS wall clock not available");
        }
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_SYSTEM, raw->flags,
                             "RTC", osmlayer_active(raw->flags) ? "Running" : "Unavailable",
                             detail, raw->value0, raw->value1);
        break;
    }
    case OSMLAYER_RAW_DEVICE_KIND_KEYBOARD:
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_INPUT, raw->flags,
                             "PS/2 Keyboard",
                             osmlayer_active(raw->flags) ? "Running" : "Unavailable",
                             "IRQ1 scancode input", raw->value0, raw->value1);
        break;
    case OSMLAYER_RAW_DEVICE_KIND_MOUSE:
        pos = 0;
        detail[0] = 0;
        if (osmlayer_active(raw->flags)) {
            osmlayer_append_text(detail, &pos, sizeof(detail), raw->aux1 ? "absolute " : "relative ");
            osmlayer_append_text(detail, &pos, sizeof(detail), "x=");
            osmlayer_append_i32(detail, &pos, sizeof(detail), (int32_t)raw->value0);
            osmlayer_append_text(detail, &pos, sizeof(detail), " y=");
            osmlayer_append_i32(detail, &pos, sizeof(detail), (int32_t)raw->value1);
            osmlayer_append_text(detail, &pos, sizeof(detail), " buttons=");
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux0);
        } else {
            osmlayer_copy_text(detail, sizeof(detail), "PS/2 mouse not detected");
        }
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_INPUT, raw->flags,
                             "PS/2 Mouse", osmlayer_active(raw->flags) ? "Running" : "Unavailable",
                             detail, raw->value0, raw->value1);
        break;
    case OSMLAYER_RAW_DEVICE_KIND_FRAMEBUFFER:
        pos = 0;
        detail[0] = 0;
        if (osmlayer_active(raw->flags)) {
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->value0);
            osmlayer_append_char(detail, &pos, sizeof(detail), 'x');
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->value1);
            osmlayer_append_text(detail, &pos, sizeof(detail), " bpp=");
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux0);
            osmlayer_append_text(detail, &pos, sizeof(detail), " pitch=");
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux1);
        } else {
            osmlayer_copy_text(detail, sizeof(detail), "No GOP framebuffer");
        }
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_DISPLAY, raw->flags,
                             "Framebuffer", osmlayer_active(raw->flags) ? "Running" : "Unavailable",
                             detail, raw->value0, raw->value1);
        break;
    case OSMLAYER_RAW_DEVICE_KIND_AHCI:
        pos = 0;
        detail[0] = 0;
        osmlayer_append_text(detail, &pos, sizeof(detail), "SATA/AHCI controller, disks=");
        osmlayer_append_u64(detail, &pos, sizeof(detail), raw->value0);
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_STORAGE, raw->flags,
                             "AHCI Controller", osmlayer_active(raw->flags) ? "Running" : "Unavailable",
                             detail, raw->value0, raw->value1);
        break;
    case OSMLAYER_RAW_DEVICE_KIND_DISK:
        pos = 0;
        name[0] = 0;
        osmlayer_append_text(name, &pos, sizeof(name), "Disk ");
        osmlayer_append_u64(name, &pos, sizeof(name), raw->aux1);
        pos = 0;
        detail[0] = 0;
        osmlayer_append_text(detail, &pos, sizeof(detail), "AHCI port ");
        osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux0);
        osmlayer_append_text(detail, &pos, sizeof(detail), ", ");
        osmlayer_append_u64(detail, &pos, sizeof(detail),
                            (raw->value0 * raw->value1) / (1024ULL * 1024ULL));
        osmlayer_append_text(detail, &pos, sizeof(detail), " MiB, sector ");
        osmlayer_append_u64(detail, &pos, sizeof(detail), raw->value1);
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_STORAGE, raw->flags,
                             name,
                             (raw->flags & OSMLAYER_DEVICE_FLAG_BOOT) ? "Boot root" : "Ready",
                             detail, raw->value0, raw->value1);
        break;
    case OSMLAYER_RAW_DEVICE_KIND_SERIAL:
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_SERIAL, raw->flags,
                             "Serial COM1",
                             osmlayer_active(raw->flags) ? "Running" : "Unavailable",
                             "I/O port 0x3f8 debug console", raw->value0, raw->value1);
        break;
    case OSMLAYER_RAW_DEVICE_KIND_E1000:
        pos = 0;
        detail[0] = 0;
        if (osmlayer_active(raw->flags)) {
            osmlayer_append_text(detail, &pos, sizeof(detail), "Intel e1000, ");
            osmlayer_append_text(detail, &pos, sizeof(detail),
                                  raw->aux1 == OSMLAYER_NET_CONFIG_SOURCE_DHCP
                                      ? "DHCP IPv4 "
                                      : "static IPv4 ");
            osmlayer_append_ipv4(detail, &pos, sizeof(detail), raw->aux0);
            osmlayer_append_text(detail, &pos, sizeof(detail), ", gateway ");
            osmlayer_append_ipv4(detail, &pos, sizeof(detail), (uint32_t)raw->value1);
        } else if (raw->flags & OSMLAYER_DEVICE_FLAG_PRESENT) {
            osmlayer_copy_text(detail, sizeof(detail), "Intel e1000 detected but not active");
        } else {
            osmlayer_copy_text(detail, sizeof(detail), "No Intel e1000 adapter detected");
        }
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_NETWORK, raw->flags,
                             "Intel e1000",
                             osmlayer_active(raw->flags) ? "Running" : "Unavailable",
                             detail, raw->value0, raw->value1);
        break;
    case OSMLAYER_RAW_DEVICE_KIND_AC97:
        pos = 0;
        detail[0] = 0;
        if (osmlayer_active(raw->flags)) {
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux0);
            osmlayer_append_text(detail, &pos, sizeof(detail), " Hz, ");
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux1 >> 16);
            osmlayer_append_text(detail, &pos, sizeof(detail), " ch, ");
            osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux1 & 0xffffu);
            osmlayer_append_text(detail, &pos, sizeof(detail), "-bit PCM");
        } else if (raw->flags & OSMLAYER_DEVICE_FLAG_PRESENT) {
            osmlayer_copy_text(detail, sizeof(detail),
                                "Audio device detected but driver not active");
        } else {
            osmlayer_copy_text(detail, sizeof(detail),
                                "No supported audio device detected");
        }
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_AUDIO, raw->flags,
                              raw->value0 == 0x12741371ULL
                                  ? "Ensoniq AudioPCI ES1371"
                                  : raw->value0 == 0x80862415ULL
                                        ? "Intel ICH AC'97"
                                        : "Audio Device",
                             osmlayer_active(raw->flags) ? "Running" : "Unavailable",
                             detail, raw->value0, raw->value1);
        break;
    default:
        break;
    }
}

/**
 * Osmlayer c device catalog.
 * @param query Caller-owned structure read or updated by the function.
 * @return The value or status produced by the operation.
 */
int osmlayer_c_device_catalog(struct osmlayer_device_catalog_query *query)
{
    if (!query || (query->raw_count && !query->raw) ||
        (query->capacity && !query->devices)) {
        return -22;
    }
    if (query->capacity > OSMLAYER_DEVICE_MAX) {
        query->capacity = OSMLAYER_DEVICE_MAX;
    }
    query->count = 0;
    uint32_t raw_count = query->raw_count;
    if (raw_count > OSMLAYER_DEVICE_MAX) {
        raw_count = OSMLAYER_DEVICE_MAX;
    }
    for (uint32_t i = 0; i < raw_count; ++i) {
        osmlayer_catalog_raw(query, &query->raw[i]);
    }
    return 0;
}

/**
 * Osmlayer c services selftest.
 * @return The value or status produced by the operation.
 */
int osmlayer_c_services_selftest(void)
{
    char path[OSMLAYER_FS_PATH_LEN];
    struct osmlayer_vfs_resolve_path vfs = {
        .cwd = "/system/config",
        .input = "../apps/desktop/desktop.elf",
        .out = path,
        .capacity = sizeof(path),
        .node_kind = 0,
        .flags = 0,
        .reserved = 0,
    };
    if (osmlayer_resolve_path(&vfs) < 0 ||
        !osmlayer_text_eq(path, "/system/apps/desktop/desktop.elf")) {
        return 0;
    }

    struct osmlayer_raw_device_info raw = {
        .kind = OSMLAYER_RAW_DEVICE_KIND_SERIAL,
        .flags = OSMLAYER_DEVICE_FLAG_PRESENT | OSMLAYER_DEVICE_FLAG_ACTIVE,
        .aux0 = 0x3f8,
        .aux1 = 0,
        .value0 = 0x3f8,
        .value1 = 0,
    };
    struct osmlayer_device_info device;
    struct osmlayer_device_catalog_query catalog = {
        .raw = &raw,
        .raw_count = 1,
        .capacity = 1,
        .devices = &device,
        .count = 0,
        .reserved = 0,
    };
    if (osmlayer_c_device_catalog(&catalog) < 0 || catalog.count != 1 ||
        device.device_class != OSMLAYER_DEVICE_CLASS_SERIAL ||
        !osmlayer_text_eq(device.name, "Serial COM1")) {
        return 0;
    }
    return 1;
}
