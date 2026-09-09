#ifndef LEONOS_GPU_SDK_H
#define LEONOS_GPU_SDK_H

/* Versioned GPU SDK client. Applications link these entry points; the
 * pre-migration leonos_gpu_* fd-3 ioctl wrappers remain only in the libc
 * transition layer. The service node is /dev/gpu. */
#include <leonos/gpu.h>
#include <stdint.h>

#define GPU_SDK_ABI_VERSION 1U

typedef struct leonos_gpu_info gpu_sdk_info_t;
typedef struct leonos_gpu_context gpu_sdk_context_t;
typedef struct leonos_gpu_vertex gpu_sdk_vertex_t;
typedef struct leonos_gpu_draw gpu_sdk_draw_t;
typedef struct leonos_gpu_frame gpu_sdk_frame_t;
typedef struct leonos_gpu_destroy gpu_sdk_destroy_t;
typedef struct leonos_gpu_diagnostics gpu_sdk_diagnostics_t;

#define GPU_SDK_MAX_VERTICES LEONOS_GPU_MAX_VERTICES
#define GPU_SDK_MAX_DRAWS LEONOS_GPU_MAX_DRAWS
#define GPU_SDK_MAX_WIDTH LEONOS_GPU_MAX_WIDTH
#define GPU_SDK_MAX_HEIGHT LEONOS_GPU_MAX_HEIGHT
#define GPU_SDK_AVAILABLE LEONOS_GPU_AVAILABLE
#define GPU_SDK_BUSY_ESTIMATED LEONOS_GPU_BUSY_ESTIMATED
#define GPU_SDK_FILL_POINT LEONOS_GPU_FILL_POINT
#define GPU_SDK_FILL_LINE LEONOS_GPU_FILL_LINE
#define GPU_SDK_FILL_SOLID LEONOS_GPU_FILL_SOLID
#define GPU_SDK_ERROR_NONE LEONOS_GPU_ERROR_NONE
#define GPU_SDK_ERROR_REAP LEONOS_GPU_ERROR_REAP
#define GPU_SDK_ERROR_PREPARE LEONOS_GPU_ERROR_PREPARE
#define GPU_SDK_ERROR_UPLOAD LEONOS_GPU_ERROR_UPLOAD
#define GPU_SDK_ERROR_STATE LEONOS_GPU_ERROR_STATE
#define GPU_SDK_ERROR_DRAW LEONOS_GPU_ERROR_DRAW
#define GPU_SDK_ERROR_READBACK LEONOS_GPU_ERROR_READBACK
#define GPU_SDK_ERROR_FENCE LEONOS_GPU_ERROR_FENCE
#define GPU_SDK_ERROR_COPYOUT LEONOS_GPU_ERROR_COPYOUT

int gpu_sdk_diagnostics(gpu_sdk_diagnostics_t *diagnostics);
int gpu_sdk_info(gpu_sdk_info_t *info);
int gpu_sdk_create(gpu_sdk_context_t *context);
int gpu_sdk_render(const gpu_sdk_frame_t *frame);
int gpu_sdk_destroy(uint64_t handle);

#endif
