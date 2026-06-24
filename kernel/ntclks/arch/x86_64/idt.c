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
extern void isr6_stub(void);
extern void isr13_stub(void);
extern void isr14_stub(void);
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
    idt_set(6, isr6_stub, 0);
    idt_set(13, isr13_stub, 0);
    idt_set(14, isr14_stub, 0);
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

void exception_dispatch(uint64_t vector, uint64_t error, uint64_t rip, uint64_t cs, uint64_t rflags)
{
    console_printf("[ntclks] exception vector=%llu error=0x%llx rip=0x%llx cs=0x%llx rflags=0x%llx\n",
                   (unsigned long long)vector,
                   (unsigned long long)error,
                   (unsigned long long)rip,
                   (unsigned long long)cs,
                   (unsigned long long)rflags);
    console_printf("[ntclks] exception cr2=0x%llx ticks=%llu\n",
                   (unsigned long long)x86_64_read_cr2(),
                   (unsigned long long)time_ticks());
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

struct task *int80_dispatch(struct trap_frame *frame)
{
    syscall_dispatch_frame(frame);
    return userland_schedule_from_frame(frame);
}
