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
#define BT_IPC_LIB_NAME  "libbthost_if.so"
#define ENC_MEDIA_FMT_NONE                                     0
#define ENC_MEDIA_FMT_AAC                                  0x00010DA6
#define ENC_MEDIA_FMT_APTX                                 0x000131ff
#define ENC_MEDIA_FMT_APTX_HD                              0x00013200
#define ENC_MEDIA_FMT_APTX_AD                              0x00013204
#define ENC_MEDIA_FMT_SBC                                  0x00010BF2
#define ENC_MEDIA_FMT_CELT                                 0x00013221
#define ENC_MEDIA_FMT_LDAC                                 0x00013224
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
#define MIXER_DEC_CONFIG_BLOCK     "SLIM_7_TX Decoder Config"
#define MIXER_ENC_BIT_FORMAT       "AFE Input Bit Format"
#define MIXER_SCRAMBLER_MODE       "AFE Scrambler Mode"
#define MIXER_SAMPLE_RATE_RX       "BT SampleRate RX"
#define MIXER_SAMPLE_RATE_TX       "BT SampleRate TX"
#define MIXER_SAMPLE_RATE_DEFAULT  "BT SampleRate"
#define MIXER_AFE_IN_CHANNELS      "AFE Input Channels"
#define MIXER_ABR_TX_FEEDBACK_PATH "A2DP_SLIM7_UL_HL Switch"
#define MIXER_SET_FEEDBACK_CHANNEL "BT set feedback channel"
#define MIXER_ENC_FMT_SBC          "SBC"
#define MIXER_ENC_FMT_AAC          "AAC"
#define MIXER_ENC_FMT_APTX         "APTX"
#define MIXER_FMT_TWS_CHANNEL_MODE "TWS Channel Mode"
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

// Slimbus Tx sample rate for ABR feedback channel
#define ABR_TX_SAMPLE_RATE             "KHZ_8"

// Purpose ID for Inter Module Communication (IMC) in AFE
#define IMC_PURPOSE_ID_BT_INFO         0x000132E2

// Maximum quality levels for ABR
#define MAX_ABR_QUALITY_LEVELS             5

// Instance identifier for A2DP
#define MAX_INSTANCE_ID                (UINT32_MAX / 2)

#define SAMPLING_RATE_48K               48000
#define SAMPLING_RATE_441K              44100
#define CH_STEREO                       2
#define CH_MONO                         1
/*
 * Below enum values are extended from audio_base.h to
 * to keep encoder codec type local to bthost_ipc
 * and audio_hal as these are intended only for handshake
 * between IPC lib and Audio HAL.
 */
typedef enum {
    ENC_CODEC_TYPE_INVALID = AUDIO_FORMAT_INVALID, // 0xFFFFFFFFUL
    ENC_CODEC_TYPE_AAC = AUDIO_FORMAT_AAC, // 0x04000000UL
    ENC_CODEC_TYPE_SBC = AUDIO_FORMAT_SBC, // 0x1F000000UL
    ENC_CODEC_TYPE_APTX = AUDIO_FORMAT_APTX, // 0x20000000UL
    ENC_CODEC_TYPE_APTX_HD = AUDIO_FORMAT_APTX_HD, // 0x21000000UL
#ifndef LINUX_ENABLED
    ENC_CODEC_TYPE_APTX_DUAL_MONO = 570425344u, // 0x22000000UL
#endif
    ENC_CODEC_TYPE_LDAC = AUDIO_FORMAT_LDAC, // 0x23000000UL
    ENC_CODEC_TYPE_CELT = 603979776u, // 0x24000000UL
    ENC_CODEC_TYPE_APTX_AD = 620756992u, // 0x25000000UL
}enc_codec_t;

/*
 * enums which describes the APTX Adaptive
 * channel mode, these values are used by encoder
 */
 typedef enum {
    APTX_AD_CHANNEL_UNCHANGED = -1,
    APTX_AD_CHANNEL_JOINT_STEREO = 0, // default
    APTX_AD_CHANNEL_MONO = 1,
    APTX_AD_CHANNEL_DUAL_MONO = 2,
    APTX_AD_CHANNEL_STEREO_TWS = 4,
    APTX_AD_CHANNEL_EARBUD = 8,
} enc_aptx_ad_channel_mode;

/*
 * enums which describes the APTX Adaptive
 * sampling frequency, these values are used
 * by encoder
 */
typedef enum {
    APTX_AD_SR_UNCHANGED = 0x0,
    APTX_AD_48 = 0x1,  // 48 KHz default
    APTX_AD_44_1 = 0x2, // 44.1kHz
} enc_aptx_ad_s_rate;

typedef int (*audio_stream_open_t)(void);
typedef int (*audio_stream_close_t)(void);
typedef int (*audio_start_stream_t)(void);
typedef int (*audio_stop_stream_t)(void);
typedef int (*audio_suspend_stream_t)(void);
typedef void (*audio_handoff_triggered_t)(void);
typedef void (*clear_a2dpsuspend_flag_t)(void);
typedef void * (*audio_get_codec_config_t)(uint8_t *multicast_status,uint8_t *num_dev,
                               enc_codec_t *codec_type);
typedef int (*audio_check_a2dp_ready_t)(void);
typedef uint16_t (*audio_get_a2dp_sink_latency_t)(void);
typedef int (*audio_is_scrambling_enabled_t)(void);
typedef bool (*audio_is_tws_mono_mode_enable_t)(void);

enum A2DP_STATE {
    A2DP_STATE_CONNECTED,
    A2DP_STATE_STARTED,
    A2DP_STATE_STOPPED,
    A2DP_STATE_DISCONNECTED,
};

typedef enum {
    IMC_TRANSMIT,
    IMC_RECEIVE,
} imc_direction_t;

typedef enum {
    IMC_DISABLE,
    IMC_ENABLE,
} imc_status_t;

typedef enum {
    MTU_SIZE,
    PEAK_BIT_RATE,
} frame_control_type_t;

