/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "modules.usbaudio_hal.hikey"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <log/log.h>
#include <cutils/list.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/audio.h>
#include <hardware/audio_alsaops.h>
#include <hardware/hardware.h>

#include <system/audio.h>

#include <tinyalsa/asoundlib.h>

#include "webrtc_wrapper.h"

#include <audio_utils/channels.h>
#include <audio_utils/resampler.h>

#include "alsa_device_profile.h"
#include "alsa_device_proxy.h"
#include "alsa_logging.h"

#define AUDIO_PARAMETER_HFP_ENABLE            "hfp_enable"
#define AUDIO_PARAMETER_HFP_SET_SAMPLING_RATE "hfp_set_sampling_rate"
#define AUDIO_PARAMETER_KEY_HFP_VOLUME        "hfp_volume"
#define AUDIO_PARAMETER_HFP_VOL_MIXER_CTL     "hfp_vol_mixer_ctl"
#define AUDIO_PARAMATER_HFP_VALUE_MAX         128
#define AUDIO_PARAMETER_KEY_HFP_MIC_VOLUME "hfp_mic_volume"

#define AUDIO_PARAMETER_CARD "card"

#define AUDIO_PARAMETER_LINEIN "line_in_ctl"

#define DEFAULT_INPUT_BUFFER_SIZE_MS 20

/* TODO
 * For multi-channel audio (> 2 channels)...
 * 
 * This USB device is going to be (as far as Android is concerned) configured as a STEREO
 * (2-channel) device, but could physically be connected to as many as 8 (7.1) speakers.
 *
 * When connecting to more than 2 speakers, the relationships between all the speakers
 * has to be managed manually, that means interleaving the multiple streams via duplication
 * and possibly performing operations on those individual streams.
 *
 * For instance, when we receive a STEREO stream, it will be interleaved as LRLRLRLRLR
 * or 0101010101. When we play a DUAL STEREO 4-channel stream, it will be interleaved
 * as 0123012301230123 where for each sample, 2==0 and 3==1. We have to copy the stream
 * manually. If our speaker topology includes CENTER and/or BASE, those two channels
 * will be MONO-blends of L and R, and in the case of BASE, also LPF.
 *
 * The USB ALSA mixer controls provide individual volume controls for all 8 output
 * channels, and all input channels. It also is able to playback input source directly to
 * output without having to transfer it up the USB and back, and is able to configure
 * playback channel mappings. This means that FM radio playback can be done as simply as
 * setting "Line Playback Switch" to 1.
 *
 *  # tinymix -D 1 -a
 *  Mixer name: 'USB Sound Device'
 *  Number of controls: 16
 *  ctl	type	num	name                                     value
 *  	range/values
 *  0	INT	8	Playback Channel Map                     0 0 0 0 0 0 0 0 (dsrange 0->36)
 *  1	INT	2	Capture Channel Map                      0 0 (dsrange 0->36)
 *  2	BOOL	1	Mic Playback Switch                      On
 *  3	INT	2	Mic Playback Volume                      8065 8065 (dsrange 0->8065)
 *  4	BOOL	1	Line Playback Switch                     On
 *  5	INT	2	Line Playback Volume                     6144 6144 (dsrange 0->8065)
 *  6	BOOL	1	Speaker Playback Switch                  On
 *  7	INT	8	Speaker Playback Volume                  197 100 100 100 0 0 0 0 (dsrange 0->197)
 *  8	BOOL	1	Mic Capture Switch                       On
 *  9	INT	2	Mic Capture Volume                       4096 4096 (dsrange 0->6928)
 *  10	BOOL	1	Line Capture Switch                      Off
 *  11	INT	2	Line Capture Volume                      4096 4096 (dsrange 0->6928)
 *  12	BOOL	1	IEC958 In Capture Switch                 Off
 *  13	BOOL	1	PCM Capture Switch                       On
 *  14	INT	2	PCM Capture Volume                       4096 4096 (dsrange 0->6928)
 *  15	ENUM	1	PCM Capture Source                       >Mic Line IEC958 In Mixer
 *
 * We can use AudioManager.setParameters() to feed configurations to this HAL, and either
 * some form of persistent storage (persistent system property? Or load on boot from
 * a boot completed receiver?)
 */


/* Lock play & record samples rates at or above this threshold */
#define RATELOCK_THRESHOLD 96000

struct audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock; /* see note below on mutex acquisition order */

    /* output */
    alsa_device_profile out_profile;
    struct listnode output_stream_list;

    /* input */
    alsa_device_profile in_profile;
    struct listnode input_stream_list;

    /* lock input & output sample rates */
    /*FIXME - How do we address multiple output streams? */
    uint32_t device_sample_rate;

    bool mic_muted;
    bool line_in;

    bool standby;

    int usbcard;
    int btcard;

    pthread_t sco_thread;
    pthread_mutex_t sco_thread_lock;

    struct pcm *sco_pcm_far_in;
    struct pcm *sco_pcm_far_out;
    struct pcm *sco_pcm_near_in;
    struct pcm *sco_pcm_near_out;

    int sco_samplerate;

    bool terminate_sco;

    struct mixer *hw_mixer;
    float *vol_balance;
    float master_volume;
};

struct stream_lock {
    pthread_mutex_t lock;               /* see note below on mutex acquisition order */
    pthread_mutex_t pre_lock;           /* acquire before lock to avoid DOS by playback thread */
};

struct stream_out {
    struct audio_stream_out stream;

    struct stream_lock  lock;

    bool standby;

    struct audio_device *adev;           /* hardware information - only using this for the lock */

    alsa_device_profile * profile;      /* Points to the alsa_device_profile in the audio_device */
    alsa_device_proxy proxy;            /* state of the stream */

    unsigned hal_channel_count;         /* channel count exposed to AudioFlinger.
                                         * This may differ from the device channel count when
                                         * the device is not compatible with AudioFlinger
                                         * capabilities, e.g. exposes too many channels or
                                         * too few channels. */
    audio_channel_mask_t hal_channel_mask;  /* USB devices deal in channel counts, not masks
                                             * so the proxy doesn't have a channel_mask, but
                                             * audio HALs need to talk about channel masks
                                             * so expose the one calculated by
                                             * adev_open_output_stream */

    struct listnode list_node;

    void * conversion_buffer;           /* any conversions are put into here
                                         * they could come from here too if
                                         * there was a previous conversion */
    size_t conversion_buffer_size;      /* in bytes */
};

struct stream_in {
    struct audio_stream_in stream;

    struct stream_lock  lock;

    bool standby;

    struct audio_device *adev;           /* hardware information - only using this for the lock */

    alsa_device_profile * profile;      /* Points to the alsa_device_profile in the audio_device */
    alsa_device_proxy proxy;            /* state of the stream */

    unsigned hal_channel_count;         /* channel count exposed to AudioFlinger.
                                         * This may differ from the device channel count when
                                         * the device is not compatible with AudioFlinger
                                         * capabilities, e.g. exposes too many channels or
                                         * too few channels. */
    audio_channel_mask_t hal_channel_mask;  /* USB devices deal in channel counts, not masks
                                             * so the proxy doesn't have a channel_mask, but
                                             * audio HALs need to talk about channel masks
                                             * so expose the one calculated by
                                             * adev_open_input_stream */

