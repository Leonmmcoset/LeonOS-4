/*
 * LeonOS osmlayer bridge: connects kernel services to the Rust middle layer.
 * Marshals VFS, account, device, mount, and policy operations across the ABI.
 */
#include <ntclks/console.h>
#include <ntclks/mm.h>
#include <ntclks/osmlayer.h>
#include <ntclks/storage.h>

static struct osmlayer_boot_summary summary;
static const struct leonos_middlelayer_api *api;
static struct osmlayer_boot_summary (*middlelayer_init)(const struct boot_info *boot);
static int64_t (*middlelayer_syscall)(const struct syscall_frame *frame);
static uint32_t (*middlelayer_selftest)(void);
static int32_t (*middlelayer_mount_policy)(const struct boot_info *boot,
                                           struct leonos_mount_policy *policy);
static int32_t (*middlelayer_unicode_op)(uint32_t op, void *arg);
static int32_t (*middlelayer_vfs_op)(uint32_t op, void *arg);
static int32_t (*middlelayer_device_catalog)(struct leonos_device_catalog_query *query);
static int32_t (*middlelayer_auth_op)(uint32_t op, void *arg);
static struct leonos_kernel_services kernel_services;

/**
 * @brief Reports whether utf8 cont.
 * @param byte Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int is_utf8_cont(uint8_t byte)
{
    return (byte & 0xc0u) == 0x80u;
}

/**
 * @brief Coordinates the fallback decode utf8 operation.
 * @param text Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @param off Input or output value used by this operation.
 * @param byte_len Length, size, or element count associated with the operation.
 * @param valid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint32_t fallback_decode_utf8(const char *text, uint32_t len,
                                     uint32_t off, uint32_t *byte_len,
                                     int *valid)
{
    const uint8_t *s = (const uint8_t *)text;
    uint8_t b0;
    if (byte_len) {
        *byte_len = 1;
    }
    if (valid) {
        *valid = 0;
    }
    if (!text || off >= len) {
        return LEONOS_TEXT_REPLACEMENT_CHAR;
    }
    b0 = s[off];
    if (b0 < 0x80u) {
        if (valid) {
            *valid = 1;
        }
        return b0;
    }
    if (b0 < 0xc2u) {
        return LEONOS_TEXT_REPLACEMENT_CHAR;
    }
    if (b0 < 0xe0u) {
        if (off + 1u >= len || !is_utf8_cont(s[off + 1u])) {
            return LEONOS_TEXT_REPLACEMENT_CHAR;
        }
        if (byte_len) {
            *byte_len = 2;
        }
        if (valid) {
            *valid = 1;
        }
        return ((uint32_t)(b0 & 0x1fu) << 6) | (uint32_t)(s[off + 1u] & 0x3fu);
    }
    if (b0 < 0xf0u) {
        uint8_t b1;
        uint8_t b2;
        if (off + 2u >= len) {
            return LEONOS_TEXT_REPLACEMENT_CHAR;
        }
        b1 = s[off + 1u];
        b2 = s[off + 2u];
        if (!is_utf8_cont(b1) || !is_utf8_cont(b2) ||
            (b0 == 0xe0u && b1 < 0xa0u) ||
            (b0 == 0xedu && b1 >= 0xa0u)) {
            return LEONOS_TEXT_REPLACEMENT_CHAR;
        }
        if (byte_len) {
            *byte_len = 3;
        }
        if (valid) {
            *valid = 1;
        }
        return ((uint32_t)(b0 & 0x0fu) << 12) |
               ((uint32_t)(b1 & 0x3fu) << 6) |
               (uint32_t)(b2 & 0x3fu);
    }
    if (b0 < 0xf5u) {
        uint8_t b1;
        uint8_t b2;
        uint8_t b3;
        if (off + 3u >= len) {
            return LEONOS_TEXT_REPLACEMENT_CHAR;
        }
        b1 = s[off + 1u];
        b2 = s[off + 2u];
        b3 = s[off + 3u];
        if (!is_utf8_cont(b1) || !is_utf8_cont(b2) || !is_utf8_cont(b3) ||
            (b0 == 0xf0u && b1 < 0x90u) ||
            (b0 == 0xf4u && b1 >= 0x90u)) {
            return LEONOS_TEXT_REPLACEMENT_CHAR;
        }
        if (byte_len) {
            *byte_len = 4;
        }
        if (valid) {
            *valid = 1;
        }
        return ((uint32_t)(b0 & 0x07u) << 18) |
               ((uint32_t)(b1 & 0x3fu) << 12) |
               ((uint32_t)(b2 & 0x3fu) << 6) |
               (uint32_t)(b3 & 0x3fu);
    }
    return LEONOS_TEXT_REPLACEMENT_CHAR;
}

/**
 * @brief Coordinates the fallback is wide operation.
 * @param cp Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int fallback_is_wide(uint32_t cp)
{
    return (cp >= 0x1100u && cp <= 0x115fu) ||
           cp == 0x2329u || cp == 0x232au ||
           (cp >= 0x2e80u && cp <= 0xa4cfu) ||
           (cp >= 0xac00u && cp <= 0xd7a3u) ||
           (cp >= 0xf900u && cp <= 0xfaffu) ||
           (cp >= 0x20000u && cp <= 0x3fffdu) ||
           (cp >= 0xfe10u && cp <= 0xfe19u) ||
           (cp >= 0xfe30u && cp <= 0xfe6fu) ||
           (cp >= 0xff00u && cp <= 0xff60u) ||
           (cp >= 0xffe0u && cp <= 0xffe6u);
}

/**
 * @brief Coordinates the fallback cell width operation.
 * @param cp Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint32_t fallback_cell_width(uint32_t cp)
{
    if (cp == 0 || cp == '\n' || cp == '\r') {
        return 0;
    }
    if (cp == '\t') {
        return 4;
    }
    return fallback_is_wide(cp) ? 2u : 1u;
}

/**
 * @brief Coordinates the fallback unicode layout operation.
 * @param layout Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int fallback_unicode_layout(struct leonos_text_layout *layout)
{
    uint32_t off = 0;
    uint32_t count = 0;
    uint32_t total = 0;
    if (!layout || !layout->text || (layout->capacity && !layout->glyphs)) {
        return -22;
    }
    while (off < layout->byte_len) {
        uint32_t len = 1;
        int valid = 0;
        uint32_t cp = fallback_decode_utf8(layout->text, layout->byte_len, off, &len, &valid);
        uint32_t cells = fallback_cell_width(cp);
        (void)valid;
        if (count < layout->capacity) {
            layout->glyphs[count].codepoint = cp;
            layout->glyphs[count].byte_offset = off;
            layout->glyphs[count].byte_len = len;
            layout->glyphs[count].cell_width = cells;
            layout->glyphs[count].pixel_width = cells * 8u;
        }
        total += cells;
        ++count;
        off += len;
    }
    layout->count = count;
    layout->total_cells = total;
    layout->total_px = total * 8u;
    return 0;
}

/**
 * @brief Coordinates the fallback unicode utf8 to utf16 operation.
 * @param cmd Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int fallback_unicode_utf8_to_utf16(struct leonos_unicode_utf8_to_utf16 *cmd)
{
    uint32_t off = 0;
    uint32_t out = 0;
    if (!cmd || !cmd->utf8 || (cmd->utf16_capacity && !cmd->utf16)) {
        return -22;
    }
    while (off < cmd->utf8_len) {
        uint32_t len = 1;
        uint32_t cp = fallback_decode_utf8(cmd->utf8, cmd->utf8_len, off, &len, 0);
        if (cp <= 0xffffu) {
            if (out < cmd->utf16_capacity) {
                cmd->utf16[out] = (uint16_t)cp;
            }
            ++out;
        } else {
            uint32_t value = cp - 0x10000u;
            if (out < cmd->utf16_capacity) {
                cmd->utf16[out] = (uint16_t)(0xd800u | (value >> 10));
            }
            ++out;
            if (out < cmd->utf16_capacity) {
                cmd->utf16[out] = (uint16_t)(0xdc00u | (value & 0x3ffu));
            }
            ++out;
        }
        off += len;
    }
    cmd->utf16_len = out;
    return 0;
}

/**
 * @brief Coordinates the fallback encode utf8 operation.
 * @param cp Input or output value used by this operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param pos Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint32_t fallback_encode_utf8(uint32_t cp, char *out, uint32_t cap, uint32_t pos)
{
    if (cp > 0x10ffffu) {
        cp = LEONOS_TEXT_REPLACEMENT_CHAR;
    }
    if (cp < 0x80u) {
        if (pos < cap) {
            out[pos] = (char)cp;
        }
        return 1;
    }
    if (cp < 0x800u) {
        if (pos < cap) {
            out[pos] = (char)(0xc0u | (cp >> 6));
        }
        if (pos + 1u < cap) {
            out[pos + 1u] = (char)(0x80u | (cp & 0x3fu));
        }
        return 2;
    }
    if (cp < 0x10000u) {
        if (pos < cap) {
            out[pos] = (char)(0xe0u | (cp >> 12));
        }
        if (pos + 1u < cap) {
            out[pos + 1u] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        }
        if (pos + 2u < cap) {
            out[pos + 2u] = (char)(0x80u | (cp & 0x3fu));
        }
        return 3;
    }
    if (pos < cap) {
        out[pos] = (char)(0xf0u | (cp >> 18));
    }
    if (pos + 1u < cap) {
        out[pos + 1u] = (char)(0x80u | ((cp >> 12) & 0x3fu));
    }
    if (pos + 2u < cap) {
        out[pos + 2u] = (char)(0x80u | ((cp >> 6) & 0x3fu));
    }
    if (pos + 3u < cap) {
        out[pos + 3u] = (char)(0x80u | (cp & 0x3fu));
    }
    return 4;
}

/**
 * @brief Coordinates the fallback unicode utf16 to utf8 operation.
 * @param cmd Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int fallback_unicode_utf16_to_utf8(struct leonos_unicode_utf16_to_utf8 *cmd)
{
    uint32_t i = 0;
    uint32_t out = 0;
    if (!cmd || !cmd->utf16 || (cmd->utf8_capacity && !cmd->utf8)) {
        return -22;
    }
    while (i < cmd->utf16_len) {
        uint16_t unit = cmd->utf16[i];
        uint32_t cp;
        if (unit >= 0xd800u && unit <= 0xdbffu) {
            if (i + 1u < cmd->utf16_len && cmd->utf16[i + 1u] >= 0xdc00u &&
                cmd->utf16[i + 1u] <= 0xdfffu) {
                cp = 0x10000u + ((((uint32_t)unit - 0xd800u) << 10) |
                                  ((uint32_t)cmd->utf16[i + 1u] - 0xdc00u));
                ++i;
            } else {
                cp = LEONOS_TEXT_REPLACEMENT_CHAR;
            }
        } else if (unit >= 0xdc00u && unit <= 0xdfffu) {
            cp = LEONOS_TEXT_REPLACEMENT_CHAR;
        } else {
            cp = unit;
        }
        out += fallback_encode_utf8(cp, cmd->utf8, cmd->utf8_capacity, out);
        ++i;
    }
    if (out < cmd->utf8_capacity) {
        cmd->utf8[out] = 0;
    }
    cmd->utf8_len = out;
    return 0;
}

/**
 * @brief Coordinates the osmlayer log operation.
 * @param message Input or output value used by this operation.
 */
