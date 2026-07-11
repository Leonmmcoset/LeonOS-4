#include <leonos/driver.h>

#include <ntclks/mm.h>
#include <ntclks/pci.h>

static const struct leonos_driver_kernel_api *kernel_api;

static void module_console_notice(void)
{
    if (kernel_api && kernel_api->console_write) {
        kernel_api->console_write("[driver] e1000 state changed\n");
    }
}

#define mm_alloc_pages(pages) kernel_api->alloc_pages((pages))
#define mm_free_pages(address, pages) kernel_api->free_pages((address), (pages))
#define pci_config_read16(bus, slot, function, offset) \
    kernel_api->pci_read16((bus), (slot), (function), (offset))
#define pci_config_write16(bus, slot, function, offset, value) \
    kernel_api->pci_write16((bus), (slot), (function), (offset), (value))
#define pci_config_read32(bus, slot, function, offset) \
    kernel_api->pci_read32((bus), (slot), (function), (offset))
#define console_printf(...) module_console_notice()

#define E1000_VENDOR_INTEL 0x8086u
#define E1000_DEVICE_82540EM 0x100eu
#define E1000_DEVICE_82545EM 0x100fu
#define E1000_DEVICE_82543GC 0x1004u
#define E1000_DEVICE_82544GC 0x100du

#define E1000_REG_CTRL 0x0000u
#define E1000_REG_STATUS 0x0008u
#define E1000_REG_ICR 0x00c0u
#define E1000_REG_IMC 0x00d8u
#define E1000_REG_RCTL 0x0100u
#define E1000_REG_TCTL 0x0400u
#define E1000_REG_TIPG 0x0410u
#define E1000_REG_RDBAL 0x2800u
#define E1000_REG_RDBAH 0x2804u
#define E1000_REG_RDLEN 0x2808u
#define E1000_REG_RDH 0x2810u
#define E1000_REG_RDT 0x2818u
#define E1000_REG_TDBAL 0x3800u
#define E1000_REG_TDBAH 0x3804u
#define E1000_REG_TDLEN 0x3808u
#define E1000_REG_TDH 0x3810u
#define E1000_REG_TDT 0x3818u
#define E1000_REG_MTA 0x5200u
#define E1000_REG_RAL 0x5400u
#define E1000_REG_RAH 0x5404u

#define E1000_RCTL_EN (1u << 1)
#define E1000_RCTL_UPE (1u << 3)
#define E1000_RCTL_MPE (1u << 4)
#define E1000_RCTL_BAM (1u << 15)
#define E1000_RCTL_BSIZE_2048 (0u << 16)
#define E1000_RCTL_SECRC (1u << 26)
#define E1000_TCTL_EN (1u << 1)
#define E1000_TCTL_PSP (1u << 3)
#define E1000_RAH_AV (1u << 31)
#define E1000_RX_STATUS_DD 0x01u
#define E1000_RX_STATUS_EOP 0x02u
#define E1000_TX_STATUS_DD 0x01u
#define E1000_TX_CMD_EOP 0x01u
#define E1000_TX_CMD_IFCS 0x02u
#define E1000_TX_CMD_RS 0x08u

#define PCI_COMMAND_IO 0x0001u
#define PCI_COMMAND_MEMORY 0x0002u
#define PCI_COMMAND_BUS_MASTER 0x0004u

#define E1000_RX_COUNT 16u
#define E1000_TX_COUNT 16u
#define E1000_BUFFER_SIZE 2048u
#define E1000_TX_WAIT_SPINS 1000000u

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

struct module_e1000_info {
    uint32_t present;
    uint32_t active;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t mac[6];
};

struct e1000_state {
    uint32_t present;
    uint32_t active;
    struct pci_device pci;
    uintptr_t mmio_base;
    volatile struct e1000_rx_desc *rx;
    volatile struct e1000_tx_desc *tx;
    uint64_t rx_phys;
    uint64_t tx_phys;
    uint8_t *rx_buf[E1000_RX_COUNT];
    uint8_t *tx_buf[E1000_TX_COUNT];
    uint32_t rx_index;
    uint32_t tx_tail;
    uint8_t mac[6];
};

