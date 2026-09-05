/* Trusted Ring-0 legacy SVGA3D API. No pointers in this API are user pointers. */
#ifndef NTCLKS_SVGA_H
#define NTCLKS_SVGA_H
#include <ntclks/types.h>
#include <leonos/gpu.h>

typedef uint64_t svga_handle;
#define SVGA_INVALID_HANDLE 0ULL
#define SVGA_OK 0
#define SVGA_EIO (-5)
#define SVGA_ENOMEM (-12)
#define SVGA_EBUSY (-16)
#define SVGA_ENODEV (-19)
#define SVGA_EINVAL (-22)
#define SVGA_ENOTSUP (-95)
#define SVGA_ETIMEDOUT (-110)

/* Trusted diagnostic snapshot of the last extended-FIFO probe, before rollback.
 * stage points to a static kernel string and must never enter the user ABI. */
struct svga_probe_info {
    const char *stage;
    int status;
    uint32_t fifo_caps, fifo_min, mem_regs, enable;
    uint32_t host_legacy, host_revised, devcap_3d;
    bool gb_objects;
};

struct svga_info {
    uint32_t device_caps, fifo_caps, host_version, guest_version;
    uint32_t generation, contexts, surfaces, gmrs, guest_pages;
    uint64_t surface_bytes;
    bool fifo_ready, available;
    struct svga_probe_info probe;
};

/* All faces have the same mip count; cube maps have six square faces.
 * Dimensions are in texels, except BUFFER width which is in bytes. */
struct svga_surface_desc {
    uint32_t format, flags, width, height, depth, mip_levels;
};

struct svga_dma_box {
    uint32_t x, y, z, w, h, d, srcx, srcy, srcz;
};
struct svga_present_rect { uint32_t x, y, w, h, srcx, srcy; };

/** @brief Snapshot device capabilities and live resource counters into non-NULL out. */
void svga_get_info(struct svga_info *out);
/** @brief Read a host capability; return ENOTSUP when that index was not advertised. */
int svga3d_capability(uint32_t index, uint32_t *out);
/** @brief Initialize extended FIFO after mm and kernel paging; 2D survives absent 3D. */
int svga3d_init(void);
/** @brief Drain and destroy all resources; on failure preserve them for a retry. */
int svga3d_shutdown(void);
/**
 * @brief Allocate a context and, for GB devices, its pinned state backing.
 * @param out Non-NULL handle storage. A nonzero handle on failure owns a partial
 * context that must be destroyed after the host recovers; it cannot render.
 * @return Zero or negative SVGA error; ownership persists until destroy succeeds.
 */
int svga3d_context_create(svga_handle *out);
/** @brief Destroy a context and its shaders/queries after outstanding work completes. */
int svga3d_context_destroy(svga_handle context);
/**
 * @brief Define all surface faces/mips and GB backing within host memory limits.
 * @param desc Trusted non-NULL dimensions, format and usage flags.
 * @param out Non-NULL handle storage. A nonzero handle on failure must be passed
 * to svga3d_surface_destroy after the host recovers; it cannot render.
 * @return Zero or negative SVGA error; unfinished retirement retains ownership.
 */
int svga3d_surface_create(const struct svga_surface_desc *desc, svga_handle *out);
/** @brief Unbind and destroy a surface; a failure retains ownership for a retry. */
int svga3d_surface_destroy(svga_handle surface);
/** @brief Allocate a DMA buffer, pin its pages and define a GMR; out receives ownership.
 * On a timeout while retiring a partially mapped GMR, out may contain a handle
 * that must be passed to svga_gmr_destroy() after the host responds.
 */
int svga_gmr_create(uint32_t bytes, svga_handle *out);
/** @brief Wait for all DMA, undefine the GMR, then free its pages. */
int svga_gmr_destroy(svga_handle buffer);
/** @brief Synchronize then copy bytes to or from a managed GMR; validates offset and size. */
int svga_gmr_transfer(svga_handle buffer, uint32_t offset, void *data,
                      uint32_t bytes, bool write);
/** @brief Copy image boxes between GMR and host surface; direction is SVGA3dTransferType. */
int svga3d_surface_dma(svga_handle surface, uint32_t face, uint32_t mip,
                       svga_handle buffer, uint32_t offset, uint32_t pitch,
                       uint32_t direction, const struct svga_dma_box *boxes,
                       uint32_t count);
