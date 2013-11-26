/* audio_stream_out.c
 **
 ** Copyright 2008-2009 Wind River Systems
 ** Copyright (c) 2011-2013, The Linux Foundation. All rights reserved
 ** Not a Contribution, Apache license notifications and license are retained
 ** for attribution purposes only.
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

#define LOG_TAG "audio_stream_out"
/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <math.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/prctl.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <cutils/atomic.h>
#include <cutils/sched_policy.h>

#include <system/thread_defs.h>
#include "audio_hw.h"
#include "platform_api.h"
#include <platform.h>

#include "sound/compress_params.h"

#define COMPRESS_OFFLOAD_FRAGMENT_SIZE (32 * 1024)
#define COMPRESS_OFFLOAD_NUM_FRAGMENTS 4
/* ToDo: Check and update a proper value in msec */
#define COMPRESS_OFFLOAD_PLAYBACK_LATENCY 96
#define COMPRESS_PLAYBACK_VOLUME_MAX 0x2000

struct pcm_config pcm_config_deep_buffer = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = DEEP_BUFFER_OUTPUT_PERIOD_SIZE,
    .period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_low_latency = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_hdmi_multi = {
    .channels = HDMI_MULTI_DEFAULT_CHANNEL_COUNT, /* changed when the stream is opened */
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE, /* changed when the stream is opened */
    .period_size = HDMI_MULTI_PERIOD_SIZE,
    .period_count = HDMI_MULTI_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

#define STRING_TO_ENUM(string) { #string, string }

struct string_to_enum {
    const char *name;
    uint32_t value;
};

static const struct string_to_enum out_channels_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_5POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_7POINT1),
};

static bool is_supported_format(audio_format_t format)
{
    if (format == AUDIO_FORMAT_MP3 ||
            format == AUDIO_FORMAT_AAC)
        return true;

    return false;
}

static int get_snd_codec_id(audio_format_t format)
{
    int id = 0;

    switch (format) {
    case AUDIO_FORMAT_MP3:
        id = SND_AUDIOCODEC_MP3;
        break;
    case AUDIO_FORMAT_AAC:
        id = SND_AUDIOCODEC_AAC;
        break;
    default:
        ALOGE("%s: Unsupported audio format", __func__);
    }

    return id;
}

/* must be called with hw device mutex locked */
static int read_hdmi_channel_masks(struct stream_out *out)
{
    int ret = 0;
    int channels = platform_edid_get_max_channels(out->dev->platform);

    switch (channels) {
        /*
         * Do not handle stereo output in Multi-channel cases
         * Stereo case is handled in normal playback path
         */
    case 6:
        ALOGV("%s: HDMI supports 5.1", __func__);
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_5POINT1;
        break;
    case 8:
        ALOGV("%s: HDMI supports 5.1 and 7.1 channels", __func__);
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_5POINT1;
        out->supported_channel_masks[1] = AUDIO_CHANNEL_OUT_7POINT1;
        break;
    default:
        ALOGE("HDMI does not support multi channel playback");
        ret = -ENOSYS;
        break;
    }
    return ret;
}

/* must be called with out->lock locked */
static int send_offload_cmd_l(struct stream_out* out, int command)
{
    struct offload_cmd *cmd = (struct offload_cmd *)calloc(1, sizeof(struct offload_cmd));

    ALOGVV("%s %d", __func__, command);

    cmd->cmd = command;
    list_add_tail(&out->offload_cmd_list, &cmd->node);
    pthread_cond_signal(&out->offload_cond);
    return 0;
}

/* must be called iwth out->lock locked */
static void stop_compressed_output_l(struct stream_out *out)
{
    out->offload_state = OFFLOAD_STATE_IDLE;
    out->playback_started = 0;
    out->send_new_metadata = 1;
    if (out->compr != NULL) {
        compress_stop(out->compr);
        while (out->offload_thread_blocked) {
            pthread_cond_wait(&out->cond, &out->lock);
        }
    }
}

