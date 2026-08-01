#include <leonos/driver.h>

#define ES1371_VENDOR_ENSONIQ 0x1274U
#define ES1371_DEVICE_AUDIOPCI 0x1371U

#define PCI_COMMAND_IO 0x0001U
#define PCI_COMMAND_BUS_MASTER 0x0004U

#define ES_REG_CONTROL 0x00U
#define ES_REG_MEM_PAGE 0x0cU
#define ES_REG_SRC 0x10U
#define ES_REG_CODEC 0x14U
#define ES_REG_LEGACY 0x18U
#define ES_REG_SERIAL 0x20U
#define ES_REG_DAC1_COUNT 0x24U
#define ES_REG_DAC1_FRAME 0x30U
#define ES_REG_DAC1_SIZE 0x34U

#define ES_PAGE_DAC 0x0cU

#define ES_CONTROL_SYNC_RESET 0x00004000U
#define ES_CONTROL_DAC1_ENABLE 0x00000040U

#define ES_SERIAL_DAC1_16BIT_STEREO 0x00000003U

#define ES_SRC_DISABLE 0x00400000U
#define ES_SRC_DISABLE_DAC1 0x00200000U
#define ES_SRC_DISABLE_DAC2 0x00100000U
#define ES_SRC_DISABLE_ADC 0x00080000U
#define ES_SRC_BUSY 0x00800000U
#define ES_SRC_WRITE 0x01000000U
#define ES_SRC_STATE_MASK (ES_SRC_DISABLE | ES_SRC_DISABLE_DAC1 | \
                           ES_SRC_DISABLE_DAC2 | ES_SRC_DISABLE_ADC)

#define ES_CODEC_WRITE_IN_PROGRESS 0x40000000U
#define ES_CODEC_SAFE_STATE 0x00010000U

#define ES_SRC_DAC1 0x70U
#define ES_SRC_DAC2 0x74U
#define ES_SRC_ADC 0x78U
#define ES_SRC_VOLUME_ADC 0x6cU
#define ES_SRC_VOLUME_DAC1 0x7cU
#define ES_SRC_VOLUME_DAC2 0x7eU
#define ES_SRC_TRUNC_N 0x00U
#define ES_SRC_INT_REGS 0x01U
#define ES_SRC_VFREQ_FRAC 0x03U

#define ES_AC97_MASTER_VOLUME 0x02U
#define ES_AC97_PCM_VOLUME 0x18U

#define ES1371_DMA_BYTES (4U * 1024U)
#define ES1371_DMA_PAGES (ES1371_DMA_BYTES / 4096U)
#define ES1371_WAIT_SPINS 10000000U
#define ES1371_PLAYBACK_SLACK_TICKS 2ULL

struct es1371_state {
    uint32_t present;
    uint32_t active;
    struct leonos_driver_pci_device pci;
    uint16_t io_port;
    uint64_t dma_phys;
    uint8_t *dma_buffer;
    uint32_t control;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t queued_bytes;
    uint32_t underruns;
};

static const struct leonos_driver_kernel_api *kernel_api;
static struct es1371_state es1371;

static void es1371_copy(void *dst, const void *src, uint32_t length)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    while (length--) {
        *out++ = *in++;
    }
}

static void es1371_zero(void *dst, uint32_t length)
{
    uint8_t *out = (uint8_t *)dst;
    while (length--) {
        *out++ = 0;
    }
}

static void es1371_pause(void)
{
    __asm__ volatile("pause");
}

static uint64_t es1371_ticks(void)
{
    return kernel_api && kernel_api->ticks ? kernel_api->ticks() : 0ULL;
}

static void es1371_wait_tick_or_pause(void)
{
    if (kernel_api && kernel_api->sleep_ms) {
        kernel_api->sleep_ms(1ULL);
    } else {
        for (uint32_t i = 0; i < 4096U; ++i) {
            es1371_pause();
        }
    }
}

static uint32_t es1371_read(uint16_t offset)
{
    return kernel_api->inl((uint16_t)(es1371.io_port + offset));
}

