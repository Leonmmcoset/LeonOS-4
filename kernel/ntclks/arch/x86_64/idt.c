/*
 * LeonOS x86_64 interrupt handling: configures the IDT and trap dispatch.
 * Handles faults, system calls, hardware IRQs, and user page faults.
 */
#include <ntclks/bugcheck.h>
#include <ntclks/arch.h>
#include <ntclks/console.h>
#include <ntclks/gui_ipc.h>
#include <ntclks/pty.h>
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

/**
 * @brief Coordinates the idt set operation.
 * @param vector Input or output value used by this operation.
 * @param handler Input or output value used by this operation.
 * @param dpl Input or output value used by this operation.
 */
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

/**
 * @brief Coordinates the idt init operation.
 */
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

/**
 * @brief Coordinates the exception dispatch operation.
 * @param vector Input or output value used by this operation.
 * @param error Input or output value used by this operation.
 * @param rip Input or output value used by this operation.
 * @param cs Input or output value used by this operation.
 * @param rflags Input or output value used by this operation.
 * @param rsp Input or output value used by this operation.
 * @param ss Input or output value used by this operation.
 */
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

/**
 * @brief Coordinates the pf append char operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param ch Input or output value used by this operation.
 */
static void pf_append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (!buf || !pos || cap == 0 || *pos + 1 >= cap) {
        return;
    }
    buf[*pos] = ch;
    ++(*pos);
    buf[*pos] = 0;
}

/**
 * @brief Coordinates the pf append text operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param text Input or output value used by this operation.
 */
static void pf_append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    if (!text) {
        return;
    }
    while (*text) {
        pf_append_char(buf, pos, cap, *text++);
    }
}

/**
 * @brief Coordinates the pf append u64 dec operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param value Input or output value used by this operation.
 */
static void pf_append_u64_dec(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    char tmp[21];
    uint32_t n = 0;
    if (value == 0) {
        pf_append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    }
    while (n) {
        pf_append_char(buf, pos, cap, tmp[--n]);
    }
}

/**
 * @brief Coordinates the pf append u64 hex operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param value Input or output value used by this operation.
 */
static void pf_append_u64_hex(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    static const char hex[] = "0123456789abcdef";
    int started = 0;
    pf_append_text(buf, pos, cap, "0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0xfu);
        if (nibble || started || shift == 0) {
            pf_append_char(buf, pos, cap, hex[nibble]);
            started = 1;
        }
    }
}

/**
 * @brief Coordinates the format user page fault report operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param task Task whose state or authority is inspected or updated.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @param cr2 Input or output value used by this operation.
 */
static void format_user_page_fault_report(char *buf, uint32_t cap,
                                          const struct task *task,
                                          const struct trap_frame *frame,
                                          uint64_t cr2)
{
    uint32_t pos = 0;
    if (!buf || cap == 0 || !frame) {
        return;
    }
    buf[0] = 0;
    pf_append_text(buf, &pos, cap, "A user application caused an unrecoverable Page Fault.\n");
    pf_append_text(buf, &pos, cap, "PID: ");
    pf_append_u64_dec(buf, &pos, cap, task ? task->pid : 0);
    pf_append_text(buf, &pos, cap, "  Name: ");
    pf_append_text(buf, &pos, cap, task && task->name ? task->name : "(unknown)");
    pf_append_char(buf, &pos, cap, '\n');
    pf_append_text(buf, &pos, cap, "Path: ");
    pf_append_text(buf, &pos, cap, task && task->path[0] ? task->path : "(unknown)");
    pf_append_char(buf, &pos, cap, '\n');
    pf_append_text(buf, &pos, cap, "User: ");
    pf_append_text(buf, &pos, cap, task && task->username[0] ? task->username : "(none)");
    pf_append_text(buf, &pos, cap, "  UID: ");
    pf_append_u64_dec(buf, &pos, cap, task ? task->uid : 0);
    pf_append_text(buf, &pos, cap, "  Role: ");
    pf_append_text(buf, &pos, cap,
                   task && task->role == LEONOS_AUTH_ROLE_ADMIN ? "Administrator" : "User");
    pf_append_char(buf, &pos, cap, '\n');
    pf_append_text(buf, &pos, cap, "Fault address: ");
    pf_append_u64_hex(buf, &pos, cap, cr2);
    pf_append_text(buf, &pos, cap, "  Error: ");
    pf_append_u64_hex(buf, &pos, cap, frame->error);
    pf_append_char(buf, &pos, cap, '\n');
    pf_append_text(buf, &pos, cap, "Flags: present=");
    pf_append_u64_dec(buf, &pos, cap, frame->error & 1ULL);
    pf_append_text(buf, &pos, cap, " write=");
    pf_append_u64_dec(buf, &pos, cap, (frame->error >> 1) & 1ULL);
    pf_append_text(buf, &pos, cap, " user=");
    pf_append_u64_dec(buf, &pos, cap, (frame->error >> 2) & 1ULL);
    pf_append_text(buf, &pos, cap, " reserved=");
    pf_append_u64_dec(buf, &pos, cap, (frame->error >> 3) & 1ULL);
    pf_append_text(buf, &pos, cap, " fetch=");
    pf_append_u64_dec(buf, &pos, cap, (frame->error >> 4) & 1ULL);
    pf_append_char(buf, &pos, cap, '\n');
    pf_append_text(buf, &pos, cap, "RIP: ");
    pf_append_u64_hex(buf, &pos, cap, frame->rip);
    pf_append_text(buf, &pos, cap, "  RSP: ");
    pf_append_u64_hex(buf, &pos, cap, frame->rsp);
    pf_append_text(buf, &pos, cap, "  RBP: ");
    pf_append_u64_hex(buf, &pos, cap, frame->rbp);
    pf_append_char(buf, &pos, cap, '\n');
    pf_append_text(buf, &pos, cap, "RAX: ");
    pf_append_u64_hex(buf, &pos, cap, frame->rax);
    pf_append_text(buf, &pos, cap, "  RBX: ");
    pf_append_u64_hex(buf, &pos, cap, frame->rbx);
    pf_append_text(buf, &pos, cap, "  RCX: ");
    pf_append_u64_hex(buf, &pos, cap, frame->rcx);
    pf_append_char(buf, &pos, cap, '\n');
    pf_append_text(buf, &pos, cap, "RDX: ");
    pf_append_u64_hex(buf, &pos, cap, frame->rdx);
    pf_append_text(buf, &pos, cap, "  RSI: ");
    pf_append_u64_hex(buf, &pos, cap, frame->rsi);
    pf_append_text(buf, &pos, cap, "  RDI: ");
    pf_append_u64_hex(buf, &pos, cap, frame->rdi);
    pf_append_char(buf, &pos, cap, '\n');
    pf_append_text(buf, &pos, cap, "CS: ");
    pf_append_u64_hex(buf, &pos, cap, frame->cs);
    pf_append_text(buf, &pos, cap, "  RFLAGS: ");
    pf_append_u64_hex(buf, &pos, cap, frame->rflags);
    pf_append_text(buf, &pos, cap, "  Ticks: ");
    pf_append_u64_dec(buf, &pos, cap, time_ticks());
}