static void *offload_thread_loop(void *context)
{
    struct stream_out *out = (struct stream_out *) context;
    struct listnode *item;

    out->offload_state = OFFLOAD_STATE_IDLE;
    out->playback_started = 0;

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
    set_sched_policy(0, SP_FOREGROUND);
    prctl(PR_SET_NAME, (unsigned long)"Offload Callback", 0, 0, 0);

    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    for (;;) {
        struct offload_cmd *cmd = NULL;
        stream_callback_event_t event;
        bool send_callback = false;

        ALOGVV("%s offload_cmd_list %d out->offload_state %d",
              __func__, list_empty(&out->offload_cmd_list),
              out->offload_state);
        if (list_empty(&out->offload_cmd_list)) {
            ALOGV("%s SLEEPING", __func__);
            pthread_cond_wait(&out->offload_cond, &out->lock);
            ALOGV("%s RUNNING", __func__);
            continue;
        }

        item = list_head(&out->offload_cmd_list);
        cmd = node_to_item(item, struct offload_cmd, node);
        list_remove(item);

        ALOGVV("%s STATE %d CMD %d out->compr %p",
               __func__, out->offload_state, cmd->cmd, out->compr);

        if (cmd->cmd == OFFLOAD_CMD_EXIT) {
            free(cmd);
            break;
        }

        if (out->compr == NULL) {
            ALOGE("%s: Compress handle is NULL", __func__);
            pthread_cond_signal(&out->cond);
            continue;
        }
        out->offload_thread_blocked = true;
        pthread_mutex_unlock(&out->lock);
        send_callback = false;
        switch(cmd->cmd) {
        case OFFLOAD_CMD_WAIT_FOR_BUFFER:
            compress_wait(out->compr, -1);
            send_callback = true;
            event = STREAM_CBK_EVENT_WRITE_READY;
            break;
        case OFFLOAD_CMD_PARTIAL_DRAIN:
            compress_next_track(out->compr);
            compress_partial_drain(out->compr);
            send_callback = true;
            event = STREAM_CBK_EVENT_DRAIN_READY;
            break;
        case OFFLOAD_CMD_DRAIN:
            compress_drain(out->compr);
            send_callback = true;
            event = STREAM_CBK_EVENT_DRAIN_READY;
            break;
        default:
            ALOGE("%s unknown command received: %d", __func__, cmd->cmd);
            break;
        }
        pthread_mutex_lock(&out->lock);
        out->offload_thread_blocked = false;
        pthread_cond_signal(&out->cond);
        if (send_callback) {
            out->offload_callback(event, NULL, out->offload_cookie);
        }
        free(cmd);
    }

    pthread_cond_signal(&out->cond);
    while (!list_empty(&out->offload_cmd_list)) {
        item = list_head(&out->offload_cmd_list);
        list_remove(item);
        free(node_to_item(item, struct offload_cmd, node));
    }
    pthread_mutex_unlock(&out->lock);

    return NULL;
}

static int create_offload_callback_thread(struct stream_out *out)
{
    pthread_cond_init(&out->offload_cond, (const pthread_condattr_t *) NULL);
    list_init(&out->offload_cmd_list);
    pthread_create(&out->offload_thread, (const pthread_attr_t *) NULL,
                    offload_thread_loop, out);
    return 0;
}

static int destroy_offload_callback_thread(struct stream_out *out)
{
    pthread_mutex_lock(&out->lock);
    stop_compressed_output_l(out);
    send_offload_cmd_l(out, OFFLOAD_CMD_EXIT);

    pthread_mutex_unlock(&out->lock);
    pthread_join(out->offload_thread, (void **) NULL);
    pthread_cond_destroy(&out->offload_cond);

    return 0;
}

static bool allow_hdmi_channel_config(struct audio_device *adev)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    bool ret = true;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            /*
             * If voice call is already existing, do not proceed further to avoid
             * disabling/enabling both RX and TX devices, CSD calls, etc.
             * Once the voice call done, the HDMI channels can be configured to
             * max channels of remaining use cases.
             */
            if (usecase->id == USECASE_VOICE_CALL) {
                ALOGD("%s: voice call is active, no change in HDMI channels",
                      __func__);
                ret = false;
                break;
            } else if (usecase->id == USECASE_AUDIO_PLAYBACK_MULTI_CH) {
                ALOGD("%s: multi channel playback is active, "
                      "no change in HDMI channels", __func__);
                ret = false;
                break;
            }
        }
    }
    return ret;
}

