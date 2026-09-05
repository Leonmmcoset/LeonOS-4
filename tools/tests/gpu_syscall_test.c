#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <leonos/gpu.h>
#include <ntclks/gpu.h>
#include <ntclks/sched.h>
#include <ntclks/svga.h>

static struct task current;
static uint64_t rejected;
static uint64_t readonly;
static int live_allocations, fail_allocation, backend_calls, backend_result;
static size_t allocation_limit = SIZE_MAX;
static const struct leonos_gpu_vertex *user_vertices;
static const struct leonos_gpu_draw *user_draws;

struct task *sched_current_task(void) { return &current; }
bool user_range_ok(uint64_t pointer, uint64_t bytes)
{
    return pointer && pointer != rejected && bytes && pointer < UINT64_MAX - bytes;
}
bool user_range_writable(uint64_t pointer, uint64_t bytes)
{
    return pointer != readonly && user_range_ok(pointer, bytes);
}
void *kernel_malloc(size_t bytes)
{
    if (bytes > allocation_limit) return NULL;
    if (fail_allocation && --fail_allocation == 0) return NULL;
    void *p = malloc(bytes);
    if (p) ++live_allocations;
    return p;
}
void kernel_free(void *p) { if (p) { --live_allocations; free(p); } }
void svga_gpu_get_info(struct leonos_gpu_info *info)
{
    ++backend_calls;
    info->flags = LEONOS_GPU_AVAILABLE | LEONOS_GPU_BUSY_ESTIMATED;
}
void svga_gpu_get_diagnostics(uint32_t owner, struct leonos_gpu_diagnostics *out)
{
    assert(owner == 42);
    *out = (struct leonos_gpu_diagnostics){.size = sizeof(*out),
        .version = 1, .status = -110, .stage = LEONOS_GPU_ERROR_FENCE,
        .handle = 123, .fifo_next = 2048, .fifo_stop = 1800,
        .fifo_fence = 15, .issued_fence = 16};
}
int svga_gpu_create(uint32_t owner, struct leonos_gpu_context *context)
{
    assert(owner == 42);
    ++backend_calls;
    context->handle = 123;
    return 0;
}
int svga_gpu_destroy(uint32_t owner, uint64_t handle)
{
    assert(owner == 42 && handle == 123);
    ++backend_calls;
    return 0;
}
int svga_gpu_render(uint32_t owner, const struct leonos_gpu_frame *frame,
                     const struct leonos_gpu_vertex *vertices,
                     const struct leonos_gpu_draw *draws, uint32_t *pixels)
{
    assert(owner == 42 && frame->handle == 123);
    assert(vertices != user_vertices && draws != user_draws);
    assert(memcmp(vertices, user_vertices, frame->vertex_count * sizeof(*vertices)) == 0);
    assert(memcmp(draws, user_draws, frame->draw_count * sizeof(*draws)) == 0);
    ++backend_calls;
    if (backend_result) return backend_result;
    struct leonos_gpu_vertex first = vertices[0];
    for (uint32_t i = 0; i < frame->pixel_capacity; ++i) pixels[i] = 0x123456;
    assert(memcmp(&first, vertices, sizeof(first)) == 0);
    return 0;
}

