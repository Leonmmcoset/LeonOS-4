/*
 * LeonOS input-manager interface: declares event routing and device handling.
 * Connects raw keyboard and pointer input to sessions and GUI clients.
 */
#ifndef NTCLKS_INPUTM_H
#define NTCLKS_INPUTM_H

#include <ntclks/types.h>

/**
 * @brief Coordinates the inputm handles ioctl operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int inputm_handles_ioctl(uint64_t request);
/**
 * @brief Coordinates the inputm handle ioctl operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param user_arg Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int64_t inputm_handle_ioctl(uint64_t request, uint64_t user_arg);
/**
 * @brief Coordinates the inputm destroy owner operation.
 * @param pid Input or output value used by this operation.
 */
void inputm_destroy_owner(uint32_t pid);

#endif
