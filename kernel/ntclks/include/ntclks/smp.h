/*
 * LeonOS symmetric multiprocessing interface.
 * Provides the CPU topology boundary used by the scheduler and APIC code.
 */
#ifndef NTCLKS_SMP_H
#define NTCLKS_SMP_H

#include <ntclks/types.h>

#define SMP_MAX_CPUS 64u

struct smp_cpu_info {
    uint32_t apic_id;
    uint64_t stack;
    volatile uint32_t online;
    volatile uint32_t started;
};

void smp_init(void);
uint32_t smp_cpu_count(void);
uint32_t smp_current_cpu(void);
bool smp_is_ready(void);
void smp_start_aps(void);
void smp_mark_bsp_user_entry(void);
/* Release APs after the BSP has completed initial userland construction. */
void smp_release_aps(void);
void smp_ap_entry(uint32_t cpu_index) __attribute__((noreturn));
bool smp_cpu_online(uint32_t cpu_index);
const struct smp_cpu_info *smp_cpu_info(uint32_t cpu_index);

#endif
