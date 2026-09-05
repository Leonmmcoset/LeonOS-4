#ifndef LEONOS_SVGA_PROTOCOL_H
#define LEONOS_SVGA_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
typedef uint32_t uint32;
typedef int32_t int32;
typedef uint16_t uint16;
typedef uint8_t uint8;
typedef int Bool;
#include "protocol/svga_reg.h"
#include "protocol/svga3d_reg.h"

/* Newer device registers absent from the vendored legacy protocol headers.
 * Linux vmwgfx/device_include/svga_reg.h defines these wire constants. */
#define SVGA_CAP_GBOBJECTS 0x08000000u
#define SVGA_REG_DEV_CAP 52u
#include "gb_protocol.h"

#endif
