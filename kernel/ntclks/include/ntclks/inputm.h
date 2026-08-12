/*
 * LeonOS input-manager interface: declares event routing and device handling.
 * Connects raw keyboard and pointer input to sessions and GUI clients.
 */
#ifndef NTCLKS_INPUTM_H
#define NTCLKS_INPUTM_H

#include <ntclks/types.h>

int inputm_handles_ioctl(uint64_t request);
int64_t inputm_handle_ioctl(uint64_t request, uint64_t user_arg);
void inputm_destroy_owner(uint32_t pid);

#endif
