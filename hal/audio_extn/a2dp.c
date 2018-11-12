/*
* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#define LOG_TAG "split_a2dp"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0
#include <errno.h>
#include <cutils/log.h>
#include <dlfcn.h>
#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include "audio_extn.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <cutils/properties.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_A2DP
#include <log_utils.h>
#endif

#ifdef SPLIT_A2DP_ENABLED
#define AUDIO_PARAMETER_A2DP_STARTED "A2dpStarted"
#define BT_IPC_SOURCE_LIB_NAME  "libbthost_if.so"
#define BT_IPC_SINK_LIB_NAME    "libbthost_if_sink.so"
#define MEDIA_FMT_NONE                                     0
#define MEDIA_FMT_AAC                                      0x00010DA6
#define MEDIA_FMT_APTX                                     0x000131ff
#define MEDIA_FMT_APTX_HD                                  0x00013200
#define MEDIA_FMT_SBC                                      0x00010BF2
#define MEDIA_FMT_CELT                                     0x00013221
#define MEDIA_FMT_LDAC                                     0x00013224
#define MEDIA_FMT_MP3                                      0x00010BE9
#define MEDIA_FMT_APTX_ADAPTIVE                            0x00013204
#define MEDIA_FMT_AAC_AOT_LC                               2
#define MEDIA_FMT_AAC_AOT_SBR                              5
#define MEDIA_FMT_AAC_AOT_PS                               29
#define PCM_CHANNEL_L                                      1
#define PCM_CHANNEL_R                                      2
#define PCM_CHANNEL_C                                      3
#define MEDIA_FMT_SBC_CHANNEL_MODE_MONO                    1
#define MEDIA_FMT_SBC_CHANNEL_MODE_STEREO                  2
#define MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO               8
#define MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO            9
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS           0
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR                1
#define MIXER_ENC_CONFIG_BLOCK     "SLIM_7_RX Encoder Config"
#define MIXER_DEC_CONFIG_BLOCK     "SLIM_9_TX Decoder Config"
#define MIXER_ENC_BIT_FORMAT       "AFE Input Bit Format"
#define MIXER_DEC_BIT_FORMAT       "AFE Output Bit Format"
#define MIXER_SCRAMBLER_MODE       "AFE Scrambler Mode"
#define MIXER_SAMPLE_RATE_SINK     "BT_TX SampleRate"
#define MIXER_SAMPLE_RATE_SOURCE   "BT SampleRate"
#define MIXER_AFE_IN_CHANNELS      "AFE Input Channels"
#define MIXER_AFE_SINK_CHANNELS    "AFE Output Channels"
#define MIXER_ENC_FMT_SBC          "SBC"
#define MIXER_ENC_FMT_AAC          "AAC"
#define MIXER_ENC_FMT_APTX         "APTX"
#define MIXER_ENC_FMT_APTXHD       "APTXHD"
#define MIXER_ENC_FMT_NONE         "NONE"
#define ENCODER_LATENCY_SBC        10
#define ENCODER_LATENCY_APTX       40
#define ENCODER_LATENCY_APTX_HD    20
#define ENCODER_LATENCY_AAC        70
//To Do: Fine Tune Encoder CELT/LDAC latency.
#define ENCODER_LATENCY_CELT       40
#define ENCODER_LATENCY_LDAC       40
#define DEFAULT_SINK_LATENCY_SBC       140
#define DEFAULT_SINK_LATENCY_APTX      160
#define DEFAULT_SINK_LATENCY_APTX_HD   180
#define DEFAULT_SINK_LATENCY_AAC       180
//To Do: Fine Tune Default CELT/LDAC Latency.
#define DEFAULT_SINK_LATENCY_CELT      180
#define DEFAULT_SINK_LATENCY_LDAC      180

#define SOURCE 0
#define SINK   1

/*
 * Below enum values are extended from audio_base.h to
 * to keep encoder and decoder type local to bthost_ipc
 * and audio_hal as these are intended only for handshake
 * between IPC lib and Audio HAL.
 */
typedef enum {
    CODEC_TYPE_INVALID = AUDIO_FORMAT_INVALID, // 0xFFFFFFFFUL
    CODEC_TYPE_AAC = AUDIO_FORMAT_AAC, // 0x04000000UL
    CODEC_TYPE_SBC = AUDIO_FORMAT_SBC, // 0x1F000000UL
    CODEC_TYPE_APTX = AUDIO_FORMAT_APTX, // 0x20000000UL
    CODEC_TYPE_APTX_HD = AUDIO_FORMAT_APTX_HD, // 0x21000000UL
#ifndef LINUX_ENABLED
    CODEC_TYPE_APTX_DUAL_MONO = 570425344u, // 0x22000000UL
#endif
    CODEC_TYPE_LDAC = AUDIO_FORMAT_LDAC, // 0x23000000UL
    CODEC_TYPE_CELT = 603979776u, // 0x24000000UL
}codec_t;

typedef int (*audio_source_open_t)(void);
typedef int (*audio_source_close_t)(void);
typedef int (*audio_source_start_t)(void);
typedef int (*audio_source_stop_t)(void);
typedef int (*audio_source_suspend_t)(void);
typedef void (*audio_source_handoff_triggered_t)(void);
typedef void (*clear_source_a2dpsuspend_flag_t)(void);
typedef void * (*audio_get_enc_config_t)(uint8_t *multicast_status,
                                uint8_t *num_dev, codec_t *codec_type);
typedef int (*audio_source_check_a2dp_ready_t)(void);
typedef int (*audio_is_source_scrambling_enabled_t)(void);
typedef int (*audio_sink_start_t)(void);
typedef int (*audio_sink_stop_t)(void);
typedef void * (*audio_get_dec_config_t)(codec_t *codec_type);
typedef void * (*audio_sink_session_setup_complete_t)(uint64_t system_latency);
typedef int (*audio_sink_check_a2dp_ready_t)(void);
typedef uint16_t (*audio_sink_get_a2dp_latency_t)(void);

enum A2DP_STATE {
    A2DP_STATE_CONNECTED,
    A2DP_STATE_STARTED,
    A2DP_STATE_STOPPED,
    A2DP_STATE_DISCONNECTED,
};

/* structure used to  update a2dp state machine
 * to communicate IPC library
 * to store DSP encoder configuration information
 */
struct a2dp_data {
    struct audio_device *adev;
    void *bt_lib_source_handle;
    audio_source_open_t audio_source_open;
    audio_source_close_t audio_source_close;
    audio_source_start_t audio_source_start;
    audio_source_stop_t audio_source_stop;
    audio_source_suspend_t audio_source_suspend;
    audio_source_handoff_triggered_t audio_source_handoff_triggered;
    clear_source_a2dpsuspend_flag_t clear_source_a2dpsuspend_flag;
    audio_get_enc_config_t audio_get_enc_config;
    audio_source_check_a2dp_ready_t audio_source_check_a2dp_ready;
    audio_is_source_scrambling_enabled_t audio_is_source_scrambling_enabled;
    enum A2DP_STATE bt_state_source;
    codec_t bt_encoder_format;
    uint32_t enc_sampling_rate;
    uint32_t enc_channels;
    bool a2dp_source_started;
    bool a2dp_source_suspended;
    int  a2dp_source_total_active_session_requests;
    bool is_a2dp_offload_supported;
    bool is_handoff_in_progress;
    bool is_aptx_dual_mono_supported;

    void *bt_lib_sink_handle;
    audio_sink_start_t audio_sink_start;
    audio_sink_stop_t audio_sink_stop;
    audio_get_dec_config_t audio_get_dec_config;
    audio_sink_session_setup_complete_t audio_sink_session_setup_complete;
    audio_sink_check_a2dp_ready_t audio_sink_check_a2dp_ready;
    audio_sink_get_a2dp_latency_t audio_sink_get_a2dp_latency;
    enum A2DP_STATE bt_state_sink;
    codec_t bt_decoder_format;
    uint32_t dec_sampling_rate;
    uint32_t dec_channels;
    bool a2dp_sink_started;
    int  a2dp_sink_total_active_session_requests;
};

struct a2dp_data a2dp;

/* START of DSP configurable structures
 * These values should match with DSP interface defintion
 */

/* AAC encoder configuration structure. */
typedef struct aac_enc_cfg_t aac_enc_cfg_t;

/* supported enc_mode are AAC_LC, AAC_SBR, AAC_PS
 * supported aac_fmt_flag are ADTS/RAW
 * supported channel_cfg are Native mode, Mono , Stereo
 */
struct aac_enc_cfg_t {
    uint32_t      enc_format;
    uint32_t      bit_rate;
    uint32_t      enc_mode;
    uint16_t      aac_fmt_flag;
    uint16_t      channel_cfg;
    uint32_t      sample_rate;
} __attribute__ ((packed));

