#include <leonos/audio.h>
#include <leonos/syscall.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "doomgeneric.h"
#include "i_sound.h"
#include "m_argv.h"
#include "w_wad.h"
#include "z_zone.h"

#define LEONOS_DOOM_AUDIO_RATE 48000U
#define LEONOS_DOOM_AUDIO_CHANNELS 2U
#define LEONOS_DOOM_AUDIO_BITS 16U
#define LEONOS_DOOM_AUDIO_TICK_HZ 35U
#define LEONOS_DOOM_AUDIO_FRAMES (LEONOS_DOOM_AUDIO_RATE / LEONOS_DOOM_AUDIO_TICK_HZ)
#define LEONOS_DOOM_AUDIO_MAX_MIX_FRAMES 5120U /* just over 100 ms at 48 kHz */
#define LEONOS_DOOM_AUDIO_MIX_CHANNELS 16U
#define LEONOS_DOOM_AUDIO_MAX_SAMPLE_FRAMES (LEONOS_DOOM_AUDIO_RATE * 8U)
#define LEONOS_DOOM_AUDIO_MUS_CHANNELS 16U
#define LEONOS_DOOM_AUDIO_MUS_TICKS_PER_TICK 4U
#define LEONOS_DOOM_AUDIO_MUS_VOICES 16U
#define LEONOS_DOOM_AUDIO_SAMPLE_CACHE 256U
#define LEONOS_DOOM_MIDI_MAX_TRACKS 32U

struct leonos_doom_sample {
    int16_t *pcm;
    uint32_t frames;
    uint32_t source_rate;
};

struct leonos_doom_channel {
    struct leonos_doom_sample *sample;
    uint32_t position;
    uint32_t step;
    uint16_t volume;
    uint16_t separation;
    uint8_t active;
};

struct leonos_doom_music_handle {
    const uint8_t *data;
    uint32_t length;
};

struct leonos_doom_music_voice {
    uint32_t phase;
    uint32_t step;
    uint16_t volume;
    uint8_t active;
    uint8_t percussion;
    uint8_t program;
    uint8_t channel;
    uint8_t note;
    uint8_t releasing;
    uint8_t envelope_stage;
    uint16_t envelope;
};

struct leonos_doom_midi_track {
    uint32_t start;
    uint32_t end;
    uint32_t position;
    uint32_t delay;
    uint8_t running_status;
    uint8_t ended;
};

struct leonos_doom_music_state {
    struct leonos_doom_music_handle *handle;
    uint32_t position;
    uint32_t score_start;
    uint32_t delay;
    uint8_t looping;
    uint8_t paused;
    uint8_t active;
    uint8_t volume;
    uint8_t format;
    uint16_t division;
    uint32_t tempo;
    uint64_t tick_remainder;
    uint8_t track_count;
    uint8_t programs[LEONOS_DOOM_AUDIO_MUS_CHANNELS];
    struct leonos_doom_midi_track tracks[LEONOS_DOOM_MIDI_MAX_TRACKS];
    struct leonos_doom_music_voice voices[LEONOS_DOOM_AUDIO_MUS_VOICES];
};

static struct leonos_audio_format leonos_doom_audio_format = {
    .sample_rate = LEONOS_DOOM_AUDIO_RATE,
    .channels = LEONOS_DOOM_AUDIO_CHANNELS,
    .bits_per_sample = LEONOS_DOOM_AUDIO_BITS,
    .flags = 0,
};
static uint8_t leonos_doom_audio_available;
static uint8_t leonos_doom_audio_reported;
static uint8_t leonos_doom_sound_initialized;
static uint8_t leonos_doom_music_only;
static uint8_t leonos_doom_use_sfx_prefix;
static uint8_t leonos_doom_mix_reported;
static uint8_t leonos_doom_music_reported;
static uint8_t leonos_doom_song_reported;
static uint8_t leonos_doom_midi_note_reported;
static uint8_t leonos_doom_midi_activity_reported;
static int16_t leonos_doom_mix[LEONOS_DOOM_AUDIO_MAX_MIX_FRAMES * 2U];
static int32_t leonos_doom_mix_left[LEONOS_DOOM_AUDIO_MAX_MIX_FRAMES];
static int32_t leonos_doom_mix_right[LEONOS_DOOM_AUDIO_MAX_MIX_FRAMES];
static uint8_t leonos_doom_pending[LEONOS_DOOM_AUDIO_MAX_MIX_FRAMES * 4U];
static uint32_t leonos_doom_pending_bytes;
static struct leonos_doom_channel leonos_doom_channels[LEONOS_DOOM_AUDIO_MIX_CHANNELS];
static struct leonos_doom_sample *leonos_doom_sample_cache[LEONOS_DOOM_AUDIO_SAMPLE_CACHE];
static uint32_t leonos_doom_sample_cache_count;
static struct leonos_doom_music_state leonos_doom_music;
static uint32_t leonos_doom_last_audio_ms;
static uint32_t leonos_doom_audio_frame_remainder;

/* Kept for the shared DOOM configuration ABI; LeonOS uses nearest-neighbour
 * conversion in this backend and never links libsamplerate. */
int use_libsamplerate;
float libsamplerate_scale = 0.65f;

static void leonos_doom_audio_log_unavailable(void)
{
    if (!leonos_doom_audio_reported) {
        printf("[doom] no PCM audio device; continuing silently\n");
        leonos_doom_audio_reported = 1;
    }
}