static int check_and_set_hdmi_channels(struct audio_device *adev,
                                       unsigned int channels)
{
    struct listnode *node;
    struct audio_usecase *usecase;

    /* Check if change in HDMI channel config is allowed */
    if (!allow_hdmi_channel_config(adev))
        return 0;

    if (channels == adev->cur_hdmi_channels) {
        ALOGD("%s: Requested channels are same as current", __func__);
        return 0;
    }

    platform_set_hdmi_channels(adev->platform, channels);
    adev->cur_hdmi_channels = channels;

    /*
     * Deroute all the playback streams routed to HDMI so that
     * the back end is deactivated. Note that backend will not
     * be deactivated if any one stream is connected to it.
     */
    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_PLAYBACK &&
                usecase->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            disable_audio_route(adev, usecase, true);
        }
    }

    /*
     * Enable all the streams disabled above. Now the HDMI backend
     * will be activated with new channel configuration
     */
    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_PLAYBACK &&
                usecase->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            enable_audio_route(adev, usecase, true);
        }
    }

    return 0;
}

static int stop_output_stream(struct stream_out *out)
{
    int i, ret = 0;
    struct audio_usecase *uc_info;
    struct audio_device *adev = out->dev;

    ALOGV("%s: enter: usecase(%d: %s)", __func__,
          out->usecase, use_case_table[out->usecase]);
    uc_info = get_usecase_from_list(adev, out->usecase);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, out->usecase);
        return -EINVAL;
    }

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD &&
            adev->visualizer_stop_output != NULL)
        adev->visualizer_stop_output(out->handle);

    /* 1. Get and set stream specific mixer controls */
    disable_audio_route(adev, uc_info, true);

    /* 2. Disable the rx device */
    disable_snd_device(adev, uc_info->out_snd_device, true);

    list_remove(&uc_info->list);
    free(uc_info);

    /* Must be called after removing the usecase from list */
    if (out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
        check_and_set_hdmi_channels(adev, DEFAULT_HDMI_OUT_CHANNELS);

    ALOGV("%s: exit: status(%d)", __func__, ret);
    return ret;
}

