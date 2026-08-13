/*
 * LeonOS port interface: declares low-level platform I/O abstractions.
 * Keeps architecture-specific port access behind a stable kernel header.
 */
#ifndef NTCLKS_PORT_H
#define NTCLKS_PORT_H

#include <ntclks/types.h>

/**
 * @brief Coordinates the x86 64 outb operation.
 * @param value Input or output value used by this operation.
 * @param port Input or output value used by this operation.
 */
void x86_64_outb(uint8_t value, uint16_t port);
/**
 * @brief Coordinates the x86 64 inb operation.
 * @param port Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t x86_64_inb(uint16_t port);
/**
 * @brief Coordinates the x86 64 outw operation.
 * @param value Input or output value used by this operation.
 * @param port Input or output value used by this operation.
 */
void x86_64_outw(uint16_t value, uint16_t port);
/**
 * @brief Coordinates the x86 64 inw operation.
 * @param port Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint16_t x86_64_inw(uint16_t port);
/**
 * @brief Coordinates the x86 64 outl operation.
 * @param value Input or output value used by this operation.
 * @param port Input or output value used by this operation.
 */
void x86_64_outl(uint32_t value, uint16_t port);
/**
 * @brief Coordinates the x86 64 inl operation.
 * @param port Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t x86_64_inl(uint16_t port);

#endif
