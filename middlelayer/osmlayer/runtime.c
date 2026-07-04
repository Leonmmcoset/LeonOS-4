#include <stddef.h>
#include <stdint.h>
#include <leonos/auth.h>
#include <leonos/boot_handoff.h>

#define OSMLAYER_VFS_OP_RESOLVE_PATH 1u
#define OSMLAYER_FS_NAME_LEN 128u
#define OSMLAYER_FS_PATH_LEN 256u
#define OSMLAYER_MAX_DRIVES 2u

#define OSMLAYER_DEVICE_MAX 24u
#define OSMLAYER_DEVICE_NAME_LEN 32u
#define OSMLAYER_DEVICE_STATUS_LEN 32u
#define OSMLAYER_DEVICE_DETAIL_LEN 96u

#define OSMLAYER_DEVICE_CLASS_SYSTEM 1u
#define OSMLAYER_DEVICE_CLASS_INPUT 2u
#define OSMLAYER_DEVICE_CLASS_DISPLAY 3u
#define OSMLAYER_DEVICE_CLASS_STORAGE 4u
#define OSMLAYER_DEVICE_CLASS_SERIAL 5u

#define OSMLAYER_DEVICE_FLAG_PRESENT 0x00000001u
#define OSMLAYER_DEVICE_FLAG_ACTIVE 0x00000002u
#define OSMLAYER_DEVICE_FLAG_BOOT 0x00000004u

#define OSMLAYER_RAW_DEVICE_KIND_RTC 1u
#define OSMLAYER_RAW_DEVICE_KIND_KEYBOARD 2u
#define OSMLAYER_RAW_DEVICE_KIND_MOUSE 3u
#define OSMLAYER_RAW_DEVICE_KIND_FRAMEBUFFER 4u
#define OSMLAYER_RAW_DEVICE_KIND_AHCI 5u
#define OSMLAYER_RAW_DEVICE_KIND_DISK 6u
#define OSMLAYER_RAW_DEVICE_KIND_SERIAL 7u

struct osmlayer_vfs_resolve_path {
    const char *cwd;
    const char *input;
    char *out;
    uint32_t capacity;
    uint32_t drive;
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

#define OSMLAYER_ACCOUNTS_PATH "0:/etc/accounts.db"
#define OSMLAYER_ACCOUNT_DB_MAX 8192u

static const struct leonos_kernel_services *osmlayer_services;
static struct osmlayer_account osmlayer_auth_accounts[LEONOS_AUTH_MAX_USERS];

void *memset(void *dst, int value, size_t len)
{
    unsigned char *p = (unsigned char *)dst;
    while (len--) {
        *p++ = (unsigned char)value;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (len--) {
        *d++ = *s++;
    }
    return dst;
}

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

static int osmlayer_text_eq(const char *a, const char *b)
{
    while (a && b && *a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return a && b && *a == 0 && *b == 0;
}

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

static uint32_t osmlayer_strlen(const char *s)
{
    uint32_t len = 0;
    while (s && s[len]) {
        ++len;
    }
    return len;
}

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

static void osmlayer_append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (buf && pos && *pos + 1 < cap) {
        buf[(*pos)++] = ch;
        buf[*pos] = 0;
    }
}

static void osmlayer_append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        osmlayer_append_char(buf, pos, cap, *text++);
    }
}

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

void osmlayer_c_bind_services(const struct leonos_kernel_services *services)
{
    osmlayer_services = services;
}

static int osmlayer_service_read_file(const char *path, void *buf,
                                      uint32_t capacity, uint32_t *out_len)
{
    if (!osmlayer_services || !osmlayer_services->read_file) {
        return -38;
    }
    return osmlayer_services->read_file(path, buf, capacity, out_len);
}

static int osmlayer_service_write_file(const char *path, const void *buf, uint32_t len)
{
    if (!osmlayer_services || !osmlayer_services->write_file) {
        return -38;
    }
    return osmlayer_services->write_file(path, buf, len);
}

static int osmlayer_service_mkdir(const char *path)
{
    int ret;
    if (!osmlayer_services || !osmlayer_services->mkdir) {
        return -38;
    }
    ret = osmlayer_services->mkdir(path);
    return ret == -17 ? 0 : ret;
}

