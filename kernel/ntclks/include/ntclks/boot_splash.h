/*
 * LeonOS boot splash interface: controls the graphical startup animation.
 * Keeps the loader's splash visible while the kernel initializes services.
 */
#ifndef NTCLKS_BOOT_SPLASH_H
#define NTCLKS_BOOT_SPLASH_H

#include <ntclks/types.h>

/**
 * @brief Start the graphical boot splash when enabled is true.
 */
void boot_splash_init(bool enabled);

/**
 * @brief Move the splash progress bar to percent (0 through 100).
 */
void boot_splash_update(uint32_t percent);

/**
 * @brief Return true while the splash still owns the framebuffer.
 */
bool boot_splash_active(void);

#endif
