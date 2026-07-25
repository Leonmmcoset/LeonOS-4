#include <leonos/driver.h>

#define AC97_VENDOR_INTEL 0x8086U
#define AC97_DEVICE_ICH 0x2415U

#define PCI_COMMAND_IO 0x0001U
#define PCI_COMMAND_BUS_MASTER 0x0004U

#define AC97_NAM_MASTER_VOLUME 0x02U
#define AC97_NAM_PCM_OUT_VOLUME 0x18U
#define AC97_NAM_EXT_AUDIO_ID 0x28U
#define AC97_NAM_EXT_AUDIO_CTRL 0x2aU
#define AC97_NAM_PCM_FRONT_DAC_RATE 0x2cU
#define AC97_EXT_AUDIO_VRA 0x0001U

#define AC97_PO_BDBAR 0x10U
#define AC97_PO_LVI 0x15U
#define AC97_PO_SR 0x16U
#define AC97_PO_CR 0x1bU

#define AC97_PO_SR_DCH 0x0001U
#define AC97_PO_SR_LVBCI 0x0004U
#define AC97_PO_SR_BCIS 0x0008U
#define AC97_PO_SR_FIFOE 0x0010U
#define AC97_PO_SR_CLEAR 0x001cU
#define AC97_PO_CR_RUN 0x01U
#define AC97_PO_CR_RESET 0x02U

#define AC97_BDL_COUNT 16U
#define AC97_BUFFER_BYTES 4096U
#define AC97_WAIT_SPINS 10000000U

struct ac97_buffer_desc {
    uint32_t address;
    uint16_t samples;
    uint16_t control;
} __attribute__((packed));

struct ac97_state {
    uint32_t present;
    uint32_t active;
    struct leonos_driver_pci_device pci;
    uint16_t mixer_port;
    uint16_t bus_master_port;
    uint64_t bdl_phys;
    uint64_t buffers_phys;
    struct ac97_buffer_desc *bdl;
    uint8_t *buffers;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t queued_bytes;
    uint32_t underruns;
    uint8_t variable_rate;
};

static const struct leonos_driver_kernel_api *kernel_api;
static struct ac97_state ac97;

static void ac97_copy(void *dst, const void *src, uint32_t length)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    while (length--) {
        *out++ = *in++;
    }
}

static void ac97_zero(void *dst, uint32_t length)
{
    uint8_t *out = (uint8_t *)dst;
    while (length--) {
        *out++ = 0;
    }
}

static void ac97_pause(void)
{
    __asm__ volatile("pause");
}

static uint16_t ac97_read16(uint16_t port)
{
    return (uint16_t)kernel_api->inb(port) |
           ((uint16_t)kernel_api->inb((uint16_t)(port + 1U)) << 8);
}

static void ac97_write16(uint16_t port, uint16_t value)
{
    kernel_api->outb(port, (uint8_t)value);
    kernel_api->outb((uint16_t)(port + 1U), (uint8_t)(value >> 8));
}

static uint16_t ac97_pci_io_bar(uint8_t offset)
{
    uint32_t bar = kernel_api->pci_read32(ac97.pci.bus, ac97.pci.slot,
                                           ac97.pci.function, offset);
    uint32_t port;
    if (!(bar & 1U)) {
        return 0;
    }
    port = bar & ~3U;
    return port > 0xffffU ? 0 : (uint16_t)port;
}

static void ac97_stop(void)
{
    if (!ac97.bus_master_port) {
        return;
    }
    kernel_api->outb((uint16_t)(ac97.bus_master_port + AC97_PO_CR), 0);
    kernel_api->outb((uint16_t)(ac97.bus_master_port + AC97_PO_CR),
                     AC97_PO_CR_RESET);
    kernel_api->outb((uint16_t)(ac97.bus_master_port + AC97_PO_CR), 0);
    ac97_write16((uint16_t)(ac97.bus_master_port + AC97_PO_SR),
                 AC97_PO_SR_CLEAR);
    ac97.queued_bytes = 0;
}

static int ac97_alloc_dma(void)
{
    ac97.bdl_phys = kernel_api->alloc_pages(1);
    ac97.buffers_phys = kernel_api->alloc_pages(AC97_BDL_COUNT);
    if (!ac97.bdl_phys || !ac97.buffers_phys) {
        return -12;
    }
    ac97.bdl = (struct ac97_buffer_desc *)(uintptr_t)ac97.bdl_phys;
    ac97.buffers = (uint8_t *)(uintptr_t)ac97.buffers_phys;
    ac97_zero(ac97.bdl, 4096U);
    ac97_zero(ac97.buffers, AC97_BDL_COUNT * AC97_BUFFER_BYTES);
    return 0;
}

