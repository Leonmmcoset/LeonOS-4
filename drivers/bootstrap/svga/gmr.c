#include "device.h"

static void release_pages(struct svga_gmr *g)
{
    svga.ops.free(g->phys, g->pages);
    svga.pages -= g->pages;
    *g = (struct svga_gmr){0};
}

static int define_gmr2(uint32_t id, uint32_t pages)
{
    SVGAFifoCmdDefineGMR2 cmd = {id, pages};
    struct svga_span span = {&cmd, sizeof(cmd)};
    return svga_fifo_packet_locked(SVGA_CMD_DEFINE_GMR2, false, &span, 1);
}

int svga_gmr_create_locked(uint32_t bytes, svga_handle *out)
{
    if (!out || !bytes || bytes > SVGA_MAX_GUEST_PAGES * 4096u) return SVGA_EINVAL;
    *out = 0;
    int ret = SVGA_ENODEV;
    uint32_t pages = (bytes + 4095u) / 4096u, id;
    if (!svga.available || !svga.memory_ready) goto done;
    for (id = 0; id < svga.gmr_limit && svga.gmrs[id].handle; ++id) {}
    ret = SVGA_ENOMEM;
    if (id == svga.gmr_limit || pages > svga.page_limit - svga.pages) goto done;
    svga_handle handle = svga_new_handle(SVGA_KIND_GMR, id);
    if (!handle) goto done;
    uint64_t phys = svga.ops.alloc(pages);
    if (!phys) goto done;
    void *ptr = svga.ops.pointer(phys);
    if (!ptr || (phys & 4095) || phys / 4096 > UINT32_MAX - pages) {
        svga.ops.free(phys, pages); goto done;
    }
    struct svga_gmr *g = &svga.gmrs[id];
    *g = (struct svga_gmr){.handle = handle, .phys = phys, .bytes = bytes, .pages = pages};
    svga.pages += pages;
    if (!(svga.caps & SVGA_CAP_GMR2)) {
        uint64_t desc_phys = svga.ops.alloc(1);
        SVGAGuestMemDescriptor *desc = desc_phys ? svga.ops.pointer(desc_phys) : NULL;
        if (!desc || (desc_phys & 4095) || (desc_phys >> 12) > UINT32_MAX) {
            if (desc_phys) svga.ops.free(desc_phys, 1);
            release_pages(g); goto done;
        }
        desc[0] = (SVGAGuestMemDescriptor){(uint32_t)(phys >> 12), pages};
        desc[1] = (SVGAGuestMemDescriptor){0, 0};
        svga_barrier();
        svga.ops.write(SVGA_REG_GMR_ID, id);
        svga.ops.write(SVGA_REG_GMR_DESCRIPTOR, (uint32_t)(desc_phys >> 12));
        /* The register write consumes the descriptor synchronously. */
        svga.ops.free(desc_phys, 1);
        g->defined = true;
    } else {
        ret = define_gmr2(id, pages);
        if (ret) { release_pages(g); goto done; }
        g->defined = true;
        for (uint32_t offset = 0; offset < pages;) {
            uint32_t ppns[256];
            uint32_t count = pages - offset;
            if (count > 256) count = 256;
            for (uint32_t j = 0; j < count; ++j) ppns[j] = (uint32_t)(phys >> 12) + offset + j;
            SVGAFifoCmdRemapGMR2 cmd = {id, SVGA_REMAP_GMR2_PPN32, offset, count};
            struct svga_span spans[] = {{&cmd, sizeof(cmd)}, {ppns, count * 4}};
            ret = svga_fifo_packet_locked(SVGA_CMD_REMAP_GMR2, false, spans, 2);
            if (ret) {
                /* Preserve an incomplete mapping until the host can retire it. */
                g->retiring = true;
                int cleanup = svga_gmr_destroy_locked(id);
                if (cleanup) {
                    /* Ownership remains with the caller when a stalled host
                     * prevents synchronous invalidation; it can retry using
                     * the returned handle without leaking the pinned pages. */
                    *out = handle;
                    ret = cleanup;
                    goto done;
                }
                goto done;
            }
            offset += count;
        }
    }
    *out = handle;
    ret = 0;
done:
    return ret;
}

int svga_gmr_create(uint32_t bytes, svga_handle *out)
{
    uint64_t flags = svga_lock();
    int ret = svga_gmr_create_locked(bytes, out);
    svga_unlock(flags);
    return ret;
}

int svga_gmr_destroy_locked(uint32_t id)
{
    struct svga_gmr *g = &svga.gmrs[id];
    int ret = svga_sync_locked();
    if (ret) return ret;
    if (g->defined) {
        if (!(svga.caps & SVGA_CAP_GMR2)) {
            svga.ops.write(SVGA_REG_GMR_ID, id);
            svga.ops.write(SVGA_REG_GMR_DESCRIPTOR, 0);
        } else {
            ret = define_gmr2(id, 0);
            if (ret) return ret;
            /* Mark before waiting: retrying destruction must not redefine it. */
            g->defined = false;
            g->retiring = true;
            ret = svga_sync_locked();
            if (ret) return ret;
        }
    }
    release_pages(g);
    return 0;
}

int svga_gmr_destroy(svga_handle handle)
{
    uint64_t flags = svga_lock();
    int id = svga_find_gmr(handle);
    int ret = id < 0 ? SVGA_EINVAL : svga_gmr_destroy_locked((uint32_t)id);
    svga_unlock(flags);
    return ret;
}

int svga_gmr_transfer(svga_handle handle, uint32_t offset, void *data,
                      uint32_t bytes, bool write)
{
    if (!data && bytes) return SVGA_EINVAL;
    uint64_t flags = svga_lock();
    int id = svga_find_gmr(handle), ret = SVGA_EINVAL;
    if (id < 0) goto done;
    struct svga_gmr *g = &svga.gmrs[id];
    if (g->retiring || offset > g->bytes || bytes > g->bytes - offset) goto done;
    ret = svga_sync_locked();
    if (ret) goto done;
    uint8_t *p = (uint8_t *)svga.ops.pointer(g->phys) + offset;
    uint8_t *q = data;
    for (uint32_t i = 0; i < bytes; ++i) {
        if (write) p[i] = q[i]; else q[i] = p[i];
    }
    svga_barrier();
done:
    svga_unlock(flags);
    return ret;
}