static void osmlayer_log(const char *message)
{
    if (message) {
        console_write(message);
    }
}

/**
 * @brief Coordinates the osmlayer log len operation.
 * @param message Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 */
static void osmlayer_log_len(const char *message, uint64_t len)
{
    if (message) {
        console_write_len(message, (size_t)len);
    }
}

/**
 * @brief Coordinates the osmlayer read file service operation.
 * @param path LeonOS path consumed by this operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param out_len Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
static int32_t osmlayer_read_file_service(const char *path, void *buf,
                                          uint32_t capacity, uint32_t *out_len)
{
    const void *data = 0;
    size_t len = 0;
    int ret;
    uint32_t pages;
    if (out_len) {
        *out_len = 0;
    }
    if (!path || (!buf && capacity != 0)) {
        return -22;
    }
    ret = storage_read_file(path, &data, &len);
    if (ret < 0) {
        return ret;
    }
    if (out_len) {
        *out_len = (uint32_t)len;
    }
    if (len > capacity) {
        pages = (uint32_t)(((len ? len : 1u) + 4095u) / 4096u);
        mm_free_pages((uint64_t)(uintptr_t)data, pages);
        return -7;
    }
    for (uint32_t i = 0; i < (uint32_t)len; ++i) {
        ((uint8_t *)buf)[i] = ((const uint8_t *)data)[i];
    }
    pages = (uint32_t)(((len ? len : 1u) + 4095u) / 4096u);
    mm_free_pages((uint64_t)(uintptr_t)data, pages);
    return 0;
}

/**
 * @brief Coordinates the osmlayer write file service operation.
 * @param path LeonOS path consumed by this operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
static int32_t osmlayer_write_file_service(const char *path, const void *buf, uint32_t len)
{
    if (!path || (!buf && len != 0)) {
        return -22;
    }
    return storage_write_file(path, buf ? buf : "", len);
}

/**
 * @brief Coordinates the osmlayer mkdir service operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
static int32_t osmlayer_mkdir_service(const char *path)
{
    if (!path) {
        return -22;
    }
    return storage_mkdir(path);
}

/**
 * @brief Coordinates the osmlayer bridge init operation.
 * @param boot Boot information supplied by the loader.
 * @param handoff Input or output value used by this operation.
 */