int start_output_stream(struct stream_out *out)
{
    int ret = 0;
    struct audio_usecase *uc_info;
    struct audio_device *adev = out->dev;

    ALOGV("%s: enter: usecase(%d: %s) devices(%#x)",
          __func__, out->usecase, use_case_table[out->usecase], out->devices);
    out->pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);
    if (out->pcm_device_id < 0) {
        ALOGE("%s: Invalid PCM device id(%d) for the usecase(%d)",
              __func__, out->pcm_device_id, out->usecase);
        ret = -EINVAL;
        goto error_config;
    }

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    uc_info->id = out->usecase;
    uc_info->type = PCM_PLAYBACK;
    uc_info->stream.out = out;
    uc_info->devices = out->devices;
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    /* This must be called before adding this usecase to the list */
    if (out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
        check_and_set_hdmi_channels(adev, out->config.channels);

    list_add_tail(&adev->usecase_list, &uc_info->list);

    select_devices(adev, out->usecase);

    ALOGV("%s: Opening PCM device card_id(%d) device_id(%d)",
          __func__, 0, out->pcm_device_id);
    if (out->usecase != USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        out->pcm = pcm_open(SOUND_CARD, out->pcm_device_id,
                               PCM_OUT | PCM_MONOTONIC, &out->config);
        if (out->pcm && !pcm_is_ready(out->pcm)) {
            ALOGE("%s: %s", __func__, pcm_get_error(out->pcm));
            pcm_close(out->pcm);
            out->pcm = NULL;
            ret = -EIO;
            goto error_open;
        }
    } else {
        out->pcm = NULL;
        out->compr = compress_open(SOUND_CARD, out->pcm_device_id,
                                   COMPRESS_IN, &out->compr_config);
        if (out->compr && !is_compress_ready(out->compr)) {
            ALOGE("%s: %s", __func__, compress_get_error(out->compr));
            compress_close(out->compr);
            out->compr = NULL;
            ret = -EIO;
            goto error_open;
        }
        if (out->offload_callback)
            compress_nonblock(out->compr, out->non_blocking);

        if (adev->visualizer_start_output != NULL)
            adev->visualizer_start_output(out->handle);
    }
    ALOGV("%s: exit", __func__);
    return 0;
error_open:
    stop_output_stream(out);
error_config:
    return ret;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    return out->sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD)
        return out->compr_config.fragment_size;

    return out->config.period_size * audio_stream_frame_size(stream);
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    return out->channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    return out->format;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static int out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;

    ALOGV("%s: enter: usecase(%d: %s)", __func__,
          out->usecase, use_case_table[out->usecase]);
    if (out->usecase == USECASE_COMPRESS_VOIP_CALL) {
        /* Ignore standby in case of voip call because the voip output
         * stream is closed in adev_close_output_stream()
         */
        ALOGV("%s: Ignore Standby in VOIP call", __func__);
        return 0;
    }

    pthread_mutex_lock(&out->lock);
    pthread_mutex_lock(&adev->lock);
    if (!out->standby) {
        out->standby = true;
        if (out->usecase != USECASE_AUDIO_PLAYBACK_OFFLOAD) {
            if (out->pcm) {
                pcm_close(out->pcm);
                out->pcm = NULL;
            }
        } else {
            stop_compressed_output_l(out);
            out->gapless_mdata.encoder_delay = 0;
            out->gapless_mdata.encoder_padding = 0;
            if (out->compr != NULL) {
                compress_close(out->compr);
                out->compr = NULL;
            }
        }
        stop_output_stream(out);
    }
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
    ALOGV("%s: exit", __func__);
    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int parse_compress_metadata(struct stream_out *out, struct str_parms *parms)
{
    int ret = 0;
    char value[32];
    struct compr_gapless_mdata tmp_mdata;

    if (!out || !parms) {
        return -EINVAL;
    }

    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_DELAY_SAMPLES, value, sizeof(value));
    if (ret >= 0) {
        tmp_mdata.encoder_delay = atoi(value); //whats a good limit check?
    } else {
        return -EINVAL;
    }

    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_PADDING_SAMPLES, value, sizeof(value));
    if (ret >= 0) {
        tmp_mdata.encoder_padding = atoi(value);
    } else {
        return -EINVAL;
    }

    out->gapless_mdata = tmp_mdata;
    out->send_new_metadata = 1;
    ALOGV("%s new encoder delay %u and padding %u", __func__,
          out->gapless_mdata.encoder_delay, out->gapless_mdata.encoder_padding);

    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    struct audio_usecase *usecase;
    struct listnode *node;
    struct str_parms *parms;
    char value[32];
    int ret, val = 0;
    bool select_new_device = false;

    ALOGD("%s: enter: usecase(%d: %s) kvpairs: %s",
          __func__, out->usecase, use_case_table[out->usecase], kvpairs);
    parms = str_parms_create_str(kvpairs);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&out->lock);
        pthread_mutex_lock(&adev->lock);

        /*
         * When HDMI cable is unplugged the music playback is paused and
         * the policy manager sends routing=0. But the audioflinger
         * continues to write data until standby time (3sec).
         * As the HDMI core is turned off, the write gets blocked.
         * Avoid this by routing audio to speaker until standby.
         */
        if (out->devices == AUDIO_DEVICE_OUT_AUX_DIGITAL &&
                val == AUDIO_DEVICE_NONE) {
            val = AUDIO_DEVICE_OUT_SPEAKER;
        }

        /*
         * select_devices() call below switches all the usecases on the same
         * backend to the new device. Refer to check_usecases_codec_backend() in
         * the select_devices(). But how do we undo this?
         *
         * For example, music playback is active on headset (deep-buffer usecase)
         * and if we go to ringtones and select a ringtone, low-latency usecase
         * will be started on headset+speaker. As we can't enable headset+speaker
         * and headset devices at the same time, select_devices() switches the music
         * playback to headset+speaker while starting low-lateny usecase for ringtone.
         * So when the ringtone playback is completed, how do we undo the same?
         *
         * We are relying on the out_set_parameters() call on deep-buffer output,
         * once the ringtone playback is ended.
         * NOTE: We should not check if the current devices are same as new devices.
         *       Because select_devices() must be called to switch back the music
         *       playback to headset.
         */
        if (val != 0) {
            out->devices = val;

            if (!out->standby)
                select_devices(adev, out->usecase);
        }

        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&out->lock);
    }

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        parse_compress_metadata(out, parms);
    }

    str_parms_destroy(parms);
    ALOGV("%s: exit: code(%d)", __func__, ret);
    return ret;
}

