#include <leonos/fs.h>
#include <leonos/http.h>
#include <leonos/license.h>
#include <leonos/net.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/system.h>

#define LICENSE_PATH "0:/etc/license.dat"
#define OFFLINE_ALPHABET "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define LOCAL_SECRET "LeonOS4 local activation"
#define OFFLINE_SECRET "LeonOS4 offline license v1"
#define HTTP_BODY_MAX 512U
#define HTTP_HEADERS_MAX 512U
#define SHA256_HEX_LEN 65U

#ifndef LEONOS_LICENSE_REQUIRE
#define LEONOS_LICENSE_REQUIRE 1
#endif

#ifndef CONFIG_LICENSE_SERVER_URL
#define CONFIG_LICENSE_SERVER_URL "http://127.0.0.1:30301"
#endif

#if CONFIG_LICENSE_DEBUG_LOG
#define LICENSE_LOG(...) printf(__VA_ARGS__)
#define LICENSE_LOG_LINE(text) puts(text)
#else
#define LICENSE_LOG(...) ((void)0)
#define LICENSE_LOG_LINE(text) ((void)0)
#endif

struct sha256_ctx {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
};

static const uint32_t sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1U < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1U < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void append_text(char *dst, uint32_t *pos, uint32_t cap, const char *src)
{
    while (src && *src) {
        append_char(dst, pos, cap, *src++);
    }
}

static void append_u32_width(char *dst, uint32_t *pos, uint32_t cap,
                             uint32_t value, uint32_t width)
{
    char tmp[12];
    uint32_t n = 0;
    do {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value && n < sizeof(tmp));
    while (n < width && n < sizeof(tmp)) {
        tmp[n++] = '0';
    }
    while (n) {
        append_char(dst, pos, cap, tmp[--n]);
    }
}

static void append_u32_dec(char *dst, uint32_t *pos, uint32_t cap, uint32_t value)
{
    append_u32_width(dst, pos, cap, value, 0);
}

static char lower_ascii(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? (char)(ch - 'A' + 'a') : ch;
}

static char upper_ascii(char ch)
{
    return ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
}

static int text_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && a[i] == b[i]) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static int text_cmp_n(const char *a, const char *b, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i) {
        uint8_t ca = (uint8_t)(a ? a[i] : 0);
        uint8_t cb = (uint8_t)(b ? b[i] : 0);
        if (ca < cb) {
            return -1;
        }
        if (ca > cb) {
            return 1;
        }
    }
    return 0;
}

static int is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void trim_copy(char *dst, uint32_t cap, const char *src)
{
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && is_space(src[start])) {
        ++start;
    }
    end = start;
    while (src && src[end]) {
        ++end;
    }
    while (end > start && is_space(src[end - 1U])) {
        --end;
    }
    while (start < end && pos + 1U < cap) {
        dst[pos++] = src[start++];
    }
    dst[pos] = 0;
}

static void normalize_email(char *dst, uint32_t cap, const char *email)
{
    char trimmed[LEONOS_LICENSE_EMAIL_LEN];
    uint32_t pos = 0;
    trim_copy(trimmed, sizeof(trimmed), email);
    while (trimmed[pos] && pos + 1U < cap) {
        dst[pos] = lower_ascii(trimmed[pos]);
        ++pos;
    }
    if (cap) {
        dst[pos] = 0;
    }
}

static uint32_t rotr32(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32U - bits));
}

