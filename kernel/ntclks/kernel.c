/*
 * LeonOS kernel bootstrap: coordinates early platform initialization.
 * Starts memory, interrupts, drivers, storage, middle layer, and scheduling.
 */
#include <ntclks/arch.h>
#include <ntclks/apic.h>
#include <ntclks/boot_splash.h>
#include <ntclks/console.h>
#include <ntclks/driver_manager.h>
#include <ntclks/framebuffer.h>
#include <ntclks/gui_ipc.h>
#include <ntclks/input.h>
#include <ntclks/kernel_debug.h>
#include <ntclks/kernel.h>
#include <ntclks/mm.h>
#include <ntclks/heap.h>
#include <ntclks/multiboot2.h>
#include <ntclks/net.h>
#include <ntclks/osmlayer.h>
#include <ntclks/platform.h>
#include <ntclks/power.h>
#include <ntclks/pty.h>
#include <ntclks/page_cache.h>
#include <ntclks/object.h>
#include <ntclks/sched.h>
#include <ntclks/smp.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/time.h>
#include <ntclks/usb.h>
#include <ntclks/userland.h>
#include <ntclks/version.h>

#include "arch/x86_64/idt.h"

static uint8_t kernel_ring0_stack[65536] __attribute__((aligned(16)));

static bool boot_handoff_is_current(const struct leonos_boot_handoff *handoff)
{
    return handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC &&
           handoff->version == LEONOS_BOOT_HANDOFF_VERSION;
}

/**
 * @brief Halt the CPU forever; used when there is nothing left to run.
 */
