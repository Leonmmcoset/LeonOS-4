#ifndef NTCLKS_OSMLAYER_H
#define NTCLKS_OSMLAYER_H

#include <ntclks/multiboot2.h>
#include <ntclks/syscall.h>
#include <ntclks/types.h>

struct osmlayer_boot_summary {
    uint32_t abi_version;
    uint32_t module_count;
    uint64_t memory_kib;
    uint32_t root_drive;
};

void osmlayer_bridge_init(const struct boot_info *boot);
int64_t osmlayer_bridge_syscall(const struct syscall_frame *frame);
void osmlayer_bridge_selftest(void);

#endif
