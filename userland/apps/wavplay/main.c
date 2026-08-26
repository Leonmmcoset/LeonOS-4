#include <leonos/audio.h>
#include <leonos/fs.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>

#define WAVPLAY_BUFFER_BYTES 4096U
#define WAVPLAY_TONE_FRAMES 1024U
#define WAVPLAY_TEST_RATE 48000U
#define WAVPLAY_TEST_SECONDS 30U

static int16_t tone_samples[WAVPLAY_TONE_FRAMES * 2U];

struct wav_info {
    struct leonos_audio_format format;
    uint32_t data_bytes;
};

static void wait_audio_frames(uint32_t frames, uint32_t sample_rate)
{
    uint32_t milliseconds;
    if (!frames || !sample_rate) {
        return;
    }
    milliseconds = (uint32_t)(((uint64_t)frames * 1000ULL + sample_rate - 1ULL) /
                              sample_rate);
    if (milliseconds) {
        sleep_ms(milliseconds);
    }
}

static int write_audio_retry(const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t offset = 0;
    uint32_t waits = 0;
    while (offset < length) {
        uint32_t status = LEONOS_AUDIO_STATUS_PLAYBACK_FAILED;
        long written = leonos_audio_write(bytes + offset, length - offset, &status);
        if (written > 0 && (uint32_t)written <= length - offset) {
            offset += (uint32_t)written;
            waits = 0;
            continue;
        }
        if (written == 0 && status == LEONOS_AUDIO_STATUS_WOULD_BLOCK &&
            waits++ < 200U) {
            sleep_ms(5U);
            continue;
        }
        return -1;
    }
    return 0;
}

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int text_eq_n(const uint8_t *left, const char *right, uint32_t length)
{
    for (uint32_t index = 0; index < length; ++index) {
        if (!right[index] || left[index] != (uint8_t)right[index]) {
            return 0;
        }
    }
    return right[length] == 0;
}

static int read_exact(int fd, void *buffer, uint32_t length)
{
    uint8_t *out = (uint8_t *)buffer;
    uint32_t offset = 0;
    while (offset < length) {
        long got = read(fd, out + offset, length - offset);
        if (got <= 0) {
            return -1;
        }
        offset += (uint32_t)got;
    }
    return 0;
}

static int skip_bytes(int fd, uint32_t length)
{
    return lseek(fd, (long)length, LEONOS_SEEK_CUR) < 0 ? -1 : 0;
}

static int parse_wav_header(int fd, struct wav_info *out)
{
    uint8_t header[12];
    uint8_t chunk[8];
    uint8_t format[16];
    uint8_t found_format = 0;
    if (!out || read_exact(fd, header, sizeof(header)) < 0 ||
        !text_eq_n(header, "RIFF", 4) || !text_eq_n(header + 8, "WAVE", 4)) {
        return -1;
    }
    *out = (struct wav_info){0};
    for (;;) {
        uint32_t chunk_size;
        if (read_exact(fd, chunk, sizeof(chunk)) < 0) {
            return -1;
        }
        chunk_size = read_le32(chunk + 4);
        if (text_eq_n(chunk, "fmt ", 4)) {
            if (chunk_size < sizeof(format) || read_exact(fd, format, sizeof(format)) < 0 ||
                read_le16(format) != 1U || read_le16(format + 2) != 2U ||
                read_le16(format + 14) != 16U ||
                skip_bytes(fd, chunk_size - sizeof(format)) < 0) {
                return -1;
            }
            out->format = (struct leonos_audio_format){
                .sample_rate = read_le32(format + 4),
                .channels = read_le16(format + 2),
                .bits_per_sample = read_le16(format + 14),
                .flags = 0,
            };
            found_format = 1;
        } else if (text_eq_n(chunk, "data", 4)) {
            if (!found_format) {
                return -1;
            }
            out->data_bytes = chunk_size;
            return 0;
        } else if (skip_bytes(fd, chunk_size) < 0) {
            return -1;
        }
        if (chunk_size & 1U && skip_bytes(fd, 1U) < 0) {
            return -1;
        }
    }
}

static int play_wav(const char *path)
{
    struct wav_info info;
    struct leonos_audio_state state;
    uint8_t *buffer;
    uint32_t remaining;
    int fd;
    int ret;
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        printf("wavplay: open failed %d\n", fd);
        return 1;
    }
    if (parse_wav_header(fd, &info) < 0) {
        puts("wavplay: supported files are PCM, stereo, 16-bit WAV");
        close(fd);
        return 1;
    }
    ret = leonos_audio_configure(&info.format);
    if (ret < 0) {
        printf("wavplay: audio format rejected %d\n", ret);
        close(fd);
        return 1;
    }
    state = (struct leonos_audio_state){0};
    leonos_audio_get_state(&state);
    printf("wavplay: %u Hz stereo PCM through %04x:%04x\n",
           info.format.sample_rate, state.vendor_id, state.device_id);
    buffer = malloc(WAVPLAY_BUFFER_BYTES + 4U);
    if (!buffer) {
        puts("wavplay: out of memory");
        close(fd);
        return 1;
    }
    remaining = info.data_bytes;
    while (remaining) {
        uint32_t wanted = remaining > WAVPLAY_BUFFER_BYTES
                              ? WAVPLAY_BUFFER_BYTES
                              : remaining;
        long got = read(fd, buffer, wanted);
        uint32_t padded;
        if (got <= 0) {
            puts("wavplay: truncated WAV data");
            free(buffer);
            close(fd);
            return 1;
        }
        padded = ((uint32_t)got + 3U) & ~3U;
        while ((uint32_t)got < padded) {
            buffer[got++] = 0;
        }
        if (write_audio_retry(buffer, padded) < 0) {
            puts("wavplay: playback failed");
            free(buffer);
            close(fd);
            return 1;
        }
        wait_audio_frames(padded / 4U, info.format.sample_rate);
        remaining -= (uint32_t)got > remaining ? remaining : (uint32_t)got;
    }
    free(buffer);
    close(fd);
    puts("wavplay: complete");
    return 0;
}

static int play_square_note(uint32_t period, uint32_t frames)
{
    uint32_t phase = 0;
    while (frames) {
        uint32_t count = frames > WAVPLAY_TONE_FRAMES ? WAVPLAY_TONE_FRAMES : frames;
        for (uint32_t index = 0; index < count; ++index) {
            int16_t value = phase < period / 2U ? 6000 : -6000;
            tone_samples[index * 2U] = value;
            tone_samples[index * 2U + 1U] = value;
            phase = (phase + 1U) % period;
        }
        if (write_audio_retry(tone_samples, count * 4U) < 0) {
            return -1;
        }
        wait_audio_frames(count, WAVPLAY_TEST_RATE);
        frames -= count;
    }
    return 0;
}

static int play_test_melody(void)
{
    struct leonos_audio_format format = {
        .sample_rate = WAVPLAY_TEST_RATE,
        .channels = 2U,
        .bits_per_sample = 16U,
        .flags = 0,
    };
    if (leonos_audio_configure(&format) < 0) {
        puts("wavplay: PCM audio device is unavailable");
        return 1;
    }
    puts("wavplay: playing 30-second 48000 Hz PCM tone");
    if (play_square_note(50U, WAVPLAY_TEST_RATE * WAVPLAY_TEST_SECONDS) < 0) {
        puts("wavplay: PCM tone failed");
        return 1;
    }
    puts("wavplay: PCM tone complete");
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    (void)envp;
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        return play_wav(argv[1]);
    }
    return play_test_melody();
}