static void ac97_free_dma(void)
{
    if (ac97.buffers_phys) {
        kernel_api->free_pages(ac97.buffers_phys, AC97_BDL_COUNT);
    }
    if (ac97.bdl_phys) {
        kernel_api->free_pages(ac97.bdl_phys, 1);
    }
    ac97.bdl_phys = 0;
    ac97.buffers_phys = 0;
    ac97.bdl = 0;
    ac97.buffers = 0;
}

static int ac97_is_ready(void)
{
    return ac97.active != 0;
}

static __attribute__((noinline)) int ac97_apply_format(
    const struct leonos_audio_format *format)
{
    ac97.sample_rate = format->sample_rate;
    ac97.channels = format->channels;
    ac97.bits_per_sample = format->bits_per_sample;
    return 0;
}

static int ac97_configure(const struct leonos_audio_format *format)
{
    uint16_t ext_id;
    uint16_t ext_ctrl;
    if (!ac97_is_ready() || !format || format->channels != 2U ||
        format->bits_per_sample != 16U || format->sample_rate < 8000U ||
        format->sample_rate > 48000U) {
        return -22;
    }
    if (format->sample_rate != 48000U) {
        ext_id = ac97_read16((uint16_t)(ac97.mixer_port + AC97_NAM_EXT_AUDIO_ID));
        if (!(ext_id & AC97_EXT_AUDIO_VRA)) {
            return -95;
        }
        ext_ctrl = ac97_read16((uint16_t)(ac97.mixer_port + AC97_NAM_EXT_AUDIO_CTRL));
        ac97_write16((uint16_t)(ac97.mixer_port + AC97_NAM_EXT_AUDIO_CTRL),
                     (uint16_t)(ext_ctrl | AC97_EXT_AUDIO_VRA));
        ac97_write16((uint16_t)(ac97.mixer_port + AC97_NAM_PCM_FRONT_DAC_RATE),
                     (uint16_t)format->sample_rate);
        ac97.variable_rate = 1;
    }
    return ac97_apply_format(format);
}

static int ac97_play_block(const uint8_t *data, uint32_t length)
{
    uint32_t descriptor_count = 0;
    uint32_t offset = 0;
    uint32_t spins;
    uint32_t started = 0;
    if (!data || !length || !ac97.bdl || !ac97.buffers ||
        length > AC97_BDL_COUNT * AC97_BUFFER_BYTES || (length & 3U)) {
        return -22;
    }
    ac97_stop();
    while (offset < length) {
        uint32_t chunk = length - offset;
        uint8_t *buffer;
        if (chunk > AC97_BUFFER_BYTES) {
            chunk = AC97_BUFFER_BYTES;
        }
        chunk &= ~3U;
        if (!chunk || descriptor_count >= AC97_BDL_COUNT) {
            return -22;
        }
        buffer = ac97.buffers + descriptor_count * AC97_BUFFER_BYTES;
        ac97_copy(buffer, data + offset, chunk);
        ac97.bdl[descriptor_count] = (struct ac97_buffer_desc){
            .address = (uint32_t)(ac97.buffers_phys +
                                  descriptor_count * AC97_BUFFER_BYTES),
            .samples = (uint16_t)(chunk / 2U),
            .control = 0x8000U,
        };
        offset += chunk;
        ++descriptor_count;
    }
    kernel_api->outl((uint16_t)(ac97.bus_master_port + AC97_PO_BDBAR),
                     (uint32_t)ac97.bdl_phys);
    ac97_write16((uint16_t)(ac97.bus_master_port + AC97_PO_SR),
                 AC97_PO_SR_CLEAR);
    kernel_api->outb((uint16_t)(ac97.bus_master_port + AC97_PO_LVI),
                     (uint8_t)(descriptor_count - 1U));
    ac97.queued_bytes = length;
    kernel_api->outb((uint16_t)(ac97.bus_master_port + AC97_PO_CR),
                     AC97_PO_CR_RUN);
    for (spins = 0; spins < AC97_WAIT_SPINS; ++spins) {
        uint16_t status = ac97_read16((uint16_t)(ac97.bus_master_port + AC97_PO_SR));
        if (!(status & AC97_PO_SR_DCH)) {
            started = 1;
        }
        if (status & (AC97_PO_SR_BCIS | AC97_PO_SR_LVBCI)) {
            ac97_write16((uint16_t)(ac97.bus_master_port + AC97_PO_SR),
                         (uint16_t)(status & (AC97_PO_SR_BCIS | AC97_PO_SR_LVBCI |
                                              AC97_PO_SR_FIFOE)));
        }
        if (started && (status & AC97_PO_SR_DCH)) {
            ac97_stop();
            return 0;
        }
        ac97_pause();
    }
    ac97_stop();
    ++ac97.underruns;
    return -110;
}

