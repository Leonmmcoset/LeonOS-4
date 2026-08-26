#include <leonos/driver.h>

#define ES1371_VENDOR_ENSONIQ 0x1274U
#define ES1371_DEVICE_AUDIOPCI 0x1371U

#define PCI_COMMAND_IO 0x0001U
#define PCI_COMMAND_BUS_MASTER 0x0004U

#define ES_REG_CONTROL 0x00U
#define ES_REG_STATUS 0x04U
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

#define ES_STATUS_INTR 0x80000000U
#define ES_STATUS_DAC1 0x00000004U

#define ES_SERIAL_P1_LOOP 0x00000000U
#define ES_SERIAL_P1_PAUSE (1U << 11)
#define ES_SERIAL_P1_INT_EN (1U << 8)
#define ES_SERIAL_P1_SCT_RLD (1U << 7)
#define ES_SERIAL_P1_16BIT_STEREO 0x00000003U
#define ES_SERIAL_DAC1_16BIT_STEREO \
    (ES_SERIAL_P1_INT_EN | ES_SERIAL_P1_16BIT_STEREO)

#define ES_SRC_DISABLE 0x00400000U
#define ES_SRC_DISABLE_DAC1 0x00200000U
#define ES_SRC_DISABLE_DAC2 0x00100000U
#define ES_SRC_DISABLE_ADC 0x00080000U
#define ES_SRC_BUSY 0x00800000U
#define ES_SRC_WRITE 0x01000000U
#define ES_SRC_STATE_MASK (ES_SRC_DISABLE | ES_SRC_DISABLE_DAC1 | \
                           ES_SRC_DISABLE_DAC2 | ES_SRC_DISABLE_ADC)

#define ES_CODEC_READY 0x80000000U
#define ES_CODEC_WRITE_IN_PROGRESS 0x40000000U
#define ES_CODEC_READ_REQUEST 0x00800000U
#define ES_CODEC_SAFE_STATE 0x00010000U
#define ES_CODEC_TRANSITION_MASK (ES_SRC_BUSY | 0x00070000U)

#define ES_SRC_DAC1 0x70U
#define ES_SRC_DAC2 0x74U
#define ES_SRC_ADC 0x78U
#define ES_SRC_VOLUME_ADC 0x6cU
#define ES_SRC_VOLUME_DAC1 0x7cU
#define ES_SRC_VOLUME_DAC2 0x7eU
#define ES_SRC_TRUNC_N 0x00U
#define ES_SRC_INT_REGS 0x01U
#define ES_SRC_VFREQ_FRAC 0x03U

#define ES_AC97_RESET 0x00U
#define ES_AC97_MASTER_VOLUME 0x02U
#define ES_AC97_PCM_VOLUME 0x18U
#define ES_AC97_GENERAL_PURPOSE 0x20U
#define ES_AC97_POWERDOWN 0x26U
#define ES_AC97_VENDOR_ID1 0x7cU
#define ES_AC97_VENDOR_ID2 0x7eU
#define ES_AC97_POWER_READY_MASK 0x000fU

#define ES1371_DMA_BYTES (16U * 1024U)
#define ES1371_DMA_PAGES (ES1371_DMA_BYTES / 4096U)
#define ES1371_DMA_FRAME_BYTES 4U
#define ES1371_DMA_PERIOD_BYTES 2048U
#define ES1371_DMA_LEAD_BYTES (ES1371_DMA_PERIOD_BYTES * 2U)
#define ES1371_DMA_GUARD_BYTES ES1371_DMA_FRAME_BYTES
#define ES1371_WAIT_SPINS 1000000U