/* PCM config for ABR Feedback hostless front end */
static struct pcm_config pcm_config_abr = {
    .channels = 1,
    .rate = 8000,
    .period_size = 240,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

/* Adaptive bitrate config for A2DP codecs */
struct a2dp_abr_config {
    /* Flag to denote whether Adaptive bitrate is enabled for codec */
    bool is_abr_enabled;
    /* Flag to denote whether front end has been opened for ABR */
    bool abr_started;
    /* ABR Tx path pcm handle */
    struct pcm *abr_tx_handle;
    /* ABR Inter Module Communication (IMC) instance ID */
    uint32_t imc_instance;
};

static uint32_t instance_id = MAX_INSTANCE_ID;

/* structure used to  update a2dp state machine
 * to communicate IPC library
 * to store DSP encoder configuration information
 */
struct a2dp_data {
    struct audio_device *adev;
    void *bt_lib_handle;
    audio_stream_open_t audio_stream_open;
    audio_stream_close_t audio_stream_close;
    audio_start_stream_t audio_start_stream;
    audio_stop_stream_t audio_stop_stream;
    audio_suspend_stream_t audio_suspend_stream;
    audio_handoff_triggered_t audio_handoff_triggered;
    clear_a2dpsuspend_flag_t clear_a2dpsuspend_flag;
    audio_get_codec_config_t audio_get_codec_config;
    audio_check_a2dp_ready_t audio_check_a2dp_ready;
    audio_get_a2dp_sink_latency_t audio_get_a2dp_sink_latency;
    audio_is_scrambling_enabled_t audio_is_scrambling_enabled;
    audio_is_tws_mono_mode_enable_t audio_is_tws_mono_mode_enable;
    enum A2DP_STATE bt_state;
    enc_codec_t bt_encoder_format;
    uint32_t enc_sampling_rate;
    uint32_t enc_channels;
    bool a2dp_started;
    bool a2dp_suspended;
    int  a2dp_total_active_session_request;
    bool is_a2dp_offload_supported;
    bool is_handoff_in_progress;
    bool is_aptx_dual_mono_supported;
    /* Mono Mode support for TWS+ */
    bool is_tws_mono_mode_on;
    bool is_aptx_adaptive;
    /* Adaptive bitrate config for A2DP codecs */
    struct a2dp_abr_config abr_config;
};

struct a2dp_data a2dp;

/* Adaptive bitrate (ABR) is supported by certain Bluetooth codecs.
 * Structures sent to configure DSP for ABR are defined below.
 * This data helps DSP configure feedback path (BTSoC to LPASS)
 * for link quality levels and mapping quality levels to codec
 * specific bitrate.
 */

/* Key value pair for link quality level to bitrate mapping. */
struct bit_rate_level_map_t {
    uint32_t link_quality_level;
    uint32_t bitrate;
};

/* Link quality level to bitrate mapping info sent to DSP. */
struct quality_level_to_bitrate_info {
    /* Number of quality levels being mapped.
     * This will be equal to the size of mapping table.
     */
    uint32_t num_levels;
    /* Quality level to bitrate mapping table */
    struct bit_rate_level_map_t bit_rate_level_map[MAX_ABR_QUALITY_LEVELS];
};

/* Structure to set up Inter Module Communication (IMC) between
 * AFE Decoder and Encoder.
 */
struct imc_dec_enc_info {
    /* Decoder to encoder communication direction.
     * Transmit = 0 / Receive = 1
     */
    uint32_t direction;
    /* Enable / disable IMC between decoder and encoder */
    uint32_t enable;
    /* Purpose of IMC being set up between decoder and encoder.
     * IMC_PURPOSE_ID_BT_INFO defined for link quality feedback
     * is the default value to be sent as purpose.
     */
    uint32_t purpose;
    /* Unique communication instance ID.
     * purpose and comm_instance together form the actual key
     * used in IMC registration, which must be the same for
     * encoder and decoder for which IMC is being set up.
     */
    uint32_t comm_instance;
};

/* Structure to control frame size of AAC encoded frames. */
struct aac_frame_size_control_t {
    /* Type of frame size control: MTU_SIZE / PEAK_BIT_RATE*/
    uint32_t ctl_type;
    /* Control value
     * MTU_SIZE: MTU size in bytes
     * PEAK_BIT_RATE: Peak bitrate in bits per second.
     */
    uint32_t ctl_value;
};

/* Structure used for ABR config of AFE encoder and decoder. */
struct abr_enc_cfg_t {
    /* Link quality level to bitrate mapping info sent to DSP. */
    struct quality_level_to_bitrate_info mapping_info;
    /* Information to set up IMC between decoder and encoder */
    struct imc_dec_enc_info imc_info;
    /* Flag to indicate whether ABR is enabled */
    bool is_abr_enabled;
}  __attribute__ ((packed));

/* Structure to send configuration for decoder introduced
 * on AFE Tx path for ABR link quality feedback to BT encoder.
 */
struct abr_dec_cfg_t {
    /* Decoder media format */
    uint32_t dec_format;
    /* Information to set up IMC between decoder and encoder */
    struct imc_dec_enc_info imc_info;
} __attribute__ ((packed));

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

struct aac_enc_cfg_v2_t {
    struct aac_enc_cfg_t aac_enc_cfg;
    struct aac_frame_size_control_t frame_ctl;
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

/* APTX AD structure */
struct aptx_ad_enc_cfg_ext_t
{
    uint32_t  sampling_freq;
    uint32_t  mtu;
    uint32_t  channel_mode;
    uint32_t  min_sink_modeA;
    uint32_t  max_sink_modeA;
    uint32_t  min_sink_modeB;
    uint32_t  max_sink_modeB;
    uint32_t  min_sink_modeC;
    uint32_t  max_sink_modeC;
    uint32_t  mode;
} __attribute__ ((packed));

struct aptx_ad_enc_cfg_t
{
    struct custom_enc_cfg_t  custom_cfg;
    struct aptx_ad_enc_cfg_ext_t aptx_ad_cfg;
    struct abr_enc_cfg_t abr_cfg;
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
    struct abr_enc_cfg_t abr_cfg;
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
    uint32_t bits_per_sample;
} audio_sbc_encoder_config;

/* Information about BT APTX encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint16_t sampling_rate;
    uint8_t  channels;
    uint32_t bitrate;
    uint32_t bits_per_sample;
} audio_aptx_default_config;

typedef struct {
    uint8_t  sampling_rate;
    uint8_t  channel_mode;
    uint16_t mtu;
    uint8_t  min_sink_modeA;
    uint8_t  max_sink_modeA;
    uint8_t  min_sink_modeB;
    uint8_t  max_sink_modeB;
    uint8_t  min_sink_modeC;
    uint8_t  max_sink_modeC;
    uint8_t  TTP_modeA_low;
    uint8_t  TTP_modeA_high;
    uint8_t  TTP_modeB_low;
    uint8_t  TTP_modeB_high;
    uint32_t bits_per_sample;
    uint16_t  encoder_mode;
} audio_aptx_ad_config;

typedef struct {
    uint16_t sampling_rate;
    uint8_t  channels;
    uint32_t bitrate;
    uint32_t sync_mode;
    uint32_t bits_per_sample;
} audio_aptx_dual_mono_config;

typedef union {
    audio_aptx_default_config *default_cfg;
    audio_aptx_dual_mono_config *dual_mono_cfg;
    audio_aptx_ad_config *ad_cfg;
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
    uint32_t bits_per_sample;
} audio_aac_encoder_config;

typedef struct {
    audio_aac_encoder_config audio_aac_enc_cfg;
    struct aac_frame_size_control_t frame_ctl;
} audio_aac_encoder_config_v2;
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
    uint32_t bits_per_sample;
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
    bool is_abr_enabled;
    struct quality_level_to_bitrate_info level_to_bitrate_map;
    uint32_t bits_per_sample;
} audio_ldac_encoder_config;

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
        } else if( strcmp(tok, "aptxadaptive") == 0) {
            ALOGD("%s: aptx adaptive offload supported\n",__func__);
            a2dp.is_a2dp_offload_supported = true;
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

static int stop_abr()
{
    struct mixer_ctl *ctl_abr_tx_path = NULL;
    struct mixer_ctl *ctl_set_bt_feedback_channel = NULL;

    /* This function can be used if !abr_started for clean up */
    ALOGV("%s: enter", __func__);

    // Close hostless front end
    if (a2dp.abr_config.abr_tx_handle != NULL) {
        pcm_close(a2dp.abr_config.abr_tx_handle);
        a2dp.abr_config.abr_tx_handle = NULL;
    }
    a2dp.abr_config.abr_started = false;
    a2dp.abr_config.imc_instance = 0;