/** @brief Bind a surface as a context render target. */
int svga3d_set_render_target(svga_handle context, uint32_t target_type,
                             svga_handle surface);
/** @brief Clear the currently bound color/depth/stencil targets. */
int svga3d_clear(svga_handle context, uint32_t flags, uint32_t color,
                 float depth, uint32_t stencil);
/** @brief Draw three fixed-pipeline vertices from a managed vertex surface. */
int svga3d_draw_triangle(svga_handle context, svga_handle vertex_surface,
                         svga_handle vertex_buffer, uint32_t target_width,
                         uint32_t target_height);
/** @brief Present rectangles from a managed surface to the legacy framebuffer. */
int svga3d_present(svga_handle surface, const struct svga_present_rect *rects,
                   uint32_t count);
/** @brief Return a managed object's wire ID, or UINT32_MAX for a stale/invalid handle. */
uint32_t svga_resource_id(svga_handle resource);
/** @brief Submit a trusted legacy state/draw/shader/query packet (without its 8-byte header).
 * @param generation Epoch from svga_get_info; rejects packets prepared before a reset.
 * @param command VMware legacy command ID; resource creation/destruction and DMA use managed APIs.
 * @param payload Readable, immutable kernel storage for the call; contains protocol wire IDs.
 * @param bytes Exact payload size, divisible by four. Callers must obey shader/state semantics.
 * @return Zero when queued; negative SVGA error on invalid layout, unsupported commands or timeout.
 * GB devices accept the shared fixed-pipeline commands; shader/query commands
 * requiring GB object translation return ENOTSUP. Pending GB retirement returns EBUSY.
 */
int svga3d_submit(uint32_t generation, uint32_t command, const void *payload, uint32_t bytes);
/** @brief Queue a fence; the returned token includes the device generation. */
int svga_fence_insert(svga_handle *out);
/** @brief Wait with a finite polling budget independent of the interrupt clock. */
int svga_fence_wait(svga_handle fence);
/** @brief Render, Present and read back the fixed-pipeline test, then release all resources. */
int svga3d_triangle_test(void);

/**
 * @brief Allocate bounded offscreen resources private to the owner process.
 * @param owner Nonzero process ID that receives ownership.
 * @param request Trusted kernel request; receives a handle only on success.
 * @return Zero, or a negative SVGA error; failed DMA retirement remains tracked.
 */
int svga_gpu_create(uint32_t owner, struct leonos_gpu_context *request);
/**
 * @brief Retire an owned context after its DMA completes, retaining failures for retry.
 * @param owner Process ID whose ownership is checked.
 * @param handle Opaque handle returned by create.
 * @return Zero or a negative SVGA error, including EINVAL for foreign handles.
 */
int svga_gpu_destroy(uint32_t owner, uint64_t handle);
/**
 * @brief Queue a frame on an owned double-buffered offscreen context and return a fenced readback.
 * @param owner Process ID whose ownership is checked.
 * @param frame Trusted kernel metadata; its encoded user addresses are ignored.
 * @param vertices Trusted array with frame->vertex_count elements.
 * @param draws Trusted array with frame->draw_count elements.
 * @param pixels Validated writable output of width*height uint32_t elements;
 * the caller must keep its mappings stable throughout the call.
 * @return Zero with top-down 00RRGGBB output; negative errors leave output unchanged.
 */
int svga_gpu_render(uint32_t owner, const struct leonos_gpu_frame *frame,
                    const struct leonos_gpu_vertex *vertices,
                    const struct leonos_gpu_draw *draws, uint32_t *pixels);
/**
 * @brief Retire all process contexts; stalled DMA stays tracked for later cleanup.
 * @param owner Process ID whose resources are released.
 */
void svga_gpu_release_owner(uint32_t owner);
/**
 * @brief Snapshot cumulative virtual GPU timing and managed resource totals.
 * @param out Non-NULL writable kernel structure; receives size and version too.
 */
void svga_gpu_get_info(struct leonos_gpu_info *out);

/** @brief Copy the latest render failure to its owner without device I/O. */
void svga_gpu_get_diagnostics(uint32_t owner, struct leonos_gpu_diagnostics *out);

#endif
