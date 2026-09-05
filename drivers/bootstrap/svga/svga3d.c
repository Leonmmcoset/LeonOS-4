#include "device.h"

static int require_ready(void)
{
    return svga.available && svga.fifo_ready ? 0 : SVGA_ENODEV;
}

static int valid_sid(uint32_t id)
{
    return svga_surface_usable(id);
}

static int valid_cid(uint32_t id)
{
    return svga_context_usable(id);
}

/* GB shader/query objects need separate translation. Only expose the v9
 * commands whose fixed-pipeline encoding is shared by the two backends. */
static bool gb_raw_supported(uint32_t command)
{
    switch (command) {
    case SVGA_3D_CMD_SURFACE_COPY:
    case SVGA_3D_CMD_SURFACE_STRETCHBLT:
    case SVGA_3D_CMD_SETTRANSFORM:
    case SVGA_3D_CMD_SETZRANGE:
    case SVGA_3D_CMD_SETRENDERSTATE:
    case SVGA_3D_CMD_SETRENDERTARGET:
    case SVGA_3D_CMD_SETTEXTURESTATE:
    case SVGA_3D_CMD_SETMATERIAL:
    case SVGA_3D_CMD_SETLIGHTDATA:
    case SVGA_3D_CMD_SETLIGHTENABLED:
    case SVGA_3D_CMD_SETVIEWPORT:
    case SVGA_3D_CMD_SETCLIPPLANE:
    case SVGA_3D_CMD_CLEAR:
    case SVGA_3D_CMD_PRESENT:
    case SVGA_3D_CMD_DRAW_PRIMITIVES:
    case SVGA_3D_CMD_SETSCISSORRECT:
        return true;
    default: return false;
    }
}

static int surface_referenced(uint32_t sid)
{
    for (uint32_t cid = 0; cid < SVGA_CONTEXTS; ++cid) {
        if (!svga.contexts[cid].handle) continue;
        for (uint32_t target = 0; target < SVGA3D_RT_MAX; ++target)
            if (svga.contexts[cid].targets[target] == sid) return 1;
        for (uint32_t stage = 0; stage < 32; ++stage)
            if (svga.contexts[cid].textures[stage] == sid) return 1;
    }
    return 0;
}

static int managed_command(uint32_t command)
{
    switch (command) {
    case SVGA_3D_CMD_SURFACE_DEFINE:
    case SVGA_3D_CMD_SURFACE_DEFINE_V2:
    case SVGA_3D_CMD_SURFACE_DESTROY:
    case SVGA_3D_CMD_SURFACE_DMA:
    case SVGA_3D_CMD_CONTEXT_DEFINE:
    case SVGA_3D_CMD_CONTEXT_DESTROY:
        return 1;
    default:
        return 0;
    }
}

int svga_format(uint32_t format, uint32_t *bw, uint32_t *bh,
                uint32_t *bytes, uint32_t *cap)
{
    if (!bw || !bh || !bytes || !cap) return SVGA_EINVAL;
    *bw = *bh = 1; *bytes = 4; *cap = 0;
    switch (format) {
    case SVGA3D_X8R8G8B8:
        *cap = SVGA3D_DEVCAP_SURFACEFMT_X8R8G8B8;
        return 0;
    case SVGA3D_A8R8G8B8:
        *cap = SVGA3D_DEVCAP_SURFACEFMT_A8R8G8B8;
        return 0;
    case SVGA3D_Z_D16:
        *bytes = 2; *cap = SVGA3D_DEVCAP_SURFACEFMT_Z_D16; return 0;
    case SVGA3D_Z_D24S8:
        *cap = SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8; return 0;
    case SVGA3D_BUFFER:
        *bytes = 1;
        *cap = SVGA3D_DEVCAP_3D;
        return 0;
    default:
        return SVGA_ENOTSUP;
    }
}

