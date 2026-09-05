#include "device.h"

static void backing_free(struct svga_mob *m)
{
    if (m->pt_phys) svga.ops.free(m->pt_phys, m->pt_pages);
    if (m->phys) svga.ops.free(m->phys, m->pages);
    svga.pages -= m->pages + m->pt_pages;
    *m = (struct svga_mob){0};
}

static uint64_t alloc_zero(uint32_t pages)
{
    uint64_t phys = svga.ops.alloc(pages);
    if (!phys) return 0;
    uint32_t *ptr = svga.ops.pointer(phys);
    if (!ptr || (phys & 4095u) || (phys >> 12) > UINT32_MAX - pages) {
        svga.ops.free(phys, pages);
        return 0;
    }
    for (uint32_t i = 0; i < pages * 1024u; ++i) ptr[i] = 0;
    return phys;
}

static int backing_alloc(struct svga_mob *m, uint32_t bytes)
{
    if (!bytes || bytes > svga.mob_max_bytes || bytes > SVGA_MAX_GUEST_PAGES * 4096u)
        return SVGA_ENOMEM;
    uint32_t pages = (bytes + 4095u) / 4096u;
    uint32_t tables = pages <= 1 ? 0 : pages <= 1024 ? 1 : 1 + (pages + 1023u) / 1024u;
    if (svga.pages > svga.page_limit || pages + tables > svga.page_limit - svga.pages)
        return SVGA_ENOMEM;
    uint64_t phys = alloc_zero(pages);
    if (!phys) return SVGA_ENOMEM;
    uint64_t pt_phys = tables ? alloc_zero(tables) : 0;
    if (tables && !pt_phys) {
        svga.ops.free(phys, pages);
        return SVGA_ENOMEM;
    }
    *m = (struct svga_mob){.phys = phys, .pt_phys = pt_phys, .pages = pages,
        .pt_pages = tables, .bytes = bytes,
        .depth = tables > 1 ? SVGA3D_MOBFMT_PT_2 : tables ? SVGA3D_MOBFMT_PT_1 : SVGA3D_MOBFMT_PT_0};
    if (tables) {
        uint32_t *pt = svga.ops.pointer(pt_phys);
        uint32_t *leaves = tables > 1 ? pt + 1024 : pt;
        if (tables > 1)
            for (uint32_t i = 0; i < tables - 1; ++i) pt[i] = (uint32_t)(pt_phys >> 12) + i + 1;
        for (uint32_t i = 0; i < pages; ++i) leaves[i] = (uint32_t)(phys >> 12) + i;
    }
    svga.pages += pages + tables;
    svga_barrier();
    return 0;
}

static uint32_t backing_root(const struct svga_mob *m)
{
    return (uint32_t)((m->pt_phys ? m->pt_phys : m->phys) >> 12);
}

static int mob_create(struct svga_mob *m, uint32_t id, uint32_t bytes)
{
    int ret = backing_alloc(m, bytes);
    if (ret) return ret;
    SVGA3dCmdDefineGBMob cmd = {id, m->depth, backing_root(m), m->bytes};
    ret = svga_command_locked(SVGA_3D_CMD_DEFINE_GB_MOB, &cmd, sizeof(cmd));
    if (!ret) m->defined = true;
    else backing_free(m);
    return ret;
}

int svga_gb_init_locked(void)
{
    static const uint32_t sizes[SVGA_OTABLE_DX9_MAX] = {
        SVGA_MOBS * SVGA_GB_MOB_ENTRY_BYTES,
        SVGA_SURFACES * SVGA_GB_SURFACE_ENTRY_BYTES,
        SVGA_CONTEXTS * SVGA_GB_CONTEXT_ENTRY_BYTES,
        4096, 0
    };
    _Static_assert(SVGA_MOBS * SVGA_GB_MOB_ENTRY_BYTES <= 4194304u &&
                   SVGA_SURFACES * SVGA_GB_SURFACE_ENTRY_BYTES <= 4194304u &&
                   SVGA_CONTEXTS * SVGA_GB_CONTEXT_ENTRY_BYTES <= 4194304u,
                   "OTable PT2 requires a separate device capability");
    if (!svga.memory_ready || svga.gb_active) return SVGA_EBUSY;
    svga.gb_active = true;
    int ret = 0;
    for (uint32_t i = 0; i < SVGA_OTABLE_DX9_MAX; ++i) {
        /* A Screen Target table switches off the legacy display interface.
         * Keep it absent while framebuffer UPDATE and v9 PRESENT own scanout. */
        if (!sizes[i]) continue;
        struct svga_mob *m = &svga.otables[i];
        ret = backing_alloc(m, (sizes[i] + 4095u) & ~4095u);
        if (ret) break;
        SVGA3dCmdSetOTableBase cmd = {i, backing_root(m), m->bytes, 0, m->depth};
        ret = svga_command_locked(SVGA_3D_CMD_SET_OTABLE_BASE, &cmd, sizeof(cmd));
        if (ret) break;
        m->defined = true;
    }
    if (!ret) ret = svga_sync_locked();
    if (ret) (void)svga_gb_shutdown_locked();
    return ret;
}

