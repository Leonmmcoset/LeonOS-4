/*
 * LeonOS kernel-debug state and module entry interface.
 * Keeps diagnostic-mode persistence, module loading, and boot handoff policy
 * outside normal syscall and userland bootstrap code.
 */
#ifndef NTCLKS_KERNEL_DEBUG_H
#define NTCLKS_KERNEL_DEBUG_H

#include <leonos/boot_handoff.h>
#include <leonos/kernel_debug.h>
#include <ntclks/types.h>

#define LEONOS_KERNEL_DEBUG_MODULE_ABI 1U
#define LEONOS_KERNEL_DEBUG_NOTE_TYPE 0x4c4b4447U
#define LEONOS_KERNEL_DEBUG_NOTE_NAME "LEONKDBG"

struct leonos_kernel_debug_api {
    uint32_t version;
    uint32_t reserved;
    void (*write)(const char *text);
    uint64_t (*rdtsc)(void);
    void (*yield)(void);
    void (*tui_init)(void);
    void (*tui_write)(const char *text);
    void (*tui_write_u64)(uint64_t value);
    int (*tui_key)(void);
    void (*tui_clear)(void);
    int (*run_safe_benchmarks)(void *report, uint32_t capacity, uint32_t *count);
    int (*run_dangerous_benchmarks)(void *report, uint32_t capacity, uint32_t *count);
    int (*clear_state)(void);
    void (*reboot)(void);
    void (*shutdown)(void);
};

struct leonos_kernel_debug_module_note {
    uint32_t abi;
    uint32_t entry_name_hash;
};

struct leonos_kernel_debug_benchmark {
    char name[48];
    uint64_t minimum_cycles;
    uint64_t average_cycles;
    uint64_t maximum_cycles;
    int32_t result;
    uint32_t iterations;
    uint32_t errors;
    uint32_t status;
    uint32_t kind;
};

#define LEONOS_KERNEL_DEBUG_BENCH_OK 0U
#define LEONOS_KERNEL_DEBUG_BENCH_EXPECTED_ERROR 1U
#define LEONOS_KERNEL_DEBUG_BENCH_SKIPPED 2U
#define LEONOS_KERNEL_DEBUG_BENCH_FAILED 3U
#define LEONOS_KERNEL_DEBUG_BENCH_SYSCALL 1U
#define LEONOS_KERNEL_DEBUG_BENCH_IOCTL 2U

typedef int (*leonos_kernel_debug_entry_fn)(const struct leonos_kernel_debug_api *api);

int kernel_debug_control(struct leonos_kernel_debug_control *control);
bool kernel_debug_boot_requested(const struct leonos_boot_handoff *handoff);
int kernel_debug_run_module(void);
int kernel_debug_clear_state(void);

#endif
