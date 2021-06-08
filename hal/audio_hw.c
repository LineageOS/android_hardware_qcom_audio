/*
 * Copyright (C) 2013-2016 The Android Open Source Project
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

#define LOG_TAG "audio_hw_primary"
#define ATRACE_TAG ATRACE_TAG_AUDIO
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
#include <limits.h>

#include <log/log.h>
#include <cutils/trace.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <cutils/atomic.h>
#include <utils/Timers.h> // systemTime

#include <hardware/audio_effect.h>
#include <hardware/audio_alsaops.h>
#include <processgroup/sched_policy.h>
#include <system/thread_defs.h>
#include <tinyalsa/asoundlib.h>
#include <audio_effects/effect_aec.h>
#include <audio_effects/effect_ns.h>
#include <audio_utils/clock.h>
#include "audio_hw.h"
#include "audio_extn.h"
#include "audio_perf.h"
#include "platform_api.h"
#include <platform.h>
#include "voice_extn.h"

#include "sound/compress_params.h"
#include "audio_extn/tfa_98xx.h"
#include "audio_extn/maxxaudio.h"
#include "audio_extn/audiozoom.h"

/* COMPRESS_OFFLOAD_FRAGMENT_SIZE must be more than 8KB and a multiple of 32KB if more than 32KB.
 * COMPRESS_OFFLOAD_FRAGMENT_SIZE * COMPRESS_OFFLOAD_NUM_FRAGMENTS must be less than 8MB. */
#define COMPRESS_OFFLOAD_FRAGMENT_SIZE (256 * 1024)
// 2 buffers causes problems with high bitrate files
#define COMPRESS_OFFLOAD_NUM_FRAGMENTS 3
/* ToDo: Check and update a proper value in msec */
#define COMPRESS_OFFLOAD_PLAYBACK_LATENCY 96
/* treat as unsigned Q1.13 */
#define APP_TYPE_GAIN_DEFAULT         0x2000
#define COMPRESS_PLAYBACK_VOLUME_MAX 0x2000
#define PCM_PLAYBACK_VOLUME_MAX 0x2000
#define INVALID_OUT_VOLUME -1

/* treat as unsigned Q1.13 */
#define VOIP_PLAYBACK_VOLUME_MAX 0x2000

#define RECORD_GAIN_MIN 0.0f
#define RECORD_GAIN_MAX 1.0f
#define RECORD_VOLUME_CTL_MAX 0x2000

#define PROXY_OPEN_RETRY_COUNT           100
#define PROXY_OPEN_WAIT_TIME             20

#define MIN_CHANNEL_COUNT                1
#define DEFAULT_CHANNEL_COUNT            2

#ifndef MAX_TARGET_SPECIFIC_CHANNEL_CNT
#define MAX_CHANNEL_COUNT 1
#else
#define MAX_CHANNEL_COUNT atoi(XSTR(MAX_TARGET_SPECIFIC_CHANNEL_CNT))
#define XSTR(x) STR(x)
#define STR(x) #x
#endif
#define MAX_HIFI_CHANNEL_COUNT 8

#define ULL_PERIOD_SIZE (DEFAULT_OUTPUT_SAMPLING_RATE/1000)

static unsigned int configured_low_latency_capture_period_size =
        LOW_LATENCY_CAPTURE_PERIOD_SIZE;


#define MMAP_PERIOD_SIZE (DEFAULT_OUTPUT_SAMPLING_RATE/1000)
#define MMAP_PERIOD_COUNT_MIN 32
#define MMAP_PERIOD_COUNT_MAX 512
#define MMAP_PERIOD_COUNT_DEFAULT (MMAP_PERIOD_COUNT_MAX)
#define MMAP_MIN_SIZE_FRAMES_MAX 64 * 1024

/* This constant enables extended precision handling.
 * TODO The flag is off until more testing is done.
 */
static const bool k_enable_extended_precision = false;

struct pcm_config pcm_config_deep_buffer = {
    .channels = DEFAULT_CHANNEL_COUNT,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = DEEP_BUFFER_OUTPUT_PERIOD_SIZE,
    .period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_low_latency = {
    .channels = DEFAULT_CHANNEL_COUNT,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_haptics_audio = {
    .channels = 1,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_haptics = {
    .channels = 1,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

static int af_period_multiplier = 4;
struct pcm_config pcm_config_rt = {
    .channels = DEFAULT_CHANNEL_COUNT,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = ULL_PERIOD_SIZE, //1 ms
    .period_count = 512, //=> buffer size is 512ms
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = ULL_PERIOD_SIZE*8, //8ms
    .stop_threshold = INT_MAX,
    .silence_threshold = 0,
    .silence_size = 0,
    .avail_min = ULL_PERIOD_SIZE, //1 ms
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

struct pcm_config pcm_config_mmap_playback = {
    .channels = DEFAULT_CHANNEL_COUNT,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = MMAP_PERIOD_SIZE,
    .period_count = MMAP_PERIOD_COUNT_DEFAULT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = MMAP_PERIOD_SIZE*8,
    .stop_threshold = INT32_MAX,
    .silence_threshold = 0,
    .silence_size = 0,
    .avail_min = MMAP_PERIOD_SIZE, //1 ms
};

struct pcm_config pcm_config_hifi = {
    .channels = DEFAULT_CHANNEL_COUNT, /* changed when the stream is opened */
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE, /* changed when the stream is opened */
    .period_size = DEEP_BUFFER_OUTPUT_PERIOD_SIZE, /* change #define */
    .period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S24_3LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

struct pcm_config pcm_config_audio_capture = {
    .channels = DEFAULT_CHANNEL_COUNT,
    .period_count = AUDIO_CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

struct pcm_config pcm_config_audio_capture_rt = {
    .channels = DEFAULT_CHANNEL_COUNT,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = ULL_PERIOD_SIZE,
    .period_count = 512,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .silence_threshold = 0,
    .silence_size = 0,
    .avail_min = ULL_PERIOD_SIZE, //1 ms
};

struct pcm_config pcm_config_mmap_capture = {
    .channels = DEFAULT_CHANNEL_COUNT,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = MMAP_PERIOD_SIZE,
    .period_count = MMAP_PERIOD_COUNT_DEFAULT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .silence_threshold = 0,
    .silence_size = 0,
    .avail_min = MMAP_PERIOD_SIZE, //1 ms
};

struct pcm_config pcm_config_voip = {
    .channels = 1,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

#define AFE_PROXY_CHANNEL_COUNT 2
#define AFE_PROXY_SAMPLING_RATE 48000

#define AFE_PROXY_PLAYBACK_PERIOD_SIZE  256
#define AFE_PROXY_PLAYBACK_PERIOD_COUNT 4

struct pcm_config pcm_config_afe_proxy_playback = {
    .channels = AFE_PROXY_CHANNEL_COUNT,
    .rate = AFE_PROXY_SAMPLING_RATE,
    .period_size = AFE_PROXY_PLAYBACK_PERIOD_SIZE,
    .period_count = AFE_PROXY_PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = AFE_PROXY_PLAYBACK_PERIOD_SIZE,
    .stop_threshold = INT_MAX,
    .avail_min = AFE_PROXY_PLAYBACK_PERIOD_SIZE,
};

#define AFE_PROXY_RECORD_PERIOD_SIZE  256
#define AFE_PROXY_RECORD_PERIOD_COUNT 4

struct pcm_config pcm_config_afe_proxy_record = {
    .channels = AFE_PROXY_CHANNEL_COUNT,
    .rate = AFE_PROXY_SAMPLING_RATE,
    .period_size = AFE_PROXY_RECORD_PERIOD_SIZE,
    .period_count = AFE_PROXY_RECORD_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = AFE_PROXY_RECORD_PERIOD_SIZE,
    .stop_threshold = AFE_PROXY_RECORD_PERIOD_SIZE * AFE_PROXY_RECORD_PERIOD_COUNT,
    .avail_min = AFE_PROXY_RECORD_PERIOD_SIZE,
};

const char * const use_case_table[AUDIO_USECASE_MAX] = {
    [USECASE_AUDIO_PLAYBACK_DEEP_BUFFER] = "deep-buffer-playback",
    [USECASE_AUDIO_PLAYBACK_LOW_LATENCY] = "low-latency-playback",
    [USECASE_AUDIO_PLAYBACK_WITH_HAPTICS] = "audio-with-haptics-playback",
    [USECASE_AUDIO_PLAYBACK_HIFI] = "hifi-playback",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD] = "compress-offload-playback",
    [USECASE_AUDIO_PLAYBACK_TTS] = "audio-tts-playback",
    [USECASE_AUDIO_PLAYBACK_ULL] = "audio-ull-playback",
    [USECASE_AUDIO_PLAYBACK_MMAP] = "mmap-playback",

    [USECASE_AUDIO_RECORD] = "audio-record",
    [USECASE_AUDIO_RECORD_LOW_LATENCY] = "low-latency-record",
    [USECASE_AUDIO_RECORD_MMAP] = "mmap-record",
    [USECASE_AUDIO_RECORD_HIFI] = "hifi-record",

    [USECASE_AUDIO_HFP_SCO] = "hfp-sco",
    [USECASE_AUDIO_HFP_SCO_WB] = "hfp-sco-wb",

    [USECASE_VOICE_CALL] = "voice-call",
    [USECASE_VOICE2_CALL] = "voice2-call",
    [USECASE_VOLTE_CALL] = "volte-call",
    [USECASE_QCHAT_CALL] = "qchat-call",
    [USECASE_VOWLAN_CALL] = "vowlan-call",
    [USECASE_VOICEMMODE1_CALL] = "voicemmode1-call",
    [USECASE_VOICEMMODE2_CALL] = "voicemmode2-call",

    [USECASE_AUDIO_SPKR_CALIB_RX] = "spkr-rx-calib",
    [USECASE_AUDIO_SPKR_CALIB_TX] = "spkr-vi-record",

    [USECASE_AUDIO_PLAYBACK_AFE_PROXY] = "afe-proxy-playback",
    [USECASE_AUDIO_RECORD_AFE_PROXY] = "afe-proxy-record",

    [USECASE_INCALL_REC_UPLINK] = "incall-rec-uplink",
    [USECASE_INCALL_REC_DOWNLINK] = "incall-rec-downlink",
    [USECASE_INCALL_REC_UPLINK_AND_DOWNLINK] = "incall-rec-uplink-and-downlink",

    [USECASE_AUDIO_PLAYBACK_VOIP] = "audio-playback-voip",
    [USECASE_AUDIO_RECORD_VOIP] = "audio-record-voip",

    [USECASE_INCALL_MUSIC_UPLINK] = "incall-music-uplink",
    [USECASE_INCALL_MUSIC_UPLINK2] = "incall-music-uplink2",

    [USECASE_AUDIO_A2DP_ABR_FEEDBACK] = "a2dp-abr-feedback",
};


#define STRING_TO_ENUM(string) { #string, string }

struct string_to_enum {
    const char *name;
    uint32_t value;
};

static const struct string_to_enum channels_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_5POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_7POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_MONO),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_FRONT_BACK),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_1),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_2),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_3),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_4),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_5),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_6),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_7),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_8),
};

struct in_effect_list {
    struct listnode list;
    effect_handle_t handle;
};

static int set_voice_volume_l(struct audio_device *adev, float volume);
static struct audio_device *adev = NULL;
static pthread_mutex_t adev_init_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int audio_device_ref_count;
//cache last MBDRC cal step level
static int last_known_cal_step = -1 ;

static int check_a2dp_restore_l(struct audio_device *adev, struct stream_out *out, bool restore);
static int out_set_compr_volume(struct audio_stream_out *stream, float left, float right);
static int out_set_pcm_volume(struct audio_stream_out *stream, float left, float right);

static int in_set_microphone_direction(const struct audio_stream_in *stream,
                                           audio_microphone_direction_t dir);
static int in_set_microphone_field_dimension(const struct audio_stream_in *stream, float zoom);

static bool may_use_noirq_mode(struct audio_device *adev, audio_usecase_t uc_id,
                               int flags __unused)
{
    int dir = 0;
    switch (uc_id) {
    case USECASE_AUDIO_RECORD_LOW_LATENCY:
        dir = 1;
    case USECASE_AUDIO_PLAYBACK_ULL:
        break;
    default:
        return false;
    }

    int dev_id = platform_get_pcm_device_id(uc_id, dir == 0 ?
                                            PCM_PLAYBACK : PCM_CAPTURE);
    if (adev->adm_is_noirq_avail)
        return adev->adm_is_noirq_avail(adev->adm_data,
                                        adev->snd_card, dev_id, dir);
    return false;
}

static void register_out_stream(struct stream_out *out)
{
    struct audio_device *adev = out->dev;
    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD)
        return;

    if (!adev->adm_register_output_stream)
        return;

    adev->adm_register_output_stream(adev->adm_data,
                                     out->handle,
                                     out->flags);

    if (!adev->adm_set_config)
        return;

    if (out->realtime) {
        adev->adm_set_config(adev->adm_data,
                             out->handle,
                             out->pcm, &out->config);
    }
}

static void register_in_stream(struct stream_in *in)
{
    struct audio_device *adev = in->dev;
    if (!adev->adm_register_input_stream)
        return;

    adev->adm_register_input_stream(adev->adm_data,
                                    in->capture_handle,
                                    in->flags);

    if (!adev->adm_set_config)
        return;

    if (in->realtime) {
        adev->adm_set_config(adev->adm_data,
                             in->capture_handle,
                             in->pcm,
                             &in->config);
    }
}

static void request_out_focus(struct stream_out *out, long ns)
{
    struct audio_device *adev = out->dev;

    if (adev->adm_request_focus_v2) {
        adev->adm_request_focus_v2(adev->adm_data, out->handle, ns);
    } else if (adev->adm_request_focus) {
        adev->adm_request_focus(adev->adm_data, out->handle);
    }
}

static void request_in_focus(struct stream_in *in, long ns)
{
    struct audio_device *adev = in->dev;

    if (adev->adm_request_focus_v2) {
        adev->adm_request_focus_v2(adev->adm_data, in->capture_handle, ns);
    } else if (adev->adm_request_focus) {
        adev->adm_request_focus(adev->adm_data, in->capture_handle);
    }
}

static void release_out_focus(struct stream_out *out, long ns __unused)
{
    struct audio_device *adev = out->dev;

    if (adev->adm_abandon_focus)
        adev->adm_abandon_focus(adev->adm_data, out->handle);
}

static void release_in_focus(struct stream_in *in, long ns __unused)
{
    struct audio_device *adev = in->dev;
    if (adev->adm_abandon_focus)
        adev->adm_abandon_focus(adev->adm_data, in->capture_handle);
}

static int parse_snd_card_status(struct str_parms * parms, int * card,
                                 card_status_t * status)
{
    char value[32]={0};
    char state[32]={0};

    int ret = str_parms_get_str(parms, "SND_CARD_STATUS", value, sizeof(value));

    if (ret < 0)
        return -1;

    // sscanf should be okay as value is of max length 32.
    // same as sizeof state.
    if (sscanf(value, "%d,%s", card, state) < 2)
        return -1;

    *status = !strcmp(state, "ONLINE") ? CARD_STATUS_ONLINE :
                                         CARD_STATUS_OFFLINE;
    return 0;
}

// always call with adev lock held
void send_gain_dep_calibration_l() {
    if (last_known_cal_step >= 0)
        platform_send_gain_dep_cal(adev->platform, last_known_cal_step);
}

__attribute__ ((visibility ("default")))
bool audio_hw_send_gain_dep_calibration(int level) {
    bool ret_val = false;
    ALOGV("%s: enter ... ", __func__);

    pthread_mutex_lock(&adev_init_lock);

    if (adev != NULL && adev->platform != NULL) {
        pthread_mutex_lock(&adev->lock);
        last_known_cal_step = level;
        send_gain_dep_calibration_l();
        pthread_mutex_unlock(&adev->lock);
    } else {
        ALOGE("%s: %s is NULL", __func__, adev == NULL ? "adev" : "adev->platform");
    }

    pthread_mutex_unlock(&adev_init_lock);

    ALOGV("%s: exit with ret_val %d ", __func__, ret_val);
    return ret_val;
}

#ifdef MAXXAUDIO_QDSP_ENABLED
bool audio_hw_send_ma_parameter(int stream_type, float vol, bool active)
{
    bool ret = false;
    ALOGV("%s: enter ...", __func__);

    pthread_mutex_lock(&adev_init_lock);

    if (adev != NULL && adev->platform != NULL) {
        pthread_mutex_lock(&adev->lock);
        ret = audio_extn_ma_set_state(adev, stream_type, vol, active);
        pthread_mutex_unlock(&adev->lock);
    }

    pthread_mutex_unlock(&adev_init_lock);

    ALOGV("%s: exit with ret %d", __func__, ret);
    return ret;
}
#else
#define audio_hw_send_ma_parameter(stream_type, vol, active) (0)
#endif

__attribute__ ((visibility ("default")))
int audio_hw_get_gain_level_mapping(struct amp_db_and_gain_table *mapping_tbl,
                                    int table_size) {
     int ret_val = 0;
     ALOGV("%s: enter ... ", __func__);

     pthread_mutex_lock(&adev_init_lock);
     if (adev == NULL) {
         ALOGW("%s: adev is NULL .... ", __func__);
         goto done;
     }

     pthread_mutex_lock(&adev->lock);
     ret_val = platform_get_gain_level_mapping(mapping_tbl, table_size);
     pthread_mutex_unlock(&adev->lock);
done:
     pthread_mutex_unlock(&adev_init_lock);
     ALOGV("%s: exit ... ", __func__);
     return ret_val;
}

static bool is_supported_format(audio_format_t format)
{
    switch (format) {
        case AUDIO_FORMAT_MP3:
        case AUDIO_FORMAT_AAC_LC:
        case AUDIO_FORMAT_AAC_HE_V1:
        case AUDIO_FORMAT_AAC_HE_V2:
            return true;
        default:
            break;
    }
    return false;
}

static bool is_supported_24bits_audiosource(audio_source_t source)
{
    switch (source) {
        case AUDIO_SOURCE_UNPROCESSED:
#ifdef ENABLED_24BITS_CAMCORDER
        case AUDIO_SOURCE_CAMCORDER:
#endif
            return true;
        default:
            break;
    }
    return false;
}

static inline bool is_mmap_usecase(audio_usecase_t uc_id)
{
    return (uc_id == USECASE_AUDIO_RECORD_AFE_PROXY) ||
           (uc_id == USECASE_AUDIO_PLAYBACK_AFE_PROXY);
}

static int get_snd_codec_id(audio_format_t format)
{
    int id = 0;

    switch (format & AUDIO_FORMAT_MAIN_MASK) {
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

static int audio_ssr_status(struct audio_device *adev)
{
    int ret = 0;
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "Audio SSR Status";

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    ret = mixer_ctl_get_value(ctl, 0);
    ALOGD("%s: value: %d", __func__, ret);
    return ret;
}

static void stream_app_type_cfg_init(struct stream_app_type_cfg *cfg)
{
    cfg->gain[0] = cfg->gain[1] = APP_TYPE_GAIN_DEFAULT;
}

static bool is_btsco_device(snd_device_t out_snd_device, snd_device_t in_snd_device)
{
   return out_snd_device == SND_DEVICE_OUT_BT_SCO ||
          out_snd_device == SND_DEVICE_OUT_BT_SCO_WB ||
          in_snd_device == SND_DEVICE_IN_BT_SCO_MIC_WB_NREC ||
          in_snd_device == SND_DEVICE_IN_BT_SCO_MIC_WB ||
          in_snd_device == SND_DEVICE_IN_BT_SCO_MIC_NREC ||
          in_snd_device == SND_DEVICE_IN_BT_SCO_MIC;

}

static bool is_a2dp_device(snd_device_t out_snd_device)
{
   return out_snd_device == SND_DEVICE_OUT_BT_A2DP;
}

int enable_audio_route(struct audio_device *adev,
                       struct audio_usecase *usecase)
{
    snd_device_t snd_device;
    char mixer_path[MIXER_PATH_MAX_LENGTH];

    if (usecase == NULL)
        return -EINVAL;

    ALOGV("%s: enter: usecase(%d)", __func__, usecase->id);

    audio_extn_sound_trigger_update_stream_status(usecase, ST_EVENT_STREAM_BUSY);

    if (usecase->type == PCM_CAPTURE) {
        struct stream_in *in = usecase->stream.in;
        struct audio_usecase *uinfo;
        snd_device = usecase->in_snd_device;

        if (in) {
            if (in->enable_aec || in->enable_ec_port) {
                audio_devices_t out_device = AUDIO_DEVICE_OUT_SPEAKER;
                struct listnode *node;
                struct audio_usecase *voip_usecase = get_usecase_from_list(adev,
                                                           USECASE_AUDIO_PLAYBACK_VOIP);
                if (voip_usecase) {
                    out_device = voip_usecase->stream.out->devices;
                } else if (adev->primary_output &&
                              !adev->primary_output->standby) {
                    out_device = adev->primary_output->devices;
                } else {
                    list_for_each(node, &adev->usecase_list) {
                        uinfo = node_to_item(node, struct audio_usecase, list);
                        if (uinfo->type != PCM_CAPTURE) {
                            out_device = uinfo->stream.out->devices;
                            break;
                        }
                    }
                }
                platform_set_echo_reference(adev, true, out_device);
                in->ec_opened = true;
            }
        }
    } else
        snd_device = usecase->out_snd_device;
    audio_extn_utils_send_app_type_cfg(adev, usecase);
    audio_extn_ma_set_device(usecase);
    audio_extn_utils_send_audio_calibration(adev, usecase);

    // we shouldn't truncate mixer_path
    ALOGW_IF(strlcpy(mixer_path, use_case_table[usecase->id], sizeof(mixer_path))
            >= sizeof(mixer_path), "%s: truncation on mixer path", __func__);
    // this also appends to mixer_path
    platform_add_backend_name(adev->platform, mixer_path, snd_device);

    ALOGD("%s: usecase(%d) apply and update mixer path: %s", __func__,  usecase->id, mixer_path);
    audio_route_apply_and_update_path(adev->audio_route, mixer_path);

    ALOGV("%s: exit", __func__);
    return 0;
}

int disable_audio_route(struct audio_device *adev,
                        struct audio_usecase *usecase)
{
    snd_device_t snd_device;
    char mixer_path[MIXER_PATH_MAX_LENGTH];

    if (usecase == NULL)
        return -EINVAL;

    ALOGV("%s: enter: usecase(%d)", __func__, usecase->id);
    if (usecase->type == PCM_CAPTURE)
        snd_device = usecase->in_snd_device;
    else
        snd_device = usecase->out_snd_device;

    // we shouldn't truncate mixer_path
    ALOGW_IF(strlcpy(mixer_path, use_case_table[usecase->id], sizeof(mixer_path))
            >= sizeof(mixer_path), "%s: truncation on mixer path", __func__);
    // this also appends to mixer_path
    platform_add_backend_name(adev->platform, mixer_path, snd_device);
    ALOGD("%s: usecase(%d) reset and update mixer path: %s", __func__, usecase->id, mixer_path);

    audio_route_reset_and_update_path(adev->audio_route, mixer_path);
    if (usecase->type == PCM_CAPTURE) {
        struct stream_in *in = usecase->stream.in;
        if (in && in->ec_opened) {
            platform_set_echo_reference(in->dev, false, AUDIO_DEVICE_NONE);
            in->ec_opened = false;
        }
    }
    audio_extn_sound_trigger_update_stream_status(usecase, ST_EVENT_STREAM_FREE);

    ALOGV("%s: exit", __func__);
    return 0;
}

int enable_snd_device(struct audio_device *adev,
                      snd_device_t snd_device)
{
    int i, num_devices = 0;
    snd_device_t new_snd_devices[2];
    int ret_val = -EINVAL;
    if (snd_device < SND_DEVICE_MIN ||
        snd_device >= SND_DEVICE_MAX) {
        ALOGE("%s: Invalid sound device %d", __func__, snd_device);
        goto on_error;
    }

    platform_send_audio_calibration(adev->platform, snd_device);

    if (adev->snd_dev_ref_cnt[snd_device] >= 1) {
        ALOGV("%s: snd_device(%d: %s) is already active",
              __func__, snd_device, platform_get_snd_device_name(snd_device));
        goto on_success;
    }

    /* due to the possibility of calibration overwrite between listen
        and audio, notify sound trigger hal before audio calibration is sent */
    audio_extn_sound_trigger_update_device_status(snd_device,
                                    ST_EVENT_SND_DEVICE_BUSY);

    if (audio_extn_spkr_prot_is_enabled())
         audio_extn_spkr_prot_calib_cancel(adev);

    audio_extn_dsm_feedback_enable(adev, snd_device, true);

    if ((snd_device == SND_DEVICE_OUT_SPEAKER ||
        snd_device == SND_DEVICE_OUT_SPEAKER_SAFE ||
        snd_device == SND_DEVICE_OUT_SPEAKER_REVERSE ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER) &&
        audio_extn_spkr_prot_is_enabled()) {
        if (platform_get_snd_device_acdb_id(snd_device) < 0) {
            goto on_error;
        }
        if (audio_extn_spkr_prot_start_processing(snd_device)) {
            ALOGE("%s: spkr_start_processing failed", __func__);
            goto on_error;
        }
    } else if (platform_can_split_snd_device(snd_device,
                                             &num_devices,
                                             new_snd_devices) == 0) {
        for (i = 0; i < num_devices; i++) {
            enable_snd_device(adev, new_snd_devices[i]);
        }
        platform_set_speaker_gain_in_combo(adev, snd_device, true);
    } else {
        char device_name[DEVICE_NAME_MAX_SIZE] = {0};
        if (platform_get_snd_device_name_extn(adev->platform, snd_device, device_name) < 0 ) {
            ALOGE(" %s: Invalid sound device returned", __func__);
            goto on_error;
        }

        ALOGD("%s: snd_device(%d: %s)", __func__, snd_device, device_name);

        if (is_a2dp_device(snd_device) &&
            (audio_extn_a2dp_start_playback() < 0)) {
               ALOGE("%s: failed to configure A2DP control path", __func__);
               goto on_error;
        }

        audio_route_apply_and_update_path(adev->audio_route, device_name);
    }
on_success:
    adev->snd_dev_ref_cnt[snd_device]++;
    ret_val = 0;
on_error:
    return ret_val;
}

int disable_snd_device(struct audio_device *adev,
                       snd_device_t snd_device)
{
    int i, num_devices = 0;
    snd_device_t new_snd_devices[2];

    if (snd_device < SND_DEVICE_MIN ||
        snd_device >= SND_DEVICE_MAX) {
        ALOGE("%s: Invalid sound device %d", __func__, snd_device);
        return -EINVAL;
    }
    if (adev->snd_dev_ref_cnt[snd_device] <= 0) {
        ALOGE("%s: device ref cnt is already 0", __func__);
        return -EINVAL;
    }
    audio_extn_tfa_98xx_disable_speaker(snd_device);

    adev->snd_dev_ref_cnt[snd_device]--;
    if (adev->snd_dev_ref_cnt[snd_device] == 0) {
        audio_extn_dsm_feedback_enable(adev, snd_device, false);

        if (is_a2dp_device(snd_device))
            audio_extn_a2dp_stop_playback();

        if ((snd_device == SND_DEVICE_OUT_SPEAKER ||
            snd_device == SND_DEVICE_OUT_SPEAKER_SAFE ||
            snd_device == SND_DEVICE_OUT_SPEAKER_REVERSE ||
            snd_device == SND_DEVICE_OUT_VOICE_SPEAKER) &&
            audio_extn_spkr_prot_is_enabled()) {
            audio_extn_spkr_prot_stop_processing(snd_device);

            // FIXME b/65363602: bullhead is the only Nexus with audio_extn_spkr_prot_is_enabled()
            // and does not use speaker swap. As this code causes a problem with device enable ref
            // counting we remove it for now.
            // when speaker device is disabled, reset swap.
            // will be renabled on usecase start
            // platform_set_swap_channels(adev, false);

        } else if (platform_can_split_snd_device(snd_device,
                                                 &num_devices,
                                                 new_snd_devices) == 0) {
            for (i = 0; i < num_devices; i++) {
                disable_snd_device(adev, new_snd_devices[i]);
            }
            platform_set_speaker_gain_in_combo(adev, snd_device, false);
        } else {
            char device_name[DEVICE_NAME_MAX_SIZE] = {0};
            if (platform_get_snd_device_name_extn(adev->platform, snd_device, device_name) < 0 ) {
                ALOGE(" %s: Invalid sound device returned", __func__);
                return -EINVAL;
            }

            ALOGD("%s: snd_device(%d: %s)", __func__, snd_device, device_name);
            audio_route_reset_and_update_path(adev->audio_route, device_name);
        }
        audio_extn_sound_trigger_update_device_status(snd_device,
                                        ST_EVENT_SND_DEVICE_FREE);
    }

    return 0;
}

#ifdef DYNAMIC_ECNS_ENABLED
static int send_effect_enable_disable_mixer_ctl(struct audio_device *adev,
                          struct stream_in *in,
                          struct audio_effect_config effect_config,
                          unsigned int param_value)
{
    char mixer_ctl_name[] = "Audio Effect";
    long set_values[6];

    struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get mixer ctl - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    set_values[0] = 1; //0:Rx 1:Tx
    set_values[1] = in->app_type_cfg.app_type;
    set_values[2] = (long)effect_config.module_id;
    set_values[3] = (long)effect_config.instance_id;
    set_values[4] = (long)effect_config.param_id;
    set_values[5] = param_value;

    mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));

    return 0;

}

