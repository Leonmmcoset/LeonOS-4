#include "device.h"

/* The shared device lock covers an entire frame. Fence waits are bounded;
 * mode changes cannot invalidate a resource halfway through a batch. */

static uint64_t gpu_clock_locked(void)
{
    struct svga_gpu_stats *s = &svga.gpu_stats;
    uint64_t now = svga.ops.clock ? svga.ops.clock() : 0;
    if (!s->initialized || s->device_generation != svga.generation || now < s->last_clock) {
        uint32_t generation = s->generation + 1u;
        *s = (struct svga_gpu_stats){.generation = generation ? generation : 1u,
            .device_generation = svga.generation, .initialized = true};
    }
    s->last_clock = now;
    return now;
}

static void gpu_finish_locked(void)
{
    uint64_t now = gpu_clock_locked();
    struct svga_gpu_stats *s = &svga.gpu_stats;
    if (s->busy) {
        uint64_t elapsed = now - s->busy_start;
        s->busy_ticks = elapsed > UINT64_MAX - s->busy_ticks ? UINT64_MAX : s->busy_ticks + elapsed;
        s->busy = false;
    }
}

static void gpu_retire_fences_locked(struct svga_gpu_context *c)
{
    for (uint32_t slot = 0; slot < 2; ++slot) {
        svga_handle fence = c->slots[slot].fence;
        if (fence && (fence >> 32) == svga.generation &&
            (int32_t)(svga.fifo[SVGA_FIFO_FENCE] - (uint32_t)fence) >= 0)
            c->slots[slot].fence = 0;
    }
}

void svga_gpu_reset_locked(void)
{
    gpu_finish_locked();
    svga.gpu_error_owner = 0;
    for (uint32_t i = 0; i < SVGA_GPU_CONTEXTS; ++i)
        svga.gpu_contexts[i] = (struct svga_gpu_context){0};
}

static struct svga_gpu_context *gpu_find_locked(uint32_t owner, uint64_t handle)
{
    for (uint32_t i = 0; i < SVGA_GPU_CONTEXTS; ++i) {
        struct svga_gpu_context *c = &svga.gpu_contexts[i];
        if (handle && c->handle == handle && owner && c->owner == owner)
            return c;
    }
    return NULL;
}

static int gpu_cleanup_locked(struct svga_gpu_context *c)
{
    /* Never use a stale wire ID: shutdown may already have retired some or all
     * resources, and a later context can occupy those same wire slots. */
    int ret = svga_sync_locked();
    if (ret) return ret;
    gpu_finish_locked();
    int id = svga_find_context(c->context);
    if (id >= 0 && (ret = svga_context_destroy_locked((uint32_t)id))) return ret;
    c->context = 0;
    for (uint32_t slot = 0; slot < 2; ++slot) {
        struct svga_gpu_slot *s = &c->slots[slot];
        svga_handle *surfaces[] = {&s->vertex, &s->depth, &s->color};
        for (uint32_t i = 0; i < 3; ++i) {
            id = svga_find_surface(*surfaces[i]);
            if (id >= 0 && (ret = svga_surface_destroy_locked((uint32_t)id))) return ret;
            *surfaces[i] = 0;
        }
        svga_handle *buffers[] = {&s->upload, &s->readback};
        for (uint32_t i = 0; i < 2; ++i) {
            id = svga_find_gmr(*buffers[i]);
            if (id >= 0 && (ret = svga_gmr_destroy_locked((uint32_t)id))) return ret;
            *buffers[i] = 0;
        }
        s->fence = 0;
    }
    *c = (struct svga_gpu_context){0};
    return 0;
}

static int gpu_reap_locked(void)
{
    for (uint32_t i = 0; i < SVGA_GPU_CONTEXTS; ++i) {
        struct svga_gpu_context *c = &svga.gpu_contexts[i];
        if (c->handle && (c->retiring || c->generation != svga.generation)) {
            c->retiring = true;
            int ret = gpu_cleanup_locked(c);
            if (ret) return ret;
        }
    }
    return 0;
}

/**
 * @brief Allocate an owner-private double-buffered offscreen context.
 * @param owner Nonzero process identifier.
 * @param request Trusted in/out request; handle stays zero on failure.
 * @return Zero or negative SVGA error; unfinished retirement remains tracked internally.
 */
