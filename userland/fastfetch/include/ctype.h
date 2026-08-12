#pragma once

#include_next <ctype.h>

#ifndef isascii
#define isascii(character) (((unsigned int)(character)) <= 0x7fU)
#endif