static int update_effect_param_ecns(struct audio_usecase *usecase,
                               unsigned int module_id, int effect_type,
                               unsigned int *param_value)
{
    int ret = 0;
    struct audio_effect_config other_effect_config;
    struct stream_in *in = NULL;

    if (!usecase)
        return -EINVAL;

    in = usecase->stream.in;

    /* Get the effect config data of the other effect */
    ret = platform_get_effect_config_data(usecase->in_snd_device,
                                          &other_effect_config,
                                          effect_type == EFFECT_AEC ? EFFECT_NS : EFFECT_AEC);
    if (ret < 0) {
        ALOGE("%s Failed to get effect params %d", __func__, ret);
        return ret;
    }

    if (module_id == other_effect_config.module_id) {
            //Same module id for AEC/NS. Values need to be combined
            if (((effect_type == EFFECT_AEC) && (in->enable_ns)) ||
                ((effect_type == EFFECT_NS) && (in->enable_aec)))
                *param_value |= other_effect_config.param_value;
    }

    return ret;
}

static int enable_disable_effect(struct audio_device *adev, struct stream_in *in,
                                   int effect_type, bool enable)
{
    struct audio_effect_config effect_config;
    struct audio_usecase *usecase = NULL;
    int ret = 0;
    unsigned int param_value = 0;

    if (!in) {
        ALOGE("%s: Invalid input stream", __func__);
        return -EINVAL;
    }

    ALOGD("%s: effect_type:%d enable:%d", __func__, effect_type, enable);

    usecase = get_usecase_from_list(adev, in->usecase);

    ret = platform_get_effect_config_data(usecase->in_snd_device,
                                           &effect_config, effect_type);
    if (ret < 0) {
        ALOGE("%s Failed to get module id %d", __func__, ret);
        return ret;
    }
    ALOGV("%s: module %d app_type %d usecase->id:%d usecase->in_snd_device:%d",
           __func__, effect_config.module_id, in->app_type_cfg.app_type,
          usecase->id, usecase->in_snd_device);

    if (enable)
        param_value = effect_config.param_value;

    /*Special handling for AEC & NS effects Param values need to be
      updated if module ids are same*/

    if ((effect_type == EFFECT_AEC) || (effect_type == EFFECT_NS)) {
        ret = update_effect_param_ecns(usecase, effect_config.module_id,
                                       effect_type, &param_value);
        if (ret < 0)
            return ret;
    }

    ret = send_effect_enable_disable_mixer_ctl(adev, in,
                                               effect_config, param_value);

    return ret;
}

static int check_and_enable_effect(struct audio_device *adev)
{
    int ret = 0;

    struct listnode *node;
    struct stream_in *in = NULL;

    list_for_each(node, &adev->usecase_list)
    {
        struct audio_usecase *usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_CAPTURE && usecase->stream.in != NULL) {
            in = usecase->stream.in;

            if (in->standby)
                continue;

            if (in->enable_aec) {
                ret = enable_disable_effect(adev, in, EFFECT_AEC, true);
            }

            if (in->enable_ns &&
                in->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
                ret = enable_disable_effect(adev, in, EFFECT_NS, true);
            }
        }
    }

    return ret;
}
#else
#define enable_disable_effect(w, x, y, z) -ENOSYS
#define check_and_enable_effect(x) -ENOSYS
#endif

/*
  legend:
  uc - existing usecase
  new_uc - new usecase
  d1, d11, d2 - SND_DEVICE enums
  a1, a2 - corresponding ANDROID device enums
  B, B1, B2 - backend strings

case 1
  uc->dev  d1 (a1)               B1
  new_uc->dev d1 (a1), d2 (a2)   B1, B2

  resolution: disable and enable uc->dev on d1

case 2
  uc->dev d1 (a1)        B1
  new_uc->dev d11 (a1)   B1

  resolution: need to switch uc since d1 and d11 are related
  (e.g. speaker and voice-speaker)
  use ANDROID_DEVICE_OUT enums to match devices since SND_DEVICE enums may vary

case 3
  uc->dev d1 (a1)        B1
  new_uc->dev d2 (a2)    B2

  resolution: no need to switch uc

case 4
  uc->dev d1 (a1)      B
  new_uc->dev d2 (a2)  B

  resolution: disable enable uc-dev on d2 since backends match
  we cannot enable two streams on two different devices if they
  share the same backend. e.g. if offload is on speaker device using
  QUAD_MI2S backend and a low-latency stream is started on voice-handset
  using the same backend, offload must also be switched to voice-handset.

case 5
  uc->dev  d1 (a1)                  B
  new_uc->dev d1 (a1), d2 (a2)      B

  resolution: disable enable uc-dev on d2 since backends match
  we cannot enable two streams on two different devices if they
  share the same backend.

case 6
  uc->dev  d1 a1    B1
  new_uc->dev d2 a1 B2

  resolution: no need to switch

case 7

  uc->dev d1 (a1), d2 (a2)       B1, B2
  new_uc->dev d1                 B1

  resolution: no need to switch

*/
static snd_device_t derive_playback_snd_device(struct audio_usecase *uc,
                                               struct audio_usecase *new_uc,
                                               snd_device_t new_snd_device)
{
    audio_devices_t a1 = uc->stream.out->devices;
    audio_devices_t a2 = new_uc->stream.out->devices;

    snd_device_t d1 = uc->out_snd_device;
    snd_device_t d2 = new_snd_device;

    // Treat as a special case when a1 and a2 are not disjoint
    if ((a1 != a2) && (a1 & a2)) {
        snd_device_t d3[2];
        int num_devices = 0;
        int ret = platform_can_split_snd_device(popcount(a1) > 1 ? d1 : d2,
                                                &num_devices,
                                                d3);
        if (ret < 0) {
            if (ret != -ENOSYS) {
                ALOGW("%s failed to split snd_device %d",
                      __func__,
                      popcount(a1) > 1 ? d1 : d2);
            }
            goto end;
        }

        // NB: case 7 is hypothetical and isn't a practical usecase yet.
        // But if it does happen, we need to give priority to d2 if
        // the combo devices active on the existing usecase share a backend.
        // This is because we cannot have a usecase active on a combo device
        // and a new usecase requests one device in this combo pair.
        if (platform_check_backends_match(d3[0], d3[1])) {
            return d2; // case 5
        } else {
            return d1; // case 1
        }
    } else {
        if (platform_check_backends_match(d1, d2)) {
            return d2; // case 2, 4
        } else {
            return d1; // case 6, 3
        }
    }

end:
    return d2; // return whatever was calculated before.
}

static void check_and_route_playback_usecases(struct audio_device *adev,
                                              struct audio_usecase *uc_info,
                                              snd_device_t snd_device)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    bool switch_device[AUDIO_USECASE_MAX];
    int i, num_uc_to_switch = 0;

    bool force_routing =  platform_check_and_set_playback_backend_cfg(adev,
                                                                      uc_info,
                                                                      snd_device);

    /* For a2dp device reconfigure all active sessions
     * with new AFE encoder format based on a2dp state
     */
    if ((SND_DEVICE_OUT_BT_A2DP == snd_device ||
         SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP == snd_device ||
         SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP == snd_device) &&
         audio_extn_a2dp_is_force_device_switch()) {
         force_routing = true;
    }

    /*
     * This function is to make sure that all the usecases that are active on
     * the hardware codec backend are always routed to any one device that is
     * handled by the hardware codec.
     * For example, if low-latency and deep-buffer usecases are currently active
     * on speaker and out_set_parameters(headset) is received on low-latency
     * output, then we have to make sure deep-buffer is also switched to headset,
     * because of the limitation that both the devices cannot be enabled
     * at the same time as they share the same backend.
     */
    /* Disable all the usecases on the shared backend other than the
       specified usecase */
    for (i = 0; i < AUDIO_USECASE_MAX; i++)
        switch_device[i] = false;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_CAPTURE || usecase == uc_info)
            continue;

        if (force_routing ||
            (usecase->out_snd_device != snd_device &&
             (usecase->devices & AUDIO_DEVICE_OUT_ALL_CODEC_BACKEND ||
              usecase->devices & (AUDIO_DEVICE_OUT_USB_DEVICE|AUDIO_DEVICE_OUT_USB_HEADSET)) &&
             platform_check_backends_match(snd_device, usecase->out_snd_device))) {
            ALOGV("%s: Usecase (%s) is active on (%s) - disabling ..",
                  __func__, use_case_table[usecase->id],
                  platform_get_snd_device_name(usecase->out_snd_device));
            disable_audio_route(adev, usecase);
            switch_device[usecase->id] = true;
            num_uc_to_switch++;
        }
    }

    if (num_uc_to_switch) {
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (switch_device[usecase->id]) {
                disable_snd_device(adev, usecase->out_snd_device);
            }
        }

        snd_device_t d_device;
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (switch_device[usecase->id]) {
                d_device = derive_playback_snd_device(usecase, uc_info,
                                                      snd_device);
                enable_snd_device(adev, d_device);
                /* Update the out_snd_device before enabling the audio route */
                usecase->out_snd_device = d_device;
            }
        }

        /* Re-route all the usecases on the shared backend other than the
           specified usecase to new snd devices */
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (switch_device[usecase->id] ) {
                enable_audio_route(adev, usecase);
                if (usecase->stream.out && usecase->id == USECASE_AUDIO_PLAYBACK_VOIP) {
                    struct stream_out *out = usecase->stream.out;
                    audio_extn_utils_send_app_type_gain(out->dev,
                                                        out->app_type_cfg.app_type,
                                                        &out->app_type_cfg.gain[0]);
                }
            }
        }
    }
}

static void check_and_route_capture_usecases(struct audio_device *adev,
                                             struct audio_usecase *uc_info,
                                             snd_device_t snd_device)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    bool switch_device[AUDIO_USECASE_MAX];
    int i, num_uc_to_switch = 0;

    platform_check_and_set_capture_backend_cfg(adev, uc_info, snd_device);

    /*
     * This function is to make sure that all the active capture usecases
     * are always routed to the same input sound device.
     * For example, if audio-record and voice-call usecases are currently
     * active on speaker(rx) and speaker-mic (tx) and out_set_parameters(earpiece)
     * is received for voice call then we have to make sure that audio-record
     * usecase is also switched to earpiece i.e. voice-dmic-ef,
     * because of the limitation that two devices cannot be enabled
     * at the same time if they share the same backend.
     */
    for (i = 0; i < AUDIO_USECASE_MAX; i++)
        switch_device[i] = false;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type != PCM_PLAYBACK &&
                usecase != uc_info &&
                usecase->in_snd_device != snd_device &&
                ((uc_info->type == VOICE_CALL &&
                  usecase->devices == AUDIO_DEVICE_IN_VOICE_CALL) ||
                 platform_check_backends_match(snd_device,\
                                              usecase->in_snd_device)) &&
                (usecase->id != USECASE_AUDIO_SPKR_CALIB_TX)) {
            ALOGV("%s: Usecase (%s) is active on (%s) - disabling ..",
                  __func__, use_case_table[usecase->id],
                  platform_get_snd_device_name(usecase->in_snd_device));
            disable_audio_route(adev, usecase);
            switch_device[usecase->id] = true;
            num_uc_to_switch++;
        }
    }

    if (num_uc_to_switch) {
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (switch_device[usecase->id]) {
                disable_snd_device(adev, usecase->in_snd_device);
            }
        }

        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (switch_device[usecase->id]) {
                enable_snd_device(adev, snd_device);
            }
        }

        /* Re-route all the usecases on the shared backend other than the
           specified usecase to new snd devices */
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            /* Update the in_snd_device only before enabling the audio route */
            if (switch_device[usecase->id] ) {
                usecase->in_snd_device = snd_device;
                enable_audio_route(adev, usecase);
            }
        }
    }
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

static ssize_t read_usb_sup_sample_rates(bool is_playback,
                                         uint32_t *supported_sample_rates,
                                         uint32_t max_rates)
{
    ssize_t count = audio_extn_usb_sup_sample_rates(is_playback,
                                                    supported_sample_rates,
                                                    max_rates);
#if !LOG_NDEBUG
    for (ssize_t i=0; i<count; i++) {
        ALOGV("%s %s %d", __func__, is_playback ? "P" : "C",
              supported_sample_rates[i]);
    }
#endif
    return count;
}

static int read_usb_sup_channel_masks(bool is_playback,
                                      audio_channel_mask_t *supported_channel_masks,
                                      uint32_t max_masks)
{
    int channels = audio_extn_usb_get_max_channels(is_playback);
    int channel_count;
    uint32_t num_masks = 0;
    if (channels > MAX_HIFI_CHANNEL_COUNT) {
        channels = MAX_HIFI_CHANNEL_COUNT;
    }
    if (is_playback) {
        // start from 2 channels as framework currently doesn't support mono.
        if (channels >= FCC_2) {
            supported_channel_masks[num_masks++] = audio_channel_out_mask_from_count(FCC_2);
        }
        for (channel_count = FCC_2;
                channel_count <= channels && num_masks < max_masks;
                ++channel_count) {
            supported_channel_masks[num_masks++] =
                    audio_channel_mask_for_index_assignment_from_count(channel_count);
        }
    } else {
        // For capture we report all supported channel masks from 1 channel up.
        channel_count = MIN_CHANNEL_COUNT;
        // audio_channel_in_mask_from_count() does the right conversion to either positional or
        // indexed mask
        for ( ; channel_count <= channels && num_masks < max_masks; channel_count++) {
            audio_channel_mask_t mask = AUDIO_CHANNEL_NONE;
            if (channel_count <= FCC_2) {
                mask = audio_channel_in_mask_from_count(channel_count);
                supported_channel_masks[num_masks++] = mask;
            }
            const audio_channel_mask_t index_mask =
                    audio_channel_mask_for_index_assignment_from_count(channel_count);
            if (mask != index_mask && num_masks < max_masks) { // ensure index mask added.
                supported_channel_masks[num_masks++] = index_mask;
            }
        }
    }
#ifdef NDEBUG
    for (size_t i = 0; i < num_masks; ++i) {
        ALOGV("%s: %s supported ch %d supported_channel_masks[%zu] %08x num_masks %d", __func__,
              is_playback ? "P" : "C", channels, i, supported_channel_masks[i], num_masks);
    }
#endif
    return num_masks;
}

static int read_usb_sup_formats(bool is_playback __unused,
                                audio_format_t *supported_formats,
                                uint32_t max_formats __unused)
{
    int bitwidth = audio_extn_usb_get_max_bit_width(is_playback);
    switch (bitwidth) {
        case 24:
            // XXX : usb.c returns 24 for s24 and s24_le?
            supported_formats[0] = AUDIO_FORMAT_PCM_24_BIT_PACKED;
            break;
        case 32:
            supported_formats[0] = AUDIO_FORMAT_PCM_32_BIT;
            break;
        case 16:
        default :
            supported_formats[0] = AUDIO_FORMAT_PCM_16_BIT;
            break;
    }
    ALOGV("%s: %s supported format %d", __func__,
          is_playback ? "P" : "C", bitwidth);
    return 1;
}

static int read_usb_sup_params_and_compare(bool is_playback,
                                           audio_format_t *format,
                                           audio_format_t *supported_formats,
                                           uint32_t max_formats,
                                           audio_channel_mask_t *mask,
                                           audio_channel_mask_t *supported_channel_masks,
                                           uint32_t max_masks,
                                           uint32_t *rate,
                                           uint32_t *supported_sample_rates,
                                           uint32_t max_rates) {
    int ret = 0;
    int num_formats;
    int num_masks;
    int num_rates;
    int i;

    num_formats = read_usb_sup_formats(is_playback, supported_formats,
                                       max_formats);
    num_masks = read_usb_sup_channel_masks(is_playback, supported_channel_masks,
                                           max_masks);

    num_rates = read_usb_sup_sample_rates(is_playback,
                                          supported_sample_rates, max_rates);

#define LUT(table, len, what, dflt)                  \
    for (i=0; i<len && (table[i] != what); i++);    \
    if (i==len) { ret |= (what == dflt ? 0 : -1); what=table[0]; }

    LUT(supported_formats, num_formats, *format, AUDIO_FORMAT_DEFAULT);
    LUT(supported_channel_masks, num_masks, *mask, AUDIO_CHANNEL_NONE);
    LUT(supported_sample_rates, num_rates, *rate, 0);

#undef LUT
    return ret < 0 ? -EINVAL : 0; // HACK TBD
}

static bool is_usb_ready(struct audio_device *adev, bool is_playback)
{
    // Check if usb is ready.
    // The usb device may have been removed quickly after insertion and hence
    // no longer available.  This will show up as empty channel masks, or rates.

    pthread_mutex_lock(&adev->lock);
    uint32_t supported_sample_rate;

    // we consider usb ready if we can fetch at least one sample rate.
    const bool ready = read_usb_sup_sample_rates(
            is_playback, &supported_sample_rate, 1 /* max_rates */) > 0;
    pthread_mutex_unlock(&adev->lock);
    return ready;
}

static audio_usecase_t get_voice_usecase_id_from_list(struct audio_device *adev)
{
    struct audio_usecase *usecase;
    struct listnode *node;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == VOICE_CALL) {
            ALOGV("%s: usecase id %d", __func__, usecase->id);
            return usecase->id;
        }
    }
    return USECASE_INVALID;
}

struct audio_usecase *get_usecase_from_list(struct audio_device *adev,
                                            audio_usecase_t uc_id)
{
    struct audio_usecase *usecase;
    struct listnode *node;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->id == uc_id)
            return usecase;
    }
    return NULL;
}

static bool force_device_switch(struct audio_usecase *usecase)
{
    if (usecase->type == PCM_CAPTURE || usecase->stream.out == NULL) {
        return false;
    }

    // Force all A2DP output devices to reconfigure for proper AFE encode format
    // Also handle a case where in earlier A2DP start failed as A2DP stream was
    // in suspended state, hence try to trigger a retry when we again get a routing request.
    if ((usecase->stream.out->devices & AUDIO_DEVICE_OUT_ALL_A2DP) &&
        audio_extn_a2dp_is_force_device_switch()) {
         ALOGD("%s: Force A2DP device switch to update new encoder config", __func__);
         return true;
    }

    return false;
}

struct stream_in *adev_get_active_input(const struct audio_device *adev)
{
    struct listnode *node;
    struct stream_in *last_active_in = NULL;

    /* Get last added active input.
     * TODO: We may use a priority mechanism to pick highest priority active source */
    list_for_each(node, &adev->usecase_list)
    {
        struct audio_usecase *usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_CAPTURE && usecase->stream.in != NULL) {
            last_active_in =  usecase->stream.in;
        }
    }

    return last_active_in;
}

struct stream_in *get_voice_communication_input(const struct audio_device *adev)
{
    struct listnode *node;

    /* First check active inputs with voice communication source and then
     * any input if audio mode is in communication */
    list_for_each(node, &adev->usecase_list)
    {
        struct audio_usecase *usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_CAPTURE && usecase->stream.in != NULL &&
            usecase->stream.in->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
            return usecase->stream.in;
        }
    }
    if (adev->mode == AUDIO_MODE_IN_COMMUNICATION) {
        return adev_get_active_input(adev);
    }
    return NULL;
}

/*
 * Aligned with policy.h
 */
static inline int source_priority(int inputSource)
{
    switch (inputSource) {
    case AUDIO_SOURCE_VOICE_COMMUNICATION:
        return 9;
    case AUDIO_SOURCE_CAMCORDER:
        return 8;
    case AUDIO_SOURCE_VOICE_PERFORMANCE:
        return 7;
    case AUDIO_SOURCE_UNPROCESSED:
        return 6;
    case AUDIO_SOURCE_MIC:
        return 5;
    case AUDIO_SOURCE_ECHO_REFERENCE:
        return 4;
    case AUDIO_SOURCE_FM_TUNER:
        return 3;
    case AUDIO_SOURCE_VOICE_RECOGNITION:
        return 2;
    case AUDIO_SOURCE_HOTWORD:
        return 1;
    default:
        break;
    }
    return 0;
}

static struct stream_in *get_priority_input(struct audio_device *adev)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    int last_priority = 0, priority;
    struct stream_in *priority_in = NULL;
    struct stream_in *in;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_CAPTURE) {
            in = usecase->stream.in;
            if (!in)
                continue;
            priority = source_priority(in->source);

            if (priority > last_priority) {
                last_priority = priority;
                priority_in = in;
            }
        }
    }
    return priority_in;
}