static void leonos_doom_audio_log_once(uint8_t *flag, const char *message)
{
    if (!*flag) {
        printf("%s\n", message);
        *flag = 1U;
    }
}

static int16_t leonos_doom_clamp16(int32_t value)
{
    if (value < -32768) {
        return -32768;
    }
    if (value > 32767) {
        return 32767;
    }
    return (int16_t)value;
}

static void leonos_doom_sample_free(struct leonos_doom_sample *sample)
{
    if (!sample) {
        return;
    }
    free(sample->pcm);
    free(sample);
}

static struct leonos_doom_sample *leonos_doom_sample_load(sfxinfo_t *sfx)
{
    uint8_t *data;
    uint32_t lump_length;
    uint32_t source_rate;
    uint32_t source_frames;
    uint32_t output_frames;
    struct leonos_doom_sample *sample;
    uint32_t i;

    if (!sfx || sfx->lumpnum < 0) {
        return NULL;
    }
    data = (uint8_t *)W_CacheLumpNum((unsigned int)sfx->lumpnum, PU_STATIC);
    lump_length = (uint32_t)W_LumpLength((unsigned int)sfx->lumpnum);
    if (!data || lump_length < 40U || data[0] != 3U || data[1] != 0U) {
        if (data) {
            W_ReleaseLumpNum((unsigned int)sfx->lumpnum);
        }
        return NULL;
    }
    source_rate = (uint32_t)data[2] | ((uint32_t)data[3] << 8);
    source_frames = (uint32_t)data[4] |
                    ((uint32_t)data[5] << 8) |
                    ((uint32_t)data[6] << 16) |
                    ((uint32_t)data[7] << 24);
    if (!source_rate || source_frames <= 32U || source_frames > lump_length - 8U) {
        W_ReleaseLumpNum((unsigned int)sfx->lumpnum);
        return NULL;
    }
    source_frames -= 32U;
    if (source_frames > LEONOS_DOOM_AUDIO_MAX_SAMPLE_FRAMES) {
        source_frames = LEONOS_DOOM_AUDIO_MAX_SAMPLE_FRAMES;
    }
    output_frames = (uint32_t)(((uint64_t)source_frames * LEONOS_DOOM_AUDIO_RATE) /
                               source_rate);
    if (!output_frames || output_frames > LEONOS_DOOM_AUDIO_MAX_SAMPLE_FRAMES) {
        W_ReleaseLumpNum((unsigned int)sfx->lumpnum);
        return NULL;
    }
    sample = (struct leonos_doom_sample *)calloc(1, sizeof(*sample));
    if (!sample) {
        W_ReleaseLumpNum((unsigned int)sfx->lumpnum);
        return NULL;
    }
    sample->pcm = (int16_t *)malloc((size_t)output_frames * sizeof(*sample->pcm));
    if (!sample->pcm) {
        free(sample);
        W_ReleaseLumpNum((unsigned int)sfx->lumpnum);
        return NULL;
    }
    sample->frames = output_frames;
    sample->source_rate = source_rate;
    /* DMX leaves a 16-byte lead-in and a trailing 16-byte guard area. */
    data += 24U;
    for (i = 0; i < output_frames; ++i) {
        uint32_t source = (uint32_t)(((uint64_t)i * source_frames) / output_frames);
        int32_t value = ((int32_t)data[source] - 128) << 8;
        sample->pcm[i] = (int16_t)value;
    }
    if (leonos_doom_sample_cache_count < LEONOS_DOOM_AUDIO_SAMPLE_CACHE) {
        leonos_doom_sample_cache[leonos_doom_sample_cache_count++] = sample;
    }
    W_ReleaseLumpNum((unsigned int)sfx->lumpnum);
    return sample;
}

static void leonos_doom_sfx_name(sfxinfo_t *sfx, char *name, size_t capacity)
{
    const char *source;
    if (sfx && sfx->link) {
        sfx = sfx->link;
    }
    source = sfx ? sfx->name : "";
    if (leonos_doom_use_sfx_prefix) {
        snprintf(name, capacity, "ds%s", source);
    } else {
        snprintf(name, capacity, "%s", source);
    }
}

static int leonos_doom_get_sfx_lump(sfxinfo_t *sfx)
{
    char name[16];
    leonos_doom_sfx_name(sfx, name, sizeof(name));
    return W_GetNumForName(name);
}

static boolean leonos_doom_init_sound(boolean use_sfx_prefix)
{
    int ret;
    struct leonos_audio_state state;
    leonos_doom_use_sfx_prefix = use_sfx_prefix ? 1U : 0U;
    leonos_doom_sound_initialized = 1U;
    leonos_doom_music_only = 0U;
    ret = leonos_audio_configure(&leonos_doom_audio_format);
    if (ret < 0) {
        printf("[doom] PCM configure failed ret=%d rate=%u\n", ret,
               leonos_doom_audio_format.sample_rate);
        leonos_doom_audio_available = 0;
        leonos_doom_audio_log_unavailable();
        return true;
    }
    leonos_doom_audio_available = 1;
    printf("[doom] PCM audio enabled: 48000 Hz stereo signed-16\n");
    state = (struct leonos_audio_state){0};
    (void)leonos_audio_get_state(&state);
    return true;
}

