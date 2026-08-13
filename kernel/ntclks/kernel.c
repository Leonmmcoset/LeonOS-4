/*
 * LeonOS kernel bootstrap: coordinates early platform initialization.
 * Starts memory, interrupts, drivers, storage, middle layer, and scheduling.
 */
#include <ntclks/arch.h>
#include <ntclks/console.h>
#include <ntclks/driver_manager.h>
#include <ntclks/framebuffer.h>
#include <ntclks/gui_ipc.h>
#include <ntclks/input.h>
#include <ntclks/kernel.h>
#include <ntclks/mm.h>
#include <ntclks/mouse.h>
#include <ntclks/multiboot2.h>
#include <ntclks/net.h>
#include <ntclks/osmlayer.h>
#include <ntclks/platform.h>
#include <ntclks/power.h>
#include <ntclks/pty.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/time.h>
#include <ntclks/usb.h>
#include <ntclks/userland.h>
#include <ntclks/version.h>

#include "arch/x86_64/idt.h"

static uint8_t kernel_ring0_stack[65536] __attribute__((aligned(16)));

/**
 * @brief Coordinates the status u32 operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param value Input or output value used by this operation.
 */
static void status_u32(char *buf, uint32_t *pos, uint32_t value)
{
    char tmp[10];
    uint32_t n = 0;
    if (value == 0) {
        buf[(*pos)++] = '0';
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (n) {
        buf[(*pos)++] = tmp[--n];
    }
}

/**
 * @brief Coordinates the status hex8 operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param value Input or output value used by this operation.
 */
static void status_hex8(char *buf, uint32_t *pos, uint8_t value)
{
    const char *digits = "0123456789abcdef";
    buf[(*pos)++] = digits[(value >> 4) & 0xf];
    buf[(*pos)++] = digits[value & 0xf];
}

/**
 * @brief Coordinates the mouse status line operation.
 */
static void mouse_status_line(void)
{
    const struct mouse_state *m = mouse_get_state();
    char line[80];
    uint32_t pos = 0;
    const char *prefix = "Mouse x=";
    for (uint32_t i = 0; prefix[i]; ++i) {
        line[pos++] = prefix[i];
    }
    status_u32(line, &pos, (uint32_t)m->x);
    line[pos++] = ' ';
    line[pos++] = 'y';
    line[pos++] = '=';
    status_u32(line, &pos, (uint32_t)m->y);
    line[pos++] = ' ';
    line[pos++] = 'b';
    line[pos++] = '=';
    status_u32(line, &pos, m->buttons);
    line[pos++] = ' ';
    line[pos++] = 'e';
    line[pos++] = '=';
    status_u32(line, &pos, mouse_event_count());
    line[pos++] = ' ';
    line[pos++] = 'p';
    line[pos++] = '=';
    line[pos++] = m->present ? '1' : '0';
    line[pos++] = ' ';
    line[pos++] = 'm';
    line[pos++] = '=';
    line[pos++] = m->absolute ? 'a' : 'r';
    line[pos++] = ' ';
    line[pos++] = 's';
    line[pos++] = '=';
    status_hex8(line, &pos, mouse_last_status());
    line[pos++] = ' ';
    line[pos++] = 'd';
    line[pos++] = '=';
    status_hex8(line, &pos, mouse_last_data());
    line[pos++] = ' ';
    line[pos++] = 'a';
    line[pos++] = '=';
    status_hex8(line, &pos, mouse_last_ack());
    while (pos < 79) {
        line[pos++] = ' ';
    }
    line[pos] = 0;

    const struct framebuffer *fb = framebuffer_get();
    if (fb->available) {
        uint32_t y = fb->height > 18 ? fb->height - 18 : 0;
        framebuffer_rect(0, y, 640, 10, 0x00c0c0c0);
        framebuffer_text(4, y + 1, line, 0x00000000, 0x00c0c0c0);
    } else {
        vga_write_at(0, 24, line);
    }
}

/**
 * @brief Coordinates the kernel idle loop operation.
 */
void kernel_idle_loop(void)
{
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/**
 * @brief Coordinates the cmdline has operation.
 * @param boot Boot information supplied by the loader.
 * @param needle Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the boot text eq operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the boot import handoff modules operation.
 * @param boot Boot information supplied by the loader.
 * @param handoff Input or output value used by this operation.
 */
static void boot_import_handoff_modules(struct boot_info *boot,
                                        const struct leonos_boot_handoff *handoff)
{
    if (!boot || !handoff || handoff->magic != LEONOS_BOOT_HANDOFF_MAGIC ||
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
 * @brief Coordinates the kernel start operation.
 * @param magic Input or output value used by this operation.
 * @param multiboot_info Input or output value used by this operation.
 * @param handoff Input or output value used by this operation.
 */
static void kernel_start(uint32_t magic, uint32_t multiboot_info,
                         const struct leonos_boot_handoff *handoff)
{
    __asm__ volatile("cli");
    console_init();
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
    if (!boot.rsdp_addr && handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC) {
        boot.rsdp_addr = handoff->rsdp_addr;
    }
    /* Parse ACPI before the physical allocator can reclaim ACPI memory. */
    power_init(&boot);
    boot_import_handoff_modules(&boot, handoff);
    platform_identity_init(&boot);

    arch_init();
    framebuffer_init(&boot);
    mm_init(&boot, handoff);
    time_init();
    input_init();
    pty_init();
    gui_ipc_init();
    gui_ipc_set_boot_theme(handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC
                               ? handoff->ui_theme
                               : 1u);
    console_set_ui_theme(gui_ipc_appearance_theme());
    console_enable_framebuffer(handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC
                                   ? &handoff->boot_log
                                   : 0);
    console_enable_vga_fallback();
    sched_init();
    sched_create_idle_task();
    syscall_init();
    arch_userland_init(kernel_ring0_stack + sizeof(kernel_ring0_stack));
    idt_init();
    irq_init();
    osmlayer_bridge_init(&boot, handoff);
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
    driver_manager_init();
    driver_manager_autoload();
    usb_init();
    net_init();
    osmlayer_bridge_selftest();
    userland_init(&boot);
    sched_dump();
    console_printf("[ntclks] boot complete: version=%s root=0:/ fs=FAT32 desktop=desktop.elf\n",
                   system->kernel_version);
    console_disable_framebuffer();
    framebuffer_clear(gui_ipc_appearance_theme() == 0u ? 0x00008080u : 0x000078d4u);
    framebuffer_text(24, 24, "LeonOS 4 starting Ring-3 desktop.elf...", 0x00ffffffu,
                     gui_ipc_appearance_theme() == 0u ? 0x00008080u : 0x000078d4u);
    mouse_status_line();

    userland_enter_first();
    kernel_idle_loop();
}

/**
 * @brief Coordinates the kernel entry operation.
 * @param handoff Input or output value used by this operation.
 */
void kernel_entry(const struct leonos_boot_handoff *handoff)
{
    if (!handoff || handoff->magic != LEONOS_BOOT_HANDOFF_MAGIC) {
        kernel_start(0, 0, handoff);
    }
    kernel_start(handoff->multiboot_magic, (uint32_t)handoff->multiboot_info, handoff);
}

/**
 * @brief Coordinates the kernel main operation.
 * @param magic Input or output value used by this operation.
 * @param multiboot_info Input or output value used by this operation.
 */
void kernel_main(uint32_t magic, uint32_t multiboot_info)
{
    kernel_start(magic, multiboot_info, 0);
}
