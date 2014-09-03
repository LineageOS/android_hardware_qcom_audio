/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include <hardware/audio.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>
#include <audio_effects/effect_ns.h>

#include <tinyalsa/asoundlib.h>

#include <audio_route/audio_route.h>
#include "audio_defs.h"

#define ACDB_DEV_TYPE_OUT 1
#define ACDB_DEV_TYPE_IN 2

#define DUALMIC_CONFIG_NONE 0      /* Target does not contain 2 mics */
#define DUALMIC_CONFIG_ENDFIRE 1
#define DUALMIC_CONFIG_BROADSIDE 2

/*
 * Below are the devices for which is back end is same, SLIMBUS_0_RX.
 * All these devices are handled by the internal HW codec. We can
 * enable any one of these devices at any time
 */
#define AUDIO_DEVICE_OUT_ALL_CODEC_BACKEND \
    (AUDIO_DEVICE_OUT_EARPIECE | AUDIO_DEVICE_OUT_SPEAKER | \
     AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE)

/* Sound devices specific to the platform
 * The DEVICE_OUT_* and DEVICE_IN_* should be mapped to these sound
 * devices to enable corresponding mixer paths
 */
typedef enum {
    SND_DEVICE_NONE = 0,

    /* Playback devices */
    SND_DEVICE_MIN,
    SND_DEVICE_OUT_BEGIN = SND_DEVICE_MIN,
    SND_DEVICE_OUT_HANDSET = SND_DEVICE_OUT_BEGIN,
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_VOICE_SPEAKER,
    SND_DEVICE_OUT_VOICE_HEADPHONES,
    SND_DEVICE_OUT_HDMI,
    SND_DEVICE_OUT_SPEAKER_AND_HDMI,
    SND_DEVICE_OUT_BT_SCO,
    SND_DEVICE_OUT_VOICE_HANDSET_TMUS,
    SND_DEVICE_OUT_VOICE_TTY_FULL_HEADPHONES,
    SND_DEVICE_OUT_VOICE_TTY_VCO_HEADPHONES,
    SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET,
    SND_DEVICE_OUT_END,

    /*
     * Note: IN_BEGIN should be same as OUT_END because total number of devices
     * SND_DEVICES_MAX should not exceed MAX_RX + MAX_TX devices.
     */
    /* Capture devices */
    SND_DEVICE_IN_BEGIN = SND_DEVICE_OUT_END,
    SND_DEVICE_IN_HANDSET_MIC  = SND_DEVICE_IN_BEGIN,
    SND_DEVICE_IN_SPEAKER_MIC,
    SND_DEVICE_IN_HEADSET_MIC,
    SND_DEVICE_IN_HANDSET_MIC_AEC,
    SND_DEVICE_IN_SPEAKER_MIC_AEC,
    SND_DEVICE_IN_HEADSET_MIC_AEC,
    SND_DEVICE_IN_VOICE_SPEAKER_MIC,
    SND_DEVICE_IN_VOICE_HEADSET_MIC,
    SND_DEVICE_IN_HDMI_MIC,
    SND_DEVICE_IN_BT_SCO_MIC,
    SND_DEVICE_IN_CAMCORDER_MIC,
    SND_DEVICE_IN_VOICE_DMIC_EF,
    SND_DEVICE_IN_VOICE_DMIC_BS,
    SND_DEVICE_IN_VOICE_DMIC_EF_TMUS,
    SND_DEVICE_IN_VOICE_SPEAKER_DMIC_EF,
    SND_DEVICE_IN_VOICE_SPEAKER_DMIC_BS,
    SND_DEVICE_IN_VOICE_TTY_FULL_HEADSET_MIC,
    SND_DEVICE_IN_VOICE_TTY_VCO_HANDSET_MIC,
    SND_DEVICE_IN_VOICE_TTY_HCO_HEADSET_MIC,
    SND_DEVICE_IN_VOICE_REC_MIC,
    SND_DEVICE_IN_VOICE_REC_DMIC_EF,
    SND_DEVICE_IN_VOICE_REC_DMIC_BS,
    SND_DEVICE_IN_VOICE_REC_DMIC_EF_FLUENCE,
    SND_DEVICE_IN_VOICE_REC_DMIC_BS_FLUENCE,
    SND_DEVICE_IN_END,

    SND_DEVICE_MAX = SND_DEVICE_IN_END,

} snd_device_t;


/* These are the supported use cases by the hardware.
 * Each usecase is mapped to a specific PCM device.
 * Refer to pcm_device_table[].
 */
typedef enum {
    USECASE_INVALID = -1,
    /* Playback usecases */
    USECASE_AUDIO_PLAYBACK_DEEP_BUFFER = 0,
    USECASE_AUDIO_PLAYBACK_LOW_LATENCY,
    USECASE_AUDIO_PLAYBACK_MULTI_CH,

    /* Capture usecases */
    USECASE_AUDIO_RECORD,
    USECASE_AUDIO_RECORD_LOW_LATENCY,

    USECASE_VOICE_CALL,

    AUDIO_USECASE_MAX
} audio_usecase_t;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define SOUND_CARD 0

#define DEFAULT_OUTPUT_SAMPLING_RATE 48000

