/*
 * LeonOS boot splash implementation: renders the graphical startup screen.
 * Draws the raster LeonOS logo and a full-width progress bar before desktop.
 */
#include <generated/boot_logo.h>
#include <ntclks/boot_splash.h>
#include <ntclks/framebuffer.h>

#define BOOT_SPLASH_BACKGROUND 0x00ffffffu
#define BOOT_SPLASH_TRACK 0x00e6f2fbu
#define BOOT_SPLASH_PROGRESS 0x000078d4u

static bool splash_active;

/**
 * @brief Calculates the boot splash progress-bar height.
 * @param framebuffer Active framebuffer metadata.
 * @return Progress-bar height in physical pixels.
 */
static uint32_t boot_splash_bar_height(const struct framebuffer *framebuffer)
{
    uint32_t height;

    if (!framebuffer || !framebuffer->height) {
        return 0;
    }
    height = framebuffer->height / 90u;
    if (height < 4u) {
        height = 4u;
    }
    if (height > 10u) {
        height = 10u;
    }
    return height > framebuffer->height ? framebuffer->height : height;
}

/**
 * @brief Paints the graphical boot splash at a requested completion level.
 * @param percent Completed startup percentage, from zero through one hundred.
 */
static void boot_splash_paint(uint32_t percent)
{
    const struct framebuffer *framebuffer = framebuffer_get();
    uint32_t bar_height;
    uint32_t available_height;
    uint32_t logo_x;
    uint32_t logo_y;
    uint32_t progress_width;

    if (!framebuffer || !framebuffer->available) {
        splash_active = false;
        return;
    }
    if (percent > 100u) {
        percent = 100u;
    }
    bar_height = boot_splash_bar_height(framebuffer);
    available_height = framebuffer->height - bar_height;
    logo_x = framebuffer->width > LEONOS_BOOT_LOGO_WIDTH
                 ? (framebuffer->width - LEONOS_BOOT_LOGO_WIDTH) / 2u
                 : 0u;
    logo_y = available_height > LEONOS_BOOT_LOGO_HEIGHT
                 ? (available_height - LEONOS_BOOT_LOGO_HEIGHT) / 2u
                 : 0u;

    framebuffer_clear(BOOT_SPLASH_BACKGROUND);
    framebuffer_blit(logo_x, logo_y, LEONOS_BOOT_LOGO_WIDTH,
                     LEONOS_BOOT_LOGO_HEIGHT, LEONOS_BOOT_LOGO_WIDTH,
                     leonos_boot_logo_pixels);
    framebuffer_rect(0, framebuffer->height - bar_height, framebuffer->width,
                     bar_height, BOOT_SPLASH_TRACK);
    progress_width = (uint32_t)(((uint64_t)framebuffer->width * percent) / 100u);
    framebuffer_rect(0, framebuffer->height - bar_height, progress_width,
                     bar_height, BOOT_SPLASH_PROGRESS);
    framebuffer_present();
}

/**
 * @brief Initializes the graphical boot splash when the boot mode permits it.
 * @param enabled True to retain the splash instead of exposing boot logs.
 */
void boot_splash_init(bool enabled)
{
    splash_active = enabled && framebuffer_get()->available;
    if (splash_active) {
        boot_splash_paint(80u);
    }
}

/**
 * @brief Updates the graphical boot splash progress indicator.
 * @param percent Completed startup percentage, clamped to one hundred.
 */
void boot_splash_update(uint32_t percent)
{
    if (splash_active) {
        boot_splash_paint(percent);
    }
}

/**
 * @brief Reports whether the graphical boot splash remains active.
 * @return True when splash progress updates own the framebuffer.
 */
bool boot_splash_active(void)
{
    return splash_active;
}