static uint64_t surface_size(const struct svga_surface_desc *d,
                             uint32_t bw, uint32_t bh, uint32_t bpp)
{
    uint64_t width = (d->width + bw - 1u) / bw;
    uint64_t height = (d->height + bh - 1u) / bh;
    uint64_t depth = d->depth ? d->depth : 1u;
    uint64_t levels = d->mip_levels ? d->mip_levels : 1u;
    uint64_t total = 0;
    for (uint64_t level = 0; level < levels; ++level) {
        uint64_t area;
        uint64_t volume;
        if (width && height > UINT64_MAX / width) return 0;
        area = width * height;
        if (area && depth > UINT64_MAX / area) return 0;
        volume = area * depth;
        if (volume && bpp > UINT64_MAX / volume) return 0;
        if (total > UINT64_MAX - volume * bpp) return 0;
        total += volume * bpp;
        if (width > 1) width >>= 1;
        if (height > 1) height >>= 1;
        if (depth > 1) depth >>= 1;
    }
    uint64_t faces = (d->flags & SVGA3D_SURFACE_CUBEMAP) ? 6u : 1u;
    if (total > UINT64_MAX / faces) return 0;
    return total * faces;
}

int svga_context_create_locked(svga_handle *out)
{
    if (!out) return SVGA_EINVAL;
    *out = 0;
    int ret = require_ready();
    if (ret) goto done;
    uint32_t id;
    for (id = 1; id < svga.context_limit && svga.contexts[id].handle; ++id) {}
    if (id >= svga.context_limit) { ret = SVGA_ENOMEM; goto done; }
    svga_handle handle = svga_new_handle(SVGA_KIND_CONTEXT, id);
    if (!handle) { ret = SVGA_ENOMEM; goto done; }
    if (svga.gb_active) {
        svga.contexts[id].handle = handle;
        for (uint32_t i = 0; i < SVGA3D_RT_MAX; ++i) svga.contexts[id].targets[i] = SVGA3D_INVALID_ID;
        ret = svga_gb_context_create_locked(id);
        if (ret) svga.contexts[id].gb.retiring = true;
        if (!ret || svga_context_destroy_locked(id)) *out = handle;
        goto done;
    }
    SVGA3dCmdDefineContext cmd = {id};
    ret = svga_command_locked(SVGA_3D_CMD_CONTEXT_DEFINE, &cmd, sizeof(cmd));
    if (!ret) {
        svga.contexts[id].handle = handle;
        for (uint32_t i = 0; i < SVGA3D_RT_MAX; ++i) svga.contexts[id].targets[i] = SVGA3D_INVALID_ID;
        *out = handle;
    }
done:
    return ret;
}

int svga3d_context_create(svga_handle *out)
{
    uint64_t flags = svga_lock();
    int ret = svga_context_create_locked(out);
    svga_unlock(flags);
    return ret;
}

int svga_context_destroy_locked(uint32_t id)
{
    if (id >= SVGA_CONTEXTS || !svga.contexts[id].handle) return SVGA_EINVAL;
    if (svga.gb_active) svga.contexts[id].gb.retiring = true;
    int ret = svga_sync_locked();
    if (ret) return ret;
    if (svga.gb_active) {
        ret = svga_gb_destroy_locked(&svga.contexts[id].gb, id, true);
        if (!ret) svga.contexts[id] = (struct svga_context){0};
        return ret;
    }
    SVGA3dCmdDestroyContext cmd = {id};
    ret = svga_command_locked(SVGA_3D_CMD_CONTEXT_DESTROY, &cmd, sizeof(cmd));
    if (!ret) svga.contexts[id] = (struct svga_context){0};
    return ret;
}

int svga3d_context_destroy(svga_handle context)
{
    uint64_t flags = svga_lock();
    int id = svga_find_context(context);
    int ret = id < 0 ? SVGA_EINVAL : svga_context_destroy_locked((uint32_t)id);
    svga_unlock(flags);
    return ret;
}

