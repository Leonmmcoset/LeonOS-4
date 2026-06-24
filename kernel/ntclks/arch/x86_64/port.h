#ifndef NTCLKS_X86_64_PORT_H
#define NTCLKS_X86_64_PORT_H

#include <stdint.h>

void x86_64_outb(uint8_t value, uint16_t port);
uint8_t x86_64_inb(uint16_t port);
__attribute__((noreturn)) void x86_64_halt(void);

#endif