int svga_gpu_create(uint32_t owner, struct leonos_gpu_context *request)
{
    if (!request) return SVGA_EINVAL;
    request->handle = 0;
    if (!owner || request->size != sizeof(*request) || request->version != LEONOS_GPU_ABI_VERSION ||
        request->reserved || !request->width || request->width > LEONOS_GPU_MAX_WIDTH ||
        !request->height || request->height > LEONOS_GPU_MAX_HEIGHT ||
        !request->vertex_capacity || request->vertex_capacity > LEONOS_GPU_MAX_VERTICES)
        return SVGA_EINVAL;
    uint64_t flags = svga_lock();
    (void)gpu_clock_locked();
    int ret = svga.available && svga.fifo_ready && svga.memory_ready ? 0 : SVGA_ENODEV;
    if (!ret) ret = gpu_reap_locked();
    struct svga_gpu_context *c = NULL;
    uint32_t owned = 0;
    for (uint32_t i = 0; !ret && i < SVGA_GPU_CONTEXTS; ++i) {
        if (!svga.gpu_contexts[i].handle && !c) c = &svga.gpu_contexts[i];
        if (svga.gpu_contexts[i].handle && svga.gpu_contexts[i].owner == owner) ++owned;
    }
    if (!ret && (!c || owned >= 4)) ret = SVGA_ENOMEM;
    if (!ret) {
        svga_handle handle = svga_new_handle(SVGA_KIND_GPU, (uint32_t)(c - svga.gpu_contexts));
        if (!handle) ret = SVGA_ENOMEM;
        else *c = (struct svga_gpu_context){.handle = handle, .owner = owner,
            .generation = svga.generation, .width = request->width, .height = request->height,
            .vertex_capacity = request->vertex_capacity, .next_slot = 0,
            .pending_slot = SVGA_GPU_NO_SLOT, .last_slot = SVGA_GPU_NO_SLOT};
    }
    if (!ret) {
        uint32_t vertex_bytes = c->vertex_capacity * sizeof(struct leonos_gpu_vertex);
        struct svga_surface_desc color = {SVGA3D_X8R8G8B8, SVGA3D_SURFACE_HINT_RENDERTARGET,
            c->width, c->height, 1, 1};
        struct svga_surface_desc depth = {SVGA3D_Z_D24S8, SVGA3D_SURFACE_HINT_DEPTHSTENCIL,
            c->width, c->height, 1, 1};
        struct svga_surface_desc vertex = {SVGA3D_BUFFER, SVGA3D_SURFACE_HINT_VERTEXBUFFER,
            vertex_bytes, 1, 1, 1};
        ret = svga_context_create_locked(&c->context);
        for (uint32_t slot = 0; !ret && slot < 2; ++slot) {
            struct svga_gpu_slot *s = &c->slots[slot];
            ret = svga_surface_create_locked(&color, &s->color);
            if (!ret) ret = svga_surface_create_locked(&depth, &s->depth);
            if (!ret) ret = svga_surface_create_locked(&vertex, &s->vertex);
            if (!ret) ret = svga_gmr_create_locked(vertex_bytes, &s->upload);
            if (!ret) ret = svga_gmr_create_locked(c->width * c->height * 4u, &s->readback);
        }
        if (!ret) ret = svga_sync_locked();
        if (ret) {
            c->retiring = true;
            c->owner = 0;
            (void)gpu_cleanup_locked(c);
        } else request->handle = c->handle;
    }
    svga_unlock(flags);
    return ret;
}

/**
 * @brief Destroy an owned context, preserving DMA resources if the host is stalled.
 * @param owner Owning process ID.
 * @param handle Opaque context identifier.
 * @return Zero, EINVAL for unknown/foreign handle, or negative retirement error.
 */
int svga_gpu_destroy(uint32_t owner, uint64_t handle)
{
    uint64_t flags = svga_lock();
    struct svga_gpu_context *c = gpu_find_locked(owner, handle);
    int ret = SVGA_EINVAL;
    if (c) {
        c->retiring = true;
        ret = gpu_cleanup_locked(c);
    }
    svga_unlock(flags);
    return ret;
}

/**
 * @brief Drop all process ownership and retry retirement of pending DMA resources.
 * @param owner Exiting process identifier.
 */