    // Reset BT driver mixer control for ABR usecase
    ctl_set_bt_feedback_channel = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SET_FEEDBACK_CHANNEL);
    if (!ctl_set_bt_feedback_channel) {
        ALOGE("%s: ERROR Set usecase mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_value(ctl_set_bt_feedback_channel, 0, 0) != 0) {
        ALOGE("%s: Failed to set BT usecase", __func__);
        return -ENOSYS;
    }

    // Reset ABR Tx feedback path
    ALOGV("%s: Disable ABR Tx feedback path", __func__);
    ctl_abr_tx_path = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_ABR_TX_FEEDBACK_PATH);
    if (!ctl_abr_tx_path) {
        ALOGE("%s: ERROR ABR Tx feedback path mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_value(ctl_abr_tx_path, 0, 0) != 0) {
        ALOGE("%s: Failed to set ABR Tx feedback path", __func__);
        return -ENOSYS;
    }

   return 0;
}

static int start_abr()
{
    struct mixer_ctl *ctl_abr_tx_path = NULL;
    struct mixer_ctl *ctl_set_bt_feedback_channel = NULL;
    int abr_device_id;
    int ret = 0;

    if (!a2dp.abr_config.is_abr_enabled) {
        ALOGE("%s: Cannot start if ABR is not enabled", __func__);
        return -ENOSYS;
    }

    if (a2dp.abr_config.abr_started) {
        ALOGI("%s: ABR has already started", __func__);
        return ret;
    }

    // Enable Slimbus 7 Tx feedback path
    ALOGV("%s: Enable ABR Tx feedback path", __func__);
    ctl_abr_tx_path = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_ABR_TX_FEEDBACK_PATH);
    if (!ctl_abr_tx_path) {
        ALOGE("%s: ERROR ABR Tx feedback path mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_value(ctl_abr_tx_path, 0, 1) != 0) {
        ALOGE("%s: Failed to set ABR Tx feedback path", __func__);
        return -ENOSYS;
    }

    // Notify ABR usecase information to BT driver to distinguish
    // between SCO and feedback usecase
    ctl_set_bt_feedback_channel = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SET_FEEDBACK_CHANNEL);
    if (!ctl_set_bt_feedback_channel) {
        ALOGE("%s: ERROR Set usecase mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_value(ctl_set_bt_feedback_channel, 0, 1) != 0) {
        ALOGE("%s: Failed to set BT usecase", __func__);
        return -ENOSYS;
    }

    // Open hostless front end and prepare ABR Tx path
    abr_device_id = platform_get_pcm_device_id(USECASE_AUDIO_A2DP_ABR_FEEDBACK,
                                               PCM_CAPTURE);
    if (!a2dp.abr_config.abr_tx_handle) {
        a2dp.abr_config.abr_tx_handle = pcm_open(a2dp.adev->snd_card,
                                                 abr_device_id, PCM_IN,
                                                 &pcm_config_abr);
        if (a2dp.abr_config.abr_tx_handle == NULL ||
            !pcm_is_ready(a2dp.abr_config.abr_tx_handle))
            goto fail;
    }
    ret = pcm_start(a2dp.abr_config.abr_tx_handle);
    if (ret < 0)
        goto fail;
    a2dp.abr_config.abr_started = true;

    return ret;

fail:
    ALOGE("%s: %s", __func__, pcm_get_error(a2dp.abr_config.abr_tx_handle));
    stop_abr();
    return -ENOSYS;
}

/* API to open BT IPC library to start IPC communication */
static void open_a2dp_output()
{
    int ret = 0;

    ALOGD(" Open A2DP output start ");
    if (a2dp.bt_lib_handle == NULL){
        ALOGD(" Requesting for BT lib handle");
        a2dp.bt_lib_handle = dlopen(BT_IPC_LIB_NAME, RTLD_NOW);

        if (a2dp.bt_lib_handle == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__, BT_IPC_LIB_NAME);
            ret = -ENOSYS;
            goto init_fail;
        } else {
            a2dp.audio_stream_open = (audio_stream_open_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stream_open");
            a2dp.audio_start_stream = (audio_start_stream_t)
                          dlsym(a2dp.bt_lib_handle, "audio_start_stream");
            a2dp.audio_get_codec_config = (audio_get_codec_config_t)
                          dlsym(a2dp.bt_lib_handle, "audio_get_codec_config");
            a2dp.audio_suspend_stream = (audio_suspend_stream_t)
                          dlsym(a2dp.bt_lib_handle, "audio_suspend_stream");
            a2dp.audio_handoff_triggered = (audio_handoff_triggered_t)
                          dlsym(a2dp.bt_lib_handle, "audio_handoff_triggered");
            a2dp.clear_a2dpsuspend_flag = (clear_a2dpsuspend_flag_t)
                          dlsym(a2dp.bt_lib_handle, "clear_a2dpsuspend_flag");
            a2dp.audio_stop_stream = (audio_stop_stream_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stop_stream");
            a2dp.audio_stream_close = (audio_stream_close_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stream_close");
            a2dp.audio_check_a2dp_ready = (audio_check_a2dp_ready_t)
                        dlsym(a2dp.bt_lib_handle,"audio_check_a2dp_ready");
            a2dp.audio_get_a2dp_sink_latency = (audio_get_a2dp_sink_latency_t)
                        dlsym(a2dp.bt_lib_handle,"audio_get_a2dp_sink_latency");
            a2dp.audio_is_scrambling_enabled = (audio_is_scrambling_enabled_t)
                        dlsym(a2dp.bt_lib_handle,"audio_is_scrambling_enabled");
           a2dp.audio_is_tws_mono_mode_enable = (audio_is_tws_mono_mode_enable_t)
                        dlsym(a2dp.bt_lib_handle,"isTwsMonomodeEnable");
        }
    }

    if (a2dp.bt_lib_handle && a2dp.audio_stream_open) {
        if (a2dp.bt_state == A2DP_STATE_DISCONNECTED) {
            ALOGD("calling BT stream open");
            ret = a2dp.audio_stream_open();
            if(ret != 0) {
                ALOGE("Failed to open output stream for a2dp: status %d", ret);
                goto init_fail;
            }
            a2dp.bt_state = A2DP_STATE_CONNECTED;
        } else {
            ALOGD("Called a2dp open with improper state, Ignoring request state %d", a2dp.bt_state);
        }
    } else {
        ALOGE("a2dp handle is not identified, Ignoring open request");
        a2dp.bt_state = A2DP_STATE_DISCONNECTED;
        goto init_fail;
    }

init_fail:
    if(ret != 0 && (a2dp.bt_lib_handle != NULL)) {
        dlclose(a2dp.bt_lib_handle);
        a2dp.bt_lib_handle = NULL;
    }
}

static int close_a2dp_output()
{
    ALOGV("%s\n",__func__);
    if (!(a2dp.bt_lib_handle && a2dp.audio_stream_close)) {
        ALOGE("a2dp handle is not identified, Ignoring close request");
        return -ENOSYS;
    }
    if (a2dp.bt_state != A2DP_STATE_DISCONNECTED) {
        ALOGD("calling BT stream close");
        if(a2dp.audio_stream_close() == false)
            ALOGE("failed close a2dp control path from BT library");
    }
    a2dp.a2dp_started = false;
    a2dp.a2dp_total_active_session_request = 0;
    a2dp.a2dp_suspended = false;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_INVALID;
    a2dp.enc_sampling_rate = 48000;
    a2dp.enc_channels = 2;
    a2dp.bt_state = A2DP_STATE_DISCONNECTED;
    if (a2dp.abr_config.is_abr_enabled && a2dp.abr_config.abr_started)
        stop_abr();
    a2dp.abr_config.is_abr_enabled = false;
    a2dp.abr_config.abr_started = false;
    a2dp.abr_config.imc_instance = 0;
    a2dp.abr_config.abr_tx_handle = NULL;

    return 0;
}

static void a2dp_check_and_set_scrambler()
{
    bool scrambler_mode = false;
    struct mixer_ctl *ctrl_scrambler_mode = NULL;
    if (a2dp.audio_is_scrambling_enabled && (a2dp.bt_state != A2DP_STATE_DISCONNECTED))
        scrambler_mode = a2dp.audio_is_scrambling_enabled();

    if (scrambler_mode) {
        //enable scrambler in dsp
        ctrl_scrambler_mode = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SCRAMBLER_MODE);
        if (!ctrl_scrambler_mode) {
            ALOGE(" ERROR scrambler mode mixer control not identifed");
            return;
        } else {
            if (mixer_ctl_set_value(ctrl_scrambler_mode, 0, true) != 0) {
                ALOGE("%s: Could not set scrambler mode", __func__);
                return;
            }
        }
    }
}

static int a2dp_set_backend_cfg()
{
    char *rate_str = NULL, *in_channels = NULL;
    uint32_t sampling_rate_rx = a2dp.enc_sampling_rate;
    struct mixer_ctl *ctl_sample_rate = NULL, *ctrl_in_channels = NULL;

    /* For LDAC encoder open slimbus port at 96Khz for 48Khz input
     * and 88.2Khz for 44.1Khz input.
     * For APTX AD encoder, open slimbus port at 96Khz for 48Khz input.
     */
    if ((a2dp.bt_encoder_format == ENC_CODEC_TYPE_LDAC) &&
        (sampling_rate_rx == SAMPLING_RATE_48K ||
         sampling_rate_rx == SAMPLING_RATE_441K)) {
        sampling_rate_rx *= 2;
    } else if (a2dp.bt_encoder_format == ENC_CODEC_TYPE_APTX_AD &&
               sampling_rate_rx == SAMPLING_RATE_48K) {
        sampling_rate_rx *= 2;
    }

    // Set Rx backend sample rate
    switch (sampling_rate_rx) {
    case 44100:
        rate_str = "KHZ_44P1";
        break;
    case 88200:
        rate_str = "KHZ_88P2";
        break;
    case 96000:
        rate_str = "KHZ_96";
        break;
    case 48000:
    default:
        rate_str = "KHZ_48";
        break;
    }

    ALOGD("%s: set backend rx sample rate = %s", __func__, rate_str);
    ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SAMPLE_RATE_RX);
    if (ctl_sample_rate) {

        if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
            ALOGE("%s: Failed to set backend sample rate = %s", __func__, rate_str);
            return -ENOSYS;
        }

        /* Set Tx backend sample rate */
        if (a2dp.abr_config.is_abr_enabled) {
            rate_str = ABR_TX_SAMPLE_RATE;

            ALOGD("%s: set backend tx sample rate = %s", __func__, rate_str);
            ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                                MIXER_SAMPLE_RATE_TX);
            if (!ctl_sample_rate) {
                ALOGE("%s: ERROR backend sample rate mixer control not identifed", __func__);
                return -ENOSYS;
            }

            if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
                ALOGE("%s: Failed to set backend sample rate = %s", __func__, rate_str);
                return -ENOSYS;
            }
        }
    } else {
        /* Fallback to legacy approch if MIXER_SAMPLE_RATE_RX and
        MIXER_SAMPLE_RATE_TX is not supported */
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SAMPLE_RATE_DEFAULT);
        if (!ctl_sample_rate) {
            ALOGE("%s: ERROR backend sample rate mixer control not identifed", __func__);
            return -ENOSYS;
        }

        if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
            ALOGE("%s: Failed to set backend sample rate = %s", __func__, rate_str);
            return -ENOSYS;
        }
    }

    //Configure AFE input channels
    switch (a2dp.enc_channels) {
    case 1:
        in_channels = "One";
        break;
    case 2:
    default:
        in_channels = "Two";
        break;
    }

    ALOGD("%s: set AFE input channels = %d", __func__, a2dp.enc_channels);
    ctrl_in_channels = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_AFE_IN_CHANNELS);
    if (!ctrl_in_channels) {
        ALOGE("%s: ERROR AFE input channels mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_enum_by_string(ctrl_in_channels, in_channels) != 0) {
        ALOGE("%s: Failed to set AFE in channels = %d", __func__, a2dp.enc_channels);
        return -ENOSYS;
    }

    return 0;
}