    struct listnode list_node;

    /* We may need to read more data from the device in order to data reduce to 16bit, 4chan */
    void * conversion_buffer;           /* any conversions are put into here
                                         * they could come from here too if
                                         * there was a previous conversion */
    size_t conversion_buffer_size;      /* in bytes */
};

/*
 * Locking Helpers
 */
/*
 * NOTE: when multiple mutexes have to be acquired, always take the
 * stream_in or stream_out mutex first, followed by the audio_device mutex.
 * stream pre_lock is always acquired before stream lock to prevent starvation of control thread by
 * higher priority playback or capture thread.
 */

static void stream_lock_init(struct stream_lock *lock) {
    pthread_mutex_init(&lock->lock, (const pthread_mutexattr_t *) NULL);
    pthread_mutex_init(&lock->pre_lock, (const pthread_mutexattr_t *) NULL);
}

static void stream_lock(struct stream_lock *lock) {
    pthread_mutex_lock(&lock->pre_lock);
    pthread_mutex_lock(&lock->lock);
    pthread_mutex_unlock(&lock->pre_lock);
}

static void stream_unlock(struct stream_lock *lock) {
    pthread_mutex_unlock(&lock->lock);
}

static void device_lock(struct audio_device *adev) {
    pthread_mutex_lock(&adev->lock);
}

static int device_try_lock(struct audio_device *adev) {
    return pthread_mutex_trylock(&adev->lock);
}

static void device_unlock(struct audio_device *adev) {
    pthread_mutex_unlock(&adev->lock);
}

/*
 * streams list management
 */
static void adev_add_stream_to_list(
    struct audio_device* adev, struct listnode* list, struct listnode* stream_node) {
    device_lock(adev);

    list_add_tail(list, stream_node);

    device_unlock(adev);
}

static void adev_remove_stream_from_list(
    struct audio_device* adev, struct listnode* stream_node) {
    device_lock(adev);

    list_remove(stream_node);

    device_unlock(adev);
}

/*
 * Extract the card and device numbers from the supplied key/value pairs.
 *   kvpairs    A null-terminated string containing the key/value pairs or card and device.
 *              i.e. "card=1;device=42"
 *   card   A pointer to a variable to receive the parsed-out card number.
 *   device A pointer to a variable to receive the parsed-out device number.
 * NOTE: The variables pointed to by card and device return -1 (undefined) if the
 *  associated key/value pair is not found in the provided string.
 *  Return true if the kvpairs string contain a card/device spec, false otherwise.
 */
static bool parse_card_device_params(const char *kvpairs, int *card, int *device)
{
    struct str_parms * parms = str_parms_create_str(kvpairs);
    char value[32];
    int param_val;

    // initialize to "undefined" state.
    *card = -1;
    *device = -1;

    param_val = str_parms_get_str(parms, "card", value, sizeof(value));
    if (param_val >= 0) {
        *card = atoi(value);
    }

    param_val = str_parms_get_str(parms, "device", value, sizeof(value));
    if (param_val >= 0) {
        *device = atoi(value);
    }

    str_parms_destroy(parms);

    return *card >= 0 && *device >= 0;
}

static char * device_get_parameters(alsa_device_profile * profile, const char * keys)
{
    if (profile->card < 0 || profile->device < 0) {
        return strdup("");
    }

    struct str_parms *query = str_parms_create_str(keys);
    struct str_parms *result = str_parms_create();

    /* These keys are from hardware/libhardware/include/audio.h */
    /* supported sample rates */
    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
        char* rates_list = profile_get_sample_rate_strs(profile);
        str_parms_add_str(result, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES,
                          rates_list);
        free(rates_list);
    }

    /* supported channel counts */
    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
        char* channels_list = profile_get_channel_count_strs(profile);
        str_parms_add_str(result, AUDIO_PARAMETER_STREAM_SUP_CHANNELS,
                          channels_list);
        free(channels_list);
    }

    /* supported sample formats */
    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        char * format_params = profile_get_format_strs(profile);
        str_parms_add_str(result, AUDIO_PARAMETER_STREAM_SUP_FORMATS,
                          format_params);
        free(format_params);
    }
    str_parms_destroy(query);

    char* result_str = str_parms_to_str(result);
    str_parms_destroy(result);

    ALOGV("device_get_parameters = %s", result_str);

    return result_str;
}

/*
 * HAl Functions
 */
/**
 * NOTE: when multiple mutexes have to be acquired, always respect the
 * following order: hw device > out stream
 */

/*
 * OUT functions
 */
static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    uint32_t rate = proxy_get_sample_rate(&((struct stream_out*)stream)->proxy);
    ALOGV("out_get_sample_rate() = %d", rate);
    return rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    const struct stream_out* out = (const struct stream_out*)stream;
    size_t buffer_size =
        proxy_get_period_size(&out->proxy) * audio_stream_out_frame_size(&(out->stream));
    return buffer_size;
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    const struct stream_out *out = (const struct stream_out*)stream;
    return out->hal_channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    /* Note: The HAL doesn't do any FORMAT conversion at this time. It
     * Relies on the framework to provide data in the specified format.
     * This could change in the future.
     */
    alsa_device_proxy * proxy = &((struct stream_out*)stream)->proxy;
    audio_format_t format = audio_format_from_pcm_format(proxy_get_format(proxy));
    return format;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    stream_lock(&out->lock);
    if (!out->standby) {
        device_lock(out->adev);
        proxy_close(&out->proxy);
        device_unlock(out->adev);
        out->standby = true;
    }
    stream_unlock(&out->lock);
    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd) {
    const struct stream_out* out_stream = (const struct stream_out*) stream;

    if (out_stream != NULL) {
        dprintf(fd, "Output Profile:\n");
        profile_dump(out_stream->profile, fd);

        dprintf(fd, "Output Proxy:\n");
        proxy_dump(&out_stream->proxy, fd);
    }

    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    ALOGV("out_set_parameters() keys:%s", kvpairs);

    struct stream_out *out = (struct stream_out *)stream;

    int routing = 0;
    int ret_value = 0;
    int card = -1;
    int device = -1;

    if (!parse_card_device_params(kvpairs, &card, &device)) {
        // nothing to do
        return ret_value;
    }

    stream_lock(&out->lock);
    /* Lock the device because that is where the profile lives */
    device_lock(out->adev);

    if (!profile_is_cached_for(out->profile, card, device)) {
        /* cannot read pcm device info if playback is active */
        if (!out->standby)
            ret_value = -ENOSYS;
        else {
            int saved_card = out->profile->card;
            int saved_device = out->profile->device;
            out->profile->card = card;
            out->profile->device = device;
            ret_value = profile_read_device_info(out->profile) ? 0 : -EINVAL;
            if (ret_value != 0) {
                out->profile->card = saved_card;
                out->profile->device = saved_device;
            }
        }
    }

    device_unlock(out->adev);
    stream_unlock(&out->lock);

    return ret_value;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct stream_out *out = (struct stream_out *)stream;
    stream_lock(&out->lock);
    device_lock(out->adev);

    char * params_str =  device_get_parameters(out->profile, keys);

    device_unlock(out->adev);
    stream_unlock(&out->lock);
    return params_str;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    alsa_device_proxy * proxy = &((struct stream_out*)stream)->proxy;
    return proxy_get_latency(proxy);
}