typedef struct audio_aac_decoder_config_t audio_aac_decoder_config_t;
struct audio_aac_decoder_config_t {
    uint16_t      aac_fmt_flag; /* LATM*/
    uint16_t      audio_object_type; /* LC */
    uint16_t      channels; /* Stereo */
    uint16_t      total_size_of_pce_bits; /* 0 - only for channel conf PCE */
    uint32_t      sampling_rate; /* 8k, 11.025k, 12k, 16k, 22.05k, 24k, 32k,
                                  44.1k, 48k, 64k, 88.2k, 96k */
} __attribute__ ((packed));

typedef struct audio_sbc_decoder_config_t audio_sbc_decoder_config_t;
struct audio_sbc_decoder_config_t {
    uint16_t      channels; /* Mono, Stereo */
    uint32_t      sampling_rate; /* 8k, 11.025k, 12k, 16k, 22.05k, 24k, 32k,
                                  44.1k, 48k, 64k, 88.2k, 96k */
} __attribute__ ((packed));

/* AAC decoder configuration structure. */
typedef struct aac_dec_cfg_t aac_dec_cfg_t;
struct aac_dec_cfg_t {
    uint32_t dec_format;
    audio_aac_decoder_config_t data;
} __attribute__ ((packed));

/* SBC decoder configuration structure. */
typedef struct sbc_dec_cfg_t sbc_dec_cfg_t;
struct sbc_dec_cfg_t {
    uint32_t dec_format;
    audio_sbc_decoder_config_t data;
} __attribute__ ((packed));

/* SBC encoder configuration structure. */
typedef struct sbc_enc_cfg_t sbc_enc_cfg_t;

/* supported num_subbands are 4/8
 * supported blk_len are 4, 8, 12, 16
 * supported channel_mode are MONO, STEREO, DUAL_MONO, JOINT_STEREO
 * supported alloc_method are LOUNDNESS/SNR
 * supported bit_rate for mono channel is max 320kbps
 * supported bit rate for stereo channel is max 512 kbps
 */
struct sbc_enc_cfg_t{
    uint32_t      enc_format;
    uint32_t      num_subbands;
    uint32_t      blk_len;
    uint32_t      channel_mode;
    uint32_t      alloc_method;
    uint32_t      bit_rate;
    uint32_t      sample_rate;
} __attribute__ ((packed));


/* supported num_channels are Mono/Stereo
 * supported channel_mapping for mono is CHANNEL_C
 * supported channel mapping for stereo is CHANNEL_L and CHANNEL_R
 * custom size and reserved are not used(for future enhancement)
 */
struct custom_enc_cfg_t
{
    uint32_t      enc_format;
    uint32_t      sample_rate;
    uint16_t      num_channels;
    uint16_t      reserved;
    uint8_t       channel_mapping[8];
    uint32_t      custom_size;
} __attribute__ ((packed));

struct celt_specific_enc_cfg_t
{
    uint32_t      bit_rate;
    uint16_t      frame_size;
    uint16_t      complexity;
    uint16_t      prediction_mode;
    uint16_t      vbr_flag;
} __attribute__ ((packed));

struct celt_enc_cfg_t
{
    struct custom_enc_cfg_t  custom_cfg;
    struct celt_specific_enc_cfg_t celt_cfg;
} __attribute__ ((packed));

/* sync_mode introduced with APTX V2 libraries
 * sync mode: 0x0 = stereo sync mode
 *            0x01 = dual mono sync mode
 *            0x02 = dual mono with no sync on either L or R codewords
 */
struct aptx_v2_enc_cfg_ext_t
{
    uint32_t       sync_mode;
} __attribute__ ((packed));

/* APTX struct for combining custom enc and V2 fields */
struct aptx_enc_cfg_t
{
    struct custom_enc_cfg_t  custom_cfg;
    struct aptx_v2_enc_cfg_ext_t aptx_v2_cfg;
} __attribute__ ((packed));

struct ldac_specific_enc_cfg_t
{
    uint32_t      bit_rate;
    uint16_t      channel_mode;
    uint16_t      mtu;
} __attribute__ ((packed));

struct ldac_enc_cfg_t
{
    struct custom_enc_cfg_t  custom_cfg;
    struct ldac_specific_enc_cfg_t ldac_cfg;
} __attribute__ ((packed));

/* In LE BT source code uses system/audio.h for below
 * structure definition. To avoid multiple definition
 * compilation error for audiohal in LE , masking structure
 * definition under "LINUX_ENABLED" which is defined only
 * in LE
 */
#ifndef LINUX_ENABLED
/* TODO: Define the following structures only for O using PLATFORM_VERSION */
/* Information about BT SBC encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t subband;        /* 4, 8 */
    uint32_t blk_len;        /* 4, 8, 12, 16 */
    uint16_t sampling_rate;  /*44.1khz,48khz*/
    uint8_t  channels;       /*0(Mono),1(Dual_mono),2(Stereo),3(JS)*/
    uint8_t  alloc;          /*0(Loudness),1(SNR)*/
    uint8_t  min_bitpool;    /* 2 */
    uint8_t  max_bitpool;    /*53(44.1khz),51 (48khz) */
    uint32_t bitrate;        /* 320kbps to 512kbps */
} audio_sbc_encoder_config;

/* Information about BT APTX encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint16_t sampling_rate;
    uint8_t  channels;
    uint32_t bitrate;
} audio_aptx_default_config;

typedef struct {
    uint16_t sampling_rate;
    uint8_t  channels;
    uint32_t bitrate;
    uint32_t sync_mode;
} audio_aptx_dual_mono_config;

typedef union {
    audio_aptx_default_config *default_cfg;
    audio_aptx_dual_mono_config *dual_mono_cfg;
} audio_aptx_encoder_config;

/* Information about BT AAC encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t enc_mode; /* LC, SBR, PS */
    uint16_t format_flag; /* RAW, ADTS */
    uint16_t channels; /* 1-Mono, 2-Stereo */
    uint32_t sampling_rate;
    uint32_t bitrate;
} audio_aac_encoder_config;
#endif

/* Information about BT CELT encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t sampling_rate; /* 32000 - 48000, 48000 */
    uint16_t channels; /* 1-Mono, 2-Stereo, 2*/
    uint16_t frame_size; /* 64-128-256-512, 512 */
    uint16_t complexity; /* 0-10, 1 */
    uint16_t prediction_mode; /* 0-1-2, 0 */
    uint16_t vbr_flag; /* 0-1, 0*/
    uint32_t bitrate; /*32000 - 1536000, 139500*/
} audio_celt_encoder_config;

/* Information about BT LDAC encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t sampling_rate; /*44100,48000,88200,96000*/
    uint32_t bit_rate; /*303000,606000,909000(in bits per second)*/
    uint16_t channel_mode; /* 0, 4, 2, 1*/
    uint16_t mtu; /*679*/
} audio_ldac_encoder_config;

/* Information about BT AAC decoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP decoder
 */
typedef struct {
    uint16_t      aac_fmt_flag; /* LATM*/
    uint16_t      audio_object_type; /* LC */
    uint16_t      channels; /* Stereo */
    uint16_t      total_size_of_pce_bits; /* 0 - only for channel conf PCE */
    uint32_t      sampling_rate; /* 8k, 11.025k, 12k, 16k, 22.05k, 24k, 32k,
                                  44.1k, 48k, 64k, 88.2k, 96k */
} audio_aac_dec_config_t;

/* Information about BT SBC decoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP decoder
 */
typedef struct {
    uint16_t      channels; /* Mono, Stereo */
    uint32_t      sampling_rate; /* 8k, 11.025k, 12k, 16k, 22.05k, 24k, 32k,
                                  44.1k, 48k, 64k, 88.2k, 96k */
}audio_sbc_dec_config_t;

/*********** END of DSP configurable structures ********************/