static void sha256_transform(struct sha256_ctx *ctx, const uint8_t data[64])
{
    uint32_t m[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    for (uint32_t i = 0, j = 0; i < 16U; ++i, j += 4U) {
        m[i] = ((uint32_t)data[j] << 24) |
               ((uint32_t)data[j + 1U] << 16) |
               ((uint32_t)data[j + 2U] << 8) |
               (uint32_t)data[j + 3U];
    }
    for (uint32_t i = 16U; i < 64U; ++i) {
        uint32_t s0 = rotr32(m[i - 15U], 7U) ^ rotr32(m[i - 15U], 18U) ^
                      (m[i - 15U] >> 3);
        uint32_t s1 = rotr32(m[i - 2U], 17U) ^ rotr32(m[i - 2U], 19U) ^
                      (m[i - 2U] >> 10);
        m[i] = m[i - 16U] + s0 + m[i - 7U] + s1;
    }
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    for (uint32_t i = 0; i < 64U; ++i) {
        uint32_t s1 = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + sha256_k[i] + m[i];
        uint32_t s0 = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
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

static void sha256_init(struct sha256_ctx *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
}

static void sha256_update(struct sha256_ctx *ctx, const void *input, uint32_t len)
{
    const uint8_t *data = (const uint8_t *)input;
    for (uint32_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64U) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512ULL;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(struct sha256_ctx *ctx, uint8_t hash[32])
{
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56U) {
        ctx->data[i++] = 0x80U;
        while (i < 56U) {
            ctx->data[i++] = 0;
        }
    } else {
        ctx->data[i++] = 0x80U;
        while (i < 64U) {
            ctx->data[i++] = 0;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56U);
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8ULL;
    ctx->data[63] = (uint8_t)ctx->bitlen;
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);
    for (i = 0; i < 4U; ++i) {
        hash[i] = (uint8_t)(ctx->state[0] >> (24U - i * 8U));
        hash[i + 4U] = (uint8_t)(ctx->state[1] >> (24U - i * 8U));
        hash[i + 8U] = (uint8_t)(ctx->state[2] >> (24U - i * 8U));
        hash[i + 12U] = (uint8_t)(ctx->state[3] >> (24U - i * 8U));
        hash[i + 16U] = (uint8_t)(ctx->state[4] >> (24U - i * 8U));
        hash[i + 20U] = (uint8_t)(ctx->state[5] >> (24U - i * 8U));
        hash[i + 24U] = (uint8_t)(ctx->state[6] >> (24U - i * 8U));
        hash[i + 28U] = (uint8_t)(ctx->state[7] >> (24U - i * 8U));
    }
}

static void sha256_bytes(const void *input, uint32_t len, uint8_t hash[32])
{
    struct sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, input, len);
    sha256_final(&ctx, hash);
}

static void sha256_text(const char *text, uint8_t hash[32])
{
    sha256_bytes(text ? text : "", (uint32_t)strlen(text ? text : ""), hash);
}

static void hmac_sha256_text(const char *key, const char *text, uint8_t out[32])
{
    uint8_t key_block[64];
    uint8_t ipad[64];
    uint8_t opad[64];
    uint8_t inner[32];
    struct sha256_ctx ctx;
    uint32_t key_len = (uint32_t)strlen(key ? key : "");
    memset(key_block, 0, sizeof(key_block));
    if (key_len > 64U) {
        sha256_bytes(key, key_len, key_block);
    } else if (key_len) {
        memcpy(key_block, key, key_len);
    }
    for (uint32_t i = 0; i < 64U; ++i) {
        ipad[i] = key_block[i] ^ 0x36U;
        opad[i] = key_block[i] ^ 0x5cU;
    }
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, sizeof(ipad));
    sha256_update(&ctx, text ? text : "", (uint32_t)strlen(text ? text : ""));
    sha256_final(&ctx, inner);
    sha256_init(&ctx);
    sha256_update(&ctx, opad, sizeof(opad));
    sha256_update(&ctx, inner, sizeof(inner));
    sha256_final(&ctx, out);
}

static uint64_t digest_first64_be(const uint8_t digest[32])
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8U; ++i) {
        value = (value << 8) | digest[i];
    }
    return value;
}

static void digest_to_hex(const uint8_t *digest, uint32_t len, char *dst, uint32_t cap)
{
    static const char hex[] = "0123456789abcdef";
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    for (uint32_t i = 0; i < len && pos + 2U < cap; ++i) {
        dst[pos++] = hex[digest[i] >> 4];
        dst[pos++] = hex[digest[i] & 0x0fU];
    }
    dst[pos] = 0;
}

static void bytes_to_hex64(const uint8_t digest[32], char *dst, uint32_t cap)
{
    digest_to_hex(digest, 32U, dst, cap);
}

static void hash_text(struct sha256_ctx *ctx, const char *text)
{
    sha256_update(ctx, text ? text : "", (uint32_t)strlen(text ? text : ""));
}

static void to_base36(uint64_t value, char *dst, uint32_t width)
{
    for (uint32_t i = 0; i < width; ++i) {
        uint32_t n = (uint32_t)(value % 36ULL);
        dst[width - i - 1U] = OFFLINE_ALPHABET[n];
        value /= 36ULL;
    }
    dst[width] = 0;
}

