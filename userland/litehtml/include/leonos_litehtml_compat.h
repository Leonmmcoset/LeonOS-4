/*
 * LeonOS freestanding C++ profile for upstream litehtml.
 *
 * LiteHTML is built without exceptions, RTTI, threads, locale, or wide
 * character support.  The libc++ headers still need the C ctype declarations
 * because a few upstream translation units use the global isspace family.
 */
#ifndef LEONOS_LITEHTML_COMPAT_H
#define LEONOS_LITEHTML_COMPAT_H

#define _LIBCPP_HAS_NO_THREADS 1
#define _LIBCPP_HAS_NO_MONOTONIC_CLOCK 1
#define _LIBCPP_HAS_NO_FILESYSTEM 1
#define _LIBCPP_HAS_NO_LOCALIZATION 1
#define _LIBCPP_HAS_NO_WIDE_CHARACTERS 1
#define _LIBCPP_HAS_NO_EXCEPTIONS 1
#define LITEHTML_NO_THREADS 1
#define LEONOS_LITEHTML_NO_EXCEPTIONS 1
#define LEONOS_LITEHTML_NO_RTTI 1

#include <ctype.h>

#endif
