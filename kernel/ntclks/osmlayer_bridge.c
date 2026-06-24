#include <ntclks/console.h>
#include <ntclks/mm.h>
#include <ntclks/osmlayer.h>

extern struct osmlayer_boot_summary osmlayer_rust_init(const struct boot_info *boot);
extern int64_t osmlayer_rust_syscall(const struct syscall_frame *frame);
extern uint32_t osmlayer_rust_selftest(void);

static struct osmlayer_boot_summary summary;

void osmlayer_bridge_init(const struct boot_info *boot)
{
    console_printf("[osmlayer] bridge init entering\n");
    summary = osmlayer_rust_init(boot);
    console_printf("[osmlayer] bridge init returned\n");
    if (summary.memory_kib == 0) {
        summary.memory_kib = mm_total_memory_kib();
    }
    console_printf("[osmlayer] init abi=%u modules=%u memory=%llu KiB root=%u:/\n",
                   summary.abi_version,
                   summary.module_count,
                   (unsigned long long)summary.memory_kib,
                   summary.root_drive);
    console_printf("[osmlayer] VFS mounted FAT32 root at 0:/ with Linux syscall ABI\n");
}

int64_t osmlayer_bridge_syscall(const struct syscall_frame *frame)
{
    return osmlayer_rust_syscall(frame);
}

void osmlayer_bridge_selftest(void)
{
    uint32_t passed = osmlayer_rust_selftest();
    console_printf("[osmlayer] selftest passed=%u/4 (vfs fat32 ipc gui)\n", passed);
}
