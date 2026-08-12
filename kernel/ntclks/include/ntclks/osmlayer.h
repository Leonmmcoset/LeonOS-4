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

void osmlayer_bridge_init(const struct boot_info *boot,
                          const struct leonos_boot_handoff *handoff);
int osmlayer_bridge_mount_policy(const struct boot_info *boot,
                                 struct leonos_mount_policy *policy);
int osmlayer_unicode_layout_utf8(struct leonos_text_layout *layout);
int osmlayer_unicode_utf8_to_utf16le(struct leonos_unicode_utf8_to_utf16 *cmd);
int osmlayer_unicode_utf16le_to_utf8(struct leonos_unicode_utf16_to_utf8 *cmd);
uint32_t osmlayer_unicode_safe_truncate_utf8(const char *text, uint32_t cap);
int osmlayer_vfs_resolve_path(struct leonos_vfs_resolve_path *query);
int osmlayer_device_catalog(struct leonos_device_catalog_query *query);
int osmlayer_auth_op(uint32_t op, void *arg);
int64_t osmlayer_bridge_syscall(const struct syscall_frame *frame);
void osmlayer_bridge_selftest(void);

#endif