struct es1371_state {
    uint32_t present;
    uint32_t active;
    struct leonos_driver_pci_device pci;
    uint16_t io_port;
    uint64_t dma_phys;
    uint8_t *dma_buffer;
    uint32_t control;
    uint32_t serial;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t queued_bytes;
    uint32_t underruns;
    uint32_t overruns;
    uint32_t dma_write;
    uint32_t dma_last_cursor;
    uint32_t dma_configured;
    uint32_t dma_running;
    uint32_t producer_initialized;
    uint32_t empty_reported;
    uint32_t overrun_reported;
    uint32_t cursor_reported;
    uint32_t nonzero_reported;
    uint32_t nonzero_range_start;
    uint32_t nonzero_range_end;
    uint32_t nonzero_range_pending;
    uint32_t nonzero_checksum;
    uint32_t diagnostics_reported;
    uint32_t cursor_probe_pending;
    uint32_t cursor_probe_baseline;
    uint32_t cursor_progress_baseline;
    uint32_t cursor_progress_samples;
    uint32_t cursor_progress_reported;
    volatile uint32_t io_lock;
};

static const struct leonos_driver_kernel_api *kernel_api;
static struct es1371_state es1371;

static uint32_t es1371_read(uint16_t offset);
static void es1371_write(uint16_t offset, uint32_t value);

static void es1371_hex32(char *out, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    for (uint32_t index = 0; index < 8U; ++index) {
        out[index] = digits[(value >> ((7U - index) * 4U)) & 0x0fU];
    }
}

static void es1371_log_registers(void)
{
    char line[] = "[driver] es1371 regs ctl=0x00000000 serial=0x00000000 "
                  "status=0x00000000 size=0x00000000 count=0x00000000\n";
    if (es1371.diagnostics_reported) return;
    es1371_write(ES_REG_MEM_PAGE, ES_PAGE_DAC);
    es1371_hex32(line + 27U, es1371_read(ES_REG_CONTROL));
    es1371_hex32(line + 45U, es1371_read(ES_REG_SERIAL));
    es1371_hex32(line + 63U, es1371_read(ES_REG_STATUS));
    es1371_hex32(line + 79U, es1371_read(ES_REG_DAC1_SIZE));
    es1371_hex32(line + 96U, es1371_read(ES_REG_DAC1_COUNT));
    kernel_api->console_write(line);
    es1371.diagnostics_reported = 1U;
}

static void es1371_pause(void)
{
    __asm__ volatile("pause");
}

static void es1371_lock(void)
{
    while (__sync_lock_test_and_set(&es1371.io_lock, 1U)) {
        es1371_pause();
    }
}

static void es1371_unlock(void)
{
    __sync_lock_release(&es1371.io_lock);
}

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

static int es1371_wait_codec_idle(uint32_t *out_value)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < ES1371_WAIT_SPINS; ++i) {
        value = es1371_read(ES_REG_CODEC);
        if (!(value & ES_CODEC_WRITE_IN_PROGRESS)) {
            if (out_value) {
                *out_value = value;
            }
            return 0;
        }
        es1371_pause();
    }
    return -110;
}

/*
 * ES1371 codec cycles may only be issued from the SRC's short safe window.
 * Keep the transition protocol in one place so a SMP caller cannot leave the
 * SRC exposed after a failed mixer operation.
 */
