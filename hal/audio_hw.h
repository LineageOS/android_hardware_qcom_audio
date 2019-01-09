/*
 * Copyright (c) 2013-2018, The Linux Foundation. All rights reserved.
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
 *
 * This file was modified by DTS, Inc. The portions of the
 * code modified by DTS, Inc are copyrighted and
 * licensed separately, as follows:
 *
 * (C) 2014 DTS, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef QCOM_AUDIO_HW_H
#define QCOM_AUDIO_HW_H

#include <stdlib.h>
#include <cutils/list.h>
#include <hardware/audio_amplifier.h>
#include <hardware/audio.h>
#include <tinyalsa/asoundlib.h>
#include <tinycompress/tinycompress.h>

#include <audio_route/audio_route.h>
#include "audio_defs.h"
#include "voice.h"
#include "audio_hw_extn_api.h"

#if LINUX_ENABLED
#define VISUALIZER_LIBRARY_PATH "libqcomvisualizer.so"
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "libqcompostprocbundle.so"
#define ADM_LIBRARY_PATH "libadm.so"
#else
#define VISUALIZER_LIBRARY_PATH "/vendor/lib/soundfx/libqcomvisualizer.so"
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/vendor/lib/soundfx/libqcompostprocbundle.so"
#define ADM_LIBRARY_PATH "/vendor/lib/libadm.so"
#endif

/* Flags used to initialize acdb_settings variable that goes to ACDB library */
#define NONE_FLAG            0x00000000
#define ANC_FLAG	     0x00000001
#define DMIC_FLAG            0x00000002
#define QMIC_FLAG            0x00000004
/* Include TMIC Flag after existing QMIC flag to avoid backward compatibility
 * issues since they are bit masked */
#define TMIC_FLAG            0x00000008
#define TTY_MODE_OFF         0x00000010
#define TTY_MODE_FULL        0x00000020
#define TTY_MODE_VCO         0x00000040
#define TTY_MODE_HCO         0x00000080
#define TTY_MODE_CLEAR       0xFFFFFF0F
#define FLUENCE_MODE_CLEAR   0xFFFFFFF0

#define ACDB_DEV_TYPE_OUT 1
#define ACDB_DEV_TYPE_IN 2

#define MAX_SUPPORTED_CHANNEL_MASKS 14
#define MAX_SUPPORTED_FORMATS 15
#define MAX_SUPPORTED_SAMPLE_RATES 7
#define DEFAULT_HDMI_OUT_CHANNELS   2
#define DEFAULT_HDMI_OUT_SAMPLE_RATE 48000
#define DEFAULT_HDMI_OUT_FORMAT AUDIO_FORMAT_PCM_16_BIT

#define SND_CARD_STATE_OFFLINE 0
#define SND_CARD_STATE_ONLINE 1

#define STREAM_DIRECTION_IN 0
#define STREAM_DIRECTION_OUT 1

#define MAX_PERF_LOCK_OPTS 20

#define MAX_STREAM_PROFILE_STR_LEN 32
typedef enum {
    EFFECT_NONE = 0,
    EFFECT_AEC,
    EFFECT_NS,
    EFFECT_MAX
} effect_type_t;

struct audio_effect_config {
    uint32_t module_id;
    uint32_t instance_id;
    uint32_t param_id;
    uint32_t param_value;
};

#define MAX_MIXER_PATH_LEN 64

typedef enum card_status_t {
    CARD_STATUS_OFFLINE,
    CARD_STATUS_ONLINE
} card_status_t;

/* These are the supported use cases by the hardware.
 * Each usecase is mapped to a specific PCM device.
 * Refer to pcm_device_table[].
 */