static int out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    return -ENOSYS;
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct stream_out *out)
{
    ALOGV("start_output_stream(card:%d device:%d)", out->profile->card, out->profile->device);

    return proxy_open(&out->proxy);
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer, size_t bytes)
{
    int ret;
    struct stream_out *out = (struct stream_out *)stream;

    if (out->adev->sco_thread != 0) return bytes;

    stream_lock(&out->lock);
    if (out->standby) {
        device_lock(out->adev);
        ret = start_output_stream(out);
        device_unlock(out->adev);
        if (ret != 0) {
            goto err;
        }
        out->standby = false;
    }

    alsa_device_proxy* proxy = &out->proxy;
    const void * write_buff = buffer;
    int num_write_buff_bytes = bytes;
    const int num_device_channels = proxy_get_channel_count(proxy); /* what we told alsa */
    const int num_req_channels = out->hal_channel_count; /* what we told AudioFlinger */
    if (num_device_channels != num_req_channels) {
        /* allocate buffer */
        const size_t required_conversion_buffer_size =
                 bytes * num_device_channels / num_req_channels;
        if (required_conversion_buffer_size > out->conversion_buffer_size) {
            out->conversion_buffer_size = required_conversion_buffer_size;
            out->conversion_buffer = realloc(out->conversion_buffer,
                                             out->conversion_buffer_size);
        }
        /* convert data */
        const audio_format_t audio_format = out_get_format(&(out->stream.common));
        const unsigned sample_size_in_bytes = audio_bytes_per_sample(audio_format);
        num_write_buff_bytes =
                adjust_channels(write_buff, num_req_channels,
                                out->conversion_buffer, num_device_channels,
                                sample_size_in_bytes, num_write_buff_bytes);
        write_buff = out->conversion_buffer;
    }

    if (write_buff != NULL && num_write_buff_bytes != 0) {
        proxy_write(&out->proxy, write_buff, num_write_buff_bytes);
    }

    stream_unlock(&out->lock);

    return bytes;

err:
    stream_unlock(&out->lock);
    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_out_frame_size(stream) /
               out_get_sample_rate(&stream->common));
    }

    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream, uint32_t *dsp_frames)
{
    return -EINVAL;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                         uint64_t *frames, struct timespec *timestamp)
{
    struct stream_out *out = (struct stream_out *)stream; // discard const qualifier
    stream_lock(&out->lock);

    const alsa_device_proxy *proxy = &out->proxy;
    const int ret = proxy_get_presentation_position(proxy, frames, timestamp);

    stream_unlock(&out->lock);
    return ret;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream, int64_t *timestamp)
{
    return -EINVAL;
}

static int adev_open_output_stream(struct audio_hw_device *hw_dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devicesSpec __unused,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address /*__unused*/)
{
    ALOGV("adev_open_output_stream() handle:0x%X, devicesSpec:0x%X, flags:0x%X, addr:%s",
          handle, devicesSpec, flags, address);

    struct stream_out *out;

    out = (struct stream_out *)calloc(1, sizeof(struct stream_out));
    if (out == NULL) {
        return -ENOMEM;
    }

    /* setup function pointers */
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_presentation_position = out_get_presentation_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;

    stream_lock_init(&out->lock);

    out->adev = (struct audio_device *)hw_dev;
    device_lock(out->adev);
    out->profile = &out->adev->out_profile;

    // build this to hand to the alsa_device_proxy
    struct pcm_config proxy_config;
    memset(&proxy_config, 0, sizeof(proxy_config));

    /* Pull out the card/device pair */
    parse_card_device_params(address, &(out->profile->card), &(out->profile->device));

    profile_read_device_info(out->profile);

    int ret = 0;

    /* Rate */
    if (config->sample_rate == 0) {
        proxy_config.rate = config->sample_rate = profile_get_default_sample_rate(out->profile);
    } else if (profile_is_sample_rate_valid(out->profile, config->sample_rate)) {
        proxy_config.rate = config->sample_rate;
    } else {
        proxy_config.rate = config->sample_rate = profile_get_default_sample_rate(out->profile);
        ret = -EINVAL;
    }

    out->adev->device_sample_rate = config->sample_rate;
    device_unlock(out->adev);

    /* Format */
    if (config->format == AUDIO_FORMAT_DEFAULT) {
        proxy_config.format = profile_get_default_format(out->profile);
        config->format = audio_format_from_pcm_format(proxy_config.format);
    } else {
        enum pcm_format fmt = pcm_format_from_audio_format(config->format);
        if (profile_is_format_valid(out->profile, fmt)) {
            proxy_config.format = fmt;
        } else {
            proxy_config.format = profile_get_default_format(out->profile);
            config->format = audio_format_from_pcm_format(proxy_config.format);
            ret = -EINVAL;
        }
    }

    /* Channels */
    bool calc_mask = false;
    if (config->channel_mask == AUDIO_CHANNEL_NONE) {
        /* query case */
        out->hal_channel_count = profile_get_default_channel_count(out->profile);
        calc_mask = true;
    } else {
        /* explicit case */
        out->hal_channel_count = audio_channel_count_from_out_mask(config->channel_mask);
    }

    /* The Framework is currently limited to no more than this number of channels */
    if (out->hal_channel_count > FCC_8) {
        out->hal_channel_count = FCC_8;
        calc_mask = true;
    }

    if (calc_mask) {
        /* need to calculate the mask from channel count either because this is the query case
         * or the specified mask isn't valid for this device, or is more then the FW can handle */
        config->channel_mask = out->hal_channel_count <= FCC_2
            /* position mask for mono and stereo*/
            ? audio_channel_out_mask_from_count(out->hal_channel_count)
            /* otherwise indexed */
            : audio_channel_mask_for_index_assignment_from_count(out->hal_channel_count);
    }

    out->hal_channel_mask = config->channel_mask;

    // Validate the "logical" channel count against support in the "actual" profile.
    // if they differ, choose the "actual" number of channels *closest* to the "logical".
    // and store THAT in proxy_config.channels
    proxy_config.channels = profile_get_closest_channel_count(out->profile, out->hal_channel_count);
    proxy_prepare(&out->proxy, out->profile, &proxy_config);

    /* TODO The retry mechanism isn't implemented in AudioPolicyManager/AudioFlinger. */
    ret = 0;

    out->conversion_buffer = NULL;
    out->conversion_buffer_size = 0;

    out->standby = true;

    /* Save the stream for adev_dump() */
    adev_add_stream_to_list(out->adev, &out->adev->output_stream_list, &out->list_node);

    *stream_out = &out->stream;

    return ret;

err_open:
    free(out);
    *stream_out = NULL;
    return -ENOSYS;
}

