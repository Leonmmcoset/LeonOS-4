/*
 * LeonOS boot splash interface: controls the graphical startup animation.
 * Keeps the loader's splash visible while the kernel initializes services.
 */
#ifndef NTCLKS_BOOT_SPLASH_H
#define NTCLKS_BOOT_SPLASH_H

#include <ntclks/types.h>

/**
 * @brief Initializes the graphical boot splash when it is enabled.
 * @param enabled Whether the graphical splash is selected for this boot.
 */
void boot_splash_init(bool enabled);

/**
 * @brief Updates the graphical boot splash progress indicator.
 * @param percent Completed startup percentage, from zero through one hundred.
 */
void boot_splash_update(uint32_t percent);

/**
 * @brief Reports whether the graphical boot splash owns the framebuffer.
 * @return True when the splash is currently active.
 */
bool boot_splash_active(void);

#endif
