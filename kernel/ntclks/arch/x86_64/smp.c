/*
 * LeonOS x86_64 symmetric multiprocessing.
 *
 * APs are started only after paging, the BSP GDT, and the IDT are ready. The
 * low-memory trampoline is copied from the kernel image and patched for one AP
 * at a time, which keeps the SIPI entry position-independent and avoids
 * reserving a permanent low-memory allocator range.
 */
#include <ntclks/apic.h>
#include <ntclks/arch.h>
#include <ntclks/console.h>
#include <ntclks/mm.h>
#include <ntclks/paging.h>
#include <ntclks/sched.h>
#include <ntclks/smp.h>
#include <ntclks/userland.h>

#include "idt.h"

#define AP_TRAMPOLINE_PHYS 0x8000ULL
#define AP_TRAMPOLINE_VECTOR ((uint8_t)(AP_TRAMPOLINE_PHYS >> 12))
#define AP_STACK_PAGES 32u
/* Conservative periodic preemption rate for a virtual LAPIC. The BSP PIT
 * remains the authoritative 100 Hz wall clock; this only prevents an AP's
 * compute-bound user task from monopolising that CPU. */
#define AP_LAPIC_TIMER_INITIAL_COUNT 1000000u
/*
 * The shared task/address-space table is not yet ready for concurrent user
 * execution.  Keep AP startup available for a later SMP implementation, but
 * run the production scheduler on the BSP until task ownership and teardown
 * are fully synchronized.  This also avoids VMware triple faults when a
 * short-lived child is reaped while another CPU is leaving its user frame.
 */
#define SMP_USER_SCHEDULER_ENABLED 0

extern uint8_t smp_trampoline_start[];
extern uint8_t smp_trampoline_end[];
extern uint8_t smp_trampoline_cr3_imm[];
extern uint8_t smp_trampoline_stack_imm[];
extern uint8_t smp_trampoline_cpu_imm[];
extern uint8_t smp_trampoline_entry_imm[];
extern uint8_t smp_trampoline_gdtr_base[];
extern uint8_t smp_trampoline_gdtr[];
extern uint8_t smp_trampoline_gdt[];
extern uint8_t smp_trampoline_pmode_off[];
extern uint8_t smp_trampoline_longmode_off[];
extern uint8_t smp_trampoline_pmode[];
extern uint8_t smp_trampoline_longmode[];

static struct smp_cpu_info cpus[SMP_MAX_CPUS];
static uint32_t cpu_count = 1;
static volatile uint32_t smp_ready;
static volatile uint32_t smp_scheduler_started;
static volatile uint32_t smp_bsp_user_entry_pending;

static uint32_t trampoline_offset(const uint8_t *symbol)
{
    return (uint32_t)(symbol - smp_trampoline_start);
}

static void copy_trampoline_for(uint32_t index, uint64_t stack_top)
{
    uint8_t *destination = (uint8_t *)(uintptr_t)AP_TRAMPOLINE_PHYS;
    uint32_t size = (uint32_t)(smp_trampoline_end - smp_trampoline_start);
    uint32_t gdt_address;
    uint32_t gdtr_address;
    uint32_t longmode_address;
    for (uint32_t i = 0; i < size; ++i) destination[i] = smp_trampoline_start[i];

    gdtr_address = (uint32_t)(AP_TRAMPOLINE_PHYS + trampoline_offset(smp_trampoline_gdtr));
    gdt_address = (uint32_t)(AP_TRAMPOLINE_PHYS + trampoline_offset(smp_trampoline_gdt));
    *(uint16_t *)(destination + trampoline_offset(smp_trampoline_gdtr_base)) =
        (uint16_t)gdtr_address;
    *(uint32_t *)(destination + trampoline_offset(smp_trampoline_gdtr) + 2u) = gdt_address;
    gdt_address = (uint32_t)(AP_TRAMPOLINE_PHYS + trampoline_offset(smp_trampoline_pmode));
    longmode_address = (uint32_t)(AP_TRAMPOLINE_PHYS + trampoline_offset(smp_trampoline_longmode));
    *(uint16_t *)(destination + trampoline_offset(smp_trampoline_pmode_off)) =
        (uint16_t)gdt_address;
    *(uint32_t *)(destination + trampoline_offset(smp_trampoline_longmode_off)) =
        longmode_address;
    *(uint32_t *)(destination + trampoline_offset(smp_trampoline_cr3_imm)) =
        (uint32_t)paging_kernel_cr3();
    *(uint64_t *)(destination + trampoline_offset(smp_trampoline_stack_imm)) = stack_top;
    *(uint32_t *)(destination + trampoline_offset(smp_trampoline_cpu_imm)) = index;
    *(uint64_t *)(destination + trampoline_offset(smp_trampoline_entry_imm)) =
        (uint64_t)(uintptr_t)smp_ap_entry;
    __asm__ volatile("wbinvd" : : : "memory");
}