static void es1371_write(uint16_t offset, uint32_t value)
{
    kernel_api->outl((uint16_t)(es1371.io_port + offset), value);
}

static uint16_t es1371_pci_io_bar(uint8_t offset)
{
    uint32_t bar = kernel_api->pci_read32(es1371.pci.bus, es1371.pci.slot,
                                           es1371.pci.function, offset);
    uint32_t port;
    if (!(bar & 1U)) {
        return 0;
    }
    port = bar & ~3U;
    return port > 0xffffU ? 0 : (uint16_t)port;
}

static int es1371_wait_src(uint32_t *out_value)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < ES1371_WAIT_SPINS; ++i) {
        value = es1371_read(ES_REG_SRC);
        if (!(value & ES_SRC_BUSY)) {
            if (out_value) {
                *out_value = value;
            }
            return 0;
        }
        es1371_pause();
    }
    return -110;
}

static int es1371_src_write(uint16_t address, uint16_t value)
{
    uint32_t state;
    if (es1371_wait_src(&state) < 0) {
        return -110;
    }
    state &= ES_SRC_STATE_MASK;
    es1371_write(ES_REG_SRC, state | ((uint32_t)(address & 0x7fU) << 25) |
                               (uint32_t)value | ES_SRC_WRITE);
    return es1371_wait_src(0);
}

static int es1371_src_read(uint16_t address, uint16_t *out_value)
{
    uint32_t original;
    uint32_t value;
    if (!out_value || es1371_wait_src(&original) < 0) {
        return -110;
    }
    es1371_write(ES_REG_SRC, (original & ES_SRC_STATE_MASK) |
                               ((uint32_t)(address & 0x7fU) << 25) |
                               ES_CODEC_SAFE_STATE);
    for (uint32_t i = 0; i < ES1371_WAIT_SPINS; ++i) {
        value = es1371_read(ES_REG_SRC);
        if ((value & (ES_SRC_BUSY | 0x00070000U)) == ES_CODEC_SAFE_STATE) {
            *out_value = (uint16_t)value;
            es1371_write(ES_REG_SRC, (original & ES_SRC_STATE_MASK) |
                                       ((uint32_t)(address & 0x7fU) << 25));
            return 0;
        }
        es1371_pause();
    }
    es1371_write(ES_REG_SRC, original & ES_SRC_STATE_MASK);
    return -110;
}

static int es1371_set_dac1_rate(uint32_t sample_rate)
{
    uint16_t int_regs;
    uint32_t frequency;
    uint32_t state;
    frequency = ((sample_rate << 15) + 1500U) / 3000U;
    if (es1371_wait_src(&state) < 0) {
        return -110;
    }
    es1371_write(ES_REG_SRC, (state & (ES_SRC_DISABLE | ES_SRC_DISABLE_DAC2 |
                                        ES_SRC_DISABLE_ADC)) |
                               ES_SRC_DISABLE_DAC1);
    if (es1371_src_read(ES_SRC_DAC1 + ES_SRC_INT_REGS, &int_regs) < 0 ||
        es1371_src_write(ES_SRC_DAC1 + ES_SRC_INT_REGS,
                          (uint16_t)((int_regs & 0x00ffU) |
                                     ((frequency >> 5) & 0xfc00U))) < 0 ||
        es1371_src_write(ES_SRC_DAC1 + ES_SRC_VFREQ_FRAC,
                          (uint16_t)(frequency & 0x7fffU)) < 0 ||
        es1371_wait_src(&state) < 0) {
        return -110;
    }
    es1371_write(ES_REG_SRC, state & (ES_SRC_DISABLE | ES_SRC_DISABLE_DAC2 |
                                       ES_SRC_DISABLE_ADC));
    return 0;
}