static int a2dp_set_bit_format(uint32_t enc_bit_format)
{
    const char *bit_format = NULL;
    struct mixer_ctl *ctrl_bit_format = NULL;

    // Configure AFE Input Bit Format
    switch (enc_bit_format) {
    case 32:
        bit_format = "S32_LE";
        break;
    case 24:
        bit_format = "S24_LE";
        break;
    case 16:
    default:
        bit_format = "S16_LE";
        break;
    }

    ALOGD("%s: set AFE input bit format = %d", __func__, enc_bit_format);
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE("%s: ERROR AFE input bit format mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_enum_by_string(ctrl_bit_format, bit_format) != 0) {
        ALOGE("%s: Failed to set AFE input bit format = %d", __func__, enc_bit_format);
        return -ENOSYS;
    }
    return 0;
}

static int a2dp_reset_backend_cfg()
{
    const char *rate_str = "KHZ_8", *in_channels = "Zero";
    struct mixer_ctl *ctl_sample_rate_rx = NULL, *ctl_sample_rate_tx = NULL;
    struct mixer_ctl *ctrl_in_channels = NULL;

    // Reset backend sampling rate
    ALOGD("%s: reset backend sample rate = %s", __func__, rate_str);
    ctl_sample_rate_rx = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SAMPLE_RATE_RX);
    if (ctl_sample_rate_rx) {

        if (mixer_ctl_set_enum_by_string(ctl_sample_rate_rx, rate_str) != 0) {
            ALOGE("%s: Failed to reset Rx backend sample rate = %s", __func__, rate_str);
            return -ENOSYS;
        }

        if (a2dp.abr_config.is_abr_enabled) {
            ctl_sample_rate_tx = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SAMPLE_RATE_TX);
            if (!ctl_sample_rate_tx) {
                ALOGE("%s: ERROR Tx backend sample rate mixer control not identifed", __func__);
                return -ENOSYS;
            }

            if (mixer_ctl_set_enum_by_string(ctl_sample_rate_tx, rate_str) != 0) {
                ALOGE("%s: Failed to reset Tx backend sample rate = %s", __func__, rate_str);
                return -ENOSYS;
            }
        }
    } else {

        ctl_sample_rate_rx = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SAMPLE_RATE_DEFAULT);
        if (!ctl_sample_rate_rx) {
            ALOGE("%s: ERROR backend sample rate mixer control not identifed", __func__);
            return -ENOSYS;
        }

        if (mixer_ctl_set_enum_by_string(ctl_sample_rate_rx, rate_str) != 0) {
            ALOGE("%s: Failed to reset backend sample rate = %s", __func__, rate_str);
            return -ENOSYS;
        }
    }

    // Reset AFE input channels
    ALOGD("%s: reset AFE input channels = %s", __func__, in_channels);
    ctrl_in_channels = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_AFE_IN_CHANNELS);
    if (!ctrl_in_channels) {
        ALOGE("%s: ERROR AFE input channels mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_enum_by_string(ctrl_in_channels, in_channels) != 0) {
        ALOGE("%s: Failed to reset AFE in channels = %d", __func__, a2dp.enc_channels);
        return -ENOSYS;
    }

    return 0;
}

/* API to configure AFE decoder in DSP */
static bool configure_a2dp_decoder_format(int dec_format)
{
    struct mixer_ctl *ctl_dec_data = NULL;
    struct abr_dec_cfg_t dec_cfg;
    int ret = 0;

    if (a2dp.abr_config.is_abr_enabled) {
        ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_DEC_CONFIG_BLOCK);
        if (!ctl_dec_data) {
            ALOGE("%s: ERROR A2DP codec config data mixer control not identifed", __func__);
            return false;
        }
        memset(&dec_cfg, 0x0, sizeof(dec_cfg));
        dec_cfg.dec_format = dec_format;
        dec_cfg.imc_info.direction = IMC_TRANSMIT;
        dec_cfg.imc_info.enable = IMC_ENABLE;
        dec_cfg.imc_info.purpose = IMC_PURPOSE_ID_BT_INFO;
        dec_cfg.imc_info.comm_instance = a2dp.abr_config.imc_instance;

        ret = mixer_ctl_set_array(ctl_dec_data, &dec_cfg,
                                  sizeof(dec_cfg));
        if (ret != 0) {
            ALOGE("%s: Failed to set decoder config", __func__);
            return false;
        }
    }

    return true;
}