void smp_init(void)
{
    uint32_t discovered = apic_cpu_count();
    cpu_count = SMP_USER_SCHEDULER_ENABLED && discovered &&
                discovered <= SMP_MAX_CPUS ? discovered : 1;
    for (uint32_t i = 0; i < SMP_MAX_CPUS; ++i) {
        cpus[i] = (struct smp_cpu_info){0};
        if (i < cpu_count) cpus[i].apic_id = apic_cpu_id_at(i);
    }
    if (!cpu_count) cpu_count = 1;
    for (uint32_t i = 0; i < cpu_count; ++i) {
        if (cpus[i].apic_id == apic_bsp_id()) {
            cpus[i].online = 1;
            cpus[i].started = 1;
            break;
        }
    }
    smp_ready = cpu_count <= 1;
    smp_scheduler_started = cpu_count <= 1;
    smp_bsp_user_entry_pending = cpu_count <= 1;
    console_printf("[ntclks] SMP topology CPUs=%u BSP APIC=%u discovered=%u%s\n",
                   (unsigned)cpu_count, (unsigned)apic_bsp_id(),
                   (unsigned)discovered,
                   SMP_USER_SCHEDULER_ENABLED ? "" : " (AP scheduler disabled)");
}

void smp_mark_bsp_user_entry(void)
{
    __asm__ volatile("mfence" : : : "memory");
    smp_bsp_user_entry_pending = 1;
    __asm__ volatile("mfence" : : : "memory");
}

void smp_release_aps(void)
{
    if (cpu_count > 1 && !smp_bsp_user_entry_pending) {
        return;
    }
    /* One user-mode timer proves the first iret completed; wait for the next
     * one so the BSP has also completed a full scheduler handoff and is no
     * longer returning through the bootstrap path when APs start selecting
     * tasks from the shared table. */
    if (cpu_count > 1 && smp_bsp_user_entry_pending == 1) {
        __asm__ volatile("mfence" : : : "memory");
        smp_bsp_user_entry_pending = 2;
        __asm__ volatile("mfence" : : : "memory");
        return;
    }
    __asm__ volatile("mfence" : : : "memory");
    smp_scheduler_started = 1;
    __asm__ volatile("mfence" : : : "memory");
}

uint32_t smp_cpu_count(void) { return cpu_count; }

uint32_t smp_current_cpu(void)
{
    uint32_t id = apic_id();
    for (uint32_t i = 0; i < cpu_count; ++i) {
        if (cpus[i].apic_id == id) return i;
    }
    return 0;
}

bool smp_is_ready(void) { return smp_ready != 0; }

bool smp_cpu_online(uint32_t cpu_index)
{
    return cpu_index < cpu_count && cpus[cpu_index].online != 0;
}

const struct smp_cpu_info *smp_cpu_info(uint32_t cpu_index)
{
    return cpu_index < cpu_count ? &cpus[cpu_index] : 0;
}

