#include <ntclks/bugcheck.h>
#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <ntclks/sched.h>
#include <ntclks/time.h>
#include <leonos/psf_font.h>

#include "../arch/x86_64/port.h"

#define BUGCHECK_BG 0x000000aaU
#define BUGCHECK_FG 0x00ffffffU
#define BUGCHECK_SUB 0x00d8d8ffU
#define BUGCHECK_PANEL 0x001c1cb8U
static int g_bugcheck_active;

struct bugcheck_info {
    const char *reason;
    const char *detail;
    uint64_t vector;
    uint64_t error;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
    uint64_t cr2;
    uint64_t ticks;
    uint64_t uptime_ms;
    uint32_t pid;
    const char *task_name;
};

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (!buf || !pos || *pos + 1 >= cap) {
        return;
    }
    buf[*pos] = ch;
    ++(*pos);
    buf[*pos] = 0;
}

static void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        append_char(buf, pos, cap, *text++);
    }
}

static void append_u64_dec(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    char tmp[24];
    uint32_t n = 0;
    if (value == 0) {
        append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    }
    while (n) {
        append_char(buf, pos, cap, tmp[--n]);
    }
}

static void append_u64_hex(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    const char *digits = "0123456789ABCDEF";
    append_text(buf, pos, cap, "0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        append_char(buf, pos, cap, digits[(value >> shift) & 0xfU]);
    }
}

static const char *exception_name(uint64_t vector)
{
    switch (vector) {
    case 0: return "Divide By Zero";
    case 1: return "Debug Exception";
    case 2: return "Non Maskable Interrupt";
    case 3: return "Breakpoint";
    case 4: return "Overflow";
    case 5: return "Bound Range Exceeded";
    case 6: return "Invalid Opcode";
    case 7: return "Device Not Available";
    case 8: return "Double Fault";
    case 10: return "Invalid TSS";
    case 11: return "Segment Not Present";
    case 12: return "Stack Segment Fault";
    case 13: return "General Protection Fault";
    case 14: return "Page Fault";
    case 16: return "x87 Floating Point Fault";
    case 17: return "Alignment Check";
    case 18: return "Machine Check";
    case 19: return "SIMD Floating Point Fault";
    case 20: return "Virtualization Exception";
    case 21: return "Control Protection Exception";
    case 28: return "Hypervisor Injection Exception";
    case 29: return "VMM Communication Exception";
    case 30: return "Security Exception";
    default: return "CPU Exception";
    }
}

static void collect_bugcheck_info(struct bugcheck_info *info)
{
    struct task *task;
    if (!info) {
        return;
    }
    info->ticks = time_ticks();
    info->uptime_ms = time_uptime_ms();
    info->pid = sched_current_pid();
    task = sched_current_task();
    info->task_name = (task && task->name) ? task->name : "(none)";
}

static void draw_bugcheck_vga(const struct bugcheck_info *info)
{
    char line[96];
    uint32_t pos;

    vga_init();
    copy_text(line, sizeof(line), "LeonOS 4 has encountered a fatal system error.");
    vga_write_at(0, 0, line);
    copy_text(line, sizeof(line), "The system has been halted to prevent further damage.");
    vga_write_at(0, 1, line);

    copy_text(line, sizeof(line), " ");
    for (uint8_t row = 2; row < 25; ++row) {
        vga_write_at(0, row, "                                                                                ");
    }

    copy_text(line, sizeof(line), "STOP: ");
    vga_write_at(0, 3, line);
    vga_write_at(6, 3, info->reason ? info->reason : "KERNEL PANIC");

    if (info->detail) {
        vga_write_at(0, 5, info->detail);
    }

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), "PID ");
    append_u64_dec(line, &pos, sizeof(line), info->pid);
    append_text(line, &pos, sizeof(line), " TASK ");
    append_text(line, &pos, sizeof(line), info->task_name);
    vga_write_at(0, 7, line);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), "RIP ");
    append_u64_hex(line, &pos, sizeof(line), info->rip);
    append_text(line, &pos, sizeof(line), "  CR2 ");
    append_u64_hex(line, &pos, sizeof(line), info->cr2);
    vga_write_at(0, 8, line);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), "ERR ");
    append_u64_hex(line, &pos, sizeof(line), info->error);
    append_text(line, &pos, sizeof(line), "  VEC ");
    append_u64_dec(line, &pos, sizeof(line), info->vector);
    vga_write_at(0, 9, line);
}