static void leonos_doom_shutdown_sound(void)
{
    uint32_t i;
    for (i = 0; i < LEONOS_DOOM_AUDIO_MIX_CHANNELS; ++i) {
        leonos_doom_channels[i].sample = NULL;
        leonos_doom_channels[i].active = 0;
    }
    for (i = 0; i < leonos_doom_sample_cache_count; ++i) {
        leonos_doom_sample_free(leonos_doom_sample_cache[i]);
        leonos_doom_sample_cache[i] = NULL;
    }
    leonos_doom_sample_cache_count = 0;
    leonos_doom_sound_initialized = 0;
    leonos_doom_audio_available = 0;
    leonos_doom_last_audio_ms = 0;
    leonos_doom_audio_frame_remainder = 0;
    leonos_doom_pending_bytes = 0;
}

static void leonos_doom_cache_sounds(sfxinfo_t *sounds, int count)
{
    (void)sounds;
    (void)count;
}

static int leonos_doom_start_sound(sfxinfo_t *sfx, int channel, int volume, int separation)
{
    struct leonos_doom_sample *sample;
    struct leonos_doom_channel *out;
    if (!sfx || channel < 0 || channel >= (int)LEONOS_DOOM_AUDIO_MIX_CHANNELS) {
        return 0;
    }
    if (sfx->lumpnum < 0) {
        sfx->lumpnum = leonos_doom_get_sfx_lump(sfx);
    }
    sample = (struct leonos_doom_sample *)sfx->driver_data;
    if (!sample) {
        sample = leonos_doom_sample_load(sfx);
        if (!sample) {
            return 0;
        }
        sfx->driver_data = sample;
    }
    out = &leonos_doom_channels[channel];
    out->sample = sample;
    out->position = 0;
    out->step = 1U << 16;
    out->volume = (uint16_t)(volume < 0 ? 0 : volume > 127 ? 127 : volume);
    out->separation = (uint16_t)(separation < 0 ? 0 : separation > 254 ? 254 : separation);
    out->active = 1;
    return channel;
}

static void leonos_doom_stop_sound(int channel)
{
    if (channel >= 0 && channel < (int)LEONOS_DOOM_AUDIO_MIX_CHANNELS) {
        leonos_doom_channels[channel].active = 0;
    }
}

static boolean leonos_doom_sound_playing(int channel)
{
    return channel >= 0 && channel < (int)LEONOS_DOOM_AUDIO_MIX_CHANNELS &&
           leonos_doom_channels[channel].active;
}

static void leonos_doom_update_sound_params(int channel, int volume, int separation)
{
    if (channel < 0 || channel >= (int)LEONOS_DOOM_AUDIO_MIX_CHANNELS) {
        return;
    }
    leonos_doom_channels[channel].volume =
        (uint16_t)(volume < 0 ? 0 : volume > 127 ? 127 : volume);
    leonos_doom_channels[channel].separation =
        (uint16_t)(separation < 0 ? 0 : separation > 254 ? 254 : separation);
}

static uint32_t leonos_doom_note_step(uint8_t note)
{
    static const uint16_t octave4[12] = {
        262U, 277U, 294U, 311U, 330U, 349U, 370U, 392U, 415U, 440U, 466U, 494U
    };
    uint32_t frequency = octave4[note % 12U];
    int octave = (int)(note / 12U) - 4;
    if (octave > 0) {
        while (octave--) frequency <<= 1;
    } else {
        while (octave++) frequency >>= 1;
    }
    if (!frequency) frequency = 1;
    return (uint32_t)(((uint64_t)frequency << 32) / LEONOS_DOOM_AUDIO_RATE);
}

static int32_t leonos_doom_wave(uint32_t phase, uint8_t percussion, uint8_t program)
{
    uint32_t value;
    if (percussion) {
        value = phase * 1103515245U + 12345U;
        return (int32_t)((value >> 16) & 0xffffU) - 32768;
    }
    if ((program % 3U) == 1U) {
        value = phase < 0x80000000U ? phase : 0xffffffffU - phase;
        return (int32_t)(value >> 15) - 16384;
    }
    if ((program % 3U) == 2U) {
        return phase < 0x80000000U ? 24000 : -24000;
    }
    if (phase & 0x80000000U) {
        return 32767 - (int32_t)((phase - 0x80000000U) >> 15);
    }
    return (int32_t)(phase >> 15) - 32768;
}

static void leonos_doom_music_stop_voices(void)
{
    memset(leonos_doom_music.voices, 0, sizeof(leonos_doom_music.voices));
}

static int leonos_doom_music_valid(const struct leonos_doom_music_handle *handle)
{
    if (!handle || !handle->data || handle->length < 4U) {
        return 0;
    }
    return (handle->length >= 16U && memcmp(handle->data, "MUS\x1a", 4) == 0) ||
           (handle->length >= 14U && memcmp(handle->data, "MThd", 4) == 0);
}

static uint16_t leonos_doom_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t leonos_doom_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int leonos_doom_midi_vlq(const uint8_t *data, uint32_t end,
                                uint32_t *position, uint32_t *value)
{
    uint32_t result = 0;
    uint32_t count = 0;
    uint8_t byte;
    if (!data || !position || !value) return -1;
    do {
        if (*position >= end || count++ == 4U) return -1;
        byte = data[(*position)++];
        result = (result << 7) | (byte & 0x7fU);
    } while (byte & 0x80U);
    *value = result;
    return 0;
}