int main(void)
{
    current.kind = TASK_KIND_USER;
    current.pid = 42;
    struct leonos_gpu_context ctx = {sizeof(ctx), LEONOS_GPU_ABI_VERSION, 2, 2, 3, 0, 0};
    assert(syscall_gpu_owns(LEONOS_IOCTL_GPU_CREATE));
    assert(!syscall_gpu_owns(0));
    assert(syscall_gpu_dispatch(0, 0) == -25);
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_CREATE, 0) == -14);
    ctx.version = 99;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_CREATE, (uintptr_t)&ctx) == -22);
    ctx.version = LEONOS_GPU_ABI_VERSION;
    readonly = (uintptr_t)&ctx;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_CREATE, (uintptr_t)&ctx) == -14);
    readonly = 0;
    assert(backend_calls == 0);
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_CREATE, (uintptr_t)&ctx) == 0);
    assert(ctx.handle == 123 && backend_calls == 1);

    struct leonos_gpu_vertex vertices[3] = {{0, 0, 0, 0xff0000}};
    struct leonos_gpu_draw draws[1] = {{.first = 0, .count = 3}};
    uint32_t pixels[4] = {0};
    struct leonos_gpu_frame frame = {
        .size = sizeof(frame), .version = LEONOS_GPU_ABI_VERSION, .handle = 123,
        .vertices = (uintptr_t)vertices, .draws = (uintptr_t)draws,
        .pixels = (uintptr_t)pixels, .vertex_count = 3, .draw_count = 1,
        .pixel_capacity = 4, .fill_mode = LEONOS_GPU_FILL_SOLID,
    };
    user_vertices = vertices;
    user_draws = draws;
    rejected = frame.vertices;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_RENDER, (uintptr_t)&frame) == -14);
    rejected = 0;
    readonly = frame.pixels;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_RENDER, (uintptr_t)&frame) == -14);
    readonly = 0;
    frame.vertex_count = UINT32_MAX;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_RENDER, (uintptr_t)&frame) == -22);
    frame.vertex_count = 3;
    frame.pixel_capacity = UINT32_MAX;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_RENDER, (uintptr_t)&frame) == -22);
    frame.pixel_capacity = 4;
    for (int i = 1; i <= 2; ++i) {
        fail_allocation = i;
        assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_RENDER, (uintptr_t)&frame) == -12);
        assert(live_allocations == 0);
    }
    assert(backend_calls == 1);
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_RENDER, (uintptr_t)&frame) == 0);
    assert(backend_calls == 2 && pixels[3] == 0x123456 && !live_allocations);
    /* A full readback must not require a second frame-sized allocation. */
    static uint32_t large_pixels[640 * 480];
    frame.pixels = (uintptr_t)large_pixels;
    frame.pixel_capacity = 640 * 480;
    allocation_limit = 4096;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_RENDER, (uintptr_t)&frame) == 0);
    assert(large_pixels[0] == 0x123456 && large_pixels[640 * 480 - 1] == 0x123456);
    assert(!live_allocations);
    allocation_limit = SIZE_MAX;
    large_pixels[0] = 0xabcdef;
    large_pixels[640 * 480 - 1] = 0x654321;
    backend_result = -5;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_RENDER, (uintptr_t)&frame) == -5);
    assert(large_pixels[0] == 0xabcdef && large_pixels[640 * 480 - 1] == 0x654321);
    assert(!live_allocations);
    backend_result = 0;
    frame.pixels = frame.vertices;
    frame.pixel_capacity = 4;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_RENDER, (uintptr_t)&frame) == 0);
    assert(!live_allocations);
    struct leonos_gpu_info info = {.size = sizeof(info), .version = LEONOS_GPU_ABI_VERSION};
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_INFO, (uintptr_t)&info) == 0);
    assert(info.flags & LEONOS_GPU_AVAILABLE);
    struct leonos_gpu_diagnostics diagnostic = {.size = sizeof(diagnostic), .version = 1};
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_DIAGNOSTICS, (uintptr_t)&diagnostic) == 0);
    assert(diagnostic.status == -110 && diagnostic.stage == LEONOS_GPU_ERROR_FENCE);
    assert(diagnostic.handle == 123 && diagnostic.fifo_next == 2048);
    assert(diagnostic.fifo_fence == 15 && diagnostic.issued_fence == 16);
    readonly = (uintptr_t)&diagnostic;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_DIAGNOSTICS, readonly) == -14);
    readonly = 0;
    diagnostic.version = 2;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_DIAGNOSTICS, (uintptr_t)&diagnostic) == -22);
    struct leonos_gpu_destroy destroy = {sizeof(destroy), LEONOS_GPU_ABI_VERSION, 123};
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_DESTROY, (uintptr_t)&destroy) == 0);
    current.kind = TASK_KIND_KERNEL;
    assert(syscall_gpu_dispatch(LEONOS_IOCTL_GPU_CREATE, (uintptr_t)&ctx) == -1);
    puts("GPU syscall tests passed: validation, immutable copies, ownership, allocation cleanup");
    return 0;
}