/* API to identify DSP encoder captabilities */
static void a2dp_offload_codec_cap_parser(char *value)
{
    char *tok = NULL,*saveptr;

    tok = strtok_r(value, "-", &saveptr);
    while (tok != NULL) {
        if (strcmp(tok, "sbc") == 0) {
            ALOGD("%s: SBC offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        } else if (strcmp(tok, "aptx") == 0) {
            ALOGD("%s: aptx offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        } else if (strcmp(tok, "aptxtws") == 0) {
            ALOGD("%s: aptx dual mono offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        } else if (strcmp(tok, "aptxhd") == 0) {
            ALOGD("%s: aptx HD offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        } else if (strcmp(tok, "aac") == 0) {
            ALOGD("%s: aac offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        } else if (strcmp(tok, "celt") == 0) {
            ALOGD("%s: celt offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        } else if (strcmp(tok, "ldac") == 0) {
            ALOGD("%s: ldac offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        }
        tok = strtok_r(NULL, "-", &saveptr);
    };
}

static void update_offload_codec_capabilities()
{
    char value[PROPERTY_VALUE_MAX] = {'\0'};

    property_get("persist.vendor.bt.a2dp_offload_cap", value, "false");
    ALOGD("get_offload_codec_capabilities = %s",value);
    a2dp.is_a2dp_offload_supported =
            property_get_bool("persist.vendor.bt.a2dp_offload_cap", false);
    if (strcmp(value, "false") != 0)
        a2dp_offload_codec_cap_parser(value);
    ALOGD("%s: codec cap = %s",__func__,value);
}

/* API to open BT IPC library to start IPC communication for BT Source*/
static void open_a2dp_source()
{
    int ret = 0;

    ALOGD(" Open A2DP source start ");
    if (a2dp.bt_lib_source_handle == NULL){
        ALOGD(" Requesting for BT lib handle");
        a2dp.bt_lib_source_handle = dlopen(BT_IPC_SOURCE_LIB_NAME, RTLD_NOW);

        if (a2dp.bt_lib_source_handle == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__, BT_IPC_SOURCE_LIB_NAME);
            ret = -ENOSYS;
            goto init_fail;
        } else {
            a2dp.audio_source_open = (audio_source_open_t)
                          dlsym(a2dp.bt_lib_source_handle, "audio_stream_open");
            a2dp.audio_source_start = (audio_source_start_t)
                          dlsym(a2dp.bt_lib_source_handle, "audio_start_stream");
            a2dp.audio_get_enc_config = (audio_get_enc_config_t)
                          dlsym(a2dp.bt_lib_source_handle, "audio_get_codec_config");
            a2dp.audio_source_suspend = (audio_source_suspend_t)
                          dlsym(a2dp.bt_lib_source_handle, "audio_suspend_stream");
            a2dp.audio_source_handoff_triggered = (audio_source_handoff_triggered_t)
                          dlsym(a2dp.bt_lib_source_handle, "audio_handoff_triggered");
            a2dp.clear_source_a2dpsuspend_flag = (clear_source_a2dpsuspend_flag_t)
                          dlsym(a2dp.bt_lib_source_handle, "clear_a2dpsuspend_flag");
            a2dp.audio_source_stop = (audio_source_stop_t)
                          dlsym(a2dp.bt_lib_source_handle, "audio_stop_stream");
            a2dp.audio_source_close = (audio_source_close_t)
                          dlsym(a2dp.bt_lib_source_handle, "audio_stream_close");
            a2dp.audio_source_check_a2dp_ready = (audio_source_check_a2dp_ready_t)
                        dlsym(a2dp.bt_lib_source_handle,"audio_check_a2dp_ready");
            a2dp.audio_sink_get_a2dp_latency = (audio_sink_get_a2dp_latency_t)
                        dlsym(a2dp.bt_lib_source_handle,"audio_sink_get_a2dp_latency");
            a2dp.audio_is_source_scrambling_enabled = (audio_is_source_scrambling_enabled_t)
                        dlsym(a2dp.bt_lib_source_handle,"audio_is_scrambling_enabled");
        }
    }

    if (a2dp.bt_lib_source_handle && a2dp.audio_source_open) {
        if (a2dp.bt_state_source == A2DP_STATE_DISCONNECTED) {
            ALOGD("calling BT stream open");
            ret = a2dp.audio_source_open();
            if(ret != 0) {
                ALOGE("Failed to open source stream for a2dp: status %d", ret);
                goto init_fail;
            }
            a2dp.bt_state_source = A2DP_STATE_CONNECTED;
        } else {
            ALOGD("Called a2dp open with improper state, Ignoring request state %d", a2dp.bt_state_source);
        }
    } else {
        ALOGE("a2dp handle is not identified, Ignoring open request");
        a2dp.bt_state_source = A2DP_STATE_DISCONNECTED;
        goto init_fail;
    }

init_fail:
    if(ret != 0 && (a2dp.bt_lib_source_handle != NULL)) {
        dlclose(a2dp.bt_lib_source_handle);
        a2dp.bt_lib_source_handle = NULL;
    }
}

/* API to open BT IPC library to start IPC communication for BT Sink*/
static void open_a2dp_sink()
{
    ALOGD(" Open A2DP input start ");
    if (a2dp.bt_lib_sink_handle == NULL){
        ALOGD(" Requesting for BT lib handle");
        a2dp.bt_lib_sink_handle = dlopen(BT_IPC_SINK_LIB_NAME, RTLD_NOW);

        if (a2dp.bt_lib_sink_handle == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__, BT_IPC_SINK_LIB_NAME);
        } else {
            a2dp.audio_sink_start = (audio_sink_start_t)
                          dlsym(a2dp.bt_lib_sink_handle, "audio_sink_start_capture");
            a2dp.audio_get_dec_config = (audio_get_dec_config_t)
                          dlsym(a2dp.bt_lib_sink_handle, "audio_get_decoder_config");
            a2dp.audio_sink_stop = (audio_sink_stop_t)
                          dlsym(a2dp.bt_lib_sink_handle, "audio_sink_stop_capture");
            a2dp.audio_sink_check_a2dp_ready = (audio_sink_check_a2dp_ready_t)
                        dlsym(a2dp.bt_lib_sink_handle,"audio_sink_check_a2dp_ready");
            a2dp.audio_sink_session_setup_complete = (audio_sink_session_setup_complete_t)
                          dlsym(a2dp.bt_lib_sink_handle, "audio_sink_session_setup_complete");
        }
    }
}

static int close_a2dp_output()
{
    ALOGV("%s\n",__func__);

    if (!(a2dp.bt_lib_source_handle && a2dp.audio_source_close)) {
        ALOGE("a2dp source handle is not identified, Ignoring close request");
        return -ENOSYS;
    }

    if (a2dp.bt_state_source != A2DP_STATE_DISCONNECTED) {
        ALOGD("calling BT source stream close");
        if(a2dp.audio_source_close() == false)
            ALOGE("failed close a2dp source control path from BT library");
    }
    a2dp.a2dp_source_started = false;
    a2dp.a2dp_source_total_active_session_requests = 0;
    a2dp.a2dp_source_suspended = false;
    a2dp.bt_encoder_format = CODEC_TYPE_INVALID;
    a2dp.enc_sampling_rate = 48000;
    a2dp.enc_channels = 2;
    a2dp.bt_state_source = A2DP_STATE_DISCONNECTED;

    return 0;
}

static int close_a2dp_input()
{
    ALOGV("%s\n",__func__);

    if (!(a2dp.bt_lib_sink_handle && a2dp.audio_source_close)) {
        ALOGE("a2dp sink handle is not identified, Ignoring close request");
        return -ENOSYS;
    }

    if (a2dp.bt_state_sink != A2DP_STATE_DISCONNECTED) {
        ALOGD("calling BT sink stream close");
        if(a2dp.audio_source_close() == false)
            ALOGE("failed close a2dp sink control path from BT library");
    }
    a2dp.a2dp_sink_started = false;
    a2dp.a2dp_sink_total_active_session_requests = 0;
    a2dp.bt_decoder_format = CODEC_TYPE_INVALID;
    a2dp.dec_sampling_rate = 48000;
    a2dp.dec_channels = 2;
    a2dp.bt_state_sink = A2DP_STATE_DISCONNECTED;

    return 0;
}

static void a2dp_check_and_set_scrambler()
{
    bool scrambler_mode = false;
    struct mixer_ctl *ctrl_scrambler_mode = NULL;
    if (a2dp.audio_is_source_scrambling_enabled && (a2dp.bt_state_source != A2DP_STATE_DISCONNECTED))
        scrambler_mode = a2dp.audio_is_source_scrambling_enabled();

    if (scrambler_mode) {
        //enable scrambler in dsp
        ctrl_scrambler_mode = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SCRAMBLER_MODE);
        if (!ctrl_scrambler_mode) {
            ALOGE(" ERROR scrambler mode mixer control not identified");
            return;
        } else {
            if (mixer_ctl_set_value(ctrl_scrambler_mode, 0, true) != 0) {
                ALOGE("%s: Could not set scrambler mode", __func__);
                return;
            }
        }
    }
}

static bool a2dp_set_backend_cfg(uint8_t direction)
{
    char *rate_str = NULL, *channels = NULL;
    uint32_t sampling_rate;
    struct mixer_ctl *ctl_sample_rate = NULL, *ctrl_channels = NULL;
    bool is_configured = false;

    if (direction == SINK) {
        sampling_rate = a2dp.dec_sampling_rate;
    } else {
        sampling_rate = a2dp.enc_sampling_rate;
    }
    //For LDAC encoder and AAC decoder open slimbus port at
    //96Khz for 48Khz input and 88.2Khz for 44.1Khz input.
    if (((a2dp.bt_encoder_format == CODEC_TYPE_LDAC) ||
         (a2dp.bt_decoder_format == CODEC_TYPE_SBC) ||
         (a2dp.bt_decoder_format == AUDIO_FORMAT_AAC)) &&
        (sampling_rate == 48000 || sampling_rate == 44100 )) {
        sampling_rate = sampling_rate *2;
    }

    //Configure backend sampling rate
    switch (sampling_rate) {
    case 44100:
        rate_str = "KHZ_44P1";
        break;
    case 48000:
        rate_str = "KHZ_48";
        break;
    case 88200:
        rate_str = "KHZ_88P2";
        break;
    case 96000:
        rate_str = "KHZ_96";
        break;
    default:
        rate_str = "KHZ_48";
        break;
    }

    if (direction == SINK) {
        ALOGD("%s: set sink backend sample rate =%s", __func__, rate_str);
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SAMPLE_RATE_SINK);
    } else {
        ALOGD("%s: set source backend sample rate =%s", __func__, rate_str);
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SAMPLE_RATE_SOURCE);
    }
    if (!ctl_sample_rate) {
        ALOGE(" ERROR: backend sample rate mixer control not identified");
    } else {
        if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
            ALOGE("%s: Failed to set backend sample rate =%s", __func__, rate_str);
            is_configured = false;
            goto fail;
        }
    }

    if (direction == SINK) {
        switch (a2dp.dec_channels) {
        case 1:
            channels = "One";
            break;
        case 2:
        default:
            channels = "Two";
            break;
        }

        ALOGD("%s: set afe dec channels =%d", __func__, channels);
        ctrl_channels = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_AFE_SINK_CHANNELS);
    } else {
        //Configure AFE enc channels
        switch (a2dp.enc_channels) {
        case 1:
            channels = "One";
            break;
        case 2:
        default:
            channels = "Two";
            break;
        }

        ALOGD("%s: set afe enc channels =%d", __func__, channels);
        ctrl_channels = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_AFE_IN_CHANNELS);
    }

    if (!ctrl_channels) {
        ALOGE(" ERROR AFE channels mixer control not identified");
    } else {
        if (mixer_ctl_set_enum_by_string(ctrl_channels, channels) != 0) {
            ALOGE("%s: Failed to set AFE channels =%d", __func__, channels);
            is_configured = false;
            goto fail;
        }
    }
    is_configured = true;
fail:
    return is_configured;
}