int select_devices_with_force_switch(struct audio_device *adev,
                                     audio_usecase_t uc_id,
                                     bool force_switch)
{
    snd_device_t out_snd_device = SND_DEVICE_NONE;
    snd_device_t in_snd_device = SND_DEVICE_NONE;
    struct audio_usecase *usecase = NULL;
    struct audio_usecase *vc_usecase = NULL;
    struct audio_usecase *hfp_usecase = NULL;
    audio_usecase_t hfp_ucid;
    struct listnode *node;
    int status = 0;
    struct audio_usecase *voip_usecase = get_usecase_from_list(adev,
                                             USECASE_AUDIO_PLAYBACK_VOIP);

    usecase = get_usecase_from_list(adev, uc_id);
    if (usecase == NULL) {
        ALOGE("%s: Could not find the usecase(%d)", __func__, uc_id);
        return -EINVAL;
    }

    if ((usecase->type == VOICE_CALL) ||
        (usecase->type == PCM_HFP_CALL)) {
        out_snd_device = platform_get_output_snd_device(adev->platform,
                                                        usecase->stream.out->devices);
        in_snd_device = platform_get_input_snd_device(adev->platform,
                                                      NULL,
                                                      usecase->stream.out->devices);
        usecase->devices = usecase->stream.out->devices;
    } else {
        /*
         * If the voice call is active, use the sound devices of voice call usecase
         * so that it would not result any device switch. All the usecases will
         * be switched to new device when select_devices() is called for voice call
         * usecase. This is to avoid switching devices for voice call when
         * check_and_route_playback_usecases() is called below.
         */
        if (voice_is_in_call(adev)) {
            vc_usecase = get_usecase_from_list(adev,
                                               get_voice_usecase_id_from_list(adev));
            if ((vc_usecase != NULL) &&
                ((vc_usecase->devices & AUDIO_DEVICE_OUT_ALL_CODEC_BACKEND) ||
                 (vc_usecase->devices == AUDIO_DEVICE_OUT_HEARING_AID) ||
                 (usecase->devices == AUDIO_DEVICE_IN_VOICE_CALL))) {
                in_snd_device = vc_usecase->in_snd_device;
                out_snd_device = vc_usecase->out_snd_device;
            }
        } else if (audio_extn_hfp_is_active(adev)) {
            hfp_ucid = audio_extn_hfp_get_usecase();
            hfp_usecase = get_usecase_from_list(adev, hfp_ucid);
            if (hfp_usecase->devices & AUDIO_DEVICE_OUT_ALL_CODEC_BACKEND) {
                   in_snd_device = hfp_usecase->in_snd_device;
                   out_snd_device = hfp_usecase->out_snd_device;
            }
        }
        if (usecase->type == PCM_PLAYBACK) {
            usecase->devices = usecase->stream.out->devices;
            in_snd_device = SND_DEVICE_NONE;
            if (out_snd_device == SND_DEVICE_NONE) {
                struct stream_out *voip_out = adev->primary_output;
                struct stream_in *voip_in = get_voice_communication_input(adev);

                out_snd_device = platform_get_output_snd_device(adev->platform,
                                            usecase->stream.out->devices);

                if (voip_usecase)
                    voip_out = voip_usecase->stream.out;

                if (usecase->stream.out == voip_out && voip_in != NULL) {
                    select_devices(adev, voip_in->usecase);
                }
            }
        } else if (usecase->type == PCM_CAPTURE) {
            usecase->devices = usecase->stream.in->device;
            out_snd_device = SND_DEVICE_NONE;
            if (in_snd_device == SND_DEVICE_NONE) {
                audio_devices_t out_device = AUDIO_DEVICE_NONE;
                struct stream_in *voip_in = get_voice_communication_input(adev);
                struct stream_in *priority_in = NULL;

                if (voip_in != NULL) {
                    struct audio_usecase *voip_usecase = get_usecase_from_list(adev,
                                                             USECASE_AUDIO_PLAYBACK_VOIP);

                    usecase->stream.in->enable_ec_port = false;

                    if (usecase->id == USECASE_AUDIO_RECORD_AFE_PROXY) {
                        out_device = AUDIO_DEVICE_OUT_TELEPHONY_TX;
                    } else if (voip_usecase) {
                        out_device = voip_usecase->stream.out->devices;
                    } else if (adev->primary_output &&
                                  !adev->primary_output->standby) {
                        out_device = adev->primary_output->devices;
                    } else {
                        /* forcing speaker o/p device to get matching i/p pair
                           in case o/p is not routed from same primary HAL */
                        out_device = AUDIO_DEVICE_OUT_SPEAKER;
                    }
                    priority_in = voip_in;
                } else {
                    /* get the input with the highest priority source*/
                    priority_in = get_priority_input(adev);

                    if (!priority_in)
                        priority_in = usecase->stream.in;
                }

                in_snd_device = platform_get_input_snd_device(adev->platform,
                                                              priority_in,
                                                              out_device);
            }
        }
    }

    if (out_snd_device == usecase->out_snd_device &&
        in_snd_device == usecase->in_snd_device) {
        if (!force_device_switch(usecase) && !force_switch)
            return 0;
    }

    if (is_a2dp_device(out_snd_device) && !audio_extn_a2dp_is_ready()) {
          ALOGD("SCO/A2DP is selected but they are not connected/ready hence dont route");
          return 0;
    }

    if ((out_snd_device == SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP ||
         out_snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP) &&
        (!audio_extn_a2dp_is_ready())) {
        ALOGW("%s: A2DP profile is not ready, routing to speaker only", __func__);
        if (out_snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP)
            out_snd_device = SND_DEVICE_OUT_SPEAKER_SAFE;
        else
            out_snd_device = SND_DEVICE_OUT_SPEAKER;
    }

    if (usecase->id == USECASE_INCALL_MUSIC_UPLINK ||
        usecase->id == USECASE_INCALL_MUSIC_UPLINK2) {
        out_snd_device = SND_DEVICE_OUT_VOICE_MUSIC_TX;
    }

    if (out_snd_device != SND_DEVICE_NONE &&
            out_snd_device != adev->last_logged_snd_device[uc_id][0]) {
        ALOGD("%s: changing use case %s output device from(%d: %s, acdb %d) to (%d: %s, acdb %d)",
              __func__,
              use_case_table[uc_id],
              adev->last_logged_snd_device[uc_id][0],
              platform_get_snd_device_name(adev->last_logged_snd_device[uc_id][0]),
              adev->last_logged_snd_device[uc_id][0] != SND_DEVICE_NONE ?
                      platform_get_snd_device_acdb_id(adev->last_logged_snd_device[uc_id][0]) :
                      -1,
              out_snd_device,
              platform_get_snd_device_name(out_snd_device),
              platform_get_snd_device_acdb_id(out_snd_device));
        adev->last_logged_snd_device[uc_id][0] = out_snd_device;
    }
    if (in_snd_device != SND_DEVICE_NONE &&
            in_snd_device != adev->last_logged_snd_device[uc_id][1]) {
        ALOGD("%s: changing use case %s input device from(%d: %s, acdb %d) to (%d: %s, acdb %d)",
              __func__,
              use_case_table[uc_id],
              adev->last_logged_snd_device[uc_id][1],
              platform_get_snd_device_name(adev->last_logged_snd_device[uc_id][1]),
              adev->last_logged_snd_device[uc_id][1] != SND_DEVICE_NONE ?
                      platform_get_snd_device_acdb_id(adev->last_logged_snd_device[uc_id][1]) :
                      -1,
              in_snd_device,
              platform_get_snd_device_name(in_snd_device),
              platform_get_snd_device_acdb_id(in_snd_device));
        adev->last_logged_snd_device[uc_id][1] = in_snd_device;
    }

    /*
     * Limitation: While in call, to do a device switch we need to disable
     * and enable both RX and TX devices though one of them is same as current
     * device.
     */
    if ((usecase->type == VOICE_CALL) &&
        (usecase->in_snd_device != SND_DEVICE_NONE) &&
        (usecase->out_snd_device != SND_DEVICE_NONE)) {
        status = platform_switch_voice_call_device_pre(adev->platform);
        /* Disable sidetone only if voice call already exists */
        if (voice_is_call_state_active(adev))
            voice_set_sidetone(adev, usecase->out_snd_device, false);
    }

    /* Disable current sound devices */
    if (usecase->out_snd_device != SND_DEVICE_NONE) {
        disable_audio_route(adev, usecase);
        disable_snd_device(adev, usecase->out_snd_device);
    }

    if (usecase->in_snd_device != SND_DEVICE_NONE) {
        disable_audio_route(adev, usecase);
        disable_snd_device(adev, usecase->in_snd_device);
    }

    /* Applicable only on the targets that has external modem.
     * New device information should be sent to modem before enabling
     * the devices to reduce in-call device switch time.
     */
    if ((usecase->type == VOICE_CALL) &&
        (usecase->in_snd_device != SND_DEVICE_NONE) &&
        (usecase->out_snd_device != SND_DEVICE_NONE)) {
        status = platform_switch_voice_call_enable_device_config(adev->platform,
                                                                 out_snd_device,
                                                                 in_snd_device);
    }

    /* Enable new sound devices */
    if (out_snd_device != SND_DEVICE_NONE) {
        if ((usecase->devices & AUDIO_DEVICE_OUT_ALL_CODEC_BACKEND) ||
            (usecase->devices & (AUDIO_DEVICE_OUT_USB_DEVICE|AUDIO_DEVICE_OUT_USB_HEADSET)) ||
            (usecase->devices & AUDIO_DEVICE_OUT_ALL_A2DP))
            check_and_route_playback_usecases(adev, usecase, out_snd_device);
        enable_snd_device(adev, out_snd_device);
    }

    if (in_snd_device != SND_DEVICE_NONE) {
        check_and_route_capture_usecases(adev, usecase, in_snd_device);
        enable_snd_device(adev, in_snd_device);
    }

    if (usecase->type == VOICE_CALL)
        status = platform_switch_voice_call_device_post(adev->platform,
                                                        out_snd_device,
                                                        in_snd_device);

    usecase->in_snd_device = in_snd_device;
    usecase->out_snd_device = out_snd_device;

    audio_extn_tfa_98xx_set_mode();

    enable_audio_route(adev, usecase);

    /* If input stream is already running the effect needs to be
       applied on the new input device that's being enabled here.  */
    if (in_snd_device != SND_DEVICE_NONE)
        check_and_enable_effect(adev);

    /* Applicable only on the targets that has external modem.
     * Enable device command should be sent to modem only after
     * enabling voice call mixer controls
     */
    if (usecase->type == VOICE_CALL) {
        status = platform_switch_voice_call_usecase_route_post(adev->platform,
                                                               out_snd_device,
                                                               in_snd_device);
         /* Enable sidetone only if voice call already exists */
        if (voice_is_call_state_active(adev))
            voice_set_sidetone(adev, out_snd_device, true);
    }

    if (usecase->type != PCM_CAPTURE && voip_usecase) {
        struct stream_out *voip_out = voip_usecase->stream.out;
        audio_extn_utils_send_app_type_gain(adev,
                                            voip_out->app_type_cfg.app_type,
                                            &voip_out->app_type_cfg.gain[0]);
    }
    return status;
}

int select_devices(struct audio_device *adev,
                   audio_usecase_t uc_id)
{
    return select_devices_with_force_switch(adev, uc_id, false);
}

static int stop_input_stream(struct stream_in *in)
{
    int i, ret = 0;
    struct audio_usecase *uc_info;
    struct audio_device *adev = in->dev;
    struct stream_in *priority_in = NULL;

    ALOGV("%s: enter: usecase(%d: %s)", __func__,
          in->usecase, use_case_table[in->usecase]);

    uc_info = get_usecase_from_list(adev, in->usecase);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, in->usecase);
        return -EINVAL;
    }

    priority_in = get_priority_input(adev);

    /* Close in-call recording streams */
    voice_check_and_stop_incall_rec_usecase(adev, in);

    /* 1. Disable stream specific mixer controls */
    disable_audio_route(adev, uc_info);

    /* 2. Disable the tx device */
    disable_snd_device(adev, uc_info->in_snd_device);

    list_remove(&uc_info->list);
    free(uc_info);

    if (priority_in == in) {
        priority_in = get_priority_input(adev);
        if (priority_in)
            select_devices(adev, priority_in->usecase);
    }

    ALOGV("%s: exit: status(%d)", __func__, ret);
    return ret;
}

int start_input_stream(struct stream_in *in)
{
    /* 1. Enable output device and stream routing controls */
    int ret = 0;
    struct audio_usecase *uc_info;
    struct audio_device *adev = in->dev;

    ALOGV("%s: enter: usecase(%d)", __func__, in->usecase);

    if (audio_extn_tfa_98xx_is_supported() && !audio_ssr_status(adev))
        return -EIO;

    if (in->card_status == CARD_STATUS_OFFLINE ||
        adev->card_status == CARD_STATUS_OFFLINE) {
        ALOGW("in->card_status or adev->card_status offline, try again");
        ret = -EAGAIN;
        goto error_config;
    }

    /* Check if source matches incall recording usecase criteria */
    ret = voice_check_and_set_incall_rec_usecase(adev, in);
    if (ret)
        goto error_config;
    else
        ALOGV("%s: usecase(%d)", __func__, in->usecase);

    in->pcm_device_id = platform_get_pcm_device_id(in->usecase, PCM_CAPTURE);
    if (in->pcm_device_id < 0) {
        ALOGE("%s: Could not find PCM device id for the usecase(%d)",
              __func__, in->usecase);
        ret = -EINVAL;
        goto error_config;
    }

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    uc_info->id = in->usecase;
    uc_info->type = PCM_CAPTURE;
    uc_info->stream.in = in;
    uc_info->devices = in->device;
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    list_add_tail(&adev->usecase_list, &uc_info->list);

    audio_streaming_hint_start();
    audio_extn_perf_lock_acquire();

    select_devices(adev, in->usecase);

    if (in->usecase == USECASE_AUDIO_RECORD_MMAP) {
        if (in->pcm == NULL || !pcm_is_ready(in->pcm)) {
            ALOGE("%s: pcm stream not ready", __func__);
            goto error_open;
        }
        ret = pcm_start(in->pcm);
        if (ret < 0) {
            ALOGE("%s: MMAP pcm_start failed ret %d", __func__, ret);
            goto error_open;
        }
    } else {
        unsigned int flags = PCM_IN | PCM_MONOTONIC;
        unsigned int pcm_open_retry_count = 0;

        if (in->usecase == USECASE_AUDIO_RECORD_AFE_PROXY) {
            flags |= PCM_MMAP | PCM_NOIRQ;
            pcm_open_retry_count = PROXY_OPEN_RETRY_COUNT;
        } else if (in->realtime) {
            flags |= PCM_MMAP | PCM_NOIRQ;
        }

        ALOGV("%s: Opening PCM device card_id(%d) device_id(%d), channels %d",
              __func__, adev->snd_card, in->pcm_device_id, in->config.channels);

        while (1) {
            in->pcm = pcm_open(adev->snd_card, in->pcm_device_id,
                               flags, &in->config);
            if (in->pcm == NULL || !pcm_is_ready(in->pcm)) {
                ALOGE("%s: %s", __func__, pcm_get_error(in->pcm));
                if (in->pcm != NULL) {
                    pcm_close(in->pcm);
                    in->pcm = NULL;
                }
                if (pcm_open_retry_count-- == 0) {
                    ret = -EIO;
                    goto error_open;
                }
                usleep(PROXY_OPEN_WAIT_TIME * 1000);
                continue;
            }
            break;
        }

        ALOGV("%s: pcm_prepare", __func__);
        ret = pcm_prepare(in->pcm);
        if (ret < 0) {
            ALOGE("%s: pcm_prepare returned %d", __func__, ret);
            pcm_close(in->pcm);
            in->pcm = NULL;
            goto error_open;
        }
        if (in->realtime) {
            ret = pcm_start(in->pcm);
            if (ret < 0) {
                ALOGE("%s: RT pcm_start failed ret %d", __func__, ret);
                pcm_close(in->pcm);
                in->pcm = NULL;
                goto error_open;
            }
        }
    }
    register_in_stream(in);
    check_and_enable_effect(adev);
    audio_extn_audiozoom_set_microphone_direction(in, in->zoom);
    audio_extn_audiozoom_set_microphone_field_dimension(in, in->direction);
    audio_streaming_hint_end();
    audio_extn_perf_lock_release();
    ALOGV("%s: exit", __func__);

    return 0;

error_open:
    stop_input_stream(in);
    audio_streaming_hint_end();
    audio_extn_perf_lock_release();

error_config:
    ALOGW("%s: exit: status(%d)", __func__, ret);
    return ret;
}

void lock_input_stream(struct stream_in *in)
{
    pthread_mutex_lock(&in->pre_lock);
    pthread_mutex_lock(&in->lock);
    pthread_mutex_unlock(&in->pre_lock);
}

void lock_output_stream(struct stream_out *out)
{
    pthread_mutex_lock(&out->pre_lock);
    pthread_mutex_lock(&out->lock);
    pthread_mutex_unlock(&out->pre_lock);
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

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
    set_sched_policy(0, SP_FOREGROUND);
    prctl(PR_SET_NAME, (unsigned long)"Offload Callback", 0, 0, 0);

    ALOGV("%s", __func__);

    lock_output_stream(out);
    out->offload_state = OFFLOAD_STATE_IDLE;
    out->playback_started = 0;
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
            free(cmd);
            pthread_cond_signal(&out->cond);
            continue;
        }
        out->offload_thread_blocked = true;
        pthread_mutex_unlock(&out->lock);
        send_callback = false;
        switch (cmd->cmd) {
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
            /* Resend the metadata for next iteration */
            out->send_new_metadata = 1;
            break;
        case OFFLOAD_CMD_DRAIN:
            compress_drain(out->compr);
            send_callback = true;
            event = STREAM_CBK_EVENT_DRAIN_READY;
            break;
        case OFFLOAD_CMD_ERROR:
            send_callback = true;
            event = STREAM_CBK_EVENT_ERROR;
            break;
        default:
            ALOGE("%s unknown command received: %d", __func__, cmd->cmd);
            break;
        }
        lock_output_stream(out);
        out->offload_thread_blocked = false;
        pthread_cond_signal(&out->cond);
        if (send_callback) {
            ALOGVV("%s: sending offload_callback event %d", __func__, event);
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
    lock_output_stream(out);
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
                ALOGV("%s: voice call is active, no change in HDMI channels",
                      __func__);
                ret = false;
                break;
            } else if (usecase->id == USECASE_AUDIO_PLAYBACK_HIFI) {
                ALOGV("%s: hifi playback is active, "
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
        ALOGV("%s: Requested channels are same as current", __func__);
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
            disable_audio_route(adev, usecase);
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
            enable_audio_route(adev, usecase);
        }
    }

    return 0;
}

static int check_and_set_usb_service_interval(struct audio_device *adev,
                                              struct audio_usecase *uc_info,
                                              bool min)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    bool switch_usecases = false;
    bool reconfig = false;

    if ((uc_info->id != USECASE_AUDIO_PLAYBACK_MMAP) &&
        (uc_info->id != USECASE_AUDIO_PLAYBACK_ULL))
        return -1;

    /* set if the valid usecase do not already exist */
    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_PLAYBACK &&
            (audio_is_usb_out_device(usecase->devices & AUDIO_DEVICE_OUT_ALL_USB))) {
            switch (usecase->id) {
                case USECASE_AUDIO_PLAYBACK_MMAP:
                case USECASE_AUDIO_PLAYBACK_ULL:
                    // cannot reconfig while mmap/ull is present.
                    return -1;
                default:
                    switch_usecases = true;
                    break;
            }
        }
        if (switch_usecases)
            break;
    }
    /*
     * client can try to set service interval in start_output_stream
     * to min or to 0 (i.e reset) in stop_output_stream .
     */
    unsigned long service_interval =
            audio_extn_usb_find_service_interval(min, true /*playback*/);
    int ret = platform_set_usb_service_interval(adev->platform,
                                                true /*playback*/,
                                                service_interval,
                                                &reconfig);
    /* no change or not supported or no active usecases */
    if (ret || !reconfig || !switch_usecases)
        return -1;
    return 0;
#undef VALID_USECASE
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

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        if (adev->visualizer_stop_output != NULL)
            adev->visualizer_stop_output(out->handle, out->pcm_device_id);
        if (adev->offload_effects_stop_output != NULL)
            adev->offload_effects_stop_output(out->handle, out->pcm_device_id);
    } else if (out->usecase == USECASE_AUDIO_PLAYBACK_ULL ||
               out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
        audio_low_latency_hint_end();
    }

    if (out->usecase == USECASE_INCALL_MUSIC_UPLINK ||
        out->usecase == USECASE_INCALL_MUSIC_UPLINK2) {
        voice_set_device_mute_flag(adev, false);
    }

    /* 1. Get and set stream specific mixer controls */
    disable_audio_route(adev, uc_info);

    /* 2. Disable the rx device */
    disable_snd_device(adev, uc_info->out_snd_device);

    list_remove(&uc_info->list);

    audio_extn_extspk_update(adev->extspk);

    /* Must be called after removing the usecase from list */
    if (out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
        check_and_set_hdmi_channels(adev, DEFAULT_HDMI_OUT_CHANNELS);
    else if (audio_is_usb_out_device(out->devices & AUDIO_DEVICE_OUT_ALL_USB)) {
        ret = check_and_set_usb_service_interval(adev, uc_info, false /*min*/);
        if (ret == 0) {
            /* default service interval was successfully updated,
               reopen USB backend with new service interval */
            check_and_route_playback_usecases(adev, uc_info, uc_info->out_snd_device);
        }
        ret = 0;
    }
    /* 1) media + voip output routing to handset must route media back to
          speaker when voip stops.
       2) trigger voip input to reroute when voip output changes to
          hearing aid. */
    if (out->usecase == USECASE_AUDIO_PLAYBACK_VOIP ||
        out->devices & AUDIO_DEVICE_OUT_SPEAKER_SAFE) {
        struct listnode *node;
        struct audio_usecase *usecase;
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if ((usecase->type == PCM_CAPTURE &&
                     usecase->id != USECASE_AUDIO_RECORD_VOIP)
                || usecase == uc_info)
                continue;

            ALOGD("%s: select_devices at usecase(%d: %s) after removing the usecase(%d: %s)",
                __func__, usecase->id, use_case_table[usecase->id],
                out->usecase, use_case_table[out->usecase]);
            select_devices(adev, usecase->id);
        }
    }

    free(uc_info);
    ALOGV("%s: exit: status(%d)", __func__, ret);
    return ret;
}

struct pcm* pcm_open_prepare_helper(unsigned int snd_card, unsigned int pcm_device_id,
                                   unsigned int flags, unsigned int pcm_open_retry_count,
                                   struct pcm_config *config)
{
    struct pcm* pcm = NULL;

    while (1) {
        pcm = pcm_open(snd_card, pcm_device_id, flags, config);
        if (pcm == NULL || !pcm_is_ready(pcm)) {
            ALOGE("%s: %s", __func__, pcm_get_error(pcm));
            if (pcm != NULL) {
                pcm_close(pcm);
                pcm = NULL;
            }
            if (pcm_open_retry_count-- == 0)
                return NULL;

            usleep(PROXY_OPEN_WAIT_TIME * 1000);
            continue;
        }
        break;
    }

    if (pcm_is_ready(pcm)) {
        int ret = pcm_prepare(pcm);
        if (ret < 0) {
            ALOGE("%s: pcm_prepare returned %d", __func__, ret);
            pcm_close(pcm);
            pcm = NULL;
        }
    }

    return pcm;
}

int start_output_stream(struct stream_out *out)
{
    int ret = 0;
    struct audio_usecase *uc_info;
    struct audio_device *adev = out->dev;
    bool a2dp_combo = false;

    ALOGV("%s: enter: usecase(%d: %s) %s devices(%#x)",
          __func__, out->usecase, use_case_table[out->usecase],
          out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS ? "(with haptics)" : "",
          out->devices);

    if (out->card_status == CARD_STATUS_OFFLINE ||
        adev->card_status == CARD_STATUS_OFFLINE) {
        ALOGW("out->card_status or adev->card_status offline, try again");
        ret = -EAGAIN;
        goto error_config;
    }

    //Update incall music usecase to reflect correct voice session
    if (out->flags & AUDIO_OUTPUT_FLAG_INCALL_MUSIC) {
        ret = voice_extn_check_and_set_incall_music_usecase(adev, out);
        if (ret != 0) {
            ALOGE("%s: Incall music delivery usecase cannot be set error:%d",
                __func__, ret);
            goto error_config;
        }
    }

    if (out->devices & AUDIO_DEVICE_OUT_ALL_A2DP) {
        if (!audio_extn_a2dp_is_ready()) {
            if (out->devices & (AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_SPEAKER_SAFE)) {
                a2dp_combo = true;
            } else {
                if (!(out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
                    ALOGE("%s: A2DP profile is not ready, return error", __func__);
                    ret = -EAGAIN;
                    goto error_config;
                }
            }
        }
    }
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
    else if (audio_is_usb_out_device(out->devices & AUDIO_DEVICE_OUT_ALL_USB)) {
        check_and_set_usb_service_interval(adev, uc_info, true /*min*/);
        /* USB backend is not reopened immediately.
           This is eventually done as part of select_devices */
    }

    list_add_tail(&adev->usecase_list, &uc_info->list);

    audio_streaming_hint_start();
    audio_extn_perf_lock_acquire();

    if ((out->devices & AUDIO_DEVICE_OUT_ALL_A2DP) &&
        (!audio_extn_a2dp_is_ready())) {
        if (!a2dp_combo) {
            check_a2dp_restore_l(adev, out, false);
        } else {
            audio_devices_t dev = out->devices;
            if (dev & AUDIO_DEVICE_OUT_SPEAKER_SAFE)
                out->devices = AUDIO_DEVICE_OUT_SPEAKER_SAFE;
            else
                out->devices = AUDIO_DEVICE_OUT_SPEAKER;
            select_devices(adev, out->usecase);
            out->devices = dev;
        }
    } else {
         select_devices(adev, out->usecase);
    }

    audio_extn_extspk_update(adev->extspk);

    if (out->usecase == USECASE_INCALL_MUSIC_UPLINK ||
        out->usecase == USECASE_INCALL_MUSIC_UPLINK2) {
        voice_set_device_mute_flag(adev, true);
    }

    ALOGV("%s: Opening PCM device card_id(%d) device_id(%d) format(%#x)",
          __func__, adev->snd_card, out->pcm_device_id, out->config.format);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        out->pcm = NULL;
        out->compr = compress_open(adev->snd_card, out->pcm_device_id,
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

        if (adev->visualizer_start_output != NULL) {
            int capture_device_id =
                platform_get_pcm_device_id(USECASE_AUDIO_RECORD_AFE_PROXY,
                                           PCM_CAPTURE);
            adev->visualizer_start_output(out->handle, out->pcm_device_id,
                                          adev->snd_card, capture_device_id);
        }
        if (adev->offload_effects_start_output != NULL)
            adev->offload_effects_start_output(out->handle, out->pcm_device_id);
    } else if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
        if (out->pcm == NULL || !pcm_is_ready(out->pcm)) {
            ALOGE("%s: pcm stream not ready", __func__);
            goto error_open;
        }
        ret = pcm_start(out->pcm);
        if (ret < 0) {
            ALOGE("%s: MMAP pcm_start failed ret %d", __func__, ret);
            goto error_open;
        }
    } else {
        unsigned int flags = PCM_OUT | PCM_MONOTONIC;
        unsigned int pcm_open_retry_count = 0;

        if (out->usecase == USECASE_AUDIO_PLAYBACK_AFE_PROXY) {
            flags |= PCM_MMAP | PCM_NOIRQ;
            pcm_open_retry_count = PROXY_OPEN_RETRY_COUNT;
        } else if (out->realtime) {
            flags |= PCM_MMAP | PCM_NOIRQ;
        }

        out->pcm = pcm_open_prepare_helper(adev->snd_card, out->pcm_device_id,
                                       flags, pcm_open_retry_count,
                                       &(out->config));
        if (out->pcm == NULL) {
           ret = -EIO;
           goto error_open;
        }

        if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
            if (adev->haptic_pcm != NULL) {
                pcm_close(adev->haptic_pcm);
                adev->haptic_pcm = NULL;
            }
            adev->haptic_pcm = pcm_open_prepare_helper(adev->snd_card,
                                   adev->haptic_pcm_device_id,
                                   flags, pcm_open_retry_count,
                                   &(adev->haptics_config));
            // failure to open haptics pcm shouldnt stop audio,
            // so do not close audio pcm in case of error
        }

        if (out->realtime) {
            ret = pcm_start(out->pcm);
            if (ret < 0) {
                ALOGE("%s: RT pcm_start failed ret %d", __func__, ret);
                pcm_close(out->pcm);
                out->pcm = NULL;
                goto error_open;
            }
        }
        if ((out->usecase == USECASE_AUDIO_PLAYBACK_LOW_LATENCY
                || out->usecase == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER
                || out->usecase == USECASE_AUDIO_PLAYBACK_ULL)) {
           out_set_pcm_volume(&out->stream, out->volume_l, out->volume_r);
         }
    }

    register_out_stream(out);
    audio_streaming_hint_end();
    audio_extn_perf_lock_release();
    audio_extn_tfa_98xx_enable_speaker();

    if (out->usecase == USECASE_AUDIO_PLAYBACK_ULL ||
        out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
        audio_low_latency_hint_start();
    }

    // consider a scenario where on pause lower layers are tear down.
    // so on resume, swap mixer control need to be sent only when
    // backend is active, hence rather than sending from enable device
    // sending it from start of stream

    platform_set_swap_channels(adev, true);

    ALOGV("%s: exit", __func__);
    return 0;