static char* out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    char *str;
    char value[256];
    struct str_parms *reply = str_parms_create();
    size_t i, j;
    int ret;
    bool first = true;
    ALOGV("%s: enter: keys - %s", __func__, keys);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        i = 0;
        while (out->supported_channel_masks[i] != 0) {
            for (j = 0; j < ARRAY_SIZE(out_channels_name_to_enum_table); j++) {
                if (out_channels_name_to_enum_table[j].value == out->supported_channel_masks[i]) {
                    if (!first) {
                        strlcat(value, "|", sizeof(value));
                    }
                    strlcat(value, out_channels_name_to_enum_table[j].name, sizeof(value));
                    first = false;
                    break;
                }
            }
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value);
        str = str_parms_to_str(reply);
    }
    str_parms_destroy(query);
    str_parms_destroy(reply);
    ALOGV("%s: exit: returns - %s", __func__, str);
    return str;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD)
        return COMPRESS_OFFLOAD_PLAYBACK_LATENCY;

    return (out->config.period_count * out->config.period_size * 1000) /
           (out->config.rate);
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    int volume[2];

    if (out->usecase == USECASE_AUDIO_PLAYBACK_MULTI_CH) {
        /* only take left channel into account: the API is for stereo anyway */
        out->muted = (left == 0.0f);
        return 0;
    } else if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        const char *mixer_ctl_name = "Compress Playback Volume";
        struct audio_device *adev = out->dev;
        struct mixer_ctl *ctl;

        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return -EINVAL;
        }
        volume[0] = (int)(left * COMPRESS_PLAYBACK_VOLUME_MAX);
        volume[1] = (int)(right * COMPRESS_PLAYBACK_VOLUME_MAX);
        mixer_ctl_set_array(ctl, volume, sizeof(volume)/sizeof(volume[0]));
        return 0;
    }

    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer,
                         size_t bytes)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    ssize_t ret = 0;

    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        out->standby = false;
        pthread_mutex_lock(&adev->lock);
            ret = start_output_stream(out);
        pthread_mutex_unlock(&adev->lock);
        /* ToDo: If use case is compress offload should return 0 */
        if (ret != 0) {
            out->standby = true;
            goto exit;
        }
    }

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        ALOGVV("%s: writing buffer (%d bytes) to compress device", __func__, bytes);
        if (out->send_new_metadata) {
            ALOGVV("send new gapless metadata");
            compress_set_gapless_metadata(out->compr, &out->gapless_mdata);
            out->send_new_metadata = 0;
        }

        ret = compress_write(out->compr, buffer, bytes);
        ALOGVV("%s: writing buffer (%d bytes) to compress device returned %d", __func__, bytes, ret);
        if (ret >= 0 && ret < (ssize_t)bytes) {
            send_offload_cmd_l(out, OFFLOAD_CMD_WAIT_FOR_BUFFER);
        }
        if (!out->playback_started) {
            compress_start(out->compr);
            out->playback_started = 1;
            out->offload_state = OFFLOAD_STATE_PLAYING;
        }
        pthread_mutex_unlock(&out->lock);
        return ret;
    } else {
        if (out->pcm) {
            if (out->muted)
                memset((void *)buffer, 0, bytes);
            ALOGVV("%s: writing buffer (%d bytes) to pcm device", __func__, bytes);
            ret = pcm_write(out->pcm, (void *)buffer, bytes);
            if (ret == 0)
                out->written += bytes / (out->config.channels * sizeof(short));
        }
    }