static int es1371_codec_write(uint16_t address, uint16_t value)
{
    uint32_t original;
    uint32_t src;
    for (uint32_t i = 0; i < ES1371_WAIT_SPINS; ++i) {
        if (!(es1371_read(ES_REG_CODEC) & ES_CODEC_WRITE_IN_PROGRESS)) {
            break;
        }
        es1371_pause();
        if (i + 1U == ES1371_WAIT_SPINS) {
            return -110;
        }
    }
    if (es1371_wait_src(&original) < 0) {
        return -110;
    }
    es1371_write(ES_REG_SRC, (original & ES_SRC_STATE_MASK) | ES_CODEC_SAFE_STATE);
    for (uint32_t i = 0; i < ES1371_WAIT_SPINS; ++i) {
        src = es1371_read(ES_REG_SRC);
        if ((src & (ES_SRC_BUSY | 0x00070000U)) == 0U) {
            break;
        }
        es1371_pause();
        if (i + 1U == ES1371_WAIT_SPINS) {
            es1371_write(ES_REG_SRC, original & ES_SRC_STATE_MASK);
            return -110;
        }
    }
    for (uint32_t i = 0; i < ES1371_WAIT_SPINS; ++i) {
        src = es1371_read(ES_REG_SRC);
        if ((src & (ES_SRC_BUSY | 0x00070000U)) == ES_CODEC_SAFE_STATE) {
            es1371_write(ES_REG_CODEC, ((uint32_t)(address & 0x7fU) << 16) | value);
            if (es1371_wait_src(0) < 0) {
                return -110;
            }
            es1371_write(ES_REG_SRC, original & ES_SRC_STATE_MASK);
            return 0;
        }
        es1371_pause();
    }
    es1371_write(ES_REG_SRC, original & ES_SRC_STATE_MASK);
    return -110;
}

static void es1371_stop(void)
{
    if (!es1371.io_port) {
        return;
    }
    es1371_write(ES_REG_SERIAL, 0);
    es1371.control &= ~ES_CONTROL_DAC1_ENABLE;
    es1371_write(ES_REG_CONTROL, es1371.control);
    es1371.queued_bytes = 0;
}

static int es1371_wait_for_wrap(uint32_t length)
{
    uint32_t frame_bytes = length;
    uint32_t previous;
    uint32_t played = 0;
    uint64_t start = es1371_ticks();
    uint64_t wait_ticks;
    if (!frame_bytes) {
        return -22;
    }
    wait_ticks = ((uint64_t)frame_bytes * 100ULL +
                  ((uint64_t)es1371.sample_rate * 4ULL - 1ULL)) /
                 ((uint64_t)es1371.sample_rate * 4ULL);
    if (wait_ticks < 1ULL) {
        wait_ticks = 1ULL;
    }
    wait_ticks += ES1371_PLAYBACK_SLACK_TICKS;

    es1371_write(ES_REG_MEM_PAGE, ES_PAGE_DAC);
    previous = (es1371_read(ES_REG_DAC1_SIZE) >> 14) & 0x3fffcU;
    if (previous >= frame_bytes) {
        previous = 0;
    }
    for (;;) {
        uint32_t current;
        uint32_t delta;
        es1371_write(ES_REG_MEM_PAGE, ES_PAGE_DAC);
        current = (es1371_read(ES_REG_DAC1_SIZE) >> 14) & 0x3fffcU;
        if (current >= frame_bytes) {
            current = 0;
        }
        delta = current >= previous ? current - previous
                                    : frame_bytes - previous + current;
        if (delta) {
            played += delta;
            if (played >= frame_bytes) {
                return 0;
            }
            previous = current;
        }
        if (es1371_ticks() - start >= wait_ticks) {
            return 0;
        }
        es1371_wait_tick_or_pause();
    }
}

static int es1371_play_block(const uint8_t *data, uint32_t length)
{
    if (!data || !length || length > ES1371_DMA_BYTES || (length & 3U)) {
        return -22;
    }
    es1371_stop();
    es1371_copy(es1371.dma_buffer, data, length);
    if (length < ES1371_DMA_BYTES) {
        es1371_zero(es1371.dma_buffer + length, ES1371_DMA_BYTES - length);
    }
    es1371_write(ES_REG_MEM_PAGE, ES_PAGE_DAC);
    es1371_write(ES_REG_DAC1_FRAME, (uint32_t)es1371.dma_phys);
    es1371_write(ES_REG_DAC1_SIZE, (length >> 2) - 1U);
    es1371_write(ES_REG_DAC1_COUNT, (length >> 2) - 1U);
    es1371_write(ES_REG_SERIAL, ES_SERIAL_DAC1_16BIT_STEREO);
    es1371.control |= ES_CONTROL_DAC1_ENABLE;
    es1371_write(ES_REG_CONTROL, es1371.control);
    es1371.queued_bytes = length;
    if (es1371_wait_for_wrap(length) < 0) {
        es1371_stop();
        ++es1371.underruns;
        return -110;
    }
    es1371_stop();
    return 0;
}