error_open:
    if (adev->haptic_pcm) {
        pcm_close(adev->haptic_pcm);
        adev->haptic_pcm = NULL;
    }
    audio_streaming_hint_end();
    audio_extn_perf_lock_release();
    stop_output_stream(out);
error_config:
    return ret;
}

static int check_input_parameters(uint32_t sample_rate,
                                  audio_format_t format,
                                  int channel_count, bool is_usb_hifi)
{
    if ((format != AUDIO_FORMAT_PCM_16_BIT) &&
        (format != AUDIO_FORMAT_PCM_8_24_BIT) &&
        (format != AUDIO_FORMAT_PCM_24_BIT_PACKED) &&
        !(is_usb_hifi && (format == AUDIO_FORMAT_PCM_32_BIT))) {
        ALOGE("%s: unsupported AUDIO FORMAT (%d) ", __func__, format);
        return -EINVAL;
    }

    int max_channel_count = is_usb_hifi ? MAX_HIFI_CHANNEL_COUNT : MAX_CHANNEL_COUNT;
    if ((channel_count < MIN_CHANNEL_COUNT) || (channel_count > max_channel_count)) {
        ALOGE("%s: unsupported channel count (%d) passed  Min / Max (%d / %d)", __func__,
               channel_count, MIN_CHANNEL_COUNT, max_channel_count);
        return -EINVAL;
    }

    switch (sample_rate) {
    case 8000:
    case 11025:
    case 12000:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
    case 96000:
        break;
    default:
        ALOGE("%s: unsupported (%d) samplerate passed ", __func__, sample_rate);
        return -EINVAL;
    }

    return 0;
}

/** Add a value in a list if not already present.
 * @return true if value was successfully inserted or already present,
 *         false if the list is full and does not contain the value.
 */
static bool register_uint(uint32_t value, uint32_t* list, size_t list_length) {
    for (size_t i = 0; i < list_length; i++) {
        if (list[i] == value) return true; // value is already present
        if (list[i] == 0) { // no values in this slot
            list[i] = value;
            return true; // value inserted
        }
    }
    return false; // could not insert value
}

/** Add channel_mask in supported_channel_masks if not already present.
 * @return true if channel_mask was successfully inserted or already present,
 *         false if supported_channel_masks is full and does not contain channel_mask.
 */
static void register_channel_mask(audio_channel_mask_t channel_mask,
            audio_channel_mask_t supported_channel_masks[static MAX_SUPPORTED_CHANNEL_MASKS]) {
    ALOGE_IF(!register_uint(channel_mask, supported_channel_masks, MAX_SUPPORTED_CHANNEL_MASKS),
        "%s: stream can not declare supporting its channel_mask %x", __func__, channel_mask);
}

/** Add format in supported_formats if not already present.
 * @return true if format was successfully inserted or already present,
 *         false if supported_formats is full and does not contain format.
 */
static void register_format(audio_format_t format,
            audio_format_t supported_formats[static MAX_SUPPORTED_FORMATS]) {
    ALOGE_IF(!register_uint(format, supported_formats, MAX_SUPPORTED_FORMATS),
             "%s: stream can not declare supporting its format %x", __func__, format);
}
/** Add sample_rate in supported_sample_rates if not already present.
 * @return true if sample_rate was successfully inserted or already present,
 *         false if supported_sample_rates is full and does not contain sample_rate.
 */
static void register_sample_rate(uint32_t sample_rate,
            uint32_t supported_sample_rates[static MAX_SUPPORTED_SAMPLE_RATES]) {
    ALOGE_IF(!register_uint(sample_rate, supported_sample_rates, MAX_SUPPORTED_SAMPLE_RATES),
             "%s: stream can not declare supporting its sample rate %x", __func__, sample_rate);
}

static size_t get_stream_buffer_size(size_t duration_ms,
                                     uint32_t sample_rate,
                                     audio_format_t format,
                                     int channel_count,
                                     bool is_low_latency)
{
    // Compute target frames based on time or period size.
    size_t target_frames = is_low_latency
             ? configured_low_latency_capture_period_size // record only
             : (sample_rate * duration_ms) / 1000;

    // Round up to a multiple of 16 frames in case sizing for the MixerThread.
    if (!is_low_latency) { // low latency flag set for record only
        target_frames = (target_frames + 0xf) & ~0xf;
    }

    // Buffer size is the target frames multiplied by the frame size in bytes.
    const size_t frame_size = channel_count * audio_bytes_per_sample(format);
    const size_t buffer_size = target_frames * frame_size;

    return buffer_size;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    return out->sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
{
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        return out->compr_config.fragment_size;
    }
    return out->config.period_size * out->af_period_multiplier *
                audio_stream_out_frame_size((const struct audio_stream_out *)stream);
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

static int out_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return -ENOSYS;
}

/* must be called with out->lock locked */
static int out_standby_l(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    bool do_stop = true;

    if (!out->standby) {
        if (adev->adm_deregister_stream)
            adev->adm_deregister_stream(adev->adm_data, out->handle);
        pthread_mutex_lock(&adev->lock);
        out->standby = true;
        if (out->usecase != USECASE_AUDIO_PLAYBACK_OFFLOAD) {
            if (out->pcm) {
                pcm_close(out->pcm);
                out->pcm = NULL;

                if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
                    if (adev->haptic_pcm) {
                        pcm_close(adev->haptic_pcm);
                        adev->haptic_pcm = NULL;
                    }

                    if (adev->haptic_buffer != NULL) {
                        free(adev->haptic_buffer);
                        adev->haptic_buffer = NULL;
                        adev->haptic_buffer_size = 0;
                    }
                }
            }
            if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
                do_stop = out->playback_started;
                out->playback_started = false;

                if (out->mmap_shared_memory_fd >= 0) {
                    ALOGV("%s: closing mmap_shared_memory_fd = %d",
                          __func__, out->mmap_shared_memory_fd);
                    close(out->mmap_shared_memory_fd);
                    out->mmap_shared_memory_fd = -1;
                }

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
        if (do_stop) {
            stop_output_stream(out);
        }
        pthread_mutex_unlock(&adev->lock);
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    ALOGV("%s: enter: usecase(%d: %s)", __func__,
          out->usecase, use_case_table[out->usecase]);

    lock_output_stream(out);
    out_standby_l(stream);
    pthread_mutex_unlock(&out->lock);
    ALOGV("%s: exit", __func__);
    return 0;
}

static int out_on_error(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    bool do_standby = false;

    lock_output_stream(out);
    if (!out->standby) {
        if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
            stop_compressed_output_l(out);
            send_offload_cmd_l(out, OFFLOAD_CMD_ERROR);
        } else
            do_standby = true;
    }
    pthread_mutex_unlock(&out->lock);

    if (do_standby)
        return out_standby(&out->stream.common);

    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    struct stream_out *out = (struct stream_out *)stream;

    // We try to get the lock for consistency,
    // but it isn't necessary for these variables.
    // If we're not in standby, we may be blocked on a write.
    const bool locked = (pthread_mutex_trylock(&out->lock) == 0);
    dprintf(fd, "      Standby: %s\n", out->standby ? "yes" : "no");
    dprintf(fd, "      Frames written: %lld\n", (long long)out->written);

    char buffer[256]; // for statistics formatting
    simple_stats_to_string(&out->fifo_underruns, buffer, sizeof(buffer));
    dprintf(fd, "      Fifo frame underruns: %s\n", buffer);

    if (out->start_latency_ms.n > 0) {
        simple_stats_to_string(&out->start_latency_ms, buffer, sizeof(buffer));
        dprintf(fd, "      Start latency ms: %s\n", buffer);
    }

    if (locked) {
        pthread_mutex_unlock(&out->lock);
    }

    // dump error info
    (void)error_log_dump(
            out->error_log, fd, "      " /* prefix */, 0 /* lines */, 0 /* limit_ns */);

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

static bool output_drives_call(struct audio_device *adev, struct stream_out *out)
{
    return out == adev->primary_output || out == adev->voice_tx_output;
}

static int get_alive_usb_card(struct str_parms* parms) {
    int card;
    if ((str_parms_get_int(parms, "card", &card) >= 0) &&
        !audio_extn_usb_alive(card)) {
        return card;
    }
    return -ENODEV;
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
    int status = 0;
    bool bypass_a2dp = false;
    bool forced_speaker_fallback = false;

    ALOGD("%s: enter: usecase(%d: %s) kvpairs: %s",
          __func__, out->usecase, use_case_table[out->usecase], kvpairs);
    parms = str_parms_create_str(kvpairs);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);

        lock_output_stream(out);

        if (val == AUDIO_DEVICE_NONE &&
                audio_is_usb_out_device(out->devices)) {
            val = AUDIO_DEVICE_OUT_SPEAKER;
            forced_speaker_fallback = true;
        }

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
            forced_speaker_fallback = true;
        }

        /*
         * When A2DP is disconnected the
         * music playback is paused and the policy manager sends routing=0
         * But the audioflingercontinues to write data until standby time
         * (3sec). As BT is turned off, the write gets blocked.
         * Avoid this by routing audio to speaker until standby.
         */
        if ((out->devices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) &&
                (val == AUDIO_DEVICE_NONE) &&
                !audio_extn_a2dp_is_ready() &&
                !adev->bt_sco_on) {
                val = AUDIO_DEVICE_OUT_SPEAKER;
                forced_speaker_fallback = true;
        }

        /* To avoid a2dp to sco overlapping / BT device improper state
         * check with BT lib about a2dp streaming support before routing
         */
        if (val & AUDIO_DEVICE_OUT_ALL_A2DP) {
            if (!audio_extn_a2dp_is_ready()) {
                if (val & (AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_SPEAKER_SAFE)) {
                    //combo usecase just by pass a2dp
                    ALOGW("%s: A2DP profile is not ready,routing to speaker only", __func__);
                    bypass_a2dp = true;
                } else {
                    ALOGE("%s: A2DP profile is not ready,ignoring routing request", __func__);
                    /* update device to a2dp and don't route as BT returned error
                     * However it is still possible a2dp routing called because
                     * of current active device disconnection (like wired headset)
                     */
                    out->devices = val;
                    pthread_mutex_unlock(&out->lock);
                    pthread_mutex_unlock(&adev->lock);
                    status = -ENOSYS;
                    goto routing_fail;
                }
            }
        }

        audio_devices_t new_dev = val;

        // Workaround: If routing to an non existing usb device, fail gracefully
        // The routing request will otherwise block during 10 second
        int card;
        if (audio_is_usb_out_device(new_dev) &&
            (card = get_alive_usb_card(parms)) >= 0) {

            ALOGW("out_set_parameters() ignoring rerouting to non existing USB card %d", card);
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_unlock(&out->lock);
            status = -ENOSYS;
            goto routing_fail;
        }

        /*
         * select_devices() call below switches all the usecases on the same
         * backend to the new device. Refer to check_and_route_playback_usecases() in
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
        if (new_dev != AUDIO_DEVICE_NONE) {
            bool same_dev = out->devices == new_dev;
            out->devices = new_dev;

            if (output_drives_call(adev, out)) {
                if (!voice_is_call_state_active(adev)) {
                    if (adev->mode == AUDIO_MODE_IN_CALL) {
                        adev->current_call_output = out;
                        ret = voice_start_call(adev);
                    }
                } else {
                    adev->current_call_output = out;
                    voice_update_devices_for_all_voice_usecases(adev);
                }
            }

            if (!out->standby) {
                int volume_delay_us = 0;
                if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
                    pthread_mutex_lock(&out->compr_mute_lock);
                    if (out->a2dp_compress_mute &&
                        (!(new_dev & AUDIO_DEVICE_OUT_ALL_A2DP) ||
                         audio_extn_a2dp_is_ready())) {
                        out->a2dp_compress_mute = false;
                    }
                    float volume_l = out->volume_l;
                    float volume_r = out->volume_r;
                    if (out->a2dp_compress_mute || forced_speaker_fallback) {
                        volume_l = 0.0;
                        volume_r = 0.0;
                    }
                    if (volume_l != out->applied_volume_l || volume_r != out->applied_volume_r)
                        volume_delay_us = COMPRESS_OFFLOAD_PLAYBACK_LATENCY * 2000;

                    out_set_compr_volume(&out->stream, volume_l, volume_r);
                    pthread_mutex_unlock(&out->compr_mute_lock);
                } else if (out->usecase == USECASE_AUDIO_PLAYBACK_LOW_LATENCY ||
                        out->usecase == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER ||
                        out->usecase == USECASE_AUDIO_PLAYBACK_ULL) {
                    float volume_l = out->volume_l;
                    float volume_r = out->volume_r;
                    if (forced_speaker_fallback) {
                        volume_l = 0.0;
                        volume_r = 0.0;
                    }
                    if (volume_l != out->applied_volume_l || volume_r != out->applied_volume_r)
                        volume_delay_us = (int)platform_render_latency(out) * 2;

                    out_set_pcm_volume(&out->stream, volume_l, volume_r);
                }
                if (volume_delay_us > 0)
                    usleep(volume_delay_us * 2);

                if (!same_dev) {
                    ALOGV("update routing change");
                    // inform adm before actual routing to prevent glitches.
                    if (adev->adm_on_routing_change) {
                        adev->adm_on_routing_change(adev->adm_data,
                                                    out->handle);
                    }
                }
                if (!bypass_a2dp) {
                    select_devices(adev, out->usecase);
                } else {
                    if (new_dev & AUDIO_DEVICE_OUT_SPEAKER_SAFE)
                        out->devices = AUDIO_DEVICE_OUT_SPEAKER_SAFE;
                    else
                        out->devices = AUDIO_DEVICE_OUT_SPEAKER;
                    select_devices(adev, out->usecase);
                    out->devices = new_dev;
                }
                audio_extn_tfa_98xx_update();

                // on device switch force swap, lower functions will make sure
                // to check if swap is allowed or not.

                if (!same_dev)
                    platform_set_swap_channels(adev, true);


            }

        }

        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&out->lock);

        /*handles device and call state changes*/
        audio_extn_extspk_update(adev->extspk);
    }
    routing_fail:

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        parse_compress_metadata(out, parms);
    }

    str_parms_destroy(parms);
    ALOGV("%s: exit: code(%d)", __func__, status);
    return status;
}

static bool stream_get_parameter_channels(struct str_parms *query,
                                          struct str_parms *reply,
                                          audio_channel_mask_t *supported_channel_masks) {
    int ret = -1;
    char value[ARRAY_SIZE(channels_name_to_enum_table) * 32 /* max channel name size */];
    bool first = true;
    size_t i, j;

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
        ret = 0;
        value[0] = '\0';
        i = 0;
        while (supported_channel_masks[i] != 0) {
            for (j = 0; j < ARRAY_SIZE(channels_name_to_enum_table); j++) {
                if (channels_name_to_enum_table[j].value == supported_channel_masks[i]) {
                    if (!first) {
                        strcat(value, "|");
                    }
                    strcat(value, channels_name_to_enum_table[j].name);
                    first = false;
                    break;
                }
            }
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value);
    }
    return ret >= 0;
}

static bool stream_get_parameter_formats(struct str_parms *query,
                                         struct str_parms *reply,
                                         audio_format_t *supported_formats) {
    int ret = -1;
    char value[256];
    int i;

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        ret = 0;
        value[0] = '\0';
        switch (supported_formats[0]) {
            case AUDIO_FORMAT_PCM_16_BIT:
                strcat(value, "AUDIO_FORMAT_PCM_16_BIT");
                break;
            case AUDIO_FORMAT_PCM_24_BIT_PACKED:
                strcat(value, "AUDIO_FORMAT_PCM_24_BIT_PACKED");
                break;
            case AUDIO_FORMAT_PCM_32_BIT:
                strcat(value, "AUDIO_FORMAT_PCM_32_BIT");
                break;
            default:
                ALOGE("%s: unsupported format %#x", __func__,
                      supported_formats[0]);
                break;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value);
    }
    return ret >= 0;
}

static bool stream_get_parameter_rates(struct str_parms *query,
                                       struct str_parms *reply,
                                       uint32_t *supported_sample_rates) {

    int i;
    char value[256];
    int ret = -1;
    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
        ret = 0;
        value[0] = '\0';
        i=0;
        int cursor = 0;
        while (supported_sample_rates[i]) {
            int avail = sizeof(value) - cursor;
            ret = snprintf(value + cursor, avail, "%s%d",
                           cursor > 0 ? "|" : "",
                           supported_sample_rates[i]);
            if (ret < 0 || ret >= avail) {
                // if cursor is at the last element of the array
                //    overwrite with \0 is duplicate work as
                //    snprintf already put a \0 in place.
                // else
                //    we had space to write the '|' at value[cursor]
                //    (which will be overwritten) or no space to fill
                //    the first element (=> cursor == 0)
                value[cursor] = '\0';
                break;
            }
            cursor += ret;
            ++i;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES,
                          value);
    }
    return ret >= 0;
}

static char* out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    char *str;
    struct str_parms *reply = str_parms_create();
    bool replied = false;
    ALOGV("%s: enter: keys - %s", __func__, keys);

    replied |= stream_get_parameter_channels(query, reply,
                                             &out->supported_channel_masks[0]);
    replied |= stream_get_parameter_formats(query, reply,
                                            &out->supported_formats[0]);
    replied |= stream_get_parameter_rates(query, reply,
                                          &out->supported_sample_rates[0]);
    if (replied) {
        str = str_parms_to_str(reply);
    } else {
        str = strdup("");
    }
    str_parms_destroy(query);
    str_parms_destroy(reply);
    ALOGV("%s: exit: returns - %s", __func__, str);
    return str;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    uint32_t hw_delay, period_ms;
    struct stream_out *out = (struct stream_out *)stream;
    uint32_t latency;

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD)
        return COMPRESS_OFFLOAD_PLAYBACK_LATENCY;
    else if ((out->realtime) ||
            (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP)) {
        // since the buffer won't be filled up faster than realtime,
        // return a smaller number
        period_ms = (out->af_period_multiplier * out->config.period_size *
                     1000) / (out->config.rate);
        hw_delay = platform_render_latency(out)/1000;
        return period_ms + hw_delay;
    }

    latency = (out->config.period_count * out->config.period_size * 1000) /
              (out->config.rate);

    if (AUDIO_DEVICE_OUT_ALL_A2DP & out->devices)
        latency += audio_extn_a2dp_get_encoder_latency();

    return latency;
}

static int out_set_compr_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    int volume[2];
    char mixer_ctl_name[128];
    struct audio_device *adev = out->dev;
    struct mixer_ctl *ctl;
    int pcm_device_id = platform_get_pcm_device_id(out->usecase,
                                               PCM_PLAYBACK);

    if (left == out->applied_volume_l && right == out->applied_volume_r)
       return 0;

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Compress Playback %d Volume", pcm_device_id);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }
    ALOGV("%s: ctl for mixer cmd - %s, left %f, right %f",
           __func__, mixer_ctl_name, left, right);
    volume[0] = (int)(left * COMPRESS_PLAYBACK_VOLUME_MAX);
    volume[1] = (int)(right * COMPRESS_PLAYBACK_VOLUME_MAX);
    mixer_ctl_set_array(ctl, volume, sizeof(volume) / sizeof(volume[0]));

    out->applied_volume_l = left;
    out->applied_volume_r = right;
    return 0;
}

static int out_set_pcm_volume(struct audio_stream_out *stream, float left,
                              float right)
{
    struct stream_out *out = (struct stream_out *)stream;

    if (left == out->applied_volume_l && right == out->applied_volume_r)
       return 0;

    /* Volume control for pcm playback */
    if (left != right) {
        return -EINVAL;
    } else {
        char mixer_ctl_name[128];
        struct audio_device *adev = out->dev;
        struct mixer_ctl *ctl;
        int pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "Playback %d Volume", pcm_device_id);
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s : Could not get ctl for mixer cmd - %s", __func__, mixer_ctl_name);
            return -EINVAL;
        }

        int volume = (int) (left * PCM_PLAYBACK_VOLUME_MAX);
        int ret = mixer_ctl_set_value(ctl, 0, volume);
        if (ret < 0) {
            ALOGE("%s: Could not set ctl, error:%d ", __func__, ret);
            return -EINVAL;
        }

        ALOGV("%s : Pcm set volume value %d left %f", __func__, volume, left);

        out->applied_volume_l = left;
        out->applied_volume_r = right;
        return 0;
    }
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret = 0;

    if (out->usecase == USECASE_AUDIO_PLAYBACK_HIFI) {
        /* only take left channel into account: the API is for stereo anyway */
        out->muted = (left == 0.0f);
        return 0;
    } else if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        pthread_mutex_lock(&out->compr_mute_lock);
        ALOGV("%s: compress mute %d", __func__, out->a2dp_compress_mute);
        if (!out->a2dp_compress_mute)
            ret = out_set_compr_volume(stream, left, right);
        out->volume_l = left;
        out->volume_r = right;
        pthread_mutex_unlock(&out->compr_mute_lock);
        return ret;
    } else if (out->usecase == USECASE_AUDIO_PLAYBACK_VOIP) {
        out->app_type_cfg.gain[0] = (int)(left * VOIP_PLAYBACK_VOLUME_MAX);
        out->app_type_cfg.gain[1] = (int)(right * VOIP_PLAYBACK_VOLUME_MAX);
        if (!out->standby) {
            // if in standby, cached volume will be sent after stream is opened
            audio_extn_utils_send_app_type_gain(out->dev,
                                                out->app_type_cfg.app_type,
                                                &out->app_type_cfg.gain[0]);
        }
        return 0;
    }

    return -ENOSYS;
}

// note: this call is safe only if the stream_cb is
// removed first in close_output_stream (as is done now).
static void out_snd_mon_cb(void * stream, struct str_parms * parms)
{
    if (!stream || !parms)
        return;

    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;

    card_status_t status;
    int card;
    if (parse_snd_card_status(parms, &card, &status) < 0)
        return;

    pthread_mutex_lock(&adev->lock);
    bool valid_cb = (card == adev->snd_card);
    pthread_mutex_unlock(&adev->lock);

    if (!valid_cb)
        return;

    lock_output_stream(out);
    if (out->card_status != status)
        out->card_status = status;
    pthread_mutex_unlock(&out->lock);

    ALOGW("out_snd_mon_cb for card %d usecase %s, status %s", card,
          use_case_table[out->usecase],
          status == CARD_STATUS_OFFLINE ? "offline" : "online");

    if (status == CARD_STATUS_OFFLINE)
        out_on_error(stream);

    return;
}