bool configure_aac_dec_format(audio_aac_dec_config_t *aac_bt_cfg)
{
    struct mixer_ctl *ctl_dec_data = NULL, *ctrl_bit_format = NULL;
    struct aac_dec_cfg_t aac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(aac_bt_cfg == NULL)
        return false;

    ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_DEC_CONFIG_BLOCK);
    if (!ctl_dec_data) {
        ALOGE(" ERROR  a2dp decoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }

    memset(&aac_dsp_cfg, 0x0, sizeof(struct aac_dec_cfg_t));
    aac_dsp_cfg.dec_format = MEDIA_FMT_AAC;
    aac_dsp_cfg.data.aac_fmt_flag = aac_bt_cfg->aac_fmt_flag;
    aac_dsp_cfg.data.channels = aac_bt_cfg->channels;
    switch(aac_bt_cfg->audio_object_type) {
    case 0:
        aac_dsp_cfg.data.audio_object_type = MEDIA_FMT_AAC_AOT_LC;
        break;
    case 2:
        aac_dsp_cfg.data.audio_object_type = MEDIA_FMT_AAC_AOT_PS;
        break;
    case 1:
    default:
        aac_dsp_cfg.data.audio_object_type = MEDIA_FMT_AAC_AOT_SBR;
        break;
    }
    aac_dsp_cfg.data.total_size_of_pce_bits = aac_bt_cfg->total_size_of_pce_bits;
    aac_dsp_cfg.data.sampling_rate = aac_bt_cfg->sampling_rate;
    ret = mixer_ctl_set_array(ctl_dec_data, (void *)&aac_dsp_cfg,
                              sizeof(struct aac_dec_cfg_t));
    if (ret != 0) {
        ALOGE("%s: failed to set AAC decoder config", __func__);
        is_configured = false;
        goto fail;
    }

    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_DEC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR Dec bit format mixer control not identified");
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set bit format to decoder", __func__);
        is_configured = false;
        goto fail;
    }

    is_configured = true;
    a2dp.bt_decoder_format = CODEC_TYPE_AAC;
    a2dp.dec_channels = aac_dsp_cfg.data.channels;
    a2dp.dec_sampling_rate = aac_dsp_cfg.data.sampling_rate;
    ALOGV("Successfully updated AAC dec format with sampling_rate: %d channels:%d",
           aac_dsp_cfg.data.sampling_rate, aac_dsp_cfg.data.channels);
fail:
    return is_configured;
}

bool configure_sbc_dec_format(audio_sbc_dec_config_t *sbc_bt_cfg)
{
    struct mixer_ctl *ctl_dec_data = NULL, *ctrl_bit_format = NULL;
    struct sbc_dec_cfg_t sbc_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(sbc_bt_cfg == NULL)
        goto fail;

    ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_DEC_CONFIG_BLOCK);
    if (!ctl_dec_data) {
        ALOGE(" ERROR  a2dp decoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }

    memset(&sbc_dsp_cfg, 0x0, sizeof(struct sbc_dec_cfg_t));
    sbc_dsp_cfg.dec_format = MEDIA_FMT_SBC;
    sbc_dsp_cfg.data.channels = sbc_bt_cfg->channels;
    sbc_dsp_cfg.data.sampling_rate = sbc_bt_cfg->sampling_rate;
    ret = mixer_ctl_set_array(ctl_dec_data, (void *)&sbc_dsp_cfg,
                              sizeof(struct sbc_dec_cfg_t));

    if (ret != 0) {
        ALOGE("%s: failed to set SBC decoder config", __func__);
        is_configured = false;
        goto fail;
    }

    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_DEC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR Dec bit format mixer control not identified");
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set bit format to decoder", __func__);
        is_configured = false;
        goto fail;
    }

    is_configured = true;
    a2dp.bt_decoder_format = CODEC_TYPE_SBC;
    if (sbc_dsp_cfg.data.channels == MEDIA_FMT_SBC_CHANNEL_MODE_MONO)
        a2dp.dec_channels = 1;
    else
        a2dp.dec_channels = 2;
    a2dp.dec_sampling_rate = sbc_dsp_cfg.data.sampling_rate;
    ALOGV("Successfully updated SBC dec format");
fail:
    return is_configured;
}

static void a2dp_reset_backend_cfg(uint8_t direction)
{
    char *rate_str = "KHZ_8", *channels = "Zero";
    struct mixer_ctl *ctl_sample_rate = NULL, *ctrl_channels = NULL;

    if (direction == SINK) {
        ALOGD("%s: reset sink backend sample rate =%s", __func__, rate_str);
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                              MIXER_SAMPLE_RATE_SINK);
    } else {
        ALOGD("%s: reset source backend sample rate =%s", __func__, rate_str);
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                              MIXER_SAMPLE_RATE_SOURCE);
    }
    if (!ctl_sample_rate) {
        ALOGE(" ERROR: backend sample rate mixer control not identified");
    } else {
        if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
            ALOGE("%s: Failed to reset backend sample rate = %s", __func__, rate_str);
        }
    }

    if (direction == SINK) {
        ALOGD("%s: reset afe sink channels =%s", __func__, channels);
        ctrl_channels = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_AFE_SINK_CHANNELS);
    } else {
        ALOGD("%s: reset afe source channels =%s", __func__, channels);
        ctrl_channels = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_AFE_IN_CHANNELS);
    }
    if (!ctrl_channels) {
        ALOGE(" ERROR AFE channel mixer control not identified");
        return;
    } else {
        if (mixer_ctl_set_enum_by_string(ctrl_channels, channels) != 0) {
            ALOGE("%s: Failed to reset AFE channels", __func__);
            return;
        }
    }
}

/* API to configure AFE decoder in DSP */
static bool configure_a2dp_dsp_decoder_format()
{
    void *codec_info = NULL;
    codec_t codec_type = CODEC_TYPE_INVALID;
    bool is_configured = false;
    struct mixer_ctl *ctl_dec_data = NULL;
    int ret = 0;

    if (!a2dp.audio_get_dec_config) {
        ALOGE(" a2dp handle is not identified, ignoring a2dp decoder config");
        return false;
    }

    ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_DEC_CONFIG_BLOCK);
    if (!ctl_dec_data) {
        ALOGE(" ERROR  a2dp decoder CONFIG data mixer control not identified");
        is_configured = false;
        return false;
    }
    codec_info = a2dp.audio_get_dec_config(&codec_type);
    switch(codec_type) {
        case CODEC_TYPE_SBC:
            ALOGD(" SBC decoder supported BT device");
            is_configured = configure_sbc_dec_format((audio_sbc_dec_config_t *)codec_info);
            break;
        case CODEC_TYPE_AAC:
            ALOGD(" AAC decoder supported BT device");
            is_configured =
              configure_aac_dec_format((audio_aac_dec_config_t *)codec_info);
            break;
        default:
            ALOGD(" Received Unsupported decoder format");
            is_configured = false;
            break;
    }
    return is_configured;
}