static void adev_close_output_stream(struct audio_hw_device *hw_dev,
                                     struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    ALOGV("adev_close_output_stream(c:%d d:%d)", out->profile->card, out->profile->device);

    adev_remove_stream_from_list(out->adev, &out->list_node);

    /* Close the pcm device */
    out_standby(&stream->common);

    free(out->conversion_buffer);

    out->conversion_buffer = NULL;
    out->conversion_buffer_size = 0;

    device_lock(out->adev);
    out->adev->device_sample_rate = 0;
    device_unlock(out->adev);

    free(stream);
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *hw_dev,
                                         const struct audio_config *config)
{
    /* TODO This needs to be calculated based on format/channels/rate */
    return 320;
}

/*
 * IN functions
 */
static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    uint32_t rate = proxy_get_sample_rate(&((const struct stream_in *)stream)->proxy);
    ALOGV("in_get_sample_rate() = %d", rate);
    return rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    ALOGV("in_set_sample_rate(%d) - NOPE", rate);
    return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    const struct stream_in * in = ((const struct stream_in*)stream);
    return proxy_get_period_size(&in->proxy) * audio_stream_in_frame_size(&(in->stream));
}

static uint32_t in_get_channels(const struct audio_stream *stream)
{
    const struct stream_in *in = (const struct stream_in*)stream;
    return in->hal_channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
     alsa_device_proxy *proxy = &((struct stream_in*)stream)->proxy;
     audio_format_t format = audio_format_from_pcm_format(proxy_get_format(proxy));
     return format;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    ALOGV("in_set_format(%d) - NOPE", format);

    return -ENOSYS;
}

static int in_standby(struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

    stream_lock(&in->lock);
    if (!in->standby) {
        device_lock(in->adev);
        proxy_close(&in->proxy);
        device_unlock(in->adev);
        in->standby = true;
    }

    stream_unlock(&in->lock);

    return 0;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
  const struct stream_in* in_stream = (const struct stream_in*)stream;
  if (in_stream != NULL) {
      dprintf(fd, "Input Profile:\n");
      profile_dump(in_stream->profile, fd);

      dprintf(fd, "Input Proxy:\n");
      proxy_dump(&in_stream->proxy, fd);
  }

  return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    ALOGV("in_set_parameters() keys:%s", kvpairs);

    struct stream_in *in = (struct stream_in *)stream;

    char value[32];
    int param_val;
    int routing = 0;
    int ret_value = 0;
    int card = -1;
    int device = -1;

    if (!parse_card_device_params(kvpairs, &card, &device)) {
        // nothing to do
        return ret_value;
    }

    stream_lock(&in->lock);
    device_lock(in->adev);

    if (card >= 0 && device >= 0 && !profile_is_cached_for(in->profile, card, device)) {
        /* cannot read pcm device info if playback is active */
        if (!in->standby)
            ret_value = -ENOSYS;
        else {
            int saved_card = in->profile->card;
            int saved_device = in->profile->device;
            in->profile->card = card;
            in->profile->device = device;
            ret_value = profile_read_device_info(in->profile) ? 0 : -EINVAL;
            if (ret_value != 0) {
                in->profile->card = saved_card;
                in->profile->device = saved_device;
            }
        }
    }

    device_unlock(in->adev);
    stream_unlock(&in->lock);

    return ret_value;
}

static char * in_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct stream_in *in = (struct stream_in *)stream;

    stream_lock(&in->lock);
    device_lock(in->adev);

    char * params_str =  device_get_parameters(in->profile, keys);

    device_unlock(in->adev);
    stream_unlock(&in->lock);

    return params_str;
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int start_input_stream(struct stream_in *in)
{
    ALOGV("start_input_stream(card:%d device:%d)", in->profile->card, in->profile->device);

    return proxy_open(&in->proxy);
}