#ifdef NO_AUDIO_OUT
static ssize_t out_write_for_no_output(struct audio_stream_out *stream,
                                       const void *buffer __unused, size_t bytes)
{
    struct stream_out *out = (struct stream_out *)stream;

    /* No Output device supported other than BT for playback.
     * Sleep for the amount of buffer duration
     */
    lock_output_stream(out);
    usleep(bytes * 1000000 / audio_stream_out_frame_size(
            (const struct audio_stream_out *)&out->stream) /
            out_get_sample_rate(&out->stream.common));
    pthread_mutex_unlock(&out->lock);
    return bytes;
}
#endif

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer,
                         size_t bytes)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    ssize_t ret = 0;
    int error_code = ERROR_CODE_STANDBY;

    lock_output_stream(out);
    // this is always nonzero
    const size_t frame_size = audio_stream_out_frame_size(stream);
    const size_t frames = bytes / frame_size;

    if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
        error_code = ERROR_CODE_WRITE;
        goto exit;
    }

    if ((out->devices & AUDIO_DEVICE_OUT_ALL_A2DP) &&
        (audio_extn_a2dp_is_suspended())) {
        if (!(out->devices & (AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_SPEAKER_SAFE))) {
            if (!(out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
                ret = -EIO;
                goto exit;
            }
        }
    }

    const bool was_in_standby = out->standby;
    if (out->standby) {
        out->standby = false;
        const int64_t startNs = systemTime(SYSTEM_TIME_MONOTONIC);

        pthread_mutex_lock(&adev->lock);
        ret = start_output_stream(out);

        /* ToDo: If use case is compress offload should return 0 */
        if (ret != 0) {
            out->standby = true;
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }

        // after standby always force set last known cal step
        // dont change level anywhere except at the audio_hw_send_gain_dep_calibration
        ALOGD("%s: retry previous failed cal level set", __func__);
        send_gain_dep_calibration_l();
        pthread_mutex_unlock(&adev->lock);

        // log startup time in ms.
        simple_stats_log(
                &out->start_latency_ms, (systemTime(SYSTEM_TIME_MONOTONIC) - startNs) * 1e-6);
        out->last_fifo_valid = false; // we're coming out of standby, last_fifo isn't valid.
    }

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        ALOGVV("%s: writing buffer (%zu bytes) to compress device", __func__, bytes);
        if (out->send_new_metadata) {
            ALOGVV("send new gapless metadata");
            compress_set_gapless_metadata(out->compr, &out->gapless_mdata);
            out->send_new_metadata = 0;
        }
        unsigned int avail;
        struct timespec tstamp;
        ret = compress_get_hpointer(out->compr, &avail, &tstamp);
        /* Do not limit write size if the available frames count is unknown */
        if (ret != 0) {
            avail = bytes;
        }
        if (avail == 0) {
            ret = 0;
        } else {
            // check for compressed format underrun, essentially an empty buffer check
            // for a lack of better measurement.
            if (!was_in_standby && avail == out->kernel_buffer_size) {
                ALOGW("%s: compressed buffer empty (underrun)", __func__);
                simple_stats_log(&out->fifo_underruns, 1.); // Note: log one frame for compressed.
            }

            if (avail > bytes) {
                avail = bytes;
            }
            ret = compress_write(out->compr, buffer, avail);
            ALOGVV("%s: writing buffer (%d bytes) to compress device returned %zd",
                   __func__, avail, ret);
        }

        if (ret >= 0 && ret < (ssize_t)bytes) {
            send_offload_cmd_l(out, OFFLOAD_CMD_WAIT_FOR_BUFFER);
        }
        if (ret > 0 && !out->playback_started) {
            compress_start(out->compr);
            out->playback_started = 1;
            out->offload_state = OFFLOAD_STATE_PLAYING;
        }
        if (ret < 0) {
            error_log_log(out->error_log, ERROR_CODE_WRITE, audio_utils_get_real_time_ns());
        } else {
            out->written += ret; // accumulate bytes written for offload.
        }
        pthread_mutex_unlock(&out->lock);
        // TODO: consider logging offload pcm
        return ret;
    } else {
        error_code = ERROR_CODE_WRITE;
        if (out->pcm) {
            size_t bytes_to_write = bytes;

            if (out->muted)
                memset((void *)buffer, 0, bytes);
            // FIXME: this can be removed once audio flinger mixer supports mono output
            if (out->usecase == USECASE_AUDIO_PLAYBACK_VOIP ||
                out->usecase == USECASE_INCALL_MUSIC_UPLINK ||
                out->usecase == USECASE_INCALL_MUSIC_UPLINK2) {
                size_t channel_count = audio_channel_count_from_out_mask(out->channel_mask);
                int16_t *src = (int16_t *)buffer;
                int16_t *dst = (int16_t *)buffer;

                LOG_ALWAYS_FATAL_IF(out->config.channels != 1 || channel_count != 2 ||
                                    out->format != AUDIO_FORMAT_PCM_16_BIT,
                                    "out_write called for VOIP use case with wrong properties");

                for (size_t i = 0; i < frames ; i++, dst++, src += 2) {
                    *dst = (int16_t)(((int32_t)src[0] + (int32_t)src[1]) >> 1);
                }
                bytes_to_write /= 2;
            }

            // Note: since out_get_presentation_position() is called alternating with out_write()
            // by AudioFlinger, we can check underruns using the prior timestamp read.
            // (Alternately we could check if the buffer is empty using pcm_get_htimestamp().
            if (out->last_fifo_valid) {
                // compute drain to see if there is an underrun.
                const int64_t current_ns = systemTime(SYSTEM_TIME_MONOTONIC); // sys call
                const int64_t frames_by_time =
                        (current_ns - out->last_fifo_time_ns) * out->config.rate / NANOS_PER_SECOND;
                const int64_t underrun = frames_by_time - out->last_fifo_frames_remaining;

                if (underrun > 0) {
                    simple_stats_log(&out->fifo_underruns, underrun);

                    ALOGW("%s: underrun(%lld) "
                            "frames_by_time(%lld) > out->last_fifo_frames_remaining(%lld)",
                            __func__,
                            (long long)out->fifo_underruns.n,
                            (long long)frames_by_time,
                            (long long)out->last_fifo_frames_remaining);
                }
                out->last_fifo_valid = false;  // we're writing below, mark fifo info as stale.
            }

            long ns = (frames * (int64_t) NANOS_PER_SECOND) / out->config.rate;
            request_out_focus(out, ns);

            bool use_mmap = is_mmap_usecase(out->usecase) || out->realtime;
            if (use_mmap) {
                ret = pcm_mmap_write(out->pcm, (void *)buffer, bytes_to_write);
            } else {
                if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
                    size_t channel_count = audio_channel_count_from_out_mask(out->channel_mask);
                    size_t bytes_per_sample = audio_bytes_per_sample(out->format);
                    size_t frame_size = channel_count * bytes_per_sample;
                    size_t frame_count = bytes_to_write / frame_size;

                    bool force_haptic_path =
                         property_get_bool("vendor.audio.test_haptic", false);

                    // extract Haptics data from Audio buffer
                    bool   alloc_haptic_buffer = false;
                    int    haptic_channel_count = adev->haptics_config.channels;
                    size_t haptic_frame_size = bytes_per_sample * haptic_channel_count;
                    size_t audio_frame_size = frame_size - haptic_frame_size;
                    size_t total_haptic_buffer_size = frame_count * haptic_frame_size;

                    if (adev->haptic_buffer == NULL) {
                        alloc_haptic_buffer = true;
                    } else if (adev->haptic_buffer_size < total_haptic_buffer_size) {
                        free(adev->haptic_buffer);
                        adev->haptic_buffer_size = 0;
                        alloc_haptic_buffer = true;
                    }

                    if (alloc_haptic_buffer) {
                        adev->haptic_buffer = (uint8_t *)calloc(1, total_haptic_buffer_size);
                        adev->haptic_buffer_size = total_haptic_buffer_size;
                    }

                    size_t src_index = 0, aud_index = 0, hap_index = 0;
                    uint8_t *audio_buffer = (uint8_t *)buffer;
                    uint8_t *haptic_buffer  = adev->haptic_buffer;

                    // This is required for testing only. This works for stereo data only.
                    // One channel is fed to audio stream and other to haptic stream for testing.
                    if (force_haptic_path) {
                       audio_frame_size = haptic_frame_size = bytes_per_sample;
                    }

                    for (size_t i = 0; i < frame_count; i++) {
                        for (size_t j = 0; j < audio_frame_size; j++)
                            audio_buffer[aud_index++] = audio_buffer[src_index++];

                        for (size_t j = 0; j < haptic_frame_size; j++)
                            haptic_buffer[hap_index++] = audio_buffer[src_index++];
                        }

                        // This is required for testing only.
                        // Discard haptic channel data.
                        if (force_haptic_path) {
                            src_index += haptic_frame_size;
                    }

                    // write to audio pipeline
                    ret = pcm_write(out->pcm,
                                    (void *)audio_buffer,
                                    frame_count * audio_frame_size);

                    // write to haptics pipeline
                    if (adev->haptic_pcm)
                        ret = pcm_write(adev->haptic_pcm,
                                        (void *)adev->haptic_buffer,
                                        frame_count * haptic_frame_size);

                } else {
                    ret = pcm_write(out->pcm, (void *)buffer, bytes_to_write);
                }
            }
            release_out_focus(out, ns);
        } else {
            LOG_ALWAYS_FATAL("out->pcm is NULL after starting output stream");
        }
    }

exit:
    // For PCM we always consume the buffer and return #bytes regardless of ret.
    if (out->usecase != USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        out->written += frames;
    }
    long long sleeptime_us = 0;

    if (ret != 0) {
        error_log_log(out->error_log, error_code, audio_utils_get_real_time_ns());
        if (out->usecase != USECASE_AUDIO_PLAYBACK_OFFLOAD) {
            ALOGE_IF(out->pcm != NULL,
                    "%s: error %zd - %s", __func__, ret, pcm_get_error(out->pcm));
            sleeptime_us = frames * 1000000LL / out_get_sample_rate(&out->stream.common);
            // usleep not guaranteed for values over 1 second but we don't limit here.
        }
    }

    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        out_on_error(&out->stream.common);
        if (sleeptime_us != 0)
            usleep(sleeptime_us);
    }
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    struct stream_out *out = (struct stream_out *)stream;
    *dsp_frames = 0;
    if ((out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) && (dsp_frames != NULL)) {
        lock_output_stream(out);
        if (out->compr != NULL) {
            unsigned long frames = 0;
            // TODO: check return value
            compress_get_tstamp(out->compr, &frames, &out->sample_rate);
            *dsp_frames = (uint32_t)frames;
            ALOGVV("%s rendered frames %d sample_rate %d",
                   __func__, *dsp_frames, out->sample_rate);
        }
        pthread_mutex_unlock(&out->lock);
        return 0;
    } else
        return -ENODATA;
}

static int out_add_audio_effect(const struct audio_stream *stream __unused,
                                effect_handle_t effect __unused)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream __unused,
                                   effect_handle_t effect __unused)
{
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream __unused,
                                        int64_t *timestamp __unused)
{
    return -ENOSYS;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                   uint64_t *frames, struct timespec *timestamp)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret = -ENODATA;
    unsigned long dsp_frames;

    lock_output_stream(out);

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        if (out->compr != NULL) {
            // TODO: check return value
            compress_get_tstamp(out->compr, &dsp_frames,
                    &out->sample_rate);
            // Adjustment accounts for A2DP encoder latency with offload usecases
            // Note: Encoder latency is returned in ms.
            if (AUDIO_DEVICE_OUT_ALL_A2DP & out->devices) {
                unsigned long offset =
                            (audio_extn_a2dp_get_encoder_latency() * out->sample_rate / 1000);
                dsp_frames = (dsp_frames > offset) ? (dsp_frames - offset) : 0;
            }
            ALOGVV("%s rendered frames %ld sample_rate %d",
                   __func__, dsp_frames, out->sample_rate);
            *frames = dsp_frames;
            ret = 0;
            /* this is the best we can do */
            clock_gettime(CLOCK_MONOTONIC, timestamp);
        }
    } else {
        if (out->pcm) {
            unsigned int avail;
            if (pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0) {

                // pcm_get_htimestamp() computes the available frames by comparing
                // the alsa driver hw_ptr and the appl_ptr levels.
                // In underrun, the hw_ptr may keep running and report an excessively
                // large number available number.
                if (avail > out->kernel_buffer_size) {
                    ALOGW("%s: avail:%u > kernel_buffer_size:%zu clamping!",
                            __func__, avail, out->kernel_buffer_size);
                    avail = out->kernel_buffer_size;
                    out->last_fifo_frames_remaining = 0;
                } else {
                    out->last_fifo_frames_remaining = out->kernel_buffer_size - avail;
                }
                out->last_fifo_valid = true;
                out->last_fifo_time_ns = audio_utils_ns_from_timespec(timestamp);

                int64_t signed_frames = out->written - out->last_fifo_frames_remaining;

                ALOGVV("%s: frames:%lld  avail:%u  kernel_buffer_size:%zu",
                        __func__, (long long)signed_frames, avail, out->kernel_buffer_size);

                // This adjustment accounts for buffering after app processor.
                // It is based on estimated DSP latency per use case, rather than exact.
                signed_frames -=
                    (platform_render_latency(out) * out->sample_rate / 1000000LL);

                // Adjustment accounts for A2DP encoder latency with non-offload usecases
                // Note: Encoder latency is returned in ms, while platform_render_latency in us.
                if (AUDIO_DEVICE_OUT_ALL_A2DP & out->devices) {
                    signed_frames -=
                            (audio_extn_a2dp_get_encoder_latency() * out->sample_rate / 1000);
                }

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
    lock_output_stream(out);
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
        status = -ENODATA;
        lock_output_stream(out);
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
        status = -ENODATA;
        lock_output_stream(out);
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
        lock_output_stream(out);
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
        lock_output_stream(out);
        stop_compressed_output_l(out);
        pthread_mutex_unlock(&out->lock);
        return 0;
    }
    return -ENOSYS;
}

static int out_stop(const struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    int ret = -ENOSYS;

    ALOGV("%s", __func__);
    pthread_mutex_lock(&adev->lock);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP && !out->standby &&
            out->playback_started && out->pcm != NULL) {
        pcm_stop(out->pcm);
        ret = stop_output_stream(out);
        out->playback_started = false;
    }
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

static int out_start(const struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    int ret = -ENOSYS;

    ALOGV("%s", __func__);
    pthread_mutex_lock(&adev->lock);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP && !out->standby &&
            !out->playback_started && out->pcm != NULL) {
        ret = start_output_stream(out);
        if (ret == 0) {
            out->playback_started = true;
        }
    }
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

/*
 * Modify config->period_count based on min_size_frames
 */
static void adjust_mmap_period_count(struct pcm_config *config, int32_t min_size_frames)
{
    int periodCountRequested = (min_size_frames + config->period_size - 1)
                               / config->period_size;
    int periodCount = MMAP_PERIOD_COUNT_MIN;

    ALOGV("%s original config.period_size = %d config.period_count = %d",
          __func__, config->period_size, config->period_count);

    while (periodCount < periodCountRequested && (periodCount * 2) < MMAP_PERIOD_COUNT_MAX) {
        periodCount *= 2;
    }
    config->period_count = periodCount;

    ALOGV("%s requested config.period_count = %d", __func__, config->period_count);
}

// Read offset for the positional timestamp from a persistent vendor property.
// This is to workaround apparent inaccuracies in the timing information that
// is used by the AAudio timing model. The inaccuracies can cause glitches.
static int64_t get_mmap_out_time_offset() {
    const int32_t kDefaultOffsetMicros = 0;
    int32_t mmap_time_offset_micros = property_get_int32(
        "persist.audio.out_mmap_delay_micros", kDefaultOffsetMicros);
    ALOGI("mmap_time_offset_micros = %d for output", mmap_time_offset_micros);
    return mmap_time_offset_micros * (int64_t)1000;
}

static int out_create_mmap_buffer(const struct audio_stream_out *stream,
                                  int32_t min_size_frames,
                                  struct audio_mmap_buffer_info *info)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    int ret = 0;
    unsigned int offset1;
    unsigned int frames1;
    const char *step = "";
    uint32_t mmap_size;
    uint32_t buffer_size;

    ALOGV("%s", __func__);
    lock_output_stream(out);
    pthread_mutex_lock(&adev->lock);

    if (info == NULL || min_size_frames <= 0 || min_size_frames > MMAP_MIN_SIZE_FRAMES_MAX) {
        ALOGE("%s: info = %p, min_size_frames = %d", __func__, info, min_size_frames);
        ret = -EINVAL;
        goto exit;
    }
    if (out->usecase != USECASE_AUDIO_PLAYBACK_MMAP || !out->standby) {
        ALOGE("%s: usecase = %d, standby = %d", __func__, out->usecase, out->standby);
        ret = -ENOSYS;
        goto exit;
    }
    out->pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);
    if (out->pcm_device_id < 0) {
        ALOGE("%s: Invalid PCM device id(%d) for the usecase(%d)",
              __func__, out->pcm_device_id, out->usecase);
        ret = -EINVAL;
        goto exit;
    }

    adjust_mmap_period_count(&out->config, min_size_frames);

    ALOGV("%s: Opening PCM device card_id(%d) device_id(%d), channels %d",
          __func__, adev->snd_card, out->pcm_device_id, out->config.channels);
    out->pcm = pcm_open(adev->snd_card, out->pcm_device_id,
                        (PCM_OUT | PCM_MMAP | PCM_NOIRQ | PCM_MONOTONIC), &out->config);
    if (out->pcm == NULL || !pcm_is_ready(out->pcm)) {
        step = "open";
        ret = -ENODEV;
        goto exit;
    }
    ret = pcm_mmap_begin(out->pcm, &info->shared_memory_address, &offset1, &frames1);
    if (ret < 0)  {
        step = "begin";
        goto exit;
    }
    info->buffer_size_frames = pcm_get_buffer_size(out->pcm);
    buffer_size = pcm_frames_to_bytes(out->pcm, info->buffer_size_frames);
    info->burst_size_frames = out->config.period_size;
    ret = platform_get_mmap_data_fd(adev->platform,
                                    out->pcm_device_id, 0 /*playback*/,
                                    &info->shared_memory_fd,
                                    &mmap_size);
    if (ret < 0) {
        // Fall back to non exclusive mode
        info->shared_memory_fd = pcm_get_poll_fd(out->pcm);
    } else {
        out->mmap_shared_memory_fd = info->shared_memory_fd; // for closing later
        ALOGV("%s: opened mmap_shared_memory_fd = %d", __func__, out->mmap_shared_memory_fd);

        if (mmap_size < buffer_size) {
            step = "mmap";
            goto exit;
        }
        // FIXME: indicate exclusive mode support by returning a negative buffer size
        info->buffer_size_frames *= -1;
    }
    memset(info->shared_memory_address, 0, buffer_size);

    ret = pcm_mmap_commit(out->pcm, 0, MMAP_PERIOD_SIZE);
    if (ret < 0) {
        step = "commit";
        goto exit;
    }

    out->mmap_time_offset_nanos = get_mmap_out_time_offset();

    out->standby = false;
    ret = 0;

    ALOGV("%s: got mmap buffer address %p info->buffer_size_frames %d",
          __func__, info->shared_memory_address, info->buffer_size_frames);

exit:
    if (ret != 0) {
        if (out->pcm == NULL) {
            ALOGE("%s: %s - %d", __func__, step, ret);
        } else {
            ALOGE("%s: %s %s", __func__, step, pcm_get_error(out->pcm));
            pcm_close(out->pcm);
            out->pcm = NULL;
        }
    }
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
    return ret;
}

static int out_get_mmap_position(const struct audio_stream_out *stream,
                                  struct audio_mmap_position *position)
{
    int ret = 0;
    struct stream_out *out = (struct stream_out *)stream;
    ALOGVV("%s", __func__);
    if (position == NULL) {
        return -EINVAL;
    }
    lock_output_stream(out);
    if (out->usecase != USECASE_AUDIO_PLAYBACK_MMAP ||
        out->pcm == NULL) {
        ret = -ENOSYS;
        goto exit;
    }

    struct timespec ts = { 0, 0 };
    ret = pcm_mmap_get_hw_ptr(out->pcm, (unsigned int *)&position->position_frames, &ts);
    if (ret < 0) {
        ALOGE("%s: %s", __func__, pcm_get_error(out->pcm));
        goto exit;
    }
    position->time_nanoseconds = audio_utils_ns_from_timespec(&ts)
            + out->mmap_time_offset_nanos;

exit:
    pthread_mutex_unlock(&out->lock);
    return ret;
}


/** audio_stream_in implementation **/
static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

    return in->config.rate;
}

static int in_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
{
    return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    return in->config.period_size * in->af_period_multiplier *
        audio_stream_in_frame_size((const struct audio_stream_in *)stream);
}

static uint32_t in_get_channels(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

    return in->channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    return in->format;
}

static int in_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return -ENOSYS;
}

static int in_standby(struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    int status = 0;
    bool do_stop = true;

    ALOGV("%s: enter", __func__);

    lock_input_stream(in);

    if (!in->standby && (in->flags & AUDIO_INPUT_FLAG_HW_HOTWORD)) {
        ALOGV("%s: sound trigger pcm stop lab", __func__);
        audio_extn_sound_trigger_stop_lab(in);
        in->standby = true;
    }

    if (!in->standby) {
        if (adev->adm_deregister_stream)
            adev->adm_deregister_stream(adev->adm_data, in->capture_handle);

        pthread_mutex_lock(&adev->lock);
        in->standby = true;
        if (in->usecase == USECASE_AUDIO_RECORD_MMAP) {
            do_stop = in->capture_started;
            in->capture_started = false;

            if (in->mmap_shared_memory_fd >= 0) {
                ALOGV("%s: closing mmap_shared_memory_fd = %d",
                      __func__, in->mmap_shared_memory_fd);
                close(in->mmap_shared_memory_fd);
                in->mmap_shared_memory_fd = -1;
            }

        }
        if (in->pcm) {
            pcm_close(in->pcm);
            in->pcm = NULL;
        }

        if (in->source == AUDIO_SOURCE_VOICE_COMMUNICATION)
            adev->enable_voicerx = false;

        if (do_stop) {
            status = stop_input_stream(in);
        }

        pthread_mutex_unlock(&adev->lock);
    }
    pthread_mutex_unlock(&in->lock);
    ALOGV("%s: exit:  status(%d)", __func__, status);
    return status;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    struct stream_in *in = (struct stream_in *)stream;

    // We try to get the lock for consistency,
    // but it isn't necessary for these variables.
    // If we're not in standby, we may be blocked on a read.
    const bool locked = (pthread_mutex_trylock(&in->lock) == 0);
    dprintf(fd, "      Standby: %s\n", in->standby ? "yes" : "no");
    dprintf(fd, "      Frames read: %lld\n", (long long)in->frames_read);
    dprintf(fd, "      Frames muted: %lld\n", (long long)in->frames_muted);

    char buffer[256]; // for statistics formatting
    if (in->start_latency_ms.n > 0) {
        simple_stats_to_string(&in->start_latency_ms, buffer, sizeof(buffer));
        dprintf(fd, "      Start latency ms: %s\n", buffer);
    }

    if (locked) {
        pthread_mutex_unlock(&in->lock);
    }

    // dump error info
    (void)error_log_dump(
            in->error_log, fd, "      " /* prefix */, 0 /* lines */, 0 /* limit_ns */);
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;
    int status = 0;

    ALOGV("%s: enter: kvpairs=%s", __func__, kvpairs);
    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value, sizeof(value));

    lock_input_stream(in);

    pthread_mutex_lock(&adev->lock);
    if (ret >= 0) {
        val = atoi(value);
        /* no audio source uses val == 0 */
        if ((in->source != val) && (val != 0)) {
            in->source = val;
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));

    if (ret >= 0) {
        val = atoi(value);
        if (((int)in->device != val) && (val != 0) && audio_is_input_device(val) ) {

            // Workaround: If routing to an non existing usb device, fail gracefully
            // The routing request will otherwise block during 10 second
            int card;
            if (audio_is_usb_in_device(val) &&
                (card = get_alive_usb_card(parms)) >= 0) {

                ALOGW("in_set_parameters() ignoring rerouting to non existing USB card %d", card);
                status = -ENOSYS;
            } else {

                in->device = val;
                /* If recording is in progress, change the tx device to new device */
                if (!in->standby) {
                    ALOGV("update input routing change");
                    // inform adm before actual routing to prevent glitches.
                    if (adev->adm_on_routing_change) {
                        adev->adm_on_routing_change(adev->adm_data,
                                                    in->capture_handle);
                    }
                    select_devices(adev, in->usecase);
                }
            }
        }
    }

    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);

    str_parms_destroy(parms);
    ALOGV("%s: exit: status(%d)", __func__, status);
    return status;
}

static char* in_get_parameters(const struct audio_stream *stream,
                               const char *keys)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    char *str;
    struct str_parms *reply = str_parms_create();
    bool replied = false;

    ALOGV("%s: enter: keys - %s", __func__, keys);
    replied |= stream_get_parameter_channels(query, reply,
                                             &in->supported_channel_masks[0]);
    replied |= stream_get_parameter_formats(query, reply,
                                            &in->supported_formats[0]);
    replied |= stream_get_parameter_rates(query, reply,
                                          &in->supported_sample_rates[0]);
    if (replied) {
        str = str_parms_to_str(reply);
    } else {
        str = strdup("");
    }
    str_parms_destroy(query);
    str_parms_destroy(reply);
    ALOGV("%s: exit: returns - %s", __func__, str);
    return str;
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    struct stream_in *in = (struct stream_in *)stream;
    char mixer_ctl_name[128];
    struct mixer_ctl *ctl;
    int ctl_value;

    ALOGV("%s: gain %f", __func__, gain);

    if (stream == NULL)
        return -EINVAL;

    /* in_set_gain() only used to silence MMAP capture for now */
    if (in->usecase != USECASE_AUDIO_RECORD_MMAP)
        return -ENOSYS;

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "Capture %d Volume", in->pcm_device_id);

    ctl = mixer_get_ctl_by_name(in->dev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGW("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -ENOSYS;
    }

    if (gain < RECORD_GAIN_MIN)
        gain  = RECORD_GAIN_MIN;
    else if (gain > RECORD_GAIN_MAX)
         gain = RECORD_GAIN_MAX;
    ctl_value = (int)(RECORD_VOLUME_CTL_MAX * gain);

    mixer_ctl_set_value(ctl, 0, ctl_value);
    return 0;
}

