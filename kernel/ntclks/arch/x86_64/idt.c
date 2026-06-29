#include <ntclks/bugcheck.h>
#include <ntclks/arch.h>
#include <ntclks/console.h>
#include <ntclks/sched.h>
#include <ntclks/syscall.h>
#include <ntclks/time.h>
#include <ntclks/userland.h>

struct __attribute__((packed)) idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
};

struct __attribute__((packed)) idt_ptr {
    uint16_t limit;
    uint64_t base;
};

static struct idt_entry idt[256];

extern void x86_64_lidt(const struct idt_ptr *ptr);
extern void isr0_stub(void);
extern void isr1_stub(void);
extern void isr2_stub(void);
extern void isr3_stub(void);
extern void isr4_stub(void);
extern void isr5_stub(void);
extern void isr6_stub(void);
extern void isr7_stub(void);
extern void isr8_stub(void);
extern void isr9_stub(void);
extern void isr10_stub(void);
extern void isr11_stub(void);
extern void isr12_stub(void);
extern void isr13_stub(void);
extern void isr14_stub(void);
extern void isr15_stub(void);
extern void isr16_stub(void);
extern void isr17_stub(void);
extern void isr18_stub(void);
extern void isr19_stub(void);
extern void isr20_stub(void);
extern void isr21_stub(void);
extern void isr22_stub(void);
extern void isr23_stub(void);
extern void isr24_stub(void);
extern void isr25_stub(void);
extern void isr26_stub(void);
extern void isr27_stub(void);
extern void isr28_stub(void);
extern void isr29_stub(void);
extern void isr30_stub(void);
extern void isr31_stub(void);
extern void isr80_stub(void);
extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq12_stub(void);
extern void irq32_stub(void);
extern uint64_t x86_64_read_cr2(void);

static void idt_set(uint8_t vector, void *handler, uint8_t dpl)
{
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = addr & 0xffff;
    idt[vector].selector = NTCLKS_KERNEL_CS;
    idt[vector].ist = 0;
    idt[vector].type_attr = (uint8_t)(0x8e | ((dpl & 3) << 5));
    idt[vector].offset_mid = (addr >> 16) & 0xffff;
    idt[vector].offset_high = (uint32_t)(addr >> 32);
    idt[vector].zero = 0;
}

void idt_init(void)
{
    idt_set(0, isr0_stub, 0);
    idt_set(1, isr1_stub, 0);
    idt_set(2, isr2_stub, 0);
    idt_set(3, isr3_stub, 0);
    idt_set(4, isr4_stub, 0);
    idt_set(5, isr5_stub, 0);
    idt_set(6, isr6_stub, 0);
    idt_set(7, isr7_stub, 0);
    idt_set(8, isr8_stub, 0);
    idt_set(9, isr9_stub, 0);
    idt_set(10, isr10_stub, 0);
    idt_set(11, isr11_stub, 0);
    idt_set(12, isr12_stub, 0);
    idt_set(13, isr13_stub, 0);
    idt_set(14, isr14_stub, 0);
    idt_set(15, isr15_stub, 0);
    idt_set(16, isr16_stub, 0);
    idt_set(17, isr17_stub, 0);
    idt_set(18, isr18_stub, 0);
    idt_set(19, isr19_stub, 0);
    idt_set(20, isr20_stub, 0);
    idt_set(21, isr21_stub, 0);
    idt_set(22, isr22_stub, 0);
    idt_set(23, isr23_stub, 0);
    idt_set(24, isr24_stub, 0);
    idt_set(25, isr25_stub, 0);
    idt_set(26, isr26_stub, 0);
    idt_set(27, isr27_stub, 0);
    idt_set(28, isr28_stub, 0);
    idt_set(29, isr29_stub, 0);
    idt_set(30, isr30_stub, 0);
    idt_set(31, isr31_stub, 0);
    idt_set(0x20, irq0_stub, 0);
    idt_set(0x21, irq1_stub, 0);
    idt_set(0x2c, irq12_stub, 0);
    for (uint8_t vector = 0x30; vector < 0x50; ++vector) {
        idt_set(vector, irq32_stub, 0);
    }
    idt_set(0x80, isr80_stub, 3);
    struct idt_ptr ptr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)(uintptr_t)idt,
    };
    x86_64_lidt(&ptr);
}

void exception_dispatch(uint64_t vector, uint64_t error, uint64_t rip, uint64_t cs,
                        uint64_t rflags, uint64_t rsp, uint64_t ss)
{
    uint64_t cr2 = x86_64_read_cr2();
    const char *mode = (cs & 3u) == 3u ? "user" : "kernel";
    console_printf("[ntclks] exception vector=%llu error=0x%llx rip=0x%llx cs=0x%llx rflags=0x%llx rsp=0x%llx ss=0x%llx\n",
                   (unsigned long long)vector,
                   (unsigned long long)error,
                   (unsigned long long)rip,
                   (unsigned long long)cs,
                   (unsigned long long)rflags,
                   (unsigned long long)rsp,
                   (unsigned long long)ss);
    console_printf("[ntclks] exception mode=%s cr2=0x%llx ticks=%llu\n",
                   mode,
                   (unsigned long long)cr2,
                   (unsigned long long)time_ticks());
    if (vector == 14) {
        console_printf("[ntclks] page fault flags present=%u write=%u user=%u reserved=%u fetch=%u\n",
                       (unsigned)(error & 1u),
                       (unsigned)((error >> 1) & 1u),
                       (unsigned)((error >> 2) & 1u),
                       (unsigned)((error >> 3) & 1u),
                       (unsigned)((error >> 4) & 1u));
    }
    bugcheck_exception(vector, error, rip, cs, rflags, rsp, ss, cr2);
}

struct task *int80_dispatch(struct trap_frame *frame)
{
    syscall_dispatch_frame(frame);
    return userland_schedule_from_frame(frame);
}
