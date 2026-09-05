#ifndef NTCLKS_GPU_H
#define NTCLKS_GPU_H
#include <ntclks/types.h>

/**
 * @brief Recognize the four versioned GPU ioctl requests.
 * @param command Full ioctl value.
 * @return True for a supported GPU command.
 */
bool syscall_gpu_owns(uint64_t command);
/**
 * @brief Validate and handle a GPU request under the kernel execution lock.
 * @param command GPU ioctl operation.
 * @param argument User structure address; never trusted by the driver.
 * @return Zero on success or negative errno on validation or device failure.
 */
int64_t syscall_gpu_dispatch(uint64_t command, uint64_t argument);

#endif
