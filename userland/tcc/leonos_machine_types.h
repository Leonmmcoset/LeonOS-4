/*
 * TinyCC target type bridge for the LeonOS x86_64 ABI.
 *
 * Picolibc's generic machine type header normally learns these definitions
 * from GCC or Clang predefined macros.  TinyCC 0.9.27 knows the target ABI,
 * but does not expose the complete GCC-compatible type-macro set to headers.
 * Define the stable ABI facts before including Picolibc's generic machinery.
 */
#ifndef _MACHINE__TYPES_H
#define _MACHINE__TYPES_H

#include <machine/leonos_tcc_builtin_types.h>
#include <machine/_default_types.h>

#endif /* _MACHINE__TYPES_H */
