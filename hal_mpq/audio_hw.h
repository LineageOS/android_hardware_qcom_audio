/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Not a contribution.
 *
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

#ifndef QCOM_AUDIO_HW_H
#define QCOM_AUDIO_HW_H

#include <cutils/list.h>
#include <hardware/audio.h>
#include <tinyalsa/asoundlib.h>
#include <tinycompress/tinycompress.h>
#include "sound/compress_params.h"
#include <audio_route/audio_route.h>

#define VISUALIZER_LIBRARY_PATH "/system/lib/soundfx/libqcomvisualizer.so"

/* Flags used to initialize acdb_settings variable that goes to ACDB library */
#define DMIC_FLAG       0x00000002
#define QMIC_FLAG       0x00000004
#define TTY_MODE_OFF    0x00000010
#define TTY_MODE_FULL   0x00000020
#define TTY_MODE_VCO    0x00000040
#define TTY_MODE_HCO    0x00000080
#define TTY_MODE_CLEAR  0xFFFFFF0F

#define ACDB_DEV_TYPE_OUT 1
#define ACDB_DEV_TYPE_IN 2

#define MAX_SUPPORTED_CHANNEL_MASKS 2
#define DEFAULT_HDMI_OUT_CHANNELS   2

typedef int snd_device_t;
#include <platform.h>

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
    USECASE_AUDIO_PLAYBACK_FM,
    USECASE_AUDIO_PLAYBACK_OFFLOAD,
    USECASE_AUDIO_PLAYBACK_OFFLOAD1,
    USECASE_AUDIO_PLAYBACK_OFFLOAD2,
    USECASE_AUDIO_PLAYBACK_OFFLOAD3,
    /* Capture usecases */
    USECASE_AUDIO_RECORD,
    USECASE_AUDIO_RECORD_COMPRESS,
    USECASE_AUDIO_RECORD_LOW_LATENCY,
    USECASE_AUDIO_RECORD_FM_VIRTUAL,

    /* Voice usecase */
    USECASE_VOICE_CALL,

    /* Voice extension usecases */
    USECASE_VOICE2_CALL,
    USECASE_VOLTE_CALL,
    USECASE_QCHAT_CALL,
    USECASE_COMPRESS_VOIP_CALL,

    USECASE_INCALL_REC_UPLINK,
    USECASE_INCALL_REC_DOWNLINK,
    USECASE_INCALL_REC_UPLINK_AND_DOWNLINK,

    USECASE_INCALL_MUSIC_UPLINK,
    USECASE_INCALL_MUSIC_UPLINK2,

    USECASE_AUDIO_SPKR_CALIB_RX,
    USECASE_AUDIO_SPKR_CALIB_TX,
    AUDIO_USECASE_MAX
} audio_usecase_t;

typedef enum {
    DEEP_BUFFER_PLAYBACK_STREAM = 0,
    LOW_LATENCY_PLAYBACK_STREAM,
    MCH_PCM_PLAYBACK_STREAM,
    OFFLOAD_PLAYBACK_STREAM,
    LOW_LATENCY_RECORD_STREAM,
    RECORD_STREAM,
    VOICE_CALL_STREAM
} audio_usecase_stream_type_t;

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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/*
 * tinyAlsa library interprets period size as number of frames
 * one frame = channel_count * sizeof (pcm sample)
 * so if format = 16-bit PCM and channels = Stereo, frame size = 2 ch * 2 = 4 bytes
 * DEEP_BUFFER_OUTPUT_PERIOD_SIZE = 1024 means 1024 * 4 = 4096 bytes
 * We should take care of returning proper size when AudioFlinger queries for
 * the buffer size of an input/output stream
 */

enum {
    OFFLOAD_CMD_EXIT,               /* exit compress offload thread loop*/
    OFFLOAD_CMD_DRAIN,              /* send a full drain request to DSP */
    OFFLOAD_CMD_PARTIAL_DRAIN,      /* send a partial drain request to DSP */
    OFFLOAD_CMD_WAIT_FOR_BUFFER,    /* wait for buffer released by DSP */
};

enum {
    OFFLOAD_STATE_IDLE,
    OFFLOAD_STATE_PLAYING,
    OFFLOAD_STATE_PAUSED,
};

struct offload_cmd {
    struct listnode node;
    int cmd;
    int data[];
};

struct alsa_handle {

    struct listnode list;
//Parameters of the stream
    struct pcm *pcm;
    struct pcm_config config;

    struct compress *compr;
    struct compr_config compr_config;

    struct stream_out *out;

    audio_usecase_t usecase;
    int device_id;
    unsigned int sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t input_format;
    audio_format_t output_format;
    audio_devices_t devices;

    route_format_t route_format;
    int decoder_type;

    bool cmd_pending ;
};