enum {
    USECASE_INVALID = -1,
    /* Playback usecases */
    USECASE_AUDIO_PLAYBACK_DEEP_BUFFER = 0,
    USECASE_AUDIO_PLAYBACK_LOW_LATENCY,
    USECASE_AUDIO_PLAYBACK_MULTI_CH,
    USECASE_AUDIO_PLAYBACK_OFFLOAD,
    USECASE_AUDIO_PLAYBACK_OFFLOAD2,
    USECASE_AUDIO_PLAYBACK_OFFLOAD3,
    USECASE_AUDIO_PLAYBACK_OFFLOAD4,
    USECASE_AUDIO_PLAYBACK_OFFLOAD5,
    USECASE_AUDIO_PLAYBACK_OFFLOAD6,
    USECASE_AUDIO_PLAYBACK_OFFLOAD7,
    USECASE_AUDIO_PLAYBACK_OFFLOAD8,
    USECASE_AUDIO_PLAYBACK_OFFLOAD9,
    USECASE_AUDIO_PLAYBACK_ULL,
    USECASE_AUDIO_PLAYBACK_MMAP,
    USECASE_AUDIO_PLAYBACK_HIFI,

    /* FM usecase */
    USECASE_AUDIO_PLAYBACK_FM,

    /* HFP Use case*/
    USECASE_AUDIO_HFP_SCO,
    USECASE_AUDIO_HFP_SCO_WB,

    /* Capture usecases */
    USECASE_AUDIO_RECORD,
    USECASE_AUDIO_RECORD_COMPRESS,
    USECASE_AUDIO_RECORD_COMPRESS2,
    USECASE_AUDIO_RECORD_COMPRESS3,
    USECASE_AUDIO_RECORD_COMPRESS4,
    USECASE_AUDIO_RECORD_COMPRESS5,
    USECASE_AUDIO_RECORD_COMPRESS6,
    USECASE_AUDIO_RECORD_LOW_LATENCY,
    USECASE_AUDIO_RECORD_FM_VIRTUAL,
    USECASE_AUDIO_RECORD_HIFI,

    USECASE_AUDIO_PLAYBACK_VOIP,
    USECASE_AUDIO_RECORD_VOIP,
    /* Voice usecase */
    USECASE_VOICE_CALL,
    USECASE_AUDIO_RECORD_MMAP,

    /* Voice extension usecases */
    USECASE_VOICE2_CALL,
    USECASE_VOLTE_CALL,
    USECASE_QCHAT_CALL,
    USECASE_VOWLAN_CALL,
    USECASE_VOICEMMODE1_CALL,
    USECASE_VOICEMMODE2_CALL,
    USECASE_COMPRESS_VOIP_CALL,

    USECASE_INCALL_REC_UPLINK,
    USECASE_INCALL_REC_DOWNLINK,
    USECASE_INCALL_REC_UPLINK_AND_DOWNLINK,
    USECASE_INCALL_REC_UPLINK_COMPRESS,
    USECASE_INCALL_REC_DOWNLINK_COMPRESS,
    USECASE_INCALL_REC_UPLINK_AND_DOWNLINK_COMPRESS,

    USECASE_INCALL_MUSIC_UPLINK,
    USECASE_INCALL_MUSIC_UPLINK2,

    USECASE_AUDIO_SPKR_CALIB_RX,
    USECASE_AUDIO_SPKR_CALIB_TX,

    USECASE_AUDIO_PLAYBACK_AFE_PROXY,
    USECASE_AUDIO_RECORD_AFE_PROXY,

    USECASE_AUDIO_PLAYBACK_SILENCE,

    USECASE_AUDIO_TRANSCODE_LOOPBACK,

    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM1,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM2,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM3,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM4,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM5,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM6,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM7,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM8,

    USECASE_AUDIO_EC_REF_LOOPBACK,

    USECASE_AUDIO_A2DP_ABR_FEEDBACK,

    USECASE_AUDIO_ULTRASOUND_RX,
    USECASE_AUDIO_ULTRASOUND_TX,

    AUDIO_USECASE_MAX
};

const char * const use_case_table[AUDIO_USECASE_MAX];

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
    OFFLOAD_CMD_ERROR,              /* offload playback hit some error */
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

typedef enum render_mode {
    RENDER_MODE_AUDIO_NO_TIMESTAMP = 0,
    RENDER_MODE_AUDIO_MASTER,
    RENDER_MODE_AUDIO_STC_MASTER,
} render_mode_t;