static struct e1000_state g_e1000;

static void e1000_cpu_relax(void)
{
    __asm__ volatile("pause");
}

static void e1000_memzero(void *dst, uint32_t len)
{
    uint8_t *p = (uint8_t *)dst;
    while (len--) {
        *p++ = 0;
    }
}

static void e1000_memcpy(void *dst, const void *src, uint32_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (len--) {
        *d++ = *s++;
    }
}

static int e1000_find(struct pci_device *out)
{
    static const uint16_t supported[] = {
        E1000_DEVICE_82540EM,
        E1000_DEVICE_82545EM,
        E1000_DEVICE_82543GC,
        E1000_DEVICE_82544GC,
    };
    for (uint32_t index = 0; index < sizeof(supported) / sizeof(supported[0]); ++index) {
        struct leonos_driver_pci_device found;
        if (kernel_api->pci_find(E1000_VENDOR_INTEL, supported[index], &found) == 0) {
            if (out) {
                *out = (struct pci_device){
                    .bus = found.bus,
                    .slot = found.slot,
                    .function = found.function,
                    .vendor_id = found.vendor_id,
                    .device_id = found.device_id,
                    .class_code = found.class_code,
                    .subclass = found.subclass,
                    .prog_if = found.prog_if,
                    .revision = 0,
                };
            }
            return 0;
        }
    }
    return -1;
}

static uint32_t e1000_reg_read(uint32_t offset)
{
    return *(volatile uint32_t *)(g_e1000.mmio_base + offset);
}

static void e1000_reg_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(g_e1000.mmio_base + offset) = value;
}

static uint64_t e1000_bar0_mmio(const struct pci_device *dev)
{
    uint32_t bar0 = pci_config_read32(dev->bus, dev->slot, dev->function, 0x10);
    uint64_t addr;
    if (bar0 & 0x1u) {
        return 0;
    }
    addr = (uint64_t)(bar0 & ~0x0fu);
    if ((bar0 & 0x6u) == 0x4u) {
        uint32_t high = pci_config_read32(dev->bus, dev->slot, dev->function, 0x14);
        addr |= (uint64_t)high << 32;
    }
    if (addr > 0xffffffffULL) {
        return 0;
    }
    return addr;
}

static void e1000_read_mac(void)
{
    uint32_t ral = e1000_reg_read(E1000_REG_RAL);
    uint32_t rah = e1000_reg_read(E1000_REG_RAH);
    g_e1000.mac[0] = (uint8_t)ral;
    g_e1000.mac[1] = (uint8_t)(ral >> 8);
    g_e1000.mac[2] = (uint8_t)(ral >> 16);
    g_e1000.mac[3] = (uint8_t)(ral >> 24);
    g_e1000.mac[4] = (uint8_t)rah;
    g_e1000.mac[5] = (uint8_t)(rah >> 8);
    if ((g_e1000.mac[0] | g_e1000.mac[1] | g_e1000.mac[2] |
         g_e1000.mac[3] | g_e1000.mac[4] | g_e1000.mac[5]) == 0) {
        g_e1000.mac[0] = 0x52;
        g_e1000.mac[1] = 0x54;
        g_e1000.mac[2] = 0x00;
        g_e1000.mac[3] = 0x12;
        g_e1000.mac[4] = 0x34;
        g_e1000.mac[5] = 0x56;
    }
    ral = (uint32_t)g_e1000.mac[0] |
          ((uint32_t)g_e1000.mac[1] << 8) |
          ((uint32_t)g_e1000.mac[2] << 16) |
          ((uint32_t)g_e1000.mac[3] << 24);
    rah = (uint32_t)g_e1000.mac[4] |
          ((uint32_t)g_e1000.mac[5] << 8) |
          E1000_RAH_AV;
    e1000_reg_write(E1000_REG_RAL, ral);
    e1000_reg_write(E1000_REG_RAH, rah);
}

