/*
 * Minimal NVMe 1.x block transport.
 *
 * The bootstrap storage stack is deliberately synchronous: each controller
 * receives one admin queue and one I/O queue and completion queues are polled.
 * This keeps the transport compatible with the existing filesystem API while
 * avoiding a second asynchronous ownership model beside AHCI's legacy path.
 */

#define NVME_MAX_CONTROLLERS 4u
#define NVME_QUEUE_DEPTH 16u
#define NVME_PAGE_SIZE 4096u
#define NVME_MAX_SECTORS (NVME_PAGE_SIZE / SECTOR_SIZE)
#define NVME_WAIT_SPINS 40000000u
#define NVME_MAX_NAMESPACE_SCAN_PAGES 8u

#define NVME_REG_CAP 0x0000u
#define NVME_REG_VS 0x0008u
#define NVME_REG_CC 0x0014u
#define NVME_REG_CSTS 0x001cu
#define NVME_REG_AQA 0x0024u
#define NVME_REG_ASQ 0x0028u
#define NVME_REG_ACQ 0x0030u
#define NVME_REG_DOORBELL 0x1000u

#define NVME_CC_EN 0x00000001u
#define NVME_CC_IOSQES_SHIFT 16u
#define NVME_CC_IOCQES_SHIFT 20u
#define NVME_CSTS_RDY 0x00000001u
#define NVME_CSTS_CFS 0x00000002u

#define NVME_ADMIN_CREATE_IO_SQ 0x01u
#define NVME_ADMIN_CREATE_IO_CQ 0x05u
#define NVME_ADMIN_IDENTIFY 0x06u
#define NVME_IO_WRITE 0x01u
#define NVME_IO_READ 0x02u

#define NVME_IDENTIFY_NAMESPACE 0u
#define NVME_IDENTIFY_CONTROLLER 1u
#define NVME_IDENTIFY_ACTIVE_NAMESPACE_LIST 2u

struct __attribute__((packed)) nvme_command {
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t reserved0;
    uint64_t metadata;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
};

struct __attribute__((packed)) nvme_completion {
    uint32_t result;
    uint32_t reserved0;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
};

struct nvme_controller {
    uint8_t present;
    uint8_t ready;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t cq_phase_admin;
    uint8_t cq_phase_io;
    uint16_t queue_depth;
    uint16_t admin_sq_tail;
    uint16_t admin_cq_head;
    uint16_t io_sq_tail;
    uint16_t io_cq_head;
    uint16_t next_command_id;
    uint32_t doorbell_stride;
    uint32_t namespace_count;
    uint64_t cap;
    volatile uint8_t *mmio;
    uint64_t admin_sq_phys;
    uint64_t admin_cq_phys;
    uint64_t io_sq_phys;
    uint64_t io_cq_phys;
    uint64_t identify_phys;
    uint64_t namespace_list_phys;
    uint64_t io_buffer_phys;
    struct nvme_command *admin_sq;
    struct nvme_completion *admin_cq;
    struct nvme_command *io_sq;
    struct nvme_completion *io_cq;
    uint8_t *identify_buffer;
    uint8_t *namespace_list;
    uint8_t *io_buffer;
    char model[41];
    struct kernel_spinlock lock;
};

static struct nvme_controller g_nvme_controllers[NVME_MAX_CONTROLLERS];
static uint32_t g_nvme_controller_count;

static void nvme_write32(const struct nvme_controller *controller, uint32_t offset,
                         uint32_t value);
static int nvme_wait_ready(const struct nvme_controller *controller, uint8_t wanted);
static void nvme_release_controller_memory(struct nvme_controller *controller);

static void nvme_reset_all_controllers(void)
{
    for (uint32_t i = 0; i < g_nvme_controller_count; ++i) {
        struct nvme_controller *controller = &g_nvme_controllers[i];
        if (controller->mmio && controller->ready) {
            nvme_write32(controller, NVME_REG_CC, 0);
            (void)nvme_wait_ready(controller, 0);
        }
        nvme_release_controller_memory(controller);
        storage_memzero(controller, sizeof(*controller));
    }
    g_nvme_controller_count = 0;
}

static void nvme_cpu_relax(void)
{
    __asm__ volatile("pause");
}

