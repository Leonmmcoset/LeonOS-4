/*
 * LeonOS local APIC and IOAPIC capability interface.
 * The BSP uses either IOAPIC routes or the LAPIC virtual-wire bridge for
 * legacy hardware IRQs.  AP startup relies on the same LAPIC interface.
 */
#ifndef NTCLKS_APIC_H
#define NTCLKS_APIC_H

#include <ntclks/types.h>

void apic_init(void);
void ioapic_init(void);
bool apic_available(void);
bool ioapic_available(void);
uint32_t apic_id(void);
uint32_t apic_bsp_id(void);
uint32_t apic_cpu_count(void);
uint32_t apic_cpu_id_at(uint32_t index);
void apic_enable(void);
bool apic_enabled(void);
void apic_enable_legacy_pic(void);
void apic_timer_init(uint8_t vector, uint32_t initial_count);
void apic_eoi(void);
void apic_send_init(uint32_t destination);
void apic_send_startup(uint32_t destination, uint8_t vector);
bool ioapic_route_irq(uint32_t irq, uint8_t vector, uint32_t destination);

#endif