/* API to configure SBC DSP encoder */
bool configure_sbc_enc_format(audio_sbc_encoder_config *sbc_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL;
    struct sbc_enc_cfg_t sbc_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(sbc_bt_cfg == NULL)
        return false;

   ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    memset(&sbc_dsp_cfg, 0x0, sizeof(struct sbc_enc_cfg_t));
    sbc_dsp_cfg.enc_format = ENC_MEDIA_FMT_SBC;
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
    ret = a2dp_set_bit_format(sbc_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_SBC;
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
static int update_aptx_ad_dsp_config(struct aptx_ad_enc_cfg_t *aptx_dsp_cfg,
                                     audio_aptx_encoder_config *aptx_bt_cfg)
{
    int ret = 0;

    if(aptx_dsp_cfg == NULL || aptx_bt_cfg == NULL) {
        ALOGE("Invalid param, aptx_dsp_cfg %p aptx_bt_cfg %p",
              aptx_dsp_cfg, aptx_bt_cfg);
        return -EINVAL;
    }

    memset(aptx_dsp_cfg, 0x0, sizeof(struct aptx_ad_enc_cfg_t));
    aptx_dsp_cfg->custom_cfg.enc_format = ENC_MEDIA_FMT_APTX_AD;


    aptx_dsp_cfg->aptx_ad_cfg.sampling_freq = aptx_bt_cfg->ad_cfg->sampling_rate;
    aptx_dsp_cfg->aptx_ad_cfg.mtu = aptx_bt_cfg->ad_cfg->mtu;
    aptx_dsp_cfg->aptx_ad_cfg.channel_mode = aptx_bt_cfg->ad_cfg->channel_mode;
    aptx_dsp_cfg->aptx_ad_cfg.min_sink_modeA = aptx_bt_cfg->ad_cfg->min_sink_modeA;
    aptx_dsp_cfg->aptx_ad_cfg.max_sink_modeA = aptx_bt_cfg->ad_cfg->max_sink_modeA;
    aptx_dsp_cfg->aptx_ad_cfg.min_sink_modeB = aptx_bt_cfg->ad_cfg->min_sink_modeB;
    aptx_dsp_cfg->aptx_ad_cfg.max_sink_modeB = aptx_bt_cfg->ad_cfg->max_sink_modeB;
    aptx_dsp_cfg->aptx_ad_cfg.min_sink_modeC = aptx_bt_cfg->ad_cfg->min_sink_modeC;
    aptx_dsp_cfg->aptx_ad_cfg.max_sink_modeC = aptx_bt_cfg->ad_cfg->max_sink_modeC;
    aptx_dsp_cfg->aptx_ad_cfg.mode = aptx_bt_cfg->ad_cfg->encoder_mode;
    aptx_dsp_cfg->abr_cfg.imc_info.direction = IMC_RECEIVE;
    aptx_dsp_cfg->abr_cfg.imc_info.enable = IMC_ENABLE;
    aptx_dsp_cfg->abr_cfg.imc_info.purpose = IMC_PURPOSE_ID_BT_INFO;
    aptx_dsp_cfg->abr_cfg.imc_info.comm_instance = a2dp.abr_config.imc_instance;


    switch(aptx_dsp_cfg->aptx_ad_cfg.channel_mode) {
        case APTX_AD_CHANNEL_UNCHANGED:
        case APTX_AD_CHANNEL_JOINT_STEREO:
        case APTX_AD_CHANNEL_DUAL_MONO:
        case APTX_AD_CHANNEL_STEREO_TWS:
        case APTX_AD_CHANNEL_EARBUD:
        default:
             a2dp.enc_channels = CH_STEREO;
             aptx_dsp_cfg->custom_cfg.num_channels = CH_STEREO;
             aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
             aptx_dsp_cfg->custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
             break;
        case APTX_AD_CHANNEL_MONO:
             a2dp.enc_channels = CH_MONO;
             aptx_dsp_cfg->custom_cfg.num_channels = CH_MONO;
             aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            break;
    }
    switch(aptx_dsp_cfg->aptx_ad_cfg.sampling_freq) {
        case APTX_AD_SR_UNCHANGED:
        case APTX_AD_48:
        default:
            a2dp.enc_sampling_rate = SAMPLING_RATE_48K;
            aptx_dsp_cfg->custom_cfg.sample_rate = SAMPLING_RATE_48K;
            break;
        case APTX_AD_44_1:
            a2dp.enc_sampling_rate = SAMPLING_RATE_441K;
            aptx_dsp_cfg->custom_cfg.sample_rate = SAMPLING_RATE_441K;
        break;
    }
    ALOGV("Successfully updated APTX AD enc format with \
               samplingrate: %d channels:%d",
               aptx_dsp_cfg->custom_cfg.sample_rate,
               aptx_dsp_cfg->custom_cfg.num_channels);

    return ret;
}

static void audio_a2dp_update_tws_channel_mode()
{
    char* channel_mode;
    struct mixer_ctl *ctl_channel_mode;
    if (a2dp.is_tws_mono_mode_on)
       channel_mode = "One";
    else
       channel_mode = "Two";
    ctl_channel_mode = mixer_get_ctl_by_name(a2dp.adev->mixer,MIXER_FMT_TWS_CHANNEL_MODE);
    if (!ctl_channel_mode) {
         ALOGE("failed to get tws mixer ctl");
         return;
    }
    if (mixer_ctl_set_enum_by_string(ctl_channel_mode, channel_mode) != 0) {
         ALOGE("%s: Failed to set the channel mode = %s", __func__, channel_mode);
         return;
    }
}

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
    aptx_dsp_cfg->custom_cfg.enc_format = ENC_MEDIA_FMT_APTX;

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
            if (!a2dp.is_tws_mono_mode_on) {
               aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
               aptx_dsp_cfg->custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            }
            else {
               a2dp.is_tws_mono_mode_on = true;
               ALOGD("Update tws for mono_mode_on: %d",a2dp.is_tws_mono_mode_on);
            }
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
    aptx_dsp_cfg->enc_format = ENC_MEDIA_FMT_APTX;
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
    struct mixer_ctl *ctl_enc_data = NULL;
    int mixer_size;
    bool is_configured = false;
    int ret = 0;
    int sample_rate_backup;

    if(aptx_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR a2dp encoder CONFIG data mixer control not identifed");
        return false;
    }

#ifndef LINUX_ENABLED
    struct aptx_enc_cfg_t aptx_dsp_cfg;
    struct aptx_ad_enc_cfg_t aptx_ad_dsp_cfg;
    if(a2dp.is_aptx_adaptive) {
        mixer_size = sizeof(struct aptx_ad_enc_cfg_t);
        ret = update_aptx_ad_dsp_config(&aptx_ad_dsp_cfg, aptx_bt_cfg);
        sample_rate_backup = aptx_ad_dsp_cfg.custom_cfg.sample_rate;
    } else {
        mixer_size = sizeof(struct aptx_enc_cfg_t);
        sample_rate_backup = aptx_bt_cfg->default_cfg->sampling_rate;
        ret = update_aptx_dsp_config_v2(&aptx_dsp_cfg, aptx_bt_cfg);
    }
    if (ret) {
        is_configured = false;
        goto fail;
    }

    if(a2dp.is_aptx_adaptive) {
        ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_ad_dsp_cfg,
                              mixer_size);
    } else {
        ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                              mixer_size);
    }
