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
    uint32_t reserved;
};

/**
 * @brief Wire the middle-layer bridge to the loader's boot info and handoff data.
 */
void osmlayer_bridge_init(const struct boot_info *boot,
                          const struct leonos_boot_handoff *handoff);
/**
 * @brief Fill policy with the mount layout the bridge should apply from boot info; 0 on success.
 */
int osmlayer_bridge_mount_policy(const struct boot_info *boot,
                                 struct leonos_mount_policy *policy);
/**
 * @brief Compute glyph layout metrics for a UTF-8 string into layout; 0 on success.
 */
int osmlayer_unicode_layout_utf8(struct leonos_text_layout *layout);
/**
 * @brief Convert the UTF-8 buffer described by cmd to UTF-16LE; 0 on success.
 */
int osmlayer_unicode_utf8_to_utf16le(struct leonos_unicode_utf8_to_utf16 *cmd);
/**
 * @brief Convert the UTF-16LE buffer described by cmd to UTF-8; 0 on success.
 */
int osmlayer_unicode_utf16le_to_utf8(struct leonos_unicode_utf16_to_utf8 *cmd);
/**
 * @brief Return the byte length of text that fits within cap bytes without splitting a code point.
 */
uint32_t osmlayer_unicode_safe_truncate_utf8(const char *text, uint32_t cap);
/**
 * @brief Resolve the VFS path in query and fill in its result; 0 on success.
 */
int osmlayer_vfs_resolve_path(struct leonos_vfs_resolve_path *query);
/**
 * @brief Enumerate devices matching query into its result buffer; 0 on success.
 */
int osmlayer_device_catalog(struct leonos_device_catalog_query *query);
/**
 * @brief Run the auth subsystem operation op with argument arg; 0 on success.
 */
int osmlayer_auth_op(uint32_t op, void *arg);
/**
 * @brief Route the syscall frame into the middle-layer bridge and return its result.
 */
int64_t osmlayer_bridge_syscall(const struct syscall_frame *frame);
/**
 * @brief Exercise the bridge's internal paths to verify the kernel/middle-layer ABI.
 */
void osmlayer_bridge_selftest(void);

#endif
