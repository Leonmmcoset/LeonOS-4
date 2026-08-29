/*
 * LeonOS input-manager interface: declares event routing and device handling.
 * Connects raw keyboard and pointer input to sessions and GUI clients.
 */
#ifndef NTCLKS_INPUTM_H
#define NTCLKS_INPUTM_H

#include <ntclks/types.h>

/**
 * @brief Return 1 when request is an input-manager ioctl, 0 otherwise.
 */
int inputm_handles_ioctl(uint64_t request);
/**
 * @brief Dispatch an input-manager ioctl, reading its argument from user_arg; returns result/errno.
 */
int64_t inputm_handle_ioctl(uint64_t request, uint64_t user_arg);
/**
 * @brief Release every input provider owned by pid (called when the process exits).
 */
void inputm_destroy_owner(uint32_t pid);

#endif