static void in_snd_mon_cb(void * stream, struct str_parms * parms)
{
    if (!stream || !parms)
        return;

    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;

    card_status_t status;
    int card;
    if (parse_snd_card_status(parms, &card, &status) < 0)
        return;

    pthread_mutex_lock(&adev->lock);
    bool valid_cb = (card == adev->snd_card);
    pthread_mutex_unlock(&adev->lock);

    if (!valid_cb)
        return;

    lock_input_stream(in);
    if (in->card_status != status)
        in->card_status = status;
    pthread_mutex_unlock(&in->lock);

    ALOGW("in_snd_mon_cb for card %d usecase %s, status %s", card,
          use_case_table[in->usecase],
          status == CARD_STATUS_OFFLINE ? "offline" : "online");

    // a better solution would be to report error back to AF and let
    // it put the stream to standby
    if (status == CARD_STATUS_OFFLINE)
        in_standby(&in->stream.common);

    return;
}

static ssize_t in_read(struct audio_stream_in *stream, void *buffer,
                       size_t bytes)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    int i, ret = -1;
    int *int_buf_stream = NULL;
    int error_code = ERROR_CODE_STANDBY; // initial errors are considered coming out of standby.

    lock_input_stream(in);
    const size_t frame_size = audio_stream_in_frame_size(stream);
    const size_t frames = bytes / frame_size;

    if (in->flags & AUDIO_INPUT_FLAG_HW_HOTWORD) {
        ALOGVV(" %s: reading on st session bytes=%zu", __func__, bytes);
        /* Read from sound trigger HAL */
        audio_extn_sound_trigger_read(in, buffer, bytes);
        pthread_mutex_unlock(&in->lock);
        return bytes;
    }

    if (in->usecase == USECASE_AUDIO_RECORD_MMAP) {
        ret = -ENOSYS;
        goto exit;
    }

    if (in->standby) {
        const int64_t startNs = systemTime(SYSTEM_TIME_MONOTONIC);

        pthread_mutex_lock(&adev->lock);
        ret = start_input_stream(in);
        pthread_mutex_unlock(&adev->lock);
        if (ret != 0) {
            goto exit;
        }
        in->standby = 0;

        // log startup time in ms.
        simple_stats_log(
                &in->start_latency_ms, (systemTime(SYSTEM_TIME_MONOTONIC) - startNs) * 1e-6);
    }

    // errors that occur here are read errors.
    error_code = ERROR_CODE_READ;

    //what's the duration requested by the client?
    long ns = pcm_bytes_to_frames(in->pcm, bytes)*1000000000LL/
                                                in->config.rate;
    request_in_focus(in, ns);

    bool use_mmap = is_mmap_usecase(in->usecase) || in->realtime;
    if (in->pcm) {
        if (use_mmap) {
            ret = pcm_mmap_read(in->pcm, buffer, bytes);
        } else {
            ret = pcm_read(in->pcm, buffer, bytes);
        }
        if (ret < 0) {
            ALOGE("Failed to read w/err %s", strerror(errno));
            ret = -errno;
        }
        if (!ret && bytes > 0 && (in->format == AUDIO_FORMAT_PCM_8_24_BIT)) {
            if (bytes % 4 == 0) {
                /* data from DSP comes in 24_8 format, convert it to 8_24 */
                int_buf_stream = buffer;
                for (size_t itt=0; itt < bytes/4 ; itt++) {
                    int_buf_stream[itt] >>= 8;
                }
            } else {
                ALOGE("%s: !!! something wrong !!! ... data not 32 bit aligned ", __func__);
                ret = -EINVAL;
                goto exit;
            }
        }
    }

    release_in_focus(in, ns);

    /*
     * Instead of writing zeroes here, we could trust the hardware
     * to always provide zeroes when muted.
     * No need to acquire adev->lock to read mic_muted here as we don't change its state.
     */
    if (ret == 0 && adev->mic_muted &&
        !voice_is_in_call_rec_stream(in) &&
        in->usecase != USECASE_AUDIO_RECORD_AFE_PROXY) {
        memset(buffer, 0, bytes);
        in->frames_muted += frames;
    }

exit:
    pthread_mutex_unlock(&in->lock);

    if (ret != 0) {
        error_log_log(in->error_log, error_code, audio_utils_get_real_time_ns());
        in_standby(&in->stream.common);
        ALOGV("%s: read failed - sleeping for buffer duration", __func__);
        usleep(frames * 1000000LL / in_get_sample_rate(&in->stream.common));
        memset(buffer, 0, bytes); // clear return data
        in->frames_muted += frames;
    }
    if (bytes > 0) {
        in->frames_read += frames;
    }
    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream __unused)
{
    return 0;
}

static int in_get_capture_position(const struct audio_stream_in *stream,
                                   int64_t *frames, int64_t *time)
{
    if (stream == NULL || frames == NULL || time == NULL) {
        return -EINVAL;
    }
    struct stream_in *in = (struct stream_in *)stream;
    int ret = -ENOSYS;

    lock_input_stream(in);
    // note: ST sessions do not close the alsa pcm driver synchronously
    // on standby. Therefore, we may return an error even though the
    // pcm stream is still opened.
    if (in->standby) {
        ALOGE_IF(in->pcm != NULL && !(in->flags & AUDIO_INPUT_FLAG_HW_HOTWORD),
                 "%s stream in standby but pcm not NULL for non ST session", __func__);
        goto exit;
    }
    if (in->pcm) {
        struct timespec timestamp;
        unsigned int avail;
        if (pcm_get_htimestamp(in->pcm, &avail, &timestamp) == 0) {
            *frames = in->frames_read + avail;
            *time = timestamp.tv_sec * 1000000000LL + timestamp.tv_nsec
                    - platform_capture_latency(in) * 1000LL;
            ret = 0;
        }
    }
exit:
    pthread_mutex_unlock(&in->lock);
    return ret;
}

static int in_update_effect_list(bool add, effect_handle_t effect,
                            struct listnode *head)
{
    struct listnode *node;
    struct in_effect_list *elist = NULL;
    struct in_effect_list *target = NULL;
    int ret = 0;

    if (!head)
        return ret;

    list_for_each(node, head) {
        elist = node_to_item(node, struct in_effect_list, list);
        if (elist->handle == effect) {
            target = elist;
            break;
        }
    }

    if (add) {
        if (target) {
            ALOGD("effect %p already exist", effect);
            return ret;
        }

        target = (struct in_effect_list *)
                     calloc(1, sizeof(struct in_effect_list));

        if (!target) {
            ALOGE("%s:fail to allocate memory", __func__);
            return -ENOMEM;
        }

        target->handle = effect;
        list_add_tail(head, &target->list);
    } else {
        if (target) {
            list_remove(&target->list);
            free(target);
        }
    }

    return ret;
}

static int add_remove_audio_effect(const struct audio_stream *stream,
                                   effect_handle_t effect,
                                   bool enable)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    int status = 0;
    effect_descriptor_t desc;

    status = (*effect)->get_descriptor(effect, &desc);
    ALOGV("%s: status %d in->standby %d enable:%d", __func__, status, in->standby, enable);

    if (status != 0)
        return status;

    lock_input_stream(in);
    pthread_mutex_lock(&in->dev->lock);
    if ((in->source == AUDIO_SOURCE_VOICE_COMMUNICATION ||
            in->source == AUDIO_SOURCE_VOICE_RECOGNITION ||
            adev->mode == AUDIO_MODE_IN_COMMUNICATION) &&
            (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0)) {

        in_update_effect_list(enable, effect, &in->aec_list);
        enable = !list_empty(&in->aec_list);
        if (enable == in->enable_aec)
            goto exit;

        in->enable_aec = enable;
        ALOGD("AEC enable %d", enable);

        if (in->source == AUDIO_SOURCE_VOICE_COMMUNICATION ||
            adev->mode == AUDIO_MODE_IN_COMMUNICATION) {
            adev->enable_voicerx = enable;
            struct audio_usecase *usecase;
            struct listnode *node;
            list_for_each(node, &adev->usecase_list) {
                usecase = node_to_item(node, struct audio_usecase, list);
                if (usecase->type == PCM_PLAYBACK)
                    select_devices(adev, usecase->id);
            }
        }
        if (!in->standby
            && enable_disable_effect(in->dev, in, EFFECT_AEC, enable) == -ENOSYS)
            select_devices(in->dev, in->usecase);
    }
    if (memcmp(&desc.type, FX_IID_NS, sizeof(effect_uuid_t)) == 0) {

        in_update_effect_list(enable, effect, &in->ns_list);
        enable = !list_empty(&in->ns_list);
        if (enable == in->enable_ns)
            goto exit;

        in->enable_ns = enable;
        ALOGD("NS enable %d", enable);
        if (!in->standby) {
            if (in->source != AUDIO_SOURCE_VOICE_COMMUNICATION
                || enable_disable_effect(in->dev, in, EFFECT_NS, enable) == -ENOSYS)
                select_devices(in->dev, in->usecase);
        }
    }
exit:
    pthread_mutex_unlock(&in->dev->lock);
    pthread_mutex_unlock(&in->lock);

    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    ALOGV("%s: effect %p", __func__, effect);
    return add_remove_audio_effect(stream, effect, true);
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    ALOGV("%s: effect %p", __func__, effect);
    return add_remove_audio_effect(stream, effect, false);
}

static int in_stop(const struct audio_stream_in* stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;

    int ret = -ENOSYS;
    ALOGV("%s", __func__);
    pthread_mutex_lock(&adev->lock);
    if (in->usecase == USECASE_AUDIO_RECORD_MMAP && !in->standby &&
            in->capture_started && in->pcm != NULL) {
        pcm_stop(in->pcm);
        ret = stop_input_stream(in);
        in->capture_started = false;
    }
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

static int in_start(const struct audio_stream_in* stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    int ret = -ENOSYS;

    ALOGV("%s in %p", __func__, in);
    pthread_mutex_lock(&adev->lock);
    if (in->usecase == USECASE_AUDIO_RECORD_MMAP && !in->standby &&
            !in->capture_started && in->pcm != NULL) {
        if (!in->capture_started) {
            ret = start_input_stream(in);
            if (ret == 0) {
                in->capture_started = true;
            }
        }
    }
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

// Read offset for the positional timestamp from a persistent vendor property.
// This is to workaround apparent inaccuracies in the timing information that
// is used by the AAudio timing model. The inaccuracies can cause glitches.
static int64_t in_get_mmap_time_offset() {
    const int32_t kDefaultOffsetMicros = 0;
    int32_t mmap_time_offset_micros = property_get_int32(
            "persist.audio.in_mmap_delay_micros", kDefaultOffsetMicros);
    ALOGI("in_get_mmap_time_offset set to %d micros", mmap_time_offset_micros);
    return mmap_time_offset_micros * (int64_t)1000;
}

static int in_create_mmap_buffer(const struct audio_stream_in *stream,
                                  int32_t min_size_frames,
                                  struct audio_mmap_buffer_info *info)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    int ret = 0;
    unsigned int offset1;
    unsigned int frames1;
    const char *step = "";
    uint32_t mmap_size;
    uint32_t buffer_size;

    lock_input_stream(in);
    pthread_mutex_lock(&adev->lock);
    ALOGV("%s in %p", __func__, in);

    if (info == NULL || min_size_frames <= 0 || min_size_frames > MMAP_MIN_SIZE_FRAMES_MAX) {
        ALOGE("%s invalid argument info %p min_size_frames %d", __func__, info, min_size_frames);
        ret = -EINVAL;
        goto exit;
    }
    if (in->usecase != USECASE_AUDIO_RECORD_MMAP || !in->standby) {
        ALOGE("%s: usecase = %d, standby = %d", __func__, in->usecase, in->standby);
        ALOGV("%s in %p", __func__, in);
        ret = -ENOSYS;
        goto exit;
    }
    in->pcm_device_id = platform_get_pcm_device_id(in->usecase, PCM_CAPTURE);
    if (in->pcm_device_id < 0) {
        ALOGE("%s: Invalid PCM device id(%d) for the usecase(%d)",
              __func__, in->pcm_device_id, in->usecase);
        ret = -EINVAL;
        goto exit;
    }

    adjust_mmap_period_count(&in->config, min_size_frames);

    ALOGV("%s: Opening PCM device card_id(%d) device_id(%d), channels %d",
          __func__, adev->snd_card, in->pcm_device_id, in->config.channels);
    in->pcm = pcm_open(adev->snd_card, in->pcm_device_id,
                        (PCM_IN | PCM_MMAP | PCM_NOIRQ | PCM_MONOTONIC), &in->config);
    if (in->pcm == NULL || !pcm_is_ready(in->pcm)) {
        step = "open";
        ret = -ENODEV;
        goto exit;
    }

    ret = pcm_mmap_begin(in->pcm, &info->shared_memory_address, &offset1, &frames1);
    if (ret < 0)  {
        step = "begin";
        goto exit;
    }
    info->buffer_size_frames = pcm_get_buffer_size(in->pcm);
    buffer_size = pcm_frames_to_bytes(in->pcm, info->buffer_size_frames);
    info->burst_size_frames = in->config.period_size;
    ret = platform_get_mmap_data_fd(adev->platform,
                                    in->pcm_device_id, 1 /*capture*/,
                                    &info->shared_memory_fd,
                                    &mmap_size);
    if (ret < 0) {
        // Fall back to non exclusive mode
        info->shared_memory_fd = pcm_get_poll_fd(in->pcm);
    } else {
        in->mmap_shared_memory_fd = info->shared_memory_fd; // for closing later
        ALOGV("%s: opened mmap_shared_memory_fd = %d", __func__, in->mmap_shared_memory_fd);

        if (mmap_size < buffer_size) {
            step = "mmap";
            goto exit;
        }
        // FIXME: indicate exclusive mode support by returning a negative buffer size
        info->buffer_size_frames *= -1;
    }

    memset(info->shared_memory_address, 0, buffer_size);

    ret = pcm_mmap_commit(in->pcm, 0, MMAP_PERIOD_SIZE);
    if (ret < 0) {
        step = "commit";
        goto exit;
    }

    in->mmap_time_offset_nanos = in_get_mmap_time_offset();

    in->standby = false;
    ret = 0;

    ALOGV("%s: got mmap buffer address %p info->buffer_size_frames %d",
          __func__, info->shared_memory_address, info->buffer_size_frames);

exit:
    if (ret != 0) {
        if (in->pcm == NULL) {
            ALOGE("%s: %s - %d", __func__, step, ret);
        } else {
            ALOGE("%s: %s %s", __func__, step, pcm_get_error(in->pcm));
            pcm_close(in->pcm);
            in->pcm = NULL;
        }
    }
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);
    return ret;
}

static int in_get_mmap_position(const struct audio_stream_in *stream,
                                  struct audio_mmap_position *position)
{
    int ret = 0;
    struct stream_in *in = (struct stream_in *)stream;
    ALOGVV("%s", __func__);
    if (position == NULL) {
        return -EINVAL;
    }
    lock_input_stream(in);
    if (in->usecase != USECASE_AUDIO_RECORD_MMAP ||
        in->pcm == NULL) {
        ret = -ENOSYS;
        goto exit;
    }
    struct timespec ts = { 0, 0 };
    ret = pcm_mmap_get_hw_ptr(in->pcm, (unsigned int *)&position->position_frames, &ts);
    if (ret < 0) {
        ALOGE("%s: %s", __func__, pcm_get_error(in->pcm));
        goto exit;
    }
    position->time_nanoseconds = audio_utils_ns_from_timespec(&ts)
            + in->mmap_time_offset_nanos;

exit:
    pthread_mutex_unlock(&in->lock);
    return ret;
}

static int in_get_active_microphones(const struct audio_stream_in *stream,
                                     struct audio_microphone_characteristic_t *mic_array,
                                     size_t *mic_count) {
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    ALOGVV("%s", __func__);

    lock_input_stream(in);
    pthread_mutex_lock(&adev->lock);
    int ret = platform_get_active_microphones(adev->platform,
                                              audio_channel_count_from_in_mask(in->channel_mask),
                                              in->usecase, mic_array, mic_count);
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);

    return ret;
}

static int adev_get_microphones(const struct audio_hw_device *dev,
                                struct audio_microphone_characteristic_t *mic_array,
                                size_t *mic_count) {
    struct audio_device *adev = (struct audio_device *)dev;
    ALOGVV("%s", __func__);

    pthread_mutex_lock(&adev->lock);
    int ret = platform_get_microphones(adev->platform, mic_array, mic_count);
    pthread_mutex_unlock(&adev->lock);

    return ret;
}

static int in_set_microphone_direction(const struct audio_stream_in *stream,
                                           audio_microphone_direction_t dir) {
    struct stream_in *in = (struct stream_in *)stream;

    ALOGVV("%s: standby %d source %d dir %d", __func__, in->standby, in->source, dir);

    in->direction = dir;

    if (in->standby)
        return 0;

    return audio_extn_audiozoom_set_microphone_direction(in, dir);
}

static int in_set_microphone_field_dimension(const struct audio_stream_in *stream, float zoom) {
    struct stream_in *in = (struct stream_in *)stream;

    ALOGVV("%s: standby %d source %d zoom %f", __func__, in->standby, in->source, zoom);

    if (zoom > 1.0 || zoom < -1.0)
        return -EINVAL;

    in->zoom = zoom;

    if (in->standby)
        return 0;

    return audio_extn_audiozoom_set_microphone_field_dimension(in, zoom);
}

static void in_update_sink_metadata(struct audio_stream_in *stream,
                                    const struct sink_metadata *sink_metadata) {

    if (stream == NULL
            || sink_metadata == NULL
            || sink_metadata->tracks == NULL) {
        return;
    }

    int error = 0;
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    audio_devices_t device = AUDIO_DEVICE_NONE;

    if (sink_metadata->track_count != 0)
        device = sink_metadata->tracks->dest_device;

    lock_input_stream(in);
    pthread_mutex_lock(&adev->lock);
    ALOGV("%s: in->usecase: %d, device: %x", __func__, in->usecase, device);

    if (in->usecase == USECASE_AUDIO_RECORD_AFE_PROXY
            && device != AUDIO_DEVICE_NONE
            && adev->voice_tx_output != NULL) {
        /* Use the rx device from afe-proxy record to route voice call because
           there is no routing if tx device is on primary hal and rx device
           is on other hal during voice call. */
        adev->voice_tx_output->devices = device;

        if (!voice_is_call_state_active(adev)) {
            if (adev->mode == AUDIO_MODE_IN_CALL) {
                adev->current_call_output = adev->voice_tx_output;
                error = voice_start_call(adev);
                if (error != 0)
                    ALOGE("%s: start voice call failed %d", __func__, error);
            }
        } else {
            adev->current_call_output = adev->voice_tx_output;
            voice_update_devices_for_all_voice_usecases(adev);
        }
    }

    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);
}

