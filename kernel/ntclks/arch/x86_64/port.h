/*
 * LeonOS x86_64 port I/O interface: declares inb/outb-style hardware access.
 * Used by legacy PCI, serial, power, and bootstrap device code.
 */
#ifndef NTCLKS_X86_64_PORT_H
#define NTCLKS_X86_64_PORT_H

#include <stdint.h>

/**
 * X86 64 outb.
 * @param value Value supplied by the caller.
 * @param port Value supplied by the caller.
 */
void x86_64_outb(uint8_t value, uint16_t port);
/**
 * X86 64 inb.
 * @param port Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
uint8_t x86_64_inb(uint16_t port);
/**
 * X86 64 outw.
 * @param value Value supplied by the caller.
 * @param port Value supplied by the caller.
 */
void x86_64_outw(uint16_t value, uint16_t port);
/**
 * X86 64 inw.
 * @param port Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
uint16_t x86_64_inw(uint16_t port);
/**
 * X86 64 outl.
 * @param value Value supplied by the caller.
 * @param port Value supplied by the caller.
 */
void x86_64_outl(uint32_t value, uint16_t port);
/**
 * X86 64 inl.
 * @param port Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
uint32_t x86_64_inl(uint16_t port);
/**
 *   attribute  .
 */
__attribute__((noreturn)) void x86_64_halt(void);

#endif
