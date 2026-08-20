/*
 * LeonOS kernel heap interface.
 * Allocates page-backed kernel memory with explicit ownership and release.
 */
#ifndef NTCLKS_HEAP_H
#define NTCLKS_HEAP_H

#include <ntclks/types.h>

void kernel_heap_init(void);
void *kernel_malloc(size_t size);
void kernel_free(void *memory);

#endif