void svga_gpu_release_owner(uint32_t owner)
{
    uint64_t flags = svga_lock();
    if (svga.gpu_error_owner == owner) svga.gpu_error_owner = 0;
    for (uint32_t i = 0; i < SVGA_GPU_CONTEXTS; ++i) {
        struct svga_gpu_context *c = &svga.gpu_contexts[i];
        if (owner && c->handle && c->owner == owner) {
            c->retiring = true;
            c->owner = 0;
        }
    }
    (void)gpu_reap_locked();
    svga_unlock(flags);
}

static void gpu_record_error_locked(const struct svga_gpu_context *c, int status, uint32_t stage)
{
    /* Capture before cleanup submits its own fences or retires the context. */
    svga.gpu_error_owner = c->owner;
    svga.gpu_error = (struct leonos_gpu_diagnostics){.size = sizeof(svga.gpu_error),
        .version = LEONOS_GPU_ABI_VERSION, .status = status, .stage = stage,
        .handle = c->handle, .generation = svga.generation,
        .fifo_min = svga.fifo[SVGA_FIFO_MIN], .fifo_max = svga.fifo[SVGA_FIFO_MAX],
        .fifo_next = svga.fifo[SVGA_FIFO_NEXT_CMD], .fifo_stop = svga.fifo[SVGA_FIFO_STOP],
        .fifo_fence = svga.fifo[SVGA_FIFO_FENCE], .issued_fence = svga.issued_fence,
        .fifo_busy = svga.min > SVGA_FIFO_BUSY * 4u ? svga.fifo[SVGA_FIFO_BUSY] : UINT32_MAX,
        .submitted_frames = svga.gpu_stats.submitted, .completed_frames = svga.gpu_stats.completed};
}

void svga_gpu_get_diagnostics(uint32_t owner, struct leonos_gpu_diagnostics *out)
{
    if (!out) return;
    uint64_t flags = svga_lock();
    *out = owner && owner == svga.gpu_error_owner ? svga.gpu_error :
        (struct leonos_gpu_diagnostics){.size = sizeof(*out), .version = LEONOS_GPU_ABI_VERSION};
    svga_unlock(flags);
}

static uint32_t float_word(const float *value)
{
    uint32_t bits;
    __builtin_memcpy(&bits, value, sizeof(bits));
    return bits;
}

static int gpu_validate(const struct svga_gpu_context *c, const struct leonos_gpu_frame *frame,
                        const struct leonos_gpu_vertex *vertices, const struct leonos_gpu_draw *draws,
                        const uint32_t *pixels)
{
    if (!vertices || !draws || !pixels || frame->size != sizeof(*frame) ||
        frame->version != LEONOS_GPU_ABI_VERSION || frame->reserved ||
        !frame->vertex_count || frame->vertex_count > c->vertex_capacity ||
        !frame->draw_count || frame->draw_count > LEONOS_GPU_MAX_DRAWS ||
        frame->pixel_capacity != c->width * c->height ||
        frame->fill_mode < LEONOS_GPU_FILL_POINT || frame->fill_mode > LEONOS_GPU_FILL_SOLID)
        return SVGA_EINVAL;
    for (uint32_t i = 0; i < frame->vertex_count; ++i) {
        if ((float_word(&vertices[i].x) & 0x7f800000u) == 0x7f800000u ||
            (float_word(&vertices[i].y) & 0x7f800000u) == 0x7f800000u ||
            (float_word(&vertices[i].z) & 0x7f800000u) == 0x7f800000u)
            return SVGA_EINVAL;
    }
    for (uint32_t i = 0; i < frame->draw_count; ++i) {
        if (!draws[i].count || draws[i].count % 3u || draws[i].first > frame->vertex_count ||
            draws[i].count > frame->vertex_count - draws[i].first) return SVGA_EINVAL;
        for (uint32_t j = 0; j < 16; ++j)
            if ((float_word(&draws[i].transform[j]) & 0x7f800000u) == 0x7f800000u)
                return SVGA_EINVAL;
    }
    return 0;
}

static int gpu_transform_locked(uint32_t cid, uint32_t type, const float *matrix)
{
    /* The SVGA fixed-function matrix has D3D row-vector memory layout. The
     * public column-major column-vector transform has the same 16 wire words. */
    uint32_t command[18] = {cid, type};
    for (uint32_t i = 0; i < 16; ++i)
        command[i + 2] = matrix ? float_word(&matrix[i]) : (i % 5u == 0 ? 0x3f800000u : 0);
    return svga_command_locked(SVGA_3D_CMD_SETTRANSFORM, command, sizeof(command));
}