int svga_surface_create_locked(const struct svga_surface_desc *desc,
                               svga_handle *out)
{
    if (!desc || !out || !desc->width || !desc->height) return SVGA_EINVAL;
    *out = 0;
    int ret = require_ready();
    uint32_t bw, bh, bpp, cap;
    if (ret || (ret = svga_format(desc->format, &bw, &bh, &bpp, &cap))) goto done;
    if (desc->mip_levels > SVGA_MAX_MIPS || desc->depth > 256u) {
        ret = SVGA_EINVAL;
        goto done;
    }
    if (desc->format != SVGA3D_BUFFER) {
        uint32_t max_width = svga.cap_valid[SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH] ?
                             svga.cap_values[SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH] : UINT32_MAX;
        uint32_t max_height = svga.cap_valid[SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT] ?
                              svga.cap_values[SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT] : UINT32_MAX;
        uint32_t max_depth = svga.cap_valid[SVGA3D_DEVCAP_MAX_VOLUME_EXTENT] ?
                             svga.cap_values[SVGA3D_DEVCAP_MAX_VOLUME_EXTENT] : UINT32_MAX;
        if (desc->width > max_width || desc->height > max_height ||
            desc->depth > max_depth) {
            ret = SVGA_EINVAL;
            goto done;
        }
    }
    if (!svga.cap_valid[cap] || !svga.cap_values[cap]) { ret = SVGA_ENOTSUP; goto done; }
    uint64_t bytes = surface_size(desc, bw, bh, bpp);
    if (!bytes || bytes > svga.surface_limit_bytes || bytes > SVGA_MAX_SURFACE_BYTES ||
        bytes > UINT32_MAX) { ret = SVGA_EINVAL; goto done; }
    if (svga.surface_bytes > svga.surface_limit_bytes - bytes) { ret = SVGA_ENOMEM; goto done; }
    uint32_t id;
    for (id = 1; id < svga.surface_limit && svga.surfaces[id].handle; ++id) {}
    if (id >= svga.surface_limit) { ret = SVGA_ENOMEM; goto done; }
    svga_handle handle = svga_new_handle(SVGA_KIND_SURFACE, id);
    if (!handle) { ret = SVGA_ENOMEM; goto done; }
    if (svga.gb_active) {
        svga.surfaces[id] = (struct svga_surface){.handle = handle, .desc = *desc,
            .block_w = bw, .block_h = bh, .block_bytes = bpp,
            .faces = (desc->flags & SVGA3D_SURFACE_CUBEMAP) ? 6u : 1u, .bytes = bytes};
        svga.surface_bytes += bytes;
        ret = svga_gb_surface_create_locked(id);
        if (ret) svga.surfaces[id].gb.retiring = true;
        if (!ret || svga_surface_destroy_locked(id)) *out = handle;
        goto done;
    }
    uint8_t command[sizeof(SVGA3dCmdDefineSurface) +
                    SVGA3D_MAX_SURFACE_FACES * SVGA_MAX_MIPS * sizeof(SVGA3dSize)] = {0};
    SVGA3dCmdDefineSurface *cmd = (SVGA3dCmdDefineSurface *)command;
    uint32_t levels = desc->mip_levels ? desc->mip_levels : 1u;
    uint32_t faces = (desc->flags & SVGA3D_SURFACE_CUBEMAP) ? 6u : 1u;
    cmd->sid = id;
    cmd->surfaceFlags = (SVGA3dSurfaceFlags)desc->flags;
    cmd->format = (SVGA3dSurfaceFormat)desc->format;
    for (uint32_t f = 0; f < faces; ++f) cmd->face[f].numMipLevels = levels;
    SVGA3dSize *sizes = (SVGA3dSize *)(command + sizeof(SVGA3dCmdDefineSurface));
    for (uint32_t f = 0; f < faces; ++f) {
        uint32_t width = desc->width, height = desc->height;
        uint32_t depth = desc->depth ? desc->depth : 1u;
        for (uint32_t level = 0; level < levels; ++level) {
            sizes[f * levels + level] = (SVGA3dSize){width, height, depth};
            if (width > 1) width >>= 1;
            if (height > 1) height >>= 1;
            if (depth > 1) depth >>= 1;
        }
    }
    ret = svga_command_locked(SVGA_3D_CMD_SURFACE_DEFINE, command,
                               sizeof(SVGA3dCmdDefineSurface) +
                               faces * levels * sizeof(*sizes));
    if (!ret) {
        svga.surfaces[id] = (struct svga_surface){.handle = handle, .desc = *desc,
            .block_w = bw, .block_h = bh, .block_bytes = bpp,
            .faces = (desc->flags & SVGA3D_SURFACE_CUBEMAP) ? 6u : 1u, .bytes = bytes};
        svga.surface_bytes += bytes;
        *out = handle;
    }
done:
    return ret;
}

int svga3d_surface_create(const struct svga_surface_desc *desc, svga_handle *out)
{
    uint64_t flags = svga_lock();
    int ret = svga_surface_create_locked(desc, out);
    svga_unlock(flags);
    return ret;
}

