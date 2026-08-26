#include <leonos/driver.h>

#define AC97_VENDOR_INTEL 0x8086U
#define AC97_DEVICE_ICH 0x2415U

#define PCI_COMMAND_IO 0x0001U
#define PCI_COMMAND_BUS_MASTER 0x0004U

#define AC97_NAM_MASTER_VOLUME 0x02U
#define AC97_NAM_RESET 0x00U
#define AC97_NAM_HEADPHONE_VOLUME 0x04U
#define AC97_NAM_CD_VOLUME 0x12U
#define AC97_NAM_PCM_OUT_VOLUME 0x18U
#define AC97_NAM_POWERDOWN 0x26U
#define AC97_NAM_EXT_AUDIO_ID 0x28U
#define AC97_NAM_EXT_AUDIO_CTRL 0x2aU
#define AC97_NAM_PCM_FRONT_DAC_RATE 0x2cU
#define AC97_EXT_AUDIO_VRA 0x0001U
#define AC97_POWER_READY_MASK 0x000fU

#define AC97_PO_BDBAR 0x10U
#define AC97_PO_CIV 0x14U
#define AC97_PO_LVI 0x15U
#define AC97_PO_SR 0x16U
#define AC97_PO_PICB 0x18U
#define AC97_PO_CR 0x1bU

#define AC97_PO_SR_DCH 0x0001U
#define AC97_PO_SR_LVBCI 0x0004U
#define AC97_PO_SR_BCIS 0x0008U
#define AC97_PO_SR_FIFOE 0x0010U
#define AC97_PO_SR_CLEAR 0x001cU
#define AC97_PO_CR_RUN 0x01U
#define AC97_PO_CR_RESET 0x02U
#define AC97_BDL_IOC 0x8000U

#define AC97_BDL_COUNT 32U
#define AC97_BUFFER_BYTES 2048U
#define AC97_RING_BYTES (AC97_BDL_COUNT * AC97_BUFFER_BYTES)
#define AC97_DMA_PAGES (AC97_RING_BYTES / 4096U)
#define AC97_LEAD_DESCRIPTORS 4U
#define AC97_START_WAIT_SPINS 100000U
#define AC97_RESET_WAIT_SPINS 1000U
#define AC97_CODEC_READY_POLLS 25U
/* A failed codec must return promptly; the caller can retry playback. */

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
    uint32_t overruns;
    uint32_t write_index;
    uint32_t queued_descriptors;
    uint32_t hw_civ;
    uint32_t hw_picb;
    uint32_t last_hw_civ;
    uint32_t last_hw_picb;
    uint32_t running;
    uint32_t stream_started;
    uint32_t underrun_reported;
    uint32_t overrun_reported;
    uint32_t diagnostic_reported;
    uint32_t nonzero_reported;
    uint32_t nonzero_consumed_reported;
    uint32_t nonzero_descriptor;
    uint16_t slot_bytes[AC97_BDL_COUNT];
    volatile uint32_t io_lock;
    uint8_t variable_rate;
};

static const struct leonos_driver_kernel_api *kernel_api;
static struct ac97_state ac97;

static void ac97_free_dma(void);

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

/* AC'97 NAM and bus-master status/PICB registers are word-wide.  Several
 * emulators (including QEMU and VirtualBox) intentionally ignore byte
 * accesses to these registers, so composing a word from two inb/outb calls
 * returns bogus PICB/status values and leaves the codec muted. */
static uint16_t ac97_io_inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void ac97_io_outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static uint16_t ac97_read16(uint16_t port)
{
    return ac97_io_inw(port);
}