static int gpu_fill_state_locked(uint32_t cid, uint32_t fill)
{
    SVGA3dRenderState fill_state = {.state = SVGA3D_RS_FILLMODE, .uintValue = fill};
    struct svga_span spans[] = {{&cid, sizeof(cid)}, {&fill_state, sizeof(fill_state)}};
    return svga_fifo_packet_locked(SVGA_3D_CMD_SETRENDERSTATE, true, spans, 2);
}

static int gpu_set_targets_locked(const struct svga_gpu_context *c, uint32_t slot)
{
    uint32_t cid = (uint32_t)svga_find_context(c->context);
    uint32_t sid = (uint32_t)svga_find_surface(c->slots[slot].color);
    uint32_t depth = (uint32_t)svga_find_surface(c->slots[slot].depth);
    SVGA3dCmdSetRenderTarget color_target = {cid, SVGA3D_RT_COLOR0, {sid, 0, 0}};
    SVGA3dCmdSetRenderTarget depth_target = {cid, SVGA3D_RT_DEPTH, {depth, 0, 0}};
    int ret = svga_command_locked(SVGA_3D_CMD_SETRENDERTARGET, &color_target, sizeof(color_target));
    if (!ret) {
        svga.contexts[cid].targets[SVGA3D_RT_COLOR0] = sid;
        ret = svga_command_locked(SVGA_3D_CMD_SETRENDERTARGET, &depth_target, sizeof(depth_target));
    }
    if (!ret) svga.contexts[cid].targets[SVGA3D_RT_DEPTH] = depth;
    return ret;
}

static int gpu_state_init_locked(struct svga_gpu_context *c, uint32_t slot, uint32_t fill)
{
    uint32_t cid = (uint32_t)svga_find_context(c->context);
    int ret = gpu_set_targets_locked(c, slot);
    SVGA3dRenderState states[] = {
        {.state = SVGA3D_RS_ZENABLE, .uintValue = 1},
        {.state = SVGA3D_RS_ZWRITEENABLE, .uintValue = 1},
        {.state = SVGA3D_RS_ZFUNC, .uintValue = SVGA3D_CMP_LESS},
        {.state = SVGA3D_RS_LIGHTINGENABLE, .uintValue = 0},
        {.state = SVGA3D_RS_CULLMODE, .uintValue = SVGA3D_FACE_NONE},
        {.state = SVGA3D_RS_COLORWRITEENABLE, .uintValue = 0xf},
        {.state = SVGA3D_RS_FILLMODE, .uintValue = fill},
        {.state = SVGA3D_RS_SHADEMODE, .uintValue = SVGA3D_SHADEMODE_SMOOTH},
        {.state = SVGA3D_RS_BLENDENABLE, .uintValue = 0},
        {.state = SVGA3D_RS_ALPHATESTENABLE, .uintValue = 0},
        {.state = SVGA3D_RS_FOGENABLE, .uintValue = 0},
        {.state = SVGA3D_RS_STENCILENABLE, .uintValue = 0},
        {.state = SVGA3D_RS_SCISSORTESTENABLE, .uintValue = 0},
        {.state = SVGA3D_RS_CLIPPING, .uintValue = 1},
        {.state = SVGA3D_RS_POINTSIZE, .uintValue = 0x3f800000u},
    };
    struct svga_span spans[] = {{&cid, sizeof(cid)}, {states, sizeof(states)}};
    if (!ret) ret = svga_fifo_packet_locked(SVGA_3D_CMD_SETRENDERSTATE, true, spans, 2);
    SVGA3dTextureState textures[] = {
        {.stage = 0, .name = SVGA3D_TS_COLOROP, .value = SVGA3D_TC_SELECTARG1},
        {.stage = 0, .name = SVGA3D_TS_COLORARG1, .value = SVGA3D_TA_DIFFUSE},
        {.stage = 0, .name = SVGA3D_TS_ALPHAOP, .value = SVGA3D_TC_SELECTARG1},
        {.stage = 0, .name = SVGA3D_TS_ALPHAARG1, .value = SVGA3D_TA_DIFFUSE},
        {.stage = 1, .name = SVGA3D_TS_COLOROP, .value = SVGA3D_TC_DISABLE},
        {.stage = 1, .name = SVGA3D_TS_ALPHAOP, .value = SVGA3D_TC_DISABLE},
    };
    spans[1] = (struct svga_span){textures, sizeof(textures)};
    if (!ret) ret = svga_fifo_packet_locked(SVGA_3D_CMD_SETTEXTURESTATE, true, spans, 2);
    SVGA3dCmdSetViewport viewport = {cid, {0, 0, c->width, c->height}};
    if (!ret) ret = svga_command_locked(SVGA_3D_CMD_SETVIEWPORT, &viewport, sizeof(viewport));
    uint32_t zrange[] = {cid, 0, 0x3f800000u};
    if (!ret) ret = svga_command_locked(SVGA_3D_CMD_SETZRANGE, zrange, sizeof(zrange));
    if (!ret) ret = gpu_transform_locked(cid, SVGA3D_TRANSFORM_WORLD, NULL);
    if (!ret) ret = gpu_transform_locked(cid, SVGA3D_TRANSFORM_VIEW, NULL);
    if (!ret) {
        c->fill_mode = fill;
        c->state_ready = true;
    }
    return ret;
}