static int leonos_doom_midi_prepare_track(struct leonos_doom_music_state *music,
                                          struct leonos_doom_midi_track *track)
{
    uint32_t delta;
    track->position = track->start;
    track->running_status = 0;
    track->ended = 0;
    if (leonos_doom_midi_vlq(music->handle->data, track->end,
                             &track->position, &delta) < 0) return -1;
    track->delay = delta;
    return 0;
}

static int leonos_doom_midi_next_delta(struct leonos_doom_music_state *music,
                                       struct leonos_doom_midi_track *track)
{
    uint32_t delta;
    if (track->position >= track->end ||
        leonos_doom_midi_vlq(music->handle->data, track->end,
                             &track->position, &delta) < 0) {
        track->ended = 1;
        return -1;
    }
    track->delay = delta;
    return 0;
}

static void leonos_doom_midi_note_off(uint8_t channel, uint8_t note)
{
    uint32_t index;
    for (index = 0; index < LEONOS_DOOM_AUDIO_MUS_VOICES; ++index) {
        struct leonos_doom_music_voice *voice = &leonos_doom_music.voices[index];
        if (voice->active && voice->channel == channel && voice->note == note) {
            voice->releasing = 1U;
            voice->envelope_stage = 2U;
        }
    }
}

static void leonos_doom_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
    struct leonos_doom_music_voice *voice = NULL;
    uint32_t index;
    if (!velocity) {
        leonos_doom_midi_note_off(channel, note);
        return;
    }
    for (index = 0; index < LEONOS_DOOM_AUDIO_MUS_VOICES; ++index) {
        if (leonos_doom_music.voices[index].active &&
            leonos_doom_music.voices[index].channel == channel &&
            leonos_doom_music.voices[index].note == note) {
            voice = &leonos_doom_music.voices[index];
            break;
        }
    }
    if (!voice) {
        for (index = 0; index < LEONOS_DOOM_AUDIO_MUS_VOICES; ++index) {
            if (!leonos_doom_music.voices[index].active) {
                voice = &leonos_doom_music.voices[index];
                break;
            }
        }
    }
    if (!voice) {
        /* Steal the first releasing voice, then the oldest slot. */
        for (index = 0; index < LEONOS_DOOM_AUDIO_MUS_VOICES; ++index) {
            if (leonos_doom_music.voices[index].releasing) {
                voice = &leonos_doom_music.voices[index];
                break;
            }
        }
    }
    if (!voice) {
        voice = &leonos_doom_music.voices[channel % LEONOS_DOOM_AUDIO_MUS_VOICES];
    }
    voice->step = leonos_doom_note_step(note);
    voice->phase = 0;
    voice->volume = velocity;
    voice->channel = channel;
    voice->note = note;
    voice->program = leonos_doom_music.programs[channel];
    voice->envelope = 0U;
    voice->releasing = 0;
    voice->envelope_stage = 0U;
    voice->active = 1;
    voice->percussion = channel == 9U;
    leonos_doom_audio_log_once(&leonos_doom_midi_note_reported,
                               "[doom] MIDI first note-on");
}

static int leonos_doom_midi_event(struct leonos_doom_music_state *music,
                                  struct leonos_doom_midi_track *track)
{
    const uint8_t *data = music->handle->data;
    uint8_t status;
    uint8_t channel;
    uint8_t type;
    uint32_t p = track->position;
    if (p >= track->end) return -1;
    status = data[p++];
    if (status < 0x80U) {
        if (!track->running_status) return -1;
        status = track->running_status;
        --p;
    } else if (status < 0xf0U) {
        track->running_status = status;
    }
    if (status == 0xffU) {
        uint8_t meta;
        uint32_t size;
        if (p >= track->end) return -1;
        meta = data[p++];
        if (leonos_doom_midi_vlq(data, track->end, &p, &size) < 0 ||
            size > track->end - p) return -1;
        if (meta == 0x51U && size == 3U) {
            music->tempo = ((uint32_t)data[p] << 16) |
                           ((uint32_t)data[p + 1U] << 8) | data[p + 2U];
            if (!music->tempo) music->tempo = 500000U;
        }
        p += size;
        track->position = p;
        if (meta == 0x2fU) track->ended = 1U;
        return track->ended ? 1 : 0;
    }
    if (status == 0xf0U || status == 0xf7U) {
        uint32_t size;
        if (leonos_doom_midi_vlq(data, track->end, &p, &size) < 0 ||
            size > track->end - p) return -1;
        track->position = p + size;
        return 0;
    }
    if (status >= 0xf1U && status <= 0xfeU) {
        /* System-common/realtime messages do not carry a MIDI channel.  We
         * do not synthesize them, but consume their exact payload so a clock
         * or tune-request event cannot desynchronise the following notes. */
        uint32_t payload = (status == 0xf1U || status == 0xf3U) ? 1U :
                           (status == 0xf2U ? 2U : 0U);
        if (payload > track->end - p) return -1;
        track->position = p + payload;
        return 0;
    }
    channel = status & 0x0fU;
    type = status & 0xf0U;
    if (channel >= LEONOS_DOOM_AUDIO_MUS_CHANNELS) return -1;
    if (type == 0x80U || type == 0x90U) {
        if (p + 1U >= track->end) return -1;
        if (type == 0x80U) leonos_doom_midi_note_off(channel, data[p]);
        else leonos_doom_midi_note_on(channel, data[p], data[p + 1U]);
        p += 2U;
    } else if (type == 0xc0U || type == 0xd0U) {
        if (p >= track->end) return -1;
        if (type == 0xc0U) {
            leonos_doom_music.programs[channel] = data[p];
        }
        ++p;
    } else {
        if (p + 1U >= track->end) return -1;
        p += 2U;
    }
    track->position = p;
    return 0;
}