/* TODO mutex stuff here (see out_write) */
static ssize_t in_read(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    size_t num_read_buff_bytes = 0;
    void * read_buff = buffer;
    void * out_buff = buffer;
    int ret = 0;

    struct stream_in * in = (struct stream_in *)stream;

    if (in->adev->sco_thread != 0) return bytes;

    stream_lock(&in->lock);
    if (in->standby) {
        device_lock(in->adev);
        ret = start_input_stream(in);
        device_unlock(in->adev);
        if (ret != 0) {
            goto err;
        }
        in->standby = false;
    }

    alsa_device_profile * profile = in->profile;

    /*
     * OK, we need to figure out how much data to read to be able to output the requested
     * number of bytes in the HAL format (16-bit, stereo).
     */
    num_read_buff_bytes = bytes;
    int num_device_channels = proxy_get_channel_count(&in->proxy); /* what we told Alsa */
    int num_req_channels = in->hal_channel_count; /* what we told AudioFlinger */

    if (num_device_channels != num_req_channels) {
        num_read_buff_bytes = (num_device_channels * num_read_buff_bytes) / num_req_channels;
    }

    /* Setup/Realloc the conversion buffer (if necessary). */
    if (num_read_buff_bytes != bytes) {
        if (num_read_buff_bytes > in->conversion_buffer_size) {
            /*TODO Remove this when AudioPolicyManger/AudioFlinger support arbitrary formats
              (and do these conversions themselves) */
            in->conversion_buffer_size = num_read_buff_bytes;
            in->conversion_buffer = realloc(in->conversion_buffer, in->conversion_buffer_size);
        }
        read_buff = in->conversion_buffer;
    }

    ret = proxy_read(&in->proxy, read_buff, num_read_buff_bytes);
    if (ret == 0) {
        if (num_device_channels != num_req_channels) {
            // ALOGV("chans dev:%d req:%d", num_device_channels, num_req_channels);

            out_buff = buffer;
            /* Num Channels conversion */
            if (num_device_channels != num_req_channels) {
                audio_format_t audio_format = in_get_format(&(in->stream.common));
                unsigned sample_size_in_bytes = audio_bytes_per_sample(audio_format);

                num_read_buff_bytes =
                    adjust_channels(read_buff, num_device_channels,
                                    out_buff, num_req_channels,
                                    sample_size_in_bytes, num_read_buff_bytes);
            }
        }

        /* no need to acquire in->adev->lock to read mic_muted here as we don't change its state */
        if (num_read_buff_bytes > 0 && in->adev->mic_muted)
            memset(buffer, 0, num_read_buff_bytes);
    } else {
        num_read_buff_bytes = 0; // reset the value after USB headset is unplugged
    }

err:
    stream_unlock(&in->lock);
    return num_read_buff_bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int adev_open_input_stream(struct audio_hw_device *hw_dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devicesSpec __unused,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags __unused,
                                  const char *address,
                                  audio_source_t source __unused)
{
    ALOGV("adev_open_input_stream() rate:%" PRIu32 ", chanMask:0x%" PRIX32 ", fmt:%" PRIu8,
          config->sample_rate, config->channel_mask, config->format);

    struct stream_in *in = (struct stream_in *)calloc(1, sizeof(struct stream_in));
    int ret = 0;

    if (in == NULL) {
        return -ENOMEM;
    }

    /* setup function pointers */
    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;

    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    stream_lock_init(&in->lock);

    in->adev = (struct audio_device *)hw_dev;
    device_lock(in->adev);

    in->profile = &in->adev->in_profile;

    struct pcm_config proxy_config;
    memset(&proxy_config, 0, sizeof(proxy_config));

    /* Pull out the card/device pair */
    parse_card_device_params(address, &(in->profile->card), &(in->profile->device));

    profile_read_device_info(in->profile);

    /* Rate */
    if (config->sample_rate == 0) {
        config->sample_rate = profile_get_default_sample_rate(in->profile);
    }

    if (in->adev->device_sample_rate != 0 &&                 /* we are playing, so lock the rate */
        in->adev->device_sample_rate >= RATELOCK_THRESHOLD) {/* but only for high sample rates */
        ret = config->sample_rate != in->adev->device_sample_rate ? -EINVAL : 0;
        proxy_config.rate = config->sample_rate = in->adev->device_sample_rate;
    } else if (profile_is_sample_rate_valid(in->profile, config->sample_rate)) {
        proxy_config.rate = config->sample_rate;
    } else {
        proxy_config.rate = config->sample_rate = profile_get_default_sample_rate(in->profile);
        ret = -EINVAL;
    }
    device_unlock(in->adev);

    /* Format */
    if (config->format == AUDIO_FORMAT_DEFAULT) {
        proxy_config.format = profile_get_default_format(in->profile);
        config->format = audio_format_from_pcm_format(proxy_config.format);
    } else {
        enum pcm_format fmt = pcm_format_from_audio_format(config->format);
        if (profile_is_format_valid(in->profile, fmt)) {
            proxy_config.format = fmt;
        } else {
            proxy_config.format = profile_get_default_format(in->profile);
            config->format = audio_format_from_pcm_format(proxy_config.format);
            ret = -EINVAL;
        }
    }

    /* Channels */
    bool calc_mask = false;
    if (config->channel_mask == AUDIO_CHANNEL_NONE) {
        /* query case */
        in->hal_channel_count = profile_get_default_channel_count(in->profile);
        calc_mask = true;
    } else {
        /* explicit case */
        in->hal_channel_count = audio_channel_count_from_in_mask(config->channel_mask);
    }

    /* The Framework is currently limited to no more than this number of channels */
    if (in->hal_channel_count > FCC_8) {
        in->hal_channel_count = FCC_8;
        calc_mask = true;
    }

    if (calc_mask) {
        /* need to calculate the mask from channel count either because this is the query case
         * or the specified mask isn't valid for this device, or is more then the FW can handle */
        in->hal_channel_mask = in->hal_channel_count <= FCC_2
            /* position mask for mono & stereo */
            ? audio_channel_in_mask_from_count(in->hal_channel_count)
            /* otherwise indexed */
            : audio_channel_mask_for_index_assignment_from_count(in->hal_channel_count);

        // if we change the mask...
        if (in->hal_channel_mask != config->channel_mask &&
            config->channel_mask != AUDIO_CHANNEL_NONE) {
            config->channel_mask = in->hal_channel_mask;
            ret = -EINVAL;
        }
    } else {
        in->hal_channel_mask = config->channel_mask;
    }

    if (ret == 0) {
        // Validate the "logical" channel count against support in the "actual" profile.
        // if they differ, choose the "actual" number of channels *closest* to the "logical".
        // and store THAT in proxy_config.channels
        proxy_config.channels =
                profile_get_closest_channel_count(in->profile, in->hal_channel_count);
        ret = proxy_prepare(&in->proxy, in->profile, &proxy_config);
        if (ret == 0) {
            in->standby = true;

            in->conversion_buffer = NULL;
            in->conversion_buffer_size = 0;

            *stream_in = &in->stream;

            /* Save this for adev_dump() */
            adev_add_stream_to_list(in->adev, &in->adev->input_stream_list, &in->list_node);
        } else {
            ALOGW("proxy_prepare error %d", ret);
            unsigned channel_count = proxy_get_channel_count(&in->proxy);
            config->channel_mask = channel_count <= FCC_2
                ? audio_channel_in_mask_from_count(channel_count)
                : audio_channel_mask_for_index_assignment_from_count(channel_count);
            config->format = audio_format_from_pcm_format(proxy_get_format(&in->proxy));
            config->sample_rate = proxy_get_sample_rate(&in->proxy);
        }
    }

    if (ret != 0) {
        // Deallocate this stream on error, because AudioFlinger won't call
        // adev_close_input_stream() in this case.
        *stream_in = NULL;
        free(in);
    }

    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *hw_dev,
                                    struct audio_stream_in *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    ALOGV("adev_close_input_stream(c:%d d:%d)", in->profile->card, in->profile->device);

    adev_remove_stream_from_list(in->adev, &in->list_node);

    /* Close the pcm device */
    in_standby(&stream->common);

    free(in->conversion_buffer);

    free(stream);
}


void set_line_in(struct audio_hw_device *hw_dev){
    struct audio_device * adev = (struct audio_device *)hw_dev;
    if (adev->hw_mixer == 0) adev->hw_mixer = mixer_open(adev->usbcard);
    if (adev->hw_mixer == 0) return;
    struct mixer_ctl *line_in_ctl = mixer_get_ctl_by_name(adev->hw_mixer, "Line Playback Switch");

    if (adev->line_in && adev->sco_thread == 0){
        mixer_ctl_set_value(line_in_ctl, 0, 1);
    } else {
        mixer_ctl_set_value(line_in_ctl, 0, 0);
    }
}

void stereo_to_mono(int16_t *stereo, int16_t *mono, size_t samples){
    // Converts interleaved stereo into mono by discarding second channel
    int i;
    for (i=0; i<samples; i++)
        mono[i] = stereo[2*i];
}

// TEMP FOR DATA WRITE TO FILE
/*
#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1

struct wav_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t riff_fmt;
    uint32_t fmt_id;
    uint32_t fmt_sz;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_sz;
};
*/
// END TEMP FOR DATA WRITE TO FILE

void* runsco(void * args) {
    int16_t *framebuf_far_stereo;
    int16_t *framebuf_far_mono;
    int16_t *framebuf_near_stereo;
    int16_t *framebuf_near_mono;

    size_t block_len_bytes_far_mono = 0;
    size_t block_len_bytes_far_stereo = 0;
    size_t block_len_bytes_near_mono = 0;
    size_t block_len_bytes_near_stereo = 0;

    size_t frames_per_block_near = 0;
    size_t frames_per_block_far = 0;

    int rc;
    struct audio_device * adev = (struct audio_device *)args;
    struct resampler_itfe *resampler_to48;
    struct resampler_itfe *resampler_from48;

    int loopcounter = 0;
    struct timespec hwtime;
    unsigned int f_read_avail;
    unsigned int f_read_bytes;
    unsigned int f_write_avail;
    unsigned int f_write_bytes;

    // AudioProcessing: Initialize
    struct audioproc *apm = audioproc_create();
    struct audioframe *frame = audioframe_create(1, adev->sco_samplerate, adev->sco_samplerate / 100);

    struct pcm_config bt_config = {
        .channels = 2,
        .rate = adev->sco_samplerate,
        .format = PCM_FORMAT_S16_LE,
        .period_size = 1024,
        .period_count = 4,
        .start_threshold = 0, // 0's mean default
        .silence_threshold = 0,
        .stop_threshold = 0,
    };

    struct pcm_config usb_config = {
        .channels = 2,
        .rate = 48000,
        .format = PCM_FORMAT_S16_LE,
        .period_size = 1024,
        .period_count = 4,
        .start_threshold = 0,
        .silence_threshold = 0,
        .stop_threshold = 0,
    };

// TEMP FOR FILE WRITE
/*struct wav_header header;

unsigned int in_far_frames = 0;
unsigned int in_near_frames = 0;
unsigned int out_near_frames = 0;
unsigned int out_far_frames = 0;

ALOGD("%s: Opening PCM logs", __func__);

FILE *in_far = fopen("/data/pcmlogs/in_far.wav", "wb");
FILE *in_near = fopen("/data/pcmlogs/in_near.wav", "wb");
FILE *out_near = fopen("/data/pcmlogs/out_near.wav", "wb");
FILE *out_far = fopen("/data/pcmlogs/out_far.wav", "wb");

ALOGD("%s: Setting up header", __func__);

header.riff_id = ID_RIFF;
header.riff_sz = 0;
header.riff_fmt = ID_WAVE;
header.fmt_id = ID_FMT;
header.fmt_sz = 16;
header.audio_format = FORMAT_PCM;
header.num_channels = 1;
header.bits_per_sample = pcm_format_to_bits(PCM_FORMAT_S16_LE);
header.block_align = header.bits_per_sample / 8;
header.data_id = ID_DATA;

ALOGD("%s: Seeking in PCM logs", __func__);

fseek(in_far, sizeof(struct wav_header), SEEK_SET);
fseek(in_near, sizeof(struct wav_header), SEEK_SET);
fseek(out_near, sizeof(struct wav_header), SEEK_SET);
fseek(out_far, sizeof(struct wav_header), SEEK_SET);
*/
// END TEMP FOR FILE WRITE

    ALOGD("%s: USBCARD: %d, BTCARD: %d", __func__, adev->usbcard, adev->btcard);

    // Put all existing streams into standby (closed). Note that when sco thread is running
    // out_write and in_read functions will bail immediately while pretending to work and
    // not opening the pcm's.
    struct listnode* node;

    list_for_each(node, &adev->output_stream_list) {
        struct audio_stream* stream = (struct audio_stream *)node_to_item(node, struct stream_out, list_node);
        out_standby((struct audio_stream_out *)stream);
    }

    list_for_each(node, &adev->input_stream_list) {
        struct audio_stream* stream = (struct audio_stream *)node_to_item(node, struct stream_in, list_node);
        in_standby((struct audio_stream_in *)stream);
    }

    adev->sco_pcm_far_in = pcm_open(adev->btcard, 0, PCM_IN, &bt_config);
    if (adev->sco_pcm_far_in == 0) {
        ALOGD("%s: failed to allocate memory for PCM far/in", __func__);
        return NULL;
    } else if (!pcm_is_ready(adev->sco_pcm_far_in)){
        pcm_close(adev->sco_pcm_far_in);
        ALOGD("%s: failed to open PCM far/in", __func__);
        return NULL;
    }

    adev->sco_pcm_far_out = pcm_open(adev->btcard, 0, PCM_OUT, &bt_config);
    if (adev->sco_pcm_far_out == 0) {
        ALOGD("%s: failed to allocate memory for PCM far/out", __func__);
        pcm_close(adev->sco_pcm_far_in);
        return NULL;
    } else if (!pcm_is_ready(adev->sco_pcm_far_out)){
        pcm_close(adev->sco_pcm_far_in);
        pcm_close(adev->sco_pcm_far_out);
        ALOGD("%s: failed to open PCM far/out", __func__);
        return NULL;
    }

    adev->sco_pcm_near_in = pcm_open(adev->usbcard, 0, PCM_IN, &usb_config);
    if (adev->sco_pcm_near_in == 0) {
        ALOGD("%s: failed to allocate memory for PCM near/in", __func__);
        pcm_close(adev->sco_pcm_far_in);
        pcm_close(adev->sco_pcm_far_out);
        return NULL;
    } else if (!pcm_is_ready(adev->sco_pcm_near_in)){
        pcm_close(adev->sco_pcm_far_in);
        pcm_close(adev->sco_pcm_far_out);
        pcm_close(adev->sco_pcm_near_in);
        ALOGD("%s: failed to open PCM near/in", __func__);
        return NULL;
    }

    adev->sco_pcm_near_out = pcm_open(adev->usbcard, 0, PCM_OUT, &usb_config);
    if (adev->sco_pcm_near_out == 0) {
        ALOGD("%s: failed to allocate memory for PCM near/out", __func__);
        pcm_close(adev->sco_pcm_far_in);
        pcm_close(adev->sco_pcm_far_out);
        pcm_close(adev->sco_pcm_near_in);
        return NULL;
    } else if (!pcm_is_ready(adev->sco_pcm_near_out)){
        pcm_close(adev->sco_pcm_far_in);
        pcm_close(adev->sco_pcm_far_out);
        pcm_close(adev->sco_pcm_near_in);
        pcm_close(adev->sco_pcm_near_out);
        ALOGD("%s: failed to open PCM near/out", __func__);
        return NULL;
    }

    // bytes / frame: channels * bytes/sample. 2 channels * 16 bits/sample = 2 channels * 2 bytes/sample = 4 (stereo), 2 (mono)
    // We read/write in blocks of 10 ms = samplerate / 100 = 80, 160, or 480 frames.

    frames_per_block_near = 48000 / 100;
    frames_per_block_far = adev->sco_samplerate / 100;

    block_len_bytes_far_mono = 2 * frames_per_block_far; // bytes/frame * frames
    block_len_bytes_far_stereo = 4 * frames_per_block_far;
    block_len_bytes_near_mono = 2 * frames_per_block_near;
    block_len_bytes_near_stereo = 4 * frames_per_block_near;

    framebuf_far_stereo = (int16_t *)malloc(block_len_bytes_far_stereo);
    framebuf_far_mono = (int16_t *)malloc(block_len_bytes_far_mono);
    framebuf_near_stereo = (int16_t *)malloc(block_len_bytes_near_stereo);
    framebuf_near_mono = (int16_t *)malloc(block_len_bytes_near_mono);
    if (framebuf_far_stereo == NULL || framebuf_near_stereo == NULL) {
        ALOGD("%s: failed to allocate frames", __func__);
        pcm_close(adev->sco_pcm_near_in);
        pcm_close(adev->sco_pcm_near_out);
        pcm_close(adev->sco_pcm_far_in);
        pcm_close(adev->sco_pcm_far_out);
        return NULL;
    }

    rc = create_resampler(adev->sco_samplerate, 48000, 1, RESAMPLER_QUALITY_DEFAULT, NULL, &resampler_to48);
    if (rc != 0) {
        resampler_to48 = NULL;
        ALOGD("%s: echo_reference_write() failure to create resampler %d", __func__, rc);
        pcm_close(adev->sco_pcm_near_in);
        pcm_close(adev->sco_pcm_near_out);
        pcm_close(adev->sco_pcm_far_in);
        pcm_close(adev->sco_pcm_far_out);
        return NULL;
    }

    rc = create_resampler(48000, adev->sco_samplerate, 1, RESAMPLER_QUALITY_DEFAULT, NULL, &resampler_from48);
    if (rc != 0) {
        resampler_from48 = NULL;
        ALOGD("%s: echo_reference_write() failure to create resampler %d", __func__, rc);
        pcm_close(adev->sco_pcm_near_in);
        pcm_close(adev->sco_pcm_near_out);
        pcm_close(adev->sco_pcm_far_in);
        pcm_close(adev->sco_pcm_far_out);
        return NULL;
    }

    // AudioProcessing: Setup
    audioproc_hpf_en(apm, 1);
    audioproc_aec_drift_comp_en(apm, 0);
    audioproc_aec_en(apm, 1);
    audioproc_ns_set_level(apm, 1); // 0 = low, 1 = moderate, 2 = high, 3 = veryhigh
    audioproc_ns_en(apm, 1);
    audioproc_agc_set_level_limits(apm, 0, 255);
    audioproc_agc_set_mode(apm, 0); // 0 = Adaptive Analog, 1 = Adaptive Digital, 2 = Fixed Digital
    audioproc_agc_en(apm, 1);

    ALOGD("%s: PCM loop starting", __func__);

    memset(framebuf_far_stereo, 0, block_len_bytes_far_stereo);
    while (!adev->terminate_sco && pcm_read(adev->sco_pcm_far_in, framebuf_far_stereo, block_len_bytes_far_stereo) == 0){

//        ALOGD("%s: Looping... #%d", __func__, loopcounter);
        loopcounter++;

        memset(framebuf_far_mono, 0, block_len_bytes_far_mono);
        stereo_to_mono(framebuf_far_stereo, framebuf_far_mono, frames_per_block_far);

        // TEMP FILE WRITE
//        fwrite(framebuf_far_mono, 1, block_len_bytes_far_mono, in_far);
//        in_far_frames += 80;

        // AudioProcessing: Analyze reverse stream
	audioframe_setdata(frame, &framebuf_far_mono, frames_per_block_far);
        audioproc_aec_echo_ref(apm, &frame);

        memset(framebuf_near_mono, 0, block_len_bytes_near_mono);
        resampler_to48->resample_from_input(resampler_to48, (int16_t *)framebuf_far_mono, (size_t *)&frames_per_block_far, (int16_t *) framebuf_near_mono, (size_t *)&frames_per_block_near);

        // TEMP FILE WRITE
//        fwrite(framebuf_near_mono, 1, block_len_bytes_near_mono, in_near);
//        in_near_frames += 480;

        memset(framebuf_near_stereo, 0, block_len_bytes_near_stereo);
        adjust_channels(framebuf_near_mono, 1, framebuf_near_stereo, 2, 2, block_len_bytes_near_mono);

        pcm_get_htimestamp(adev->sco_pcm_near_out, &f_write_avail, &hwtime);
        if (4 * (4096 - f_write_avail) > block_len_bytes_near_stereo) f_write_bytes = block_len_bytes_near_stereo;
        else f_write_bytes = 4 * (4096 - f_write_avail);

        pcm_write(adev->sco_pcm_near_out, framebuf_near_stereo, f_write_bytes);
        memset(framebuf_near_stereo, 0, block_len_bytes_near_stereo);

        pcm_get_htimestamp(adev->sco_pcm_near_in, &f_read_avail, &hwtime);
        if ((4*f_read_avail) < block_len_bytes_near_stereo) f_read_bytes = 4 * f_read_avail;
        else f_read_bytes = block_len_bytes_near_stereo;

        pcm_read(adev->sco_pcm_near_in, framebuf_near_stereo, f_read_bytes);

        ALOGD("%s: near out avail: %d, out written: %d, near in avail: %d, near read: %d. (%d)", __func__, f_write_avail, f_write_bytes/4, f_read_avail, f_read_bytes/4, loopcounter);

        memset(framebuf_near_mono, 0, block_len_bytes_near_mono);
        stereo_to_mono(framebuf_near_stereo, framebuf_near_mono, frames_per_block_near);

        // TEMP FILE WRITE
//        fwrite(framebuf_near_mono, 1, block_len_bytes_near_mono, out_near);
//        out_near_frames += 480;

        memset(framebuf_far_mono, 0, block_len_bytes_far_mono);
        resampler_from48->resample_from_input(resampler_from48, (int16_t *)framebuf_near_mono, (size_t *)&frames_per_block_near, (int16_t *)framebuf_far_mono, (size_t *)&frames_per_block_far);

        // TEMP FILE WRITE
//        fwrite(framebuf_far_mono, 1, block_len_bytes_far_mono, out_far);
//        out_far_frames += 80;

        // AudioProcessing: Process Audio
	audioframe_setdata(frame, &framebuf_far_mono, frames_per_block_far);
        audioproc_process(apm, frame);
	audioframe_getdata(frame, &framebuf_far_mono, frames_per_block_far);

        memset(framebuf_far_stereo, 0, block_len_bytes_far_stereo);
        adjust_channels(framebuf_far_mono, 1, framebuf_far_stereo, 2, 2, block_len_bytes_far_mono);

        pcm_write(adev->sco_pcm_far_out, framebuf_far_stereo, block_len_bytes_far_stereo);

        memset(framebuf_far_stereo, 0, block_len_bytes_far_stereo);
    }

    ALOGD("%s: PCM loop terminated", __func__);

    // AudioProcessing: Done
    audioproc_destroy(apm);

// TEMP FOR FILE WRITE
/*
// in_far
header.sample_rate = 8000;
header.byte_rate = (header.bits_per_sample / 8) * 1 * 8000;
header.data_sz = in_far_frames * header.block_align;
header.riff_sz = header.data_sz + sizeof(header) - 8;
fseek(in_far, 0, SEEK_SET);
fwrite(&header, sizeof(struct wav_header), 1, in_far);
fclose(in_far);

// in_near
header.sample_rate = 48000;
header.byte_rate = (header.bits_per_sample / 8) * 1 * 48000;
header.data_sz = in_near_frames * header.block_align;
header.riff_sz = header.data_sz + sizeof(header) - 8;
fseek(in_near, 0, SEEK_SET);
fwrite(&header, sizeof(struct wav_header), 1, in_near);
fclose(in_near);

// out_near
header.sample_rate = 48000;
header.byte_rate = (header.bits_per_sample / 8) * 1 * 48000;
header.data_sz = out_near_frames * header.block_align;
header.riff_sz = header.data_sz + sizeof(header) - 8;
fseek(out_near, 0, SEEK_SET);
fwrite(&header, sizeof(struct wav_header), 1, out_near);
fclose(out_near);

// out_far
header.sample_rate = 8000;
header.byte_rate = (header.bits_per_sample / 8) * 1 * 8000;
header.data_sz = out_far_frames * header.block_align;
header.riff_sz = header.data_sz + sizeof(header) - 8;
fseek(out_far, 0, SEEK_SET);
fwrite(&header, sizeof(struct wav_header), 1, out_far);
fclose(out_far);
*/
// END TEMP FOR FILE WRITE

    // We're done, close the PCM's and return.
    pcm_close(adev->sco_pcm_near_in);
    pcm_close(adev->sco_pcm_near_out);
    pcm_close(adev->sco_pcm_far_in);
    pcm_close(adev->sco_pcm_far_out);

    adev->sco_pcm_near_in = 0;
    adev->sco_pcm_near_out = 0;
    adev->sco_pcm_far_in = 0;
    adev->sco_pcm_far_out = 0;

    adev->sco_thread = 0;

    return NULL;

}

void set_hfp_volume(struct audio_hw_device *hw_dev, int volume)
{
    // value of volume will be between 1 and 15 inclusive

    struct audio_device * adev = (struct audio_device *)hw_dev;
    if (adev->hw_mixer == 0) adev->hw_mixer = mixer_open(adev->usbcard);
    if (adev->hw_mixer == 0) return;
    struct mixer_ctl *vol_ctl = mixer_get_ctl_by_name(adev->hw_mixer, "Speaker Playback Volume");
    int max = mixer_ctl_get_range_max(vol_ctl);
    int size = mixer_ctl_get_num_values(vol_ctl);
    int i;

    for (i=0; i<size; i++){
        if (i < 2) mixer_ctl_set_value(vol_ctl, i, (int)((float) max * ((float)volume / 15.0)));
        else mixer_ctl_set_value(vol_ctl, i, 0);
    }
}

/*
 * ADEV Functions
 */
static int adev_set_master_volume(struct audio_hw_device *hw_dev, float volume)
{
    // value of volume will be between 0.0 and 1.0 inclusive

    struct audio_device * adev = (struct audio_device *)hw_dev;
    if (adev->hw_mixer == 0) adev->hw_mixer = mixer_open(adev->usbcard);
    if (adev->hw_mixer == 0) return 0;
    struct mixer_ctl *vol_ctl = mixer_get_ctl_by_name(adev->hw_mixer, "Speaker Playback Volume");
    int max = mixer_ctl_get_range_max(vol_ctl);
    int size = mixer_ctl_get_num_values(vol_ctl);
    int i;

    adev->master_volume = volume;
    if (adev->vol_balance == 0){ // setup default volume balance if not already set
        adev->vol_balance = malloc(size * sizeof(float));
        for (i=0; i<size; i++){
            if (i < 2) adev->vol_balance[i] = 1.0;
            else if (i < 4) adev->vol_balance[i] = 0.75;
            else adev->vol_balance[i] = 0.0;
        }
    }

    for (i=0; i<size; i++)
        mixer_ctl_set_value(vol_ctl, i, (int)((float) max * adev->vol_balance[i] * volume));

    return 0; // any return value other than 0 means that master volume becomes software emulated.
}

static int adev_set_parameters(struct audio_hw_device *hw_dev, const char *kvpairs)
{
    ALOGD("%s: kvpairs: %s", __func__, kvpairs);

    struct audio_device * adev = (struct audio_device *)hw_dev;
    char value[32];
    int ret, val = 0;
    struct str_parms *parms;

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_CARD, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        adev->usbcard = val;
        adev->btcard = (val + 1) % 2;
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HFP_SET_SAMPLING_RATE, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        //TODO adev->sco_samplerate = val;
        adev->sco_samplerate = 8000;
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HFP_ENABLE, value, sizeof(value));
    if (ret >= 0) {
        pthread_mutex_lock(&adev->sco_thread_lock);
        if (strcmp(value, "true") == 0){
            if (adev->sco_thread == 0) {
                adev->terminate_sco = false;
                pthread_create(&adev->sco_thread, NULL, &runsco, adev);
                set_line_in(hw_dev);
            }
        } else {
            if (adev->sco_thread != 0) {
                adev->terminate_sco = true; // this will cause the thread to exit the main loop and terminate.
                adev->sco_thread = 0;
                set_line_in(hw_dev);
                adev_set_master_volume(hw_dev, adev->master_volume); // reset master volume on termination
            }
        }
        pthread_mutex_unlock(&adev->sco_thread_lock);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HFP_VOLUME, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        set_hfp_volume(hw_dev, val); // val is always 1-15 inclusive
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_LINEIN, value, sizeof(value));
    if (ret >= 0){
        if (strcmp(value, "play") == 0) adev->line_in = true;
        else adev->line_in = false;

        set_line_in(hw_dev);
    }

    return 0;
}

