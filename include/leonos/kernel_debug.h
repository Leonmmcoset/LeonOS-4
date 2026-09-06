/*
 * LeonOS kernel-debug control ABI.
 * Lets trusted desktop applications arm the next Ring-0 diagnostic boot.
 */
#ifndef LEONOS_KERNEL_DEBUG_H
#define LEONOS_KERNEL_DEBUG_H

#include <stdint.h>

#define LEONOS_KERNEL_DEBUG_VERSION 1U

#define LEONOS_KERNEL_DEBUG_CONTROL_GET_STATE 1U
#define LEONOS_KERNEL_DEBUG_CONTROL_SET_ENABLED 2U
#define LEONOS_KERNEL_DEBUG_CONTROL_ARM_NEXT_BOOT 3U
#define LEONOS_KERNEL_DEBUG_CONTROL_CLEAR 4U

#define LEONOS_KERNEL_DEBUG_STATE_ENABLED 0x00000001U
#define LEONOS_KERNEL_DEBUG_STATE_NEXT_BOOT 0x00000002U

struct leonos_kernel_debug_control {
    uint32_t version;
    uint32_t command;
    uint32_t flags;
    uint32_t result_flags;
};

int leonos_kernel_debug_get_state(uint32_t *flags);
int leonos_kernel_debug_set_enabled(int enabled);
int leonos_kernel_debug_arm_next_boot(void);
int leonos_kernel_debug_clear(void);

#endif
