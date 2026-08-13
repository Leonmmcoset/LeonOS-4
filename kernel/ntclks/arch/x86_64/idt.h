/*
 * LeonOS x86_64 IDT interface: declares interrupt-table initialization.
 * Shared by architecture startup and the interrupt implementation.
 */
#ifndef NTCLKS_X86_64_IDT_H
#define NTCLKS_X86_64_IDT_H

/**
 * @brief Coordinates the idt init operation.
 */
void idt_init(void);

#endif
