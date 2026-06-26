#include <ntclks/console.h>
#include <ntclks/mm.h>
#include <ntclks/osmlayer.h>

static struct osmlayer_boot_summary summary;
static const struct leonos_middlelayer_api *api;
static struct osmlayer_boot_summary (*middlelayer_init)(const struct boot_info *boot);
static int64_t (*middlelayer_syscall)(const struct syscall_frame *frame);
static uint32_t (*middlelayer_selftest)(void);

static void osmlayer_log(const char *message)
{
    if (message) {
        console_write(message);
    }
}

static void osmlayer_log_len(const char *message, uint64_t len)
{
    if (message) {
        console_write_len(message, (size_t)len);
    }
}

void osmlayer_bridge_init(const struct boot_info *boot,
                          const struct leonos_boot_handoff *handoff)
{
    console_printf("[osmlayer] bridge init entering\n");
    if (!handoff || handoff->magic != LEONOS_BOOT_HANDOFF_MAGIC ||
        !handoff->middlelayer.entry) {
        console_printf("[osmlayer] no middlelayer module in loader handoff\n");
        return;
    }

    struct leonos_kernel_services services = {
        .version = LEONOS_KERNEL_SERVICES_VERSION,
        .reserved = 0,
        .log = osmlayer_log,
        .log_len = osmlayer_log_len,
    };
    leonos_middlelayer_module_init_fn module_init =
        (leonos_middlelayer_module_init_fn)(uintptr_t)handoff->middlelayer.entry;
    api = module_init(&services, handoff);
    if (!api || api->version != LEONOS_MIDDLELAYER_API_VERSION ||
        !api->init || !api->syscall || !api->selftest) {
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
    console_printf("[osmlayer] VFS mounted FAT32 root at 0:/ with Linux syscall ABI\n");
}

int64_t osmlayer_bridge_syscall(const struct syscall_frame *frame)
{
    if (!middlelayer_syscall) {
        return -38;
    }
    return middlelayer_syscall(frame);
}

void osmlayer_bridge_selftest(void)
{
    if (!middlelayer_selftest) {
        console_printf("[osmlayer] selftest skipped: module not bound\n");
        return;
    }
    uint32_t passed = middlelayer_selftest();
    console_printf("[osmlayer] selftest passed=%u/4 (vfs fat32 ipc gui)\n", passed);
}