/*
 * tinyAlsa library interprets period size as number of frames
 * one frame = channel_count * sizeof (pcm sample)
 * so if format = 16-bit PCM and channels = Stereo, frame size = 2 ch * 2 = 4 bytes
 * DEEP_BUFFER_OUTPUT_PERIOD_SIZE = 1024 means 1024 * 4 = 4096 bytes
 * We should take care of returning proper size when AudioFlinger queries for
 * the buffer size of an input/output stream
 */
#ifdef MSM8974
#define DEEP_BUFFER_OUTPUT_PERIOD_SIZE 1024
#else
#define DEEP_BUFFER_OUTPUT_PERIOD_SIZE 960
#endif
#define DEEP_BUFFER_OUTPUT_PERIOD_COUNT 8

#ifdef MSM8974
#define LOW_LATENCY_OUTPUT_PERIOD_SIZE 256
#else
#define LOW_LATENCY_OUTPUT_PERIOD_SIZE 240
#endif
#define LOW_LATENCY_OUTPUT_PERIOD_COUNT 2

#define HDMI_MULTI_PERIOD_SIZE  336
#define HDMI_MULTI_PERIOD_COUNT 8
#define HDMI_MULTI_DEFAULT_CHANNEL_COUNT 6
#define HDMI_MULTI_PERIOD_BYTES (HDMI_MULTI_PERIOD_SIZE * HDMI_MULTI_DEFAULT_CHANNEL_COUNT * 2)

#ifdef MSM8974
#define AUDIO_CAPTURE_PERIOD_SIZE 512
#define AUDIO_CAPTURE_PERIOD_COUNT 16
#else
#define AUDIO_CAPTURE_PERIOD_SIZE 320
#define AUDIO_CAPTURE_PERIOD_COUNT 2
#endif

#define MAX_SUPPORTED_CHANNEL_MASKS 2

struct stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    int standby;
    int pcm_device_id;
    audio_channel_mask_t channel_mask;
    audio_devices_t devices;
    audio_output_flags_t flags;
    audio_usecase_t usecase;
    /* Array of supported channel mask configurations. +1 so that the last entry is always 0 */
    audio_channel_mask_t supported_channel_masks[MAX_SUPPORTED_CHANNEL_MASKS + 1];
    bool muted;

    struct audio_device *dev;
};

struct stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    int standby;
    int source;
    int pcm_device_id;
    int device;
    audio_channel_mask_t channel_mask;
    audio_usecase_t usecase;
    bool enable_aec;

    struct audio_device *dev;
};

typedef enum {
    PCM_PLAYBACK,
    PCM_CAPTURE,
    VOICE_CALL
} usecase_type_t;

union stream_ptr {
    struct stream_in *in;
    struct stream_out *out;
};

struct audio_usecase {
    struct listnode list;
    audio_usecase_t id;
    usecase_type_t  type;
    audio_devices_t devices;
    snd_device_t out_snd_device;
    snd_device_t in_snd_device;
    union stream_ptr stream;
};

typedef void (*acdb_deallocate_t)();
typedef int  (*acdb_init_t)();
typedef void (*acdb_send_audio_cal_t)(int, int);
typedef void (*acdb_send_voice_cal_t)(int, int);

typedef int (*csd_client_init_t)();
typedef int (*csd_client_deinit_t)();
typedef int (*csd_disable_device_t)();
typedef int (*csd_enable_device_t)(int, int, uint32_t);
typedef int (*csd_volume_t)(int);
typedef int (*csd_mic_mute_t)(int);
typedef int (*csd_start_voice_t)();
typedef int (*csd_stop_voice_t)();

struct audio_device {
    struct audio_hw_device device;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    struct mixer *mixer;
    audio_mode_t mode;
    audio_devices_t out_device;
    struct stream_in *active_input;
    struct stream_out *primary_output;
    int in_call;
    float voice_volume;
    bool mic_mute;
    int tty_mode;
    bool bluetooth_nrec;
    bool screen_off;
    struct pcm *voice_call_rx;
    struct pcm *voice_call_tx;
    int snd_dev_ref_cnt[SND_DEVICE_MAX];
    struct listnode usecase_list;
    struct audio_route *audio_route;
    int acdb_settings;

    bool mic_type_analog;
    bool fluence_in_spkr_mode;
    bool fluence_in_voice_call;
    bool fluence_in_voice_rec;
    int  dualmic_config;

    /* Audio calibration related functions */
    void *acdb_handle;
    acdb_init_t acdb_init;
    acdb_deallocate_t acdb_deallocate;
    acdb_send_audio_cal_t acdb_send_audio_cal;
    acdb_send_voice_cal_t acdb_send_voice_cal;

    /* CSD Client related functions for voice call */
    void *csd_client;
    csd_client_init_t csd_client_init;
    csd_client_deinit_t csd_client_deinit;
    csd_disable_device_t csd_disable_device;
    csd_enable_device_t csd_enable_device;
    csd_volume_t csd_volume;
    csd_mic_mute_t csd_mic_mute;
    csd_start_voice_t csd_start_voice;
    csd_stop_voice_t csd_stop_voice;
};

/*
 * NOTE: when multiple mutexes have to be acquired, always take the
 * stream_in or stream_out mutex first, followed by the audio_device mutex.
 */

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

struct pcm_config pcm_config_audio_capture = {
    .channels = 2,
    .period_size = AUDIO_CAPTURE_PERIOD_SIZE,
    .period_count = AUDIO_CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_voice_call = {
    .channels = 1,
    .rate = 8000,
    .period_size = 160,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
};