/**
 * @brief Coordinates the abort user page fault task operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @param cr2 Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static struct task *abort_user_page_fault_task(struct trap_frame *frame, uint64_t cr2)
{
    struct task *task = sched_current_task();
    struct task *window_server = sched_find_window_server();
    char report[GUI_IPC_WINDOW_TEXT_MAX];
    if (!task || task->kind != TASK_KIND_USER) {
        bugcheck_trap("Unhandled Page Fault", frame, cr2);
    }
    if (task->flags & TASK_FLAG_WINDOW_SERVER) {
        console_printf("[ntclks] window server page fault pid=%u, falling back to bugcheck\n",
                       task->pid);
        bugcheck_trap("Window Server Page Fault", frame, cr2);
    }
    format_user_page_fault_report(report, sizeof(report), task, frame, cr2);
    console_printf("[ntclks] user page fault killed pid=%u name=%s cr2=0x%llx rip=0x%llx error=0x%llx\n",
                   task->pid,
                   task->name,
                   (unsigned long long)cr2,
                   (unsigned long long)frame->rip,
                   (unsigned long long)frame->error);
    syscall_release_task_files(task);
    gui_ipc_destroy_owner(task->pid);
    pty_process_exit(task->pid);
    if (window_server && window_server->pid != task->pid) {
        (void)gui_ipc_post_system_window(window_server->pid, 560, 270,
                                         "Application Page Fault",
                                         report,
                                         task->path,
                                         0);
    }
    sched_exit(task->pid, 0x8000000eULL);
    return userland_schedule_from_frame(NULL);
}

/**
 * @brief Coordinates the page fault dispatch operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @return Result, status, or value defined by this API.
 */
struct task *page_fault_dispatch(struct trap_frame *frame)
{
    uint64_t cr2 = x86_64_read_cr2();
    if (frame && syscall_handle_user_page_fault(cr2, frame->error)) {
        /*
         * A lazy file-backed page may require a synchronous FAT read.  Switch
         * away after each resolved fault so a newly-starting app cannot hold
         * the desktop in a long run of consecutive page-ins.
         */
        return userland_schedule_from_frame(frame);
    }
    if (!frame) {
        bugcheck_exception(14, 0, 0, 0, 0, 0, 0, cr2);
    }
    console_printf("[ntclks] page fault unhandled cr2=0x%llx error=0x%llx rip=0x%llx cs=0x%llx\n",
                   (unsigned long long)cr2,
                   (unsigned long long)frame->error,
                   (unsigned long long)frame->rip,
                   (unsigned long long)frame->cs);
    console_printf("[ntclks] page fault flags present=%u write=%u user=%u reserved=%u fetch=%u\n",
                   (unsigned)(frame->error & 1u),
                   (unsigned)((frame->error >> 1) & 1u),
                   (unsigned)((frame->error >> 2) & 1u),
                   (unsigned)((frame->error >> 3) & 1u),
                   (unsigned)((frame->error >> 4) & 1u));
    if ((frame->cs & 3ULL) == 3ULL) {
        return abort_user_page_fault_task(frame, cr2);
    }
    bugcheck_trap("Unhandled Page Fault", frame, cr2);
}

/**
 * @brief Coordinates the int80 dispatch operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @return Result, status, or value defined by this API.
 */
struct task *int80_dispatch(struct trap_frame *frame)
{
    syscall_dispatch_frame(frame);
    return userland_schedule_from_frame(frame);
}