static int gpu_state_locked(struct svga_gpu_context *c, uint32_t slot,
                            const struct leonos_gpu_frame *frame)
{
    uint32_t cid = (uint32_t)svga_find_context(c->context);
    int ret = c->state_ready ? gpu_set_targets_locked(c, slot) :
        gpu_state_init_locked(c, slot, frame->fill_mode);
    if (!ret && c->fill_mode != frame->fill_mode)
        ret = gpu_fill_state_locked(cid, frame->fill_mode);
    if (!ret) c->fill_mode = frame->fill_mode;
    uint32_t clear[] = {cid, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
        frame->clear_color, 0x3f800000u, 0, 0, 0, c->width, c->height};
    if (!ret) ret = svga_command_locked(SVGA_3D_CMD_CLEAR, clear, sizeof(clear));
    return ret;
}

static int gpu_draw_locked(struct svga_gpu_context *c, uint32_t slot,
                           const struct leonos_gpu_draw *draw)
{
    uint32_t cid = (uint32_t)svga_find_context(c->context);
    uint32_t vertex = (uint32_t)svga_find_surface(c->slots[slot].vertex);
    int ret = gpu_transform_locked(cid, SVGA3D_TRANSFORM_PROJECTION, draw->transform);
    struct {
        SVGA3dCmdDrawPrimitives command;
        SVGA3dVertexDecl declarations[2];
        SVGA3dPrimitiveRange range;
    } packet = {
        .command = {cid, 2, 1},
        .declarations = {
            {{SVGA3D_DECLTYPE_FLOAT3, SVGA3D_DECLMETHOD_DEFAULT, SVGA3D_DECLUSAGE_POSITION, 0},
             {vertex, 0, sizeof(struct leonos_gpu_vertex)}, {draw->first, draw->first + draw->count - 1}},
            {{SVGA3D_DECLTYPE_D3DCOLOR, SVGA3D_DECLMETHOD_DEFAULT, SVGA3D_DECLUSAGE_COLOR, 0},
             {vertex, 12, sizeof(struct leonos_gpu_vertex)}, {draw->first, draw->first + draw->count - 1}},
        },
        .range = {SVGA3D_PRIMITIVE_TRIANGLELIST, draw->count / 3u,
            {SVGA3D_INVALID_ID, 0, 0}, 0, (int32_t)draw->first},
    };
    if (!ret) ret = svga_command_locked(SVGA_3D_CMD_DRAW_PRIMITIVES, &packet, sizeof(packet));
    return ret;
}

static bool gpu_slot_usable_locked(const struct svga_gpu_context *c, uint32_t slot)
{
    int context_id = svga_find_context(c->context);
    int color = svga_find_surface(c->slots[slot].color);
    int depth = svga_find_surface(c->slots[slot].depth);
    int vertex = svga_find_surface(c->slots[slot].vertex);
    int upload = svga_find_gmr(c->slots[slot].upload);
    int readback = svga_find_gmr(c->slots[slot].readback);
    return context_id >= 0 && svga_context_usable((uint32_t)context_id) &&
           color >= 0 && svga_surface_usable((uint32_t)color) &&
           depth >= 0 && svga_surface_usable((uint32_t)depth) &&
           vertex >= 0 && svga_surface_usable((uint32_t)vertex) &&
           upload >= 0 && !svga.gmrs[upload].retiring &&
           readback >= 0 && !svga.gmrs[readback].retiring;
}

