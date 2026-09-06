#ifndef LEONOS_SVGA_DEVICE_H
#define LEONOS_SVGA_DEVICE_H
#include <ntclks/svga.h>
#include "svga_protocol.h"

#define SVGA_CONTEXTS 64u
#define SVGA_SURFACES 256u
#define SVGA_GMRS 128u
#define SVGA_MAX_GUEST_PAGES 16384u
#define SVGA_MAX_SURFACE_BYTES (128ULL * 1024 * 1024)
#define SVGA_MAX_MIPS 16u
#define SVGA_POLL_LIMIT 4000u
#define SVGA_KIND_CONTEXT 1u
#define SVGA_KIND_SURFACE 2u
#define SVGA_KIND_GMR 3u
#define SVGA_KIND_GPU 4u
#define SVGA_GPU_CONTEXTS 16u
#define SVGA_MOBS (SVGA_CONTEXTS + SVGA_SURFACES)

struct svga_ops {
    uint32_t (*read)(uint32_t reg);
    void (*write)(uint32_t reg, uint32_t value);
    uint64_t (*alloc)(uint32_t pages);
    void (*free)(uint64_t phys, uint32_t pages);
    void *(*pointer)(uint64_t phys);
    uint64_t (*clock)(void);
};
struct svga_mob {
    uint64_t phys, pt_phys;
    uint32_t pages, pt_pages, bytes, depth;
    bool defined;
};
struct svga_gb_resource {
    struct svga_mob mob;
    bool defined, bound, retiring;
};
struct svga_context {
    svga_handle handle;
    uint32_t targets[SVGA3D_RT_MAX], textures[32];
    struct svga_gb_resource gb;
};
struct svga_surface {
    svga_handle handle;
    struct svga_surface_desc desc;
    uint32_t block_w, block_h, block_bytes, faces;
    uint64_t bytes;
    struct svga_gb_resource gb;
};
struct svga_gmr {
    svga_handle handle;
    uint64_t phys;
    uint32_t bytes, pages;
    bool defined, retiring;
};
#define SVGA_GPU_NO_SLOT UINT32_MAX
struct svga_gpu_slot {
    svga_handle color, depth, vertex, upload, readback;
    svga_handle fence;
};
struct svga_gpu_context {
    svga_handle handle, context;
    struct svga_gpu_slot slots[2];
    uint32_t owner, generation, width, height, vertex_capacity;
    uint32_t fill_mode, next_slot, pending_slot, last_slot;
    bool retiring, state_ready, has_last;
};
struct svga_gpu_stats {
    uint64_t last_clock, busy_ticks, busy_start;
    uint64_t submitted, completed, failed, triangles;
    uint32_t generation, device_generation;
    bool initialized, busy;
};
struct svga_device {
    struct svga_ops ops;
    volatile uint32_t *fifo;
    uint32_t fifo_bytes, min, caps, fifo_caps, host_version;
    uint32_t generation, handle_serial, next_fence, issued_fence;
    bool fifo_notify_deferred;
    uint32_t cap_values[SVGA3D_DEVCAP_MAX];
    bool cap_valid[SVGA3D_DEVCAP_MAX];
    uint32_t context_limit, surface_limit, gmr_limit, page_limit, pages;
    uint64_t surface_bytes, surface_limit_bytes;
    bool bound, fifo_ready, available, memory_ready, transitioning;
    bool gb_active;
    uint32_t mob_max_bytes;
    struct svga_mob otables[SVGA_OTABLE_DX9_MAX];
    struct svga_probe_info probe;
    struct svga_context contexts[SVGA_CONTEXTS];
    struct svga_surface surfaces[SVGA_SURFACES];
    struct svga_gmr gmrs[SVGA_GMRS];
    struct svga_gpu_context gpu_contexts[SVGA_GPU_CONTEXTS];
    struct svga_gpu_stats gpu_stats;
    struct leonos_gpu_diagnostics gpu_error;
    uint32_t gpu_error_owner;
};
struct svga_span { const void *data; uint32_t bytes; };
extern struct svga_device svga;

/* Callers hold the shared device lock for every _locked helper. */
uint64_t svga_lock(void);
void svga_unlock(uint64_t flags);
void svga_barrier(void);
void svga_relax(void);
void svga_bind(const struct svga_ops *ops, volatile uint32_t *fifo, uint32_t bytes);
int svga_configure_locked(bool extended);
int svga_fifo_packet_locked(uint32_t id, bool three_d, const struct svga_span *spans, uint32_t count);
void svga_fifo_defer_notify_locked(void);
void svga_fifo_flush_notify_locked(void);
int svga_drain_locked(void);
int svga_fence_locked(svga_handle *out);
int svga_wait_locked(svga_handle fence);
int svga_sync_locked(void);
int svga_shutdown_locked(void);
int svga_negotiate_locked(void);
svga_handle svga_new_handle(uint32_t kind, uint32_t slot);
int svga_find_context(svga_handle handle);
int svga_find_surface(svga_handle handle);
int svga_find_gmr(svga_handle handle);
bool svga_context_usable(uint32_t id);
bool svga_surface_usable(uint32_t id);
int svga_context_destroy_locked(uint32_t id);
int svga_context_create_locked(svga_handle *out);
int svga_surface_create_locked(const struct svga_surface_desc *desc, svga_handle *out);
int svga_surface_destroy_locked(uint32_t id);
int svga_gmr_create_locked(uint32_t bytes, svga_handle *out);
int svga_gmr_destroy_locked(uint32_t id);
int svga_surface_dma_locked(svga_handle surface, uint32_t face, uint32_t mip,
    svga_handle buffer, uint32_t offset, uint32_t pitch, uint32_t direction,
    const struct svga_dma_box *boxes, uint32_t count);
void svga_gpu_reset_locked(void);
int svga_format(uint32_t format, uint32_t *bw, uint32_t *bh, uint32_t *bytes, uint32_t *cap);
int svga_command_locked(uint32_t id, const void *data, uint32_t bytes);
int svga_gb_init_locked(void);
int svga_gb_shutdown_locked(void);
int svga_gb_context_create_locked(uint32_t id);
int svga_gb_surface_create_locked(uint32_t id);
int svga_gb_destroy_locked(struct svga_gb_resource *resource, uint32_t id, bool context);

/* Bootstrap framebuffer adapter. All register I/O uses this same lock. */
void svga_platform_bind(uint16_t port, volatile uint32_t *fifo, uint32_t bytes);
int svga_update(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
int svga_mode_begin(uint64_t *flags);
void svga_mode_end(uint64_t flags);
uint32_t svga_read_register(uint32_t reg);
void svga_write_register(uint32_t reg, uint32_t value);

#endif