static int e1000_alloc_rings(void)
{
    g_e1000.rx_phys = mm_alloc_pages(1);
    g_e1000.tx_phys = mm_alloc_pages(1);
    if (!g_e1000.rx_phys || !g_e1000.tx_phys) {
        return -1;
    }
    g_e1000.rx = (volatile struct e1000_rx_desc *)(uintptr_t)g_e1000.rx_phys;
    g_e1000.tx = (volatile struct e1000_tx_desc *)(uintptr_t)g_e1000.tx_phys;
    e1000_memzero((void *)g_e1000.rx, 4096);
    e1000_memzero((void *)g_e1000.tx, 4096);

    for (uint32_t i = 0; i < E1000_RX_COUNT; ++i) {
        uint64_t phys = mm_alloc_pages(1);
        if (!phys) {
            return -1;
        }
        g_e1000.rx_buf[i] = (uint8_t *)(uintptr_t)phys;
        e1000_memzero(g_e1000.rx_buf[i], E1000_BUFFER_SIZE);
        g_e1000.rx[i].addr = phys;
        g_e1000.rx[i].status = 0;
    }
    for (uint32_t i = 0; i < E1000_TX_COUNT; ++i) {
        uint64_t phys = mm_alloc_pages(1);
        if (!phys) {
            return -1;
        }
        g_e1000.tx_buf[i] = (uint8_t *)(uintptr_t)phys;
        e1000_memzero(g_e1000.tx_buf[i], E1000_BUFFER_SIZE);
        g_e1000.tx[i].addr = phys;
        g_e1000.tx[i].status = E1000_TX_STATUS_DD;
    }
    return 0;
}

static void e1000_configure_rx_tx(void)
{
    e1000_reg_write(E1000_REG_RCTL, 0);
    e1000_reg_write(E1000_REG_TCTL, 0);
    e1000_reg_write(E1000_REG_IMC, 0xffffffffu);
    (void)e1000_reg_read(E1000_REG_ICR);

    for (uint32_t i = 0; i < 128; ++i) {
        e1000_reg_write(E1000_REG_MTA + i * 4u, 0);
    }

    e1000_reg_write(E1000_REG_RDBAL, (uint32_t)g_e1000.rx_phys);
    e1000_reg_write(E1000_REG_RDBAH, (uint32_t)(g_e1000.rx_phys >> 32));
    e1000_reg_write(E1000_REG_RDLEN, E1000_RX_COUNT * sizeof(struct e1000_rx_desc));
    e1000_reg_write(E1000_REG_RDH, 0);
    e1000_reg_write(E1000_REG_RDT, E1000_RX_COUNT - 1u);
    g_e1000.rx_index = 0;

    e1000_reg_write(E1000_REG_TDBAL, (uint32_t)g_e1000.tx_phys);
    e1000_reg_write(E1000_REG_TDBAH, (uint32_t)(g_e1000.tx_phys >> 32));
    e1000_reg_write(E1000_REG_TDLEN, E1000_TX_COUNT * sizeof(struct e1000_tx_desc));
    e1000_reg_write(E1000_REG_TDH, 0);
    e1000_reg_write(E1000_REG_TDT, 0);
    g_e1000.tx_tail = 0;

    e1000_reg_write(E1000_REG_TIPG, 10u | (8u << 10) | (6u << 20));
    e1000_reg_write(E1000_REG_TCTL,
                    E1000_TCTL_EN | E1000_TCTL_PSP |
                        (0x10u << 4) | (0x40u << 12));
    e1000_reg_write(E1000_REG_RCTL,
                    E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE |
                        E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048 |
                        E1000_RCTL_SECRC);
}

