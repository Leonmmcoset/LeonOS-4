/* Checked Ring-3 GPU requests with immutable inputs and validated copyout. */
#include <ntclks/gpu.h>
#include <ntclks/heap.h>
#include <ntclks/sched.h>
#include <ntclks/svga.h>
#include <ntclks/usercopy.h>
#include <leonos/gpu.h>

/**
 * @brief Recognize the versioned GPU ioctl requests.
 * @param command Full ioctl value, including any high bits.
 * @return True for a supported GPU command.
 */
bool syscall_gpu_owns(uint64_t command)
{
    return command >= LEONOS_IOCTL_GPU_INFO && command <= LEONOS_IOCTL_GPU_DIAGNOSTICS;
}

/**
 * @brief Snapshot a user request and validate its versioned header.
 * @param destination Kernel storage for bytes bytes, including the header.
 * @param source User address; the range is validated before access.
 * @param bytes Exact expected structure size, at least eight bytes.
 * @return Zero, EFAULT for inaccessible memory, or EINVAL for an ABI mismatch.
 */
static int gpu_request_copy(void *destination, uint64_t source, uint32_t bytes)
{
    if (!user_range_ok(source, bytes)) return -14;
    __builtin_memcpy(destination, (const void *)(uintptr_t)source, bytes);
    uint32_t header[2];
    __builtin_memcpy(header, destination, sizeof(header));
    return header[0] == bytes && header[1] == LEONOS_GPU_ABI_VERSION ? 0 : -22;
}

/**
 * @brief Snapshot frame inputs and validate the entire fenced readback destination.
 * @param owner Calling process; the backend checks handle ownership.
 * @param argument User frame address, accessed under the kernel execution lock.
 * @return Zero with the output filled, or negative errno with output unchanged.
 */
static int gpu_render_user(uint32_t owner, uint64_t argument)
{
    struct leonos_gpu_frame frame;
    int ret = gpu_request_copy(&frame, argument, sizeof(frame));
    if (ret) return ret;
    if (!frame.handle || frame.reserved || !frame.vertex_count ||
        frame.vertex_count > LEONOS_GPU_MAX_VERTICES || !frame.draw_count ||
        frame.draw_count > LEONOS_GPU_MAX_DRAWS || !frame.pixel_capacity ||
        frame.pixel_capacity > LEONOS_GPU_MAX_WIDTH * LEONOS_GPU_MAX_HEIGHT ||
        frame.fill_mode < LEONOS_GPU_FILL_POINT || frame.fill_mode > LEONOS_GPU_FILL_SOLID)
        return -22;
    size_t vertex_bytes = (size_t)frame.vertex_count * sizeof(struct leonos_gpu_vertex);
    size_t draw_bytes = (size_t)frame.draw_count * sizeof(struct leonos_gpu_draw);
    size_t pixel_bytes = (size_t)frame.pixel_capacity * sizeof(uint32_t);
    if (!user_range_ok(frame.vertices, vertex_bytes) ||
        !user_range_ok(frame.draws, draw_bytes) ||
        !user_range_writable(frame.pixels, pixel_bytes)) return -14;

    struct leonos_gpu_vertex *vertices = kernel_malloc(vertex_bytes);
    struct leonos_gpu_draw *draws = kernel_malloc(draw_bytes);
    if (!vertices || !draws) {
        ret = -12;
    } else {
        __builtin_memcpy(vertices, (const void *)(uintptr_t)frame.vertices, vertex_bytes);
        __builtin_memcpy(draws, (const void *)(uintptr_t)frame.draws, draw_bytes);
        /* The kernel execution lock keeps the checked writable mappings stable.
         * The backend touches output only after a successful fenced readback;
         * inputs stay immutable even if the user's output aliases them. */
        ret = svga_gpu_render(owner, &frame, vertices, draws,
                              (uint32_t *)(uintptr_t)frame.pixels);
    }
    kernel_free(draws);
    kernel_free(vertices);
    return ret;
}

/**
 * @brief Handle a GPU ioctl for the current live user task.
 * @param command GPU operation; the caller holds the kernel execution lock.
 * @param argument User structure address, checked before reads or writes.
 * @return Zero on success, or negative errno for request or device failures.
 */
int64_t syscall_gpu_dispatch(uint64_t command, uint64_t argument)
{
    if (!syscall_gpu_owns(command)) return -25;
    struct task *task = sched_current_task();
    if (!task || task->kind != TASK_KIND_USER || task->state == TASK_EXITED) return -1;
    if (command == LEONOS_IOCTL_GPU_DIAGNOSTICS) {
        struct leonos_gpu_diagnostics diagnostic;
        int ret = gpu_request_copy(&diagnostic, argument, sizeof(diagnostic));
        if (ret) return ret;
        if (!user_range_writable(argument, sizeof(diagnostic))) return -14;
        svga_gpu_get_diagnostics(task->pid, &diagnostic);
        __builtin_memcpy((void *)(uintptr_t)argument, &diagnostic, sizeof(diagnostic));
        return 0;
    }
    if (command == LEONOS_IOCTL_GPU_INFO) {
        struct leonos_gpu_info info;
        int ret = gpu_request_copy(&info, argument, sizeof(info));
        if (ret) return ret;
        if (!user_range_writable(argument, sizeof(info))) return -14;
        __builtin_memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        info.version = LEONOS_GPU_ABI_VERSION;
        svga_gpu_get_info(&info);
        __builtin_memcpy((void *)(uintptr_t)argument, &info, sizeof(info));
        return 0;
    }
    if (command == LEONOS_IOCTL_GPU_CREATE) {
        struct leonos_gpu_context context;
        int ret = gpu_request_copy(&context, argument, sizeof(context));
        if (ret) return ret;
        if (context.handle || context.reserved || !context.width || !context.height ||
            context.width > LEONOS_GPU_MAX_WIDTH || context.height > LEONOS_GPU_MAX_HEIGHT ||
            !context.vertex_capacity || context.vertex_capacity > LEONOS_GPU_MAX_VERTICES)
            return -22;
        if (!user_range_writable(argument, sizeof(context))) return -14;
        ret = svga_gpu_create(task->pid, &context);
        __builtin_memcpy((void *)(uintptr_t)argument, &context, sizeof(context));
        return ret;
    }
    if (command == LEONOS_IOCTL_GPU_DESTROY) {
        struct leonos_gpu_destroy destroy;
        int ret = gpu_request_copy(&destroy, argument, sizeof(destroy));
        if (ret) return ret;
        return svga_gpu_destroy(task->pid, destroy.handle);
    }
    return gpu_render_user(task->pid, argument);
}