static int valid_yyyymmdd(const char *s)
{
    uint32_t y;
    uint32_t m;
    uint32_t d;
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!s) {
        return 0;
    }
    for (uint32_t i = 0; i < 8U; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
    }
    if (s[8]) {
        return 0;
    }
    y = (uint32_t)(s[0] - '0') * 1000U + (uint32_t)(s[1] - '0') * 100U +
        (uint32_t)(s[2] - '0') * 10U + (uint32_t)(s[3] - '0');
    m = (uint32_t)(s[4] - '0') * 10U + (uint32_t)(s[5] - '0');
    d = (uint32_t)(s[6] - '0') * 10U + (uint32_t)(s[7] - '0');
    if (y < 1970U || m < 1U || m > 12U || d < 1U) {
        return 0;
    }
    uint32_t max = days[m - 1U];
    if (m == 2U && ((y % 4U == 0U && y % 100U != 0U) || y % 400U == 0U)) {
        max = 29U;
    }
    return d <= max;
}

static void time_date_text(const struct leonos_time_info *time, char *out, uint32_t cap)
{
    uint32_t pos = 0;
    if (!out || cap < 9U || !time || !time->valid) {
        if (out && cap) {
            out[0] = 0;
        }
        return;
    }
    out[0] = 0;
    append_u32_width(out, &pos, cap, time->year, 4U);
    append_u32_width(out, &pos, cap, time->month, 2U);
    append_u32_width(out, &pos, cap, time->day, 2U);
}

static void email_hash36(const char *email, char *out, uint32_t cap)
{
    char normalized[LEONOS_LICENSE_EMAIL_LEN];
    uint8_t digest[32];
    if (!out || cap < 11U) {
        return;
    }
    normalize_email(normalized, sizeof(normalized), email);
    sha256_text(normalized, digest);
    to_base36(digest_first64_be(digest), out, 10U);
}

static void offline_mac(const char *payload, char *out, uint32_t cap)
{
    uint8_t digest[32];
    if (!out || cap < 12U) {
        return;
    }
    hmac_sha256_text(OFFLINE_SECRET, payload, digest);
    to_base36(digest_first64_be(digest), out, 11U);
}

