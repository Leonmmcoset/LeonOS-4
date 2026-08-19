/*
 * LeonOS x86_64 SMP bootstrap boundary.
 * AP startup is intentionally staged; current scheduling remains on BSP.
 */
#include <ntclks/console.h>
#include <ntclks/smp.h>

static uint32_t cpu_count = 1;
static bool smp_ready;

void smp_init(void)
{
    cpu_count = 1;
    smp_ready = false;
    console_printf("[ntclks] SMP bootstrap staged; CPUs=%u BSP scheduler active\n",
                   (unsigned)cpu_count);
}

uint32_t smp_cpu_count(void)
{
    return cpu_count;
}

uint32_t smp_current_cpu(void)
{
    return 0;
}

bool smp_is_ready(void)
{
    return smp_ready;
}