void osmlayer_bridge_init(const struct boot_info *boot,
                          const struct leonos_boot_handoff *handoff)
{
    console_printf("[osmlayer] bridge init entering\n");
    if (!handoff || handoff->magic != LEONOS_BOOT_HANDOFF_MAGIC ||
        !handoff->middlelayer.entry) {
        console_printf("[osmlayer] no middlelayer module in loader handoff\n");
        return;
    }

    kernel_services = (struct leonos_kernel_services){
        .version = LEONOS_KERNEL_SERVICES_VERSION,
        .reserved = 0,
        .log = osmlayer_log,
        .log_len = osmlayer_log_len,
        .read_file = osmlayer_read_file_service,
        .write_file = osmlayer_write_file_service,
        .mkdir = osmlayer_mkdir_service,
    };
    leonos_middlelayer_module_init_fn module_init =
        (leonos_middlelayer_module_init_fn)(uintptr_t)handoff->middlelayer.entry;
    api = module_init(&kernel_services, handoff);
    if (!api || api->version != LEONOS_MIDDLELAYER_API_VERSION ||
        !api->init || !api->syscall || !api->selftest || !api->mount_policy ||
        !api->unicode_op || !api->vfs_op || !api->device_catalog || !api->auth_op) {
        console_printf("[osmlayer] middlelayer ABI rejected api=%p version=%u\n",
                       (void *)api,
                       api ? api->version : 0);
        api = 0;
        return;
    }
    middlelayer_init =
        (struct osmlayer_boot_summary (*)(const struct boot_info *))(uintptr_t)api->init;
    middlelayer_syscall =
        (int64_t (*)(const struct syscall_frame *))(uintptr_t)api->syscall;
    middlelayer_selftest =
        (uint32_t (*)(void))(uintptr_t)api->selftest;
    middlelayer_mount_policy =
        (int32_t (*)(const struct boot_info *,
                     struct leonos_mount_policy *))(uintptr_t)api->mount_policy;
    middlelayer_unicode_op =
        (int32_t (*)(uint32_t, void *))(uintptr_t)api->unicode_op;
    middlelayer_vfs_op =
        (int32_t (*)(uint32_t, void *))(uintptr_t)api->vfs_op;
    middlelayer_device_catalog =
        (int32_t (*)(struct leonos_device_catalog_query *))(uintptr_t)api->device_catalog;
    middlelayer_auth_op =
        (int32_t (*)(uint32_t, void *))(uintptr_t)api->auth_op;
    summary = middlelayer_init(boot);
    console_printf("[osmlayer] bridge init returned\n");
    if (summary.memory_kib == 0) {
        summary.memory_kib = mm_total_memory_kib();
    }
    console_printf("[osmlayer] init abi=%u modules=%u memory=%llu KiB root=%u:/\n",
                   summary.abi_version,
                   summary.module_count,
                   (unsigned long long)summary.memory_kib,
                   summary.root_drive);
    console_printf("[osmlayer] VFS policy owner=middlelayer root=%u:/ linux_syscall=1\n",
                   summary.root_drive);
}