static int read_file_text(const char *path, char *out, uint32_t cap)
{
    int fd;
    uint32_t len = 0;
    if (!out || cap == 0) {
        return -1;
    }
    out[0] = 0;
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return fd;
    }
    while (len + 1U < cap) {
        long got = read(fd, out + len, cap - len - 1U);
        if (got < 0) {
            close(fd);
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    out[len] = 0;
    return 0;
}

static int write_file_text(const char *path, const char *text)
{
    int fd = open(path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    uint32_t len = (uint32_t)strlen(text);
    long wrote;
    if (fd < 0) {
        return fd;
    }
    wrote = write(fd, text, len);
    close(fd);
    return wrote == (long)len ? 0 : -1;
}

static int config_value(const char *buf, const char *key, char *out, uint32_t cap)
{
    uint32_t pos = 0;
    uint32_t key_len = (uint32_t)strlen(key);
    if (out && cap) {
        out[0] = 0;
    }
    while (buf && buf[pos]) {
        uint32_t start = pos;
        uint32_t end;
        while (buf[pos] && buf[pos] != '\n' && buf[pos] != '\r') {
            ++pos;
        }
        end = pos;
        while (buf[pos] == '\n' || buf[pos] == '\r') {
            ++pos;
        }
        if (end > start + key_len && buf[start + key_len] == '=') {
            uint32_t matched = 1;
            for (uint32_t i = 0; i < key_len; ++i) {
                if (buf[start + i] != key[i]) {
                    matched = 0;
                    break;
                }
            }
            if (matched) {
                uint32_t vstart = start + key_len + 1U;
                uint32_t vpos = 0;
                while (vstart < end && vpos + 1U < cap) {
                    out[vpos++] = buf[vstart++];
                }
                out[vpos] = 0;
                return 1;
            }
        }
    }
    return 0;
}

static void url_encode(char *dst, uint32_t *pos, uint32_t cap, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    while (src && *src) {
        uint8_t ch = (uint8_t)*src++;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.') {
            append_char(dst, pos, cap, (char)ch);
        } else if (ch == ' ') {
            append_char(dst, pos, cap, '+');
        } else {
            append_char(dst, pos, cap, '%');
            append_char(dst, pos, cap, hex[ch >> 4]);
            append_char(dst, pos, cap, hex[ch & 0x0fU]);
        }
    }
}

static int response_value(const char *body, const char *key, char *out, uint32_t cap)
{
    return config_value(body, key, out, cap);
}

int leonos_license_default_server(char *out, uint32_t cap)
{
    if (!out || cap == 0) {
        return -1;
    }
    copy_text(out, cap, CONFIG_LICENSE_SERVER_URL);
    return 0;
}

int leonos_license_required(void)
{
#if LEONOS_LICENSE_REQUIRE
    return 1;
#else
    return 0;
#endif
}

int leonos_license_install_id(char *out, uint32_t cap)
{
    struct sha256_ctx ctx;
    struct leonos_machine_identity identity;
    uint8_t digest[32];
    char hex[33];
    uint32_t pos;
    if (!out || cap == 0) {
        return -1;
    }

    identity = (struct leonos_machine_identity){0};
    if (leonos_machine_identity(&identity) < 0) {
        copy_text(out, cap, "");
        return -1;
    }
    sha256_init(&ctx);
    hash_text(&ctx, "LeonOS4 machine id v2|");
    if ((identity.flags & LEONOS_MACHINE_IDENTITY_FLAG_PLATFORM_UUID) &&
        identity.platform_uuid[0]) {
        hash_text(&ctx, "platform-uuid|");
        hash_text(&ctx, identity.platform_uuid);
    } else if ((identity.flags & LEONOS_MACHINE_IDENTITY_FLAG_BOOT_DISK_GUID) &&
               (identity.flags & LEONOS_MACHINE_IDENTITY_FLAG_BOOT_PARTITION_GUID) &&
               identity.boot_disk_guid[0] &&
               identity.boot_partition_guid[0]) {
        hash_text(&ctx, "boot-gpt|");
        hash_text(&ctx, identity.boot_disk_guid);
        hash_text(&ctx, "|");
        hash_text(&ctx, identity.boot_partition_guid);
    } else {
        copy_text(out, cap, "");
        return -1;
    }

    sha256_final(&ctx, digest);
    digest_to_hex(digest, 16U, hex, sizeof(hex));

    pos = 0;
    out[0] = 0;
    append_text(out, &pos, cap, "L4M-");
    append_text(out, &pos, cap, hex);
    return 0;
}

static void local_mac(const char *mode, const char *email_hash,
                      const char *install_id, const char *key_hash,
                      char *out, uint32_t cap)
{
    char payload[256];
    uint32_t pos = 0;
    uint8_t digest[32];
    payload[0] = 0;
    append_text(payload, &pos, sizeof(payload), mode);
    append_text(payload, &pos, sizeof(payload), "|");
    append_text(payload, &pos, sizeof(payload), email_hash);
    append_text(payload, &pos, sizeof(payload), "|");
    append_text(payload, &pos, sizeof(payload), install_id);
    append_text(payload, &pos, sizeof(payload), "|");
    append_text(payload, &pos, sizeof(payload), key_hash);
    hmac_sha256_text(LOCAL_SECRET, payload, digest);
    bytes_to_hex64(digest, out, cap);
}

static int write_license(const char *mode, const char *email_hash,
                         const char *install_id, const char *key_hash)
{
    char mac[SHA256_HEX_LEN];
    char body[512];
    uint32_t pos = 0;
    local_mac(mode, email_hash, install_id, key_hash, mac, sizeof(mac));
    body[0] = 0;
    append_text(body, &pos, sizeof(body), "version=1\nmode=");
    append_text(body, &pos, sizeof(body), mode);
    append_text(body, &pos, sizeof(body), "\nemail_hash=");
    append_text(body, &pos, sizeof(body), email_hash);
    append_text(body, &pos, sizeof(body), "\ninstall_id=");
    append_text(body, &pos, sizeof(body), install_id);
    append_text(body, &pos, sizeof(body), "\nkey_hash=");
    append_text(body, &pos, sizeof(body), key_hash);
    append_text(body, &pos, sizeof(body), "\nmac=");
    append_text(body, &pos, sizeof(body), mac);
    append_text(body, &pos, sizeof(body), "\n");
    return write_file_text(LICENSE_PATH, body);
}

int leonos_license_status(struct leonos_license_info *info)
{
    char body[640];
    char version[8];
    char mode[16];
    char email_hash[24];
    char install_id[LEONOS_LICENSE_INSTALL_ID_LEN];
    char current_install_id[LEONOS_LICENSE_INSTALL_ID_LEN];
    char key_hash[SHA256_HEX_LEN + 8U];
    char mac[SHA256_HEX_LEN + 8U];
    char expected[SHA256_HEX_LEN];
    if (info) {
        *info = (struct leonos_license_info){0};
    }
    if (read_file_text(LICENSE_PATH, body, sizeof(body)) < 0) {
        if (info) {
            info->status = LEONOS_LICENSE_STATUS_MISSING;
            copy_text(info->detail, sizeof(info->detail), "license missing");
        }
        return 0;
    }
    if (!config_value(body, "version", version, sizeof(version)) ||
        !config_value(body, "mode", mode, sizeof(mode)) ||
        !config_value(body, "email_hash", email_hash, sizeof(email_hash)) ||
        !config_value(body, "install_id", install_id, sizeof(install_id)) ||
        !config_value(body, "key_hash", key_hash, sizeof(key_hash)) ||
        !config_value(body, "mac", mac, sizeof(mac)) ||
        !text_eq(version, "1")) {
        if (info) {
            info->status = LEONOS_LICENSE_STATUS_INVALID;
            copy_text(info->detail, sizeof(info->detail), "license malformed");
        }
        return 0;
    }
    if (!text_eq(mode, "online") && !text_eq(mode, "offline")) {
        if (info) {
            info->status = LEONOS_LICENSE_STATUS_INVALID;
            copy_text(info->detail, sizeof(info->detail), "license mode invalid");
        }
        return 0;
    }
    local_mac(mode, email_hash, install_id, key_hash, expected, sizeof(expected));
    if (!text_eq(mac, expected)) {
        if (info) {
            info->status = LEONOS_LICENSE_STATUS_INVALID;
            copy_text(info->detail, sizeof(info->detail), "license signature invalid");
        }
        return 0;
    }
    if (leonos_license_install_id(current_install_id, sizeof(current_install_id)) < 0 ||
        !current_install_id[0]) {
        if (info) {
            info->status = LEONOS_LICENSE_STATUS_INVALID;
            copy_text(info->detail, sizeof(info->detail), "machine identity unavailable");
        }
        return 0;
    }
    if (!text_eq(current_install_id, install_id)) {
        if (info) {
            info->status = LEONOS_LICENSE_STATUS_INVALID;
            copy_text(info->detail, sizeof(info->detail), "license machine mismatch");
        }
        return 0;
    }
    if (info) {
        info->status = LEONOS_LICENSE_STATUS_OK;
        copy_text(info->mode, sizeof(info->mode), mode);
        copy_text(info->email_hash, sizeof(info->email_hash), email_hash);
        copy_text(info->install_id, sizeof(info->install_id), install_id);
        copy_text(info->detail, sizeof(info->detail), "licensed");
    }
    return 0;
}

static void key_hash_text(const char *key, char *out, uint32_t cap)
{
    uint8_t digest[32];
    sha256_text(key ? key : "", digest);
    bytes_to_hex64(digest, out, cap);
}

static int online_key_format_ok(const char *key)
{
    char trimmed[LEONOS_LICENSE_KEY_LEN];
    trim_copy(trimmed, sizeof(trimmed), key);
    for (uint32_t i = 0; i < 17U; ++i) {
        char ch = trimmed[i];
        if (i == 5U || i == 11U) {
            if (ch != '-') {
                return 0;
            }
            continue;
        }
        if (!((ch >= '0' && ch <= '9') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z'))) {
            return 0;
        }
    }
    return trimmed[17] == 0;
}

static int server_is_localhost(const char *server)
{
    static const char prefix[] = "http://localhost";
    uint32_t i = 0;
    while (prefix[i]) {
        if (!server || lower_ascii(server[i]) != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return server[i] == 0 || server[i] == ':' || server[i] == '/';
}

static uint32_t server_port_or_default(const char *server)
{
    uint32_t i = 0;
    uint32_t port = 0;
    if (!server) {
        return 80U;
    }
    while (server[i] && server[i] != ':' && server[i] != '/') {
        ++i;
    }
    if (server[i] != ':') {
        return 80U;
    }
    ++i;
    while (server[i] >= '0' && server[i] <= '9') {
        port = port * 10U + (uint32_t)(server[i] - '0');
        ++i;
    }
    return port ? port : 80U;
}

static void append_ipv4(char *dst, uint32_t *pos, uint32_t cap, uint32_t ip)
{
    append_u32_dec(dst, pos, cap, (ip >> 24) & 0xffU);
    append_char(dst, pos, cap, '.');
    append_u32_dec(dst, pos, cap, (ip >> 16) & 0xffU);
    append_char(dst, pos, cap, '.');
    append_u32_dec(dst, pos, cap, (ip >> 8) & 0xffU);
    append_char(dst, pos, cap, '.');
    append_u32_dec(dst, pos, cap, ip & 0xffU);
}

static int build_license_url_for_ip(char *out, uint32_t cap,
                                    uint32_t ip, uint32_t port)
{
    uint32_t pos = 0;
    if (!out || cap == 0 || ip == 0) {
        return -1;
    }
    out[0] = 0;
    append_text(out, &pos, cap, "http://");
    append_ipv4(out, &pos, cap, ip);
    if (port != 80U) {
        append_char(out, &pos, cap, ':');
        append_u32_dec(out, &pos, cap, port);
    }
    append_text(out, &pos, cap, "/api/activate");
    return out[0] ? 0 : -1;
}

static int post_online_activation(const char *url, const char *body,
                                  struct leonos_http_response *response,
                                  char *response_body,
                                  uint32_t response_body_cap,
                                  char *response_headers,
                                  uint32_t response_headers_cap)
{
    struct leonos_http_request request;
    int http_ret;
    if (response_body && response_body_cap) {
        response_body[0] = 0;
    }
    if (response_headers && response_headers_cap) {
        response_headers[0] = 0;
    }
    if (response) {
        *response = (struct leonos_http_response){0};
    }
    LICENSE_LOG("[license] POST %s body_len=%d\n", url, (int)strlen(body));
    request = (struct leonos_http_request){
        .url = url,
        .method = "POST",
        .extra_headers = "Content-Type: application/x-www-form-urlencoded\r\n",
        .request_body = body,
        .request_body_len = (uint32_t)strlen(body),
        .timeout_ms = LEONOS_HTTP_DEFAULT_TIMEOUT_MS,
        .max_redirects = 0,
        .response_body = response_body,
        .response_body_capacity = response_body_cap,
        .response_headers = response_headers,
        .response_headers_capacity = response_headers_cap,
    };
    http_ret = leonos_http_request(&request, response);
    LICENSE_LOG("[license] HTTP ret=%d net_status=%d http_status=%d body_len=%d redirects=%d flags=0x%x\n",
                http_ret, response ? (int)response->net_status : -1,
                response ? (int)response->http_status : -1,
                response ? (int)response->body_len : -1,
                response ? (int)response->redirect_count : -1,
                response ? response->flags : 0U);
    if (response && response->net_status == LEONOS_NET_STATUS_OK &&
        response_body && response_body[0]) {
        LICENSE_LOG("[license] response: %s\n", response_body);
    }
    return http_ret;
}

static int retry_localhost_activation(const char *server, const char *body,
                                      const struct leonos_net_config *config,
                                      struct leonos_http_response *response,
                                      char *response_body,
                                      uint32_t response_body_cap,
                                      char *response_headers,
                                      uint32_t response_headers_cap)
{
    uint32_t port = server_port_or_default(server + 7U);
    uint32_t candidates[2];
    uint32_t count = 0;
    uint32_t network;
    uint32_t host_one = 0;
    char retry_url[LEONOS_LICENSE_SERVER_URL_LEN + 24U];
    int http_ret = 0;
    if (!config) {
        return 0;
    }
    if (config->gateway_ip) {
        candidates[count++] = config->gateway_ip;
    }
    network = config->subnet_mask ? (config->local_ip & config->subnet_mask)
                                  : (config->local_ip & 0xffffff00U);
    if (network) {
        host_one = network | 1U;
        if (host_one && host_one != config->local_ip &&
            host_one != config->gateway_ip) {
            candidates[count++] = host_one;
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (build_license_url_for_ip(retry_url, sizeof(retry_url),
                                     candidates[i], port) < 0) {
            continue;
        }
        LICENSE_LOG("[license] localhost DNS failed; retry candidate %d url=%s\n",
                    (int)i, retry_url);
        http_ret = post_online_activation(retry_url, body, response,
                                          response_body, response_body_cap,
                                          response_headers,
                                          response_headers_cap);
        if (http_ret == 0 &&
            response->net_status == LEONOS_NET_STATUS_OK &&
            response->http_status >= 200U && response->http_status < 300U) {
            return http_ret;
        }
    }
    return http_ret;
}

int leonos_license_activate_online(const char *email, const char *key,
                                   char *detail, uint32_t detail_cap)
{
    char server[LEONOS_LICENSE_SERVER_URL_LEN];
    char install_id[LEONOS_LICENSE_INSTALL_ID_LEN];
    char url[LEONOS_LICENSE_SERVER_URL_LEN + 24U];
    char body[256];
    char response_body[HTTP_BODY_MAX];
    char response_headers[HTTP_HEADERS_MAX];
    char ok[8];
    char status[64];
    char email_hash[16];
    char key_hash[SHA256_HEX_LEN];
    uint32_t pos = 0;
    int http_ret;
    int write_ret;
    struct leonos_net_config net_config;
    struct leonos_http_response response;
    int have_net_config = 0;
    LICENSE_LOG_LINE("[license] online activation requested");
    if (!email || !key || !email[0] || !key[0]) {
        copy_text(detail, detail_cap, "email and key required");
        LICENSE_LOG_LINE("[license] online activation denied: missing email or key");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    if (!online_key_format_ok(key)) {
        copy_text(detail, detail_cap, "online key format invalid");
        LICENSE_LOG_LINE("[license] online activation denied: key format invalid");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    leonos_license_default_server(server, sizeof(server));
    if (leonos_license_install_id(install_id, sizeof(install_id)) < 0 ||
        !install_id[0]) {
        copy_text(detail, detail_cap, "machine identity unavailable");
        LICENSE_LOG_LINE("[license] online activation denied: machine identity unavailable");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    LICENSE_LOG("[license] default server=%s machine_id=%s\n", server, install_id);
    net_config = (struct leonos_net_config){0};
    if (leonos_net_config(&net_config) == 0) {
        have_net_config = 1;
        LICENSE_LOG("[license] net config source=%d flags=0x%x ip=0x%x mask=0x%x gateway=0x%x dns=0x%x dhcp=0x%x\n",
                    (int)net_config.source, net_config.flags,
                    net_config.local_ip, net_config.subnet_mask,
                    net_config.gateway_ip, net_config.dns_ip,
                    net_config.dhcp_server_ip);
    } else {
        LICENSE_LOG_LINE("[license] net config unavailable");
    }
    copy_text(url, sizeof(url), server);
    pos = (uint32_t)strlen(url);
    if (pos && url[pos - 1U] == '/') {
        --pos;
        url[pos] = 0;
    }
    append_text(url, &pos, sizeof(url), "/api/activate");
    pos = 0;
    body[0] = 0;
    append_text(body, &pos, sizeof(body), "email=");
    url_encode(body, &pos, sizeof(body), email);
    append_text(body, &pos, sizeof(body), "&key=");
    url_encode(body, &pos, sizeof(body), key);
    append_text(body, &pos, sizeof(body), "&install_id=");
    url_encode(body, &pos, sizeof(body), install_id);
    http_ret = post_online_activation(url, body, &response,
                                      response_body, sizeof(response_body),
                                      response_headers,
                                      sizeof(response_headers));
    if (http_ret == 0 &&
        response.net_status == LEONOS_NET_STATUS_DNS_NO_ANSWER &&
        server_is_localhost(server) && have_net_config) {
        http_ret = retry_localhost_activation(server, body, &net_config,
                                              &response, response_body,
                                              sizeof(response_body),
                                              response_headers,
                                              sizeof(response_headers));
    }
    if (http_ret < 0 ||
        response.net_status != LEONOS_NET_STATUS_OK ||
        response.http_status < 200U || response.http_status >= 300U) {
        copy_text(detail, detail_cap, "network activation failed");
        LICENSE_LOG_LINE("[license] online activation failed before server acceptance");
        return LEONOS_LICENSE_STATUS_NETWORK;
    }
    ok[0] = 0;
    status[0] = 0;
    response_value(response_body, "ok", ok, sizeof(ok));
    response_value(response_body, "status", status, sizeof(status));
    if (!text_eq(ok, "1")) {
        copy_text(detail, detail_cap, status[0] ? status : "activation denied");
        LICENSE_LOG("[license] online activation denied by server: ok=%s status=%s\n",
                    ok, status);
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    email_hash36(email, email_hash, sizeof(email_hash));
    key_hash_text(key, key_hash, sizeof(key_hash));
    write_ret = write_license("online", email_hash, install_id, key_hash);
    if (write_ret < 0) {
        copy_text(detail, detail_cap, "could not save license");
        LICENSE_LOG("[license] online activation save failed ret=%d\n", write_ret);
        return LEONOS_LICENSE_STATUS_INVALID;
    }
    copy_text(detail, detail_cap, status[0] ? status : "activated");
    LICENSE_LOG_LINE("[license] online activation saved license.dat");
    return LEONOS_LICENSE_STATUS_OK;
}

int leonos_license_activate_offline(const char *email, const char *offline_key,
                                    char *detail, uint32_t detail_cap)
{
    char trimmed[LEONOS_LICENSE_KEY_LEN];
    char clean[LEONOS_LICENSE_KEY_LEN];
    char payload[40];
    char start[9];
    char end[9];
    char want_email[16];
    char got_email[16];
    char want_mac[16];
    char got_mac[16];
    char today[9];
    char install_id[LEONOS_LICENSE_INSTALL_ID_LEN];
    char key_hash[SHA256_HEX_LEN];
    struct leonos_time_info time;
    uint32_t src = 0;
    uint32_t dst = 0;
    if (!email || !offline_key || !email[0] || !offline_key[0]) {
        copy_text(detail, detail_cap, "email and offline key required");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    trim_copy(trimmed, sizeof(trimmed), offline_key);
    while (trimmed[src] && dst + 1U < sizeof(clean)) {
        char ch = upper_ascii(trimmed[src++]);
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z'))) {
            copy_text(detail, detail_cap, "offline key characters invalid");
            return LEONOS_LICENSE_STATUS_DENIED;
        }
        clean[dst++] = ch;
    }
    clean[dst] = 0;
    if (dst != 50U || clean[0] != 'A') {
        copy_text(detail, detail_cap, "offline key format invalid");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    for (uint32_t i = 0; i < 39U; ++i) {
        payload[i] = clean[i];
    }
    payload[39] = 0;
    for (uint32_t i = 0; i < 8U; ++i) {
        start[i] = clean[1U + i];
        end[i] = clean[9U + i];
    }
    start[8] = 0;
    end[8] = 0;
    if (!valid_yyyymmdd(start) || !valid_yyyymmdd(end) ||
        text_cmp_n(start, end, 8U) > 0) {
        copy_text(detail, detail_cap, "offline key dates invalid");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    for (uint32_t i = 0; i < 10U; ++i) {
        got_email[i] = clean[17U + i];
    }
    got_email[10] = 0;
    email_hash36(email, want_email, sizeof(want_email));
    if (!text_eq(want_email, got_email)) {
        copy_text(detail, detail_cap, "offline key email mismatch");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    for (uint32_t i = 0; i < 11U; ++i) {
        got_mac[i] = clean[39U + i];
    }
    got_mac[11] = 0;
    offline_mac(payload, want_mac, sizeof(want_mac));
    if (!text_eq(want_mac, got_mac)) {
        copy_text(detail, detail_cap, "offline key checksum invalid");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    time = (struct leonos_time_info){0};
    if (leonos_time_info(&time) < 0 || !time.valid) {
        copy_text(detail, detail_cap, "RTC clock unavailable");
        return LEONOS_LICENSE_STATUS_CLOCK;
    }
    time_date_text(&time, today, sizeof(today));
    if (!valid_yyyymmdd(today)) {
        copy_text(detail, detail_cap, "RTC date invalid");
        return LEONOS_LICENSE_STATUS_CLOCK;
    }
    if (text_cmp_n(today, start, 8U) < 0) {
        copy_text(detail, detail_cap, "offline key not valid yet");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    if (text_cmp_n(today, end, 8U) > 0) {
        copy_text(detail, detail_cap, "offline key expired");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    if (leonos_license_install_id(install_id, sizeof(install_id)) < 0 ||
        !install_id[0]) {
        copy_text(detail, detail_cap, "machine identity unavailable");
        return LEONOS_LICENSE_STATUS_DENIED;
    }
    key_hash_text(clean, key_hash, sizeof(key_hash));
    if (write_license("offline", want_email, install_id, key_hash) < 0) {
        copy_text(detail, detail_cap, "could not save license");
        return LEONOS_LICENSE_STATUS_INVALID;
    }
    copy_text(detail, detail_cap, "offline activated");
    return LEONOS_LICENSE_STATUS_OK;
}