static void leonos_doom_midi_process_ready(void)
{
    uint32_t index;
    for (index = 0; index < leonos_doom_music.track_count; ++index) {
        struct leonos_doom_midi_track *track = &leonos_doom_music.tracks[index];
        uint32_t guard = 0;
        while (!track->ended && track->delay == 0U && guard++ < 128U) {
            int ret = leonos_doom_midi_event(&leonos_doom_music, track);
            if (ret < 0) {
                track->ended = 1U;
                break;
            }
            if (!track->ended) leonos_doom_midi_next_delta(&leonos_doom_music, track);
        }
    }
}

static int leonos_doom_midi_reset(void)
{
    const uint8_t *data = leonos_doom_music.handle->data;
    uint32_t header_size;
    uint16_t tracks;
    uint32_t position = 14U;
    uint32_t index;
    if (leonos_doom_music.handle->length < 14U || memcmp(data, "MThd", 4) != 0)
        return -1;
    header_size = leonos_doom_be32(data + 4U);
    if (header_size < 6U || header_size > leonos_doom_music.handle->length - 8U)
        return -1;
    tracks = leonos_doom_be16(data + 10U);
    leonos_doom_music.division = leonos_doom_be16(data + 12U);
    if (!tracks || !leonos_doom_music.division ||
        (leonos_doom_music.division & 0x8000U) ||
        tracks > LEONOS_DOOM_MIDI_MAX_TRACKS)
        return -1;
    position = 8U + header_size;
    leonos_doom_music.track_count = 0;
    for (index = 0; index < tracks; ++index) {
        uint32_t size;
        struct leonos_doom_midi_track *track;
        if (position + 8U > leonos_doom_music.handle->length ||
            memcmp(data + position, "MTrk", 4) != 0) return -1;
        size = leonos_doom_be32(data + position + 4U);
        position += 8U;
        if (size > leonos_doom_music.handle->length - position) return -1;
        track = &leonos_doom_music.tracks[leonos_doom_music.track_count++];
        track->start = position;
        track->end = position + size;
        if (leonos_doom_midi_prepare_track(&leonos_doom_music, track) < 0) return -1;
        position += size;
    }
    leonos_doom_music.tempo = 500000U;
    leonos_doom_music.tick_remainder = 0;
    memset(leonos_doom_music.programs, 0, sizeof(leonos_doom_music.programs));
    leonos_doom_music_stop_voices();
    printf("[doom] MIDI tracks=%u PPQN=%u\n", tracks,
           leonos_doom_music.division);
    leonos_doom_midi_process_ready();
    return 0;
}

static void leonos_doom_music_reset(void)
{
    const uint8_t *data;
    if (leonos_doom_music.handle && leonos_doom_music.handle->length >= 4U &&
        memcmp(leonos_doom_music.handle->data, "MThd", 4) == 0) {
        leonos_doom_music.active = 0;
        leonos_doom_music.format = 2U;
        if (leonos_doom_midi_reset() < 0) {
            leonos_doom_audio_log_once(&leonos_doom_music_reported,
                                       "[doom] MIDI song rejected");
            return;
        }
        leonos_doom_music.active = 1U;
        leonos_doom_music.paused = 0;
        leonos_doom_audio_log_once(&leonos_doom_music_reported,
                                   "[doom] MIDI song accepted and synthesizer active");
        return;
    }
    if (!leonos_doom_music_valid(leonos_doom_music.handle)) {
        leonos_doom_music.active = 0;
        leonos_doom_audio_log_once(&leonos_doom_music_reported,
                                   "[doom] MUS song rejected");
        return;
    }
    data = leonos_doom_music.handle->data;
    leonos_doom_music.score_start = (uint32_t)data[6] | ((uint32_t)data[7] << 8);
    if (leonos_doom_music.score_start < 16U ||
        leonos_doom_music.score_start >= leonos_doom_music.handle->length) {
        leonos_doom_music.active = 0;
        leonos_doom_audio_log_once(&leonos_doom_music_reported,
                                   "[doom] MUS score offset invalid");
        return;
    }
    leonos_doom_music.position = leonos_doom_music.score_start;
    leonos_doom_music.delay = 0;
    leonos_doom_music.tick_remainder = 0;
    leonos_doom_music.active = 1;
    leonos_doom_music.format = 1U;
    leonos_doom_music.paused = 0;
    leonos_doom_music.volume = 127U;
    leonos_doom_music_stop_voices();
    leonos_doom_audio_log_once(&leonos_doom_music_reported,
                               "[doom] MUS song accepted and synthesizer active");
}

