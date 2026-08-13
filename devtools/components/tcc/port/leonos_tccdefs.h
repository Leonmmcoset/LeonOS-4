/*
 * LeonOS target predefines for TinyCC's x86_64 LP64 sysroot.
 *
 * This file is included by TinyCC's target tccdefs.h. It is deliberately
 * kept on the compiler side of the boundary: Picolibc headers are staged
 * byte-for-byte and do not receive build-time edits.
 */
#ifndef LEONOS_TCCDEFS_H
#define LEONOS_TCCDEFS_H

#undef __linux__
#undef __linux
#define __LEONOS__ 1
#define __leonos__ 1
#define __ELF__ 1
#define __LP64__ 1

#undef __SCHAR_MAX__
#define __SCHAR_MAX__ 127
#undef __SHRT_MAX__
#define __SHRT_MAX__ 32767
#undef __INT_MAX__
#define __INT_MAX__ 2147483647
#undef __LONG_MAX__
#define __LONG_MAX__ 9223372036854775807L
#undef __LONG_LONG_MAX__
#define __LONG_LONG_MAX__ 9223372036854775807LL

#undef __SCHAR_WIDTH__
#define __SCHAR_WIDTH__ 8
#undef __SHRT_WIDTH__
#define __SHRT_WIDTH__ 16
#undef __INT_WIDTH__
#define __INT_WIDTH__ 32
#undef __LONG_WIDTH__
#define __LONG_WIDTH__ 64
#undef __LONG_LONG_WIDTH__
#define __LONG_LONG_WIDTH__ 64

#undef __SIZEOF_CHAR__
#define __SIZEOF_CHAR__ 1
#undef __SIZEOF_SHORT__
#define __SIZEOF_SHORT__ 2
#undef __SIZEOF_INT__
#define __SIZEOF_INT__ 4
#undef __SIZEOF_LONG__
#define __SIZEOF_LONG__ 8
#undef __SIZEOF_LONG_LONG__
#define __SIZEOF_LONG_LONG__ 8
#undef __SIZEOF_POINTER__
#define __SIZEOF_POINTER__ 8
#undef __SIZEOF_FLOAT__
#define __SIZEOF_FLOAT__ 4
#undef __SIZEOF_DOUBLE__
#define __SIZEOF_DOUBLE__ 8
#undef __SIZEOF_LONG_DOUBLE__
#define __SIZEOF_LONG_DOUBLE__ 16
#undef __SIZEOF_WCHAR_T__
#define __SIZEOF_WCHAR_T__ 4
#undef __SIZEOF_WINT_T__
#define __SIZEOF_WINT_T__ 4
#undef __SIZEOF_SIZE_T__
#define __SIZEOF_SIZE_T__ 8
#undef __SIZEOF_PTRDIFF_T__
#define __SIZEOF_PTRDIFF_T__ 8

#undef __FLT_MANT_DIG__
#define __FLT_MANT_DIG__ 24
#undef __DBL_MANT_DIG__
#define __DBL_MANT_DIG__ 53
#undef __LDBL_MANT_DIG__
#define __LDBL_MANT_DIG__ 64

#undef __INT8_TYPE__
#define __INT8_TYPE__ signed char
#undef __UINT8_TYPE__
#define __UINT8_TYPE__ unsigned char
#undef __INT16_TYPE__
#define __INT16_TYPE__ short
#undef __UINT16_TYPE__
#define __UINT16_TYPE__ unsigned short
#undef __INT32_TYPE__
#define __INT32_TYPE__ int
#undef __UINT32_TYPE__
#define __UINT32_TYPE__ unsigned int
#undef __INT64_TYPE__
#define __INT64_TYPE__ long
#undef __UINT64_TYPE__
#define __UINT64_TYPE__ unsigned long
#undef __INT_LEAST8_TYPE__
#define __INT_LEAST8_TYPE__ signed char
#undef __UINT_LEAST8_TYPE__
#define __UINT_LEAST8_TYPE__ unsigned char
#undef __INT_LEAST16_TYPE__
#define __INT_LEAST16_TYPE__ short
#undef __UINT_LEAST16_TYPE__
#define __UINT_LEAST16_TYPE__ unsigned short
#undef __INT_LEAST32_TYPE__
#define __INT_LEAST32_TYPE__ int
#undef __UINT_LEAST32_TYPE__
#define __UINT_LEAST32_TYPE__ unsigned int
#undef __INT_LEAST64_TYPE__
#define __INT_LEAST64_TYPE__ long
#undef __UINT_LEAST64_TYPE__
#define __UINT_LEAST64_TYPE__ unsigned long
#undef __INTMAX_TYPE__
#define __INTMAX_TYPE__ long
#undef __UINTMAX_TYPE__
#define __UINTMAX_TYPE__ unsigned long
#undef __INTPTR_TYPE__
#define __INTPTR_TYPE__ long
#undef __UINTPTR_TYPE__
#define __UINTPTR_TYPE__ unsigned long
#undef __SIZE_TYPE__
#define __SIZE_TYPE__ unsigned long
#undef __PTRDIFF_TYPE__
#define __PTRDIFF_TYPE__ long
#undef __WCHAR_TYPE__
#define __WCHAR_TYPE__ int
#undef __WINT_TYPE__
#define __WINT_TYPE__ unsigned int

#undef __BYTE_ORDER__
#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__

#ifndef __has_extension
#define __has_extension(x) 0
#endif

#endif /* LEONOS_TCCDEFS_H */