void kernel_idle_loop(void)
{
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/**
 * @brief Return 1 if the boot command line contains the substring needle.
 */
static int cmdline_has(const struct boot_info *boot, const char *needle)
{
    if (!boot || !boot->cmdline || !needle) {
        return 0;
    }
    for (const char *p = boot->cmdline; *p; ++p) {
        const char *a = p;
        const char *b = needle;
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        if (*b == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Return 1 if the two NUL-terminated strings are identical.
 */
static int boot_text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

/**
 * @brief Replace or append the installer-root module in boot from the loader handoff.
 */
static void boot_import_handoff_modules(struct boot_info *boot,
                                        const struct leonos_boot_handoff *handoff)
{
    if (!boot || !boot_handoff_is_current(handoff) ||
        handoff->installer_root.end <= handoff->installer_root.start) {
        return;
    }

    for (uint32_t i = 0; i < boot->module_count && i < 16; ++i) {
        if (boot_text_eq(boot->modules[i].name, "leonos-installer-root")) {
            boot->modules[i].start = handoff->installer_root.start;
            boot->modules[i].end = handoff->installer_root.end;
            return;
        }
    }

    if (boot->module_count < 16) {
        struct boot_module *module = &boot->modules[boot->module_count++];
        module->start = handoff->installer_root.start;
        module->end = handoff->installer_root.end;
        module->name = handoff->installer_root.path;
        console_printf("[ntclks] imported installer root module start=%p bytes=%llu\n",
                       (void *)(uintptr_t)module->start,
                       (unsigned long long)(module->end - module->start));
    }
}

/**
 * @brief Full boot sequence: parse Multiboot2/EFI info, then initialize every kernel subsystem in order and enter userland.
 */
static void kernel_start(uint32_t magic, uint32_t multiboot_info,
                         const struct leonos_boot_handoff *handoff)
{
    bool boot_log_screen;
    int startup_tty;

    __asm__ volatile("cli");
    console_init();
    console_set_boot_uptime_us(handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC
                                   ? handoff->boot_uptime_us
                                   : 0ULL);
    const struct leonos_system_info *system = ntclks_system_info();
    console_printf("LeonOS 4 %s %s booting\n",
                   system->kernel_name,
                   system->kernel_version);
    if (handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC) {
        console_printf("[ntclks] loader handoff kernel=%p-%p middlelayer=%p-%p entry=%p\n",
                       (void *)(uintptr_t)handoff->kernel.start,
                       (void *)(uintptr_t)handoff->kernel.end,
                       (void *)(uintptr_t)handoff->middlelayer.start,
                       (void *)(uintptr_t)handoff->middlelayer.end,
                       (void *)(uintptr_t)handoff->middlelayer.entry);
    }

    struct boot_info boot;
    multiboot2_parse(magic, (uintptr_t)multiboot_info, &boot);
#ifdef CONFIG_STARTUP_TTY
    startup_tty = 1;
#else
    startup_tty = 0;
#endif
    if (cmdline_has(&boot, "startup=tty")) {
        startup_tty = 1;
    } else if (cmdline_has(&boot, "mode=installer")) {
        startup_tty = 0;
    } else if (cmdline_has(&boot, "startup=desktop")) {
        startup_tty = 0;
    }
    /**
 * @brief UEFI GRUB keeps boot services active for the second-stage loader and may therefore omit Multiboot2 memory-map tags. The loader captures a stable EFI map after loading all images; use it before the allocator falls back to the legacy 512 MiB estimate.
 */
    if (!boot.mmap_entry_count && !boot.efi_mmap_entry_count &&
        handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC &&
        handoff->efi_mmap_addr && handoff->efi_mmap_entry_count) {
        boot.efi_mmap_addr = handoff->efi_mmap_addr;
        boot.efi_mmap_entry_size = handoff->efi_mmap_entry_size;
        boot.efi_mmap_entry_count = handoff->efi_mmap_entry_count;
        console_printf("[ntclks] using loader-captured EFI memory map entries=%u descriptor=%u\n",
                       boot.efi_mmap_entry_count, boot.efi_mmap_entry_size);
    }
    boot_log_screen = cmdline_has(&boot, "bootlog=1");
    if (!boot.rsdp_addr && handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC) {
        boot.rsdp_addr = handoff->rsdp_addr;
    }
    /**
 * @brief Parse ACPI before the physical allocator can reclaim ACPI memory.
 */
    power_init(&boot);
    boot_import_handoff_modules(&boot, handoff);
    platform_identity_init(&boot);

    arch_init();
    apic_init();
    ioapic_init();
    smp_init();
    framebuffer_init(&boot);
    /* A TTY boot owns the framebuffer after kernel initialization, so leave
     * the splash disabled and route the console to the visible text panel. */
    boot_splash_init(!boot_log_screen && !startup_tty);
    mm_init(&boot, handoff);
    kernel_heap_init();
    page_cache_init();
    kernel_objects_init();
    boot_splash_update(84u);
    time_init();
    input_init();
    pty_init();
    gui_ipc_init();
    gui_ipc_set_boot_theme(handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC
                               ? handoff->ui_theme
                               : 1u);
    console_set_ui_theme(gui_ipc_appearance_theme());
    if (boot_log_screen || startup_tty) {
        console_enable_framebuffer(handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC
                                       ? &handoff->boot_log
                                       : 0);
    }
    console_enable_vga_fallback();
    sched_init();
    sched_create_idle_task();
    syscall_init();
    arch_userland_init(kernel_ring0_stack + sizeof(kernel_ring0_stack));
    idt_init();
    irq_init();
    boot_splash_update(90u);
    osmlayer_bridge_init(&boot, handoff);
    boot_splash_update(93u);
    {
        struct leonos_mount_policy mount_policy;
        int policy_ret = osmlayer_bridge_mount_policy(&boot, &mount_policy);
        if (policy_ret == 0) {
            storage_apply_mount_policy(&mount_policy);
            if (cmdline_has(&boot, "mode=installer") && !storage_ready()) {
                console_printf("[ntclks] installer mount policy did not produce a ready root, retrying handoff module\n");
                storage_init_installer_root(&boot);
            }
        } else if (cmdline_has(&boot, "mode=installer")) {
            console_printf("[ntclks] middlelayer mount policy unavailable ret=%d, using installer fallback\n",
                           policy_ret);
            storage_init();
            storage_init_installer_root(&boot);
        } else {
            console_printf("[ntclks] middlelayer mount policy unavailable ret=%d, using boot root fallback\n",
                           policy_ret);
            storage_init();
        }
    }
    boot_splash_update(96u);
    driver_manager_init();
    driver_manager_autoload();
    usb_init();
    net_init();
    boot_splash_update(98u);
    osmlayer_bridge_selftest();
    if (kernel_debug_boot_requested(handoff)) {
        console_printf("[ntclks] entering kernel debug tool before userland\n");
        (void)kernel_debug_run_module();
        console_printf("[ntclks] kernel debug tool finished; continuing normal startup\n");
    }
    userland_init(&boot);
    /* All initial task objects are now present. APs may enter the shared
     * scheduler without racing the bootstrap task construction above. */
    smp_start_aps();
    sched_dump();
    if (startup_tty) {
        console_printf("[ntclks] boot complete: version=%s root=/ fs=%s startup=tty\n",
                       system->kernel_version, storage_root_filesystem_name());
    } else {
        console_printf("[ntclks] boot complete: version=%s root=/ fs=%s desktop=desktop.elf\n",
                       system->kernel_version, storage_root_filesystem_name());
    }
    boot_splash_update(100u);
    if (boot_log_screen) {
        if (startup_tty) {
            console_printf("[ntclks] starting Ring-3 BusyBox TTY\n");
        } else {
            /**
             * @brief Keep the original log console visible until the Ring-3 desktop replaces it. The graphical path retains the completed splash.
             */
            console_printf("[ntclks] starting Ring-3 desktop.elf\n");
        }
    }

    if (startup_tty) {
        console_enter_tty_runtime();
    }

    userland_enter_first();
    kernel_idle_loop();
}

/**
 * @brief Validate the loader handoff and start the kernel, or idle-loop on a mismatch.
 */
void kernel_entry(const struct leonos_boot_handoff *handoff)
{
    if (!boot_handoff_is_current(handoff)) {
        console_init();
        console_printf("[ntclks] rejected loader handoff abi=%u expected=%u\n",
                       handoff ? handoff->version : 0u,
                       LEONOS_BOOT_HANDOFF_VERSION);
        kernel_idle_loop();
    }
    kernel_start(handoff->multiboot_magic, (uint32_t)handoff->multiboot_info, handoff);
}

/**
 * @brief Legacy Multiboot2 entry point; starts the kernel without a loader handoff.
 */
void kernel_main(uint32_t magic, uint32_t multiboot_info)
{
    kernel_start(magic, multiboot_info, 0);
}