static void osmlayer_append_i32(char *buf, uint32_t *pos, uint32_t cap, int32_t value)
{
    if (value < 0) {
        osmlayer_append_char(buf, pos, cap, '-');
        value = -value;
    }
    osmlayer_append_u64(buf, pos, cap, (uint32_t)value);
}

static int osmlayer_abs_drive_path(const char *path)
{
    return path && path[0] >= '0' && path[0] < (char)('0' + OSMLAYER_MAX_DRIVES) &&
           path[1] == ':' && path[2] == '/';
}

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

static int osmlayer_resolve_path(struct osmlayer_vfs_resolve_path *query)
{
    char parts[16][OSMLAYER_FS_NAME_LEN];
    uint32_t part_count = 0;
    char drive = '0';
    int use_cwd = 1;
    const char *input_part;
    uint32_t out_pos = 0;

    if (!query || !query->input || !query->out || query->capacity < 4) {
        return -22;
    }
    if (osmlayer_abs_drive_path(query->input)) {
        drive = query->input[0];
        use_cwd = 0;
        input_part = query->input + 3;
    } else if (query->input[0] == '/') {
        if (osmlayer_abs_drive_path(query->cwd)) {
            drive = query->cwd[0];
        }
        use_cwd = 0;
        input_part = query->input + 1;
    } else {
        if (osmlayer_abs_drive_path(query->cwd)) {
            drive = query->cwd[0];
        } else {
            query->cwd = "0:/";
        }
        input_part = query->input;
    }
    if (use_cwd && osmlayer_push_path_parts(parts, &part_count, query->cwd + 3) < 0) {
        return -22;
    }
    if (osmlayer_push_path_parts(parts, &part_count, input_part) < 0) {
        return -22;
    }

    query->out[out_pos++] = drive;
    query->out[out_pos++] = ':';
    query->out[out_pos++] = '/';
    query->out[out_pos] = 0;
    for (uint32_t i = 0; i < part_count; ++i) {
        uint32_t len = osmlayer_strlen(parts[i]);
        if (out_pos + len + 1 >= query->capacity) {
            return -22;
        }
        if (out_pos > 3) {
            query->out[out_pos++] = '/';
        }
        for (uint32_t j = 0; parts[i][j]; ++j) {
            query->out[out_pos++] = parts[i][j];
        }
        query->out[out_pos] = 0;
    }
    query->drive = (uint32_t)(drive - '0');
    query->node_kind = osmlayer_text_eq(query->out, "0:/dev") ? 1u :
                       (query->out[0] == '0' && query->out[1] == ':' &&
                        query->out[2] == '/' && query->out[3] == 'd' &&
                        query->out[4] == 'e' && query->out[5] == 'v' &&
                        query->out[6] == '/') ? 3u : 2u;
    return 0;
}

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

static uint32_t osmlayer_rotr32(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32u - bits));
}

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

static int osmlayer_password_valid(const char *password)
{
    uint32_t len = osmlayer_strlen(password);
    return len > 0 && len < LEONOS_AUTH_PASSWORD_LEN;
}

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

static void osmlayer_home_for_user(char *home, uint32_t cap, const char *username)
{
    uint32_t pos = 0;
    if (!home || cap == 0) {
        return;
    }
    home[0] = 0;
    osmlayer_append_text(home, &pos, cap, "0:/users/");
    osmlayer_append_text(home, &pos, cap, username);
}

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

static void osmlayer_append_hex(char *buf, uint32_t *pos, uint32_t cap,
                                const uint8_t *bytes, uint32_t count)
{
    static const char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < count; ++i) {
        osmlayer_append_char(buf, pos, cap, hex[(bytes[i] >> 4) & 0xfu]);
        osmlayer_append_char(buf, pos, cap, hex[bytes[i] & 0xfu]);
    }
}

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

static void osmlayer_append_dec(char *buf, uint32_t *pos, uint32_t cap, uint32_t value)
{
    osmlayer_append_u64(buf, pos, cap, value);
}

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