static int es1371_is_ready(void)
{
    return es1371.active != 0;
}

static int es1371_configure(const struct leonos_audio_format *format)
{
    if (!es1371_is_ready() || !format || format->channels != 2U ||
        format->bits_per_sample != 16U || format->sample_rate < 8000U ||
        format->sample_rate > 48000U || es1371_set_dac1_rate(format->sample_rate) < 0) {
        return -22;
    }
    es1371.sample_rate = format->sample_rate;
    es1371.channels = format->channels;
    es1371.bits_per_sample = format->bits_per_sample;
    return 0;
}

static long es1371_audio_write(const void *data, uint32_t length, uint32_t *out_status)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t written = 0;
    if (out_status) {
        *out_status = LEONOS_AUDIO_STATUS_PLAYBACK_FAILED;
    }
    if (!es1371_is_ready() || (!data && length) || (length & 3U)) {
        return -22;
    }
    while (written < length) {
        uint32_t chunk = length - written;
        if (chunk > ES1371_DMA_BYTES) {
            chunk = ES1371_DMA_BYTES;
        }
        if (es1371_play_block(bytes + written, chunk) < 0) {
            return written ? (long)written : -5;
        }
        written += chunk;
    }
    if (out_status) {
        *out_status = LEONOS_AUDIO_STATUS_OK;
    }
    return (long)written;
}

static void es1371_get_state(struct leonos_audio_state *out)
{
    if (!out) {
        return;
    }
    *out = (struct leonos_audio_state){
        .present = es1371.present,
        .active = es1371.active,
        .sample_rate = es1371.sample_rate,
        .channels = es1371.channels,
        .bits_per_sample = es1371.bits_per_sample,
        .queued_bytes = es1371.queued_bytes,
        .underruns = es1371.underruns,
        .vendor_id = es1371.pci.vendor_id,
        .device_id = es1371.pci.device_id,
        .bus = es1371.pci.bus,
        .slot = es1371.pci.slot,
        .function = es1371.pci.function,
    };
}

