/*
 * LeonOS osmlayer runtime: provides the C ABI and syscall bridge for Rust.
 * Implements storage, identity, IPC, device, and policy callbacks.
 */
#include <stddef.h>
#include <stdint.h>
#include <leonos/auth.h>
#include <leonos/boot_handoff.h>
#include <leonos/fs.h>
#include <leonos/permissions.h>
#include <leonos/layout.h>

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
#define OSMLAYER_RAW_DEVICE_KIND_IDE 10u
#define OSMLAYER_RAW_DEVICE_KIND_NVME 11u

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
};

#define OSMLAYER_ACCOUNTS_PATH LEONOS_PATH_ACCOUNTS_DB

static const struct leonos_kernel_services *osmlayer_services;
/* Legacy ACL defaults have no account authority; ext2 owns UID/GID/mode. */
static struct osmlayer_account *const osmlayer_auth_accounts = NULL;

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
 * @brief Builds the home path for an account.
 * @param home Destination buffer.
 * @param cap Capacity of `home`, including its terminator.
 * @param username Account name appended after `/home/`.
 */
static void osmlayer_home_for_user(char *home, uint32_t cap, const char *username)
{
    uint32_t pos = 0;
    if (!home || cap == 0) {
        return;
    }
    home[0] = 0;
    osmlayer_append_text(home, &pos, cap, "/home/");
    osmlayer_append_text(home, &pos, cap, username);
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
#define OSMLAYER_ACL_TLV_POSIX 2U
#define OSMLAYER_ACL_DISK_VERSION 2U
#define OSMLAYER_ACL_LEGACY_ACE_DENY 0x00000001U

struct osmlayer_acl_record {
    char name[LEONOS_FS_NAME_LEN];
    uint32_t owner_uid;
    uint32_t flags;
    uint32_t ace_count;
    uint32_t has_mode;
    uint32_t mode;
    uint32_t gid;
    struct leonos_fs_acl_ace aces[LEONOS_FS_ACL_MAX_ACE];
};

struct osmlayer_acl_dir {
    uint32_t count;
    uint32_t corrupt;
    struct osmlayer_acl_record records[OSMLAYER_ACL_MAX_RECORDS];
};

/* A directory occupies about 25 KiB. Its owners must stay noinline so the
 * auth dispatcher does not reserve a second copy on the 64 KiB kernel stack. */
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
        (osmlayer_get_u32((const uint8_t *)osmlayer_acl_buf + 4) != LEONOS_FS_ACL_VERSION &&
         osmlayer_get_u32((const uint8_t *)osmlayer_acl_buf + 4) != OSMLAYER_ACL_DISK_VERSION) ||
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
        if (type == OSMLAYER_ACL_TLV_RECORD || type == OSMLAYER_ACL_TLV_POSIX) {
            const uint8_t *p = (const uint8_t *)osmlayer_acl_buf + pos + 4u;
            uint16_t name_len = osmlayer_get_u16(p);
            uint16_t ace_count = osmlayer_get_u16(p + 2u);
            uint32_t prefix = type == OSMLAYER_ACL_TLV_POSIX ? 20u : 12u;
            uint32_t need = prefix + (uint32_t)name_len + (uint32_t)ace_count * 12u;
            if (name_len == 0 || name_len >= LEONOS_FS_NAME_LEN ||
                ace_count > LEONOS_FS_ACL_MAX_ACE || need > tlv_len) {
                dir->corrupt = 1;
                return 0;
            }
            struct osmlayer_acl_record *rec = &dir->records[dir->count++];
            rec->owner_uid = osmlayer_get_u32(p + 4u);
            rec->flags = osmlayer_get_u32(p + 8u);
            rec->ace_count = ace_count;
            rec->has_mode = type == OSMLAYER_ACL_TLV_POSIX;
            if (rec->has_mode) {
                rec->gid = osmlayer_get_u32(p + 12u);
                rec->mode = osmlayer_get_u32(p + 16u) & 0177777u;
            }
            for (uint32_t i = 0; i < name_len; ++i) {
                rec->name[i] = (char)p[prefix + i];
            }
            rec->name[name_len] = 0;
            const uint8_t *ace = p + prefix + name_len;
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
    osmlayer_put_u32(buf + 4u, OSMLAYER_ACL_DISK_VERSION);
    osmlayer_put_u32(buf + 8u, dir->count);
    for (uint32_t r = 0; r < dir->count; ++r) {
        const struct osmlayer_acl_record *rec = &dir->records[r];
        uint32_t name_len = osmlayer_strlen(rec->name);
        uint32_t prefix = rec->has_mode ? 20u : 12u;
        uint32_t payload_len = prefix + name_len + rec->ace_count * 12u;
        if (!name_len || name_len >= LEONOS_FS_NAME_LEN ||
            rec->ace_count > LEONOS_FS_ACL_MAX_ACE ||
            pos + 4u + payload_len > sizeof(osmlayer_acl_buf)) {
            return -7;
        }
        osmlayer_put_u16(buf + pos, rec->has_mode ? OSMLAYER_ACL_TLV_POSIX : OSMLAYER_ACL_TLV_RECORD);
        osmlayer_put_u16(buf + pos + 2u, (uint16_t)payload_len);
        pos += 4u;
        osmlayer_put_u16(buf + pos, (uint16_t)name_len);
        osmlayer_put_u16(buf + pos + 2u, (uint16_t)rec->ace_count);
        osmlayer_put_u32(buf + pos + 4u, rec->owner_uid);
        osmlayer_put_u32(buf + pos + 8u, rec->flags);
        if (rec->has_mode) {
            osmlayer_put_u32(buf + pos + 12u, rec->gid);
            osmlayer_put_u32(buf + pos + 16u, rec->mode);
        }
        pos += prefix;
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

static int osmlayer_acl_store(const char *path,
                              const struct leonos_fs_acl *acl);


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
    /* Read-only system locations.  /var/lib/leonos and /root are deliberately
     * absent: account databases, license state and per-user data must not be
     * world-readable through the default ACL.  /home itself is readable and
     * searchable, but /home/<name> ownership is synthesized per account
     * below. */
    return osmlayer_text_eq(path, "/") ||
           osmlayer_text_eq(path, "/boot") || osmlayer_path_under(path, "/boot") ||
           osmlayer_text_eq(path, "/install") || osmlayer_path_under(path, "/install") ||
           osmlayer_text_eq(path, "/home") ||
           osmlayer_text_eq(path, "/run") || osmlayer_path_under(path, "/run") ||
           osmlayer_text_eq(path, "/lib") || osmlayer_path_under(path, "/lib") ||
           osmlayer_text_eq(path, "/etc") || osmlayer_path_under(path, "/etc") ||
           osmlayer_text_eq(path, "/usr") || osmlayer_path_under(path, "/usr") ||
           osmlayer_text_eq(path, "/opt") || osmlayer_path_under(path, "/opt") ||
           osmlayer_text_eq(path, "/bin") || osmlayer_path_under(path, "/bin") ||
           osmlayer_text_eq(path, "/sbin") || osmlayer_path_under(path, "/sbin") ||
           osmlayer_text_eq(path, "/dev") || osmlayer_path_under(path, "/dev") ||
           osmlayer_text_eq(path, "/media") || osmlayer_path_under(path, "/media") ||
           osmlayer_text_eq(path, "/mnt") || osmlayer_path_under(path, "/mnt");
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
    if (osmlayer_text_eq(path, "/tmp") || osmlayer_path_under(path, "/tmp") ||
        osmlayer_text_eq(path, "/var/tmp") || osmlayer_path_under(path, "/var/tmp")) {
        osmlayer_acl_add_ace(acl, LEONOS_FS_ACL_PRINCIPAL_USERS, 0, LEONOS_FS_PERM_FULL);
        return;
    }
    if (osmlayer_text_eq(path, LEONOS_PATH_DISPLAY_CONF) ||
        osmlayer_text_eq(path, LEONOS_PATH_LOCALE_CONF) ||
        osmlayer_text_eq(path, LEONOS_PATH_OOBE_DONE)) {
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
static __attribute__((noinline)) int osmlayer_acl_get_explicit_or_default(const char *path,
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
        if (rec->has_mode) {
            const uint32_t principals[] = {LEONOS_FS_ACL_PRINCIPAL_OWNER,
                LEONOS_FS_ACL_PRINCIPAL_GROUP, LEONOS_FS_ACL_PRINCIPAL_EVERYONE};
            acl->ace_count = 3;
            for (uint32_t i = 0; i < 3; ++i) {
                uint32_t bits = (rec->mode >> (6u - i * 3u)) & 7u;
                acl->aces[i].principal = principals[i];
                acl->aces[i].permissions = ((bits & 4u) >> 2) | (bits & 2u) | ((bits & 1u) << 2);
            }
            return 0;
        }
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
static __attribute__((noinline)) int osmlayer_acl_store(const char *path,
                                                       const struct leonos_fs_acl *acl)
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
static uint32_t osmlayer_acl_to_rwx(uint32_t bits)
{
    return ((bits & LEONOS_FS_PERM_READ) << 2) |
           (bits & LEONOS_FS_PERM_WRITE) | ((bits & LEONOS_FS_PERM_EXEC) >> 2);
}

/* Version 1 records remain readable. Each record gains explicit uid/gid/mode
 * on its first POSIX metadata write; unrelated records are preserved. */
static __attribute__((noinline)) int osmlayer_posix_explicit(struct leonos_permissions_request *req)
{
    struct osmlayer_acl_dir dir;
    char parent[LEONOS_FS_PATH_LEN], name[LEONOS_FS_NAME_LEN];
    if (!req || (req->action != LEONOS_PERMISSIONS_GET &&
                 req->action != LEONOS_PERMISSIONS_SET) ||
        osmlayer_path_parent_name(req->path, parent, sizeof(parent), name, sizeof(name)) < 0) return -22;
    int ret = osmlayer_acl_load_dir(parent, &dir);
    if (ret < 0) return ret;
    if (dir.corrupt) return -5;
    int idx = osmlayer_acl_find_record(&dir, name);
    if (req->action == LEONOS_PERMISSIONS_SET) {
        if (idx < 0) {
            if (dir.count == OSMLAYER_ACL_MAX_RECORDS) return -28;
            idx = (int)dir.count++;
        }
        struct osmlayer_acl_record *rec = &dir.records[idx];
        memset(rec, 0, sizeof(*rec));
        osmlayer_copy_text(rec->name, sizeof(rec->name), name);
        rec->owner_uid = req->value.uid;
        rec->gid = req->value.gid;
        rec->mode = req->value.mode & 0177777u;
        rec->has_mode = 1;
        return osmlayer_acl_save_dir(parent, &dir);
    }
    if (idx >= 0 && dir.records[idx].has_mode) {
        const struct osmlayer_acl_record *rec = &dir.records[idx];
        req->value = (struct leonos_permissions){rec->mode, rec->owner_uid, rec->gid};
        return 0;
    }
    /* Release the large directory frame before loading the account database. */
    if (idx < 0) return 1;
    struct leonos_fs_acl acl = {0};
    const struct osmlayer_acl_record *rec = &dir.records[idx];
    acl.owner_uid = rec->owner_uid;
    acl.ace_count = rec->ace_count;
    for (uint32_t i = 0; i < rec->ace_count; ++i) acl.aces[i] = rec->aces[i];
    uint32_t owner = 0, other = 0;
    for (uint32_t i = 0; i < acl.ace_count; ++i) {
        const struct leonos_fs_acl_ace *ace = &acl.aces[i];
        if (ace->principal == LEONOS_FS_ACL_PRINCIPAL_OWNER ||
            (!acl.owner_uid && ace->principal == LEONOS_FS_ACL_PRINCIPAL_SYSTEM)) owner |= ace->permissions;
        if (ace->principal == LEONOS_FS_ACL_PRINCIPAL_USERS ||
            ace->principal == LEONOS_FS_ACL_PRINCIPAL_EVERYONE) other |= ace->permissions;
    }
    owner = osmlayer_acl_to_rwx(owner | other);
    other = osmlayer_acl_to_rwx(other);
    req->value = (struct leonos_permissions){(owner << 6) | (other << 3) | other,
                                           acl.owner_uid, acl.owner_uid};
    return 0;
}

static int osmlayer_posix_permissions(struct leonos_permissions_request *req)
{
    int ret = osmlayer_posix_explicit(req);
    if (ret != 1) return ret;
    if (osmlayer_text_eq(req->path, "/etc/shadow") ||
        osmlayer_text_eq(req->path, "/etc/gshadow")) {
        req->value = (struct leonos_permissions){0600, 0, 0};
        return 0;
    }
    uint32_t count = 0;
    /* Ownership is stored in filesystem metadata, never inferred from account files. */
    struct leonos_fs_acl acl = {0};
    osmlayer_acl_default_for_path(req->path, osmlayer_auth_accounts, count, &acl);
    uint32_t owner = 0, other = 0;
    for (uint32_t i = 0; i < acl.ace_count; ++i) {
        const struct leonos_fs_acl_ace *ace = &acl.aces[i];
        if (ace->principal == LEONOS_FS_ACL_PRINCIPAL_OWNER ||
            (!acl.owner_uid && ace->principal == LEONOS_FS_ACL_PRINCIPAL_SYSTEM)) owner |= ace->permissions;
        if (ace->principal == LEONOS_FS_ACL_PRINCIPAL_USERS ||
            ace->principal == LEONOS_FS_ACL_PRINCIPAL_EVERYONE) other |= ace->permissions;
    }
    owner = osmlayer_acl_to_rwx(owner | other);
    other = osmlayer_acl_to_rwx(other);
    req->value = (struct leonos_permissions){(owner << 6) | (other << 3) | other,
                                           acl.owner_uid, acl.owner_uid};
    if (osmlayer_text_eq(req->path, "/tmp") || osmlayer_text_eq(req->path, "/var/tmp")) {
        req->value.mode = 01777;
    }
    return 0;
}

static __attribute__((noinline)) int osmlayer_acl_remove(const char *path)
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
static __attribute__((noinline)) int osmlayer_acl_rename(const char *old_path,
                                                        const char *new_path)
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
    int target = osmlayer_acl_find_record(&dir, new_name);
    if (target == idx && idx >= 0) return 0;
    if (target >= 0) {
        for (uint32_t i = (uint32_t)target; i + 1u < dir.count; ++i)
            dir.records[i] = dir.records[i + 1u];
        --dir.count;
        if (idx > target) --idx;
    }
    if (idx < 0) {
        return target < 0 ? 0 : osmlayer_acl_save_dir(old_parent, &dir);
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
    if (!req || !req->path[0]) {
        return 0;
    }
    if (osmlayer_path_is_accounts_db(req->path) || osmlayer_path_is_acl_file(req->path)) {
        req->allowed = 0;
        return 0;
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
    int ret;
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
    /* Account and password operations belong exclusively to userspace PAM/shadow. */
    case LEONOS_AUTH_OP_AUTHORIZE:
        return osmlayer_auth_authorize((struct leonos_authz_request *)arg);
    case LEONOS_AUTH_OP_FSPERM:
        return osmlayer_fsacl_handle((struct leonos_fs_acl_request *)arg);
    case LEONOS_AUTH_OP_POSIX_PERMISSIONS:
        return osmlayer_posix_permissions((struct leonos_permissions_request *)arg);
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
    case OSMLAYER_RAW_DEVICE_KIND_IDE:
        pos = 0;
        detail[0] = 0;
        osmlayer_append_text(detail, &pos, sizeof(detail), "IDE/PATA controller, disks=");
        osmlayer_append_u64(detail, &pos, sizeof(detail), raw->value0);
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_STORAGE, raw->flags,
                             "IDE/PATA Controller",
                             osmlayer_active(raw->flags) ? "Running" : "Unavailable",
                             detail, raw->value0, raw->value1);
        break;
    case OSMLAYER_RAW_DEVICE_KIND_NVME:
        pos = 0;
        detail[0] = 0;
        osmlayer_append_text(detail, &pos, sizeof(detail), "NVMe controller, namespaces=");
        osmlayer_append_u64(detail, &pos, sizeof(detail), raw->value0);
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_STORAGE, raw->flags,
                             "NVMe Controller",
                             osmlayer_active(raw->flags) ? "Running" : "Unavailable",
                             detail, raw->value0, raw->value1);
        break;
    case OSMLAYER_RAW_DEVICE_KIND_DISK:
        pos = 0;
        name[0] = 0;
        osmlayer_append_text(name, &pos, sizeof(name), "Disk ");
        osmlayer_append_u64(name, &pos, sizeof(name), raw->aux1);
        pos = 0;
        detail[0] = 0;
        osmlayer_append_text(detail, &pos, sizeof(detail),
                             raw->aux0 == 2u ? "IDE/PATA port "
                             : (raw->aux0 == 3u ? "NVMe namespace " : "AHCI port "));
        osmlayer_append_u64(detail, &pos, sizeof(detail), raw->aux0);
        osmlayer_append_text(detail, &pos, sizeof(detail), ", ");
        osmlayer_append_u64(detail, &pos, sizeof(detail),
                            (raw->value0 * raw->value1) / (1024ULL * 1024ULL));
        osmlayer_append_text(detail, &pos, sizeof(detail), " MiB, sector ");
        osmlayer_append_u64(detail, &pos, sizeof(detail), raw->value1);
        if (raw->aux0 == 3u) {
            pos = 0;
            name[0] = 0;
            osmlayer_append_text(name, &pos, sizeof(name), "NVMe Namespace ");
            osmlayer_append_u64(name, &pos, sizeof(name), raw->aux1);
        }
        osmlayer_catalog_add(query, OSMLAYER_DEVICE_CLASS_STORAGE, raw->flags, name,
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
        .cwd = LEONOS_LAYOUT_ETC_LEONOS,
        .input = "../../usr/lib/leonos/apps/desktop/desktop.elf",
        .out = path,
        .capacity = sizeof(path),
        .node_kind = 0,
        .flags = 0,
        .reserved = 0,
    };
    if (osmlayer_resolve_path(&vfs) < 0 ||
        !osmlayer_text_eq(path, LEONOS_LAYOUT_LEONOS_APPS "/desktop/desktop.elf")) {
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
