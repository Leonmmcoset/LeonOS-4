#ifndef LEONOS_GPU_H
#define LEONOS_GPU_H

#include <stdint.h>

#define LEONOS_GPU_ABI_VERSION 1U
#define LEONOS_IOCTL_GPU_INFO 0x4c475001UL
#define LEONOS_IOCTL_GPU_CREATE 0x4c475002UL
#define LEONOS_IOCTL_GPU_RENDER 0x4c475003UL
#define LEONOS_IOCTL_GPU_DESTROY 0x4c475004UL
#define LEONOS_IOCTL_GPU_DIAGNOSTICS 0x4c475005UL
#define LEONOS_GPU_MAX_VERTICES 32768U
#define LEONOS_GPU_MAX_DRAWS 16U
#define LEONOS_GPU_MAX_WIDTH 2048U
#define LEONOS_GPU_MAX_HEIGHT 1536U
#define LEONOS_GPU_AVAILABLE 1U
#define LEONOS_GPU_BUSY_ESTIMATED 2U
#define LEONOS_GPU_FILL_POINT 1U
#define LEONOS_GPU_FILL_LINE 2U
#define LEONOS_GPU_FILL_SOLID 3U

/* Counters use one device clock domain. Compute utilization from differences
 * in busy_ticks / sample_ticks; they are not CPU scheduler ticks. The estimate
 * includes host scheduling and DMA through fence completion, not physical GPU
 * engine utilization. Structures start with size/version, set by the caller. */
struct leonos_gpu_info {
    uint32_t size, version, flags, generation;
    uint64_t sample_ticks, busy_ticks;
    uint64_t submitted_frames, completed_frames, failed_frames, triangles;
    uint64_t surface_bytes, guest_bytes;
    uint32_t contexts, reserved;
};

/* Contexts are private to the creating process; handle is output on CREATE.
 * vertex_capacity limits each frame's upload. Resize by replacing the context. */
struct leonos_gpu_context {
    uint32_t size, version, width, height;
    uint32_t vertex_capacity, reserved;
    uint64_t handle;
};

/* Object-space FLOAT3 and packed AARRGGBB diffuse color. */
struct leonos_gpu_vertex {
    float x, y, z;
    uint32_t color;
};

/* TRIANGLELIST; count is a multiple of three. Transform is the column-major
 * object-to-clip matrix with D3D depth range [0,w]. Color is interpolated by GPU.
 * Vertices are prepared/lit by the caller, transformed and rasterized by SVGA. */
struct leonos_gpu_draw {
    uint32_t first, count;
    float transform[16];
};

/* All pointer fields are user virtual addresses encoded as uint64_t. Output is
 * top-down, tightly packed 00RRGGBB, suitable for leonos_gui_present_window().
 * Render returns a fenced readback. The kernel keeps two offscreen slots, so
 * from the third frame onward it waits for frame N-1 while frame N is already
 * queued on the GPU; the first two calls return the first rendered frame. */
struct leonos_gpu_frame {
    uint32_t size, version;
    uint64_t handle, vertices, draws, pixels;
    uint32_t vertex_count, draw_count, pixel_capacity, fill_mode;
    uint32_t clear_color, reserved;
};

struct leonos_gpu_destroy {
    uint32_t size, version;
    uint64_t handle;
};

#define LEONOS_GPU_ERROR_NONE 0U
#define LEONOS_GPU_ERROR_REAP 1U
#define LEONOS_GPU_ERROR_PREPARE 2U
#define LEONOS_GPU_ERROR_UPLOAD 3U
#define LEONOS_GPU_ERROR_STATE 4U
#define LEONOS_GPU_ERROR_DRAW 5U
#define LEONOS_GPU_ERROR_READBACK 6U
#define LEONOS_GPU_ERROR_FENCE 7U
#define LEONOS_GPU_ERROR_COPYOUT 8U

/* Most recent device-side render failure, captured before cleanup. Returned
 * only to its owner; status=0 means no snapshot for the caller. A later failure
 * replaces it. fifo_busy=UINT32_MAX means unavailable.
 * FIFO fields are byte offsets or sequence values, never physical addresses. */
struct leonos_gpu_diagnostics {
    uint32_t size, version;
    int32_t status;
    uint32_t stage;
    uint64_t handle;
    uint32_t generation, fifo_min, fifo_max, fifo_next, fifo_stop;
    uint32_t fifo_fence, issued_fence, fifo_busy;
    uint64_t submitted_frames, completed_frames;
};

int leonos_gpu_diagnostics(struct leonos_gpu_diagnostics *diagnostics);
int leonos_gpu_info(struct leonos_gpu_info *info);
int leonos_gpu_create(struct leonos_gpu_context *context);
int leonos_gpu_render(const struct leonos_gpu_frame *frame);
int leonos_gpu_destroy(uint64_t handle);

#endif
