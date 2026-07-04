#include <ntclks/console.h>

#include "../arch/x86_64/port.h"

#define COM1 0x3f8

static int serial_ready;

static int serial_transmit_empty(void)
{
    return x86_64_inb(COM1 + 5) & 0x20;
}

void serial_init(void)
{
    x86_64_outb(0x00, COM1 + 1);
    x86_64_outb(0x80, COM1 + 3);
    x86_64_outb(0x03, COM1 + 0);
    x86_64_outb(0x00, COM1 + 1);
    x86_64_outb(0x03, COM1 + 3);
    x86_64_outb(0xc7, COM1 + 2);
    x86_64_outb(0x0b, COM1 + 4);
    serial_ready = 1;
}

int serial_is_ready(void)
{
    return serial_ready;
}

static void serial_putc(char ch)
{
    if (!serial_ready) {
        return;
    }
    (void)serial_transmit_empty;
    x86_64_outb((uint8_t)ch, COM1);
}

void serial_write(const char *s)
{
    while (s && *s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}
