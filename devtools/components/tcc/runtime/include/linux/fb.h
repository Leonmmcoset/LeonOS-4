#ifndef LEONOS_UAPI_LINUX_FB_H
#define LEONOS_UAPI_LINUX_FB_H

#include <stdint.h>

struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
};

struct fb_fix_screeninfo {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t line_length;
};

#define FBIOGET_VSCREENINFO 0x4600UL
#define FBIOPUT_VSCREENINFO 0x4601UL
#define FBIOGET_FSCREENINFO 0x4602UL
/* Linux fbdev pan/flush request.  On VMware SVGA the visible surface is
 * refreshed from VRAM only when the host receives an update command, so
 * mmap writers need this to push frames to the display. */
#define FBIOPAN_DISPLAY 0x4606UL

#endif
