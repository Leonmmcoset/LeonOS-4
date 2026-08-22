/*
 * LeonOS port interface: declares low-level platform I/O abstractions.
 * Keeps architecture-specific port access behind a stable kernel header.
 */
#ifndef NTCLKS_PORT_H
#define NTCLKS_PORT_H

#include <ntclks/types.h>

/**
 * @brief Write the byte value to the I/O port.
 */
void x86_64_outb(uint8_t value, uint16_t port);
/**
 * @brief Read a byte from the I/O port.
 */
uint8_t x86_64_inb(uint16_t port);
/**
 * @brief Write the 16-bit value to the I/O port.
 */
void x86_64_outw(uint16_t value, uint16_t port);
/**
 * @brief Read a 16-bit value from the I/O port.
 */
uint16_t x86_64_inw(uint16_t port);
/**
 * @brief Write the 32-bit value to the I/O port.
 */
void x86_64_outl(uint32_t value, uint16_t port);
/**
 * @brief Read a 32-bit value from the I/O port.
 */
uint32_t x86_64_inl(uint16_t port);

#endif