exit:
    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        if (out->pcm)
            ALOGE("%s: error %d - %s", __func__, ret, pcm_get_error(out->pcm));
        out_standby(&out->stream.common);
        usleep(bytes * 1000000 / audio_stream_frame_size(&out->stream.common) /
               out_get_sample_rate(&out->stream.common));
    }
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    struct stream_out *out = (struct stream_out *)stream;
    *dsp_frames = 0;
    if ((out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) && (dsp_frames != NULL)) {
        pthread_mutex_lock(&out->lock);
        if (out->compr != NULL) {
            compress_get_tstamp(out->compr, (unsigned long *)dsp_frames,
                    &out->sample_rate);
            ALOGVV("%s rendered frames %d sample_rate %d",
                   __func__, *dsp_frames, out->sample_rate);
        }
        pthread_mutex_unlock(&out->lock);
        return 0;
    } else
        return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    return -EINVAL;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                   uint64_t *frames, struct timespec *timestamp)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret = -1;
    unsigned long dsp_frames;

    pthread_mutex_lock(&out->lock);

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        if (out->compr != NULL) {
            compress_get_tstamp(out->compr, &dsp_frames,
                    &out->sample_rate);
            ALOGVV("%s rendered frames %ld sample_rate %d",
                   __func__, dsp_frames, out->sample_rate);
            *frames = dsp_frames;
            ret = 0;
            /* this is the best we can do */
            clock_gettime(CLOCK_MONOTONIC, timestamp);
        }
    } else {
        if (out->pcm) {
            size_t avail;
            if (pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0) {
                size_t kernel_buffer_size = out->config.period_size * out->config.period_count;
                int64_t signed_frames = out->written - kernel_buffer_size + avail;
                // This adjustment accounts for buffering after app processor.
                // It is based on estimated DSP latency per use case, rather than exact.
                signed_frames -=
                    (platform_render_latency(out->usecase) * out->sample_rate / 1000000LL);

                // It would be unusual for this value to be negative, but check just in case ...
                if (signed_frames >= 0) {
                    *frames = signed_frames;
                    ret = 0;
                }
            }
        }
    }

    pthread_mutex_unlock(&out->lock);

    return ret;
}

static int out_set_callback(struct audio_stream_out *stream,
            stream_callback_t callback, void *cookie)
{
    struct stream_out *out = (struct stream_out *)stream;

    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    out->offload_callback = callback;
    out->offload_cookie = cookie;
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int out_pause(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        pthread_mutex_lock(&out->lock);
        if (out->compr != NULL && out->offload_state == OFFLOAD_STATE_PLAYING) {
            status = compress_pause(out->compr);
            out->offload_state = OFFLOAD_STATE_PAUSED;
        }
        pthread_mutex_unlock(&out->lock);
    }
    return status;
}

static int out_resume(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        status = 0;
        pthread_mutex_lock(&out->lock);
        if (out->compr != NULL && out->offload_state == OFFLOAD_STATE_PAUSED) {
            status = compress_resume(out->compr);
            out->offload_state = OFFLOAD_STATE_PLAYING;
        }
        pthread_mutex_unlock(&out->lock);
    }
    return status;
}

static int out_drain(struct audio_stream_out* stream, audio_drain_type_t type )
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        pthread_mutex_lock(&out->lock);
        if (type == AUDIO_DRAIN_EARLY_NOTIFY)
            status = send_offload_cmd_l(out, OFFLOAD_CMD_PARTIAL_DRAIN);
        else
            status = send_offload_cmd_l(out, OFFLOAD_CMD_DRAIN);
        pthread_mutex_unlock(&out->lock);
    }
    return status;
}

static int out_flush(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    ALOGV("%s", __func__);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        pthread_mutex_lock(&out->lock);
        stop_compressed_output_l(out);
        pthread_mutex_unlock(&out->lock);
        return 0;
    }
    return -ENOSYS;
}