static void draw_bugcheck_fb(const struct bugcheck_info *info)
{
    const struct framebuffer *fb = framebuffer_get();
    char line[128];
    uint32_t x = 32;
    uint32_t y = 28;
    uint32_t lh = LEONOS_FONT_H + 6;
    uint32_t panel_w;
    uint32_t panel_h;

    if (!fb->available) {
        draw_bugcheck_vga(info);
        return;
    }

    panel_w = fb->width > 64 ? fb->width - 64 : fb->width;
    panel_h = fb->height > 56 ? fb->height - 56 : fb->height;

    framebuffer_clear(BUGCHECK_BG);
    framebuffer_rect(x, y, panel_w, panel_h, BUGCHECK_PANEL);
    framebuffer_rect(x, y, panel_w, 2, BUGCHECK_FG);
    framebuffer_rect(x, y, 2, panel_h, BUGCHECK_FG);
    framebuffer_rect(x + panel_w - 2, y, 2, panel_h, BUGCHECK_SUB);
    framebuffer_rect(x, y + panel_h - 2, panel_w, 2, BUGCHECK_SUB);

    framebuffer_text(x + 18, y + 18, "LeonOS 4", BUGCHECK_FG, BUGCHECK_PANEL);
    framebuffer_text(x + 18, y + 18 + lh, "A fatal system error has occurred.", BUGCHECK_FG, BUGCHECK_PANEL);
    framebuffer_text(x + 18, y + 18 + lh * 2,
                     "The system has been halted to prevent further corruption.",
                     BUGCHECK_SUB, BUGCHECK_PANEL);

    copy_text(line, sizeof(line), "STOP:");
    framebuffer_text(x + 18, y + 18 + lh * 4, line, BUGCHECK_FG, BUGCHECK_PANEL);
    framebuffer_text(x + 72, y + 18 + lh * 4,
                     info->reason ? info->reason : "KERNEL PANIC",
                     BUGCHECK_FG, BUGCHECK_PANEL);

    if (info->detail && info->detail[0]) {
        framebuffer_text(x + 18, y + 18 + lh * 5, info->detail, BUGCHECK_SUB, BUGCHECK_PANEL);
    }

    {
        uint32_t row = 7;
        uint32_t pos = 0;
        line[0] = 0;
        append_text(line, &pos, sizeof(line), "PID ");
        append_u64_dec(line, &pos, sizeof(line), info->pid);
        append_text(line, &pos, sizeof(line), "  TASK ");
        append_text(line, &pos, sizeof(line), info->task_name);
        framebuffer_text(x + 18, y + 18 + lh * row++, line, BUGCHECK_FG, BUGCHECK_PANEL);

        pos = 0; line[0] = 0;
        append_text(line, &pos, sizeof(line), "VECTOR ");
        append_u64_dec(line, &pos, sizeof(line), info->vector);
        append_text(line, &pos, sizeof(line), "  ERROR ");
        append_u64_hex(line, &pos, sizeof(line), info->error);
        framebuffer_text(x + 18, y + 18 + lh * row++, line, BUGCHECK_FG, BUGCHECK_PANEL);

        pos = 0; line[0] = 0;
        append_text(line, &pos, sizeof(line), "RIP ");
        append_u64_hex(line, &pos, sizeof(line), info->rip);
        append_text(line, &pos, sizeof(line), "  CS ");
        append_u64_hex(line, &pos, sizeof(line), info->cs);
        framebuffer_text(x + 18, y + 18 + lh * row++, line, BUGCHECK_FG, BUGCHECK_PANEL);

        pos = 0; line[0] = 0;
        append_text(line, &pos, sizeof(line), "RFLAGS ");
        append_u64_hex(line, &pos, sizeof(line), info->rflags);
        framebuffer_text(x + 18, y + 18 + lh * row++, line, BUGCHECK_FG, BUGCHECK_PANEL);

        pos = 0; line[0] = 0;
        append_text(line, &pos, sizeof(line), "RSP ");
        append_u64_hex(line, &pos, sizeof(line), info->rsp);
        append_text(line, &pos, sizeof(line), "  SS ");
        append_u64_hex(line, &pos, sizeof(line), info->ss);
        framebuffer_text(x + 18, y + 18 + lh * row++, line, BUGCHECK_FG, BUGCHECK_PANEL);

        pos = 0; line[0] = 0;
        append_text(line, &pos, sizeof(line), "CR2 ");
        append_u64_hex(line, &pos, sizeof(line), info->cr2);
        framebuffer_text(x + 18, y + 18 + lh * row++, line, BUGCHECK_FG, BUGCHECK_PANEL);

        pos = 0; line[0] = 0;
        append_text(line, &pos, sizeof(line), "TICKS ");
        append_u64_dec(line, &pos, sizeof(line), info->ticks);
        append_text(line, &pos, sizeof(line), "  UPTIME_MS ");
        append_u64_dec(line, &pos, sizeof(line), info->uptime_ms);
        framebuffer_text(x + 18, y + 18 + lh * row++, line, BUGCHECK_FG, BUGCHECK_PANEL);
    }

    framebuffer_text(x + 18, y + panel_h - 32,
                     "Restart the virtual machine after collecting the information above.",
                     BUGCHECK_SUB, BUGCHECK_PANEL);
}