static void e1000_hardware_init(void)
{
    struct pci_device dev;
    uint64_t mmio;
    uint16_t command;
    e1000_memzero(&g_e1000, sizeof(g_e1000));

    if (e1000_find(&dev) < 0) {
        console_printf("[ntclks] e1000 not found\n");
        return;
    }
    g_e1000.present = 1;
    g_e1000.pci = dev;
    mmio = e1000_bar0_mmio(&dev);
    if (!mmio) {
        console_printf("[ntclks] e1000 present but BAR0 MMIO is unusable\n");
        return;
    }
    g_e1000.mmio_base = (uintptr_t)mmio;

    command = pci_config_read16(dev.bus, dev.slot, dev.function, 0x04);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    command &= (uint16_t)~PCI_COMMAND_IO;
    pci_config_write16(dev.bus, dev.slot, dev.function, 0x04, command);

    e1000_read_mac();
    if (e1000_alloc_rings() < 0) {
        console_printf("[ntclks] e1000 ring allocation failed\n");
        return;
    }
    e1000_configure_rx_tx();
    g_e1000.active = 1;
    console_printf("[ntclks] e1000 ready pci=%u:%u.%u device=0x%x mmio=%p status=0x%x mac=%x:%x:%x:%x:%x:%x\n",
                   dev.bus, dev.slot, dev.function, dev.device_id,
                   (void *)g_e1000.mmio_base, e1000_reg_read(E1000_REG_STATUS),
                   g_e1000.mac[0], g_e1000.mac[1], g_e1000.mac[2],
                   g_e1000.mac[3], g_e1000.mac[4], g_e1000.mac[5]);
}

static int e1000_is_ready(void)
{
    return g_e1000.active != 0;
}

static const uint8_t *e1000_mac(void)
{
    return g_e1000.mac;
}

static int e1000_send(const void *frame, uint32_t len)
{
    uint32_t index;
    uint32_t next;
    if (!g_e1000.active || !frame || len == 0 || len > E1000_BUFFER_SIZE) {
        return -1;
    }
    index = g_e1000.tx_tail;
    for (uint32_t spin = 0; spin < E1000_TX_WAIT_SPINS; ++spin) {
        if (g_e1000.tx[index].status & E1000_TX_STATUS_DD) {
            break;
        }
        if (spin + 1u == E1000_TX_WAIT_SPINS) {
            return -1;
        }
        e1000_cpu_relax();
    }
    e1000_memcpy(g_e1000.tx_buf[index], frame, len);
    g_e1000.tx[index].length = (uint16_t)len;
    g_e1000.tx[index].cso = 0;
    g_e1000.tx[index].cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    g_e1000.tx[index].status = 0;
    g_e1000.tx[index].css = 0;
    g_e1000.tx[index].special = 0;
    next = (index + 1u) % E1000_TX_COUNT;
    g_e1000.tx_tail = next;
    e1000_reg_write(E1000_REG_TDT, next);
    for (uint32_t spin = 0; spin < E1000_TX_WAIT_SPINS; ++spin) {
        if (g_e1000.tx[index].status & E1000_TX_STATUS_DD) {
            return 0;
        }
        e1000_cpu_relax();
    }
    return -1;
}

static int e1000_poll(void *frame, uint32_t capacity, uint32_t *out_len)
{
    uint32_t index;
    uint32_t len;
    uint8_t status;
    if (out_len) {
        *out_len = 0;
    }
    if (!g_e1000.active || !frame || !capacity) {
        return -1;
    }
    index = g_e1000.rx_index;
    status = g_e1000.rx[index].status;
    if ((status & E1000_RX_STATUS_DD) == 0) {
        return 0;
    }
    len = g_e1000.rx[index].length;
    if ((status & E1000_RX_STATUS_EOP) == 0 || g_e1000.rx[index].errors) {
        len = 0;
    }
    if (len > capacity) {
        len = capacity;
    }
    if (len) {
        e1000_memcpy(frame, g_e1000.rx_buf[index], len);
    }
    g_e1000.rx[index].length = 0;
    g_e1000.rx[index].checksum = 0;
    g_e1000.rx[index].status = 0;
    g_e1000.rx[index].errors = 0;
    g_e1000.rx[index].special = 0;
    e1000_reg_write(E1000_REG_RDT, index);
    g_e1000.rx_index = (index + 1u) % E1000_RX_COUNT;
    if (out_len) {
        *out_len = len;
    }
    return len ? 1 : 0;
}

