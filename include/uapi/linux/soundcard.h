#ifndef LEONOS_UAPI_LINUX_SOUNDCARD_H
#define LEONOS_UAPI_LINUX_SOUNDCARD_H

/*
 * Deliberately small OSS soundcard UAPI.  LeonOS exposes playback through
 * /dev/dsp rather than the historical application-specific audio ioctls.
 * Keep these values/layouts compatible with Linux's legacy OSS ABI so ports
 * can use their normal PCM setup path.
 */
#include <linux/ioctl.h>

#define AFMT_QUERY   0x00000000
#define AFMT_U8      0x00000008
#define AFMT_S16_LE  0x00000010

struct audio_buf_info {
    int fragments;
    int fragstotal;
    int fragsize;
    int bytes;
};

#define SNDCTL_DSP_RESET       _IO('P', 0)
#define SNDCTL_DSP_SYNC        _IO('P', 1)
#define SNDCTL_DSP_SPEED       _IOWR('P', 2, int)
#define SNDCTL_DSP_STEREO      _IOWR('P', 3, int)
#define SNDCTL_DSP_GETBLKSIZE  _IOWR('P', 4, int)
#define SNDCTL_DSP_SETFMT      _IOWR('P', 5, int)
#define SOUND_PCM_READ_CHANNELS  _IOR('P', 6, int)
#define SOUND_PCM_WRITE_CHANNELS _IOWR('P', 6, int)
#define SNDCTL_DSP_CHANNELS SOUND_PCM_WRITE_CHANNELS
#define SOUND_PCM_READ_RATE    _IOR('P', 2, int)
#define SOUND_PCM_WRITE_RATE   _IOWR('P', 2, int)
#define SNDCTL_DSP_GETFMTS     _IOR('P', 11, int)
#define SNDCTL_DSP_GETOSPACE   _IOR('P', 12, struct audio_buf_info)
#define SNDCTL_DSP_NONBLOCK    _IO('P', 14)
#define SNDCTL_DSP_GETCAPS     _IOR('P', 15, int)
#define SNDCTL_DSP_GETODELAY   _IOR('P', 23, int)

#define DSP_CAP_TRIGGER 0x00001000
#define DSP_CAP_OUTPUT  0x00020000

#endif
