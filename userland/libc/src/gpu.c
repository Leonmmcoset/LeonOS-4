#include <leonos/gpu.h>
#include <leonos/syscall.h>

int leonos_gpu_diagnostics(struct leonos_gpu_diagnostics *diagnostic)
{
    if (!diagnostic) return -22;
    diagnostic->size = sizeof(*diagnostic);
    diagnostic->version = LEONOS_GPU_ABI_VERSION;
    return ioctl(3, LEONOS_IOCTL_GPU_DIAGNOSTICS, diagnostic);
}

int leonos_gpu_info(struct leonos_gpu_info *info)
{
    if (!info) return -22;
    info->size = sizeof(*info);
    info->version = LEONOS_GPU_ABI_VERSION;
    return ioctl(3, LEONOS_IOCTL_GPU_INFO, info);
}

int leonos_gpu_create(struct leonos_gpu_context *context)
{
    if (!context) return -22;
    context->size = sizeof(*context);
    context->version = LEONOS_GPU_ABI_VERSION;
    return ioctl(3, LEONOS_IOCTL_GPU_CREATE, context);
}

int leonos_gpu_render(const struct leonos_gpu_frame *frame)
{
    if (!frame) return -22;
    struct leonos_gpu_frame request = *frame;
    request.size = sizeof(request);
    request.version = LEONOS_GPU_ABI_VERSION;
    return ioctl(3, LEONOS_IOCTL_GPU_RENDER, &request);
}

int leonos_gpu_destroy(uint64_t handle)
{
    struct leonos_gpu_destroy request = {sizeof(request), LEONOS_GPU_ABI_VERSION, handle};
    return ioctl(3, LEONOS_IOCTL_GPU_DESTROY, &request);
}
