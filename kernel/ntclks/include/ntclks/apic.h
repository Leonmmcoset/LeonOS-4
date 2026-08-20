/*
 * LeonOS local APIC and IOAPIC capability interface.
 * The initial implementation probes capabilities without replacing the
 * stable PIC path; interrupt routing can be enabled after SMP is ready.
 */
#ifndef NTCLKS_APIC_H
#define NTCLKS_APIC_H

#include <ntclks/types.h>

void apic_init(void);
void ioapic_init(void);
bool apic_available(void);
bool ioapic_available(void);

#endif