static void leonos_doom_music_event(uint8_t descriptor)
{
    uint8_t channel = descriptor & 0x0fU;
    uint8_t type = (descriptor >> 4) & 0x07U;
    uint8_t note;
    uint8_t velocity;
    struct leonos_doom_music_voice *voice;
    if (channel >= LEONOS_DOOM_AUDIO_MUS_CHANNELS ||
        !leonos_doom_music.handle ||
        leonos_doom_music.position >= leonos_doom_music.handle->length) {
        return;
    }
    voice = &leonos_doom_music.voices[channel];
    if (type == 0U) {
        /* Release-note carries the note number even though this lightweight
         * synth tracks one active voice per MUS channel. */
        if (leonos_doom_music.position < leonos_doom_music.handle->length) {
            ++leonos_doom_music.position;
        }
        voice->active = 0;
    } else if (type == 1U) {
        note = leonos_doom_music.handle->data[leonos_doom_music.position++];
        velocity = (uint8_t)(voice->volume ? voice->volume : 100U);
        if (note & 0x80U) {
            note &= 0x7fU;
            if (leonos_doom_music.position < leonos_doom_music.handle->length) {
                velocity = leonos_doom_music.handle->data[leonos_doom_music.position++];
            }
        }
        voice->step = leonos_doom_note_step(note);
        voice->volume = velocity;
        voice->phase = 0;
        voice->active = 1;
        voice->percussion = channel == 9U;
        voice->releasing = 0;
        voice->envelope_stage = 1U;
        voice->envelope = 65535U;
    } else if (type == 2U) {
        if (leonos_doom_music.position < leonos_doom_music.handle->length) {
            ++leonos_doom_music.position;
        }
    } else if (type == 3U) {
        /* System event has one controller byte. */
        if (leonos_doom_music.position < leonos_doom_music.handle->length) {
            ++leonos_doom_music.position;
        }
    } else if (type == 4U) {
        if (leonos_doom_music.position + 1U < leonos_doom_music.handle->length) {
            leonos_doom_music.position += 2U;
        }
    }
}

static void leonos_doom_music_next_group(void)
{
    uint8_t descriptor;
    uint32_t delay = 0;
    uint8_t value;
    if (!leonos_doom_music.handle ||
        leonos_doom_music.position >= leonos_doom_music.handle->length) {
        if (leonos_doom_music.looping) {
            leonos_doom_music_reset();
        } else {
            leonos_doom_music.active = 0;
        }
        return;
    }
    do {
        descriptor = leonos_doom_music.handle->data[leonos_doom_music.position++];
        leonos_doom_music_event(descriptor);
    } while (!(descriptor & 0x80U) &&
             leonos_doom_music.position < leonos_doom_music.handle->length);
    if (descriptor & 0x80U) {
        do {
            if (leonos_doom_music.position >= leonos_doom_music.handle->length) {
                break;
            }
            value = leonos_doom_music.handle->data[leonos_doom_music.position++];
            delay = (delay << 7) | (value & 0x7fU);
        } while (value & 0x80U);
    }
    leonos_doom_music.delay = delay;
}

static void leonos_doom_music_advance(uint32_t frames)
{
    uint32_t i;
    if (!leonos_doom_music.active || leonos_doom_music.paused) {
        return;
    }
    if (leonos_doom_music.format == 2U) {
        leonos_doom_music.tick_remainder +=
            1000000ULL * (uint64_t)leonos_doom_music.division * frames;
        for (;;) {
            uint64_t threshold = (uint64_t)(leonos_doom_music.tempo ?
                                            leonos_doom_music.tempo : 500000U) *
                                 LEONOS_DOOM_AUDIO_RATE;
            if (leonos_doom_music.tick_remainder < threshold) {
                break;
            }
            leonos_doom_music.tick_remainder -= threshold;
            leonos_doom_midi_process_ready();
            for (i = 0; i < leonos_doom_music.track_count; ++i) {
                if (leonos_doom_music.tracks[i].delay) {
                    --leonos_doom_music.tracks[i].delay;
                }
            }
        }
        leonos_doom_midi_process_ready();
        for (i = 0; i < leonos_doom_music.track_count; ++i) {
            if (!leonos_doom_music.tracks[i].ended) break;
        }
        if (i == leonos_doom_music.track_count) {
            if (leonos_doom_music.looping) {
                leonos_doom_music_reset();
            } else {
                leonos_doom_music.active = 0;
            }
        }
        return;
    }
    leonos_doom_music.tick_remainder +=
        (uint64_t)frames * LEONOS_DOOM_AUDIO_MUS_TICKS_PER_TICK *
        LEONOS_DOOM_AUDIO_TICK_HZ;
    while (leonos_doom_music.tick_remainder >= LEONOS_DOOM_AUDIO_RATE) {
        leonos_doom_music.tick_remainder -= LEONOS_DOOM_AUDIO_RATE;
        if (!leonos_doom_music.delay) {
            leonos_doom_music_next_group();
        }
        if (leonos_doom_music.delay) {
            --leonos_doom_music.delay;
        }
    }
}