/**
 * @brief Coordinates the osmlayer bridge mount policy operation.
 * @param boot Boot information supplied by the loader.
 * @param policy Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_bridge_mount_policy(const struct boot_info *boot,
                                 struct leonos_mount_policy *policy)
{
    if (!middlelayer_mount_policy || !policy) {
        return -38;
    }
    return middlelayer_mount_policy(boot, policy);
}

/**
 * @brief Coordinates the osmlayer unicode layout utf8 operation.
 * @param layout Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_unicode_layout_utf8(struct leonos_text_layout *layout)
{
    if (middlelayer_unicode_op) {
        return middlelayer_unicode_op(LEONOS_UNICODE_OP_LAYOUT_UTF8, layout);
    }
    return fallback_unicode_layout(layout);
}

/**
 * @brief Coordinates the osmlayer unicode utf8 to utf16le operation.
 * @param cmd Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_unicode_utf8_to_utf16le(struct leonos_unicode_utf8_to_utf16 *cmd)
{
    if (middlelayer_unicode_op) {
        return middlelayer_unicode_op(LEONOS_UNICODE_OP_UTF8_TO_UTF16LE, cmd);
    }
    return fallback_unicode_utf8_to_utf16(cmd);
}

/**
 * @brief Coordinates the osmlayer unicode utf16le to utf8 operation.
 * @param cmd Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_unicode_utf16le_to_utf8(struct leonos_unicode_utf16_to_utf8 *cmd)
{
    if (middlelayer_unicode_op) {
        return middlelayer_unicode_op(LEONOS_UNICODE_OP_UTF16LE_TO_UTF8, cmd);
    }
    return fallback_unicode_utf16_to_utf8(cmd);
}

/**
 * @brief Coordinates the osmlayer unicode safe truncate utf8 operation.
 * @param text Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @return Result, status, or value defined by this API.
 */
