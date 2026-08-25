/*
 * LeonOS x86_64 IDT interface: declares interrupt-table initialization.
 * Shared by architecture startup and the interrupt implementation.
 */
#ifndef NTCLKS_X86_64_IDT_H
#define NTCLKS_X86_64_IDT_H

/**
 * Idt init.
 */
void idt_init(void);

/**
 * Load the already initialized shared IDT on another CPU.
 *
 * The IDT entries are process-wide kernel text pointers and are immutable
 * after boot.  APs must only load the table; rebuilding it concurrently can
 * expose partially written gate descriptors to another CPU.
 */
void idt_load(void);

#endif