static __attribute__((noreturn)) void bugcheck_halt_forever(void)
{
    console_disable_framebuffer();
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static __attribute__((noreturn)) void bugcheck_commit(struct bugcheck_info *info)
{
    if (!info) {
        x86_64_halt();
    }
    if (g_bugcheck_active) {
        console_printf("[bugcheck] recursive halt reason=%s rip=0x%llx\n",
                       info->reason ? info->reason : "(null)",
                       (unsigned long long)info->rip);
        bugcheck_halt_forever();
    }
    g_bugcheck_active = 1;
    collect_bugcheck_info(info);
    console_printf("\n[bugcheck] %s\n", info->reason ? info->reason : "KERNEL PANIC");
    if (info->detail) {
        console_printf("[bugcheck] detail=%s\n", info->detail);
    }
    console_printf("[bugcheck] pid=%u task=%s vec=%llu err=0x%llx rip=0x%llx cr2=0x%llx rsp=0x%llx\n",
                   info->pid,
                   info->task_name,
                   (unsigned long long)info->vector,
                   (unsigned long long)info->error,
                   (unsigned long long)info->rip,
                   (unsigned long long)info->cr2,
                   (unsigned long long)info->rsp);
    draw_bugcheck_fb(info);
    bugcheck_halt_forever();
}

__attribute__((noreturn)) void bugcheck_panic(const char *message)
{
    struct bugcheck_info info = {
        .reason = "KERNEL PANIC",
        .detail = message ? message : "(null)",
        .vector = 0,
        .error = 0,
        .rip = 0,
        .cs = 0,
        .rflags = 0,
        .rsp = 0,
        .ss = 0,
        .cr2 = 0,
    };
    bugcheck_commit(&info);
}

__attribute__((noreturn)) void bugcheck_exception(uint64_t vector, uint64_t error,
                                                  uint64_t rip, uint64_t cs,
                                                  uint64_t rflags, uint64_t rsp,
                                                  uint64_t ss, uint64_t cr2)
{
    struct bugcheck_info info = {
        .reason = exception_name(vector),
        .detail = "Unhandled CPU exception",
        .vector = vector,
        .error = error,
        .rip = rip,
        .cs = cs,
        .rflags = rflags,
        .rsp = rsp,
        .ss = ss,
        .cr2 = cr2,
    };
    bugcheck_commit(&info);
}

__attribute__((noreturn)) void bugcheck_trap(const char *reason, const struct trap_frame *frame,
                                             uint64_t cr2)
{
    struct bugcheck_info info = {
        .reason = reason ? reason : "TRAP",
        .detail = "Trap frame captured",
        .vector = frame ? frame->vector : 0,
        .error = frame ? frame->error : 0,
        .rip = frame ? frame->rip : 0,
        .cs = frame ? frame->cs : 0,
        .rflags = frame ? frame->rflags : 0,
        .rsp = frame ? frame->rsp : 0,
        .ss = frame ? frame->ss : 0,
        .cr2 = cr2,
    };
    bugcheck_commit(&info);
}
