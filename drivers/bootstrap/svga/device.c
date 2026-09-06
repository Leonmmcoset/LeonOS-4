#include "device.h"
#ifndef SVGA_HOST_TEST
#include <ntclks/lock.h>
#include <ntclks/mm.h>
#include <ntclks/paging.h>
#include <ntclks/port.h>
#include <ntclks/framebuffer.h>
#include <ntclks/console.h>
static struct kernel_spinlock device_lock = KERNEL_SPINLOCK_INIT;
static uint16_t io_port;
#endif

struct svga_device svga;

uint64_t svga_lock(void)
{
#ifdef SVGA_HOST_TEST
    return 0;
#else
    uint64_t flags;
    kernel_spin_lock_irqsave(&device_lock, &flags);
    return flags;
#endif
}
void svga_unlock(uint64_t flags)
{
#ifdef SVGA_HOST_TEST
    (void)flags;
#else
    kernel_spin_unlock_irqrestore(&device_lock, flags);
#endif
}
void svga_barrier(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
void svga_relax(void) { __asm__ volatile("pause" ::: "memory"); }

#ifndef SVGA_HOST_TEST
static uint32_t port_read(uint32_t reg)
{
    x86_64_outl(reg, io_port);
    return x86_64_inl(io_port + 1);
}
static void port_write(uint32_t reg, uint32_t value)
{
    x86_64_outl(reg, io_port);
    x86_64_outl(value, io_port + 1);
}
static uint64_t device_clock(void)
{
    uint32_t low, high;
    __asm__ volatile("lfence; rdtsc; lfence" : "=a"(low), "=d"(high) :: "memory");
    return ((uint64_t)high << 32) | low;
}
void svga_platform_bind(uint16_t port, volatile uint32_t *fifo, uint32_t bytes)
{
    io_port = port;
    const struct svga_ops ops = {port_read, port_write, mm_alloc_pages,
                                 mm_free_pages, paging_kernel_direct_map, device_clock};
    svga_bind(&ops, fifo, bytes);
}
#endif

void svga_bind(const struct svga_ops *ops, volatile uint32_t *fifo, uint32_t bytes)
{
    svga.ops = *ops;
    svga.fifo = fifo;
    svga.fifo_bytes = bytes;
    svga.bound = true;
}

/* Record the original failure before a four-register 2D FIFO replaces it. */
static int probe_result(const char *stage, int status)
{
    svga.probe.stage = stage;
    svga.probe.status = status;
    return status;
}

int svga_configure_locked(bool extended)
{
    if (svga.gb_active) return SVGA_EBUSY;
    if (extended) {
        svga.probe = (struct svga_probe_info){.stage = "fifo-setup", .status = SVGA_EIO};
    }
    if (!svga.bound || !svga.fifo || svga.fifo_bytes < 16384 ||
        (svga.fifo_bytes & 3)) {
        if (extended) probe_result("fifo-mapping", SVGA_ENODEV);
        return SVGA_ENODEV;
    }
    svga.ops.write(SVGA_REG_CONFIG_DONE, 0);
    svga.fifo_ready = false;
    svga.available = false;
    svga.caps = svga.ops.read(SVGA_REG_CAPABILITIES);
    if (extended) svga.probe.enable = svga.ops.read(SVGA_REG_ENABLE);
    /* Keep the legacy path's four DWORD header. SVGA_FIFO_MIN is a byte
     * offset, so this yields the 16-byte minimum used by the 2D adapter. */
    uint32_t regs = 4;
    if (extended && (svga.caps & SVGA_CAP_EXTENDED_FIFO)) {
        regs = svga.ops.read(SVGA_REG_MEM_REGS);
        svga.probe.mem_regs = regs;
        if (regs < SVGA_FIFO_NUM_REGS) regs = SVGA_FIFO_NUM_REGS;
        if (regs > (svga.fifo_bytes - 10240) / 4)
            return probe_result("fifo-registers", SVGA_EIO);
    }
    svga.min = regs * 4;
    if (extended) svga.probe.fifo_min = svga.min;
    svga.fifo[SVGA_FIFO_MIN] = svga.min;
    svga.fifo[SVGA_FIFO_MAX] = svga.fifo_bytes;
    svga.fifo[SVGA_FIFO_NEXT_CMD] = svga.min;
    svga.fifo[SVGA_FIFO_STOP] = svga.min;
    if (regs > SVGA_FIFO_GUEST_3D_HWVERSION) {
        svga.fifo[SVGA_FIFO_GUEST_3D_HWVERSION] = SVGA3D_HWVERSION_WS8_B1;
        svga.fifo[SVGA_FIFO_RESERVED] = 0;
        svga.fifo[SVGA_FIFO_FENCE] = 0;
    }
    if (regs > SVGA_FIFO_BUSY) svga.fifo[SVGA_FIFO_BUSY] = 0;
    svga_barrier();
    svga.ops.write(SVGA_REG_CONFIG_DONE, 1);
    if (svga.fifo[SVGA_FIFO_MIN] != svga.min || svga.fifo[SVGA_FIFO_MAX] != svga.fifo_bytes) {
        if (extended) probe_result("fifo-header", SVGA_EIO);
        return SVGA_EIO;
    }
    svga.fifo_caps = regs > SVGA_FIFO_CAPABILITIES ? svga.fifo[SVGA_FIFO_CAPABILITIES] : 0;
    svga.fifo_ready = true;
    ++svga.generation;
    if (!svga.generation) ++svga.generation;
    svga.next_fence = 1;
    svga.issued_fence = 0;
    if (extended) (void)svga_negotiate_locked();
    return 0;
}

int svga_negotiate_locked(void)
{
    svga.available = false;
    svga.host_version = 0;
    svga.probe.fifo_caps = svga.fifo_caps;
    svga.probe.host_legacy = svga.min > SVGA_FIFO_3D_HWVERSION * 4 ?
                            svga.fifo[SVGA_FIFO_3D_HWVERSION] : 0;
    svga.probe.host_revised = svga.min > SVGA_FIFO_3D_HWVERSION_REVISED * 4 ?
                             svga.fifo[SVGA_FIFO_3D_HWVERSION_REVISED] : 0;
    svga.probe.gb_objects = (svga.caps & SVGA_CAP_GBOBJECTS) != 0;
    svga.probe.devcap_3d = 0;
    if (svga.probe.gb_objects) {
        svga.ops.write(SVGA_REG_DEV_CAP, SVGA3D_DEVCAP_3D);
        svga.probe.devcap_3d = svga.ops.read(SVGA_REG_DEV_CAP);
    }
    for (uint32_t i = 0; i < SVGA3D_DEVCAP_MAX; ++i) {
        svga.cap_valid[i] = false; svga.cap_values[i] = 0;
    }
    if (!(svga.caps & SVGA_CAP_3D)) return probe_result("device-3d", SVGA_ENOTSUP);
    if (!(svga.caps & SVGA_CAP_EXTENDED_FIFO))
        return probe_result("extended-fifo", SVGA_ENOTSUP);
    if (!(svga.fifo_caps & SVGA_FIFO_CAP_FENCE)) return probe_result("fence", SVGA_ENOTSUP);
    if (!(svga.caps & (SVGA_CAP_GMR | SVGA_CAP_GMR2))) return probe_result("gmr", SVGA_ENOTSUP);
    if (svga.min <= SVGA_FIFO_GUEST_3D_HWVERSION * 4)
        return probe_result("guest-version-register", SVGA_ENOTSUP);
    if (svga.probe.gb_objects) {
        if (!svga.probe.devcap_3d) return probe_result("devcap-3d", SVGA_ENOTSUP);
        /* GB devices expose capabilities through indexed registers. Like
         * vmwgfx's legacy userspace interface, report WS8_B1 compatibility. */
        svga.host_version = SVGA3D_HWVERSION_WS8_B1;
        for (uint32_t i = 0; i < SVGA3D_DEVCAP_MAX; ++i) {
            svga.ops.write(SVGA_REG_DEV_CAP, i);
            svga.cap_values[i] = svga.ops.read(SVGA_REG_DEV_CAP);
            svga.cap_valid[i] = true;
        }
    } else {
        svga.host_version = svga.fifo[(svga.fifo_caps & SVGA_FIFO_CAP_3D_HWVERSION_REVISED) ?
                                     SVGA_FIFO_3D_HWVERSION_REVISED : SVGA_FIFO_3D_HWVERSION];
        if (svga.host_version < SVGA3D_HWVERSION_WS65_B1)
            return probe_result("host-version", SVGA_ENOTSUP);
        uint32_t pos = SVGA_FIFO_3D_CAPS;
        while (pos <= SVGA_FIFO_3D_CAPS_LAST) {
            uint32_t length = svga.fifo[pos];
            if (!length) break;
            if (length < 2 || length > SVGA_FIFO_3D_CAPS_LAST + 1 - pos)
                return probe_result("cap-record", SVGA_EIO);
            /* DEVCAPS records contain index/value pairs. */
            if (svga.fifo[pos + 1] == 0x100) {
                if (length & 1) return probe_result("cap-record", SVGA_EIO);
                for (uint32_t p = pos + 2; p < pos + length; p += 2) {
                    uint32_t index = svga.fifo[p];
                    if (index < SVGA3D_DEVCAP_MAX) {
                        svga.cap_values[index] = svga.fifo[p + 1];
                        svga.cap_valid[index] = true;
                    }
                }
            }
            pos += length;
        }
    }
    if (!svga.cap_valid[SVGA3D_DEVCAP_3D] || !svga.cap_values[SVGA3D_DEVCAP_3D])
        return probe_result("devcap-3d", SVGA_ENOTSUP);
    svga.context_limit = SVGA_CONTEXTS;
    svga.surface_limit = SVGA_SURFACES;
    if (!svga.probe.gb_objects && svga.cap_valid[SVGA3D_DEVCAP_MAX_CONTEXT_IDS] &&
        svga.cap_values[SVGA3D_DEVCAP_MAX_CONTEXT_IDS] < svga.context_limit)
        svga.context_limit = svga.cap_values[SVGA3D_DEVCAP_MAX_CONTEXT_IDS];
    if (!svga.probe.gb_objects && svga.cap_valid[SVGA3D_DEVCAP_MAX_SURFACE_IDS] &&
        svga.cap_values[SVGA3D_DEVCAP_MAX_SURFACE_IDS] < svga.surface_limit)
        svga.surface_limit = svga.cap_values[SVGA3D_DEVCAP_MAX_SURFACE_IDS];
    svga.gmr_limit = svga.ops.read(SVGA_REG_GMR_MAX_IDS);
    if (svga.gmr_limit > SVGA_GMRS) svga.gmr_limit = SVGA_GMRS;
    svga.page_limit = SVGA_MAX_GUEST_PAGES;
    svga.surface_limit_bytes = SVGA_MAX_SURFACE_BYTES;
    if (svga.caps & SVGA_CAP_GMR2) {
        uint32_t pages = svga.ops.read(SVGA_REG_GMRS_MAX_PAGES);
        if (pages < svga.page_limit) svga.page_limit = pages;
    } else if (svga.ops.read(SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH) < 2)
        return probe_result("gmr-descriptors", SVGA_ENOTSUP);
    if (svga.probe.gb_objects) {
        uint32_t pages = svga.ops.read(SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB) / 4;
        svga.mob_max_bytes = svga.ops.read(SVGA_REG_MOB_MAX_SIZE);
        if (!pages || svga.mob_max_bytes < SVGA_GB_CONTEXT_BYTES)
            return probe_result("mob-limits", SVGA_ENOTSUP);
        if (pages < svga.page_limit) svga.page_limit = pages;
        uint64_t bytes = (uint64_t)svga.page_limit * 4096u;
        if (bytes < svga.surface_limit_bytes) svga.surface_limit_bytes = bytes;
    } else if (svga.caps & SVGA_CAP_GMR2) {
        uint32_t memory = svga.ops.read(SVGA_REG_MEMORY_SIZE);
        uint32_t vram = svga.ops.read(SVGA_REG_VRAM_SIZE);
        if (memory < vram) return probe_result("surface-memory", SVGA_ENOTSUP);
        memory -= vram;
        if (memory < svga.surface_limit_bytes) svga.surface_limit_bytes = memory;
    }
    if (!svga.context_limit || !svga.surface_limit || !svga.gmr_limit ||
        !svga.page_limit || !svga.surface_limit_bytes)
        return probe_result("resource-limits", SVGA_ENOTSUP);
    if (svga.probe.gb_objects) {
        int ret = svga_gb_init_locked();
        if (ret) return probe_result("gb-tables", ret);
    }
    svga.available = true;
    return probe_result(svga.probe.gb_objects ? "ready-gb" : "ready", 0);
}

svga_handle svga_new_handle(uint32_t kind, uint32_t slot)
{
    if (svga.handle_serial == UINT32_MAX) return 0;
    ++svga.handle_serial;
    return ((uint64_t)svga.handle_serial << 32) | (kind << 24) | slot;
}
int svga_find_context(svga_handle h)
{
    uint32_t id = (uint32_t)h & 0xffffff;
    return h && id < SVGA_CONTEXTS && svga.contexts[id].handle == h ? (int)id : -1;
}
int svga_find_surface(svga_handle h)
{
    uint32_t id = (uint32_t)h & 0xffffff;
    return h && id < SVGA_SURFACES && svga.surfaces[id].handle == h ? (int)id : -1;
}
int svga_find_gmr(svga_handle h)
{
    uint32_t id = (uint32_t)h & 0xffffff;
    return h && id < SVGA_GMRS && svga.gmrs[id].handle == h ? (int)id : -1;
}

bool svga_context_usable(uint32_t id)
{
    return id < SVGA_CONTEXTS && svga.contexts[id].handle && !svga.contexts[id].gb.retiring;
}
bool svga_surface_usable(uint32_t id)
{
    return id < SVGA_SURFACES && svga.surfaces[id].handle && !svga.surfaces[id].gb.retiring;
}

uint32_t svga_resource_id(svga_handle h)
{
    uint64_t flags = svga_lock();
    int id = svga_find_context(h);
    if (id < 0) id = svga_find_surface(h);
    if (id < 0) id = svga_find_gmr(h);
    svga_unlock(flags);
    return id < 0 ? UINT32_MAX : (uint32_t)id;
}

void svga_get_info(struct svga_info *out)
{
    if (!out) return;
    uint64_t flags = svga_lock();
    *out = (struct svga_info){ .device_caps = svga.caps, .fifo_caps = svga.fifo_caps,
        .host_version = svga.host_version, .guest_version = SVGA3D_HWVERSION_WS8_B1,
        .generation = svga.generation, .guest_pages = svga.pages,
        .surface_bytes = svga.surface_bytes, .fifo_ready = svga.fifo_ready,
        .available = svga.available, .probe = svga.probe };
    for (uint32_t i = 0; i < SVGA_CONTEXTS; ++i) out->contexts += !!svga.contexts[i].handle;
    for (uint32_t i = 0; i < SVGA_SURFACES; ++i) out->surfaces += !!svga.surfaces[i].handle;
    for (uint32_t i = 0; i < SVGA_GMRS; ++i) out->gmrs += !!svga.gmrs[i].handle;
    svga_unlock(flags);
}

int svga3d_capability(uint32_t index, uint32_t *out)
{
    uint64_t flags = svga_lock();
    int ret = SVGA_ENOTSUP;
    if (out && index < SVGA3D_DEVCAP_MAX && svga.cap_valid[index]) {
        *out = svga.cap_values[index]; ret = 0;
    }
    svga_unlock(flags);
    return ret;
}

int svga3d_init(void)
{
    uint64_t flags = svga_lock();
    svga.probe = (struct svga_probe_info){.stage = "not-bound", .status = SVGA_ENODEV};
    if (!svga.bound) {
        svga_unlock(flags);
        return SVGA_ENODEV;
    }
#ifndef SVGA_HOST_TEST
    const struct framebuffer *fb = framebuffer_get();
    if (!paging_mmio_uncached((uint64_t)(uintptr_t)svga.fifo, svga.fifo_bytes) ||
        !paging_mmio_uncached(fb->reservation_start, fb->reservation_bytes)) {
        probe_result("mmio-cache", SVGA_ENOTSUP);
        svga_unlock(flags);
        console_printf("[svga3d] UC mapping failed; retaining 2D\n");
        return SVGA_ENOTSUP;
    }
#endif
    int ret = svga_shutdown_locked();
    if (ret) probe_result("drain", ret);
    if (!ret) {
        svga.memory_ready = true;
        ret = svga_configure_locked(true);
        if (ret || !svga.available) {
            int status = ret ? ret : svga.probe.status;
            ret = svga_gb_shutdown_locked();
            if (!ret) {
                svga.ops.write(SVGA_REG_CONFIG_DONE, 0);
                svga.fifo_ready = false;
                ret = svga_configure_locked(false) == 0 ? status : SVGA_EIO;
            }
        }
    }
    svga_unlock(flags);
#ifndef SVGA_HOST_TEST
    console_printf("[svga3d] init=%d caps=0x%x fifo=0x%x host=0x%x guest=0x%x\n",
                   ret, svga.caps, svga.fifo_caps, svga.host_version, SVGA3D_HWVERSION_WS8_B1);
#endif
    return ret;
}

int svga_shutdown_locked(void)
{
    if (!svga.fifo_ready) return svga.gb_active ? SVGA_EBUSY : 0;
    int ret = svga_sync_locked();
    if (ret) return ret;
    for (uint32_t i = 0; i < SVGA_CONTEXTS; ++i)
        if (svga.contexts[i].handle && (ret = svga_context_destroy_locked(i))) return ret;
    for (uint32_t i = 0; i < SVGA_SURFACES; ++i)
        if (svga.surfaces[i].handle && (ret = svga_surface_destroy_locked(i))) return ret;
    for (uint32_t i = 0; i < SVGA_GMRS; ++i)
        if (svga.gmrs[i].handle && (ret = svga_gmr_destroy_locked(i))) return ret;
    if ((ret = svga_gb_shutdown_locked())) return ret;
    svga_gpu_reset_locked();
    svga.available = false;
    return 0;
}
int svga3d_shutdown(void)
{
    uint64_t flags = svga_lock();
    int ret = svga_shutdown_locked();
    if (!ret && svga.bound) {
        svga.ops.write(SVGA_REG_CONFIG_DONE, 0);
        svga.fifo_ready = false;
        if (svga_configure_locked(false) != 0) ret = SVGA_EIO;
    }
    svga_unlock(flags);
    return ret;
}
int svga_update(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    uint32_t data[] = {x, y, width, height};
    struct svga_span span = {data, sizeof(data)};
    uint64_t flags = svga_lock();
    int ret = svga_fifo_packet_locked(SVGA_CMD_UPDATE, false, &span, 1);
    if (!ret) ret = svga_drain_locked();
    svga_unlock(flags);
    return ret;
}
int svga_mode_begin(uint64_t *flags)
{
    *flags = svga_lock();
    int ret = svga_shutdown_locked();
    if (!ret && svga.bound) {
        svga.ops.write(SVGA_REG_CONFIG_DONE, 0);
        svga.fifo_ready = false;
        svga.available = false;
    }
    if (ret) svga_unlock(*flags);
    return ret;
}
void svga_mode_end(uint64_t flags)
{
    int ret = svga_configure_locked(svga.memory_ready);
    if (ret || (svga.memory_ready && !svga.available)) {
        if (!svga_gb_shutdown_locked()) {
            svga.ops.write(SVGA_REG_CONFIG_DONE, 0);
            svga.fifo_ready = false;
            (void)svga_configure_locked(false);
        }
    }
    svga_unlock(flags);
}
