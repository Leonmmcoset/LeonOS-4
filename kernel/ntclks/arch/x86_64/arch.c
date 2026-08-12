/*
 * LeonOS x86_64 architecture support: initializes processor control state.
 * Provides CPU feature setup and the transition into user execution.
 */
#include <ntclks/console.h>
#include <ntclks/types.h>

#define X86_CR0_MP (1ULL << 1)
#define X86_CR0_EM (1ULL << 2)
#define X86_CR0_TS (1ULL << 3)
#define X86_CR0_NE (1ULL << 5)
#define X86_CR4_OSFXSR (1ULL << 9)
#define X86_CR4_OSXMMEXCPT (1ULL << 10)

static uint8_t initial_fpu_state[512] __attribute__((aligned(16)));

static void copy_fpu_state(void *dst, const void *src)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    for (uint32_t i = 0; i < sizeof(initial_fpu_state); ++i) {
        out[i] = in[i];
    }
}

void arch_init(void)
{
    console_printf("[ntclks] arch/x86_64 initialized\n");
}

void arch_fpu_init(void)
{
    uint64_t cr0;
    uint64_t cr4;
    uint32_t default_mxcsr = 0x1f80U;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(X86_CR0_EM | X86_CR0_TS);
    cr0 |= X86_CR0_MP | X86_CR0_NE;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= X86_CR4_OSFXSR | X86_CR4_OSXMMEXCPT;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

    __asm__ volatile("fninit\n\tldmxcsr %0" : : "m"(default_mxcsr) : "memory");
    __asm__ volatile("fxsave64 (%0)" : : "r"(initial_fpu_state) : "memory");
    console_printf("[ntclks] x87/SSE task state enabled\n");
}

void arch_fpu_task_init(void *state)
{
    copy_fpu_state(state, initial_fpu_state);
}

void arch_fpu_save(void *state)
{
    __asm__ volatile("fxsave64 (%0)" : : "r"(state) : "memory");
}

void arch_fpu_restore(const void *state)
{
    __asm__ volatile("fxrstor64 (%0)" : : "r"(state) : "memory");
}