static int check_and_set_gapless_mode(struct audio_device *adev)
{
    bool gapless_enabled = false;
    const char *mixer_ctl_name = "Compress Gapless Playback";
    struct mixer_ctl *ctl;

    ALOGV("%s:", __func__);
    gapless_enabled = property_get_bool("vendor.audio.offload.gapless.enabled", false);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
                               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    if (mixer_ctl_set_value(ctl, 0, gapless_enabled) < 0) {
        ALOGE("%s: Could not set gapless mode %d",
                       __func__, gapless_enabled);
        return -EINVAL;
    }
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address __unused)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_out *out;
    int i, ret = 0;
    bool is_hdmi = devices & AUDIO_DEVICE_OUT_AUX_DIGITAL;
    bool is_usb_dev = audio_is_usb_out_device(devices) &&
                      (devices != AUDIO_DEVICE_OUT_USB_ACCESSORY);
    bool force_haptic_path =
            property_get_bool("vendor.audio.test_haptic", false);

    if (is_usb_dev && !is_usb_ready(adev, true /* is_playback */)) {
        return -ENOSYS;
    }

    ALOGV("%s: enter: format(%#x) sample_rate(%d) channel_mask(%#x) devices(%#x) flags(%#x)",
          __func__, config->format, config->sample_rate, config->channel_mask, devices, flags);

    *stream_out = NULL;
    out = (struct stream_out *)calloc(1, sizeof(struct stream_out));

    pthread_mutex_init(&out->compr_mute_lock, (const pthread_mutexattr_t *) NULL);

    if (devices == AUDIO_DEVICE_NONE)
        devices = AUDIO_DEVICE_OUT_SPEAKER;

    out->flags = flags;
    out->devices = devices;
    out->dev = adev;
    out->handle = handle;
    out->a2dp_compress_mute = false;
    out->mmap_shared_memory_fd = -1; // not open

    /* Init use case and pcm_config */
    if ((is_hdmi || is_usb_dev) &&
        (audio_is_linear_pcm(config->format) || config->format == AUDIO_FORMAT_DEFAULT) &&
        (flags == AUDIO_OUTPUT_FLAG_NONE ||
        (flags & AUDIO_OUTPUT_FLAG_DIRECT) != 0)) {
        audio_format_t req_format = config->format;
        audio_channel_mask_t req_channel_mask = config->channel_mask;
        uint32_t req_sample_rate = config->sample_rate;

        pthread_mutex_lock(&adev->lock);
        if (is_hdmi) {
            ret = read_hdmi_channel_masks(out);
            if (config->sample_rate == 0)
                config->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
            if (config->channel_mask == AUDIO_CHANNEL_NONE)
                config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
            if (config->format == AUDIO_FORMAT_DEFAULT)
                config->format = AUDIO_FORMAT_PCM_16_BIT;
        } else if (is_usb_dev) {
            ret = read_usb_sup_params_and_compare(true /*is_playback*/,
                                                  &config->format,
                                                  &out->supported_formats[0],
                                                  MAX_SUPPORTED_FORMATS,
                                                  &config->channel_mask,
                                                  &out->supported_channel_masks[0],
                                                  MAX_SUPPORTED_CHANNEL_MASKS,
                                                  &config->sample_rate,
                                                  &out->supported_sample_rates[0],
                                                  MAX_SUPPORTED_SAMPLE_RATES);
            ALOGV("plugged dev USB ret %d", ret);
        }
        pthread_mutex_unlock(&adev->lock);
        if (ret != 0) {
            // For MMAP NO IRQ, allow conversions in ADSP
            if (is_hdmi || (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) == 0)
                goto error_open;

            if (req_sample_rate != 0 && config->sample_rate != req_sample_rate)
                config->sample_rate = req_sample_rate;
            if (req_channel_mask != AUDIO_CHANNEL_NONE && config->channel_mask != req_channel_mask)
                config->channel_mask = req_channel_mask;
            if (req_format != AUDIO_FORMAT_DEFAULT && config->format != req_format)
                config->format = req_format;
        }

        out->sample_rate = config->sample_rate;
        out->channel_mask = config->channel_mask;
        out->format = config->format;
        if (is_hdmi) {
            out->usecase = USECASE_AUDIO_PLAYBACK_HIFI;
            out->config = pcm_config_hdmi_multi;
        } else if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
            out->usecase = USECASE_AUDIO_PLAYBACK_MMAP;
            out->config = pcm_config_mmap_playback;
            out->stream.start = out_start;
            out->stream.stop = out_stop;
            out->stream.create_mmap_buffer = out_create_mmap_buffer;
            out->stream.get_mmap_position = out_get_mmap_position;
        } else {
            out->usecase = USECASE_AUDIO_PLAYBACK_HIFI;
            out->config = pcm_config_hifi;
        }

        out->config.rate = out->sample_rate;
        out->config.channels = audio_channel_count_from_out_mask(out->channel_mask);
        if (is_hdmi) {
            out->config.period_size = HDMI_MULTI_PERIOD_BYTES / (out->config.channels *
                                                         audio_bytes_per_sample(out->format));
        }
        out->config.format = pcm_format_from_audio_format(out->format);
    } else if (flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        pthread_mutex_lock(&adev->lock);
        bool offline = (adev->card_status == CARD_STATUS_OFFLINE);
        pthread_mutex_unlock(&adev->lock);

        // reject offload during card offline to allow
        // fallback to s/w paths
        if (offline) {
            ret = -ENODEV;
            goto error_open;
        }

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
        out->sample_rate = config->offload_info.sample_rate;
        if (config->offload_info.channel_mask != AUDIO_CHANNEL_NONE)
            out->channel_mask = config->offload_info.channel_mask;
        else if (config->channel_mask != AUDIO_CHANNEL_NONE)
            out->channel_mask = config->channel_mask;
        else
            out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;

        out->format = config->offload_info.format;

        out->compr_config.codec = (struct snd_codec *)
                                    calloc(1, sizeof(struct snd_codec));

        out->usecase = USECASE_AUDIO_PLAYBACK_OFFLOAD;

        out->stream.set_callback = out_set_callback;
        out->stream.pause = out_pause;
        out->stream.resume = out_resume;
        out->stream.drain = out_drain;
        out->stream.flush = out_flush;

        out->compr_config.codec->id =
                get_snd_codec_id(config->offload_info.format);
        out->compr_config.fragment_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
        out->compr_config.fragments = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
        out->compr_config.codec->sample_rate = out->sample_rate;
        out->compr_config.codec->bit_rate =
                    config->offload_info.bit_rate;
        out->compr_config.codec->ch_in =
                audio_channel_count_from_out_mask(out->channel_mask);
        out->compr_config.codec->ch_out = out->compr_config.codec->ch_in;

        if (flags & AUDIO_OUTPUT_FLAG_NON_BLOCKING)
            out->non_blocking = 1;

        out->send_new_metadata = 1;

        check_and_set_gapless_mode(adev);

        create_offload_callback_thread(out);
        ALOGV("%s: offloaded output offload_info version %04x bit rate %d",
                __func__, config->offload_info.version,
                config->offload_info.bit_rate);
    } else if (out->flags & AUDIO_OUTPUT_FLAG_INCALL_MUSIC) {
        switch (config->sample_rate) {
            case 0:
                out->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
                break;
            case 8000:
            case 16000:
            case 48000:
                out->sample_rate = config->sample_rate;
                break;
            default:
                ALOGE("%s: Unsupported sampling rate %d for Incall Music", __func__,
                      config->sample_rate);
                config->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
                ret = -EINVAL;
                goto error_open;
        }
        //FIXME: add support for MONO stream configuration when audioflinger mixer supports it
        switch (config->channel_mask) {
            case AUDIO_CHANNEL_NONE:
            case AUDIO_CHANNEL_OUT_STEREO:
                out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                break;
            default:
                ALOGE("%s: Unsupported channel mask %#x for Incall Music", __func__,
                      config->channel_mask);
                config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                ret = -EINVAL;
                goto error_open;
        }
        switch (config->format) {
            case AUDIO_FORMAT_DEFAULT:
            case AUDIO_FORMAT_PCM_16_BIT:
                out->format = AUDIO_FORMAT_PCM_16_BIT;
                break;
            default:
                ALOGE("%s: Unsupported format %#x for Incall Music", __func__,
                      config->format);
                config->format = AUDIO_FORMAT_PCM_16_BIT;
                ret = -EINVAL;
                goto error_open;
        }

        voice_extn_check_and_set_incall_music_usecase(adev, out);
    } else  if (out->devices == AUDIO_DEVICE_OUT_TELEPHONY_TX) {
        switch (config->sample_rate) {
            case 0:
                out->sample_rate = AFE_PROXY_SAMPLING_RATE;
                break;
            case 8000:
            case 16000:
            case 48000:
                out->sample_rate = config->sample_rate;
                break;
            default:
                ALOGE("%s: Unsupported sampling rate %d for Telephony TX", __func__,
                      config->sample_rate);
                config->sample_rate = AFE_PROXY_SAMPLING_RATE;
                ret = -EINVAL;
                break;
        }
        //FIXME: add support for MONO stream configuration when audioflinger mixer supports it
        switch (config->channel_mask) {
            case AUDIO_CHANNEL_NONE:
                out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                break;
            case AUDIO_CHANNEL_OUT_STEREO:
                out->channel_mask = config->channel_mask;
                break;
            default:
                ALOGE("%s: Unsupported channel mask %#x for Telephony TX", __func__,
                      config->channel_mask);
                config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                ret = -EINVAL;
                break;
        }
        switch (config->format) {
            case AUDIO_FORMAT_DEFAULT:
                out->format = AUDIO_FORMAT_PCM_16_BIT;
                break;
            case AUDIO_FORMAT_PCM_16_BIT:
                out->format = config->format;
                break;
            default:
                ALOGE("%s: Unsupported format %#x for Telephony TX", __func__,
                      config->format);
                config->format = AUDIO_FORMAT_PCM_16_BIT;
                ret = -EINVAL;
                break;
        }
        if (ret != 0)
            goto error_open;

        out->usecase = USECASE_AUDIO_PLAYBACK_AFE_PROXY;
        out->config = pcm_config_afe_proxy_playback;
        out->config.rate = out->sample_rate;
        out->config.channels =
                audio_channel_count_from_out_mask(out->channel_mask);
        out->config.format = pcm_format_from_audio_format(out->format);
        adev->voice_tx_output = out;
    } else if (flags == AUDIO_OUTPUT_FLAG_VOIP_RX) {
        switch (config->sample_rate) {
            case 0:
                out->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
                break;
            case 8000:
            case 16000:
            case 32000:
            case 48000:
                out->sample_rate = config->sample_rate;
                break;
            default:
                ALOGE("%s: Unsupported sampling rate %d for Voip RX", __func__,
                      config->sample_rate);
                config->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
                ret = -EINVAL;
                break;
        }
        //FIXME: add support for MONO stream configuration when audioflinger mixer supports it
        switch (config->channel_mask) {
            case AUDIO_CHANNEL_NONE:
                out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                break;
            case AUDIO_CHANNEL_OUT_STEREO:
                out->channel_mask = config->channel_mask;
                break;
            default:
                ALOGE("%s: Unsupported channel mask %#x for Voip RX", __func__,
                      config->channel_mask);
                config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                ret = -EINVAL;
                break;
        }
        switch (config->format) {
            case AUDIO_FORMAT_DEFAULT:
                out->format = AUDIO_FORMAT_PCM_16_BIT;
                break;
            case AUDIO_FORMAT_PCM_16_BIT:
                out->format = config->format;
                break;
            default:
                ALOGE("%s: Unsupported format %#x for Voip RX", __func__,
                      config->format);
                config->format = AUDIO_FORMAT_PCM_16_BIT;
                ret = -EINVAL;
                break;
        }
        if (ret != 0)
            goto error_open;

        uint32_t buffer_size, frame_size;
        out->usecase = USECASE_AUDIO_PLAYBACK_VOIP;
        out->config = pcm_config_voip;
        out->config.rate = out->sample_rate;
        out->config.format = pcm_format_from_audio_format(out->format);
        buffer_size = get_stream_buffer_size(VOIP_PLAYBACK_PERIOD_DURATION_MSEC,
                                             out->sample_rate,
                                             out->format,
                                             out->config.channels,
                                             false /*is_low_latency*/);
        frame_size = audio_bytes_per_sample(out->format) * out->config.channels;
        out->config.period_size = buffer_size / frame_size;
        out->config.period_count = VOIP_PLAYBACK_PERIOD_COUNT;
        out->af_period_multiplier = 1;
    } else {
        if (flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
            out->usecase = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
            out->config = pcm_config_deep_buffer;
        } else if (flags & AUDIO_OUTPUT_FLAG_TTS) {
            out->usecase = USECASE_AUDIO_PLAYBACK_TTS;
            out->config = pcm_config_deep_buffer;
        } else if (flags & AUDIO_OUTPUT_FLAG_RAW) {
            out->usecase = USECASE_AUDIO_PLAYBACK_ULL;
            out->realtime = may_use_noirq_mode(adev, USECASE_AUDIO_PLAYBACK_ULL, out->flags);
            out->config = out->realtime ? pcm_config_rt : pcm_config_low_latency;
        } else if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
            out->usecase = USECASE_AUDIO_PLAYBACK_MMAP;
            out->config = pcm_config_mmap_playback;
            out->stream.start = out_start;
            out->stream.stop = out_stop;
            out->stream.create_mmap_buffer = out_create_mmap_buffer;
            out->stream.get_mmap_position = out_get_mmap_position;
        } else {
            if (config->channel_mask & AUDIO_CHANNEL_HAPTIC_ALL) {
                out->usecase = USECASE_AUDIO_PLAYBACK_WITH_HAPTICS;
                adev->haptic_pcm_device_id = platform_get_haptics_pcm_device_id();
                if (adev->haptic_pcm_device_id < 0) {
                    ALOGE("%s: Invalid Haptics pcm device id(%d) for the usecase(%d)",
                          __func__, adev->haptic_pcm_device_id, out->usecase);
                    ret = -ENOSYS;
                    goto error_open;
                }
                out->config = pcm_config_haptics_audio;
                if (force_haptic_path)
                    adev->haptics_config = pcm_config_haptics_audio;
                else
                    adev->haptics_config = pcm_config_haptics;
            } else {
                out->usecase = USECASE_AUDIO_PLAYBACK_LOW_LATENCY;
                out->config = pcm_config_low_latency;
            }
        }

        if (out->usecase == USECASE_AUDIO_PLAYBACK_LOW_LATENCY ||
               out->usecase == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER ||
               out->usecase == USECASE_AUDIO_PLAYBACK_ULL) {
            out->volume_l = 1.0;
            out->volume_r = 1.0;
        }

        if (config->sample_rate == 0) {
            out->sample_rate = out->config.rate;
        } else {
            out->sample_rate = config->sample_rate;
        }

        if (config->channel_mask == AUDIO_CHANNEL_NONE) {
            out->channel_mask = audio_channel_out_mask_from_count(out->config.channels);
        } else {
            out->channel_mask = config->channel_mask;
        }

        if (config->format == AUDIO_FORMAT_DEFAULT)
            out->format = audio_format_from_pcm_format(out->config.format);
        else if (!audio_is_linear_pcm(config->format)) {
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            ret = -EINVAL;
            goto error_open;
        } else {
            out->format = config->format;
        }

        out->config.rate = out->sample_rate;

        if (config->channel_mask & AUDIO_CHANNEL_HAPTIC_ALL) {
             out->config.channels =
                audio_channel_count_from_out_mask(out->channel_mask &
                                                  ~AUDIO_CHANNEL_HAPTIC_ALL);

             if (force_haptic_path) {
                 out->config.channels = 1;
                 adev->haptics_config.channels = 1;
             } else {
                 adev->haptics_config.channels =
                     audio_channel_count_from_out_mask(out->channel_mask &
                                                      AUDIO_CHANNEL_HAPTIC_ALL);
             }
        } else {
             out->config.channels =
                    audio_channel_count_from_out_mask(out->channel_mask);
        }

        if (out->format != audio_format_from_pcm_format(out->config.format)) {
            out->config.format = pcm_format_from_audio_format(out->format);
        }
    }

    if ((config->sample_rate != 0 && config->sample_rate != out->sample_rate) ||
        (config->format != AUDIO_FORMAT_DEFAULT && config->format != out->format) ||
        (config->channel_mask != AUDIO_CHANNEL_NONE && config->channel_mask != out->channel_mask)) {
        ALOGI("%s: Unsupported output config. sample_rate:%u format:%#x channel_mask:%#x",
              __func__, config->sample_rate, config->format, config->channel_mask);
        config->sample_rate = out->sample_rate;
        config->format = out->format;
        config->channel_mask = out->channel_mask;
        ret = -EINVAL;
        goto error_open;
    }

    ALOGV("%s: Usecase(%s) config->format %#x  out->config.format %#x\n",
            __func__, use_case_table[out->usecase], config->format, out->config.format);

    if (flags & AUDIO_OUTPUT_FLAG_PRIMARY) {
        if (adev->primary_output == NULL)
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

    out->applied_volume_l = INVALID_OUT_VOLUME;
    out->applied_volume_r = INVALID_OUT_VOLUME;

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
#ifdef NO_AUDIO_OUT
    out->stream.write = out_write_for_no_output;
#else
    out->stream.write = out_write;
#endif
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;

    if (out->realtime)
        out->af_period_multiplier = af_period_multiplier;
    else
        out->af_period_multiplier = 1;

    out->kernel_buffer_size = out->config.period_size * out->config.period_count;

    out->standby = 1;
    /* out->muted = false; by calloc() */
    /* out->written = 0; by calloc() */

    pthread_mutex_init(&out->lock, (const pthread_mutexattr_t *) NULL);
    pthread_mutex_init(&out->pre_lock, (const pthread_mutexattr_t *) NULL);
    pthread_cond_init(&out->cond, (const pthread_condattr_t *) NULL);

    config->format = out->stream.common.get_format(&out->stream.common);
    config->channel_mask = out->stream.common.get_channels(&out->stream.common);
    config->sample_rate = out->stream.common.get_sample_rate(&out->stream.common);

    register_format(out->format, out->supported_formats);
    register_channel_mask(out->channel_mask, out->supported_channel_masks);
    register_sample_rate(out->sample_rate, out->supported_sample_rates);

    out->error_log = error_log_create(
            ERROR_LOG_ENTRIES,
            1000000000 /* aggregate consecutive identical errors within one second in ns */);

    /*
       By locking output stream before registering, we allow the callback
       to update stream's state only after stream's initial state is set to
       adev state.
    */
    lock_output_stream(out);
    audio_extn_snd_mon_register_listener(out, out_snd_mon_cb);
    pthread_mutex_lock(&adev->lock);
    out->card_status = adev->card_status;
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);

    stream_app_type_cfg_init(&out->app_type_cfg);

    *stream_out = &out->stream;

    ALOGV("%s: exit", __func__);
    return 0;

error_open:
    free(out);
    *stream_out = NULL;
    ALOGW("%s: exit: ret %d", __func__, ret);
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev __unused,
                                     struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;

    ALOGV("%s: enter", __func__);

    // must deregister from sndmonitor first to prevent races
    // between the callback and close_stream
    audio_extn_snd_mon_unregister_listener(out);
    out_standby(&stream->common);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        destroy_offload_callback_thread(out);

        if (out->compr_config.codec != NULL)
            free(out->compr_config.codec);
    }

    out->a2dp_compress_mute = false;

    if (adev->voice_tx_output == out)
        adev->voice_tx_output = NULL;

    error_log_destroy(out->error_log);
    out->error_log = NULL;

    pthread_cond_destroy(&out->cond);
    pthread_mutex_destroy(&out->pre_lock);
    pthread_mutex_destroy(&out->lock);
    free(stream);
    ALOGV("%s: exit", __func__);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int val;
    int ret;
    int status = 0;
    bool a2dp_reconfig = false;

    ALOGV("%s: enter: %s", __func__, kvpairs);

    pthread_mutex_lock(&adev->lock);

    parms = str_parms_create_str(kvpairs);
    status = voice_set_parameters(adev, parms);
    if (status != 0) {
        goto done;
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_NREC, value, sizeof(value));
    if (ret >= 0) {
        /* When set to false, HAL should disable EC and NS */
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->bluetooth_nrec = true;
        else
            adev->bluetooth_nrec = false;
    }

    ret = str_parms_get_str(parms, "screen_state", value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->screen_off = false;
        else
            adev->screen_off = true;
    }

    ret = str_parms_get_int(parms, "rotation", &val);
    if (ret >= 0) {
        bool reverse_speakers = false;
        int camera_rotation = CAMERA_ROTATION_LANDSCAPE;
        switch (val) {
        // FIXME: note that the code below assumes that the speakers are in the correct placement
        //   relative to the user when the device is rotated 90deg from its default rotation. This
        //   assumption is device-specific, not platform-specific like this code.
        case 270:
            reverse_speakers = true;
            camera_rotation = CAMERA_ROTATION_INVERT_LANDSCAPE;
            break;
        case 0:
        case 180:
            camera_rotation = CAMERA_ROTATION_PORTRAIT;
            break;
        case 90:
            camera_rotation = CAMERA_ROTATION_LANDSCAPE;
            break;
        default:
            ALOGE("%s: unexpected rotation of %d", __func__, val);
            status = -EINVAL;
        }
        if (status == 0) {
            // check and set swap
            //   - check if orientation changed and speaker active
            //   - set rotation and cache the rotation value
            adev->camera_orientation =
                           (adev->camera_orientation & ~CAMERA_ROTATION_MASK) | camera_rotation;
#ifndef MAXXAUDIO_QDSP_ENABLED
            platform_check_and_set_swap_lr_channels(adev, reverse_speakers);
#endif
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_SCO_WB, value, sizeof(value));
    if (ret >= 0) {
        adev->bt_wb_speech_enabled = !strcmp(value, AUDIO_PARAMETER_VALUE_ON);
    }

    ret = str_parms_get_str(parms, "BT_SCO", value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->bt_sco_on = true;
        else
            adev->bt_sco_on = false;
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value, sizeof(value));
    if (ret >= 0) {
        audio_devices_t device = (audio_devices_t)strtoul(value, NULL, 10);
        if (audio_is_usb_out_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0) {
                const int card = atoi(value);
                audio_extn_usb_add_device(device, card);
            }
        } else if (audio_is_usb_in_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0) {
                const int card = atoi(value);
                audio_extn_usb_add_device(device, card);
            }
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value, sizeof(value));
    if (ret >= 0) {
        audio_devices_t device = (audio_devices_t)strtoul(value, NULL, 10);
        if (audio_is_usb_out_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0) {
                const int card = atoi(value);
                audio_extn_usb_remove_device(device, card);
            }
        } else if (audio_is_usb_in_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0) {
                const int card = atoi(value);
                audio_extn_usb_remove_device(device, card);
            }
        }
    }

    audio_extn_hfp_set_parameters(adev, parms);
    audio_extn_ma_set_parameters(adev, parms);

    status = audio_extn_a2dp_set_parameters(parms, &a2dp_reconfig);
    if (status >= 0 && a2dp_reconfig) {
        struct audio_usecase *usecase;
        struct listnode *node;
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if ((usecase->type == PCM_PLAYBACK) &&
                (usecase->devices & AUDIO_DEVICE_OUT_ALL_A2DP)) {
                ALOGD("%s: reconfigure A2DP... forcing device switch", __func__);

                pthread_mutex_unlock(&adev->lock);
                lock_output_stream(usecase->stream.out);
                pthread_mutex_lock(&adev->lock);
                audio_extn_a2dp_set_handoff_mode(true);
                // force device switch to reconfigure encoder
                select_devices(adev, usecase->id);
                audio_extn_a2dp_set_handoff_mode(false);
                pthread_mutex_unlock(&usecase->stream.out->lock);
                break;
            }
        }
    }

    //FIXME: to be replaced by proper video capture properties API
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_CAMERA_FACING, value, sizeof(value));
    if (ret >= 0) {
        int camera_facing = CAMERA_FACING_BACK;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_FRONT) == 0)
            camera_facing = CAMERA_FACING_FRONT;
        else if (strcmp(value, AUDIO_PARAMETER_VALUE_BACK) == 0)
            camera_facing = CAMERA_FACING_BACK;
        else {
            ALOGW("%s: invalid camera facing value: %s", __func__, value);
            goto done;
        }
        adev->camera_orientation =
                       (adev->camera_orientation & ~CAMERA_FACING_MASK) | camera_facing;
        struct audio_usecase *usecase;
        struct listnode *node;
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            struct stream_in *in = usecase->stream.in;
            if (usecase->type == PCM_CAPTURE && in != NULL &&
                    in->source == AUDIO_SOURCE_CAMCORDER && !in->standby) {
                select_devices(adev, in->usecase);
            }
        }
    }

done:
    str_parms_destroy(parms);
    pthread_mutex_unlock(&adev->lock);
    ALOGV("%s: exit with code(%d)", __func__, status);
    return status;
}

static char* adev_get_parameters(const struct audio_hw_device *dev,
                                 const char *keys)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct str_parms *reply = str_parms_create();
    struct str_parms *query = str_parms_create_str(keys);
    char *str;

    pthread_mutex_lock(&adev->lock);

    voice_get_parameters(adev, query, reply);
    audio_extn_a2dp_get_parameters(query, reply);

    str = str_parms_to_str(reply);
    str_parms_destroy(query);
    str_parms_destroy(reply);

    pthread_mutex_unlock(&adev->lock);
    ALOGV("%s: exit: returns - %s", __func__, str);
    return str;
}

static int adev_init_check(const struct audio_hw_device *dev __unused)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    int ret;
    struct audio_device *adev = (struct audio_device *)dev;

    audio_extn_extspk_set_voice_vol(adev->extspk, volume);

    pthread_mutex_lock(&adev->lock);
    ret = voice_set_volume(adev, volume);
    pthread_mutex_unlock(&adev->lock);

    return ret;
}

static int adev_set_master_volume(struct audio_hw_device *dev __unused, float volume __unused)
{
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev __unused,
                                  float *volume __unused)
{
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev __unused, bool muted __unused)
{
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev __unused, bool *muted __unused)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct audio_device *adev = (struct audio_device *)dev;

    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        ALOGD("%s: mode %d", __func__, (int)mode);
        adev->mode = mode;
        if ((mode == AUDIO_MODE_NORMAL || mode == AUDIO_MODE_IN_COMMUNICATION) &&
                voice_is_in_call(adev)) {
            voice_stop_call(adev);
            adev->current_call_output = NULL;

            /*
             * After stopping the call, it must check if any active capture
             * activity device needs to be re-selected.
             */
            struct audio_usecase *usecase;
            struct listnode *node;
            list_for_each(node, &adev->usecase_list) {
                usecase = node_to_item(node, struct audio_usecase, list);
                if (usecase->type == PCM_CAPTURE && usecase->stream.in != NULL) {
                    select_devices_with_force_switch(adev, usecase->id, true);
                }
            }
        }
    }
    pthread_mutex_unlock(&adev->lock);

    audio_extn_extspk_set_mode(adev->extspk, mode);

    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    int ret;
    struct audio_device *adev = (struct audio_device *)dev;

    ALOGD("%s: state %d", __func__, (int)state);
    pthread_mutex_lock(&adev->lock);
    if (audio_extn_tfa_98xx_is_supported() && adev->enable_hfp) {
        ret = audio_extn_hfp_set_mic_mute(adev, state);
    } else {
        ret = voice_set_mic_mute(adev, state);
    }
    adev->mic_muted = state;
    pthread_mutex_unlock(&adev->lock);

    return ret;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    *state = voice_get_mic_mute((struct audio_device *)dev);
    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev __unused,
                                         const struct audio_config *config)
{
    int channel_count = audio_channel_count_from_in_mask(config->channel_mask);

    /* Don't know if USB HIFI in this context so use true to be conservative */
    if (check_input_parameters(config->sample_rate, config->format, channel_count,
                               true /*is_usb_hifi */) != 0)
        return 0;

    return get_stream_buffer_size(AUDIO_CAPTURE_PERIOD_DURATION_MSEC,
                                 config->sample_rate, config->format,
                                 channel_count,
                                 false /* is_low_latency: since we don't know, be conservative */);
}

