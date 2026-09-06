/* Legacy leonos_gpu_* compatibility entry points. All requests target the
 * /dev/gpu device node; fd 3 is no longer special. */
#include <leonos/device.h>
#include <leonos/gpu.h>
#include <leonos/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int gpu_fd(void)
{
    static int fd = -1;
    if (fd < 0) fd = open(LEONOS_DEV_GPU, LEONOS_O_RDWR, 0);
    return fd;
}

static int gpu_ioctl(unsigned long request, void *arg)
{
    long result = syscall3(SYS_ioctl, gpu_fd(), (long)request, (long)arg);
    if (result < 0) { errno = (int)-result; return -1; }
    return (int)result;
}

int leonos_gpu_diagnostics(struct leonos_gpu_diagnostics *diagnostic)
{
    if (!diagnostic) return -22;
    diagnostic->size = sizeof(*diagnostic);
    diagnostic->version = LEONOS_GPU_ABI_VERSION;
    return gpu_ioctl(LEONOS_IOCTL_GPU_DIAGNOSTICS, diagnostic);
}

int leonos_gpu_info(struct leonos_gpu_info *info)
{
    if (!info) return -22;
    info->size = sizeof(*info);
    info->version = LEONOS_GPU_ABI_VERSION;
    return gpu_ioctl(LEONOS_IOCTL_GPU_INFO, info);
}

int leonos_gpu_create(struct leonos_gpu_context *context)
{
    if (!context) return -22;
    context->size = sizeof(*context);
    context->version = LEONOS_GPU_ABI_VERSION;
    return gpu_ioctl(LEONOS_IOCTL_GPU_CREATE, context);
}

int leonos_gpu_render(const struct leonos_gpu_frame *frame)
{
    struct leonos_gpu_frame request;
    if (!frame) return -22;
    request = *frame;
    request.size = sizeof(request);
    request.version = LEONOS_GPU_ABI_VERSION;
    return gpu_ioctl(LEONOS_IOCTL_GPU_RENDER, &request);
}

int leonos_gpu_destroy(uint64_t handle)
{
    struct leonos_gpu_destroy request = {sizeof(request), LEONOS_GPU_ABI_VERSION, handle};
    return gpu_ioctl(LEONOS_IOCTL_GPU_DESTROY, &request);
}
