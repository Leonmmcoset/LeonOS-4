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

#ifndef __SCHAR_MAX__
#define __SCHAR_MAX__ 127
#endif
#ifndef __SHRT_MAX__
#define __SHRT_MAX__ 32767
#endif
#ifndef __INT_MAX__
#define __INT_MAX__ 2147483647
#endif
#ifndef __LONG_MAX__
#define __LONG_MAX__ 9223372036854775807L
#endif
#ifndef __LONG_LONG_MAX__
#define __LONG_LONG_MAX__ 9223372036854775807LL
#endif

#ifndef __SCHAR_WIDTH__
#define __SCHAR_WIDTH__ 8
#endif
#ifndef __SHRT_WIDTH__
#define __SHRT_WIDTH__ 16
#endif
#ifndef __INT_WIDTH__
#define __INT_WIDTH__ 32
#endif
#ifndef __LONG_WIDTH__
#define __LONG_WIDTH__ 64
#endif
#ifndef __LONG_LONG_WIDTH__
#define __LONG_LONG_WIDTH__ 64
#endif

#ifndef __SIZEOF_SHORT__
#define __SIZEOF_SHORT__ 2
#endif
#ifndef __SIZEOF_INT__
#define __SIZEOF_INT__ 4
#endif
#ifndef __SIZEOF_LONG__
#define __SIZEOF_LONG__ 8
#endif
#ifndef __SIZEOF_LONG_LONG__
#define __SIZEOF_LONG_LONG__ 8
#endif
#ifndef __SIZEOF_POINTER__
#define __SIZEOF_POINTER__ 8
#endif

#ifndef __INT8_TYPE__
#define __INT8_TYPE__ signed char
#endif
#ifndef __UINT8_TYPE__
#define __UINT8_TYPE__ unsigned char
#endif
#ifndef __INT16_TYPE__
#define __INT16_TYPE__ short
#endif
#ifndef __UINT16_TYPE__
#define __UINT16_TYPE__ unsigned short
#endif
#ifndef __INT32_TYPE__
#define __INT32_TYPE__ int
#endif
#ifndef __UINT32_TYPE__
#define __UINT32_TYPE__ unsigned int
#endif
#ifndef __INT64_TYPE__
#define __INT64_TYPE__ long
#endif
#ifndef __UINT64_TYPE__
#define __UINT64_TYPE__ unsigned long
#endif
#ifndef __INTMAX_TYPE__
#define __INTMAX_TYPE__ long
#endif
#ifndef __UINTMAX_TYPE__
#define __UINTMAX_TYPE__ unsigned long
#endif
#ifndef __SIZE_TYPE__
#define __SIZE_TYPE__ unsigned long
#endif
#ifndef __PTRDIFF_TYPE__
#define __PTRDIFF_TYPE__ long
#endif
#ifndef __INTPTR_TYPE__
#define __INTPTR_TYPE__ long
#endif
#ifndef __UINTPTR_TYPE__
#define __UINTPTR_TYPE__ unsigned long
#endif
#ifndef __WCHAR_TYPE__
#define __WCHAR_TYPE__ int
#endif
#ifndef __WINT_TYPE__
#define __WINT_TYPE__ unsigned int
#endif

#include <machine/_default_types.h>

#endif /* _MACHINE__TYPES_H */