struct stream_app_type_cfg {
    int sample_rate;
    uint32_t bit_width;
    int app_type;
};

struct stream_config {
    unsigned int sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    audio_devices_t devices;
    unsigned int bit_width;
};
struct stream_inout {
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by playback thread */
    pthread_cond_t  cond;
    struct stream_config in_config;
    struct stream_config out_config;
    struct stream_app_type_cfg out_app_type_cfg;
    char profile[MAX_STREAM_PROFILE_STR_LEN];
    struct audio_device *dev;
    void *adsp_hdlr_stream_handle;
    void *ip_hdlr_handle;
    stream_callback_t client_callback;
    void *client_cookie;
};
struct stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by playback thread */
    pthread_mutex_t compr_mute_lock; /* acquire before setting compress volume */
    pthread_mutex_t position_query_lock; /* acquire before updating/getting position of track offload*/
    pthread_cond_t  cond;
    struct pcm_config config;
    struct compr_config compr_config;
    struct pcm *pcm;
    struct compress *compr;
    int standby;
    int pcm_device_id;
    unsigned int sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    audio_devices_t devices;
    audio_output_flags_t flags;
    char profile[MAX_STREAM_PROFILE_STR_LEN];
    audio_usecase_t usecase;
    /* Array of supported channel mask configurations. +1 so that the last entry is always 0 */
    audio_channel_mask_t supported_channel_masks[MAX_SUPPORTED_CHANNEL_MASKS + 1];
    audio_format_t supported_formats[MAX_SUPPORTED_FORMATS+1];
    uint32_t supported_sample_rates[MAX_SUPPORTED_SAMPLE_RATES+1];
    bool muted;
    uint64_t written; /* total frames written, not cleared when entering standby */
    audio_io_handle_t handle;
    struct stream_app_type_cfg app_type_cfg;

    int non_blocking;
    int playback_started;
    int offload_state;
    pthread_cond_t offload_cond;
    pthread_t offload_thread;
    struct listnode offload_cmd_list;
    bool offload_thread_blocked;
    struct timespec writeAt;

    void *adsp_hdlr_stream_handle;
    void *ip_hdlr_handle;

    stream_callback_t client_callback;
    void *client_cookie;
    struct compr_gapless_mdata gapless_mdata;
    int send_new_metadata;
    bool send_next_track_params;
    bool is_compr_metadata_avail;
    unsigned int bit_width;
    uint32_t hal_fragment_size;
    audio_format_t hal_ip_format;
    audio_format_t hal_op_format;
    void *convert_buffer;

    bool realtime;
    int af_period_multiplier;
    struct audio_device *dev;
    card_status_t card_status;

    void* qaf_stream_handle;
    pthread_cond_t qaf_offload_cond;
    pthread_t qaf_offload_thread;
    struct listnode qaf_offload_cmd_list;
    uint32_t platform_latency;
    render_mode_t render_mode;
    bool drift_correction_enabled;

    struct audio_out_channel_map_param channel_map_param; /* input channel map */
    audio_offload_info_t info;
    int started;
    qahwi_stream_out_t qahwi_out;

    bool is_iec61937_info_available;
    bool a2dp_compress_mute;
    float volume_l;
    float volume_r;

    char pm_qos_mixer_path[MAX_MIXER_PATH_LEN];
    int hal_output_suspend_supported;
    int dynamic_pm_qos_config_supported;
    bool stream_config_changed;
    mix_matrix_params_t pan_scale_params;
    mix_matrix_params_t downmix_params;
    bool set_dual_mono;
};