static int es1371_codec_begin(uint32_t *out_original)
{
    uint32_t original;
    uint32_t src;
    if (!out_original || es1371_wait_codec_idle(0) < 0 ||
        es1371_wait_src(&original) < 0) {
        return -110;
    }
    es1371_write(ES_REG_SRC,
                  (original & ES_SRC_STATE_MASK) | ES_CODEC_SAFE_STATE);
    for (uint32_t i = 0; i < ES1371_WAIT_SPINS; ++i) {
        src = es1371_read(ES_REG_SRC);
        if ((src & ES_CODEC_TRANSITION_MASK) == 0U) {
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
        if ((src & ES_CODEC_TRANSITION_MASK) == ES_CODEC_SAFE_STATE) {
            *out_original = original;
            return 0;
        }
        es1371_pause();
    }
    es1371_write(ES_REG_SRC, original & ES_SRC_STATE_MASK);
    return -110;
}

static void es1371_codec_end(uint32_t original)
{
    (void)es1371_wait_src(0);
    es1371_write(ES_REG_SRC, original & ES_SRC_STATE_MASK);
}

static int es1371_codec_write(uint16_t address, uint16_t value)
{
    uint32_t original;
    if (es1371_codec_begin(&original) < 0) {
        return -110;
    }
    es1371_write(ES_REG_CODEC, ((uint32_t)(address & 0x7fU) << 16) | value);
    es1371_codec_end(original);
    return es1371_wait_codec_idle(0);
}

static int es1371_codec_read(uint16_t address, uint16_t *out_value)
{
    uint32_t original;
    uint32_t codec;
    if (!out_value || es1371_codec_begin(&original) < 0) {
        return -110;
    }
    es1371_write(ES_REG_CODEC, ((uint32_t)(address & 0x7fU) << 16) |
                                  ES_CODEC_READ_REQUEST);
    es1371_codec_end(original);
    if (es1371_wait_codec_idle(0) < 0) {
        return -110;
    }
    for (uint32_t i = 0; i < ES1371_WAIT_SPINS; ++i) {
        codec = es1371_read(ES_REG_CODEC);
        if (codec & ES_CODEC_READY) {
            *out_value = (uint16_t)codec;
            return 0;
        }
        es1371_pause();
    }
    return -110;
}

static void es1371_sleep_ms(uint32_t milliseconds)
{
    if (kernel_api->sleep_ms) {
        kernel_api->sleep_ms(milliseconds);
        return;
    }
    for (uint32_t i = 0; i < milliseconds * 1000U; ++i) {
        es1371_pause();
    }
}

static void es1371_hex16(char *out, uint16_t value)
{
    static const char digits[] = "0123456789abcdef";
    for (uint32_t index = 0; index < 4U; ++index) {
        out[index] = digits[(value >> ((3U - index) * 4U)) & 0x0fU];
    }
}

static void es1371_log_codec(uint16_t reset, uint16_t vendor1,
                              uint16_t vendor2, uint16_t power,
                              uint16_t master, uint16_t pcm)
{
    char line[] = "[driver] es1371 codec reset=0x0000 vendor=0x00000000 "
                  "power=0x0000 master=0x0000 pcm=0x0000\n";
    es1371_hex16(line + 30U, reset);
    es1371_hex16(line + 44U, vendor1);
    es1371_hex16(line + 48U, vendor2);
    es1371_hex16(line + 61U, power);
    es1371_hex16(line + 75U, master);
    es1371_hex16(line + 86U, pcm);
    kernel_api->console_write(line);
}

static int es1371_codec_initialize(void)
{
    uint16_t reset;
    uint16_t vendor1;
    uint16_t vendor2;
    uint16_t power;
    uint16_t master;
    uint16_t pcm;

    /* Match the ES1371/AC'97 sequence used by mature drivers: reset the
     * codec, give its Vref time to settle, then prove read and write cycles
     * work before enabling DAC1. */
    if (es1371_codec_write(ES_AC97_RESET, 0U) < 0) {
        return -110;
    }
    es1371_sleep_ms(750U);
    if (es1371_codec_read(ES_AC97_RESET, &reset) < 0 ||
        es1371_codec_read(ES_AC97_VENDOR_ID1, &vendor1) < 0 ||
        es1371_codec_read(ES_AC97_VENDOR_ID2, &vendor2) < 0) {
        return -110;
    }
    es1371_sleep_ms(50U);
    if (es1371_codec_write(ES_AC97_POWERDOWN, 0U) < 0 ||
        es1371_codec_write(ES_AC97_GENERAL_PURPOSE, 0U) < 0 ||
        es1371_codec_write(ES_AC97_MASTER_VOLUME, 0U) < 0 ||
        es1371_codec_write(ES_AC97_PCM_VOLUME, 0U) < 0) {
        return -110;
    }
    for (uint32_t attempt = 0; attempt < 100U; ++attempt) {
        if (es1371_codec_read(ES_AC97_POWERDOWN, &power) < 0) {
            return -110;
        }
        if ((power & ES_AC97_POWER_READY_MASK) == ES_AC97_POWER_READY_MASK) {
            break;
        }
        if (attempt + 1U == 100U) {
            return -110;
        }
        es1371_sleep_ms(1U);
    }
    if (es1371_codec_read(ES_AC97_MASTER_VOLUME, &master) < 0 ||
        es1371_codec_read(ES_AC97_PCM_VOLUME, &pcm) < 0) {
        return -110;
    }
    es1371_log_codec(reset, vendor1, vendor2, power, master, pcm);
    if (master != 0U || pcm != 0U) {
        kernel_api->console_write("[driver] es1371 codec mixer readback mismatch\n");
        return -5;
    }
    return 0;
}

static void es1371_stop(void)
{
    if (!es1371.io_port) {
        return;
    }
    es1371_write(ES_REG_SERIAL, 0);
    es1371.serial = 0;
    es1371.control &= ~ES_CONTROL_DAC1_ENABLE;
    es1371_write(ES_REG_CONTROL, es1371.control);
    es1371.queued_bytes = 0;
    es1371.dma_write = 0;
    es1371.dma_last_cursor = 0;
    es1371.dma_configured = 0;
    es1371.dma_running = 0;
    es1371.producer_initialized = 0;
    es1371.empty_reported = 0;
    es1371.overrun_reported = 0;
}

/*
 * DAC1 period status is level-triggered.  An ES1371 acknowledges it by
 * briefly clearing P1_INT_EN then restoring the active serial format.  This
 * is the same sequence used by the Linux driver.  LeonOS currently polls
 * audio progress rather than registering the PCI audio IRQ, so perform the
 * acknowledgement at each serialized hardware access.
 */
static void es1371_ack_dac1_irq(void)
{
    uint32_t status;
    if (!es1371.dma_running || !(es1371.serial & ES_SERIAL_P1_INT_EN)) {
        return;
    }
    status = es1371_read(ES_REG_STATUS);
    if ((status & (ES_STATUS_INTR | ES_STATUS_DAC1)) ==
        (ES_STATUS_INTR | ES_STATUS_DAC1)) {
        es1371_write(ES_REG_SERIAL, es1371.serial & ~ES_SERIAL_P1_INT_EN);
        es1371_write(ES_REG_SERIAL, es1371.serial);
    }
}

static uint32_t es1371_dma_cursor(void)
{
    uint32_t current;
    es1371_write(ES_REG_MEM_PAGE, ES_PAGE_DAC);
    current = (es1371_read(ES_REG_DAC1_SIZE) >> 14) & 0x3fffcU;
    if (current >= ES1371_DMA_BYTES) {
        return 0;
    }
    return current & ~(ES1371_DMA_FRAME_BYTES - 1U);
}

static void es1371_ring_copy(uint32_t offset, const uint8_t *data, uint32_t length)
{
    uint32_t first = ES1371_DMA_BYTES - offset;
    if (first > length) {
        first = length;
    }
    es1371_copy(es1371.dma_buffer + offset, data, first);
    if (length > first) {
        es1371_copy(es1371.dma_buffer, data + first, length - first);
    }
}

/*
 * The device may be consuming the ring while a producer detects an
 * underrun.  Never clear the complete allocation in that case: doing so can
 * erase a sample which the DMA engine has already fetched but not yet handed
 * to the codec.  Only initialise the new, known-safe lead region.
 */
static void es1371_ring_zero(uint32_t offset, uint32_t length)
{
    uint32_t first = ES1371_DMA_BYTES - offset;
    if (first > length) {
        first = length;
    }
    es1371_zero(es1371.dma_buffer + offset, first);
    if (length > first) {
        es1371_zero(es1371.dma_buffer, length - first);
    }
}

static int es1371_start_dma(void)
{
    uint32_t cursor;
    if (!es1371.dma_buffer || !es1371.dma_phys) {
        return -19;
    }
    if (es1371.dma_configured) {
        return 0;
    }

    /* Program DAC1 while stopped.  Starting from a zeroed ring guarantees
     * that an initial scheduling delay produces silence, never stale data. */
    es1371_stop();
    es1371_zero(es1371.dma_buffer, ES1371_DMA_BYTES);
    es1371_write(ES_REG_MEM_PAGE, ES_PAGE_DAC);
    es1371_write(ES_REG_DAC1_FRAME, (uint32_t)es1371.dma_phys);
    es1371_write(ES_REG_DAC1_SIZE,
                  (ES1371_DMA_BYTES / ES1371_DMA_FRAME_BYTES) - 1U);
    /* ES1371's sample counter is a period counter.  P1_INT_EN is required by
     * both real AudioPCI hardware and VMware's model to advance the DMA
     * engine; the interrupt is harmless because LeonOS polls the cursor. */
    es1371.serial = ES_SERIAL_DAC1_16BIT_STEREO;
    es1371_write(ES_REG_SERIAL, es1371.serial);
    es1371_write(ES_REG_DAC1_COUNT,
                  (ES1371_DMA_PERIOD_BYTES / ES1371_DMA_FRAME_BYTES) - 1U);
    es1371.control &= ~ES_CONTROL_DAC1_ENABLE;
    es1371_write(ES_REG_CONTROL, es1371.control);
    es1371.dma_configured = 1;
    es1371.dma_running = 0;
    /* VMware can expose a non-zero current-count field immediately after
     * FRAME/SIZE are programmed.  Anchor the software producer to that
     * value instead of assuming the ring starts at byte zero. */
    cursor = es1371_dma_cursor();
    es1371.dma_last_cursor = cursor;
    es1371.dma_write = 0;
    es1371.queued_bytes = 0;
    es1371.producer_initialized = 0;
    es1371.cursor_reported = 0;
    es1371.nonzero_reported = 0;
    es1371.nonzero_range_start = 0;
    es1371.nonzero_range_end = 0;
    es1371.nonzero_range_pending = 0;
    es1371.nonzero_checksum = 0;
    es1371.cursor_probe_pending = 0;
    es1371.cursor_progress_baseline = 0;
    es1371.cursor_progress_samples = 0;
    es1371.cursor_progress_reported = 0;
    es1371.diagnostics_reported = 0;
    kernel_api->console_write("[driver] es1371 DMA ring prepared (loop, period=2048)\n");
    es1371_log_registers();
    return 0;
}

static int es1371_enable_dma(void)
{
    uint32_t cursor;
    if (!es1371.dma_configured) {
        return -19;
    }
    if (es1371.dma_running) {
        return 0;
    }
    __sync_synchronize();
    es1371.control |= ES_CONTROL_DAC1_ENABLE;
    es1371_write(ES_REG_CONTROL, es1371.control);
    cursor = es1371_dma_cursor();
    es1371.dma_last_cursor = cursor;
    es1371.dma_running = 1;
    if (!(es1371_read(ES_REG_CONTROL) & ES_CONTROL_DAC1_ENABLE)) {
        kernel_api->console_write("[driver] es1371 DAC1 enable rejected\n");
        es1371.dma_running = 0;
        return -5;
    }
    kernel_api->console_write("[driver] es1371 DMA ring started (loop, period=2048)\n");
    return 0;
}

static uint32_t es1371_cursor_delta(uint32_t previous, uint32_t current)
{
    return current >= previous ? current - previous
                               : ES1371_DMA_BYTES - previous + current;
}

static int es1371_cursor_reached(uint32_t previous, uint32_t current,
                                  uint32_t target)
{
    uint32_t delta = es1371_cursor_delta(previous, current);
    uint32_t distance = es1371_cursor_delta(previous, target);
    return delta && distance && distance <= delta;
}

static void es1371_recover_empty(uint32_t cursor)
{
    /* The whole ring was cleared before its first start.  While DMA is live,
     * only clear the lead which will be consumed before the next producer
     * write; this avoids racing a hardware read of a later ring segment. */
    es1371_ring_zero(cursor, ES1371_DMA_LEAD_BYTES);
    __sync_synchronize();
    es1371.dma_write = (cursor + ES1371_DMA_LEAD_BYTES) % ES1371_DMA_BYTES;
    es1371.queued_bytes = ES1371_DMA_LEAD_BYTES;
    es1371.producer_initialized = 1;
    es1371.empty_reported = 0;
    es1371.nonzero_range_pending = 0;
}

static void es1371_update_consumption(uint32_t cursor)
{
    uint32_t previous;
    uint32_t delta;
    if (!es1371.producer_initialized) {
        es1371.dma_last_cursor = cursor;
        es1371_recover_empty(cursor);
        return;
    }
    previous = es1371.dma_last_cursor;
    delta = es1371_cursor_delta(previous, cursor);
    es1371.dma_last_cursor = cursor;
    if (!delta) {
        return;
    }
    if (delta >= es1371.queued_bytes) {
        ++es1371.underruns;
        if (!es1371.empty_reported) {
            kernel_api->console_write("[driver] es1371 DMA underrun; refilling silence\n");
            es1371.empty_reported = 1;
        }
        es1371_recover_empty(cursor);
        return;
    }
    es1371.queued_bytes -= delta;
    if (es1371.cursor_probe_pending) {
        es1371.cursor_progress_samples += delta;
        if (es1371.cursor_progress_samples >= ES1371_DMA_BYTES &&
            !es1371.cursor_progress_reported) {
            kernel_api->console_write("[driver] es1371 DMA cursor remains advancing\n");
            es1371.cursor_progress_reported = 1U;
        }
    }
    if (es1371.nonzero_range_pending &&
        es1371_cursor_reached(previous, cursor, es1371.nonzero_range_end)) {
        kernel_api->console_write("[driver] es1371 first nonzero DMA range consumed\n");
        es1371.nonzero_range_pending = 0;
    }
}

static int es1371_queue_block(const uint8_t *data, uint32_t length)
{
    uint32_t cursor;
    uint32_t free_bytes;
    uint32_t nonzero = 0;
    uint32_t checksum = 0;
    uint32_t write_offset;
    if (!data || !length || length > ES1371_DMA_BYTES / 2U ||
        (length & (ES1371_DMA_FRAME_BYTES - 1U)) || !es1371.dma_configured) {
        return -22;
    }
    if (!es1371.nonzero_reported) {
        for (uint32_t index = 0; index < length; ++index) {
            nonzero |= data[index];
            checksum = (checksum << 5) - checksum + data[index];
        }
    }
    es1371_ack_dac1_irq();
    cursor = es1371.dma_running ? es1371_dma_cursor() : es1371.dma_last_cursor;
    if (es1371.dma_running) {
        es1371_update_consumption(cursor);
    } else if (!es1371.producer_initialized) {
        es1371_recover_empty(cursor);
    }
    free_bytes = (ES1371_DMA_BYTES - ES1371_DMA_GUARD_BYTES) -
                 es1371.queued_bytes;
    if (free_bytes < length) {
        ++es1371.overruns;
        if (!es1371.overrun_reported) {
            kernel_api->console_write("[driver] es1371 DMA queue full; short write\n");
            es1371.overrun_reported = 1;
        }
        return -11;
    }
    write_offset = es1371.dma_write;
    es1371_ring_copy(write_offset, data, length);
    __sync_synchronize();
    es1371.dma_write = (es1371.dma_write + length) % ES1371_DMA_BYTES;
    es1371.queued_bytes += length;
    es1371.empty_reported = 0;
    if (nonzero && !es1371.nonzero_reported) {
        kernel_api->console_write("[driver] es1371 first nonzero PCM buffer queued\n");
        es1371.nonzero_reported = 1;
        es1371.nonzero_range_start = write_offset;
        es1371.nonzero_range_end = (write_offset + length) % ES1371_DMA_BYTES;
        es1371.nonzero_range_pending = 1;
        es1371.nonzero_checksum = checksum;
    }
    return 0;
}

static int es1371_is_ready(void)
{
    return es1371.active != 0;
}

static int es1371_configure(const struct leonos_audio_format *format)
{
    int ret;
    if (!es1371_is_ready() || !format || format->channels != 2U ||
        format->bits_per_sample != 16U || format->sample_rate < 8000U ||
        format->sample_rate > 48000U) {
        return -22;
    }
    es1371_lock();
    es1371_stop();
    ret = es1371_set_dac1_rate(format->sample_rate);
    if (ret < 0) {
        es1371_unlock();
        return ret;
    }
    es1371.sample_rate = format->sample_rate;
    es1371.channels = format->channels;
    es1371.bits_per_sample = format->bits_per_sample;
    es1371_unlock();
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
    es1371_lock();
    if (!es1371.dma_configured) {
        if (es1371_start_dma() < 0) {
            es1371_unlock();
            return -5;
        }
    }
    es1371_ack_dac1_irq();
    if (es1371.cursor_probe_pending && !es1371.cursor_reported) {
        uint32_t after = es1371_dma_cursor();
        kernel_api->console_write(after != es1371.cursor_probe_baseline
            ? "[driver] es1371 DMA cursor advancing\n"
            : "[driver] es1371 DMA cursor stalled\n");
        es1371.cursor_reported = 1U;
        es1371.cursor_probe_pending = 0U;
    }
    while (written < length) {
        uint32_t chunk = length - written;
        if (chunk > ES1371_DMA_BYTES / 2U) {
            chunk = ES1371_DMA_BYTES / 2U;
        }
        if (es1371_queue_block(bytes + written, chunk) < 0) {
            if (out_status) {
                *out_status = LEONOS_AUDIO_STATUS_WOULD_BLOCK;
            }
            es1371_unlock();
            return written ? (long)written : 0;
        }
        written += chunk;
        if (!es1371.dma_running && es1371.queued_bytes >= ES1371_DMA_LEAD_BYTES) {
            if (es1371_enable_dma() < 0) {
                es1371_unlock();
                return written ? (long)written : -5;
            }
        }
    }
    if (es1371.nonzero_reported && !es1371.cursor_reported &&
        !es1371.cursor_probe_pending) {
        es1371.cursor_probe_baseline = es1371.dma_last_cursor;
        es1371.cursor_progress_baseline = es1371.dma_last_cursor;
        es1371.cursor_progress_samples = 0;
        es1371.cursor_probe_pending = 1U;
    }
    if (out_status) {
        *out_status = LEONOS_AUDIO_STATUS_OK;
    }
    es1371_unlock();
    return (long)written;
}

static void es1371_get_state(struct leonos_audio_state *out)
{
    if (!out) {
        return;
    }
    es1371_lock();
    es1371_ack_dac1_irq();
    if (es1371.dma_running) {
        es1371_update_consumption(es1371_dma_cursor());
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
    es1371_unlock();
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
    /* Flush the reset write before holding the AC-link warm-reset pulse.
     * The hardware requires at least 20 us; the bounded spin is deliberately
     * longer and avoids depending on a scheduler tick during early boot. */
    (void)es1371_read(ES_REG_CONTROL);
    for (uint32_t i = 0; i < 20000U; ++i) {
        es1371_pause();
    }
    /* ES1371 playback uses the codec clock generated by the normal reset
     * state.  Do not enable the optional joystick gate: VMware's AudioPCI
     * emulation can leave DAC1 halted when that unrelated gate is asserted. */
    es1371.control = 0;
    es1371_write(ES_REG_CONTROL, es1371.control);
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
    if (es1371_codec_initialize() < 0) {
        kernel_api->console_write("[driver] es1371 codec initialization failed\n");
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
    es1371.sample_rate = default_format.sample_rate;
    es1371.channels = default_format.channels;
    es1371.bits_per_sample = default_format.bits_per_sample;
    kernel_api->console_write("[driver] es1371 ready (PCM DAC1 stereo, nonblocking)\n");
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