static long ac97_write(const void *data, uint32_t length, uint32_t *out_status)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t written = 0;
    if (out_status) {
        *out_status = LEONOS_AUDIO_STATUS_PLAYBACK_FAILED;
    }
    if (!ac97_is_ready() || (!data && length) || (length & 3U)) {
        return -22;
    }
    while (written < length) {
        uint32_t chunk = length - written;
        if (chunk > AC97_BDL_COUNT * AC97_BUFFER_BYTES) {
            chunk = AC97_BDL_COUNT * AC97_BUFFER_BYTES;
        }
        chunk &= ~3U;
        if (ac97_play_block(bytes + written, chunk) < 0) {
            return written ? (long)written : -5;
        }
        written += chunk;
    }
    if (out_status) {
        *out_status = LEONOS_AUDIO_STATUS_OK;
    }
    return (long)written;
}

static void ac97_get_state(struct leonos_audio_state *out)
{
    if (!out) {
        return;
    }
    *out = (struct leonos_audio_state){
        .present = ac97.present,
        .active = ac97.active,
        .sample_rate = ac97.sample_rate,
        .channels = ac97.channels,
        .bits_per_sample = ac97.bits_per_sample,
        .queued_bytes = ac97.queued_bytes,
        .underruns = ac97.underruns,
        .vendor_id = ac97.pci.vendor_id,
        .device_id = ac97.pci.device_id,
        .bus = ac97.pci.bus,
        .slot = ac97.pci.slot,
        .function = ac97.pci.function,
    };
}

static int ac97_hardware_init(void)
{
    uint16_t command;
    struct leonos_audio_format default_format = {
        .sample_rate = 48000U,
        .channels = 2U,
        .bits_per_sample = 16U,
        .flags = 0,
    };
    ac97_zero(&ac97, sizeof(ac97));
    if (kernel_api->pci_find(AC97_VENDOR_INTEL, AC97_DEVICE_ICH, &ac97.pci) < 0) {
        return -19;
    }
    ac97.present = 1;
    ac97.mixer_port = ac97_pci_io_bar(0x10U);
    ac97.bus_master_port = ac97_pci_io_bar(0x14U);
    if (!ac97.mixer_port || !ac97.bus_master_port) {
        return -19;
    }
    command = kernel_api->pci_read16(ac97.pci.bus, ac97.pci.slot,
                                      ac97.pci.function, 0x04U);
    command |= PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER;
    kernel_api->pci_write16(ac97.pci.bus, ac97.pci.slot, ac97.pci.function,
                            0x04U, command);
    ac97_write16(ac97.mixer_port, 0);
    ac97_write16((uint16_t)(ac97.mixer_port + AC97_NAM_MASTER_VOLUME), 0);
    ac97_write16((uint16_t)(ac97.mixer_port + AC97_NAM_PCM_OUT_VOLUME), 0);
    if (ac97_alloc_dma() < 0) {
        return -12;
    }
    ac97.active = 1;
    ac97_stop();
    if (ac97_configure(&default_format) < 0) {
        ac97_free_dma();
        ac97.active = 0;
        return -95;
    }
    kernel_api->console_write("[driver] ac97 ready\n");
    return 0;
}

static int ac97_driver_init(const struct leonos_driver_kernel_api *api)
{
    static const struct leonos_driver_audio_ops ops = {
        .is_ready = ac97_is_ready,
        .configure = ac97_configure,
        .write = ac97_write,
        .get_state = ac97_get_state,
    };
    if (!api || api->abi_version != LEONOS_DRIVER_ABI_VERSION ||
        api->struct_size < sizeof(*api)) {
        return -22;
    }
    kernel_api = api;
    if (ac97_hardware_init() < 0) {
        return -19;
    }
    return kernel_api->register_audio(&ops);
}

static void ac97_driver_fini(void)
{
    if (kernel_api) {
        ac97_stop();
        ac97_free_dma();
    }
    ac97_zero(&ac97, sizeof(ac97));
}

const struct leonos_driver_module leonos_driver_module = {
    .magic = LEONOS_DRIVER_MODULE_MAGIC,
    .abi_version = LEONOS_DRIVER_ABI_VERSION,
    .struct_size = sizeof(struct leonos_driver_module),
    .kind = LEONOS_DRIVER_KIND_AUDIO,
    .name = "ac97",
    .version = 1U,
    .reserved = 0,
    .init = ac97_driver_init,
    .fini = ac97_driver_fini,
};
