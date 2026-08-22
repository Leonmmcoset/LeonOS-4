/*
 * LeonOS x86_64 GDT setup: defines kernel and user segment descriptors.
 * Loads the descriptor table required for protected-mode execution.
 */
#include <ntclks/arch.h>
#include <ntclks/console.h>
#include <ntclks/framebuffer.h>

struct __attribute__((packed)) gdt_ptr {
    uint16_t limit;
    uint64_t base;
};

struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
};

static uint64_t gdt[7];
static struct tss64 tss;

#define IDENTITY_MAP_LIMIT (1ULL << 32)

extern void x86_64_lgdt(const struct gdt_ptr *ptr);
extern void x86_64_load_segments(void);
extern void x86_64_ltr(uint16_t selector);
void paging_init_user_identity(void);

/**
 * Descriptor.
 * @param base Value supplied by the caller.
 * @param limit Value supplied by the caller.
 * @param access Identifier or flags controlling the operation.
 * @param flags Identifier or flags controlling the operation.
 * @return The value or status produced by the operation.
 */
static uint64_t descriptor(uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    uint64_t desc = 0;
    desc |= limit & 0xffffULL;
    desc |= (base & 0xffffffULL) << 16;
    desc |= (uint64_t)access << 40;
    desc |= ((uint64_t)(limit >> 16) & 0x0f) << 48;
    desc |= ((uint64_t)flags & 0x0f) << 52;
    desc |= ((uint64_t)(base >> 24) & 0xff) << 56;
    return desc;
}

/**
 * Set tss descriptor.
 * @param index Identifier or flags controlling the operation.
 * @param base Value supplied by the caller.
 * @param limit Value supplied by the caller.
 */
static void set_tss_descriptor(uint32_t index, uint64_t base, uint32_t limit)
{
    uint64_t low = 0;
    low |= limit & 0xffffULL;
    low |= (base & 0xffffffULL) << 16;
    low |= 0x89ULL << 40;
    low |= ((uint64_t)(limit >> 16) & 0x0f) << 48;
    low |= ((base >> 24) & 0xffULL) << 56;

    uint64_t high = base >> 32;
    gdt[index] = low;
    gdt[index + 1] = high;
}

/**
 * Framebuffer survives identity map.
 * @return The value or status produced by the operation.
 */
static int framebuffer_survives_identity_map(void)
{
    const struct framebuffer *fb = framebuffer_get();
    uint64_t start;
    uint64_t bytes;

    if (!fb || !fb->available || !fb->pixels || !fb->pitch || !fb->height) {
        return 0;
    }
    start = (uint64_t)(uintptr_t)fb->pixels;
    bytes = (uint64_t)fb->pitch * fb->height;
    return start < IDENTITY_MAP_LIMIT && bytes <= IDENTITY_MAP_LIMIT - start;
}

/**
 * Arch userland init.
 * @param kernel_stack_top Value supplied by the caller.
 */
void arch_userland_init(void *kernel_stack_top)
{
    if (!framebuffer_survives_identity_map()) {
        console_disable_framebuffer();
    }
    tss.rsp0 = (uint64_t)(uintptr_t)kernel_stack_top;
    tss.io_map_base = sizeof(tss);

    gdt[0] = 0;
    gdt[1] = descriptor(0, 0xfffff, 0x9a, 0x0a);
    gdt[2] = descriptor(0, 0xfffff, 0x92, 0x0c);
    gdt[3] = descriptor(0, 0xfffff, 0xf2, 0x0c);
    gdt[4] = descriptor(0, 0xfffff, 0xfa, 0x0a);
    set_tss_descriptor(5, (uint64_t)(uintptr_t)&tss, sizeof(tss) - 1);

    struct gdt_ptr ptr = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)(uintptr_t)gdt,
    };
    x86_64_lgdt(&ptr);
    x86_64_load_segments();
    x86_64_ltr(0x28);
    paging_init_user_identity();
    arch_fpu_init();
    (void)kernel_stack_top;
}
