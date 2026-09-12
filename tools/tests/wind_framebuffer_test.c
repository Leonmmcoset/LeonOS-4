#include <assert.h>
#include <stdarg.h>
#include <sys/un.h>

#define mmap test_mmap
#define munmap test_munmap
#include "../../userland/libc/src/wind.c"
#undef mmap
#undef munmap

static uint32_t vram[1920 * 1080];
static uint32_t display_width = 1280, display_height = 800;
static struct leonos_fb_capabilities hardware = {
    .bytes_per_pixel = 4, .capabilities = LEONOS_FB_CAP_MODE_SET,
    .max_width = 4096, .max_height = 4096, .max_bytes = 128 * 1024 * 1024,
    .backend = LEONOS_FB_BACKEND_VMWARE_SVGA};
static size_t mapped_bytes;
static unsigned maps, unmaps;
static int map_failure;
static unsigned damage_calls;

int open(const char *path, int flags, ...)
{
    (void)flags;
    assert(strcmp(path, LEONOS_DEV_FB0) == 0);
    return 42;
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    va_start(args, request);
    void *arg = va_arg(args, void *);
    va_end(args);
    assert(fd == 42);
    if (request == FBIOGET_VSCREENINFO) {
        *(struct fb_var_screeninfo *)arg = (struct fb_var_screeninfo){
            .xres = display_width, .yres = display_height, .bits_per_pixel = 32};
    } else if (request == FBIOGET_FSCREENINFO) {
        *(struct fb_fix_screeninfo *)arg = (struct fb_fix_screeninfo){
            .line_length = display_width * 4, .smem_len = display_width * display_height * 4};
    } else if (request == FBIOPUT_VSCREENINFO) {
        struct fb_var_screeninfo *mode = arg;
        if (mode->xres == 1234) { errno = EINVAL; return -1; }
        display_width = mode->xres;
        display_height = mode->yres;
    } else if (request == 0x46f1UL) {
        const uint32_t *rect = arg;
        assert(rect[0] == 1919 && rect[1] == 1079 && rect[2] == 1 && rect[3] == 1);
        ++damage_calls;
    } else if (request == FBIOPAN_DISPLAY) {
        assert(!"A one-pixel blit must not force a full-screen VMware update");
    } else {
        /* The device capability query must carry the real VRAM limits. */
        assert(request == 0x46f0UL);
        *(struct leonos_fb_capabilities *)arg = hardware;
    }
    return 0;
}

void *test_mmap(void *addr, size_t bytes, int prot, int flags, int fd, off_t off)
{
    (void)addr; (void)prot; (void)flags;
    assert(fd == 42 && off == 0 && bytes <= sizeof(vram));
    ++maps;
    if (map_failure) { errno = ENOMEM; return MAP_FAILED; }
    assert(!mapped_bytes);
    mapped_bytes = bytes;
    return vram;
}

int test_munmap(void *addr, size_t bytes)
{
    assert(addr == vram && bytes == mapped_bytes);
    mapped_bytes = 0;
    ++unmaps;
    return 0;
}

int main(void)
{
    struct leonos_fb_capabilities caps;
    assert(leonos_fb_capabilities(&caps) == 0);
    assert(caps.max_bytes == 128 * 1024 * 1024 && caps.max_width == 4096 &&
           caps.max_height == 4096 && caps.backend == LEONOS_FB_BACKEND_VMWARE_SVGA);
    assert(caps.max_bytes >= 1920 * 1080 * 4);
    assert(leonos_fb_rect(1279, 799, 1, 1, 0x123456) == 0);
    assert(vram[1280 * 800 - 1] == 0x123456 && maps == 1);
    assert(leonos_fb_set_mode(1234, 800) < 0);
    assert(unmaps == 0 && leonos_fb_pixel(1279, 799) == 0x123456);
    assert(leonos_fb_set_mode(1920, 1080) == 0);
    const uint32_t pixel = 0xabcdef;
    assert(leonos_fb_blit(1919, 1079, 1, 1, 1, &pixel) == 0);
    assert(mapped_bytes == sizeof(vram) && maps == 2 && unmaps == 1);
    assert(vram[1920 * 1080 - 1] == pixel);
    assert(damage_calls == 1);
    assert(leonos_fb_set_mode(1280, 800) == 0);
    assert(leonos_fb_pixel(0, 0) == 0 && maps == 3 && unmaps == 2);

    /* A mode change by another process must also refresh our mapping. */
    display_width = 1024;
    display_height = 768;
    assert(leonos_fb_rect(1023, 767, 1, 1, 42) == 0);
    assert(mapped_bytes == 1024 * 768 * 4 && maps == 4 && unmaps == 3);
    assert(leonos_fb_set_mode(1280, 800) == 0);
    map_failure = 1;
    assert(leonos_fb_rect(0, 0, 1, 1, 0) < 0 && !mapped_bytes);
    map_failure = 0;
    assert(leonos_fb_rect(0, 0, 1, 1, 42) == 0);

    hardware = (struct leonos_fb_capabilities){.bytes_per_pixel = 4,
        .max_width = 1280, .max_height = 800, .max_bytes = 1280 * 800 * 4};
    assert(leonos_fb_capabilities(&caps) == 0);
    assert(!caps.capabilities && caps.backend == LEONOS_FB_BACKEND_BOOT &&
           caps.max_width == 1280 && caps.max_bytes == 1280 * 800 * 4);
    puts("Framebuffer capabilities and remapping survive grow/shrink, failed and external mode changes");
    return 0;
}