int svga_surface_destroy_locked(uint32_t id)
{
    if (id >= SVGA_SURFACES || !svga.surfaces[id].handle) return SVGA_EINVAL;
    if (surface_referenced(id)) return SVGA_EBUSY;
    if (svga.gb_active) svga.surfaces[id].gb.retiring = true;
    int ret = svga_sync_locked();
    if (ret) return ret;
    if (svga.gb_active) ret = svga_gb_destroy_locked(&svga.surfaces[id].gb, id, false);
    else {
        SVGA3dCmdDestroySurface cmd = {id};
        ret = svga_command_locked(SVGA_3D_CMD_SURFACE_DESTROY, &cmd, sizeof(cmd));
    }
    if (!ret) {
        if (svga.surface_bytes >= svga.surfaces[id].bytes) svga.surface_bytes -= svga.surfaces[id].bytes;
        svga.surfaces[id] = (struct svga_surface){0};
    }
    return ret;
}
int svga3d_surface_destroy(svga_handle surface)
{
    uint64_t flags = svga_lock();
    int id = svga_find_surface(surface);
    int ret = id < 0 ? SVGA_EINVAL : svga_surface_destroy_locked((uint32_t)id);
    svga_unlock(flags);
    return ret;
}

static int gb_submit_binding(uint32_t command, const void *payload, uint32_t bytes)
{
    uint32_t cid;
    if (bytes < sizeof(cid)) return SVGA_EINVAL;
    __builtin_memcpy(&cid, payload, sizeof(cid));
    if (!valid_cid(cid)) return SVGA_EINVAL;
    if (command == SVGA_3D_CMD_SETRENDERTARGET) {
        SVGA3dCmdSetRenderTarget cmd;
        if (bytes != sizeof(cmd)) return SVGA_EINVAL;
        __builtin_memcpy(&cmd, payload, sizeof(cmd));
        if ((uint32_t)cmd.type >= SVGA3D_RT_MAX) return SVGA_EINVAL;
        if (cmd.target.sid != SVGA3D_INVALID_ID) {
            if (!valid_sid(cmd.target.sid)) return SVGA_EINVAL;
            const struct svga_surface *s = &svga.surfaces[cmd.target.sid];
            if (cmd.target.face >= s->faces ||
                cmd.target.mipmap >= (s->desc.mip_levels ? s->desc.mip_levels : 1u))
                return SVGA_EINVAL;
        }
        int ret = svga_command_locked(command, payload, bytes);
        if (!ret) svga.contexts[cid].targets[cmd.type] = cmd.target.sid;
        return ret;
    }
    if ((bytes - sizeof(cid)) % sizeof(SVGA3dTextureState)) return SVGA_EINVAL;
    uint32_t count = (bytes - sizeof(cid)) / sizeof(SVGA3dTextureState);
    const uint8_t *states = (const uint8_t *)payload + sizeof(cid);
    for (uint32_t i = 0; i < count; ++i) {
        SVGA3dTextureState state;
        __builtin_memcpy(&state, states + i * sizeof(state), sizeof(state));
        if (state.stage >= 32 || (state.name == SVGA3D_TS_BIND_TEXTURE &&
            state.value != SVGA3D_INVALID_ID && !valid_sid(state.value))) return SVGA_EINVAL;
    }
    int ret = svga_command_locked(command, payload, bytes);
    if (!ret) {
        for (uint32_t i = 0; i < count; ++i) {
            SVGA3dTextureState state;
            __builtin_memcpy(&state, states + i * sizeof(state), sizeof(state));
            if (state.name == SVGA3D_TS_BIND_TEXTURE)
                svga.contexts[cid].textures[state.stage] = state.value;
        }
    }
    return ret;
}

int svga3d_submit(uint32_t generation, uint32_t command,
                  const void *payload, uint32_t bytes)
{
    if (!payload || (bytes & 3u) || command < SVGA_3D_CMD_BASE ||
        command >= SVGA_3D_CMD_MAX || managed_command(command))
        return SVGA_EINVAL;
    uint64_t flags = svga_lock();
    int ret = generation != svga.generation ? SVGA_EIO : require_ready();
    if (!ret && svga.gb_active) {
        if (!gb_raw_supported(command)) ret = SVGA_ENOTSUP;
        /* Raw ring-0 packets may contain several resource IDs. Require all
         * pending retirements to finish before accepting another packet. */
        for (uint32_t i = 0; !ret && i < SVGA_CONTEXTS; ++i)
            if (svga.contexts[i].gb.retiring) ret = SVGA_EBUSY;
        for (uint32_t i = 0; !ret && i < SVGA_SURFACES; ++i)
            if (svga.surfaces[i].gb.retiring) ret = SVGA_EBUSY;
    }
    if (!ret) {
        if (svga.gb_active && (command == SVGA_3D_CMD_SETRENDERTARGET ||
                              command == SVGA_3D_CMD_SETTEXTURESTATE))
            ret = gb_submit_binding(command, payload, bytes);
        else ret = svga_command_locked(command, payload, bytes);
    }
    svga_unlock(flags);
    return ret;
}