void smp_start_aps(void)
{
    if (smp_ready || cpu_count <= 1 || !apic_available()) {
        smp_ready = 1;
        return;
    }
    /* irq_init() already configured the BSP LAPIC and its external interrupt
     * delivery mode.  Reinitializing it here would mask LINT0 and break the
     * virtual-wire PIC fallback after the first user-mode transition. */
    if (!apic_enabled()) {
        apic_enable();
    }
    for (uint32_t i = 0; i < cpu_count; ++i) {
        uint64_t stack;
        uint32_t id = cpus[i].apic_id;
        if (cpus[i].online || id == apic_bsp_id() || id > 0xffu) continue;
        /* Never issue INIT/SIPI to the current processor.  This guard also
         * protects against malformed MADT tables that duplicate the BSP ID
         * after topology discovery. */
        if (id == apic_id()) {
            cpus[i].online = 1;
            cpus[i].started = 1;
            console_printf("[ntclks] SMP CPU%u is current BSP APIC=%u\n",
                           (unsigned)i, (unsigned)id);
            continue;
        }
        stack = mm_alloc_pages(AP_STACK_PAGES);
        if (!stack) {
            console_printf("[ntclks] SMP AP%u stack allocation failed\n", (unsigned)i);
            continue;
        }
        cpus[i].stack = stack + (uint64_t)AP_STACK_PAGES * 4096ULL;
        cpus[i].started = 1;
        copy_trampoline_for(i, cpus[i].stack);
        apic_send_init(id);
        apic_send_startup(id, AP_TRAMPOLINE_VECTOR);
        for (volatile uint32_t delay = 0; delay < 100000u; ++delay) {
            __asm__ volatile("pause");
        }
        apic_send_startup(id, AP_TRAMPOLINE_VECTOR);
        for (uint32_t wait = 0; wait < 5000000u && !cpus[i].online; ++wait) {
            __asm__ volatile("pause");
        }
        if (!cpus[i].online) {
            cpus[i].started = 0;
            /* Do not release a timed-out AP stack: a delayed SIPI may still
             * enter the trampoline and use it after this barrier expires. */
            console_printf("[ntclks] SMP APIC %u failed to come online\n", (unsigned)id);
        } else {
            console_printf("[ntclks] SMP CPU%u APIC=%u online\n", (unsigned)i, (unsigned)id);
        }
    }
    smp_ready = 1;
    uint32_t online = 0;
    for (uint32_t i = 0; i < cpu_count; ++i) online += cpus[i].online != 0;
    console_printf("[ntclks] SMP ready online=%u/%u\n", (unsigned)online, (unsigned)cpu_count);
}

void smp_ap_entry(uint32_t cpu_index)
{
    __asm__ volatile("cli");
    if (cpu_index >= cpu_count || cpus[cpu_index].apic_id != apic_id()) {
        cpu_index = smp_current_cpu();
    }
    if (cpu_index >= cpu_count || !cpus[cpu_index].stack) {
        for (;;) __asm__ volatile("hlt");
    }
    arch_ap_init(cpu_index, (void *)(uintptr_t)cpus[cpu_index].stack);
    /* The BSP initialized the shared IDT before AP startup.  Loading it is
     * sufficient; rebuilding entries here races the BSP and can expose a
     * partially written interrupt gate. */
    idt_load();
    apic_enable();
    if (cpu_index < cpu_count) {
        cpus[cpu_index].online = 1;
    }
    /* The BSP creates and prepares the initial user tasks immediately after
     * smp_start_aps(). Do not let an AP race that first transition or invoke
     * lazy ELF loading while the bootstrap CPU still owns the task table. */
    while (!smp_scheduler_started) {
        __asm__ volatile("pause" : : : "memory");
    }
    /* No device IRQ is routed to an AP. Its local timer is sufficient for
     * preemption and sends vector 0x40 through the shared, immutable IDT. */
    apic_timer_init(0x40u, AP_LAPIC_TIMER_INITIAL_COUNT);
    for (;;) {
        struct task *next;
        struct trap_frame *frame;
        uint64_t cr3;

        next = userland_schedule_from_frame(NULL);
        frame = sched_task_frame(next);
        cr3 = sched_task_cr3(next);
        if (frame && cr3) {
            arch_enter_user_frame(frame, cr3);
        }
        /* Normally userland_schedule_from_frame waits until it has a READY
         * task. This is a defensive idle path for a rejected task frame. */
        __asm__ volatile("sti; hlt; cli" : : : "memory");
    }
}