uint32_t osmlayer_unicode_safe_truncate_utf8(const char *text, uint32_t cap)
{
    uint32_t off = 0;
    uint32_t limit;
    if (!text || cap == 0) {
        return 0;
    }
    limit = cap - 1u;
    while (text[off] && off < limit) {
        uint32_t len = 1;
        int valid = 0;
        /**
 * @brief Coordinates the fallback decode utf8 operation.
 * @param text Input or output value used by this operation.
 * @param u Input or output value used by this operation.
 * @param off Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @param valid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
        (void)fallback_decode_utf8(text, limit + 1u, off, &len, &valid);
        if (!valid || off + len > limit) {
            break;
        }
        off += len;
    }
    return off;
}

/**
 * @brief Coordinates the osmlayer vfs resolve path operation.
 * @param query Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_vfs_resolve_path(struct leonos_vfs_resolve_path *query)
{
    if (!middlelayer_vfs_op || !query) {
        return -38;
    }
    return middlelayer_vfs_op(LEONOS_VFS_OP_RESOLVE_PATH, query);
}

/**
 * @brief Coordinates the osmlayer device catalog operation.
 * @param query Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_device_catalog(struct leonos_device_catalog_query *query)
{
    if (!middlelayer_device_catalog || !query) {
        return -38;
    }
    return middlelayer_device_catalog(query);
}

/**
 * @brief Coordinates the osmlayer auth op operation.
 * @param op Input or output value used by this operation.
 * @param arg Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_auth_op(uint32_t op, void *arg)
{
    if (!middlelayer_auth_op) {
        return -38;
    }
    return middlelayer_auth_op(op, arg);
}

/**
 * @brief Coordinates the osmlayer bridge syscall operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @return Result, status, or value defined by this API.
 */
int64_t osmlayer_bridge_syscall(const struct syscall_frame *frame)
{
    if (!middlelayer_syscall) {
        return -38;
    }
    return middlelayer_syscall(frame);
}

/**
 * @brief Coordinates the osmlayer bridge selftest operation.
 */
void osmlayer_bridge_selftest(void)
{
    if (!middlelayer_selftest) {
        console_printf("[osmlayer] selftest skipped: module not bound\n");
        return;
    }
    uint32_t passed = middlelayer_selftest();
    console_printf("[osmlayer] selftest passed=%u/5 (vfs fat32 ipc gui device)\n", passed);
}