static char * adev_get_parameters(const struct audio_hw_device *hw_dev, const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *hw_dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *hw_dev, float volume)
{
    return -ENOSYS; // this is probably fine, since we will use hfp_volume parameter instead.
}

static int adev_set_mode(struct audio_hw_device *hw_dev, audio_mode_t mode)
{
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *hw_dev, bool state)
{
    struct audio_device * adev = (struct audio_device *)hw_dev;
    device_lock(adev);
    adev->mic_muted = state;
    device_unlock(adev);
    return -ENOSYS;
}

static int adev_get_mic_mute(const struct audio_hw_device *hw_dev, bool *state)
{
    return -ENOSYS;
}

static int adev_dump(const struct audio_hw_device *device, int fd)
{
    dprintf(fd, "\nUSB audio module:\n");

    struct audio_device* adev = (struct audio_device*)device;
    const int kNumRetries = 3;
    const int kSleepTimeMS = 500;

    // use device_try_lock() in case we dumpsys during a deadlock
    int retry = kNumRetries;
    while (retry > 0 && device_try_lock(adev) != 0) {
      sleep(kSleepTimeMS);
      retry--;
    }

    if (retry > 0) {
        if (list_empty(&adev->output_stream_list)) {
            dprintf(fd, "  No output streams.\n");
        } else {
            struct listnode* node;
            list_for_each(node, &adev->output_stream_list) {
                struct audio_stream* stream =
                        (struct audio_stream *)node_to_item(node, struct stream_out, list_node);
                out_dump(stream, fd);
            }
        }

        if (list_empty(&adev->input_stream_list)) {
            dprintf(fd, "\n  No input streams.\n");
        } else {
            struct listnode* node;
            list_for_each(node, &adev->input_stream_list) {
                struct audio_stream* stream =
                        (struct audio_stream *)node_to_item(node, struct stream_in, list_node);
                in_dump(stream, fd);
            }
        }

        device_unlock(adev);
    } else {
        // Couldn't lock
        dprintf(fd, "  Could not obtain device lock.\n");
    }

    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct audio_device *adev = (struct audio_device *)device;
    if (adev->hw_mixer != 0) mixer_close(adev->hw_mixer);
    free(device);

    return 0;
}

static int adev_open(const hw_module_t* module, const char* name, hw_device_t** device)
{
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    struct audio_device *adev = calloc(1, sizeof(struct audio_device));
    if (!adev)
        return -ENOMEM;

    profile_init(&adev->out_profile, PCM_OUT);
    profile_init(&adev->in_profile, PCM_IN);

    list_init(&adev->output_stream_list);
    list_init(&adev->input_stream_list);

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module = (struct hw_module_t *)module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;

    adev->line_in = false;
    adev->hw_mixer = 0;
    adev->vol_balance = 0;
    adev->master_volume = (float)0.5;

    *device = &adev->hw_device.common;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "USB audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};

