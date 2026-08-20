/*
 * LeonOS x86_64 APIC probing.
 * Keeps legacy PIC routing active while exposing the modern interrupt path.
 */
#include <ntclks/apic.h>
#include <ntclks/console.h>

#define CPUID_FEATURE_APIC (1u << 9)
#define IA32_APIC_BASE 0x1bu

static bool local_apic_present;
static bool ioapic_present;

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

static uint64_t read_msr(uint32_t msr)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void apic_init(void)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint64_t base;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    local_apic_present = (edx & CPUID_FEATURE_APIC) != 0;
    base = local_apic_present ? read_msr(IA32_APIC_BASE) : 0;
    console_printf("[ntclks] local APIC %s base=0x%llx (PIC routing retained)\n",
                   local_apic_present ? "detected" : "unavailable",
                   (unsigned long long)(base & 0xfffff000ULL));
}

void ioapic_init(void)
{
    /* ACPI IOAPIC enumeration will be connected to the parsed MADT next. */
    ioapic_present = false;
    console_printf("[ntclks] IOAPIC routing deferred to ACPI MADT support\n");
}

bool apic_available(void)
{
    return local_apic_present;
}

bool ioapic_available(void)
{
    return ioapic_present;
}
