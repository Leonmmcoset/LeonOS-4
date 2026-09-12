#ifndef LEONOS_FB_H
#define LEONOS_FB_H

#include <stdint.h>

#define LEONOS_FB_CAP_MODE_SET 0x0001U
#define LEONOS_FB_BACKEND_BOOT 0U
#define LEONOS_FB_BACKEND_BOCHS_VBE 1U
#define LEONOS_FB_BACKEND_VMWARE_SVGA 2U

/* /dev/fb0 hardware limits; fbdev's visible geometry is not a mode limit. */
#define LEONOS_FBIOGET_CAPABILITIES 0x46f0UL

struct leonos_fb_capabilities {
    uint8_t bytes_per_pixel;
    uint8_t reserved;
    uint16_t capabilities;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t max_bytes;
    uint32_t backend;
};

#endif
