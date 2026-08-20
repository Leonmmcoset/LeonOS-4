/*
 * LeonOS symmetric multiprocessing interface.
 * Provides the CPU topology boundary used by the scheduler and APIC code.
 */
#ifndef NTCLKS_SMP_H
#define NTCLKS_SMP_H

#include <ntclks/types.h>

void smp_init(void);
uint32_t smp_cpu_count(void);
uint32_t smp_current_cpu(void);
bool smp_is_ready(void);

#endif