int svga_gb_shutdown_locked(void)
{
    if (!svga.gb_active) return 0;
    svga.available = false;
    /* Object tables remain pinned until every detach has passed a Fence.
     * A failed submission or wait leaves the remaining state retryable. */
    for (uint32_t i = SVGA_OTABLE_DX9_MAX; i-- > 0;) {
        struct svga_mob *m = &svga.otables[i];
        if (!m->defined) continue;
        SVGA3dCmdSetOTableBase cmd = {i, 0, 0, 0, SVGA3D_MOBFMT_INVALID};
        int ret = svga_command_locked(SVGA_3D_CMD_SET_OTABLE_BASE, &cmd, sizeof(cmd));
        if (ret) return ret;
        m->defined = false;
    }
    int ret = svga_sync_locked();
    if (ret) return ret;
    for (uint32_t i = 0; i < SVGA_OTABLE_DX9_MAX; ++i) backing_free(&svga.otables[i]);
    svga.gb_active = false;
    return 0;
}

int svga_gb_context_create_locked(uint32_t id)
{
    struct svga_gb_resource *r = &svga.contexts[id].gb;
    int ret = mob_create(&r->mob, id, SVGA_GB_CONTEXT_BYTES);
    if (ret) return ret;
    ret = svga_command_locked(SVGA_3D_CMD_DEFINE_GB_CONTEXT, &id, sizeof(id));
    if (ret) return ret;
    r->defined = true;
    SVGA3dCmdBindGBContext bind = {id, id, 0};
    ret = svga_command_locked(SVGA_3D_CMD_BIND_GB_CONTEXT, &bind, sizeof(bind));
    if (!ret) r->bound = true;
    if (!ret) ret = svga_sync_locked();
    return ret;
}

int svga_gb_surface_create_locked(uint32_t id)
{
    struct svga_surface *s = &svga.surfaces[id];
    struct svga_gb_resource *r = &s->gb;
    uint32_t mobid = SVGA_CONTEXTS + id;
    int ret = mob_create(&r->mob, mobid, (uint32_t)s->bytes);
    if (ret) return ret;
    SVGA3dCmdDefineGBSurface cmd = {.sid = id, .surfaceFlags = s->desc.flags,
        .format = s->desc.format, .numMipLevels = s->desc.mip_levels ? s->desc.mip_levels : 1,
        .autogenFilter = SVGA3D_TEX_FILTER_NONE,
        .size = {s->desc.width, s->desc.height, s->desc.depth ? s->desc.depth : 1}};
    ret = svga_command_locked(SVGA_3D_CMD_DEFINE_GB_SURFACE, &cmd, sizeof(cmd));
    if (ret) return ret;
    r->defined = true;
    SVGA3dCmdBindGBSurface bind = {id, mobid};
    ret = svga_command_locked(SVGA_3D_CMD_BIND_GB_SURFACE, &bind, sizeof(bind));
    if (ret) return ret;
    r->bound = true;
    ret = svga_command_locked(SVGA_3D_CMD_UPDATE_GB_SURFACE, &id, sizeof(id));
    if (!ret) ret = svga_sync_locked();
    return ret;
}

int svga_gb_destroy_locked(struct svga_gb_resource *r, uint32_t id, bool context)
{
    r->retiring = true;
    int ret;
    if (r->bound) {
        if (context) {
            SVGA3dCmdBindGBContext cmd = {id, SVGA3D_INVALID_ID, 0};
            ret = svga_command_locked(SVGA_3D_CMD_BIND_GB_CONTEXT, &cmd, sizeof(cmd));
        } else {
            ret = svga_command_locked(SVGA_3D_CMD_INVALIDATE_GB_SURFACE, &id, sizeof(id));
            if (ret) return ret;
            SVGA3dCmdBindGBSurface cmd = {id, SVGA3D_INVALID_ID};
            ret = svga_command_locked(SVGA_3D_CMD_BIND_GB_SURFACE, &cmd, sizeof(cmd));
        }
        if (ret) return ret;
        r->bound = false;
    }
    if (r->defined) {
        ret = svga_command_locked(context ? SVGA_3D_CMD_DESTROY_GB_CONTEXT :
                                  SVGA_3D_CMD_DESTROY_GB_SURFACE, &id, sizeof(id));
        if (ret) return ret;
        r->defined = false;
    }
    if (r->mob.defined) {
        uint32_t mobid = context ? id : SVGA_CONTEXTS + id;
        ret = svga_command_locked(SVGA_3D_CMD_DESTROY_GB_MOB, &mobid, sizeof(mobid));
        if (ret) return ret;
        r->mob.defined = false;
    }
    if (r->mob.phys) {
        ret = svga_sync_locked();
        if (ret) return ret;
        backing_free(&r->mob);
    }
    return 0;
}