struct stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    pthread_cond_t  cond;
    /* TODO remove this */
    /*
    struct pcm_config config;
    struct compr_config compr_config;
    struct pcm *pcm;
    struct compress *compr;
    */
    int standby;
    int pcm_device_id;
    unsigned int sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    audio_devices_t devices;
    audio_output_flags_t flags;
    audio_usecase_t usecase;
    /* Array of supported channel mask configurations. +1 so that the last entry is always 0 */
    audio_channel_mask_t supported_channel_masks[MAX_SUPPORTED_CHANNEL_MASKS + 1];
    bool muted;
    uint64_t written; /* total frames written, not cleared when entering standby */
    audio_io_handle_t handle;

    int non_blocking;
    int playback_started;
    int offload_state;
    pthread_cond_t offload_cond;
    pthread_t offload_thread;
    struct listnode offload_cmd_list;
    bool offload_thread_blocked;

    stream_callback_t offload_callback;
    void *offload_cookie;
    struct compr_gapless_mdata gapless_mdata;
    int send_new_metadata;

    struct audio_device *dev;

    /*devices configuration */
    int left_volume;
    int right_volume;
    audio_usecase_stream_type_t uc_strm_type;
    int hdmi_format;
    int spdif_format;
    int* device_formats;   //TODO:Needs to come from AudioRutingManager
    struct audio_config *config;

    /* list of the session handles */
    struct listnode session_list;

    /* /MS11 instance */
    int use_ms11_decoder;
    void  *ms11_decoder;
    struct compr_config compr_config;

    int channels;

    /* Buffering utility */
    struct audio_bitstream_sm    *bitstrm;

    int                 buffer_size;
    int                 decoder_type;
    bool                dec_conf_set;
    uint32_t            min_bytes_req_to_dec;
    bool                is_m11_file_mode;
    void                *dec_conf_buf;
    int32_t             dec_conf_bufLength;
    bool                first_bitstrm_buf;

    bool                open_dec_route;
    int                 dec_format_devices;
    bool                open_dec_mch_route;
    int                 dec_mch_format_devices;
    bool                open_passt_route;
    int                 passt_format_devices;
    bool                sw_open_trans_route;
    int                 sw_trans_format_devices;
    bool                hw_open_trans_route;
    int                 hw_trans_format_devices;
    bool                channel_status_set;
    unsigned char       channel_status[24];
    int                 route_audio_to_a2dp;
    int                 is_ms11_file_playback_mode;
    char *              write_temp_buf;
    struct output_metadata  output_meta_data;
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
    bool enable_ns;
    audio_format_t format;

    struct audio_device *dev;
};

typedef enum {
    PCM_PLAYBACK,
    PCM_CAPTURE,
    VOICE_CALL,
    VOIP_CALL
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
    struct alsa_handle *handle;
};

struct audio_device {
    struct audio_hw_device device;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    struct mixer *mixer;
    audio_mode_t mode;
    audio_devices_t out_device;
    struct stream_in *active_input;
    struct stream_out *primary_output;
    bool bluetooth_nrec;
    bool screen_off;
    int *snd_dev_ref_cnt;
    struct listnode usecase_list;
    struct audio_route *audio_route;
    int acdb_settings;
    bool speaker_lr_swap;
    unsigned int cur_hdmi_channels;

    void *platform;

    void *visualizer_lib;
    int (*visualizer_start_output)(audio_io_handle_t);
    int (*visualizer_stop_output)(audio_io_handle_t);
};

static const char * const use_case_table[AUDIO_USECASE_MAX] = {
    [USECASE_AUDIO_PLAYBACK_DEEP_BUFFER] = "deep-buffer-playback",
    [USECASE_AUDIO_PLAYBACK_LOW_LATENCY] = "low-latency-playback",
    [USECASE_AUDIO_PLAYBACK_MULTI_CH] = "multi-channel-playback",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD] = "compress-offload-playback",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD1] = "compress-offload-playback1",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD2] = "compress-offload-playback2",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD3] = "compress-offload-playback3",
    [USECASE_AUDIO_RECORD] = "audio-record",
    [USECASE_AUDIO_RECORD_COMPRESS] = "audio-record-compress",
    [USECASE_AUDIO_RECORD_LOW_LATENCY] = "low-latency-record",
    [USECASE_AUDIO_RECORD_FM_VIRTUAL] = "fm-virtual-record",
    [USECASE_AUDIO_PLAYBACK_FM] = "play-fm",
    [USECASE_VOICE_CALL] = "voice-call",

    [USECASE_VOICE2_CALL] = "voice2-call",
    [USECASE_VOLTE_CALL] = "volte-call",
    [USECASE_QCHAT_CALL] = "qchat-call",
    [USECASE_COMPRESS_VOIP_CALL] = "compress-voip-call",
    [USECASE_INCALL_REC_UPLINK] = "incall-rec-uplink",
    [USECASE_INCALL_REC_DOWNLINK] = "incall-rec-downlink",
    [USECASE_INCALL_REC_UPLINK_AND_DOWNLINK] = "incall-rec-uplink-and-downlink",
    [USECASE_INCALL_MUSIC_UPLINK] = "incall_music_uplink",
    [USECASE_INCALL_MUSIC_UPLINK2] = "incall_music_uplink2",
    [USECASE_AUDIO_SPKR_CALIB_RX] = "spkr-rx-calib",
    [USECASE_AUDIO_SPKR_CALIB_TX] = "spkr-vi-record",
};


int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out);

void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream);

int select_devices(struct audio_device *adev,
                          audio_usecase_t uc_id);
int disable_audio_route(struct audio_device *adev,
                               struct audio_usecase *usecase,
                               bool update_mixer);
int disable_snd_device(struct audio_device *adev,
                              snd_device_t snd_device,
                              bool update_mixer);
int enable_snd_device(struct audio_device *adev,
                             snd_device_t snd_device,
                             bool update_mixer);
int enable_audio_route(struct audio_device *adev,
                              struct audio_usecase *usecase,
                              bool update_mixer);
struct audio_usecase *get_usecase_from_list(struct audio_device *adev,
                                                   audio_usecase_t uc_id);
/*
 * NOTE: when multiple mutexes have to be acquired, always take the
 * stream_in or stream_out mutex first, followed by the audio_device mutex.
 */

#endif // QCOM_AUDIO_HW_H
