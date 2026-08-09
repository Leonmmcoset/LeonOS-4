#ifndef NTCLKS_PORT_H
#define NTCLKS_PORT_H

#include <ntclks/types.h>

void x86_64_outb(uint8_t value, uint16_t port);
uint8_t x86_64_inb(uint16_t port);
void x86_64_outw(uint16_t value, uint16_t port);
uint16_t x86_64_inw(uint16_t port);
void x86_64_outl(uint32_t value, uint16_t port);
uint32_t x86_64_inl(uint16_t port);

#endif