static int es1371_hardware_init(void)
{
    struct leonos_audio_format default_format = {
        .sample_rate = 48000U,
        .channels = 2U,
        .bits_per_sample = 16U,
        .flags = 0,
    };
    uint16_t command;

    es1371_zero(&es1371, sizeof(es1371));
    if (kernel_api->pci_find(ES1371_VENDOR_ENSONIQ, ES1371_DEVICE_AUDIOPCI,
                             &es1371.pci) < 0) {
        return -19;
    }
    es1371.present = 1;
    es1371.io_port = es1371_pci_io_bar(0x10U);
    if (!es1371.io_port) {
        return -19;
    }
    command = kernel_api->pci_read16(es1371.pci.bus, es1371.pci.slot,
                                      es1371.pci.function, 0x04U);
    command |= PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER;
    kernel_api->pci_write16(es1371.pci.bus, es1371.pci.slot, es1371.pci.function,
                            0x04U, command);

    es1371_write(ES_REG_CONTROL, 0);
    es1371_write(ES_REG_SERIAL, 0);
    es1371_write(ES_REG_LEGACY, 0);
    es1371_write(ES_REG_CONTROL, ES_CONTROL_SYNC_RESET);
    for (uint32_t i = 0; i < 1000U; ++i) {
        es1371_pause();
    }
    es1371_write(ES_REG_CONTROL, 0);
    if (es1371_wait_src(0) < 0) {
        return -110;
    }
    es1371_write(ES_REG_SRC, ES_SRC_DISABLE);
    for (uint16_t index = 0; index < 0x80U; ++index) {
        if (es1371_src_write(index, 0) < 0) {
            return -110;
        }
    }
    if (es1371_src_write(ES_SRC_DAC1 + ES_SRC_TRUNC_N, 16U << 4) < 0 ||
        es1371_src_write(ES_SRC_DAC1 + ES_SRC_INT_REGS, 16U << 10) < 0 ||
        es1371_src_write(ES_SRC_DAC2 + ES_SRC_TRUNC_N, 16U << 4) < 0 ||
        es1371_src_write(ES_SRC_DAC2 + ES_SRC_INT_REGS, 16U << 10) < 0 ||
        es1371_src_write(ES_SRC_VOLUME_ADC, 1U << 12) < 0 ||
        es1371_src_write(ES_SRC_VOLUME_ADC + 1U, 1U << 12) < 0 ||
        es1371_src_write(ES_SRC_VOLUME_DAC1, 1U << 12) < 0 ||
        es1371_src_write(ES_SRC_VOLUME_DAC1 + 1U, 1U << 12) < 0 ||
        es1371_src_write(ES_SRC_VOLUME_DAC2, 1U << 12) < 0 ||
        es1371_src_write(ES_SRC_VOLUME_DAC2 + 1U, 1U << 12) < 0 ||
        es1371_set_dac1_rate(default_format.sample_rate) < 0 ||
        es1371_wait_src(0) < 0) {
        return -110;
    }
    es1371_write(ES_REG_SRC, 0);
    es1371_write(ES_REG_CODEC, 0);
    if (es1371_codec_write(ES_AC97_MASTER_VOLUME, 0x0404U) < 0 ||
        es1371_codec_write(ES_AC97_PCM_VOLUME, 0x0404U) < 0) {
        return -110;
    }

    es1371.dma_phys = kernel_api->alloc_pages(ES1371_DMA_PAGES);
    if (!es1371.dma_phys || es1371.dma_phys > 0xffffffffULL ||
        es1371.dma_phys + ES1371_DMA_BYTES > 0x100000000ULL) {
        if (es1371.dma_phys) {
            kernel_api->free_pages(es1371.dma_phys, ES1371_DMA_PAGES);
        }
        es1371.dma_phys = 0;
        return -12;
    }
    es1371.dma_buffer = (uint8_t *)(uintptr_t)es1371.dma_phys;
    es1371_zero(es1371.dma_buffer, ES1371_DMA_BYTES);
    es1371.active = 1;
    if (es1371_configure(&default_format) < 0) {
        kernel_api->free_pages(es1371.dma_phys, ES1371_DMA_PAGES);
        es1371_zero(&es1371, sizeof(es1371));
        return -95;
    }
    kernel_api->console_write("[driver] es1371 ready\n");
    return 0;
}

static int es1371_driver_init(const struct leonos_driver_kernel_api *api)
{
    static const struct leonos_driver_audio_ops ops = {
        .is_ready = es1371_is_ready,
        .configure = es1371_configure,
        .write = es1371_audio_write,
        .get_state = es1371_get_state,
    };
    if (!api || api->abi_version != LEONOS_DRIVER_ABI_VERSION ||
        api->struct_size < sizeof(*api)) {
        return -22;
    }
    kernel_api = api;
    if (es1371_hardware_init() < 0) {
        return -19;
    }
    return kernel_api->register_audio(&ops);
}

static void es1371_driver_fini(void)
{
    if (kernel_api) {
        es1371_stop();
        if (es1371.dma_phys) {
            kernel_api->free_pages(es1371.dma_phys, ES1371_DMA_PAGES);
        }
    }
    es1371_zero(&es1371, sizeof(es1371));
}

const struct leonos_driver_module leonos_driver_module = {
    .magic = LEONOS_DRIVER_MODULE_MAGIC,
    .abi_version = LEONOS_DRIVER_ABI_VERSION,
    .struct_size = sizeof(struct leonos_driver_module),
    .kind = LEONOS_DRIVER_KIND_AUDIO,
    .name = "es1371",
    .version = 1U,
    .reserved = 0,
    .init = es1371_driver_init,
    .fini = es1371_driver_fini,
};