int svga3d_set_render_target(svga_handle context, uint32_t target_type,
                             svga_handle surface)
{
    uint64_t flags = svga_lock();
    int cid = svga_find_context(context), sid = svga_find_surface(surface);
    int ret = (!valid_cid((uint32_t)cid) || !valid_sid((uint32_t)sid) ||
               target_type >= SVGA3D_RT_MAX) ? SVGA_EINVAL : require_ready();
    if (!ret) {
        SVGA3dCmdSetRenderTarget cmd = {(uint32_t)cid, (SVGA3dRenderTargetType)target_type,
                                        {(uint32_t)sid, 0, 0}};
        ret = svga_command_locked(SVGA_3D_CMD_SETRENDERTARGET, &cmd, sizeof(cmd));
        if (!ret) svga.contexts[cid].targets[target_type] = (uint32_t)sid;
    }
    svga_unlock(flags);
    return ret;
}

int svga3d_clear(svga_handle context, uint32_t flags, uint32_t color,
                 float depth, uint32_t stencil)
{
    uint64_t lock_flags = svga_lock();
    int cid = svga_find_context(context);
    int ret = !valid_cid((uint32_t)cid) ? SVGA_EINVAL : require_ready();
    if (!ret) {
        uint32_t sid = svga.contexts[cid].targets[(flags & SVGA3D_CLEAR_COLOR) ?
            SVGA3D_RT_COLOR0 : SVGA3D_RT_DEPTH];
        if (!flags || (flags & ~(SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH | SVGA3D_CLEAR_STENCIL)) ||
            !valid_sid(sid)) ret = SVGA_EINVAL;
        else {
            SVGA3dCmdClear cmd = {(uint32_t)cid, (SVGA3dClearFlag)flags, color, depth, stencil};
            SVGA3dRect rect = {0, 0, svga.surfaces[sid].desc.width, svga.surfaces[sid].desc.height};
            struct svga_span spans[] = {{&cmd, sizeof(cmd)}, {&rect, sizeof(rect)}};
            ret = svga_fifo_packet_locked(SVGA_3D_CMD_CLEAR, true, spans, 2);
        }
    }
    svga_unlock(lock_flags);
    return ret;
}

static int checked_gmr(int id, uint32_t offset, uint32_t bytes)
{
    if (id < 0 || (uint32_t)id >= SVGA_GMRS || !svga.gmrs[id].handle ||
        svga.gmrs[id].retiring || offset > svga.gmrs[id].bytes || bytes > svga.gmrs[id].bytes - offset)
        return SVGA_EINVAL;
    return 0;
}