/* API to configure SBC DSP encoder */
bool configure_sbc_enc_format(audio_sbc_encoder_config *sbc_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct sbc_enc_cfg_t sbc_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(sbc_bt_cfg == NULL)
        return false;

   ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    memset(&sbc_dsp_cfg, 0x0, sizeof(struct sbc_enc_cfg_t));
    sbc_dsp_cfg.enc_format = MEDIA_FMT_SBC;
    sbc_dsp_cfg.num_subbands = sbc_bt_cfg->subband;
    sbc_dsp_cfg.blk_len = sbc_bt_cfg->blk_len;
    switch(sbc_bt_cfg->channels) {
        case 0:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_MONO;
            break;
        case 1:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO;
            break;
        case 3:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO;
            break;
        case 2:
        default:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_STEREO;
            break;
    }
    if (sbc_bt_cfg->alloc)
        sbc_dsp_cfg.alloc_method = MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS;
    else
        sbc_dsp_cfg.alloc_method = MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR;
    sbc_dsp_cfg.bit_rate = sbc_bt_cfg->bitrate;
    sbc_dsp_cfg.sample_rate = sbc_bt_cfg->sampling_rate;
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&sbc_dsp_cfg,
                                    sizeof(struct sbc_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: failed to set SBC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR bit format CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set bit format to encoder", __func__);
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_SBC;
    a2dp.enc_sampling_rate = sbc_bt_cfg->sampling_rate;

    if (sbc_dsp_cfg.channel_mode == MEDIA_FMT_SBC_CHANNEL_MODE_MONO)
        a2dp.enc_channels = 1;
    else
        a2dp.enc_channels = 2;

    ALOGV("Successfully updated SBC enc format with samplingrate: %d channelmode:%d",
           sbc_dsp_cfg.sample_rate, sbc_dsp_cfg.channel_mode);
fail:
    return is_configured;
}

#ifndef LINUX_ENABLED
static int update_aptx_dsp_config_v2(struct aptx_enc_cfg_t *aptx_dsp_cfg,
                                     audio_aptx_encoder_config *aptx_bt_cfg)
{
    int ret = 0;

    if(aptx_dsp_cfg == NULL || aptx_bt_cfg == NULL) {
        ALOGE("Invalid param, aptx_dsp_cfg %p aptx_bt_cfg %p",
              aptx_dsp_cfg, aptx_bt_cfg);
        return -EINVAL;
    }

    memset(aptx_dsp_cfg, 0x0, sizeof(struct aptx_enc_cfg_t));
    aptx_dsp_cfg->custom_cfg.enc_format = MEDIA_FMT_APTX;

    if (!a2dp.is_aptx_dual_mono_supported) {
        aptx_dsp_cfg->custom_cfg.sample_rate = aptx_bt_cfg->default_cfg->sampling_rate;
        aptx_dsp_cfg->custom_cfg.num_channels = aptx_bt_cfg->default_cfg->channels;
    } else {
        aptx_dsp_cfg->custom_cfg.sample_rate = aptx_bt_cfg->dual_mono_cfg->sampling_rate;
        aptx_dsp_cfg->custom_cfg.num_channels = aptx_bt_cfg->dual_mono_cfg->channels;
        aptx_dsp_cfg->aptx_v2_cfg.sync_mode = aptx_bt_cfg->dual_mono_cfg->sync_mode;
    }

    switch(aptx_dsp_cfg->custom_cfg.num_channels) {
        case 1:
            aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
            aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
            aptx_dsp_cfg->custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }
    a2dp.enc_channels = aptx_dsp_cfg->custom_cfg.num_channels;
    if (!a2dp.is_aptx_dual_mono_supported) {
        a2dp.enc_sampling_rate = aptx_bt_cfg->default_cfg->sampling_rate;
        ALOGV("Successfully updated APTX enc format with samplingrate: %d \
               channels:%d", aptx_dsp_cfg->custom_cfg.sample_rate,
               aptx_dsp_cfg->custom_cfg.num_channels);
    } else {
        a2dp.enc_sampling_rate = aptx_bt_cfg->dual_mono_cfg->sampling_rate;
        ALOGV("Successfully updated APTX dual mono enc format with \
               samplingrate: %d channels:%d syncmode %d",
               aptx_dsp_cfg->custom_cfg.sample_rate,
               aptx_dsp_cfg->custom_cfg.num_channels,
               aptx_dsp_cfg->aptx_v2_cfg.sync_mode);
    }
    return ret;
}
#else
static int update_aptx_dsp_config_v1(struct custom_enc_cfg_t *aptx_dsp_cfg,
                                     audio_aptx_encoder_config *aptx_bt_cfg)
{
    int ret = 0;

    if(aptx_dsp_cfg == NULL || aptx_bt_cfg == NULL) {
        ALOGE("Invalid param, aptx_dsp_cfg %p aptx_bt_cfg %p",
              aptx_dsp_cfg, aptx_bt_cfg);
        return -EINVAL;
    }

    memset(&aptx_dsp_cfg, 0x0, sizeof(struct custom_enc_cfg_t));
    aptx_dsp_cfg->enc_format = MEDIA_FMT_APTX;
    aptx_dsp_cfg->sample_rate = aptx_bt_cfg->sampling_rate;
    aptx_dsp_cfg->num_channels = aptx_bt_cfg->channels;
    switch(aptx_dsp_cfg->num_channels) {
        case 1:
            aptx_dsp_cfg->channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
            aptx_dsp_cfg->channel_mapping[0] = PCM_CHANNEL_L;
            aptx_dsp_cfg->channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }

    ALOGV("Updated APTX enc format with samplingrate: %d channels:%d",
            aptx_dsp_cfg->sample_rate, aptx_dsp_cfg->num_channels);

    return ret;
}
#endif

/* API to configure APTX DSP encoder */
bool configure_aptx_enc_format(audio_aptx_encoder_config *aptx_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    int mixer_size;
    bool is_configured = false;
    int ret = 0;
    int sample_rate_backup;

    if(aptx_bt_cfg == NULL)
        return false;

#ifndef LINUX_ENABLED
    struct aptx_enc_cfg_t aptx_dsp_cfg;
    mixer_size = sizeof(struct aptx_enc_cfg_t);
    sample_rate_backup = aptx_bt_cfg->default_cfg->sampling_rate;
#else
    struct custom_enc_cfg_t aptx_dsp_cfg;
    mixer_size = sizeof(struct custom_enc_cfg_t);
    sample_rate_backup = aptx_bt_cfg->sampling_rate;
#endif

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }

#ifndef LINUX_ENABLED
    ret = update_aptx_dsp_config_v2(&aptx_dsp_cfg, aptx_bt_cfg);
#else
    ret = update_aptx_dsp_config_v1(&aptx_dsp_cfg, aptx_bt_cfg);
#endif

    if (ret) {
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                              mixer_size);
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE("ERROR bit format CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    } else {
        ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
        if (ret != 0) {
            ALOGE("%s: Failed to set bit format to encoder", __func__);
            is_configured = false;
            goto fail;
        }
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_APTX;
fail:
    /*restore sample rate */
    if(!is_configured)
        a2dp.enc_sampling_rate = sample_rate_backup;
    return is_configured;
}

/* API to configure APTX HD DSP encoder
 */
#ifndef LINUX_ENABLED
bool configure_aptx_hd_enc_format(audio_aptx_default_config *aptx_bt_cfg)
#else
bool configure_aptx_hd_enc_format(audio_aptx_encoder_config *aptx_bt_cfg)
#endif
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct custom_enc_cfg_t aptx_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(aptx_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }

    memset(&aptx_dsp_cfg, 0x0, sizeof(struct custom_enc_cfg_t));
    aptx_dsp_cfg.enc_format = MEDIA_FMT_APTX_HD;
    aptx_dsp_cfg.sample_rate = aptx_bt_cfg->sampling_rate;
    aptx_dsp_cfg.num_channels = aptx_bt_cfg->channels;
    switch(aptx_dsp_cfg.num_channels) {
        case 1:
            aptx_dsp_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
            aptx_dsp_cfg.channel_mapping[0] = PCM_CHANNEL_L;
            aptx_dsp_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                              sizeof(struct custom_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX HD encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR  bit format CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S24_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX HD encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_APTX_HD;
    a2dp.enc_sampling_rate = aptx_bt_cfg->sampling_rate;
    a2dp.enc_channels = aptx_bt_cfg->channels;
    ALOGV("Successfully updated APTX HD encformat with samplingrate: %d channels:%d",
           aptx_dsp_cfg.sample_rate, aptx_dsp_cfg.num_channels);
fail:
    return is_configured;
}

/* API to configure AAC DSP encoder */
bool configure_aac_enc_format(audio_aac_encoder_config *aac_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct aac_enc_cfg_t aac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(aac_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    memset(&aac_dsp_cfg, 0x0, sizeof(struct aac_enc_cfg_t));
    aac_dsp_cfg.enc_format = MEDIA_FMT_AAC;
    aac_dsp_cfg.bit_rate = aac_bt_cfg->bitrate;
    aac_dsp_cfg.sample_rate = aac_bt_cfg->sampling_rate;
    switch(aac_bt_cfg->enc_mode) {
        case 0:
            aac_dsp_cfg.enc_mode = MEDIA_FMT_AAC_AOT_LC;
            break;
        case 2:
            aac_dsp_cfg.enc_mode = MEDIA_FMT_AAC_AOT_PS;
            break;
        case 1:
        default:
            aac_dsp_cfg.enc_mode = MEDIA_FMT_AAC_AOT_SBR;
            break;
    }
    aac_dsp_cfg.aac_fmt_flag = aac_bt_cfg->format_flag;
    aac_dsp_cfg.channel_cfg = aac_bt_cfg->channels;
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aac_dsp_cfg,
                              sizeof(struct aac_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: failed to set SBC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        is_configured = false;
        ALOGE(" ERROR  bit format CONFIG data mixer control not identified");
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set bit format to encoder", __func__);
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_AAC;
    a2dp.enc_sampling_rate = aac_bt_cfg->sampling_rate;
    a2dp.enc_channels = aac_bt_cfg->channels;
    ALOGV("Successfully updated AAC enc format with samplingrate: %d channels:%d",
           aac_dsp_cfg.sample_rate, aac_dsp_cfg.channel_cfg);
fail:
    return is_configured;
}

bool configure_celt_enc_format(audio_celt_encoder_config *celt_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct celt_enc_cfg_t celt_dsp_cfg;
    bool is_configured = false;
    int ret = 0;
    if(celt_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    memset(&celt_dsp_cfg, 0x0, sizeof(struct celt_enc_cfg_t));

    celt_dsp_cfg.custom_cfg.enc_format = MEDIA_FMT_CELT;
    celt_dsp_cfg.custom_cfg.sample_rate = celt_bt_cfg->sampling_rate;
    celt_dsp_cfg.custom_cfg.num_channels = celt_bt_cfg->channels;
    switch(celt_dsp_cfg.custom_cfg.num_channels) {
        case 1:
            celt_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
            celt_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
            celt_dsp_cfg.custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }

    celt_dsp_cfg.custom_cfg.custom_size = sizeof(struct celt_enc_cfg_t);

    celt_dsp_cfg.celt_cfg.frame_size = celt_bt_cfg->frame_size;
    celt_dsp_cfg.celt_cfg.complexity = celt_bt_cfg->complexity;
    celt_dsp_cfg.celt_cfg.prediction_mode = celt_bt_cfg->prediction_mode;
    celt_dsp_cfg.celt_cfg.vbr_flag = celt_bt_cfg->vbr_flag;
    celt_dsp_cfg.celt_cfg.bit_rate = celt_bt_cfg->bitrate;

    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&celt_dsp_cfg,
                              sizeof(struct celt_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set CELT encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR  bit format CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set bit format to encoder", __func__);
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_CELT;
    a2dp.enc_sampling_rate = celt_bt_cfg->sampling_rate;
    a2dp.enc_channels = celt_bt_cfg->channels;
    ALOGV("Successfully updated CELT encformat with samplingrate: %d channels:%d",
           celt_dsp_cfg.custom_cfg.sample_rate, celt_dsp_cfg.custom_cfg.num_channels);
fail:
    return is_configured;
}

bool configure_ldac_enc_format(audio_ldac_encoder_config *ldac_bt_cfg)
{
    struct mixer_ctl *ldac_enc_data = NULL, *ctrl_bit_format = NULL;
    struct ldac_enc_cfg_t ldac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;
    if(ldac_bt_cfg == NULL)
        return false;

    ldac_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ldac_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    memset(&ldac_dsp_cfg, 0x0, sizeof(struct ldac_enc_cfg_t));

    ldac_dsp_cfg.custom_cfg.enc_format = MEDIA_FMT_LDAC;
    ldac_dsp_cfg.custom_cfg.sample_rate = ldac_bt_cfg->sampling_rate;
    ldac_dsp_cfg.ldac_cfg.channel_mode = ldac_bt_cfg->channel_mode;
    switch(ldac_dsp_cfg.ldac_cfg.channel_mode) {
        case 4:
            ldac_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            ldac_dsp_cfg.custom_cfg.num_channels = 1;
            break;
        case 2:
        case 1:
        default:
            ldac_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
            ldac_dsp_cfg.custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            ldac_dsp_cfg.custom_cfg.num_channels = 2;
            break;
    }

    ldac_dsp_cfg.custom_cfg.custom_size = sizeof(struct ldac_enc_cfg_t);
    ldac_dsp_cfg.ldac_cfg.mtu = ldac_bt_cfg->mtu;
    ldac_dsp_cfg.ldac_cfg.bit_rate = ldac_bt_cfg->bit_rate;
    ret = mixer_ctl_set_array(ldac_enc_data, (void *)&ldac_dsp_cfg,
                              sizeof(struct ldac_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set LDAC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR  bit format CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set bit format to encoder", __func__);
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_LDAC;
    a2dp.enc_sampling_rate = ldac_bt_cfg->sampling_rate;
    a2dp.enc_channels = ldac_dsp_cfg.custom_cfg.num_channels;
    ALOGV("Successfully updated LDAC encformat with samplingrate: %d channels:%d",
           ldac_dsp_cfg.custom_cfg.sample_rate, ldac_dsp_cfg.custom_cfg.num_channels);
fail:
    return is_configured;
}

bool configure_a2dp_encoder_format()
{
    void *codec_info = NULL;
    uint8_t multi_cast = 0, num_dev = 1;
    codec_t codec_type = CODEC_TYPE_INVALID;
    bool is_configured = false;
    audio_aptx_encoder_config aptx_encoder_cfg;

    if (!a2dp.audio_get_enc_config) {
        ALOGE(" a2dp handle is not identified, ignoring a2dp encoder config");
        return false;
    }
    ALOGD("configure_a2dp_encoder_format start");
    codec_info = a2dp.audio_get_enc_config(&multi_cast, &num_dev,
                               &codec_type);

    switch(codec_type) {
        case CODEC_TYPE_SBC:
            ALOGD(" Received SBC encoder supported BT device");
            is_configured =
              configure_sbc_enc_format((audio_sbc_encoder_config *)codec_info);
            break;
        case CODEC_TYPE_APTX:
            ALOGD(" Received APTX encoder supported BT device");
#ifndef LINUX_ENABLED
            a2dp.is_aptx_dual_mono_supported = false;
            aptx_encoder_cfg.default_cfg = (audio_aptx_default_config *)codec_info;
#endif
            is_configured =
              configure_aptx_enc_format(&aptx_encoder_cfg);
            break;
        case CODEC_TYPE_APTX_HD:
            ALOGD(" Received APTX HD encoder supported BT device");
#ifndef LINUX_ENABLED
            is_configured =
              configure_aptx_hd_enc_format((audio_aptx_default_config *)codec_info);
#else
            is_configured =
              configure_aptx_hd_enc_format((audio_aptx_encoder_config *)codec_info);
#endif
            break;
#ifndef LINUX_ENABLED
        case CODEC_TYPE_APTX_DUAL_MONO:
            ALOGD(" Received APTX dual mono encoder supported BT device");
            a2dp.is_aptx_dual_mono_supported = true;
            aptx_encoder_cfg.dual_mono_cfg = (audio_aptx_dual_mono_config *)codec_info;
            is_configured =
              configure_aptx_enc_format(&aptx_encoder_cfg);
            break;
#endif
        case CODEC_TYPE_AAC:
            ALOGD(" Received AAC encoder supported BT device");
            is_configured =
              configure_aac_enc_format((audio_aac_encoder_config *)codec_info);
            break;
        case CODEC_TYPE_CELT:
            ALOGD(" Received CELT encoder supported BT device");
            is_configured =
              configure_celt_enc_format((audio_celt_encoder_config *)codec_info);
            break;
        case CODEC_TYPE_LDAC:
            ALOGD(" Received LDAC encoder supported BT device");
            is_configured =
              configure_ldac_enc_format((audio_ldac_encoder_config *)codec_info);
            break;
        default:
            ALOGD(" Received Unsupported encoder formar");
            is_configured = false;
            break;
    }
    return is_configured;
}

int audio_extn_a2dp_start_playback()
{
    int ret = 0;

    ALOGD("audio_extn_a2dp_start_playback start");

    if(!(a2dp.bt_lib_source_handle && a2dp.audio_source_start
       && a2dp.audio_get_enc_config)) {
        ALOGE("a2dp handle is not identified, Ignoring start playback request");
        return -ENOSYS;
    }

    if(a2dp.a2dp_source_suspended == true) {
        //session will be restarted after suspend completion
        ALOGD("a2dp start requested during suspend state");
        return -ENOSYS;
    }

    if (!a2dp.a2dp_source_started && !a2dp.a2dp_source_total_active_session_requests) {
        ALOGD("calling BT module stream start");
        /* This call indicates BT IPC lib to start playback */
        ret =  a2dp.audio_source_start();
        ALOGE("BT controller start return = %d",ret);
        if (ret != 0 ) {
           ALOGE("BT controller start failed");
           a2dp.a2dp_source_started = false;
        } else {
           if(configure_a2dp_encoder_format() == true) {
                a2dp.a2dp_source_started = true;
                ret = 0;
                ALOGD("Start playback successful to BT library");
           } else {
                ALOGD(" unable to configure DSP encoder");
                a2dp.a2dp_source_started = false;
                ret = -ETIMEDOUT;
           }
        }
    }

    if (a2dp.a2dp_source_started) {
        a2dp.a2dp_source_total_active_session_requests++;
        a2dp_check_and_set_scrambler();
        a2dp_set_backend_cfg(SOURCE);
    }

    ALOGD("start A2DP playback total active sessions :%d",
          a2dp.a2dp_source_total_active_session_requests);
    return ret;
}

uint64_t audio_extn_a2dp_get_decoder_latency()
{
    uint32_t latency = 0;

    switch(a2dp.bt_decoder_format) {
        case CODEC_TYPE_SBC:
            latency = DEFAULT_SINK_LATENCY_SBC;
            break;
        case CODEC_TYPE_AAC:
            latency = DEFAULT_SINK_LATENCY_AAC;
            break;
        default:
            latency = 200;
            ALOGD("No valid decoder defined, setting latency to %dms", latency);
            break;
    }
    return (uint64_t)latency;
}

bool a2dp_send_sink_setup_complete(void) {
    uint64_t system_latency = 0;
    bool is_complete = false;

    system_latency = audio_extn_a2dp_get_decoder_latency();

    if (a2dp.audio_sink_session_setup_complete(system_latency) == 0) {
        is_complete = true;
    }
    return is_complete;
}

int audio_extn_a2dp_start_capture()
{
    int ret = 0;

    ALOGD("audio_extn_a2dp_start_capture start");

    if(!(a2dp.bt_lib_sink_handle && a2dp.audio_sink_start
       && a2dp.audio_get_dec_config)) {
        ALOGE("a2dp handle is not identified, Ignoring start capture request");
        return -ENOSYS;
    }

    if (!a2dp.a2dp_sink_started && !a2dp.a2dp_sink_total_active_session_requests) {
        ALOGD("calling BT module stream start");
        /* This call indicates BT IPC lib to start capture */
        ret =  a2dp.audio_sink_start();
        ALOGE("BT controller start capture return = %d",ret);
        if (ret != 0 ) {
           ALOGE("BT controller start capture failed");
           a2dp.a2dp_sink_started = false;
        } else {

           if(!audio_extn_a2dp_sink_is_ready()) {
                ALOGD("Wait for capture ready not successful");
                ret = -ETIMEDOUT;
           }

           if(configure_a2dp_dsp_decoder_format() == true) {
                a2dp.a2dp_sink_started = true;
                ret = 0;
                ALOGD("Start capture successful to BT library");
           } else {
                ALOGD(" unable to configure DSP decoder");
                a2dp.a2dp_sink_started = false;
                ret = -ETIMEDOUT;
           }

           if (!a2dp_send_sink_setup_complete()) {
               ALOGD("sink_setup_complete not successful");
               ret = -ETIMEDOUT;
           }
        }
    }

    if (a2dp.a2dp_sink_started) {
        if (a2dp_set_backend_cfg(SINK) == true) {
        	a2dp.a2dp_sink_total_active_session_requests++;
        }
    }

    ALOGD("start A2DP sink total active sessions :%d",
          a2dp.a2dp_sink_total_active_session_requests);
    return ret;
}

static void reset_a2dp_enc_config_params()
{
    int ret =0;

    struct mixer_ctl *ctl_enc_config, *ctrl_bit_format;
    struct sbc_enc_cfg_t dummy_reset_config;

    memset(&dummy_reset_config, 0x0, sizeof(struct sbc_enc_cfg_t));
    ctl_enc_config = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                           MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_config) {
        ALOGE(" ERROR  a2dp encoder format mixer control not identified");
    } else {
        ret = mixer_ctl_set_array(ctl_enc_config, (void *)&dummy_reset_config,
                                        sizeof(struct sbc_enc_cfg_t));
         a2dp.bt_encoder_format = MEDIA_FMT_NONE;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR  bit format CONFIG data mixer control not identified");
    } else {
        ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
        if (ret != 0) {
            ALOGE("%s: Failed to set bit format to encoder", __func__);
        }
    }
}

static void reset_a2dp_dec_config_params()
{
    int ret =0;

    struct mixer_ctl *ctl_dec_config, *ctrl_bit_format;
    struct aac_dec_cfg_t dummy_reset_config;

    memset(&dummy_reset_config, 0x0, sizeof(struct aac_dec_cfg_t));
    ctl_dec_config = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                           MIXER_DEC_CONFIG_BLOCK);
    if (!ctl_dec_config) {
        ALOGE(" ERROR  a2dp decoder format mixer control not identified");
    } else {
        ret = mixer_ctl_set_array(ctl_dec_config, (void *)&dummy_reset_config,
                                        sizeof(struct aac_dec_cfg_t));
         a2dp.bt_decoder_format = MEDIA_FMT_NONE;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_DEC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR  bit format CONFIG data mixer control not identified");
    } else {
        ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
        if (ret != 0) {
            ALOGE("%s: Failed to set bit format to decoder", __func__);
        }
    }
}

int audio_extn_a2dp_stop_playback()
{
    int ret =0;

    ALOGV("audio_extn_a2dp_stop_playback start");
    if(!(a2dp.bt_lib_source_handle && a2dp.audio_source_stop)) {
        ALOGE("a2dp handle is not identified, Ignoring stop request");
        return -ENOSYS;
    }

    if (a2dp.a2dp_source_total_active_session_requests > 0)
        a2dp.a2dp_source_total_active_session_requests--;

    if ( a2dp.a2dp_source_started && !a2dp.a2dp_source_total_active_session_requests) {
        ALOGV("calling BT module stream stop");
        ret = a2dp.audio_source_stop();
        if (ret < 0)
            ALOGE("stop stream to BT IPC lib failed");
        else
            ALOGV("stop steam to BT IPC lib successful");
        reset_a2dp_enc_config_params();
        a2dp_reset_backend_cfg(SOURCE);
    }
    if(!a2dp.a2dp_source_total_active_session_requests)
       a2dp.a2dp_source_started = false;
    ALOGD("Stop A2DP playback, total active sessions :%d",
          a2dp.a2dp_source_total_active_session_requests);
    return 0;
}

int audio_extn_a2dp_stop_capture()
{
    int ret =0;

    ALOGV("audio_extn_a2dp_stop_capture start");
    if(!(a2dp.bt_lib_sink_handle && a2dp.audio_sink_stop)) {
        ALOGE("a2dp handle is not identified, Ignoring stop request");
        return -ENOSYS;
    }

    if (a2dp.a2dp_sink_total_active_session_requests > 0)
        a2dp.a2dp_sink_total_active_session_requests--;

    if ( a2dp.a2dp_sink_started && !a2dp.a2dp_sink_total_active_session_requests) {
        ALOGV("calling BT module stream stop");
        ret = a2dp.audio_sink_stop();
        if (ret < 0)
            ALOGE("stop stream to BT IPC lib failed");
        else
            ALOGV("stop steam to BT IPC lib successful");
        reset_a2dp_dec_config_params();
        a2dp_reset_backend_cfg(SINK);
    }
    if(!a2dp.a2dp_sink_total_active_session_requests)
       a2dp.a2dp_source_started = false;
    ALOGD("Stop A2DP capture, total active sessions :%d",
          a2dp.a2dp_sink_total_active_session_requests);
    return 0;
}

void audio_extn_a2dp_set_parameters(struct str_parms *parms)
{
     int ret, val;
     char value[32]={0};
     struct audio_usecase *uc_info;
     struct listnode *node;

     if(a2dp.is_a2dp_offload_supported == false) {
        ALOGV("no supported codecs identified,ignoring a2dp setparam");
        return;
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value,
                            sizeof(value));
     if (ret >= 0) {
         val = atoi(value);
         if (audio_is_a2dp_out_device(val)) {
             ALOGV("Received device connect request for A2DP source");
             open_a2dp_source();
         }
         goto param_handled;
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                         sizeof(value));

     if (ret >= 0) {
         val = atoi(value);
         if (audio_is_a2dp_out_device(val)) {
             ALOGV("Received source device dis- connect request");
             close_a2dp_output();
             reset_a2dp_enc_config_params();
             a2dp_reset_backend_cfg(SOURCE);
         } else if (audio_is_a2dp_in_device(val)) {
             ALOGV("Received sink device dis- connect request");
             close_a2dp_input();
             reset_a2dp_dec_config_params();
             a2dp_reset_backend_cfg(SINK);
         }
         goto param_handled;
     }

     ret = str_parms_get_str(parms, "A2dpSuspended", value, sizeof(value));
     if (ret >= 0) {
         if (a2dp.bt_lib_source_handle && (a2dp.bt_state_source != A2DP_STATE_DISCONNECTED) ) {
             if ((!strncmp(value,"true",sizeof(value)))) {
                ALOGD("Setting a2dp to suspend state");
                a2dp.a2dp_source_suspended = true;
                list_for_each(node, &a2dp.adev->usecase_list) {
                    uc_info = node_to_item(node, struct audio_usecase, list);
                    if (uc_info->type == PCM_PLAYBACK &&
                         (uc_info->stream.out->devices & AUDIO_DEVICE_OUT_ALL_A2DP)) {
                        pthread_mutex_unlock(&a2dp.adev->lock);
                        check_a2dp_restore(a2dp.adev, uc_info->stream.out, false);
                        pthread_mutex_lock(&a2dp.adev->lock);
                    }
                }
                reset_a2dp_enc_config_params();
                if(a2dp.audio_source_suspend)
                   a2dp.audio_source_suspend();
            } else if (a2dp.a2dp_source_suspended == true) {
                ALOGD("Resetting a2dp suspend state");
                struct audio_usecase *uc_info;
                struct listnode *node;
                if(a2dp.clear_source_a2dpsuspend_flag)
                    a2dp.clear_source_a2dpsuspend_flag();
                a2dp.a2dp_source_suspended = false;
                /*
                 * It is possible that before suspend,a2dp sessions can be active
                 * for example during music + voice activation concurrency
                 * a2dp suspend will be called & BT will change to sco mode
                 * though music is paused as a part of voice activation
                 * compress session close happens only after pause timeout(10secs)
                 * so if resume request comes before pause timeout as a2dp session
                 * is already active IPC start will not be called from APM/audio_hw
                 * Fix is to call a2dp start for IPC library post suspend
                 * based on number of active session count
                 */
                if (a2dp.a2dp_source_total_active_session_requests > 0) {
                    ALOGD(" Calling IPC lib start post suspend state");
                    if(a2dp.audio_source_start) {
                        ret =  a2dp.audio_source_start();
                        if (ret != 0) {
                            ALOGE("BT controller start failed");
                            a2dp.a2dp_source_started = false;
                        }
                    }
                }
                list_for_each(node, &a2dp.adev->usecase_list) {
                    uc_info = node_to_item(node, struct audio_usecase, list);
                    if (uc_info->type == PCM_PLAYBACK &&
                         (uc_info->stream.out->devices & AUDIO_DEVICE_OUT_ALL_A2DP)) {
                        pthread_mutex_unlock(&a2dp.adev->lock);
                        check_a2dp_restore(a2dp.adev, uc_info->stream.out, true);
                        pthread_mutex_lock(&a2dp.adev->lock);
                    }
                }
            }
        }
        goto param_handled;
     }
param_handled:
     ALOGV("end of a2dp setparam");
}

void audio_extn_a2dp_set_handoff_mode(bool is_on)
{
    a2dp.is_handoff_in_progress = is_on;
}

bool audio_extn_a2dp_is_force_device_switch()
{
    //During encoder reconfiguration mode, force a2dp device switch
    // Or if a2dp device is selected but earlier start failed ( as a2dp
    // was suspended, force retry.
    return a2dp.is_handoff_in_progress || !a2dp.a2dp_source_started;
}

void audio_extn_a2dp_get_enc_sample_rate(int *sample_rate)
{
    *sample_rate = a2dp.enc_sampling_rate;
}

void audio_extn_a2dp_get_dec_sample_rate(int *sample_rate)
{
    *sample_rate = a2dp.dec_sampling_rate;
}

bool audio_extn_a2dp_source_is_ready()
{
    bool ret = false;

    if (a2dp.a2dp_source_suspended)
        return ret;

    if ((a2dp.bt_state_source != A2DP_STATE_DISCONNECTED) &&
        (a2dp.is_a2dp_offload_supported) &&
        (a2dp.audio_source_check_a2dp_ready))
           ret = a2dp.audio_source_check_a2dp_ready();
    return ret;
}

bool audio_extn_a2dp_sink_is_ready()
{
    bool ret = false;

    if ((a2dp.bt_state_sink != A2DP_STATE_DISCONNECTED) &&
        (a2dp.is_a2dp_offload_supported) &&
        (a2dp.audio_sink_check_a2dp_ready))
           ret = a2dp.audio_sink_check_a2dp_ready();
    return ret;
}

bool audio_extn_a2dp_source_is_suspended()
{
    return a2dp.a2dp_source_suspended;
}

void audio_extn_a2dp_init (void *adev)
{
  a2dp.adev = (struct audio_device*)adev;
  a2dp.bt_lib_source_handle = NULL;
  a2dp.a2dp_source_started = false;
  a2dp.bt_state_source = A2DP_STATE_DISCONNECTED;
  a2dp.a2dp_source_total_active_session_requests = 0;
  a2dp.a2dp_source_suspended = false;
  a2dp.bt_encoder_format = CODEC_TYPE_INVALID;
  a2dp.enc_sampling_rate = 48000;
  a2dp.is_handoff_in_progress = false;
  a2dp.is_aptx_dual_mono_supported = false;
  reset_a2dp_enc_config_params();

  a2dp.bt_lib_sink_handle = NULL;
  a2dp.a2dp_sink_started = false;
  a2dp.bt_state_sink = A2DP_STATE_DISCONNECTED;
  a2dp.a2dp_sink_total_active_session_requests = 0;
  open_a2dp_sink();

  a2dp.is_a2dp_offload_supported = false;
  update_offload_codec_capabilities();
}

uint32_t audio_extn_a2dp_get_encoder_latency()
{
    uint32_t latency = 0;
    int avsync_runtime_prop = 0;
    int sbc_offset = 0, aptx_offset = 0, aptxhd_offset = 0,
        aac_offset = 0, celt_offset = 0, ldac_offset = 0;
    char value[PROPERTY_VALUE_MAX];

    memset(value, '\0', sizeof(char)*PROPERTY_VALUE_MAX);
    avsync_runtime_prop = property_get("vendor.audio.a2dp.codec.latency", value, NULL);
    if (avsync_runtime_prop > 0) {
        if (sscanf(value, "%d/%d/%d/%d/%d%d",
                  &sbc_offset, &aptx_offset, &aptxhd_offset, &aac_offset, &celt_offset, &ldac_offset) != 6) {
            ALOGI("Failed to parse avsync offset params from '%s'.", value);
            avsync_runtime_prop = 0;
        }
    }

    uint32_t slatency = 0;
    if (a2dp.audio_sink_get_a2dp_latency && a2dp.bt_state_source != A2DP_STATE_DISCONNECTED) {
        slatency = a2dp.audio_sink_get_a2dp_latency();
    }

    switch(a2dp.bt_encoder_format) {
        case CODEC_TYPE_SBC:
            latency = (avsync_runtime_prop > 0) ? sbc_offset : ENCODER_LATENCY_SBC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_SBC : slatency;
            break;
        case CODEC_TYPE_APTX:
            latency = (avsync_runtime_prop > 0) ? aptx_offset : ENCODER_LATENCY_APTX;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_APTX : slatency;
            break;
        case CODEC_TYPE_APTX_HD:
            latency = (avsync_runtime_prop > 0) ? aptxhd_offset : ENCODER_LATENCY_APTX_HD;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_APTX_HD : slatency;
            break;
        case CODEC_TYPE_AAC:
            latency = (avsync_runtime_prop > 0) ? aac_offset : ENCODER_LATENCY_AAC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_AAC : slatency;
            break;
        case CODEC_TYPE_CELT:
            latency = (avsync_runtime_prop > 0) ? celt_offset : ENCODER_LATENCY_CELT;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_CELT : slatency;
            break;
        case CODEC_TYPE_LDAC:
            latency = (avsync_runtime_prop > 0) ? ldac_offset : ENCODER_LATENCY_LDAC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_LDAC : slatency;
            break;
        default:
            latency = 200;
            break;
    }
    return latency;
}
#endif // SPLIT_A2DP_ENABLED