static int osmlayer_ensure_user_dirs(const char *username)
{
    char home[LEONOS_AUTH_HOME_LEN];
    char child[LEONOS_AUTH_HOME_LEN + 16];
    uint32_t pos;
    int ret;
    if (!osmlayer_username_valid(username)) {
        return -22;
    }
    (void)osmlayer_service_mkdir("0:/users");
    (void)osmlayer_service_mkdir("0:/tmp");
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

static int osmlayer_auth_status(struct leonos_auth_status *status)
{
    struct osmlayer_account *accounts = osmlayer_auth_accounts;
    uint32_t count = 0;
    int ret = osmlayer_accounts_load(accounts, &count);
    if (ret < 0) {
        return ret;
    }
    (void)osmlayer_service_mkdir("0:/users");
    (void)osmlayer_service_mkdir("0:/tmp");
    if (status) {
        status->user_count = count;
        status->has_admin = osmlayer_account_enabled_admin_count(accounts, count) > 0 ? 1u : 0u;
        status->reserved0 = 0;
        status->reserved1 = 0;
    }
    return 0;
}

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
    ret = osmlayer_accounts_save(accounts, count + 1u);
    if (ret < 0) {
        return ret;
    }
    osmlayer_fill_user_info(&create->user, account);
    return 0;
}

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

static int osmlayer_path_is_accounts_db(const char *path)
{
    return osmlayer_text_eq(path, OSMLAYER_ACCOUNTS_PATH);
}

static int osmlayer_path_is_public_read(const char *path)
{
    return osmlayer_text_eq(path, "0:/") ||
           osmlayer_path_under(path, "0:/system") ||
           osmlayer_path_under(path, "0:/userland") ||
           osmlayer_path_under(path, "0:/dev") ||
           osmlayer_text_eq(path, "0:/system") ||
           osmlayer_text_eq(path, "0:/userland") ||
           osmlayer_text_eq(path, "0:/dev") ||
           osmlayer_text_eq(path, "0:/etc") ||
           osmlayer_text_eq(path, "0:/etc/leonos.conf") ||
           osmlayer_text_eq(path, "0:/etc/oobe.done") ||
           osmlayer_text_eq(path, "0:/etc/display.conf") ||
           osmlayer_text_eq(path, "0:/etc/locale.conf");
}

static int osmlayer_path_is_public_write(const char *path)
{
    return osmlayer_text_eq(path, "0:/etc/oobe.done") ||
           osmlayer_text_eq(path, "0:/etc/display.conf") ||
           osmlayer_text_eq(path, "0:/etc/locale.conf");
}

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
    if (osmlayer_path_is_accounts_db(req->path)) {
        req->allowed = 0;
        return 0;
    }
    if (req->role == LEONOS_AUTH_ROLE_ADMIN) {
        req->allowed = 1;
        return 0;
    }
    if (req->op == LEONOS_AUTHZ_EXEC || req->op == LEONOS_AUTHZ_READ) {
        if (osmlayer_path_is_public_read(req->path) ||
            (req->home[0] && osmlayer_path_under(req->path, req->home)) ||
            (req->home[0] && osmlayer_text_eq(req->path, req->home)) ||
            osmlayer_path_under(req->path, "0:/tmp") ||
            osmlayer_text_eq(req->path, "0:/tmp")) {
            req->allowed = 1;
        }
        return 0;
    }
    if (req->op == LEONOS_AUTHZ_WRITE) {
        if (osmlayer_path_is_public_write(req->path) ||
            (req->home[0] && osmlayer_path_under(req->path, req->home)) ||
            (req->home[0] && osmlayer_text_eq(req->path, req->home)) ||
            osmlayer_path_under(req->path, "0:/tmp") ||
            osmlayer_text_eq(req->path, "0:/tmp")) {
            req->allowed = 1;
        }
        return 0;
    }
    return 0;
}

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
    default:
        return -38;
    }
}

static int osmlayer_active(uint32_t flags)
{
    return (flags & (OSMLAYER_DEVICE_FLAG_PRESENT | OSMLAYER_DEVICE_FLAG_ACTIVE)) ==
           (OSMLAYER_DEVICE_FLAG_PRESENT | OSMLAYER_DEVICE_FLAG_ACTIVE);
}

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
    default:
        break;
    }
}

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

int osmlayer_c_services_selftest(void)
{
    char path[OSMLAYER_FS_PATH_LEN];
    struct osmlayer_vfs_resolve_path vfs = {
        .cwd = "0:/etc",
        .input = "../userland/desktop.elf",
        .out = path,
        .capacity = sizeof(path),
        .drive = 0,
        .node_kind = 0,
        .flags = 0,
        .reserved = 0,
    };
    if (osmlayer_resolve_path(&vfs) < 0 ||
        !osmlayer_text_eq(path, "0:/userland/desktop.elf")) {
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