int svga_surface_dma_locked(svga_handle surface, uint32_t face, uint32_t mip,
                       svga_handle buffer, uint32_t offset, uint32_t pitch,
                       uint32_t direction, const struct svga_dma_box *boxes,
                       uint32_t count)
{
    if (!boxes || !count || count > 64 || !pitch ||
        (direction != SVGA3D_WRITE_HOST_VRAM && direction != SVGA3D_READ_HOST_VRAM))
        return SVGA_EINVAL;
    int sid = svga_find_surface(surface), gid = svga_find_gmr(buffer);
    int ret = (!valid_sid((uint32_t)sid) || gid < 0 || face >= svga.surfaces[sid].faces ||
               mip >= (svga.surfaces[sid].desc.mip_levels ? svga.surfaces[sid].desc.mip_levels : 1u)) ? SVGA_EINVAL : require_ready();
    uint64_t bytes = 0;
    if (!ret) {
        uint32_t level_width = svga.surfaces[sid].desc.width;
        uint32_t level_height = svga.surfaces[sid].desc.height;
        uint32_t level_depth = svga.surfaces[sid].desc.depth ? svga.surfaces[sid].desc.depth : 1u;
        for (uint32_t level = 0; level < mip; ++level) {
            if (level_width > 1) level_width >>= 1;
            if (level_height > 1) level_height >>= 1;
            if (level_depth > 1) level_depth >>= 1;
        }
        level_width = (level_width + svga.surfaces[sid].block_w - 1u) /
                      svga.surfaces[sid].block_w;
        level_height = (level_height + svga.surfaces[sid].block_h - 1u) /
                       svga.surfaces[sid].block_h;
        for (uint32_t i = 0; i < count; ++i) {
            if (!boxes[i].w || !boxes[i].h || !boxes[i].d ||
                boxes[i].x > level_width || boxes[i].w > level_width - boxes[i].x ||
                boxes[i].y > level_height || boxes[i].h > level_height - boxes[i].y ||
                boxes[i].z > level_depth || boxes[i].d > level_depth - boxes[i].z ||
                (uint64_t)pitch * boxes[i].h > UINT32_MAX) { ret = SVGA_EINVAL; break; }
            uint64_t row_bytes = (uint64_t)boxes[i].w * svga.surfaces[sid].block_bytes;
            uint64_t source_x = (uint64_t)boxes[i].srcx * svga.surfaces[sid].block_bytes;
            if (source_x > pitch || row_bytes > pitch - source_x) {
                ret = SVGA_EINVAL;
                break;
            }
            uint64_t slice_stride = (uint64_t)pitch * level_height;
            uint64_t slice_index = (uint64_t)boxes[i].srcz + boxes[i].d - 1u;
            uint64_t row_index = (uint64_t)boxes[i].srcy + boxes[i].h - 1u;
            if ((slice_stride && slice_index > UINT32_MAX / slice_stride) ||
                (pitch && row_index > UINT32_MAX / pitch)) {
                ret = SVGA_EINVAL;
                break;
            }
            uint64_t last_slice = slice_index * slice_stride;
            uint64_t last_row = row_index * pitch;
            uint64_t end = last_slice + last_row +
                           source_x + row_bytes;
            if (end < last_slice || end < last_row || end < source_x || end > UINT32_MAX) {
                ret = SVGA_EINVAL;
                break;
            }
            if (end > bytes) bytes = end;
        }
        if (!ret) ret = checked_gmr(gid, offset, (uint32_t)bytes);
    }
    if (!ret) {
        SVGA3dCmdSurfaceDMA cmd = {0};
        cmd.guest.ptr.gmrId = (uint32_t)gid;
        cmd.guest.ptr.offset = offset;
        cmd.guest.pitch = pitch;
        cmd.host.sid = (uint32_t)sid;
        cmd.host.face = face;
        cmd.host.mipmap = mip;
        cmd.transfer = (SVGA3dTransferType)direction;
        struct svga_span spans[] = {{&cmd, sizeof(cmd)}, {boxes, count * sizeof(*boxes)}};
        ret = svga_fifo_packet_locked(SVGA_3D_CMD_SURFACE_DMA, true, spans, 2);
    }
    return ret;
}

int svga3d_surface_dma(svga_handle surface, uint32_t face, uint32_t mip,
                       svga_handle buffer, uint32_t offset, uint32_t pitch,
                       uint32_t direction, const struct svga_dma_box *boxes, uint32_t count)
{
    uint64_t flags = svga_lock();
    int ret = svga_surface_dma_locked(surface, face, mip, buffer, offset, pitch,
                                      direction, boxes, count);
    svga_unlock(flags);
    return ret;
}

int svga3d_present(svga_handle surface, const struct svga_present_rect *rects,
                   uint32_t count)
{
    if (!rects || !count || count > 64) return SVGA_EINVAL;
    uint64_t flags = svga_lock();
    int sid = svga_find_surface(surface);
    int ret = !valid_sid((uint32_t)sid) ? SVGA_EINVAL : require_ready();
    if (!ret) {
        for (uint32_t i = 0; i < count; ++i) {
            if (!rects[i].w || !rects[i].h || rects[i].x > svga.surfaces[sid].desc.width - 1u ||
                rects[i].y > svga.surfaces[sid].desc.height - 1u ||
                rects[i].w > svga.surfaces[sid].desc.width - rects[i].x ||
                rects[i].h > svga.surfaces[sid].desc.height - rects[i].y) { ret = SVGA_EINVAL; break; }
        }
    }
    if (!ret) {
        SVGA3dCmdPresent cmd = {(uint32_t)sid};
        struct svga_span spans[] = {{&cmd, sizeof(cmd)}, {rects, count * sizeof(*rects)}};
        ret = svga_fifo_packet_locked(SVGA_3D_CMD_PRESENT, true, spans, 2);
        if (!ret) ret = svga_sync_locked();
    }
    svga_unlock(flags);
    return ret;
}