static void leonos_doom_mix_music(int32_t *left, int32_t *right, uint32_t frames)
{
    uint32_t i;
    uint32_t active_voices = 0;
    for (i = 0; i < LEONOS_DOOM_AUDIO_MUS_VOICES; ++i) {
        struct leonos_doom_music_voice *voice = &leonos_doom_music.voices[i];
        uint32_t frame;
        if (!voice->active) continue;
        ++active_voices;
        for (frame = 0; frame < frames; ++frame) {
            int32_t value = leonos_doom_wave(voice->phase, voice->percussion,
                                              voice->program);
            value = (value * voice->volume * leonos_doom_music.volume * voice->envelope) /
                    (127LL * 127LL * 3LL * 65535LL);
            left[frame] += value;
            right[frame] += value;
            voice->phase += voice->step;
            if (voice->envelope_stage == 0U) {
                if (voice->envelope < 62000U) voice->envelope += 3500U;
                else {
                    voice->envelope = 65535U;
                    voice->envelope_stage = 1U;
                }
            } else if (voice->releasing) {
                if (voice->envelope > 1200U) voice->envelope -= 1200U;
                else {
                    voice->envelope = 0;
                    voice->active = 0;
                }
            }
        }
    }
    if (active_voices && !leonos_doom_midi_activity_reported) {
        printf("[doom] MIDI active voices=%u\n", active_voices);
        leonos_doom_midi_activity_reported = 1U;
    }
}

static int leonos_doom_flush_pending(void)
{
    while (leonos_doom_pending_bytes) {
        uint32_t status = LEONOS_AUDIO_STATUS_PLAYBACK_FAILED;
        long written = leonos_audio_write(leonos_doom_pending,
                                          leonos_doom_pending_bytes, &status);
        if (written == 0 && status == LEONOS_AUDIO_STATUS_WOULD_BLOCK) {
            return 1;
        }
        if (written < 0 ||
            (status != LEONOS_AUDIO_STATUS_OK &&
             status != LEONOS_AUDIO_STATUS_WOULD_BLOCK)) {
            return -1;
        }
        if ((uint32_t)written > leonos_doom_pending_bytes) {
            return -1;
        }
        leonos_doom_pending_bytes -= (uint32_t)written;
        if (leonos_doom_pending_bytes) {
            memmove(leonos_doom_pending, leonos_doom_pending + written,
                    leonos_doom_pending_bytes);
        }
        if (status == LEONOS_AUDIO_STATUS_WOULD_BLOCK) {
            return 1;
        }
    }
    return 0;
}

static void leonos_doom_update_sound(void)
{
    int32_t *left = leonos_doom_mix_left;
    int32_t *right = leonos_doom_mix_right;
    uint32_t channel;
    uint32_t frame;
    uint32_t now;
    uint32_t frames;
    uint32_t elapsed;
    uint32_t output_bytes;
    long written;
    uint32_t status = LEONOS_AUDIO_STATUS_OK;
    uint8_t mixed = 0;
    /* Keep unsent data until the device has room.  Dropping a short write
     * produces exactly the symptom of a running DMA engine with silence. */
    if (leonos_doom_audio_available && leonos_doom_pending_bytes) {
        int flush = leonos_doom_flush_pending();
        if (flush < 0) {
            leonos_doom_audio_available = 0;
            leonos_doom_audio_log_unavailable();
            return;
        }
        if (flush > 0) {
            return;
        }
    }
    now = DG_GetTicksMs();
    if (leonos_doom_last_audio_ms == 0U) {
        frames = LEONOS_DOOM_AUDIO_FRAMES * 2U;
    } else {
        elapsed = (uint32_t)(now - leonos_doom_last_audio_ms);
        if (elapsed > 100U) {
            elapsed = 100U;
        }
        uint64_t frame_time = (uint64_t)elapsed * LEONOS_DOOM_AUDIO_RATE +
                              leonos_doom_audio_frame_remainder;
        frames = (uint32_t)(frame_time / 1000ULL);
        if (frames < 256U) {
            return;
        }
        leonos_doom_audio_frame_remainder = (uint32_t)(frame_time % 1000ULL);
    }
    if (frames > LEONOS_DOOM_AUDIO_MAX_MIX_FRAMES) {
        frames = LEONOS_DOOM_AUDIO_MAX_MIX_FRAMES;
    }
    leonos_doom_last_audio_ms = now;
    memset(left, 0, sizeof(leonos_doom_mix_left));
    memset(right, 0, sizeof(leonos_doom_mix_right));
    leonos_doom_music_advance(frames);
    for (channel = 0; channel < LEONOS_DOOM_AUDIO_MIX_CHANNELS; ++channel) {
        struct leonos_doom_channel *source = &leonos_doom_channels[channel];
        if (!source->active || !source->sample) continue;
        for (frame = 0; frame < frames; ++frame) {
            uint32_t index = source->position >> 16;
            int32_t value;
            if (index >= source->sample->frames) {
                source->active = 0;
                break;
            }
            value = source->sample->pcm[index] * (int32_t)source->volume / 127;
            left[frame] += value * (254 - source->separation) / 254;
            right[frame] += value * source->separation / 254;
            source->position += source->step;
        }
    }
    leonos_doom_mix_music(left, right, frames);
    for (frame = 0; frame < frames; ++frame) {
        if (left[frame] || right[frame]) {
            mixed = 1U;
        }
        leonos_doom_mix[frame * 2U] = leonos_doom_clamp16(left[frame]);
        leonos_doom_mix[frame * 2U + 1U] = leonos_doom_clamp16(right[frame]);
    }
    if (mixed) {
        leonos_doom_audio_log_once(&leonos_doom_mix_reported,
                                   "[doom] first nonzero mixed PCM block");
    }
    if (leonos_doom_audio_available) {
        output_bytes = frames * 4U;
        written = leonos_audio_write(leonos_doom_mix, output_bytes, &status);
        if (written < 0 ||
            (status != LEONOS_AUDIO_STATUS_OK &&
             status != LEONOS_AUDIO_STATUS_WOULD_BLOCK) ||
            (uint32_t)written > output_bytes) {
            leonos_doom_audio_available = 0;
            leonos_doom_audio_log_once(&leonos_doom_song_reported,
                                       "[doom] PCM submission failed");
            leonos_doom_audio_log_unavailable();
        } else if ((uint32_t)written < output_bytes) {
            leonos_doom_pending_bytes = output_bytes - (uint32_t)written;
            memcpy(leonos_doom_pending, (uint8_t *)leonos_doom_mix + written,
                   leonos_doom_pending_bytes);
        }
    }
}