struct stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by playback thread */
    struct pcm_config config;
    struct pcm *pcm;
    int standby;
    int source;
    int pcm_device_id;
    audio_devices_t device;
    audio_channel_mask_t channel_mask;
    audio_usecase_t usecase;
    bool enable_aec;
    bool enable_ns;
    audio_format_t format;
    audio_io_handle_t capture_handle;
    audio_input_flags_t flags;
    char profile[MAX_STREAM_PROFILE_STR_LEN];
    bool is_st_session;
    bool is_st_session_active;
    unsigned int sample_rate;
    unsigned int bit_width;
    bool realtime;
    int af_period_multiplier;
    struct stream_app_type_cfg app_type_cfg;
    void *cin_extn;
    qahwi_stream_in_t qahwi_in;

    struct audio_device *dev;
    card_status_t card_status;
    int capture_started;

    /* Array of supported channel mask configurations. +1 so that the last entry is always 0 */
    audio_channel_mask_t supported_channel_masks[MAX_SUPPORTED_CHANNEL_MASKS + 1];
    audio_format_t supported_formats[MAX_SUPPORTED_FORMATS + 1];
    uint32_t supported_sample_rates[MAX_SUPPORTED_SAMPLE_RATES + 1];
};

typedef enum {
    PCM_PLAYBACK,
    PCM_CAPTURE,
    VOICE_CALL,
    VOIP_CALL,
    PCM_HFP_CALL,
    TRANSCODE_LOOPBACK
} usecase_type_t;

union stream_ptr {
    struct stream_in *in;
    struct stream_out *out;
    struct stream_inout *inout;
};

struct audio_usecase {
    struct listnode list;
    audio_usecase_t id;
    usecase_type_t  type;
    audio_devices_t devices;
    snd_device_t out_snd_device;
    snd_device_t in_snd_device;
    struct stream_app_type_cfg out_app_type_cfg;
    struct stream_app_type_cfg in_app_type_cfg;
    union stream_ptr stream;
};

struct stream_format {
    struct listnode list;
    audio_format_t format;
};

struct stream_sample_rate {
    struct listnode list;
    uint32_t sample_rate;
};

typedef union {
    audio_output_flags_t out_flags;
    audio_input_flags_t in_flags;
} audio_io_flags_t;

struct streams_io_cfg {
    struct listnode list;
    audio_io_flags_t flags;
    char profile[MAX_STREAM_PROFILE_STR_LEN];
    struct listnode format_list;
    struct listnode sample_rate_list;
    struct stream_app_type_cfg app_type_cfg;
};

typedef void* (*adm_init_t)();
typedef void (*adm_deinit_t)(void *);
typedef void (*adm_register_output_stream_t)(void *, audio_io_handle_t, audio_output_flags_t);
typedef void (*adm_register_input_stream_t)(void *, audio_io_handle_t, audio_input_flags_t);
typedef void (*adm_deregister_stream_t)(void *, audio_io_handle_t);
typedef void (*adm_request_focus_t)(void *, audio_io_handle_t);
typedef void (*adm_abandon_focus_t)(void *, audio_io_handle_t);
typedef void (*adm_set_config_t)(void *, audio_io_handle_t,
                                         struct pcm *,
                                         struct pcm_config *);
typedef void (*adm_request_focus_v2_t)(void *, audio_io_handle_t, long);
typedef bool (*adm_is_noirq_avail_t)(void *, int, int, int);
typedef void (*adm_on_routing_change_t)(void *, audio_io_handle_t);

struct audio_device {
    struct audio_hw_device device;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    struct mixer *mixer;
    audio_mode_t mode;
    audio_devices_t out_device;
    struct stream_in *active_input;
    struct stream_out *primary_output;
    struct stream_out *voice_tx_output;
    struct stream_out *current_call_output;
    bool bluetooth_nrec;
    bool screen_off;
    int *snd_dev_ref_cnt;
    struct listnode usecase_list;
    struct listnode streams_output_cfg_list;
    struct listnode streams_input_cfg_list;
    struct audio_route *audio_route;
    int acdb_settings;
    bool speaker_lr_swap;
    struct voice voice;
    unsigned int cur_hdmi_channels;
    audio_format_t cur_hdmi_format;
    unsigned int cur_hdmi_sample_rate;
    unsigned int cur_hdmi_bit_width;
    unsigned int cur_wfd_channels;
    bool bt_wb_speech_enabled;
    bool allow_afe_proxy_usage;
    bool is_charging; // from battery listener
    bool mic_break_enabled;

