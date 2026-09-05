/* Versioned GPU SDK client: all requests go through /dev/gpu. */
#include <leonos/device.h>
#include <leonos/gpu.h>
#include <leonos/gpu_sdk.h>
#include <leonos/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int gpu_sdk_device_fd(void)
{
    static int fd = -1;
    if (fd < 0) {
        fd = open(LEONOS_DEV_GPU, O_RDWR, 0);
    }
    return fd;
}

int gpu_sdk_diagnostics(gpu_sdk_diagnostics_t *diagnostic)
{
    if (!diagnostic) { errno = EINVAL; return -1; }
    diagnostic->size = sizeof(*diagnostic);
    diagnostic->version = LEONOS_GPU_ABI_VERSION;
    long result = syscall3(SYS_ioctl, gpu_sdk_device_fd(),
                           LEONOS_IOCTL_GPU_DIAGNOSTICS, (long)diagnostic);
    if (result < 0) { errno = (int)-result; return -1; }
    return (int)result;
}

int gpu_sdk_info(gpu_sdk_info_t *info)
{
    if (!info) { errno = EINVAL; return -1; }
    info->size = sizeof(*info);
    info->version = LEONOS_GPU_ABI_VERSION;
    long result = syscall3(SYS_ioctl, gpu_sdk_device_fd(), LEONOS_IOCTL_GPU_INFO,
                           (long)info);
    if (result < 0) { errno = (int)-result; return -1; }
    return (int)result;
}

int gpu_sdk_create(gpu_sdk_context_t *context)
{
    if (!context) { errno = EINVAL; return -1; }
    context->size = sizeof(*context);
    context->version = LEONOS_GPU_ABI_VERSION;
    long result = syscall3(SYS_ioctl, gpu_sdk_device_fd(), LEONOS_IOCTL_GPU_CREATE,
                           (long)context);
    if (result < 0) { errno = (int)-result; return -1; }
    return (int)result;
}

int gpu_sdk_render(const gpu_sdk_frame_t *frame)
{
    gpu_sdk_frame_t request;
    if (!frame) { errno = EINVAL; return -1; }
    request = *frame;
    request.size = sizeof(request);
    request.version = LEONOS_GPU_ABI_VERSION;
    long result = syscall3(SYS_ioctl, gpu_sdk_device_fd(), LEONOS_IOCTL_GPU_RENDER,
                           (long)&request);
    if (result < 0) { errno = (int)-result; return -1; }
    return (int)result;
}

int gpu_sdk_destroy(uint64_t handle)
{
    gpu_sdk_destroy_t request = {sizeof(request), LEONOS_GPU_ABI_VERSION, handle};
    long result = syscall3(SYS_ioctl, gpu_sdk_device_fd(), LEONOS_IOCTL_GPU_DESTROY,
                           (long)&request);
    if (result < 0) { errno = (int)-result; return -1; }
    return (int)result;
}