#else
    struct custom_enc_cfg_t aptx_dsp_cfg;
    mixer_size = sizeof(struct custom_enc_cfg_t);
    sample_rate_backup = aptx_bt_cfg->sampling_rate;
    ret = update_aptx_dsp_config_v1(&aptx_dsp_cfg, aptx_bt_cfg);
    if (ret) {
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                          mixer_size);
#endif
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    if(a2dp.is_aptx_adaptive)
        ret = a2dp_set_bit_format(aptx_bt_cfg->ad_cfg->bits_per_sample);
    else if(a2dp.is_aptx_dual_mono_supported)
        ret = a2dp_set_bit_format(aptx_bt_cfg->dual_mono_cfg->bits_per_sample);
    else
        ret = a2dp_set_bit_format(aptx_bt_cfg->default_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    if (a2dp.is_aptx_adaptive)
        a2dp.bt_encoder_format = ENC_CODEC_TYPE_APTX_AD;
    else
        a2dp.bt_encoder_format = ENC_CODEC_TYPE_APTX;
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
    struct mixer_ctl *ctl_enc_data = NULL;
    struct custom_enc_cfg_t aptx_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if(aptx_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }

    memset(&aptx_dsp_cfg, 0x0, sizeof(struct custom_enc_cfg_t));
    aptx_dsp_cfg.enc_format = ENC_MEDIA_FMT_APTX_HD;
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
    ret = a2dp_set_bit_format(aptx_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_APTX_HD;
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
    struct mixer_ctl *ctl_enc_data = NULL;
    struct aac_enc_cfg_t aac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (aac_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    memset(&aac_dsp_cfg, 0x0, sizeof(struct aac_enc_cfg_t));
    aac_dsp_cfg.enc_format = ENC_MEDIA_FMT_AAC;
    aac_dsp_cfg.bit_rate = aac_bt_cfg->bitrate;
    aac_dsp_cfg.sample_rate = aac_bt_cfg->sampling_rate;
    switch (aac_bt_cfg->enc_mode) {
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
        ALOGE("%s: Failed to set AAC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ret = a2dp_set_bit_format(aac_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_AAC;
    a2dp.enc_sampling_rate = aac_bt_cfg->sampling_rate;
    a2dp.enc_channels = aac_bt_cfg->channels;
    ALOGV("%s: Successfully updated AAC enc format with sampling rate: %d channels:%d",
           __func__, aac_dsp_cfg.sample_rate, aac_dsp_cfg.channel_cfg);
fail:
    return is_configured;
}

bool configure_aac_enc_format_v2(audio_aac_encoder_config_v2 *aac_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL;
    struct aac_enc_cfg_v2_t aac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (aac_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    memset(&aac_dsp_cfg, 0x0, sizeof(struct aac_enc_cfg_v2_t));
    aac_dsp_cfg.aac_enc_cfg.enc_format = ENC_MEDIA_FMT_AAC;
    aac_dsp_cfg.aac_enc_cfg.bit_rate = aac_bt_cfg->audio_aac_enc_cfg.bitrate;
    aac_dsp_cfg.aac_enc_cfg.sample_rate = aac_bt_cfg->audio_aac_enc_cfg.sampling_rate;
    switch (aac_bt_cfg->audio_aac_enc_cfg.enc_mode) {
        case 0:
            aac_dsp_cfg.aac_enc_cfg.enc_mode = MEDIA_FMT_AAC_AOT_LC;
            break;
        case 2:
            aac_dsp_cfg.aac_enc_cfg.enc_mode = MEDIA_FMT_AAC_AOT_PS;
            break;
        case 1:
        default:
            aac_dsp_cfg.aac_enc_cfg.enc_mode = MEDIA_FMT_AAC_AOT_SBR;
            break;
    }
    aac_dsp_cfg.aac_enc_cfg.aac_fmt_flag = aac_bt_cfg->audio_aac_enc_cfg.format_flag;
    aac_dsp_cfg.aac_enc_cfg.channel_cfg = aac_bt_cfg->audio_aac_enc_cfg.channels;
    aac_dsp_cfg.frame_ctl.ctl_type = aac_bt_cfg->frame_ctl.ctl_type;
    aac_dsp_cfg.frame_ctl.ctl_value = aac_bt_cfg->frame_ctl.ctl_value;

    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aac_dsp_cfg,
                              sizeof(struct aac_enc_cfg_v2_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set AAC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ret = a2dp_set_bit_format(aac_bt_cfg->audio_aac_enc_cfg.bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_AAC;
    a2dp.enc_sampling_rate = aac_bt_cfg->audio_aac_enc_cfg.sampling_rate;
    a2dp.enc_channels = aac_bt_cfg->audio_aac_enc_cfg.channels;
    ALOGV("%s: Successfully updated AAC enc format with sampling rate: %d channels:%d",
           __func__, aac_dsp_cfg.aac_enc_cfg.sample_rate, aac_dsp_cfg.aac_enc_cfg.channel_cfg);
fail:
    return is_configured;
}

bool configure_celt_enc_format(audio_celt_encoder_config *celt_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL;
    struct celt_enc_cfg_t celt_dsp_cfg;
    bool is_configured = false;
    int ret = 0;
    if(celt_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    memset(&celt_dsp_cfg, 0x0, sizeof(struct celt_enc_cfg_t));

    celt_dsp_cfg.custom_cfg.enc_format = ENC_MEDIA_FMT_CELT;
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
    ret = a2dp_set_bit_format(celt_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_CELT;
    a2dp.enc_sampling_rate = celt_bt_cfg->sampling_rate;
    a2dp.enc_channels = celt_bt_cfg->channels;
    ALOGV("Successfully updated CELT encformat with samplingrate: %d channels:%d",
           celt_dsp_cfg.custom_cfg.sample_rate, celt_dsp_cfg.custom_cfg.num_channels);
fail:
    return is_configured;
}

bool configure_ldac_enc_format(audio_ldac_encoder_config *ldac_bt_cfg)
{
    struct mixer_ctl *ldac_enc_data = NULL;
    struct ldac_enc_cfg_t ldac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;
    if(ldac_bt_cfg == NULL)
        return false;

    ldac_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ldac_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    memset(&ldac_dsp_cfg, 0x0, sizeof(struct ldac_enc_cfg_t));

    ldac_dsp_cfg.custom_cfg.enc_format = ENC_MEDIA_FMT_LDAC;
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
    if (ldac_bt_cfg->is_abr_enabled) {
        ldac_dsp_cfg.abr_cfg.mapping_info = ldac_bt_cfg->level_to_bitrate_map;
        ldac_dsp_cfg.abr_cfg.imc_info.direction = IMC_RECEIVE;
        ldac_dsp_cfg.abr_cfg.imc_info.enable = IMC_ENABLE;
        ldac_dsp_cfg.abr_cfg.imc_info.purpose = IMC_PURPOSE_ID_BT_INFO;
        ldac_dsp_cfg.abr_cfg.imc_info.comm_instance = a2dp.abr_config.imc_instance;
        ldac_dsp_cfg.abr_cfg.is_abr_enabled = ldac_bt_cfg->is_abr_enabled;
    }

    ret = mixer_ctl_set_array(ldac_enc_data, (void *)&ldac_dsp_cfg,
                              sizeof(struct ldac_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set LDAC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ret = a2dp_set_bit_format(ldac_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_LDAC;
    a2dp.enc_sampling_rate = ldac_bt_cfg->sampling_rate;
    a2dp.enc_channels = ldac_dsp_cfg.custom_cfg.num_channels;
    a2dp.abr_config.is_abr_enabled = ldac_bt_cfg->is_abr_enabled;
    ALOGV("Successfully updated LDAC encformat with samplingrate: %d channels:%d",
           ldac_dsp_cfg.custom_cfg.sample_rate, ldac_dsp_cfg.custom_cfg.num_channels);
fail:
    return is_configured;
}

bool configure_a2dp_encoder_format()
{
    void *codec_info = NULL;
    uint8_t multi_cast = 0, num_dev = 1;
    enc_codec_t codec_type = ENC_CODEC_TYPE_INVALID;
    bool is_configured = false;
    audio_aptx_encoder_config aptx_encoder_cfg;

    if (!a2dp.audio_get_codec_config) {
        ALOGE(" a2dp handle is not identified, ignoring a2dp encoder config");
        return false;
    }
    ALOGD("configure_a2dp_encoder_format start");
    codec_info = a2dp.audio_get_codec_config(&multi_cast, &num_dev,
                               &codec_type);

    // ABR disabled by default for all codecs
    a2dp.abr_config.is_abr_enabled = false;
    a2dp.is_aptx_adaptive = false;

    switch(codec_type) {
        case ENC_CODEC_TYPE_SBC:
            ALOGD(" Received SBC encoder supported BT device");
            is_configured =
              configure_sbc_enc_format((audio_sbc_encoder_config *)codec_info);
            break;
        case ENC_CODEC_TYPE_APTX:
            ALOGD(" Received APTX encoder supported BT device");
#ifndef LINUX_ENABLED
            a2dp.is_aptx_dual_mono_supported = false;
            aptx_encoder_cfg.default_cfg = (audio_aptx_default_config *)codec_info;
#endif
            is_configured =
              configure_aptx_enc_format(&aptx_encoder_cfg);
            break;
        case ENC_CODEC_TYPE_APTX_HD:
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
        case ENC_CODEC_TYPE_APTX_DUAL_MONO:
            ALOGD(" Received APTX dual mono encoder supported BT device");
            a2dp.is_aptx_dual_mono_supported = true;
            if (a2dp.audio_is_tws_mono_mode_enable != NULL)
                a2dp.is_tws_mono_mode_on = a2dp.audio_is_tws_mono_mode_enable();
            aptx_encoder_cfg.dual_mono_cfg = (audio_aptx_dual_mono_config *)codec_info;
            is_configured =
              configure_aptx_enc_format(&aptx_encoder_cfg);
            break;
#endif
        case ENC_CODEC_TYPE_AAC:
            ALOGD(" Received AAC encoder supported BT device");
            bool is_aac_frame_ctl_enabled =
                    property_get_bool("persist.vendor.bt.aac_frm_ctl.enabled", false);
            is_configured = is_aac_frame_ctl_enabled ?
                  configure_aac_enc_format_v2((audio_aac_encoder_config_v2 *) codec_info) :
                  configure_aac_enc_format((audio_aac_encoder_config *) codec_info);
            break;
        case ENC_CODEC_TYPE_CELT:
            ALOGD(" Received CELT encoder supported BT device");
            is_configured =
              configure_celt_enc_format((audio_celt_encoder_config *)codec_info);
            break;
        case ENC_CODEC_TYPE_LDAC:
            ALOGD(" Received LDAC encoder supported BT device");
            if (!instance_id || instance_id > MAX_INSTANCE_ID)
                instance_id = MAX_INSTANCE_ID;
            a2dp.abr_config.imc_instance = instance_id--;
            is_configured =
                (configure_ldac_enc_format((audio_ldac_encoder_config *)codec_info) &&
                 configure_a2dp_decoder_format(ENC_CODEC_TYPE_LDAC));
            break;
         case ENC_CODEC_TYPE_APTX_AD:
             ALOGD(" Received APTX AD encoder supported BT device");
             if (!instance_id || instance_id > MAX_INSTANCE_ID)
                 instance_id = MAX_INSTANCE_ID;
              a2dp.abr_config.imc_instance = instance_id--;
              a2dp.abr_config.is_abr_enabled = true; // for APTX Adaptive ABR is Always on
              a2dp.is_aptx_adaptive = true;
              aptx_encoder_cfg.ad_cfg = (audio_aptx_ad_config *)codec_info;
              is_configured =
                (configure_aptx_enc_format(&aptx_encoder_cfg) &&
                 configure_a2dp_decoder_format(ENC_MEDIA_FMT_APTX_AD));
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

    if(!(a2dp.bt_lib_handle && a2dp.audio_start_stream
       && a2dp.audio_get_codec_config)) {
        ALOGE("a2dp handle is not identified, Ignoring start request");
        return -ENOSYS;
    }

    if(a2dp.a2dp_suspended == true) {
        //session will be restarted after suspend completion
        ALOGD("a2dp start requested during suspend state");
        return -ENOSYS;
    }

    if (!a2dp.a2dp_started && !a2dp.a2dp_total_active_session_request) {
        ALOGD("calling BT module stream start");
        /* This call indicates BT IPC lib to start playback */
        ret =  a2dp.audio_start_stream();
        ALOGE("BT controller start return = %d",ret);
        if (ret != 0 ) {
           ALOGE("BT controller start failed");
           a2dp.a2dp_started = false;
        } else {
           if(configure_a2dp_encoder_format() == true) {
                a2dp.a2dp_started = true;
                ret = 0;
                ALOGD("Start playback successful to BT library");
           } else {
                ALOGD(" unable to configure DSP encoder");
                a2dp.a2dp_started = false;
                ret = -ETIMEDOUT;
           }
        }
    }

    if (a2dp.a2dp_started) {
        a2dp.a2dp_total_active_session_request++;
        a2dp_check_and_set_scrambler();
        audio_a2dp_update_tws_channel_mode();
        a2dp_set_backend_cfg();
        if (a2dp.abr_config.is_abr_enabled)
            start_abr();
    }

    ALOGD("start A2DP playback total active sessions :%d",
          a2dp.a2dp_total_active_session_request);
    return ret;
}

static void reset_a2dp_enc_config_params()
{
    int ret =0;

    struct mixer_ctl *ctl_enc_config, *ctrl_bit_format, *ctl_channel_mode;
    struct sbc_enc_cfg_t dummy_reset_config;
    char* channel_mode;

    memset(&dummy_reset_config, 0x0, sizeof(struct sbc_enc_cfg_t));
    ctl_enc_config = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                           MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_config) {
        ALOGE(" ERROR  a2dp encoder format mixer control not identifed");
    } else {
        ret = mixer_ctl_set_array(ctl_enc_config, (void *)&dummy_reset_config,
                                        sizeof(struct sbc_enc_cfg_t));
         a2dp.bt_encoder_format = ENC_MEDIA_FMT_NONE;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR  bit format CONFIG data mixer control not identifed");
    } else {
        ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
        if (ret != 0) {
            ALOGE("%s: Failed to set bit format to encoder", __func__);
        }
    }
    ctl_channel_mode = mixer_get_ctl_by_name(a2dp.adev->mixer,MIXER_FMT_TWS_CHANNEL_MODE);

    if (!ctl_channel_mode) {
        ALOGE("failed to get tws mixer ctl");
    } else {
        channel_mode = "Two";
        if (mixer_ctl_set_enum_by_string(ctl_channel_mode, channel_mode) != 0) {
            ALOGE("%s: Failed to set the channel mode = %s", __func__, channel_mode);
        }
        a2dp.is_tws_mono_mode_on = false;
    }
}

static int reset_a2dp_dec_config_params()
{
    struct mixer_ctl *ctl_dec_data = NULL;
    struct abr_dec_cfg_t dummy_reset_cfg;
    int ret = 0;

    if (a2dp.abr_config.is_abr_enabled) {
        ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_DEC_CONFIG_BLOCK);
        if (!ctl_dec_data) {
            ALOGE("%s: ERROR A2DP decoder config mixer control not identifed", __func__);
            return -EINVAL;
        }
        memset(&dummy_reset_cfg, 0x0, sizeof(dummy_reset_cfg));
        ret = mixer_ctl_set_array(ctl_dec_data, (void *)&dummy_reset_cfg,
                                  sizeof(dummy_reset_cfg));
        if (ret != 0) {
            ALOGE("%s: Failed to set dummy decoder config", __func__);
            return ret;
        }
    }

    return ret;
}

static void reset_a2dp_config() {
    reset_a2dp_enc_config_params();
    reset_a2dp_dec_config_params();
    a2dp_reset_backend_cfg();
    if (a2dp.abr_config.is_abr_enabled && a2dp.abr_config.abr_started)
        stop_abr();
    a2dp.abr_config.is_abr_enabled = false;
}

int audio_extn_a2dp_stop_playback()
{
    int ret =0;

    ALOGV("audio_extn_a2dp_stop_playback start");
    if(!(a2dp.bt_lib_handle && a2dp.audio_stop_stream)) {
        ALOGE("a2dp handle is not identified, Ignoring start request");
        return -ENOSYS;
    }

    if (a2dp.a2dp_total_active_session_request > 0)
        a2dp.a2dp_total_active_session_request--;

    if ( a2dp.a2dp_started && !a2dp.a2dp_total_active_session_request) {
        ALOGV("calling BT module stream stop");
        ret = a2dp.audio_stop_stream();
        if (ret < 0)
            ALOGE("stop stream to BT IPC lib failed");
        else
            ALOGV("stop stream to BT IPC lib successful");
        if (!a2dp.a2dp_suspended)
            reset_a2dp_config();
        a2dp.a2dp_started = false;
    }
    if(!a2dp.a2dp_total_active_session_request)
       a2dp.a2dp_started = false;
    ALOGD("Stop A2DP playback total active sessions :%d",
          a2dp.a2dp_total_active_session_request);
    return 0;
}

void audio_extn_a2dp_set_parameters(struct str_parms *parms)
{
     int ret, val;
     char value[32]={0};
     struct audio_usecase *uc_info;
     struct listnode *node;

     if(a2dp.is_a2dp_offload_supported == false) {
        ALOGV("no supported encoders identified,ignoring a2dp setparam");
        return;
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value,
                            sizeof(value));
     if (ret >= 0) {
         val = atoi(value);
         if (audio_is_a2dp_out_device(val)) {
             ALOGV("Received device connect request for A2DP");
             open_a2dp_output();
         }
         goto param_handled;
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                         sizeof(value));

     if (ret >= 0) {
         val = atoi(value);
         if (audio_is_a2dp_out_device(val)) {
             ALOGV("Received device dis- connect request");
             reset_a2dp_enc_config_params();
             reset_a2dp_dec_config_params();
             close_a2dp_output();
             a2dp_reset_backend_cfg();
         }
         goto param_handled;
     }

     ret = str_parms_get_str(parms, "TwsChannelConfig", value, sizeof(value));
     if (ret>=0) {
         ALOGD("Setting tws channel mode to %s",value);
         if(!(strncmp(value,"mono",strlen(value))))
            a2dp.is_tws_mono_mode_on = true;
         else if(!(strncmp(value,"dual-mono",strlen(value))))
            a2dp.is_tws_mono_mode_on = false;
         audio_a2dp_update_tws_channel_mode();
     goto param_handled;
     }

     ret = str_parms_get_str(parms, "A2dpSuspended", value, sizeof(value));
     if (ret >= 0) {
         if (a2dp.bt_lib_handle) {
             if ((!strncmp(value,"true",sizeof(value)))) {
                ALOGD("Setting a2dp to suspend state");
                a2dp.a2dp_suspended = true;
                if (a2dp.bt_state == A2DP_STATE_DISCONNECTED)
                    goto param_handled;
                list_for_each(node, &a2dp.adev->usecase_list) {
                    uc_info = node_to_item(node, struct audio_usecase, list);
                    if (uc_info->type == PCM_PLAYBACK &&
                         (uc_info->stream.out->devices & AUDIO_DEVICE_OUT_ALL_A2DP)) {
                        pthread_mutex_unlock(&a2dp.adev->lock);
                        check_a2dp_restore(a2dp.adev, uc_info->stream.out, false);
                        pthread_mutex_lock(&a2dp.adev->lock);
                    }
                }
                reset_a2dp_config();
                if (a2dp.audio_suspend_stream)
                   a2dp.audio_suspend_stream();
            } else if (a2dp.a2dp_suspended == true) {
                ALOGD("Resetting a2dp suspend state");
                struct audio_usecase *uc_info;
                struct listnode *node;
                if (a2dp.clear_a2dpsuspend_flag)
                    a2dp.clear_a2dpsuspend_flag();
                a2dp.a2dp_suspended = false;
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
                if (a2dp.a2dp_total_active_session_request > 0) {
                    ALOGD(" Calling IPC lib start post suspend state");
                    if(a2dp.audio_start_stream) {
                        ret =  a2dp.audio_start_stream();
                        if (ret != 0) {
                            ALOGE("BT controller start failed");
                            a2dp.a2dp_started = false;
                        } else {
                            if (!configure_a2dp_encoder_format()) {
                                ALOGE("%s: Encoder params configuration failed post suspend", __func__);
                                a2dp.a2dp_started = false;
                                ret = -ETIMEDOUT;
                            }
                        }
                    }
                    if (a2dp.a2dp_started) {
                        a2dp_set_backend_cfg();
                        if (a2dp.abr_config.is_abr_enabled)
                            start_abr();
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
    return a2dp.is_handoff_in_progress || !a2dp.a2dp_started;
}

void audio_extn_a2dp_get_sample_rate(int *sample_rate)
{
    *sample_rate = a2dp.enc_sampling_rate;
}

bool audio_extn_a2dp_is_ready()
{
    bool ret = false;

    if (a2dp.a2dp_suspended)
        return ret;

    if ((a2dp.bt_state != A2DP_STATE_DISCONNECTED) &&
        (a2dp.is_a2dp_offload_supported) &&
        (a2dp.audio_check_a2dp_ready))
           ret = a2dp.audio_check_a2dp_ready();
    return ret;
}

bool audio_extn_a2dp_is_suspended()
{
    return a2dp.a2dp_suspended;
}

void audio_extn_a2dp_init (void *adev)
{
  a2dp.adev = (struct audio_device*)adev;
  a2dp.bt_lib_handle = NULL;
  a2dp.a2dp_started = false;
  a2dp.bt_state = A2DP_STATE_DISCONNECTED;
  a2dp.a2dp_total_active_session_request = 0;
  a2dp.a2dp_suspended = false;
  a2dp.bt_encoder_format = ENC_CODEC_TYPE_INVALID;
  a2dp.enc_sampling_rate = 48000;
  a2dp.is_a2dp_offload_supported = false;
  a2dp.is_handoff_in_progress = false;
  a2dp.is_aptx_dual_mono_supported = false;
  a2dp.is_aptx_adaptive = false;
  a2dp.abr_config.is_abr_enabled = false;
  a2dp.abr_config.abr_started = false;
  a2dp.abr_config.imc_instance = 0;
  a2dp.abr_config.abr_tx_handle = NULL;
  a2dp.is_tws_mono_mode_on = false;
  reset_a2dp_enc_config_params();
  reset_a2dp_dec_config_params();
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
    if (a2dp.audio_get_a2dp_sink_latency && a2dp.bt_state != A2DP_STATE_DISCONNECTED) {
        slatency = a2dp.audio_get_a2dp_sink_latency();
    }

    switch(a2dp.bt_encoder_format) {
        case ENC_CODEC_TYPE_SBC:
            latency = (avsync_runtime_prop > 0) ? sbc_offset : ENCODER_LATENCY_SBC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_SBC : slatency;
            break;
        case ENC_CODEC_TYPE_APTX:
            latency = (avsync_runtime_prop > 0) ? aptx_offset : ENCODER_LATENCY_APTX;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_APTX : slatency;
            break;
        case ENC_CODEC_TYPE_APTX_HD:
            latency = (avsync_runtime_prop > 0) ? aptxhd_offset : ENCODER_LATENCY_APTX_HD;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_APTX_HD : slatency;
            break;
        case ENC_CODEC_TYPE_AAC:
            latency = (avsync_runtime_prop > 0) ? aac_offset : ENCODER_LATENCY_AAC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_AAC : slatency;
            break;
        case ENC_CODEC_TYPE_CELT:
            latency = (avsync_runtime_prop > 0) ? celt_offset : ENCODER_LATENCY_CELT;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_CELT : slatency;
            break;
        case ENC_CODEC_TYPE_LDAC:
            latency = (avsync_runtime_prop > 0) ? ldac_offset : ENCODER_LATENCY_LDAC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_LDAC : slatency;
            break;
        case ENC_CODEC_TYPE_APTX_AD: // for aptx adaptive the latency depends on the mode (HQ/LL) and
            latency = slatency;      // BT IPC will take care of accomodating the mode factor and return latency
            break;
        default:
            latency = 200;
            break;
    }
    return latency;
}
#endif // SPLIT_A2DP_ENABLED