static void e1000_get_info(struct module_e1000_info *info)
{
    if (!info) {
        return;
    }
    *info = (struct module_e1000_info){
        .present = g_e1000.present,
        .active = g_e1000.active,
        .vendor_id = g_e1000.pci.vendor_id,
        .device_id = g_e1000.pci.device_id,
        .bus = g_e1000.pci.bus,
        .slot = g_e1000.pci.slot,
        .function = g_e1000.pci.function,
    };
    for (uint32_t i = 0; i < 6; ++i) {
        info->mac[i] = g_e1000.mac[i];
    }
}

static void e1000_driver_get_info(struct leonos_driver_e1000_info *out)
{
    struct module_e1000_info info;
    if (!out) {
        return;
    }
    e1000_get_info(&info);
    *out = (struct leonos_driver_e1000_info){
        .present = info.present,
        .active = info.active,
        .vendor_id = info.vendor_id,
        .device_id = info.device_id,
        .bus = info.bus,
        .slot = info.slot,
        .function = info.function,
        .reserved = 0,
        .reserved2 = {0, 0},
    };
    for (uint32_t index = 0; index < 6; ++index) {
        out->mac[index] = info.mac[index];
    }
}

static void e1000_release_rings(void)
{
    for (uint32_t index = 0; index < E1000_RX_COUNT; ++index) {
        if (g_e1000.rx_buf[index]) {
            mm_free_pages((uint64_t)(uintptr_t)g_e1000.rx_buf[index], 1);
        }
    }
    for (uint32_t index = 0; index < E1000_TX_COUNT; ++index) {
        if (g_e1000.tx_buf[index]) {
            mm_free_pages((uint64_t)(uintptr_t)g_e1000.tx_buf[index], 1);
        }
    }
    if (g_e1000.rx_phys) {
        mm_free_pages(g_e1000.rx_phys, 1);
    }
    if (g_e1000.tx_phys) {
        mm_free_pages(g_e1000.tx_phys, 1);
    }
    e1000_memzero(&g_e1000, sizeof(g_e1000));
}

static int e1000_driver_init(const struct leonos_driver_kernel_api *api)
{
    static const struct leonos_driver_e1000_ops ops = {
        .is_ready = e1000_is_ready,
        .mac = e1000_mac,
        .send = e1000_send,
        .poll = e1000_poll,
        .get_info = e1000_driver_get_info,
    };
    if (!api || api->abi_version != LEONOS_DRIVER_ABI_VERSION ||
        api->struct_size < sizeof(*api)) {
        return -22;
    }
    kernel_api = api;
    e1000_hardware_init();
    if (!e1000_is_ready()) {
        return -19;
    }
    return kernel_api->register_e1000(&ops);
}

static void e1000_driver_fini(void)
{
    if (g_e1000.mmio_base) {
        e1000_reg_write(E1000_REG_RCTL, 0);
        e1000_reg_write(E1000_REG_TCTL, 0);
        e1000_reg_write(E1000_REG_IMC, 0xffffffffu);
    }
    e1000_release_rings();
}

struct leonos_driver_module leonos_driver_module = {
    .magic = LEONOS_DRIVER_MODULE_MAGIC,
    .abi_version = LEONOS_DRIVER_ABI_VERSION,
    .struct_size = sizeof(struct leonos_driver_module),
    .kind = LEONOS_DRIVER_KIND_NETWORK,
    .name = "e1000",
    .version = 1U,
    .reserved = 0,
    .init = e1000_driver_init,
    .fini = e1000_driver_fini,
};