static boolean leonos_doom_music_init(void)
{
    leonos_doom_music.volume = 127U;
    if (M_CheckParm("-nosound") > 0) {
        leonos_doom_music_only = 0;
        return true;
    }
    if (!leonos_doom_sound_initialized && !leonos_doom_audio_available) {
        if (leonos_audio_configure(&leonos_doom_audio_format) == 0) {
            leonos_doom_audio_available = 1;
        } else {
            leonos_doom_audio_log_unavailable();
        }
    }
    leonos_doom_music_only = leonos_doom_sound_initialized ? 0U : 1U;
    return true;
}

static void leonos_doom_music_shutdown(void)
{
    leonos_doom_music.active = 0;
    leonos_doom_music_stop_voices();
}

static void leonos_doom_music_set_volume(int volume)
{
    leonos_doom_music.volume = (uint8_t)(volume < 0 ? 0 : volume > 127 ? 127 : volume);
}

static void leonos_doom_music_pause(void)
{
    leonos_doom_music.paused = 1;
}

static void leonos_doom_music_resume(void)
{
    leonos_doom_music.paused = 0;
}

static void *leonos_doom_music_register(void *data, int length)
{
    struct leonos_doom_music_handle *handle;
    int valid_type;
    if (!data || length <= 0) return NULL;
    handle = (struct leonos_doom_music_handle *)calloc(1, sizeof(*handle));
    if (!handle) return NULL;
    handle->data = (const uint8_t *)data;
    handle->length = (uint32_t)length;
    valid_type = leonos_doom_music_valid(handle) ?
                 (memcmp(handle->data, "MThd", 4) == 0 ? 2 : 1) : 0;
    if (!valid_type) {
        leonos_doom_audio_log_once(&leonos_doom_song_reported,
                                   "[doom] registered song rejected");
    } else if (valid_type == 2) {
        leonos_doom_audio_log_once(&leonos_doom_song_reported,
                                   "[doom] MIDI song registered");
    } else {
        leonos_doom_audio_log_once(&leonos_doom_song_reported,
                                   "[doom] MUS lump registered");
    }
    return handle;
}

static void leonos_doom_music_unregister(void *opaque)
{
    struct leonos_doom_music_handle *handle = (struct leonos_doom_music_handle *)opaque;
    if (leonos_doom_music.handle == handle) {
        leonos_doom_music.active = 0;
        leonos_doom_music.handle = NULL;
    }
    free(handle);
}

static void leonos_doom_music_play(void *opaque, boolean looping)
{
    leonos_doom_music.handle = (struct leonos_doom_music_handle *)opaque;
    leonos_doom_music.looping = looping ? 1U : 0U;
    leonos_doom_music_reset();
    leonos_doom_audio_log_once(&leonos_doom_song_reported,
                               "[doom] song playback requested");
}

static void leonos_doom_music_stop(void)
{
    leonos_doom_music.active = 0;
    leonos_doom_music_stop_voices();
}

static boolean leonos_doom_music_playing(void)
{
    return leonos_doom_music.active && !leonos_doom_music.paused;
}

static void leonos_doom_music_poll(void)
{
    if (leonos_doom_music_only) {
        leonos_doom_update_sound();
    }
}

static snddevice_t leonos_doom_devices[] = {SNDDEVICE_SB, SNDDEVICE_GENMIDI};

sound_module_t DG_sound_module = {
    leonos_doom_devices,
    (int)(sizeof(leonos_doom_devices) / sizeof(leonos_doom_devices[0])),
    leonos_doom_init_sound,
    leonos_doom_shutdown_sound,
    leonos_doom_get_sfx_lump,
    leonos_doom_update_sound,
    leonos_doom_update_sound_params,
    leonos_doom_start_sound,
    leonos_doom_stop_sound,
    leonos_doom_sound_playing,
    leonos_doom_cache_sounds,
};

music_module_t DG_music_module = {
    leonos_doom_devices,
    (int)(sizeof(leonos_doom_devices) / sizeof(leonos_doom_devices[0])),
    leonos_doom_music_init,
    leonos_doom_music_shutdown,
    leonos_doom_music_set_volume,
    leonos_doom_music_pause,
    leonos_doom_music_resume,
    leonos_doom_music_register,
    leonos_doom_music_unregister,
    leonos_doom_music_play,
    leonos_doom_music_stop,
    leonos_doom_music_playing,
    leonos_doom_music_poll,
};
