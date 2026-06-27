#include <ntclks/console.h>
#include <ntclks/power.h>

#include "port.h"

static void io_delay(void)
{
    for (volatile uint32_t i = 0; i < 100000; ++i) {
        __asm__ volatile("pause");
    }
}

void power_reboot(void)
{
    console_printf("[ntclks] reboot requested\n");
    for (;;) {
        while (x86_64_inb(0x64) & 0x02) {
        }
        x86_64_outb(0xfe, 0x64);
        io_delay();
    }
}

void power_shutdown(void)
{
    console_printf("[ntclks] shutdown requested\n");
    x86_64_outw(0x2000, 0x604);
    io_delay();
    x86_64_outw(0x3400, 0xb004);
    io_delay();
    x86_64_outw(0x2000, 0x4004);
    io_delay();
    x86_64_outl(0x2000, 0x604);
    x86_64_halt();
}
