/*
 * LeonOS osmlayer interface: declares the kernel-to-middle-layer ABI.
 * Defines VFS, identity, device, mount, and policy requests and responses.
 */
#ifndef NTCLKS_OSMLAYER_H
#define NTCLKS_OSMLAYER_H

#include <leonos/boot_handoff.h>
#include <leonos/auth.h>
#include <leonos/text.h>
#include <ntclks/multiboot2.h>
#include <ntclks/syscall.h>
#include <ntclks/types.h>

struct osmlayer_boot_summary {
    uint32_t abi_version;
    uint32_t module_count;
    uint64_t memory_kib;
    uint32_t root_drive;
};

/**
 * @brief Coordinates the osmlayer bridge init operation.
 * @param boot Boot information supplied by the loader.
 * @param handoff Input or output value used by this operation.
 */
void osmlayer_bridge_init(const struct boot_info *boot,
                          const struct leonos_boot_handoff *handoff);
/**
 * @brief Coordinates the osmlayer bridge mount policy operation.
 * @param boot Boot information supplied by the loader.
 * @param policy Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_bridge_mount_policy(const struct boot_info *boot,
                                 struct leonos_mount_policy *policy);
/**
 * @brief Coordinates the osmlayer unicode layout utf8 operation.
 * @param layout Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_unicode_layout_utf8(struct leonos_text_layout *layout);
/**
 * @brief Coordinates the osmlayer unicode utf8 to utf16le operation.
 * @param cmd Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_unicode_utf8_to_utf16le(struct leonos_unicode_utf8_to_utf16 *cmd);
/**
 * @brief Coordinates the osmlayer unicode utf16le to utf8 operation.
 * @param cmd Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_unicode_utf16le_to_utf8(struct leonos_unicode_utf16_to_utf8 *cmd);
/**
 * @brief Coordinates the osmlayer unicode safe truncate utf8 operation.
 * @param text Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @return Result, status, or value defined by this API.
 */
uint32_t osmlayer_unicode_safe_truncate_utf8(const char *text, uint32_t cap);
/**
 * @brief Coordinates the osmlayer vfs resolve path operation.
 * @param query Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_vfs_resolve_path(struct leonos_vfs_resolve_path *query);
/**
 * @brief Coordinates the osmlayer device catalog operation.
 * @param query Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_device_catalog(struct leonos_device_catalog_query *query);
/**
 * @brief Coordinates the osmlayer auth op operation.
 * @param op Input or output value used by this operation.
 * @param arg Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int osmlayer_auth_op(uint32_t op, void *arg);
/**
 * @brief Coordinates the osmlayer bridge syscall operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @return Result, status, or value defined by this API.
 */
int64_t osmlayer_bridge_syscall(const struct syscall_frame *frame);
/**
 * @brief Coordinates the osmlayer bridge selftest operation.
 */
void osmlayer_bridge_selftest(void);

#endif