static int gpu_submit_locked(struct svga_gpu_context *c, uint32_t slot,
                             const struct leonos_gpu_frame *frame,
                             const struct leonos_gpu_vertex *vertices,
                             const struct leonos_gpu_draw *draws, uint32_t *stage)
{
    struct svga_gpu_slot *s = &c->slots[slot];
    int upload = svga_find_gmr(s->upload);
    if (!gpu_slot_usable_locked(c, slot)) return SVGA_EIO;
    void *destination = svga.ops.pointer(svga.gmrs[upload].phys);
    if (!destination) return SVGA_EIO;
    __builtin_memcpy(destination, vertices, frame->vertex_count * sizeof(*vertices));
    svga_barrier();

    /* Queue one frame as a batch: wake the host after the fence instead of
     * issuing a SYNC port write for every state/DMA/draw packet. */
    svga_fifo_defer_notify_locked();
    uint32_t bytes = frame->vertex_count * sizeof(*vertices);
    struct svga_dma_box upload_box = {0, 0, 0, bytes, 1, 1, 0, 0, 0};
    int ret = svga_surface_dma_locked(s->vertex, 0, 0, s->upload, 0, bytes,
        SVGA3D_WRITE_HOST_VRAM, &upload_box, 1);
    if (!ret) {
        *stage = LEONOS_GPU_ERROR_STATE;
        ret = gpu_state_locked(c, slot, frame);
    }
    for (uint32_t i = 0; !ret && i < frame->draw_count; ++i) {
        *stage = LEONOS_GPU_ERROR_DRAW;
        ret = gpu_draw_locked(c, slot, &draws[i]);
        if (!ret) svga.gpu_stats.triangles += draws[i].count / 3u;
    }
    struct svga_dma_box read_box = {0, 0, 0, c->width, c->height, 1, 0, 0, 0};
    if (!ret) {
        *stage = LEONOS_GPU_ERROR_READBACK;
        ret = svga_surface_dma_locked(s->color, 0, 0, s->readback, 0, c->width * 4u,
            SVGA3D_READ_HOST_VRAM, &read_box, 1);
    }
    if (!ret) {
        *stage = LEONOS_GPU_ERROR_FENCE;
        svga_fifo_flush_notify_locked();
        ret = svga_fence_locked(&s->fence);
        if (ret) s->fence = 0;
    }
    if (ret && svga.fifo_notify_deferred)
        svga_fifo_flush_notify_locked();
    return ret;
}

static int gpu_copyout_locked(const struct svga_gpu_context *c, uint32_t slot, uint32_t *pixels)
{
    int gmr = svga_find_gmr(c->slots[slot].readback);
    if (gmr < 0 || svga.gmrs[gmr].retiring) return SVGA_EIO;
    const uint32_t *source = svga.ops.pointer(svga.gmrs[gmr].phys);
    if (!source) return SVGA_EIO;
    for (uint32_t i = 0; i < c->width * c->height; ++i) pixels[i] = source[i] & 0x00ffffffu;
    return 0;
}

/**
 * @brief Render into a double-buffered slot and return the previous frame.
 *
 * The ioctl remains synchronous from the caller's point of view, but the GPU
 * has a full frame interval to finish the previous submission before its fence
 * is needed. Only the first two calls wait for the frame just submitted; later
 * calls wait for frame N-1 while frame N is already queued.
 *
 * @return Zero with top-down 00RRGGBB output; negative errors leave output unchanged.
 */