static void nvme_memory_barrier(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

static uint32_t nvme_read32(const struct nvme_controller *controller, uint32_t offset)
{
    return *(volatile uint32_t *)(void *)(controller->mmio + offset);
}

static uint64_t nvme_read64(const struct nvme_controller *controller, uint32_t offset)
{
    return *(volatile uint64_t *)(void *)(controller->mmio + offset);
}

static void nvme_write32(const struct nvme_controller *controller, uint32_t offset,
                         uint32_t value)
{
    *(volatile uint32_t *)(void *)(controller->mmio + offset) = value;
}

static void nvme_write64(const struct nvme_controller *controller, uint32_t offset,
                         uint64_t value)
{
    *(volatile uint64_t *)(void *)(controller->mmio + offset) = value;
}

static int nvme_wait_ready(const struct nvme_controller *controller, uint8_t wanted)
{
    for (uint32_t i = 0; i < NVME_WAIT_SPINS; ++i) {
        uint32_t status = nvme_read32(controller, NVME_REG_CSTS);
        if ((status & NVME_CSTS_CFS) != 0) {
            return -5;
        }
        if (((status & NVME_CSTS_RDY) != 0) == (wanted != 0)) {
            return 0;
        }
        nvme_cpu_relax();
    }
    return -5;
}

static uint32_t nvme_doorbell_offset(const struct nvme_controller *controller,
                                     uint16_t queue_id, uint8_t completion)
{
    return NVME_REG_DOORBELL +
           ((uint32_t)queue_id * 2u + (completion ? 1u : 0u)) *
               controller->doorbell_stride;
}

static int nvme_allocate_page(uint64_t *out_phys, uint8_t **out_virtual)
{
    uint64_t phys;
    void *mapping;
    if (!out_phys || !out_virtual) {
        return -22;
    }
    phys = mm_alloc_pages(1);
    if (!phys || (phys & (NVME_PAGE_SIZE - 1u)) != 0 ||
        !paging_kernel_direct_map_range(phys, NVME_PAGE_SIZE)) {
        if (phys) {
            mm_free_pages(phys, 1);
        }
        return -12;
    }
    mapping = paging_kernel_direct_map(phys);
    if (!mapping) {
        mm_free_pages(phys, 1);
        return -12;
    }
    storage_memzero(mapping, NVME_PAGE_SIZE);
    *out_phys = phys;
    *out_virtual = (uint8_t *)mapping;
    return 0;
}

static void nvme_release_page(uint64_t *phys)
{
    if (phys && *phys) {
        mm_free_pages(*phys, 1);
    }
    if (phys) {
        *phys = 0;
    }
}

static void nvme_release_controller_memory(struct nvme_controller *controller)
{
    if (!controller) {
        return;
    }
    nvme_release_page(&controller->admin_sq_phys);
    nvme_release_page(&controller->admin_cq_phys);
    nvme_release_page(&controller->io_sq_phys);
    nvme_release_page(&controller->io_cq_phys);
    nvme_release_page(&controller->identify_phys);
    nvme_release_page(&controller->namespace_list_phys);
    nvme_release_page(&controller->io_buffer_phys);
    controller->admin_sq = 0;
    controller->admin_cq = 0;
    controller->io_sq = 0;
    controller->io_cq = 0;
    controller->identify_buffer = 0;
    controller->namespace_list = 0;
    controller->io_buffer = 0;
}

static int nvme_submit_locked(struct nvme_controller *controller, uint8_t admin,
                              struct nvme_command *command)
{
    struct nvme_command *sq;
    struct nvme_completion *cq;
    uint16_t *sq_tail;
    uint16_t *cq_head;
    uint8_t *cq_phase;
    uint16_t queue_id;
    uint16_t command_id;
    uint16_t status;

    if (!controller || !controller->ready || !command || !controller->queue_depth) {
        return -22;
    }
    if (admin) {
        sq = controller->admin_sq;
        cq = controller->admin_cq;
        sq_tail = &controller->admin_sq_tail;
        cq_head = &controller->admin_cq_head;
        cq_phase = &controller->cq_phase_admin;
        queue_id = 0;
    } else {
        sq = controller->io_sq;
        cq = controller->io_cq;
        sq_tail = &controller->io_sq_tail;
        cq_head = &controller->io_cq_head;
        cq_phase = &controller->cq_phase_io;
        queue_id = 1;
    }
    if (!sq || !cq) {
        return -22;
    }

    command_id = ++controller->next_command_id;
    if (command_id == 0) {
        command_id = ++controller->next_command_id;
    }
    command->cdw0 = (command->cdw0 & 0xffu) | ((uint32_t)command_id << 16);
    sq[*sq_tail] = *command;
    nvme_memory_barrier();
    *sq_tail = (uint16_t)((*sq_tail + 1u) % controller->queue_depth);
    nvme_write32(controller, nvme_doorbell_offset(controller, queue_id, 0), *sq_tail);

    for (uint32_t i = 0; i < NVME_WAIT_SPINS; ++i) {
        status = cq[*cq_head].status;
        if ((status & 1u) == *cq_phase) {
            if (cq[*cq_head].command_id != command_id) {
                console_printf("[ntclks] nvme unexpected completion cid=%u expected=%u qid=%u\n",
                               cq[*cq_head].command_id, command_id, queue_id);
                return -5;
            }
            nvme_memory_barrier();
            *cq_head = (uint16_t)((*cq_head + 1u) % controller->queue_depth);
            if (*cq_head == 0) {
                *cq_phase ^= 1u;
            }
            nvme_write32(controller, nvme_doorbell_offset(controller, queue_id, 1), *cq_head);
            return (status & 0xfffeu) == 0 ? 0 : -5;
        }
        if ((nvme_read32(controller, NVME_REG_CSTS) & NVME_CSTS_CFS) != 0) {
            return -5;
        }
        nvme_cpu_relax();
    }
    console_printf("[ntclks] nvme timeout pci=%u:%u.%u qid=%u opcode=0x%x\n",
                   controller->bus, controller->slot, controller->function,
                   queue_id, command->cdw0 & 0xffu);
    return -5;
}

static int nvme_submit(struct nvme_controller *controller, uint8_t admin,
                       struct nvme_command *command)
{
    int ret;
    if (!controller) {
        return -22;
    }
    kernel_spin_lock(&controller->lock);
    ret = nvme_submit_locked(controller, admin, command);
    kernel_spin_unlock(&controller->lock);
    return ret;
}

static int nvme_identify(struct nvme_controller *controller, uint32_t nsid,
                         uint8_t cns, uint8_t *buffer, uint64_t buffer_phys)
{
    struct nvme_command command;
    if (!controller || !buffer || !buffer_phys) {
        return -22;
    }
    storage_memzero(buffer, NVME_PAGE_SIZE);
    storage_memzero(&command, sizeof(command));
    command.cdw0 = NVME_ADMIN_IDENTIFY;
    command.nsid = nsid;
    command.prp1 = buffer_phys;
    command.cdw10 = cns;
    return nvme_submit(controller, 1, &command);
}

static void nvme_copy_model(char out[41], const uint8_t *model)
{
    uint32_t end = 40;
    if (!out) {
        return;
    }
    while (end && (model[end - 1u] == ' ' || model[end - 1u] == 0)) {
        --end;
    }
    for (uint32_t i = 0; i < end; ++i) {
        uint8_t c = model[i];
        out[i] = (c >= 0x20u && c < 0x7fu) ? (char)c : '?';
    }
    out[end] = 0;
}

static int nvme_create_io_queues(struct nvme_controller *controller)
{
    struct nvme_command command;
    uint32_t queue_size;
    int ret;
    if (!controller) {
        return -22;
    }
    queue_size = (uint32_t)(controller->queue_depth - 1u) << 16;

    storage_memzero(&command, sizeof(command));
    command.cdw0 = NVME_ADMIN_CREATE_IO_CQ;
    command.prp1 = controller->io_cq_phys;
    command.cdw10 = 1u | queue_size;
    command.cdw11 = 1u; /* physically contiguous queue, interrupt disabled */
    ret = nvme_submit(controller, 1, &command);
    if (ret < 0) {
        return ret;
    }

    storage_memzero(&command, sizeof(command));
    command.cdw0 = NVME_ADMIN_CREATE_IO_SQ;
    command.prp1 = controller->io_sq_phys;
    command.cdw10 = 1u | queue_size;
    command.cdw11 = 1u | (1u << 16); /* contiguous, attached to CQ 1 */
    return nvme_submit(controller, 1, &command);
}

static int nvme_initialize_controller(struct nvme_controller *controller)
{
    uint64_t cap;
    uint32_t max_entries;
    uint32_t mpsmin;
    uint32_t mpsmax;
    uint32_t cc;
    int ret;

    if (!controller || !controller->mmio) {
        return -22;
    }
    cap = nvme_read64(controller, NVME_REG_CAP);
    if ((cap & (1ULL << 37)) == 0) {
        console_printf("[ntclks] nvme pci=%u:%u.%u does not expose the NVM command set\n",
                       controller->bus, controller->slot, controller->function);
        return -95;
    }
    mpsmin = (uint32_t)((cap >> 48) & 0x0fu);
    mpsmax = (uint32_t)((cap >> 52) & 0x0fu);
    if (mpsmin != 0 || mpsmax < mpsmin) {
        console_printf("[ntclks] nvme pci=%u:%u.%u requires unsupported page size mps=%u..%u\n",
                       controller->bus, controller->slot, controller->function, mpsmin, mpsmax);
        return -95;
    }
    max_entries = (uint32_t)(cap & 0xffffu) + 1u;
    if (max_entries < 2u) {
        return -95;
    }
    controller->queue_depth = (uint16_t)min_u32(max_entries, NVME_QUEUE_DEPTH);
    controller->cap = cap;
    controller->doorbell_stride = 4u << ((cap >> 32) & 0x0fu);
    if (controller->doorbell_stride == 0 || controller->doorbell_stride > 1024u) {
        return -95;
    }

    cc = nvme_read32(controller, NVME_REG_CC);
    if (cc & NVME_CC_EN) {
        nvme_write32(controller, NVME_REG_CC, cc & ~NVME_CC_EN);
        if (nvme_wait_ready(controller, 0) < 0) {
            return -5;
        }
    }

    ret = nvme_allocate_page(&controller->admin_sq_phys, (uint8_t **)(void *)&controller->admin_sq);
    if (ret < 0) goto fail;
    ret = nvme_allocate_page(&controller->admin_cq_phys, (uint8_t **)(void *)&controller->admin_cq);
    if (ret < 0) goto fail;
    ret = nvme_allocate_page(&controller->io_sq_phys, (uint8_t **)(void *)&controller->io_sq);
    if (ret < 0) goto fail;
    ret = nvme_allocate_page(&controller->io_cq_phys, (uint8_t **)(void *)&controller->io_cq);
    if (ret < 0) goto fail;
    ret = nvme_allocate_page(&controller->identify_phys, &controller->identify_buffer);
    if (ret < 0) goto fail;
    ret = nvme_allocate_page(&controller->namespace_list_phys, &controller->namespace_list);
    if (ret < 0) goto fail;
    ret = nvme_allocate_page(&controller->io_buffer_phys, &controller->io_buffer);
    if (ret < 0) goto fail;

    controller->admin_sq_tail = 0;
    controller->admin_cq_head = 0;
    controller->io_sq_tail = 0;
    controller->io_cq_head = 0;
    controller->cq_phase_admin = 1;
    controller->cq_phase_io = 1;
    controller->next_command_id = 0;
    nvme_write32(controller, NVME_REG_AQA,
                 (uint32_t)(controller->queue_depth - 1u) |
                 ((uint32_t)(controller->queue_depth - 1u) << 16));
    nvme_write64(controller, NVME_REG_ASQ, controller->admin_sq_phys);
    nvme_write64(controller, NVME_REG_ACQ, controller->admin_cq_phys);
    nvme_memory_barrier();
    nvme_write32(controller, NVME_REG_CC,
                 NVME_CC_EN | (6u << NVME_CC_IOSQES_SHIFT) |
                 (4u << NVME_CC_IOCQES_SHIFT));
    if (nvme_wait_ready(controller, 1) < 0) {
        ret = -5;
        goto fail;
    }
    controller->ready = 1;
    ret = nvme_identify(controller, 0, NVME_IDENTIFY_CONTROLLER,
                        controller->identify_buffer, controller->identify_phys);
    if (ret < 0) goto fail;
    controller->namespace_count = storage_get_u32(controller->identify_buffer + 516u);
    nvme_copy_model(controller->model, controller->identify_buffer + 24u);
    if (!controller->model[0]) {
        storage_copy_text(controller->model, sizeof(controller->model), "NVMe Controller");
    }
    ret = nvme_create_io_queues(controller);
    if (ret < 0) goto fail;
    console_printf("[ntclks] NVMe controller ready pci=%u:%u.%u model=\"%s\" namespaces=%u qdepth=%u\n",
                   controller->bus, controller->slot, controller->function,
                   controller->model, controller->namespace_count, controller->queue_depth);
    return 0;

fail:
    controller->ready = 0;
    nvme_write32(controller, NVME_REG_CC, 0);
    (void)nvme_wait_ready(controller, 0);
    nvme_release_controller_memory(controller);
    return ret < 0 ? ret : -5;
}

static int nvme_readwrite(struct nvme_controller *controller, uint32_t nsid,
                          uint64_t lba, uint32_t sector_count, void *buffer,
                          uint8_t write)
{
    struct nvme_command command;
    uint64_t bytes;
    int ret;
    if (!controller || !controller->ready || !controller->io_buffer || !buffer ||
        nsid == 0 || sector_count == 0 || sector_count > NVME_MAX_SECTORS) {
        return -22;
    }
    bytes = (uint64_t)sector_count * SECTOR_SIZE;
    if (bytes > NVME_PAGE_SIZE || lba + sector_count < lba) {
        return -22;
    }
    kernel_spin_lock(&controller->lock);
    if (write) {
        storage_memcpy(controller->io_buffer, buffer, (size_t)bytes);
        nvme_memory_barrier();
    }
    storage_memzero(&command, sizeof(command));
    command.cdw0 = write ? NVME_IO_WRITE : NVME_IO_READ;
    command.nsid = nsid;
    command.prp1 = controller->io_buffer_phys;
    command.cdw10 = (uint32_t)lba;
    command.cdw11 = (uint32_t)(lba >> 32);
    command.cdw12 = sector_count - 1u;
    ret = nvme_submit_locked(controller, 0, &command);
    if (ret == 0 && !write) {
        nvme_memory_barrier();
        storage_memcpy(buffer, controller->io_buffer, (size_t)bytes);
    }
    kernel_spin_unlock(&controller->lock);
    return ret;
}

static int nvme_namespace_usable(struct nvme_controller *controller, uint32_t nsid,
                                 uint64_t *out_sectors)
{
    uint64_t nsze;
    uint32_t format;
    uint32_t offset;
    uint16_t metadata_size;
    uint8_t lbads;
    int ret;
    if (!controller || !out_sectors || !nsid) {
        return -22;
    }
    ret = nvme_identify(controller, nsid, NVME_IDENTIFY_NAMESPACE,
                        controller->identify_buffer, controller->identify_phys);
    if (ret < 0) {
        return ret;
    }
    nsze = (uint64_t)storage_get_u32(controller->identify_buffer) |
           ((uint64_t)storage_get_u32(controller->identify_buffer + 4u) << 32);
    format = controller->identify_buffer[26u] & 0x0fu;
    if (format > controller->identify_buffer[25u]) {
        return -5;
    }
    offset = 128u + format * 4u;
    metadata_size = (uint16_t)(controller->identify_buffer[offset] |
                               ((uint16_t)controller->identify_buffer[offset + 1u] << 8));
    lbads = controller->identify_buffer[offset + 2u];
    if (!nsze || lbads != 9u || metadata_size != 0u) {
        console_printf("[ntclks] NVMe namespace %u skipped: sectors=%llu lbads=%u metadata=%u\n",
                       nsid, (unsigned long long)nsze, lbads, metadata_size);
        return -95;
    }
    *out_sectors = nsze;
    return 0;
}

static void nvme_register_namespace(struct nvme_controller *controller, uint32_t nsid,
                                    uint8_t *root_ready)
{
    uint64_t sectors;
    struct install_disk_state *disk;
    if (!controller || !root_ready || !nsid ||
        g_install_disk_count >= STORAGE_MAX_INSTALL_DISKS ||
        nvme_namespace_usable(controller, nsid, &sectors) < 0) {
        return;
    }
    disk = &g_install_disks[g_install_disk_count++];
    storage_memzero(disk, sizeof(*disk));
    disk->present = true;
    disk->bus = controller->bus;
    disk->slot = controller->slot;
    disk->function = controller->function;
    disk->port = nsid <= 0xffu ? (uint8_t)nsid : 0xffu;
    disk->transport = STORAGE_TRANSPORT_NVME;
    disk->nvme = controller;
    disk->nvme_nsid = nsid;
    disk->sector_count = sectors;
    storage_copy_text(disk->device_model, sizeof(disk->device_model), controller->model);
    console_printf("[ntclks] NVMe namespace ready pci=%u:%u.%u nsid=%u sectors=%llu model=\"%s\"\n",
                   controller->bus, controller->slot, controller->function, nsid,
                   (unsigned long long)sectors, disk->device_model);
    if (!*root_ready && storage_try_mount_root_disk(disk) == 0) {
        *root_ready = 1;
    }
}

static void storage_scan_nvme_controller(uint8_t bus, uint8_t slot, uint8_t function,
                                         uint8_t *root_ready)
{
    struct nvme_controller *controller;
    uint32_t bar0;
    uint32_t bar1 = 0;
    uint64_t bar_phys;
    uint16_t pci_command;
    uint32_t start_nsid = 0;
    uint32_t namespace_ids_seen = 0;
    uint8_t finished = 0;
    uint8_t list_failed = 0;
    int ret;

    if (!root_ready || g_nvme_controller_count >= NVME_MAX_CONTROLLERS) {
        return;
    }
    bar0 = pci_config_read32(bus, slot, function, 0x10);
    if (!bar0 || (bar0 & 1u) != 0) {
        console_printf("[ntclks] NVMe pci=%u:%u.%u has no memory BAR0\n", bus, slot, function);
        return;
    }
    if (((bar0 >> 1) & 3u) == 2u) {
        bar1 = pci_config_read32(bus, slot, function, 0x14);
    } else if (((bar0 >> 1) & 3u) != 0u) {
        return;
    }
    bar_phys = ((uint64_t)bar1 << 32) | (bar0 & ~0x0fu);
    if (!bar_phys || !paging_kernel_direct_map_range(bar_phys, 0x4000u)) {
        return;
    }
    controller = &g_nvme_controllers[g_nvme_controller_count];
    storage_memzero(controller, sizeof(*controller));
    controller->bus = bus;
    controller->slot = slot;
    controller->function = function;
    controller->mmio = (volatile uint8_t *)paging_kernel_direct_map(bar_phys);
    if (!controller->mmio) {
        return;
    }
    kernel_spin_init(&controller->lock);
    pci_command = pci_config_read16(bus, slot, function, 0x04);
    pci_config_write16(bus, slot, function, 0x04, (uint16_t)(pci_command | 0x0006u));
    ret = nvme_initialize_controller(controller);
    if (ret < 0) {
        console_printf("[ntclks] NVMe controller init failed pci=%u:%u.%u ret=%d\n",
                       bus, slot, function, ret);
        storage_memzero(controller, sizeof(*controller));
        return;
    }
    controller->present = 1;
    ++g_nvme_controller_count;

    for (uint32_t page = 0; page < NVME_MAX_NAMESPACE_SCAN_PAGES && !finished; ++page) {
        ret = nvme_identify(controller, start_nsid, NVME_IDENTIFY_ACTIVE_NAMESPACE_LIST,
                            controller->namespace_list, controller->namespace_list_phys);
        if (ret < 0) {
            console_printf("[ntclks] NVMe active namespace list failed pci=%u:%u.%u ret=%d\n",
                           bus, slot, function, ret);
            list_failed = 1;
            break;
        }
        for (uint32_t index = 0; index < NVME_PAGE_SIZE / sizeof(uint32_t); ++index) {
            uint32_t nsid = storage_get_u32(controller->namespace_list + index * 4u);
            if (!nsid) {
                finished = 1;
                break;
            }
            if (nsid <= start_nsid || nsid > controller->namespace_count) {
                finished = 1;
                break;
            }
            start_nsid = nsid;
            ++namespace_ids_seen;
            nvme_register_namespace(controller, nsid, root_ready);
        }
        if (start_nsid >= controller->namespace_count) {
            finished = 1;
        }
    }
    if (list_failed && namespace_ids_seen == 0 && controller->namespace_count != 0) {
        uint32_t limit = min_u32(controller->namespace_count, 256u);
        console_printf("[ntclks] NVMe using bounded namespace scan pci=%u:%u.%u nn=%u\n",
                       bus, slot, function, controller->namespace_count);
        for (uint32_t nsid = 1; nsid <= limit; ++nsid) {
            nvme_register_namespace(controller, nsid, root_ready);
        }
    }
}