static bool adev_input_allow_hifi_record(struct audio_device *adev,
                                         audio_devices_t devices,
                                         audio_input_flags_t flags,
                                         audio_source_t source) {
    const bool allowed = true;

    if (!audio_is_usb_in_device(devices))
        return !allowed;

    switch (flags) {
        case AUDIO_INPUT_FLAG_NONE:
        case AUDIO_INPUT_FLAG_FAST: // just fast, not fast|raw || fast|mmap
            break;
        default:
            return !allowed;
    }

    switch (source) {
        case AUDIO_SOURCE_DEFAULT:
        case AUDIO_SOURCE_MIC:
        case AUDIO_SOURCE_UNPROCESSED:
            break;
        default:
            return !allowed;
    }

    switch (adev->mode) {
        case 0:
            break;
        default:
            return !allowed;
    }

    return allowed;
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags,
                                  const char *address __unused,
                                  audio_source_t source )
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_in *in;
    int ret = 0, buffer_size, frame_size;
    int channel_count;
    bool is_low_latency = false;
    bool is_usb_dev = audio_is_usb_in_device(devices);
    bool may_use_hifi_record = adev_input_allow_hifi_record(adev,
                                                            devices,
                                                            flags,
                                                            source);
    ALOGV("%s: enter: flags %#x, is_usb_dev %d, may_use_hifi_record %d,"
            " sample_rate %u, channel_mask %#x, format %#x",
            __func__, flags, is_usb_dev, may_use_hifi_record,
            config->sample_rate, config->channel_mask, config->format);
    *stream_in = NULL;

    if (is_usb_dev && !is_usb_ready(adev, false /* is_playback */)) {
        return -ENOSYS;
    }

    if (!(is_usb_dev && may_use_hifi_record)) {
        if (config->sample_rate == 0)
            config->sample_rate = DEFAULT_INPUT_SAMPLING_RATE;
        if (config->channel_mask == AUDIO_CHANNEL_NONE)
            config->channel_mask = AUDIO_CHANNEL_IN_MONO;
        if (config->format == AUDIO_FORMAT_DEFAULT)
            config->format = AUDIO_FORMAT_PCM_16_BIT;

        channel_count = audio_channel_count_from_in_mask(config->channel_mask);

        if (check_input_parameters(config->sample_rate, config->format, channel_count, false) != 0)
            return -EINVAL;
    }

    if (audio_extn_tfa_98xx_is_supported() &&
        (audio_extn_hfp_is_active(adev) || voice_is_in_call(adev)))
        return -EINVAL;

    in = (struct stream_in *)calloc(1, sizeof(struct stream_in));

    pthread_mutex_init(&in->lock, (const pthread_mutexattr_t *) NULL);
    pthread_mutex_init(&in->pre_lock, (const pthread_mutexattr_t *) NULL);

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
    in->stream.get_capture_position = in_get_capture_position;
    in->stream.get_active_microphones = in_get_active_microphones;
    in->stream.set_microphone_direction = in_set_microphone_direction;
    in->stream.set_microphone_field_dimension = in_set_microphone_field_dimension;
    in->stream.update_sink_metadata = in_update_sink_metadata;

    in->device = devices;
    in->source = source;
    in->dev = adev;
    in->standby = 1;
    in->capture_handle = handle;
    in->flags = flags;
    in->direction = MIC_DIRECTION_UNSPECIFIED;
    in->zoom = 0;
    in->mmap_shared_memory_fd = -1; // not open
    list_init(&in->aec_list);
    list_init(&in->ns_list);

    ALOGV("%s: source %d, config->channel_mask %#x", __func__, source, config->channel_mask);
    if (source == AUDIO_SOURCE_VOICE_UPLINK ||
         source == AUDIO_SOURCE_VOICE_DOWNLINK) {
        /* Force channel config requested to mono if incall
           record is being requested for only uplink/downlink */
        if (config->channel_mask != AUDIO_CHANNEL_IN_MONO) {
            config->channel_mask = AUDIO_CHANNEL_IN_MONO;
            ret = -EINVAL;
            goto err_open;
        }
    }

    if (is_usb_dev && may_use_hifi_record) {
        /* HiFi record selects an appropriate format, channel, rate combo
           depending on sink capabilities*/
        ret = read_usb_sup_params_and_compare(false /*is_playback*/,
                                              &config->format,
                                              &in->supported_formats[0],
                                              MAX_SUPPORTED_FORMATS,
                                              &config->channel_mask,
                                              &in->supported_channel_masks[0],
                                              MAX_SUPPORTED_CHANNEL_MASKS,
                                              &config->sample_rate,
                                              &in->supported_sample_rates[0],
                                              MAX_SUPPORTED_SAMPLE_RATES);
        if (ret != 0) {
            ret = -EINVAL;
            goto err_open;
        }
        channel_count = audio_channel_count_from_in_mask(config->channel_mask);
    } else if (config->format == AUDIO_FORMAT_DEFAULT) {
        config->format = AUDIO_FORMAT_PCM_16_BIT;
    } else if (config->format == AUDIO_FORMAT_PCM_FLOAT ||
               config->format == AUDIO_FORMAT_PCM_24_BIT_PACKED ||
               config->format == AUDIO_FORMAT_PCM_8_24_BIT) {
        bool ret_error = false;
        /* 24 bit is restricted to UNPROCESSED source only,also format supported
           from HAL is 8_24
           *> In case of UNPROCESSED source, for 24 bit, if format requested is other than
              8_24 return error indicating supported format is 8_24
           *> In case of any other source requesting 24 bit or float return error
              indicating format supported is 16 bit only.

           on error flinger will retry with supported format passed
         */
        if (!is_supported_24bits_audiosource(source)) {
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            ret_error = true;
        } else if (config->format != AUDIO_FORMAT_PCM_8_24_BIT) {
            config->format = AUDIO_FORMAT_PCM_8_24_BIT;
            ret_error = true;
        }

        if (ret_error) {
            ret = -EINVAL;
            goto err_open;
        }
    }

    in->format = config->format;
    in->channel_mask = config->channel_mask;

    /* Update config params with the requested sample rate and channels */
    if (in->device == AUDIO_DEVICE_IN_TELEPHONY_RX) {
        if (config->sample_rate == 0)
            config->sample_rate = AFE_PROXY_SAMPLING_RATE;
        if (config->sample_rate != 48000 && config->sample_rate != 16000 &&
                config->sample_rate != 8000) {
            config->sample_rate = AFE_PROXY_SAMPLING_RATE;
            ret = -EINVAL;
            goto err_open;
        }

        if (config->format != AUDIO_FORMAT_PCM_16_BIT) {
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            ret = -EINVAL;
            goto err_open;
        }

        in->usecase = USECASE_AUDIO_RECORD_AFE_PROXY;
        in->config = pcm_config_afe_proxy_record;
        in->af_period_multiplier = 1;
    } else if (is_usb_dev && may_use_hifi_record) {
        in->usecase = USECASE_AUDIO_RECORD_HIFI;
        in->config = pcm_config_audio_capture;
        frame_size = audio_stream_in_frame_size(&in->stream);
        buffer_size = get_stream_buffer_size(AUDIO_CAPTURE_PERIOD_DURATION_MSEC,
                                             config->sample_rate,
                                             config->format,
                                             channel_count,
                                             false /*is_low_latency*/);
        in->config.period_size = buffer_size / frame_size;
        in->config.rate = config->sample_rate;
        in->af_period_multiplier = 1;
        in->config.format = pcm_format_from_audio_format(config->format);
    } else {
        in->usecase = USECASE_AUDIO_RECORD;
        if (config->sample_rate == LOW_LATENCY_CAPTURE_SAMPLE_RATE &&
                (in->flags & AUDIO_INPUT_FLAG_FAST) != 0) {
            is_low_latency = true;
#if LOW_LATENCY_CAPTURE_USE_CASE
            in->usecase = USECASE_AUDIO_RECORD_LOW_LATENCY;
#endif
            in->realtime = may_use_noirq_mode(adev, in->usecase, in->flags);
            if (!in->realtime) {
                in->config = pcm_config_audio_capture;
                frame_size = audio_stream_in_frame_size(&in->stream);
                buffer_size = get_stream_buffer_size(AUDIO_CAPTURE_PERIOD_DURATION_MSEC,
                                                     config->sample_rate,
                                                     config->format,
                                                     channel_count,
                                                     is_low_latency);
                in->config.period_size = buffer_size / frame_size;
                in->config.rate = config->sample_rate;
                in->af_period_multiplier = 1;
            } else {
                // period size is left untouched for rt mode playback
                in->config = pcm_config_audio_capture_rt;
                in->af_period_multiplier = af_period_multiplier;
            }
        } else if ((config->sample_rate == LOW_LATENCY_CAPTURE_SAMPLE_RATE) &&
                ((in->flags & AUDIO_INPUT_FLAG_MMAP_NOIRQ) != 0)) {
            // FIXME: Add support for multichannel capture over USB using MMAP
            in->usecase = USECASE_AUDIO_RECORD_MMAP;
            in->config = pcm_config_mmap_capture;
            in->stream.start = in_start;
            in->stream.stop = in_stop;
            in->stream.create_mmap_buffer = in_create_mmap_buffer;
            in->stream.get_mmap_position = in_get_mmap_position;
            in->af_period_multiplier = 1;
            ALOGV("%s: USECASE_AUDIO_RECORD_MMAP", __func__);
        } else if (in->source == AUDIO_SOURCE_VOICE_COMMUNICATION &&
                   in->flags & AUDIO_INPUT_FLAG_VOIP_TX &&
                   (config->sample_rate == 8000 ||
                    config->sample_rate == 16000 ||
                    config->sample_rate == 32000 ||
                    config->sample_rate == 48000) &&
                   channel_count == 1) {
            in->usecase = USECASE_AUDIO_RECORD_VOIP;
            in->config = pcm_config_audio_capture;
            frame_size = audio_stream_in_frame_size(&in->stream);
            buffer_size = get_stream_buffer_size(VOIP_CAPTURE_PERIOD_DURATION_MSEC,
                                                 config->sample_rate,
                                                 config->format,
                                                 channel_count, false /*is_low_latency*/);
            in->config.period_size = buffer_size / frame_size;
            in->config.period_count = VOIP_CAPTURE_PERIOD_COUNT;
            in->config.rate = config->sample_rate;
            in->af_period_multiplier = 1;
        } else {
            in->config = pcm_config_audio_capture;
            frame_size = audio_stream_in_frame_size(&in->stream);
            buffer_size = get_stream_buffer_size(AUDIO_CAPTURE_PERIOD_DURATION_MSEC,
                                                 config->sample_rate,
                                                 config->format,
                                                 channel_count,
                                                 is_low_latency);
            in->config.period_size = buffer_size / frame_size;
            in->config.rate = config->sample_rate;
            in->af_period_multiplier = 1;
        }
        if (config->format == AUDIO_FORMAT_PCM_8_24_BIT)
            in->config.format = PCM_FORMAT_S24_LE;
    }

    in->config.channels = channel_count;
    in->sample_rate  = in->config.rate;


    register_format(in->format, in->supported_formats);
    register_channel_mask(in->channel_mask, in->supported_channel_masks);
    register_sample_rate(in->sample_rate, in->supported_sample_rates);

    in->error_log = error_log_create(
            ERROR_LOG_ENTRIES,
            NANOS_PER_SECOND /* aggregate consecutive identical errors within one second */);

    /* This stream could be for sound trigger lab,
       get sound trigger pcm if present */
    audio_extn_sound_trigger_check_and_get_session(in);

    if (in->is_st_session)
        in->flags |= AUDIO_INPUT_FLAG_HW_HOTWORD;

    lock_input_stream(in);
    audio_extn_snd_mon_register_listener(in, in_snd_mon_cb);
    pthread_mutex_lock(&adev->lock);
    in->card_status = adev->card_status;
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);

    stream_app_type_cfg_init(&in->app_type_cfg);

    *stream_in = &in->stream;
    ALOGV("%s: exit", __func__);
    return 0;

err_open:
    free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev __unused,
                                    struct audio_stream_in *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    ALOGV("%s", __func__);

    // must deregister from sndmonitor first to prevent races
    // between the callback and close_stream
    audio_extn_snd_mon_unregister_listener(stream);
    in_standby(&stream->common);

    error_log_destroy(in->error_log);
    in->error_log = NULL;

    pthread_mutex_destroy(&in->pre_lock);
    pthread_mutex_destroy(&in->lock);

    free(stream);

    return;
}

static int adev_dump(const audio_hw_device_t *device __unused, int fd __unused)
{
    return 0;
}

/* verifies input and output devices and their capabilities.
 *
 * This verification is required when enabling extended bit-depth or
 * sampling rates, as not all qcom products support it.
 *
 * Suitable for calling only on initialization such as adev_open().
 * It fills the audio_device use_case_table[] array.
 *
 * Has a side-effect that it needs to configure audio routing / devices
 * in order to power up the devices and read the device parameters.
 * It does not acquire any hw device lock. Should restore the devices
 * back to "normal state" upon completion.
 */
static int adev_verify_devices(struct audio_device *adev)
{
    /* enumeration is a bit difficult because one really wants to pull
     * the use_case, device id, etc from the hidden pcm_device_table[].
     * In this case there are the following use cases and device ids.
     *
     * [USECASE_AUDIO_PLAYBACK_DEEP_BUFFER] = {0, 0},
     * [USECASE_AUDIO_PLAYBACK_LOW_LATENCY] = {15, 15},
     * [USECASE_AUDIO_PLAYBACK_HIFI] = {1, 1},
     * [USECASE_AUDIO_PLAYBACK_OFFLOAD] = {9, 9},
     * [USECASE_AUDIO_RECORD] = {0, 0},
     * [USECASE_AUDIO_RECORD_LOW_LATENCY] = {15, 15},
     * [USECASE_VOICE_CALL] = {2, 2},
     *
     * USECASE_AUDIO_PLAYBACK_OFFLOAD, USECASE_AUDIO_PLAYBACK_HIFI omitted.
     * USECASE_VOICE_CALL omitted, but possible for either input or output.
     */

    /* should be the usecases enabled in adev_open_input_stream() */
    static const int test_in_usecases[] = {
             USECASE_AUDIO_RECORD,
             USECASE_AUDIO_RECORD_LOW_LATENCY, /* does not appear to be used */
    };
    /* should be the usecases enabled in adev_open_output_stream()*/
    static const int test_out_usecases[] = {
            USECASE_AUDIO_PLAYBACK_DEEP_BUFFER,
            USECASE_AUDIO_PLAYBACK_LOW_LATENCY,
    };
    static const usecase_type_t usecase_type_by_dir[] = {
            PCM_PLAYBACK,
            PCM_CAPTURE,
    };
    static const unsigned flags_by_dir[] = {
            PCM_OUT,
            PCM_IN,
    };

    size_t i;
    unsigned dir;
    const unsigned card_id = adev->snd_card;
    char info[512]; /* for possible debug info */

    for (dir = 0; dir < 2; ++dir) {
        const usecase_type_t usecase_type = usecase_type_by_dir[dir];
        const unsigned flags_dir = flags_by_dir[dir];
        const size_t testsize =
                dir ? ARRAY_SIZE(test_in_usecases) : ARRAY_SIZE(test_out_usecases);
        const int *testcases =
                dir ? test_in_usecases : test_out_usecases;
        const audio_devices_t audio_device =
                dir ? AUDIO_DEVICE_IN_BUILTIN_MIC : AUDIO_DEVICE_OUT_SPEAKER;

        for (i = 0; i < testsize; ++i) {
            const audio_usecase_t audio_usecase = testcases[i];
            int device_id;
            snd_device_t snd_device;
            struct pcm_params **pparams;
            struct stream_out out;
            struct stream_in in;
            struct audio_usecase uc_info;
            int retval;

            pparams = &adev->use_case_table[audio_usecase];
            pcm_params_free(*pparams); /* can accept null input */
            *pparams = NULL;

            /* find the device ID for the use case (signed, for error) */
            device_id = platform_get_pcm_device_id(audio_usecase, usecase_type);
            if (device_id < 0)
                continue;

            /* prepare structures for device probing */
            memset(&uc_info, 0, sizeof(uc_info));
            uc_info.id = audio_usecase;
            uc_info.type = usecase_type;
            if (dir) {
                memset(&in, 0, sizeof(in));
                in.device = audio_device;
                in.source = AUDIO_SOURCE_VOICE_COMMUNICATION;
                uc_info.stream.in = &in;
            }
            memset(&out, 0, sizeof(out));
            out.devices = audio_device; /* only field needed in select_devices */
            uc_info.stream.out = &out;
            uc_info.devices = audio_device;
            uc_info.in_snd_device = SND_DEVICE_NONE;
            uc_info.out_snd_device = SND_DEVICE_NONE;
            list_add_tail(&adev->usecase_list, &uc_info.list);

            /* select device - similar to start_(in/out)put_stream() */
            retval = select_devices(adev, audio_usecase);
            if (retval >= 0) {
                *pparams = pcm_params_get(card_id, device_id, flags_dir);
#if LOG_NDEBUG == 0
                if (*pparams) {
                    ALOGV("%s: (%s) card %d  device %d", __func__,
                            dir ? "input" : "output", card_id, device_id);
                    pcm_params_to_string(*pparams, info, ARRAY_SIZE(info));
                } else {
                    ALOGV("%s: cannot locate card %d  device %d", __func__, card_id, device_id);
                }
#endif
            }

            /* deselect device - similar to stop_(in/out)put_stream() */
            /* 1. Get and set stream specific mixer controls */
            retval = disable_audio_route(adev, &uc_info);
            /* 2. Disable the rx device */
            retval = disable_snd_device(adev,
                    dir ? uc_info.in_snd_device : uc_info.out_snd_device);
            list_remove(&uc_info.list);
        }
    }
    return 0;
}

static int adev_close(hw_device_t *device)
{
    size_t i;

    pthread_mutex_lock(&adev_init_lock);
    if (!device || ((struct audio_device *)device != adev))
        goto done;

    if ((--audio_device_ref_count) == 0) {
        audio_extn_snd_mon_unregister_listener(adev);
        audio_extn_tfa_98xx_deinit();
        audio_extn_ma_deinit();
        audio_route_free(adev->audio_route);
        free(adev->snd_dev_ref_cnt);
        platform_deinit(adev->platform);
        audio_extn_extspk_deinit(adev->extspk);
        audio_extn_sound_trigger_deinit(adev);
        audio_extn_snd_mon_deinit();
        for (i = 0; i < ARRAY_SIZE(adev->use_case_table); ++i) {
            pcm_params_free(adev->use_case_table[i]);
        }
        if (adev->adm_deinit)
            adev->adm_deinit(adev->adm_data);
        pthread_mutex_destroy(&adev->lock);
        free(device);
        adev = NULL;
    }

done:
    pthread_mutex_unlock(&adev_init_lock);
    return 0;
}

/* This returns 1 if the input parameter looks at all plausible as a low latency period size,
 * or 0 otherwise.  A return value of 1 doesn't mean the value is guaranteed to work,
 * just that it _might_ work.
 */
static int period_size_is_plausible_for_low_latency(int period_size)
{
    switch (period_size) {
    case 48:
    case 96:
    case 144:
    case 160:
    case 192:
    case 240:
    case 320:
    case 480:
        return 1;
    default:
        return 0;
    }
}

static void adev_snd_mon_cb(void * stream __unused, struct str_parms * parms)
{
    int card;
    card_status_t status;

    if (!parms)
        return;

    if (parse_snd_card_status(parms, &card, &status) < 0)
        return;

    pthread_mutex_lock(&adev->lock);
    bool valid_cb = (card == adev->snd_card);
    if (valid_cb) {
        if (adev->card_status != status) {
            adev->card_status = status;
            platform_snd_card_update(adev->platform, status);
        }
    }
    pthread_mutex_unlock(&adev->lock);
    return;
}

/* out and adev lock held */
static int check_a2dp_restore_l(struct audio_device *adev, struct stream_out *out, bool restore)
{
    struct audio_usecase *uc_info;
    float left_p;
    float right_p;
    audio_devices_t devices;

    uc_info = get_usecase_from_list(adev, out->usecase);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, out->usecase);
        return -EINVAL;
    }

    ALOGD("%s: enter: usecase(%d: %s)", __func__,
          out->usecase, use_case_table[out->usecase]);

    if (restore) {
        // restore A2DP device for active usecases and unmute if required
        if ((out->devices & AUDIO_DEVICE_OUT_ALL_A2DP) &&
            !is_a2dp_device(uc_info->out_snd_device)) {
            ALOGD("%s: restoring A2DP and unmuting stream", __func__);
            select_devices(adev, uc_info->id);
            pthread_mutex_lock(&out->compr_mute_lock);
            if ((out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) &&
                (out->a2dp_compress_mute)) {
                out->a2dp_compress_mute = false;
                out_set_compr_volume(&out->stream, out->volume_l, out->volume_r);
            }
            pthread_mutex_unlock(&out->compr_mute_lock);
        }
    } else {
        // mute compress stream if suspended
        pthread_mutex_lock(&out->compr_mute_lock);
        if ((out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) &&
            (!out->a2dp_compress_mute)) {
            if (!out->standby) {
                ALOGD("%s: selecting speaker and muting stream", __func__);
                devices = out->devices;
                out->devices = AUDIO_DEVICE_OUT_SPEAKER;
                left_p = out->volume_l;
                right_p = out->volume_r;
                if (out->offload_state == OFFLOAD_STATE_PLAYING)
                    compress_pause(out->compr);
                out_set_compr_volume(&out->stream, 0.0f, 0.0f);
                out->a2dp_compress_mute = true;
                select_devices(adev, out->usecase);
                if (out->offload_state == OFFLOAD_STATE_PLAYING)
                    compress_resume(out->compr);
                out->devices = devices;
                out->volume_l = left_p;
                out->volume_r = right_p;
            }
        }
        pthread_mutex_unlock(&out->compr_mute_lock);
    }
    ALOGV("%s: exit", __func__);
    return 0;
}

int check_a2dp_restore(struct audio_device *adev, struct stream_out *out, bool restore)
{
    int ret = 0;

    lock_output_stream(out);
    pthread_mutex_lock(&adev->lock);

    ret = check_a2dp_restore_l(adev, out, restore);

    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
    return ret;
}

static int adev_open(const hw_module_t *module, const char *name,
                     hw_device_t **device)
{
    int i, ret;

    ALOGD("%s: enter", __func__);
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) return -EINVAL;
    pthread_mutex_lock(&adev_init_lock);
    if (audio_device_ref_count != 0) {
        *device = &adev->device.common;
        audio_device_ref_count++;
        ALOGV("%s: returning existing instance of adev", __func__);
        ALOGV("%s: exit", __func__);
        pthread_mutex_unlock(&adev_init_lock);
        return 0;
    }
    adev = calloc(1, sizeof(struct audio_device));

    pthread_mutex_init(&adev->lock, (const pthread_mutexattr_t *) NULL);

    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->device.common.module = (struct hw_module_t *)module;
    adev->device.common.close = adev_close;

    adev->device.init_check = adev_init_check;
    adev->device.set_voice_volume = adev_set_voice_volume;
    adev->device.set_master_volume = adev_set_master_volume;
    adev->device.get_master_volume = adev_get_master_volume;
    adev->device.set_master_mute = adev_set_master_mute;
    adev->device.get_master_mute = adev_get_master_mute;
    adev->device.set_mode = adev_set_mode;
    adev->device.set_mic_mute = adev_set_mic_mute;
    adev->device.get_mic_mute = adev_get_mic_mute;
    adev->device.set_parameters = adev_set_parameters;
    adev->device.get_parameters = adev_get_parameters;
    adev->device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->device.open_output_stream = adev_open_output_stream;
    adev->device.close_output_stream = adev_close_output_stream;
    adev->device.open_input_stream = adev_open_input_stream;

    adev->device.close_input_stream = adev_close_input_stream;
    adev->device.dump = adev_dump;
    adev->device.get_microphones = adev_get_microphones;

    /* Set the default route before the PCM stream is opened */
    pthread_mutex_lock(&adev->lock);
    adev->mode = AUDIO_MODE_NORMAL;
    adev->primary_output = NULL;
    adev->bluetooth_nrec = true;
    adev->acdb_settings = TTY_MODE_OFF;
    /* adev->cur_hdmi_channels = 0;  by calloc() */
    adev->snd_dev_ref_cnt = calloc(SND_DEVICE_MAX, sizeof(int));
    voice_init(adev);
    list_init(&adev->usecase_list);
    pthread_mutex_unlock(&adev->lock);

    /* Loads platform specific libraries dynamically */
    adev->platform = platform_init(adev);
    if (!adev->platform) {
        free(adev->snd_dev_ref_cnt);
        free(adev);
        ALOGE("%s: Failed to init platform data, aborting.", __func__);
        *device = NULL;
        pthread_mutex_unlock(&adev_init_lock);
        return -EINVAL;
    }
    adev->extspk = audio_extn_extspk_init(adev);

    adev->visualizer_lib = dlopen(VISUALIZER_LIBRARY_PATH, RTLD_NOW);
    if (adev->visualizer_lib == NULL) {
        ALOGW("%s: DLOPEN failed for %s", __func__, VISUALIZER_LIBRARY_PATH);
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__, VISUALIZER_LIBRARY_PATH);
        adev->visualizer_start_output =
                    (int (*)(audio_io_handle_t, int, int, int))dlsym(adev->visualizer_lib,
                                                    "visualizer_hal_start_output");
        adev->visualizer_stop_output =
                    (int (*)(audio_io_handle_t, int))dlsym(adev->visualizer_lib,
                                                    "visualizer_hal_stop_output");
    }

    adev->offload_effects_lib = dlopen(OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH, RTLD_NOW);
    if (adev->offload_effects_lib == NULL) {
        ALOGW("%s: DLOPEN failed for %s", __func__,
              OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH);
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__,
              OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH);
        adev->offload_effects_start_output =
                    (int (*)(audio_io_handle_t, int))dlsym(adev->offload_effects_lib,
                                     "offload_effects_bundle_hal_start_output");
        adev->offload_effects_stop_output =
                    (int (*)(audio_io_handle_t, int))dlsym(adev->offload_effects_lib,
                                     "offload_effects_bundle_hal_stop_output");
    }

    adev->adm_lib = dlopen(ADM_LIBRARY_PATH, RTLD_NOW);
    if (adev->adm_lib == NULL) {
        ALOGW("%s: DLOPEN failed for %s", __func__, ADM_LIBRARY_PATH);
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__, ADM_LIBRARY_PATH);
        adev->adm_init = (adm_init_t)
                                dlsym(adev->adm_lib, "adm_init");
        adev->adm_deinit = (adm_deinit_t)
                                dlsym(adev->adm_lib, "adm_deinit");
        adev->adm_register_input_stream = (adm_register_input_stream_t)
                                dlsym(adev->adm_lib, "adm_register_input_stream");
        adev->adm_register_output_stream = (adm_register_output_stream_t)
                                dlsym(adev->adm_lib, "adm_register_output_stream");
        adev->adm_deregister_stream = (adm_deregister_stream_t)
                                dlsym(adev->adm_lib, "adm_deregister_stream");
        adev->adm_request_focus = (adm_request_focus_t)
                                dlsym(adev->adm_lib, "adm_request_focus");
        adev->adm_abandon_focus = (adm_abandon_focus_t)
                                dlsym(adev->adm_lib, "adm_abandon_focus");
        adev->adm_set_config = (adm_set_config_t)
                                    dlsym(adev->adm_lib, "adm_set_config");
        adev->adm_request_focus_v2 = (adm_request_focus_v2_t)
                                    dlsym(adev->adm_lib, "adm_request_focus_v2");
        adev->adm_is_noirq_avail = (adm_is_noirq_avail_t)
                                    dlsym(adev->adm_lib, "adm_is_noirq_avail");
        adev->adm_on_routing_change = (adm_on_routing_change_t)
                                    dlsym(adev->adm_lib, "adm_on_routing_change");
    }

    adev->bt_wb_speech_enabled = false;
    adev->enable_voicerx = false;

    *device = &adev->device.common;

    if (k_enable_extended_precision)
        adev_verify_devices(adev);

    char value[PROPERTY_VALUE_MAX];
    int trial;
    if ((property_get("vendor.audio_hal.period_size", value, NULL) > 0) ||
        (property_get("audio_hal.period_size", value, NULL) > 0)) {
        trial = atoi(value);
        if (period_size_is_plausible_for_low_latency(trial)) {
            pcm_config_low_latency.period_size = trial;
            pcm_config_low_latency.start_threshold = trial / 4;
            pcm_config_low_latency.avail_min = trial / 4;
            configured_low_latency_capture_period_size = trial;
        }
    }
    if ((property_get("vendor.audio_hal.in_period_size", value, NULL) > 0) ||
        (property_get("audio_hal.in_period_size", value, NULL) > 0)) {
        trial = atoi(value);
        if (period_size_is_plausible_for_low_latency(trial)) {
            configured_low_latency_capture_period_size = trial;
        }
    }

    adev->mic_break_enabled = property_get_bool("vendor.audio.mic_break", false);

    adev->camera_orientation = CAMERA_DEFAULT;

    // commented as full set of app type cfg is sent from platform
    // audio_extn_utils_send_default_app_type_cfg(adev->platform, adev->mixer);
    audio_device_ref_count++;

    if ((property_get("vendor.audio_hal.period_multiplier", value, NULL) > 0) ||
        (property_get("audio_hal.period_multiplier", value, NULL) > 0)) {
        af_period_multiplier = atoi(value);
        if (af_period_multiplier < 0) {
            af_period_multiplier = 2;
        } else if (af_period_multiplier > 4) {
            af_period_multiplier = 4;
        }
        ALOGV("new period_multiplier = %d", af_period_multiplier);
    }

    audio_extn_tfa_98xx_init(adev);
    audio_extn_ma_init(adev->platform);
    audio_extn_audiozoom_init();

    pthread_mutex_unlock(&adev_init_lock);

    if (adev->adm_init)
        adev->adm_data = adev->adm_init();

    audio_extn_perf_lock_init();
    audio_extn_snd_mon_init();
    pthread_mutex_lock(&adev->lock);
    audio_extn_snd_mon_register_listener(NULL, adev_snd_mon_cb);
    adev->card_status = CARD_STATUS_ONLINE;
    pthread_mutex_unlock(&adev->lock);
    audio_extn_sound_trigger_init(adev);/* dependent on snd_mon_init() */

    ALOGD("%s: exit", __func__);
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
        .name = "QCOM Audio HAL",
        .author = "Code Aurora Forum",
        .methods = &hal_module_methods,
    },
};