static void ac97_write16(uint16_t port, uint16_t value)
{
    ac97_io_outw(port, value);
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

static void ac97_pause(void)
{
    __asm__ volatile("pause");
}

static void ac97_lock(void)
{
    while (__sync_lock_test_and_set(&ac97.io_lock, 1U)) {
        ac97_pause();
    }
}

static void ac97_unlock(void)
{
    __sync_lock_release(&ac97.io_lock);
}

static uint8_t ac97_bm_read8(uint16_t offset)
{
    return kernel_api->inb((uint16_t)(ac97.bus_master_port + offset));
}

static void ac97_bm_write8(uint16_t offset, uint8_t value)
{
    kernel_api->outb((uint16_t)(ac97.bus_master_port + offset), value);
}

static uint16_t ac97_bm_read16(uint16_t offset)
{
    return ac97_io_inw((uint16_t)(ac97.bus_master_port + offset));
}

static void ac97_bm_write16(uint16_t offset, uint16_t value)
{
    ac97_io_outw((uint16_t)(ac97.bus_master_port + offset), value);
}

static void ac97_reset_stream_state(void)
{
    ac97.queued_bytes = 0;
    ac97.write_index = 0;
    ac97.queued_descriptors = 0;
    ac97.hw_civ = 0;
    ac97.hw_picb = 0;
    ac97.last_hw_civ = 0;
    ac97.last_hw_picb = 0;
    ac97.running = 0;
    ac97.stream_started = 0;
    ac97.underrun_reported = 0;
    ac97.overrun_reported = 0;
    ac97.diagnostic_reported = 0;
    ac97.nonzero_reported = 0;
    ac97.nonzero_consumed_reported = 0;
    ac97.nonzero_descriptor = 0;
    ac97_zero(ac97.slot_bytes, sizeof(ac97.slot_bytes));
}

static void ac97_refill_silence_locked(void);

static void ac97_stop_locked(void)
{
    uint32_t attempt;
    if (!ac97.bus_master_port) {
        return;
    }
    ac97_bm_write8(AC97_PO_CR, 0);
    ac97_bm_write8(AC97_PO_CR, AC97_PO_CR_RESET);
    for (attempt = 0; attempt < AC97_RESET_WAIT_SPINS; ++attempt) {
        if (!(ac97_bm_read8(AC97_PO_CR) & AC97_PO_CR_RESET)) {
            break;
        }
        ac97_pause();
    }
    ac97_bm_write8(AC97_PO_CR, 0);
    ac97_bm_write16(AC97_PO_SR, AC97_PO_SR_CLEAR);
    ac97_reset_stream_state();
}

static void ac97_stop(void)
{
    ac97_lock();
    ac97_stop_locked();
    ac97_unlock();
}

static int ac97_alloc_dma(void)
{
    ac97.bdl_phys = kernel_api->alloc_pages(1);
    ac97.buffers_phys = kernel_api->alloc_pages(AC97_RING_BYTES / 4096U);
    if (!ac97.bdl_phys || !ac97.buffers_phys ||
        ac97.bdl_phys > 0xffffffffULL ||
        ac97.buffers_phys > 0xffffffffULL ||
        ac97.buffers_phys + AC97_RING_BYTES > 0x100000000ULL) {
        ac97_free_dma();
        return -12;
    }
    ac97.bdl = (struct ac97_buffer_desc *)(uintptr_t)ac97.bdl_phys;
    ac97.buffers = (uint8_t *)(uintptr_t)ac97.buffers_phys;
    ac97_zero(ac97.bdl, 4096U);
    ac97_zero(ac97.buffers, AC97_RING_BYTES);
    ac97_reset_stream_state();
    return 0;
}

static void ac97_free_dma(void)
{
    if (ac97.buffers_phys) {
        kernel_api->free_pages(ac97.buffers_phys, AC97_DMA_PAGES);
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

static int ac97_configure_mixer_locked(void)
{
    static const uint8_t mixer_registers[] = {
        AC97_NAM_MASTER_VOLUME,
        AC97_NAM_HEADPHONE_VOLUME,
        AC97_NAM_PCM_OUT_VOLUME,
        AC97_NAM_CD_VOLUME,
    };
    uint32_t index;
    uint8_t warned = 0;
    for (index = 0; index < sizeof(mixer_registers); ++index) {
        uint16_t port = (uint16_t)(ac97.mixer_port + mixer_registers[index]);
        ac97_write16(port, 0U);
        if (ac97_read16(port) & 0x8000U) {
            /* Some emulators expose the mute bit for optional mixer paths
             * (notably headphone/CD) as read-only.  The write is still the
             * correct unmute request; do not reject an otherwise usable PCM
             * codec merely because that optional readback is conservative. */
            if (!warned) {
                kernel_api->console_write("[driver] ac97 codec mixer readback muted\n");
                warned = 1;
            }
        }
    }
    return 0;
}

/*
 * AC'97 codecs power up asynchronously after a mixer reset.  VirtualBox is
 * deliberately conservative about the power-ready bits, while QEMU may
 * expose them immediately.  Use the same reset/wake sequence for both and
 * keep the wait bounded; a missing optional status bit must not disable an
 * otherwise usable PCM path.
 */
static void ac97_codec_initialize(void)
{
    uint16_t power = 0;
    uint32_t attempt;

    ac97_write16((uint16_t)(ac97.mixer_port + AC97_NAM_RESET), 0U);
    if (kernel_api->sleep_ms) {
        kernel_api->sleep_ms(2U);
    }
    ac97_write16((uint16_t)(ac97.mixer_port + AC97_NAM_POWERDOWN), 0U);
    for (attempt = 0; attempt < AC97_CODEC_READY_POLLS; ++attempt) {
        power = ac97_read16((uint16_t)(ac97.mixer_port + AC97_NAM_POWERDOWN));
        if ((power & AC97_POWER_READY_MASK) == AC97_POWER_READY_MASK) {
            break;
        }
        if (kernel_api->sleep_ms) {
            kernel_api->sleep_ms(1U);
        } else {
            ac97_pause();
        }
    }
    if ((power & AC97_POWER_READY_MASK) != AC97_POWER_READY_MASK) {
        kernel_api->console_write("[driver] ac97 codec power-ready bits unavailable; continuing\n");
    }
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
    ac97_lock();
    /* A format change is the one normal operation allowed to stop the
     * controller.  It leaves the persistent BDL in a known empty state. */
    ac97_stop_locked();
    if (format->sample_rate != 48000U) {
        ext_id = ac97_read16((uint16_t)(ac97.mixer_port + AC97_NAM_EXT_AUDIO_ID));
        if (!(ext_id & AC97_EXT_AUDIO_VRA)) {
            ac97_unlock();
            return -95;
        }
        ext_ctrl = ac97_read16((uint16_t)(ac97.mixer_port + AC97_NAM_EXT_AUDIO_CTRL));
        ac97_write16((uint16_t)(ac97.mixer_port + AC97_NAM_EXT_AUDIO_CTRL),
                     (uint16_t)(ext_ctrl | AC97_EXT_AUDIO_VRA));
        ac97_write16((uint16_t)(ac97.mixer_port + AC97_NAM_PCM_FRONT_DAC_RATE),
                     (uint16_t)format->sample_rate);
        ac97.variable_rate = 1;
    } else {
        ext_ctrl = ac97_read16((uint16_t)(ac97.mixer_port + AC97_NAM_EXT_AUDIO_CTRL));
        ac97_write16((uint16_t)(ac97.mixer_port + AC97_NAM_EXT_AUDIO_CTRL),
                     (uint16_t)(ext_ctrl & (uint16_t)~AC97_EXT_AUDIO_VRA));
        ac97_write16((uint16_t)(ac97.mixer_port + AC97_NAM_PCM_FRONT_DAC_RATE),
                     48000U);
        ac97.variable_rate = 0;
    }
    if (ac97_configure_mixer_locked() < 0) {
        ac97_unlock();
        return -5;
    }
    ext_ctrl = ac97_read16((uint16_t)(ac97.mixer_port + AC97_NAM_EXT_AUDIO_CTRL));
    (void)ext_ctrl;
    ac97_apply_format(format);
    ac97_unlock();
    return 0;
}

static uint32_t ac97_civ_distance(uint32_t from, uint32_t to)
{
    return to >= from ? to - from : AC97_BDL_COUNT - from + to;
}

static void ac97_log_position(uint16_t status)
{
    char line[] = "[driver] ac97 DMA ring active\n";
    if (ac97.diagnostic_reported) {
        return;
    }
    kernel_api->console_write(line);
    (void)status;
    ac97.diagnostic_reported = 1U;
}

static void ac97_program_lvi_locked(void)
{
    uint32_t last;
    if (!ac97.queued_descriptors) {
        return;
    }
    last = (ac97.write_index + AC97_BDL_COUNT - 1U) % AC97_BDL_COUNT;
    ac97_bm_write8(AC97_PO_LVI, (uint8_t)last);
}

static int ac97_queue_descriptor_locked(const uint8_t *data, uint32_t length)
{
    uint32_t index;
    uint8_t *buffer;
    uint32_t nonzero = 0;
    if (!ac97.bdl || !ac97.buffers || !length || length > AC97_BUFFER_BYTES ||
        (length & 3U) || ac97.queued_descriptors >= AC97_BDL_COUNT) {
        return -22;
    }
    index = ac97.write_index;
    buffer = ac97.buffers + index * AC97_BUFFER_BYTES;
    if (data) {
        ac97_copy(buffer, data, length);
        for (uint32_t offset = 0; offset < length; ++offset) {
            nonzero |= data[offset];
        }
    } else {
        ac97_zero(buffer, length);
    }
    if (length < AC97_BUFFER_BYTES) {
        ac97_zero(buffer + length, AC97_BUFFER_BYTES - length);
    }
    ac97.bdl[index] = (struct ac97_buffer_desc){
        .address = (uint32_t)(ac97.buffers_phys + index * AC97_BUFFER_BYTES),
        .samples = (uint16_t)(length / 2U),
        .control = AC97_BDL_IOC,
    };
    __sync_synchronize();
    ac97.slot_bytes[index] = (uint16_t)length;
    if (nonzero && !ac97.nonzero_reported) {
        ac97.nonzero_reported = 1U;
        ac97.nonzero_descriptor = index;
        kernel_api->console_write("[driver] ac97 first nonzero PCM descriptor queued\n");
    }
    ac97.write_index = (index + 1U) % AC97_BDL_COUNT;
    ++ac97.queued_descriptors;
    ac97.queued_bytes += length;
    __sync_synchronize();
    ac97_program_lvi_locked();
    return 0;
}

static void ac97_refresh_locked(void)
{
    uint16_t status;
    uint32_t civ;
    uint32_t picb;
    uint32_t completed;
    uint32_t consumed = 0;
    uint32_t index;
    uint32_t terminal;

    if (!ac97.stream_started) {
        return;
    }
    status = ac97_bm_read16(AC97_PO_SR);
    civ = ac97_bm_read8(AC97_PO_CIV) % AC97_BDL_COUNT;
    picb = ac97_bm_read16(AC97_PO_PICB);
    ac97.hw_civ = civ;
    ac97.hw_picb = picb;
    if (status & AC97_PO_SR_CLEAR) {
        ac97_bm_write16(AC97_PO_SR, (uint16_t)(status & AC97_PO_SR_CLEAR));
    }
    if (!ac97.running) {
        ac97.running = (status & AC97_PO_SR_DCH) == 0U;
        ac97.last_hw_civ = civ;
        ac97.last_hw_picb = picb;
        if (!ac97.running) {
            ac97_refill_silence_locked();
        }
        return;
    }
    completed = ac97_civ_distance(ac97.last_hw_civ, civ);
    /* AC'97 leaves CIV pointing at the last descriptor when it reaches LVI.
     * PICB=0 plus DCH therefore represents one additional completed
     * descriptor even though CIV did not advance. */
    terminal = (status & AC97_PO_SR_DCH) && picb == 0U &&
               ac97.queued_descriptors > completed;
    if (terminal) {
        ++completed;
    }
    if (completed > ac97.queued_descriptors) {
        ++ac97.underruns;
        if (!ac97.underrun_reported) {
            kernel_api->console_write("[driver] ac97 DMA underrun; refilling silence\n");
            ac97.underrun_reported = 1U;
        }
        /* The hardware may still be running on the descriptor at CIV.  Drop
         * only software ownership and continue from the descriptor after it;
         * resetting the controller here would turn a transient underrun into
         * an audible click on every producer call. */
        ac97.queued_bytes = 0;
        ac97.queued_descriptors = 0;
        ac97.write_index = (civ + 1U) % AC97_BDL_COUNT;
        ac97_zero(ac97.slot_bytes, sizeof(ac97.slot_bytes));
        ac97.stream_started = 1U;
        ac97.running = (status & AC97_PO_SR_DCH) == 0U;
        ac97.last_hw_civ = civ;
        ac97.last_hw_picb = picb;
        ac97_refill_silence_locked();
        return;
    }
    if (completed) {
        index = ac97.last_hw_civ;
        consumed = ac97.last_hw_picb ? (ac97.last_hw_picb * 2U) :
                   ac97.slot_bytes[index];
        if (consumed > ac97.slot_bytes[index]) {
            consumed = ac97.slot_bytes[index];
        }
        for (uint32_t count = 0; count < completed; ++count) {
            index = (ac97.last_hw_civ + count) % AC97_BDL_COUNT;
            if (count) {
                consumed += ac97.slot_bytes[index];
            }
            ac97.slot_bytes[index] = 0;
            if (index == ac97.nonzero_descriptor &&
                ac97.nonzero_reported && !ac97.nonzero_consumed_reported) {
                kernel_api->console_write("[driver] ac97 first nonzero DMA descriptor consumed\n");
                ac97.nonzero_consumed_reported = 1U;
            }
            if (ac97.queued_descriptors) {
                --ac97.queued_descriptors;
            }
        }
    } else if (picb < ac97.last_hw_picb) {
        consumed = (ac97.last_hw_picb - picb) * 2U;
        if (consumed > ac97.queued_bytes) {
            consumed = ac97.queued_bytes;
        }
    }
    if (consumed >= ac97.queued_bytes) {
        ac97.queued_bytes = 0;
    } else {
        ac97.queued_bytes -= consumed;
    }
    ac97.last_hw_civ = civ;
    ac97.last_hw_picb = picb;
    if (status & AC97_PO_SR_DCH) {
        ac97.running = 0;
        /* Keep the controller fed even when the producer missed an entire
         * queue window.  The next write is appended after this silent lead,
         * so it is never discarded and DMA is not repeatedly reset. */
        ac97_refill_silence_locked();
    }
}

static int ac97_start_locked(void)
{
    uint16_t status;
    uint32_t attempt;
    if (!ac97.queued_descriptors || ac97.running) {
        return 0;
    }
    kernel_api->outl((uint16_t)(ac97.bus_master_port + AC97_PO_BDBAR),
                     (uint32_t)ac97.bdl_phys);
    ac97_program_lvi_locked();
    ac97_bm_write16(AC97_PO_SR, AC97_PO_SR_CLEAR);
    ac97_bm_write8(AC97_PO_CR, AC97_PO_CR_RUN);
    status = AC97_PO_SR_DCH;
    for (attempt = 0; attempt < AC97_START_WAIT_SPINS; ++attempt) {
        status = ac97_bm_read16(AC97_PO_SR);
        if (!(status & AC97_PO_SR_DCH)) {
            break;
        }
        ac97_pause();
    }
    ac97.running = (status & AC97_PO_SR_DCH) == 0U;
    ac97.stream_started = ac97.running ? 1U : ac97.stream_started;
    ac97.last_hw_civ = ac97_bm_read8(AC97_PO_CIV) % AC97_BDL_COUNT;
    ac97.last_hw_picb = ac97_bm_read16(AC97_PO_PICB);
    ac97.hw_civ = ac97.last_hw_civ;
    ac97.hw_picb = ac97.last_hw_picb;
    if (ac97.running) {
        kernel_api->console_write("[driver] ac97 DMA ring started (loop, period=2048)\n");
        ac97_log_position(status);
    } else {
        kernel_api->console_write("[driver] ac97 DMA start rejected\n");
    }
    return ac97.running ? 0 : -5;
}

static void ac97_refill_silence_locked(void)
{
    while (ac97.queued_descriptors < AC97_LEAD_DESCRIPTORS &&
           ac97.queued_descriptors < AC97_BDL_COUNT) {
        if (ac97_queue_descriptor_locked(0, AC97_BUFFER_BYTES) < 0) {
            break;
        }
    }
    if (!ac97.running && ac97.queued_descriptors) {
        (void)ac97_start_locked();
    }
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
    ac97_lock();
    ac97_refresh_locked();
    if (!ac97.stream_started && !ac97.queued_descriptors) {
        /* Prime two complete periods before exposing the stream to the
         * controller.  This gives the first real PCM block a stable lead and
         * prevents a just-started DMA engine from racing the producer. */
        while (ac97.queued_descriptors < 2U) {
            if (ac97_queue_descriptor_locked(0, AC97_BUFFER_BYTES) < 0) {
                break;
            }
        }
    }
    while (written < length) {
        uint32_t chunk;
        if (ac97.queued_descriptors >= AC97_BDL_COUNT) {
            ++ac97.overruns;
            if (!ac97.overrun_reported) {
                kernel_api->console_write("[driver] ac97 DMA queue full; short write\n");
                ac97.overrun_reported = 1U;
            }
            break;
        }
        chunk = length - written;
        if (chunk > AC97_BUFFER_BYTES) {
            chunk = AC97_BUFFER_BYTES;
        }
        chunk &= ~3U;
        if (!chunk || ac97_queue_descriptor_locked(bytes + written, chunk) < 0) {
            break;
        }
        written += chunk;
    }
    if (!ac97.running && ac97.queued_descriptors >= AC97_LEAD_DESCRIPTORS) {
        (void)ac97_start_locked();
    }
    if (out_status) {
        *out_status = written < length ? LEONOS_AUDIO_STATUS_WOULD_BLOCK
                                        : LEONOS_AUDIO_STATUS_OK;
    }
    ac97_unlock();
    return (long)written;
}

static void ac97_get_state(struct leonos_audio_state *out)
{
    if (!out) {
        return;
    }
    ac97_lock();
    ac97_refresh_locked();
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
    ac97_unlock();
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
    ac97_codec_initialize();
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