int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_out *out;
    int i, ret;

    ALOGV("%s: enter: sample_rate(%d) channel_mask(%#x) devices(%#x) flags(%#x)",
          __func__, config->sample_rate, config->channel_mask, devices, flags);
    *stream_out = NULL;
    out = (struct stream_out *)calloc(1, sizeof(struct stream_out));

    if (devices == AUDIO_DEVICE_NONE)
        devices = AUDIO_DEVICE_OUT_SPEAKER;

    out->flags = flags;
    out->devices = devices;
    out->dev = adev;
    out->format = config->format;
    out->sample_rate = config->sample_rate;
    out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
    out->handle = handle;

    /* Init use case and pcm_config */
    if (out->flags == AUDIO_OUTPUT_FLAG_DIRECT &&
        out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
        pthread_mutex_lock(&adev->lock);
        ret = read_hdmi_channel_masks(out);
        pthread_mutex_unlock(&adev->lock);
        if (ret != 0)
            goto error_open;

        if (config->sample_rate == 0)
            config->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        if (config->channel_mask == 0)
            config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;

        out->channel_mask = config->channel_mask;
        out->sample_rate = config->sample_rate;
        out->usecase = USECASE_AUDIO_PLAYBACK_MULTI_CH;
        out->config = pcm_config_hdmi_multi;
        out->config.rate = config->sample_rate;
        out->config.channels = popcount(out->channel_mask);
        out->config.period_size = HDMI_MULTI_PERIOD_BYTES / (out->config.channels * 2);
    } else if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        if (config->offload_info.version != AUDIO_INFO_INITIALIZER.version ||
            config->offload_info.size != AUDIO_INFO_INITIALIZER.size) {
            ALOGE("%s: Unsupported Offload information", __func__);
            ret = -EINVAL;
            goto error_open;
        }
        if (!is_supported_format(config->offload_info.format)) {
            ALOGE("%s: Unsupported audio format", __func__);
            ret = -EINVAL;
            goto error_open;
        }

        out->compr_config.codec = (struct snd_codec *)
                                    calloc(1, sizeof(struct snd_codec));

        out->usecase = USECASE_AUDIO_PLAYBACK_OFFLOAD;
        if (config->offload_info.channel_mask)
            out->channel_mask = config->offload_info.channel_mask;
        else if (config->channel_mask)
            out->channel_mask = config->channel_mask;
        out->format = config->offload_info.format;
        out->sample_rate = config->offload_info.sample_rate;

        out->stream.set_callback = out_set_callback;
        out->stream.pause = out_pause;
        out->stream.resume = out_resume;
        out->stream.drain = out_drain;
        out->stream.flush = out_flush;

        out->compr_config.codec->id =
                get_snd_codec_id(config->offload_info.format);
        out->compr_config.fragment_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
        out->compr_config.fragments = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
        out->compr_config.codec->sample_rate =
                    compress_get_alsa_rate(config->offload_info.sample_rate);
        out->compr_config.codec->bit_rate =
                    config->offload_info.bit_rate;
        out->compr_config.codec->ch_in =
                    popcount(config->channel_mask);
        out->compr_config.codec->ch_out = out->compr_config.codec->ch_in;

        if (flags & AUDIO_OUTPUT_FLAG_NON_BLOCKING)
            out->non_blocking = 1;

        out->send_new_metadata = 1;
        create_offload_callback_thread(out);
        ALOGV("%s: offloaded output offload_info version %04x bit rate %d",
                __func__, config->offload_info.version,
                config->offload_info.bit_rate);
    } else if (out->flags & AUDIO_OUTPUT_FLAG_FAST) {
        out->usecase = USECASE_AUDIO_PLAYBACK_LOW_LATENCY;
        out->config = pcm_config_low_latency;
        out->sample_rate = out->config.rate;
    } else {
        out->usecase = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
        out->config = pcm_config_deep_buffer;
        out->sample_rate = out->config.rate;
    }

    if (flags & AUDIO_OUTPUT_FLAG_PRIMARY) {
        if(adev->primary_output == NULL)
            adev->primary_output = out;
        else {
            ALOGE("%s: Primary output is already opened", __func__);
            ret = -EEXIST;
            goto error_open;
        }
    }

    /* Check if this usecase is already existing */
    pthread_mutex_lock(&adev->lock);
    if (get_usecase_from_list(adev, out->usecase) != NULL) {
        ALOGE("%s: Usecase (%d) is already present", __func__, out->usecase);
        pthread_mutex_unlock(&adev->lock);
        ret = -EEXIST;
        goto error_open;
    }
    pthread_mutex_unlock(&adev->lock);

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
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;

    out->standby = 1;
    /* out->muted = false; by calloc() */
    /* out->written = 0; by calloc() */

    pthread_mutex_init(&out->lock, (const pthread_mutexattr_t *) NULL);
    pthread_cond_init(&out->cond, (const pthread_condattr_t *) NULL);

    config->format = out->stream.common.get_format(&out->stream.common);
    config->channel_mask = out->stream.common.get_channels(&out->stream.common);
    config->sample_rate = out->stream.common.get_sample_rate(&out->stream.common);

    *stream_out = &out->stream;
    ALOGV("%s: exit", __func__);
    return 0;

error_open:
    free(out);
    *stream_out = NULL;
    ALOGD("%s: exit: ret %d", __func__, ret);
    return ret;
}

void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    int ret = 0;

    ALOGV("%s: enter", __func__);
    out_standby(&stream->common);

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        destroy_offload_callback_thread(out);

        if (out->compr_config.codec != NULL)
            free(out->compr_config.codec);
    }
    pthread_cond_destroy(&out->cond);
    pthread_mutex_destroy(&out->lock);
    free(stream);
    ALOGV("%s: exit", __func__);
}
