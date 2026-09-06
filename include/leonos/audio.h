#ifndef LEONOS_AUDIO_H
#define LEONOS_AUDIO_H

#include <stdint.h>


#define LEONOS_AUDIO_MAX_WRITE (64U * 1024U)
/* Audio drivers poll hardware synchronously; keep each kernel call bounded. */
#define LEONOS_AUDIO_IO_SLICE_BYTES 4096U

#define LEONOS_AUDIO_STATUS_OK 0U
#define LEONOS_AUDIO_STATUS_NO_DEVICE 1U
#define LEONOS_AUDIO_STATUS_BAD_FORMAT 2U
#define LEONOS_AUDIO_STATUS_PLAYBACK_FAILED 3U
#define LEONOS_AUDIO_STATUS_WOULD_BLOCK 4U

struct leonos_audio_format {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t flags;
};

struct leonos_audio_write {
    const void *data;
    uint32_t length;
    uint32_t transferred;
    uint32_t status;
};

struct leonos_audio_state {
    uint32_t present;
    uint32_t active;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t queued_bytes;
    uint32_t underruns;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t reserved;
};

int leonos_audio_configure(const struct leonos_audio_format *format);
long leonos_audio_write(const void *data, uint32_t length,
                        uint32_t *out_status);
int leonos_audio_get_state(struct leonos_audio_state *state);

#endif