    int snd_card;
    card_status_t card_status;
    unsigned int cur_codec_backend_samplerate;
    unsigned int cur_codec_backend_bit_width;
    bool is_channel_status_set;
    void *platform;
    unsigned int offload_usecases_state;
    void *visualizer_lib;
    int (*visualizer_start_output)(audio_io_handle_t, int);
    int (*visualizer_stop_output)(audio_io_handle_t, int);
    void *offload_effects_lib;
    int (*offload_effects_start_output)(audio_io_handle_t, int, struct mixer *);
    int (*offload_effects_stop_output)(audio_io_handle_t, int);

    int (*offload_effects_set_hpx_state)(bool);

    void *adm_data;
    void *adm_lib;
    adm_init_t adm_init;
    adm_deinit_t adm_deinit;
    adm_register_input_stream_t adm_register_input_stream;
    adm_register_output_stream_t adm_register_output_stream;
    adm_deregister_stream_t adm_deregister_stream;
    adm_request_focus_t adm_request_focus;
    adm_abandon_focus_t adm_abandon_focus;
    adm_set_config_t adm_set_config;
    adm_request_focus_v2_t adm_request_focus_v2;
    adm_is_noirq_avail_t adm_is_noirq_avail;
    adm_on_routing_change_t adm_on_routing_change;

    void (*offload_effects_get_parameters)(struct str_parms *,
                                           struct str_parms *);
    void (*offload_effects_set_parameters)(struct str_parms *);

    bool multi_offload_enable;
    int perf_lock_handle;
    int perf_lock_opts[MAX_PERF_LOCK_OPTS];
    int perf_lock_opts_size;
    bool native_playback_enabled;
    bool asrc_mode_enabled;
    qahwi_device_t qahwi_dev;
    bool vr_audio_mode_enabled;
    uint32_t dsp_bit_width_enforce_mode;
    bool bt_sco_on;
    struct audio_device_config_param *device_cfg_params;
    unsigned int interactive_usecase_state;
    bool dp_allowed_for_voice;
    void *ext_hw_plugin;
    amplifier_device_t *amp;
};

int select_devices(struct audio_device *adev,
                          audio_usecase_t uc_id);
int disable_audio_route(struct audio_device *adev,
                        struct audio_usecase *usecase);
int disable_snd_device(struct audio_device *adev,
                       snd_device_t snd_device);
int enable_snd_device(struct audio_device *adev,
                      snd_device_t snd_device);

int enable_audio_route(struct audio_device *adev,
                       struct audio_usecase *usecase);

struct audio_usecase *get_usecase_from_list(const struct audio_device *adev,
                                                   audio_usecase_t uc_id);

struct stream_in *get_next_active_input(const struct audio_device *adev);

bool is_offload_usecase(audio_usecase_t uc_id);

bool audio_is_true_native_stream_active(struct audio_device *adev);

bool audio_is_dsd_native_stream_active(struct audio_device *adev);

uint32_t adev_get_dsp_bit_width_enforce_mode();

int pcm_ioctl(struct pcm *pcm, int request, ...);

audio_usecase_t get_usecase_id_from_usecase_type(const struct audio_device *adev,
                                                 usecase_type_t type);

int check_a2dp_restore(struct audio_device *adev, struct stream_out *out, bool restore);

int adev_open_output_stream(struct audio_hw_device *dev,
                            audio_io_handle_t handle,
                            audio_devices_t devices,
                            audio_output_flags_t flags,
                            struct audio_config *config,
                            struct audio_stream_out **stream_out,
                            const char *address __unused);
void adev_close_output_stream(struct audio_hw_device *dev __unused,
                              struct audio_stream_out *stream);

bool is_interactive_usecase(audio_usecase_t uc_id);

#define LITERAL_TO_STRING(x) #x
#define CHECK(condition) LOG_ALWAYS_FATAL_IF(!(condition), "%s",\
            __FILE__ ":" LITERAL_TO_STRING(__LINE__)\
            " ASSERT_FATAL(" #condition ") failed.")

/*
 * NOTE: when multiple mutexes have to be acquired, always take the
 * stream_in or stream_out mutex first, followed by the audio_device mutex.
 */

#endif // QCOM_AUDIO_HW_H