int svga_gpu_render(uint32_t owner, const struct leonos_gpu_frame *frame,
                    const struct leonos_gpu_vertex *vertices,
                    const struct leonos_gpu_draw *draws, uint32_t *pixels)
{
    if (!frame) return SVGA_EINVAL;
    uint64_t flags = svga_lock();
    struct svga_gpu_context *c = gpu_find_locked(owner, frame->handle);
    int ret = !c || c->retiring || c->generation != svga.generation ? SVGA_EINVAL : 0;
    if (!ret) ret = gpu_validate(c, frame, vertices, draws, pixels);
    if (!ret && (!svga.available || !svga.fifo_ready)) ret = SVGA_ENODEV;
    if (ret) { svga_unlock(flags); return ret; }
    struct svga_gpu_stats *stats = &svga.gpu_stats;
    ++stats->submitted;
    uint32_t stage = LEONOS_GPU_ERROR_REAP;
    ret = gpu_reap_locked();
    if (!ret) gpu_retire_fences_locked(c);
    if (!ret) {
        uint64_t now = gpu_clock_locked();
        if (!stats->busy) {
            stats->busy_start = now;
            stats->busy = true;
        }
        stage = LEONOS_GPU_ERROR_PREPARE;
        uint32_t slot = c->next_slot;
        if (c->slots[slot].fence) {
            ret = svga_wait_locked(c->slots[slot].fence);
            if (!ret) c->slots[slot].fence = 0;
        }
        if (!ret) {
            stage = LEONOS_GPU_ERROR_UPLOAD;
            ret = gpu_submit_locked(c, slot, frame, vertices, draws, &stage);
        }
        uint32_t output = slot;
        if (!ret) {
            if (c->has_last && c->pending_slot == SVGA_GPU_NO_SLOT) {
                /* Startup: frame one is still in flight; return the last
                 * completed readback one more time to start the pipeline. */
                output = c->last_slot;
            } else if (c->pending_slot != SVGA_GPU_NO_SLOT) {
                output = c->pending_slot;
            }
            if (c->slots[output].fence) {
                stage = LEONOS_GPU_ERROR_FENCE;
                ret = svga_wait_locked(c->slots[output].fence);
                if (!ret) c->slots[output].fence = 0;
            }
        }
        if (!ret) {
            stage = LEONOS_GPU_ERROR_COPYOUT;
            ret = gpu_copyout_locked(c, output, pixels);
        }
        if (!ret) {
            ++stats->completed;
            if (!c->has_last) {
                c->last_slot = output;
                c->has_last = true;
            } else if (c->pending_slot == SVGA_GPU_NO_SLOT) {
                c->pending_slot = slot;
                c->last_slot = output;
            } else {
                c->pending_slot = slot;
                c->last_slot = output;
            }
            c->next_slot ^= 1u;
        }
    }
    if (ret) {
        gpu_record_error_locked(c, ret, stage);
        ++stats->failed;
        c->retiring = true;
        gpu_finish_locked();
        (void)gpu_cleanup_locked(c);
    } else {
        gpu_retire_fences_locked(c);
        /* Estimate only the interval this render call spent in the driver
         * (including any synchronous Fence wait). Idle time between frames
         * is not counted as GPU-busy. */
        gpu_finish_locked();
    }
    svga_unlock(flags);
    return ret;
}

/**
 * @brief Snapshot timing estimates in a single monotonic clock domain and live resources.
 * @param out Writable kernel output, ignored when NULL.
 */
void svga_gpu_get_info(struct leonos_gpu_info *out)
{
    if (!out) return;
    uint64_t flags = svga_lock();
    /* A caller may already have fallen back to software after a timeout.
     * Once the host drains its FIFO, a stats poll can retire abandoned DMA. */
    if (svga.fifo_ready && svga.fifo &&
        svga.fifo[SVGA_FIFO_NEXT_CMD] == svga.fifo[SVGA_FIFO_STOP] &&
        !svga.ops.read(SVGA_REG_BUSY)) (void)gpu_reap_locked();
    uint64_t now = gpu_clock_locked();
    struct svga_gpu_stats *s = &svga.gpu_stats;
    uint64_t pending = s->busy ? now - s->busy_start : 0;
    *out = (struct leonos_gpu_info){.size = sizeof(*out), .version = LEONOS_GPU_ABI_VERSION,
        .flags = svga.available ? LEONOS_GPU_AVAILABLE | LEONOS_GPU_BUSY_ESTIMATED : 0,
        .generation = s->generation, .sample_ticks = now,
        .busy_ticks = pending > UINT64_MAX - s->busy_ticks ? UINT64_MAX : s->busy_ticks + pending,
        .submitted_frames = s->submitted, .completed_frames = s->completed,
        .failed_frames = s->failed, .triangles = s->triangles,
        .surface_bytes = svga.surface_bytes, .guest_bytes = (uint64_t)svga.pages * 4096u};
    for (uint32_t i = 0; i < SVGA_GPU_CONTEXTS; ++i)
        out->contexts += !!svga.gpu_contexts[i].handle;
    svga_unlock(flags);
}
