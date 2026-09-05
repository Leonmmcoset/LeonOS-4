#include <leonos/fs.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>

#include "installer_sha256.h"

#define INSTALLER_SHA256_COPY_BUF_SIZE (64U * 1024U)

/* Keep the file hash scratch space out of the installer's small user stack. */
static uint8_t installer_sha256_buffer[INSTALLER_SHA256_COPY_BUF_SIZE];

struct installer_sha256_ctx {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
};

static const uint32_t installer_sha256_k[64] = {
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

static uint32_t installer_sha256_rotr(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void installer_sha256_transform(struct installer_sha256_ctx *ctx,
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
        uint32_t s0 = installer_sha256_rotr(m[i - 15], 7) ^
                      installer_sha256_rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = installer_sha256_rotr(m[i - 2], 17) ^
                      installer_sha256_rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t s1 = installer_sha256_rotr(e, 6) ^ installer_sha256_rotr(e, 11) ^
                      installer_sha256_rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + installer_sha256_k[i] + m[i];
        uint32_t s0 = installer_sha256_rotr(a, 2) ^ installer_sha256_rotr(a, 13) ^
                      installer_sha256_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void installer_sha256_init(struct installer_sha256_ctx *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
}

static void installer_sha256_update(struct installer_sha256_ctx *ctx,
                                    const void *input, uint32_t len)
{
    const uint8_t *data = (const uint8_t *)input;
    for (uint32_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            installer_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void installer_sha256_final(struct installer_sha256_ctx *ctx,
                                   uint8_t hash[INSTALLER_SHA256_HASH_LEN])
{
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80u;
        while (i < 56) ctx->data[i++] = 0;
    } else {
        ctx->data[i++] = 0x80u;
        while (i < 64) ctx->data[i++] = 0;
        installer_sha256_transform(ctx, ctx->data);
        for (i = 0; i < 56; ++i) ctx->data[i] = 0;
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8u;
    for (i = 0; i < 8; ++i) {
        ctx->data[63 - i] = (uint8_t)(ctx->bitlen >> (i * 8));
    }
    installer_sha256_transform(ctx, ctx->data);
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

static int installer_hash_file(const char *path, uint8_t hash[INSTALLER_SHA256_HASH_LEN])
{
    struct installer_sha256_ctx ctx;
    int fd = open(path, LEONOS_O_RDONLY, 0);
    long got;
    if (fd < 0) return fd;
    installer_sha256_init(&ctx);
    while ((got = read(fd, installer_sha256_buffer,
                       sizeof(installer_sha256_buffer))) > 0) {
        installer_sha256_update(&ctx, installer_sha256_buffer, (uint32_t)got);
    }
    close(fd);
    if (got < 0) return (int)got;
    installer_sha256_final(&ctx, hash);
    return 0;
}

static int installer_hash_equal(const uint8_t *left, const uint8_t *right, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i) {
        if (left[i] != right[i]) return 0;
    }
    return 1;
}

int installer_files_equal(const char *source, const char *target,
                          uint8_t *out_missing, uint8_t *out_diff)
{
    uint8_t source_hash[INSTALLER_SHA256_HASH_LEN];
    uint8_t target_hash[INSTALLER_SHA256_HASH_LEN];
    struct leonos_stat source_st;
    struct leonos_stat target_st;
    int ret;
    if (out_missing) *out_missing = 0;
    if (out_diff) *out_diff = 1;
    if (leonos_stat_legacy(source, &source_st) < 0 || source_st.type != LEONOS_FS_TYPE_FILE) return -2;
    if (leonos_stat_legacy(target, &target_st) < 0 || target_st.type != LEONOS_FS_TYPE_FILE) {
        if (out_missing) *out_missing = 1;
        return 0;
    }
    ret = installer_hash_file(source, source_hash);
    if (ret < 0) return ret;
    ret = installer_hash_file(target, target_hash);
    if (ret < 0) return ret;
    if (!installer_hash_equal(source_hash, target_hash, INSTALLER_SHA256_HASH_LEN)) return 0;
    if (out_diff) *out_diff = 0;
    return 0;
}
