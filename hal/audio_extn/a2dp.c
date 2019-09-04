/*
 * Copyright (C) 2013-2018 The Android Open Source Project
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

#define LOG_TAG "a2dp_offload"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <hardware/audio.h>

#include "audio_hw.h"
#include "audio_extn.h"
#include "platform_api.h"

#ifdef A2DP_OFFLOAD_ENABLED
#define BT_IPC_LIB_NAME  "libbthost_if.so"
#define BTAUDIO_OFFLOAD_LIB_NAME  "btaudio_offload_if.so"

// Media format definitions
#define ENC_MEDIA_FMT_AAC                                  0x00010DA6
#define ENC_MEDIA_FMT_APTX                                 0x000131ff
#define ENC_MEDIA_FMT_APTX_HD                              0x00013200
#define ENC_MEDIA_FMT_LDAC                                 0x00013224
#define ENC_MEDIA_FMT_SBC                                  0x00010BF2
#define ENC_MEDIA_FMT_NONE                                 0
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS           0
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR                1
#define MEDIA_FMT_AAC_AOT_LC                               2
#define MEDIA_FMT_AAC_AOT_SBR                              5
#define MEDIA_FMT_AAC_AOT_PS                               29
#define MEDIA_FMT_SBC_CHANNEL_MODE_MONO                    1
#define MEDIA_FMT_SBC_CHANNEL_MODE_STEREO                  2
#define MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO               8
#define MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO            9

// PCM channels
#define PCM_CHANNEL_L                                      1
#define PCM_CHANNEL_R                                      2
#define PCM_CHANNEL_C                                      3

// Mixer controls sent to ALSA
#define MIXER_ENC_CONFIG_BLOCK     "SLIM_7_RX Encoder Config"
#define MIXER_DEC_CONFIG_BLOCK     "SLIM_7_TX Decoder Config"
#define MIXER_ENC_BIT_FORMAT       "AFE Input Bit Format"
#define MIXER_SCRAMBLER_MODE       "AFE Scrambler Mode"
#define MIXER_SAMPLE_RATE_RX       "BT SampleRate RX"
#define MIXER_SAMPLE_RATE_TX       "BT SampleRate TX"
#define MIXER_AFE_IN_CHANNELS      "AFE Input Channels"
#define MIXER_ABR_TX_FEEDBACK_PATH "A2DP_SLIM7_UL_HL Switch"
#define MIXER_SET_FEEDBACK_CHANNEL "BT set feedback channel"

// Encoder format strings
#define ENC_FMT_AAC                "aac"
#define ENC_FMT_APTX               "aptx"
#define ENC_FMT_APTXHD             "aptxhd"
#define ENC_FMT_LDAC               "ldac"
#define ENC_FMT_SBC                "sbc"

// System properties used for A2DP Offload
#define SYSPROP_A2DP_OFFLOAD_SUPPORTED "ro.bluetooth.a2dp_offload.supported"
#define SYSPROP_A2DP_OFFLOAD_DISABLED  "persist.bluetooth.a2dp_offload.disabled"
#define SYSPROP_BLUETOOTH_AUDIO_HAL_DISABLED  "persist.bluetooth.bluetooth_audio_hal.disabled"
#define SYSPROP_A2DP_CODEC_LATENCIES   "vendor.audio.a2dp.codec.latency"

// Default encoder bit width
#define DEFAULT_ENCODER_BIT_FORMAT 16

// Default encoder latency
#define DEFAULT_ENCODER_LATENCY    200

// Encoder latency offset for codecs supported
#define ENCODER_LATENCY_AAC        70
#define ENCODER_LATENCY_APTX       40
#define ENCODER_LATENCY_APTX_HD    20
#define ENCODER_LATENCY_LDAC       40
#define ENCODER_LATENCY_SBC        10
#define ENCODER_LATENCY_PCM        50

// Default A2DP sink latency offset
#define DEFAULT_SINK_LATENCY_AAC       180
#define DEFAULT_SINK_LATENCY_APTX      160
#define DEFAULT_SINK_LATENCY_APTX_HD   180
#define DEFAULT_SINK_LATENCY_LDAC      180
#define DEFAULT_SINK_LATENCY_SBC       140
#define DEFAULT_SINK_LATENCY_PCM       140

// Slimbus Tx sample rate for ABR feedback channel
#define ABR_TX_SAMPLE_RATE             "KHZ_8"

// Purpose ID for Inter Module Communication (IMC) in AFE
#define IMC_PURPOSE_ID_BT_INFO         0x000132E2

// Maximum quality levels for ABR
#define MAX_ABR_QUALITY_LEVELS             5

// Instance identifier for A2DP
#define MAX_INSTANCE_ID                (UINT32_MAX / 2)

/*
 * Below enum values are extended from audio-base.h to
 * keep encoder codec type local to bthost_ipc
 * and audio_hal as these are intended only for handshake
 * between IPC lib and Audio HAL.
 */
typedef enum {
    ENC_CODEC_TYPE_INVALID = AUDIO_FORMAT_INVALID, // 0xFFFFFFFFUL
    ENC_CODEC_TYPE_AAC = AUDIO_FORMAT_AAC, // 0x04000000UL
    ENC_CODEC_TYPE_SBC = AUDIO_FORMAT_SBC, // 0x1F000000UL
    ENC_CODEC_TYPE_APTX = AUDIO_FORMAT_APTX, // 0x20000000UL
    ENC_CODEC_TYPE_APTX_HD = AUDIO_FORMAT_APTX_HD, // 0x21000000UL
    ENC_CODEC_TYPE_LDAC = AUDIO_FORMAT_LDAC, // 0x23000000UL
    ENC_CODEC_TYPE_PCM = AUDIO_FORMAT_PCM_16_BIT, // 0x1u
} enc_codec_t;

typedef int (*audio_stream_open_t)(void);
typedef int (*audio_stream_close_t)(void);
typedef int (*audio_stream_start_t)(void);
typedef int (*audio_stream_stop_t)(void);
typedef int (*audio_stream_suspend_t)(void);
typedef void (*audio_handoff_triggered_t)(void);
typedef void (*clear_a2dp_suspend_flag_t)(void);
typedef void * (*audio_get_codec_config_t)(uint8_t *multicast_status, uint8_t *num_dev,
                               enc_codec_t *codec_type);
typedef int (*audio_check_a2dp_ready_t)(void);
typedef int (*audio_is_scrambling_enabled_t)(void);
typedef uint16_t (*audio_get_a2dp_sink_latency_t)(void);

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

/* Data structure used to:
 * - Update the A2DP state machine
 * - Communicate with the libbthost_if.so IPC library
 * - Store DSP encoder configuration information
 */
struct a2dp_data {
    /* Audio device handle */
    struct audio_device *adev;
    /* Bluetooth IPC library handle */
    void *bt_lib_handle;
    /* Open A2DP audio stream. Initialize audio datapath */
    audio_stream_open_t audio_stream_open;
    /* Close A2DP audio stream */
    audio_stream_close_t audio_stream_close;
    /* Start A2DP audio stream. Start audio datapath */
    audio_stream_start_t audio_stream_start;
    /* Stop A2DP audio stream */
    audio_stream_stop_t audio_stream_stop;
    /* Suspend A2DP audio stream */
    audio_stream_suspend_t audio_stream_suspend;
    /* Notify Bluetooth IPC library of handoff being triggered */
    audio_handoff_triggered_t audio_handoff_triggered;
    /* Clear A2DP suspend flag in Bluetooth IPC library */
    clear_a2dp_suspend_flag_t clear_a2dp_suspend_flag;
    /* Get codec configuration from Bluetooth stack via
     * Bluetooth IPC library */
    audio_get_codec_config_t audio_get_codec_config;
    /* Check if A2DP is ready */
    audio_check_a2dp_ready_t audio_check_a2dp_ready;
    /* Check if scrambling is enabled on BTSoC */
    audio_is_scrambling_enabled_t audio_is_scrambling_enabled;
    /* Get sink latency from Bluetooth stack */
    audio_get_a2dp_sink_latency_t audio_get_a2dp_sink_latency;
    /* Internal A2DP state identifier */
    enum A2DP_STATE bt_state;
    /* A2DP codec type configured */
    enc_codec_t bt_encoder_format;
    /* Sampling rate configured with A2DP encoder on DSP */
    uint32_t enc_sampling_rate;
    /* Channel configuration of A2DP on DSP */
    uint32_t enc_channels;
    /* Flag to denote whether A2DP audio datapath has started */
    bool a2dp_started;
    /* Flag to denote whether A2DP audio datapath is suspended */
    bool a2dp_suspended;
    /* Number of active sessions on A2DP output */
    int  a2dp_total_active_session_request;
    /* Flag to denote whether A2DP offload is enabled */
    bool is_a2dp_offload_enabled;
    /* Flag to denote whether codec reconfiguration/soft handoff is in progress */
    bool is_handoff_in_progress;
    /* Flag to denote whether APTX Dual Mono encoder is supported */
    bool is_aptx_dual_mono_supported;
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

struct aac_cfg_blk_t {
    /* Encoder media format for AAC */
    uint32_t      enc_format;

    /* Encoding rate in bits per second */
    uint32_t      bit_rate;

   /* supported enc_mode are AAC_LC, AAC_SBR, AAC_PS */
    uint32_t      enc_mode;

    /* supported aac_fmt_flag are ADTS/RAW */
    uint16_t      aac_fmt_flag;

    /* supported channel_cfg are Native mode, Mono , Stereo */
    uint16_t      channel_cfg;

    /* Number of samples per second */
    uint32_t      sample_rate;
} __attribute__ ((packed));

/* AAC encoder configuration structure. */
typedef struct aac_enc_cfg_t aac_enc_cfg_t;

struct aac_enc_cfg_t {
    struct aac_cfg_blk_t aac_cfg;
    struct aac_frame_size_control_t frame_ctl;
} __attribute__ ((packed));

/* SBC encoder configuration structure. */
typedef struct sbc_enc_cfg_t sbc_enc_cfg_t;

struct sbc_enc_cfg_t {
    /* Encoder media format for SBC */
    uint32_t      enc_format;

    /* supported num_subbands are 4/8 */
    uint32_t      num_subbands;

    /* supported blk_len are 4, 8, 12, 16 */
    uint32_t      blk_len;

    /* supported channel_mode are MONO, STEREO, DUAL_MONO, JOINT_STEREO */
    uint32_t      channel_mode;

    /* supported alloc_method are LOUNDNESS/SNR */
    uint32_t      alloc_method;

    /* supported bit_rate for mono channel is max 320kbps
     * supported bit rate for stereo channel is max 512 kbps */
    uint32_t      bit_rate;

    /* Number of samples per second */
    uint32_t      sample_rate;
} __attribute__ ((packed));

struct custom_enc_cfg_t {
    /* Custom encoder media format */
    uint32_t      enc_format;

    /* Number of samples per second */
    uint32_t      sample_rate;

    /* supported num_channels are Mono/Stereo */
    uint16_t      num_channels;

    /* Reserved for future enhancement */
    uint16_t      reserved;

    /* supported channel_mapping for mono is CHANNEL_C
     * supported channel mapping for stereo is CHANNEL_L and CHANNEL_R */
    uint8_t       channel_mapping[8];

    /* Reserved for future enhancement */
    uint32_t      custom_size;
} __attribute__ ((packed));

struct aptx_v2_enc_cfg_ext_t {
/* sync_mode introduced with APTX V2 libraries
 * sync mode: 0x0 = stereo sync mode
 *            0x01 = dual mono sync mode
 *            0x02 = dual mono with no sync on either L or R codewords
 */
    uint32_t       sync_mode;
} __attribute__ ((packed));

/* APTX struct for combining custom enc and V2 members */
struct aptx_enc_cfg_t {
    struct custom_enc_cfg_t  custom_cfg;
    struct aptx_v2_enc_cfg_ext_t aptx_v2_cfg;
} __attribute__ ((packed));

struct ldac_specific_enc_cfg_t {
   /*
    * This is used to calculate the encoder output
    * bytes per frame (i.e. bytes per packet).
    * Bit rate also configures the EQMID.
    * The min bit rate 303000 bps is calculated for
    * 44.1 kHz and 88.2 KHz sampling frequencies with
    * Mobile use Quality.
    * The max bit rate of 990000 bps is calculated for
    * 96kHz and 48 KHz with High Quality
    * @Range(in bits per second)
    * 303000 for Mobile use Quality
    * 606000 for standard Quality
    * 909000 for High Quality
    */
    uint32_t      bit_rate;

   /*
    * The channel setting information for LDAC specification
    * of Bluetooth A2DP which is determined by SRC and SNK
    * devices in Bluetooth transmission.
    * @Range:
    * 0 for native mode
    * 4 for mono
    * 2 for dual channel
    * 1 for stereo
    */
    uint16_t      channel_mode;

   /*
    * Maximum Transmission Unit (MTU).
    * The minimum MTU that a L2CAP implementation for LDAC shall
    * support is 679 bytes, because LDAC is optimized with 2-DH5
    * packet as its target.
    * @Range : 679
    * @Default: 679 for LDACBT_MTU_2DH5
    */
    uint16_t      mtu;
} __attribute__ ((packed));

/* LDAC struct for combining custom enc and standard members */
struct ldac_enc_cfg_t {
    struct custom_enc_cfg_t  custom_cfg;
    struct ldac_specific_enc_cfg_t ldac_cfg;
    struct abr_enc_cfg_t abr_cfg;
} __attribute__ ((packed));

/* Information about Bluetooth SBC encoder configuration
 * This data is used between audio HAL module and
 * Bluetooth IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t subband;          /* 4, 8 */
    uint32_t blk_len;          /* 4, 8, 12, 16 */
    uint16_t sampling_rate;    /* 44.1khz, 48khz */
    uint8_t  channels;         /* 0(Mono), 1(Dual_mono), 2(Stereo), 3(JS) */
    uint8_t  alloc;            /* 0(Loudness), 1(SNR) */
    uint8_t  min_bitpool;      /* 2 */
    uint8_t  max_bitpool;      /* 53(44.1khz), 51 (48khz) */
    uint32_t bitrate;          /* 320kbps to 512kbps */
    uint32_t bits_per_sample;  /* 16 bit */
} audio_sbc_encoder_config;

/* Information about Bluetooth APTX encoder configuration
 * This data is used between audio HAL module and
 * Bluetooth IPC library to configure DSP encoder
 */
typedef struct {
    uint16_t sampling_rate;
    uint8_t  channels;
    uint32_t bitrate;
    uint32_t bits_per_sample;
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

/* Information about Bluetooth AAC encoder configuration
 * This data is used between audio HAL module and
 * Bluetooth IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t enc_mode; /* LC, SBR, PS */
    uint16_t format_flag; /* RAW, ADTS */
    uint16_t channels; /* 1-Mono, 2-Stereo */
    uint32_t sampling_rate;
    uint32_t bitrate;
    uint32_t bits_per_sample;
    struct aac_frame_size_control_t frame_ctl;
} audio_aac_encoder_config;

/* Information about Bluetooth LDAC encoder configuration
 * This data is used between audio HAL module and
 * Bluetooth IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t sampling_rate; /* 44100, 48000, 88200, 96000 */
    uint32_t bit_rate; /* 303000, 606000, 909000 (in bits per second) */
    uint16_t channel_mode; /* 0, 4, 2, 1 */
    uint16_t mtu;
    uint32_t bits_per_sample; /* 16, 24, 32 (bits) */
    bool is_abr_enabled;
    struct quality_level_to_bitrate_info level_to_bitrate_map;
} audio_ldac_encoder_config;

/*********** END of DSP configurable structures ********************/

static void a2dp_common_init()
{
    a2dp.a2dp_started = false;
    a2dp.a2dp_total_active_session_request = 0;
    a2dp.a2dp_suspended = false;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_INVALID;
    a2dp.bt_state = A2DP_STATE_DISCONNECTED;
    a2dp.abr_config.is_abr_enabled = false;
    a2dp.abr_config.abr_started = false;
    a2dp.abr_config.imc_instance = 0;
    a2dp.abr_config.abr_tx_handle = NULL;
}

static void update_offload_codec_support()
{
    a2dp.is_a2dp_offload_enabled =
            property_get_bool(SYSPROP_A2DP_OFFLOAD_SUPPORTED, false) &&
            !property_get_bool(SYSPROP_A2DP_OFFLOAD_DISABLED, false);

    ALOGD("%s: A2DP offload enabled = %d", __func__,
          a2dp.is_a2dp_offload_enabled);
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

/* API to open Bluetooth IPC library to start IPC communication */
static int open_a2dp_output()
{
    int ret = 0;
    ALOGD("%s: Open A2DP output start", __func__);
    bool hal_v2_enabled =
              !property_get_bool(SYSPROP_BLUETOOTH_AUDIO_HAL_DISABLED, false);

    if (a2dp.bt_state != A2DP_STATE_DISCONNECTED) {
        ALOGD("%s: Called A2DP open with improper state, Ignoring request state %d",
                   __func__, a2dp.bt_state);
        return -ENOSYS;
    }

    if (a2dp.bt_lib_handle == NULL) {
        ALOGD("%s: Requesting for Bluetooth IPC lib handle [%s]", __func__,
              hal_v2_enabled ? BTAUDIO_OFFLOAD_LIB_NAME : BT_IPC_LIB_NAME);
        if (hal_v2_enabled) {
           a2dp.bt_lib_handle = dlopen(BTAUDIO_OFFLOAD_LIB_NAME, RTLD_NOW);
        } else {
           a2dp.bt_lib_handle = dlopen(BT_IPC_LIB_NAME, RTLD_NOW);
        }
        if (a2dp.bt_lib_handle == NULL) {
            ret = -errno;
            ALOGE("%s: DLOPEN failed for %s errno %d strerror %s", __func__,
                      BT_IPC_LIB_NAME, errno, strerror(errno));
            a2dp.bt_state = A2DP_STATE_DISCONNECTED;
            return ret;
        } else {
            a2dp.audio_stream_open = (audio_stream_open_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stream_open");
            a2dp.audio_stream_start = (audio_stream_start_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stream_start");
            a2dp.audio_get_codec_config = (audio_get_codec_config_t)
                          dlsym(a2dp.bt_lib_handle, "audio_get_codec_config");
            a2dp.audio_stream_suspend = (audio_stream_suspend_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stream_suspend");
            a2dp.audio_handoff_triggered = (audio_handoff_triggered_t)
                          dlsym(a2dp.bt_lib_handle, "audio_handoff_triggered");
            a2dp.clear_a2dp_suspend_flag = (clear_a2dp_suspend_flag_t)
                          dlsym(a2dp.bt_lib_handle, "clear_a2dp_suspend_flag");
            a2dp.audio_stream_stop = (audio_stream_stop_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stream_stop");
            a2dp.audio_stream_close = (audio_stream_close_t)
                          dlsym(a2dp.bt_lib_handle, "audio_stream_close");
            a2dp.audio_check_a2dp_ready = (audio_check_a2dp_ready_t)
                        dlsym(a2dp.bt_lib_handle,"audio_check_a2dp_ready");
            a2dp.audio_is_scrambling_enabled = (audio_is_scrambling_enabled_t)
                        dlsym(a2dp.bt_lib_handle,"audio_is_scrambling_enabled");
            a2dp.audio_get_a2dp_sink_latency = (audio_get_a2dp_sink_latency_t)
                        dlsym(a2dp.bt_lib_handle,"audio_get_a2dp_sink_latency");
        }
    }

    if (a2dp.bt_lib_handle && a2dp.audio_stream_open) {
        ALOGD("%s: calling Bluetooth stream open", __func__);
        ret = a2dp.audio_stream_open();
        if (ret != 0) {
            ALOGE("%s: Failed to open output stream for A2DP: status %d", __func__, ret);
            dlclose(a2dp.bt_lib_handle);
            a2dp.bt_lib_handle = NULL;
            a2dp.bt_state = A2DP_STATE_DISCONNECTED;
            return ret;
        }
        a2dp.bt_state = A2DP_STATE_CONNECTED;
    } else {
        ALOGE("%s: A2DP handle is not identified, Ignoring open request", __func__);
        a2dp.bt_state = A2DP_STATE_DISCONNECTED;
        return -ENOSYS;
    }

    return ret;
}

static int close_a2dp_output()
{
    ALOGV("%s\n",__func__);
    if (!(a2dp.bt_lib_handle && a2dp.audio_stream_close)) {
        ALOGE("%s: A2DP handle is not identified, Ignoring close request", __func__);
        return -ENOSYS;
    }
    if (a2dp.bt_state != A2DP_STATE_DISCONNECTED) {
        ALOGD("%s: calling Bluetooth stream close", __func__);
        if (a2dp.audio_stream_close() == false)
            ALOGE("%s: failed close A2DP control path from Bluetooth IPC library", __func__);
    }
    a2dp_common_init();
    a2dp.enc_sampling_rate = 0;
    a2dp.enc_channels = 0;

    return 0;
}

static int a2dp_check_and_set_scrambler()
{
    bool scrambler_mode = false;
    struct mixer_ctl *ctrl_scrambler_mode = NULL;
    int ret = 0;
    if (a2dp.audio_is_scrambling_enabled && (a2dp.bt_state != A2DP_STATE_DISCONNECTED))
        scrambler_mode = a2dp.audio_is_scrambling_enabled();

    // Scrambling needs to be enabled in DSP if scrambler mode is set
    // disable scrambling not required
    if (scrambler_mode) {
        // enable scrambler in dsp
        ctrl_scrambler_mode = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SCRAMBLER_MODE);
        if (!ctrl_scrambler_mode) {
            ALOGE("%s: ERROR scrambler mode mixer control not identifed", __func__);
            return -ENOSYS;
        } else {
            ret = mixer_ctl_set_value(ctrl_scrambler_mode, 0, true);
            if (ret != 0) {
                ALOGE("%s: Could not set scrambler mode", __func__);
                return ret;
            }
        }
    }
    return 0;
}

static int a2dp_set_backend_cfg()
{
    const char *rate_str = NULL, *in_channels = NULL;
    uint32_t sampling_rate_rx = a2dp.enc_sampling_rate;
    struct mixer_ctl *ctl_sample_rate = NULL, *ctrl_in_channels = NULL;

    // For LDAC encoder open slimbus port at 96Khz for 48Khz input
    // and 88.2Khz for 44.1Khz input.
    if ((a2dp.bt_encoder_format == ENC_CODEC_TYPE_LDAC) &&
        (sampling_rate_rx == 48000 || sampling_rate_rx == 44100 )) {
        sampling_rate_rx *= 2;
    }
    // No need to configure backend for PCM format.
    if (a2dp.bt_encoder_format == ENC_CODEC_TYPE_PCM) {
        return 0;
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

    ALOGV("%s: set backend rx sample rate = %s", __func__, rate_str);
    ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SAMPLE_RATE_RX);
    if (!ctl_sample_rate) {
        ALOGE("%s: ERROR backend sample rate mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
        ALOGE("%s: Failed to set backend sample rate = %s", __func__, rate_str);
        return -ENOSYS;
    }

    // Set Tx backend sample rate
    if (a2dp.abr_config.is_abr_enabled) {
        rate_str = ABR_TX_SAMPLE_RATE;

        ALOGV("%s: set backend tx sample rate = %s", __func__, rate_str);
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SAMPLE_RATE_TX);
        if (!ctl_sample_rate) {
            ALOGE("%s: ERROR backend sample rate mixer control not identifed", __func__);
            return -ENOSYS;
        }
        if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
            ALOGE("%s: Failed to set backend sample rate = %s",
                                        __func__, rate_str);
            return -ENOSYS;
        }
    }

    // Configure AFE input channels
    switch (a2dp.enc_channels) {
    case 1:
        in_channels = "One";
        break;
    case 2:
    default:
        in_channels = "Two";
        break;
    }

    ALOGV("%s: set AFE input channels = %d", __func__, a2dp.enc_channels);
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
    ALOGV("%s: reset backend sample rate = %s", __func__, rate_str);
    ctl_sample_rate_rx = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SAMPLE_RATE_RX);
    if (!ctl_sample_rate_rx) {
        ALOGE("%s: ERROR Rx backend sample rate mixer control not identifed", __func__);
        return -ENOSYS;
    }
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

    // Reset AFE input channels
    ALOGV("%s: reset AFE input channels = %s", __func__, in_channels);
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

        ret = mixer_ctl_set_array(ctl_dec_data, (void *)&dec_cfg,
                                  sizeof(dec_cfg));
        if (ret != 0) {
            ALOGE("%s: Failed to set decoder config", __func__);
            return false;
        }
    }

    return true;
}

/* API to configure SBC DSP encoder */
static bool configure_sbc_enc_format(audio_sbc_encoder_config *sbc_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct sbc_enc_cfg_t sbc_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (sbc_bt_cfg == NULL) {
        ALOGE("%s: Failed to get SBC encoder config from BT", __func__);
        return false;
    }

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE("%s: ERROR A2DP encoder config data mixer control not identifed", __func__);
        is_configured = false;
        goto exit;
    }
    memset(&sbc_dsp_cfg, 0x0, sizeof(sbc_dsp_cfg));
    sbc_dsp_cfg.enc_format = ENC_MEDIA_FMT_SBC;
    sbc_dsp_cfg.num_subbands = sbc_bt_cfg->subband;
    sbc_dsp_cfg.blk_len = sbc_bt_cfg->blk_len;
    switch (sbc_bt_cfg->channels) {
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
                                    sizeof(sbc_dsp_cfg));
    if (ret != 0) {
        ALOGE("%s: failed to set SBC encoder config", __func__);
        is_configured = false;
        goto exit;
    }
    ret = a2dp_set_bit_format(sbc_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto exit;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_SBC;
    a2dp.enc_sampling_rate = sbc_bt_cfg->sampling_rate;

    if (sbc_dsp_cfg.channel_mode == MEDIA_FMT_SBC_CHANNEL_MODE_MONO)
        a2dp.enc_channels = 1;
    else
        a2dp.enc_channels = 2;

    ALOGV("%s: Successfully updated SBC enc format with sampling rate: %d channel mode:%d",
           __func__, sbc_dsp_cfg.sample_rate, sbc_dsp_cfg.channel_mode);
exit:
    return is_configured;
}

/* API to configure APTX DSP encoder */
static bool configure_aptx_enc_format(audio_aptx_encoder_config *aptx_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    int mixer_size;
    bool is_configured = false;
    int ret = 0;
    struct aptx_enc_cfg_t aptx_dsp_cfg;
    mixer_size = sizeof(aptx_dsp_cfg);

    if (aptx_bt_cfg == NULL) {
        ALOGE("%s: Failed to get APTX encoder config from BT", __func__);
        return false;
    }

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE("%s: ERROR A2DP encoder config data mixer control not identifed", __func__);
        is_configured = false;
        goto exit;
    }

    memset(&aptx_dsp_cfg, 0x0, sizeof(aptx_dsp_cfg));
    aptx_dsp_cfg.custom_cfg.enc_format = ENC_MEDIA_FMT_APTX;

    if (!a2dp.is_aptx_dual_mono_supported) {
        aptx_dsp_cfg.custom_cfg.sample_rate = aptx_bt_cfg->default_cfg->sampling_rate;
        aptx_dsp_cfg.custom_cfg.num_channels = aptx_bt_cfg->default_cfg->channels;
    } else {
        aptx_dsp_cfg.custom_cfg.sample_rate = aptx_bt_cfg->dual_mono_cfg->sampling_rate;
        aptx_dsp_cfg.custom_cfg.num_channels = aptx_bt_cfg->dual_mono_cfg->channels;
        aptx_dsp_cfg.aptx_v2_cfg.sync_mode = aptx_bt_cfg->dual_mono_cfg->sync_mode;
    }

    switch (aptx_dsp_cfg.custom_cfg.num_channels) {
        case 1:
            aptx_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
            aptx_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
            aptx_dsp_cfg.custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                              mixer_size);
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX encoder config", __func__);
        is_configured = false;
        goto exit;
    }
    ret = a2dp_set_bit_format(aptx_bt_cfg->default_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto exit;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_APTX;
    a2dp.enc_channels = aptx_dsp_cfg.custom_cfg.num_channels;
    if (!a2dp.is_aptx_dual_mono_supported) {
        a2dp.enc_sampling_rate = aptx_bt_cfg->default_cfg->sampling_rate;
        ALOGV("%s: Successfully updated APTX enc format with sampling rate: %d \
               channels:%d", __func__, aptx_dsp_cfg.custom_cfg.sample_rate,
               aptx_dsp_cfg.custom_cfg.num_channels);
    } else {
        a2dp.enc_sampling_rate = aptx_bt_cfg->dual_mono_cfg->sampling_rate;
        ALOGV("%s: Successfully updated APTX dual mono enc format with \
               sampling rate: %d channels:%d sync mode %d", __func__,
               aptx_dsp_cfg.custom_cfg.sample_rate,
               aptx_dsp_cfg.custom_cfg.num_channels,
               aptx_dsp_cfg.aptx_v2_cfg.sync_mode);
    }

exit:
    return is_configured;
}

/* API to configure APTX HD DSP encoder
 */
static bool configure_aptx_hd_enc_format(audio_aptx_default_config *aptx_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct custom_enc_cfg_t aptx_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (aptx_bt_cfg == NULL) {
        ALOGE("%s: Failed to get APTX HD encoder config from BT", __func__);
        return false;
    }

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE("%s: ERROR A2DP encoder config data mixer control not identifed", __func__);
        is_configured = false;
        goto exit;
    }

    memset(&aptx_dsp_cfg, 0x0, sizeof(aptx_dsp_cfg));
    aptx_dsp_cfg.enc_format = ENC_MEDIA_FMT_APTX_HD;
    aptx_dsp_cfg.sample_rate = aptx_bt_cfg->sampling_rate;
    aptx_dsp_cfg.num_channels = aptx_bt_cfg->channels;
    switch (aptx_dsp_cfg.num_channels) {
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
                              sizeof(aptx_dsp_cfg));
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX HD encoder config", __func__);
        is_configured = false;
        goto exit;
    }
    ret = a2dp_set_bit_format(aptx_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto exit;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_APTX_HD;
    a2dp.enc_sampling_rate = aptx_bt_cfg->sampling_rate;
    a2dp.enc_channels = aptx_bt_cfg->channels;
    ALOGV("%s: Successfully updated APTX HD encformat with sampling rate: %d channels:%d",
           __func__, aptx_dsp_cfg.sample_rate, aptx_dsp_cfg.num_channels);
exit:
    return is_configured;
}

/* API to configure AAC DSP encoder */
static bool configure_aac_enc_format(audio_aac_encoder_config *aac_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL, *ctrl_bit_format = NULL;
    struct aac_enc_cfg_t aac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (aac_bt_cfg == NULL) {
        ALOGE("%s: Failed to get AAC encoder config from BT", __func__);
        return false;
    }

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE("%s: ERROR A2DP encoder config data mixer control not identifed", __func__);
        is_configured = false;
        goto exit;
    }
    memset(&aac_dsp_cfg, 0x0, sizeof(aac_dsp_cfg));
    aac_dsp_cfg.aac_cfg.enc_format = ENC_MEDIA_FMT_AAC;
    aac_dsp_cfg.aac_cfg.bit_rate = aac_bt_cfg->bitrate;
    aac_dsp_cfg.aac_cfg.sample_rate = aac_bt_cfg->sampling_rate;
    switch (aac_bt_cfg->enc_mode) {
        case 0:
            aac_dsp_cfg.aac_cfg.enc_mode = MEDIA_FMT_AAC_AOT_LC;
            break;
        case 2:
            aac_dsp_cfg.aac_cfg.enc_mode = MEDIA_FMT_AAC_AOT_PS;
            break;
        case 1:
        default:
            aac_dsp_cfg.aac_cfg.enc_mode = MEDIA_FMT_AAC_AOT_SBR;
            break;
    }
    aac_dsp_cfg.aac_cfg.aac_fmt_flag = aac_bt_cfg->format_flag;
    aac_dsp_cfg.aac_cfg.channel_cfg = aac_bt_cfg->channels;
    aac_dsp_cfg.frame_ctl.ctl_type = aac_bt_cfg->frame_ctl.ctl_type;
    aac_dsp_cfg.frame_ctl.ctl_value = aac_bt_cfg->frame_ctl.ctl_value;
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aac_dsp_cfg,
                              sizeof(aac_dsp_cfg));
    if (ret != 0) {
        ALOGE("%s: failed to set AAC encoder config", __func__);
        is_configured = false;
        goto exit;
    }
    ret = a2dp_set_bit_format(aac_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto exit;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_AAC;
    a2dp.enc_sampling_rate = aac_bt_cfg->sampling_rate;
    a2dp.enc_channels = aac_bt_cfg->channels;
    ALOGV("%s: Successfully updated AAC enc format with sampling rate: %d channels:%d",
           __func__, aac_dsp_cfg.aac_cfg.sample_rate, aac_dsp_cfg.aac_cfg.channel_cfg);
exit:
    return is_configured;
}

static bool configure_ldac_enc_format(audio_ldac_encoder_config *ldac_bt_cfg)
{
    struct mixer_ctl *ldac_enc_data = NULL, *ctrl_bit_format = NULL;
    struct ldac_enc_cfg_t ldac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (ldac_bt_cfg == NULL) {
        ALOGE("%s: Failed to get LDAC encoder config from BT", __func__);
        return false;
    }

    ldac_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ldac_enc_data) {
        ALOGE("%s: ERROR A2DP encoder config data mixer control not identifed", __func__);
        is_configured = false;
        goto exit;
    }
    memset(&ldac_dsp_cfg, 0x0, sizeof(ldac_dsp_cfg));

    ldac_dsp_cfg.custom_cfg.enc_format = ENC_MEDIA_FMT_LDAC;
    ldac_dsp_cfg.custom_cfg.sample_rate = ldac_bt_cfg->sampling_rate;
    ldac_dsp_cfg.ldac_cfg.channel_mode = ldac_bt_cfg->channel_mode;
    switch (ldac_dsp_cfg.ldac_cfg.channel_mode) {
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

    ldac_dsp_cfg.custom_cfg.custom_size = sizeof(ldac_dsp_cfg);
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
                              sizeof(ldac_dsp_cfg));
    if (ret != 0) {
        ALOGE("%s: Failed to set LDAC encoder config", __func__);
        is_configured = false;
        goto exit;
    }
    ret = a2dp_set_bit_format(ldac_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto exit;
    }
    is_configured = true;
    a2dp.bt_encoder_format = ENC_CODEC_TYPE_LDAC;
    a2dp.enc_sampling_rate = ldac_bt_cfg->sampling_rate;
    a2dp.enc_channels = ldac_dsp_cfg.custom_cfg.num_channels;
    a2dp.abr_config.is_abr_enabled = ldac_bt_cfg->is_abr_enabled;
    ALOGV("%s: Successfully updated LDAC encformat with sampling rate: %d channels:%d",
           __func__, ldac_dsp_cfg.custom_cfg.sample_rate,
           ldac_dsp_cfg.custom_cfg.num_channels);
exit:
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
        ALOGE("%s: A2DP handle is not identified, ignoring A2DP encoder config", __func__);
        return false;
    }
    ALOGD("%s: start", __func__);
    codec_info = a2dp.audio_get_codec_config(&multi_cast, &num_dev,
                               &codec_type);

    // ABR disabled by default for all codecs
    a2dp.abr_config.is_abr_enabled = false;

    switch (codec_type) {
        case ENC_CODEC_TYPE_SBC:
            ALOGD("%s: Received SBC encoder supported Bluetooth device", __func__);
            is_configured =
              configure_sbc_enc_format((audio_sbc_encoder_config *)codec_info);
            break;
        case ENC_CODEC_TYPE_APTX:
            ALOGD("%s: Received APTX encoder supported Bluetooth device", __func__);
            a2dp.is_aptx_dual_mono_supported = false;
            aptx_encoder_cfg.default_cfg = (audio_aptx_default_config *)codec_info;
            is_configured =
              configure_aptx_enc_format(&aptx_encoder_cfg);
            break;
        case ENC_CODEC_TYPE_APTX_HD:
            ALOGD("%s: Received APTX HD encoder supported Bluetooth device", __func__);
            is_configured =
              configure_aptx_hd_enc_format((audio_aptx_default_config *)codec_info);
            break;
        case ENC_CODEC_TYPE_AAC:
            ALOGD("%s: Received AAC encoder supported Bluetooth device", __func__);
            is_configured =
              configure_aac_enc_format((audio_aac_encoder_config *)codec_info);
            break;
        case ENC_CODEC_TYPE_LDAC:
            ALOGD("%s: Received LDAC encoder supported Bluetooth device", __func__);
            if (!instance_id || instance_id > MAX_INSTANCE_ID)
                instance_id = MAX_INSTANCE_ID;
            a2dp.abr_config.imc_instance = instance_id--;
            is_configured =
                (configure_ldac_enc_format((audio_ldac_encoder_config *)codec_info) &&
                 configure_a2dp_decoder_format(ENC_CODEC_TYPE_LDAC));
            break;
        case ENC_CODEC_TYPE_PCM:
            ALOGD("Received PCM format for BT device");
            a2dp.bt_encoder_format = ENC_CODEC_TYPE_PCM;
            is_configured = true;
            break;
        default:
            ALOGD("%s: Received unsupported encoder format", __func__);
            is_configured = false;
            break;
    }
    return is_configured;
}

int audio_extn_a2dp_start_playback()
{
    int ret = 0;

    ALOGD("%s: start", __func__);

    if (!(a2dp.bt_lib_handle && a2dp.audio_stream_start
       && a2dp.audio_get_codec_config)) {
        ALOGE("%s: A2DP handle is not identified, Ignoring start request", __func__);
        return -ENOSYS;
    }

    if (a2dp.a2dp_suspended) {
        // session will be restarted after suspend completion
        ALOGD("%s: A2DP start requested during suspend state", __func__);
        return -ENOSYS;
    }

    if (!a2dp.a2dp_started && !a2dp.a2dp_total_active_session_request) {
        ALOGD("%s: calling Bluetooth module stream start", __func__);
        /* This call indicates Bluetooth IPC lib to start playback */
        ret =  a2dp.audio_stream_start();
        if (ret != 0 ) {
           ALOGE("%s: Bluetooth controller start failed", __func__);
           a2dp.a2dp_started = false;
        } else {
           if (configure_a2dp_encoder_format() == true) {
                a2dp.a2dp_started = true;
                ret = 0;
                ALOGD("%s: Start playback successful to Bluetooth IPC library", __func__);
           } else {
                ALOGD("%s: unable to configure DSP encoder", __func__);
                a2dp.a2dp_started = false;
                ret = -ETIMEDOUT;
           }
        }
    }

    if (a2dp.a2dp_started) {
        a2dp.a2dp_total_active_session_request++;
        a2dp_check_and_set_scrambler();
        a2dp_set_backend_cfg();
        if (a2dp.abr_config.is_abr_enabled)
            start_abr();
    }

    ALOGD("%s: start A2DP playback total active sessions :%d", __func__,
          a2dp.a2dp_total_active_session_request);
    return ret;
}

static int reset_a2dp_enc_config_params()
{
    int ret = 0;

    struct mixer_ctl *ctl_enc_config, *ctrl_bit_format;
    struct sbc_enc_cfg_t dummy_reset_config;

    memset(&dummy_reset_config, 0x0, sizeof(dummy_reset_config));
    ctl_enc_config = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                           MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_config) {
        ALOGE("%s: ERROR A2DP encoder format mixer control not identifed", __func__);
    } else {
        ret = mixer_ctl_set_array(ctl_enc_config, (void *)&dummy_reset_config,
                                        sizeof(dummy_reset_config));
         a2dp.bt_encoder_format = ENC_MEDIA_FMT_NONE;
    }

    ret = a2dp_set_bit_format(DEFAULT_ENCODER_BIT_FORMAT);

    return ret;
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
    int ret = 0;

    ALOGV("%s: stop", __func__);
    if (!(a2dp.bt_lib_handle && a2dp.audio_stream_stop)) {
        ALOGE("%s: A2DP handle is not identified, Ignoring start request", __func__);
        return -ENOSYS;
    }

    if (a2dp.a2dp_total_active_session_request > 0)
        a2dp.a2dp_total_active_session_request--;
    else
        ALOGE("%s: No active playback session requests on A2DP", __func__);

    if (a2dp.a2dp_started && !a2dp.a2dp_total_active_session_request) {
        ALOGV("%s: calling Bluetooth module stream stop", __func__);
        ret = a2dp.audio_stream_stop();
        if (ret < 0)
            ALOGE("%s: stop stream to Bluetooth IPC lib failed", __func__);
        else
            ALOGV("%s: stop steam to Bluetooth IPC lib successful", __func__);
        if (!a2dp.a2dp_suspended)
            reset_a2dp_config();
        a2dp.a2dp_started = false;
    }
    ALOGD("%s: Stop A2DP playback total active sessions :%d", __func__,
          a2dp.a2dp_total_active_session_request);
    return 0;
}

int audio_extn_a2dp_set_parameters(struct str_parms *parms, bool *reconfig)
{
     int ret = 0, val;
     int status = 0;
     char value[32] = {0};
     struct audio_usecase *uc_info;
     struct listnode *node;

     if (a2dp.is_a2dp_offload_enabled == false) {
        ALOGV("%s: No supported encoders identified,ignoring A2DP setparam", __func__);
        status = -EINVAL;
        goto param_handled;
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value,
                            sizeof(value));
     if (ret >= 0) {
         val = atoi(value);
         if (audio_is_a2dp_out_device(val)) {
             ALOGV("%s: Received device connect request for A2DP", __func__);
             open_a2dp_output();
         }
         goto param_handled;
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                         sizeof(value));

     if (ret >= 0) {
         val = atoi(value);
         if (audio_is_a2dp_out_device(val)) {
             ALOGV("%s: Received device disconnect request", __func__);
             reset_a2dp_config();
             close_a2dp_output();
         }
         goto param_handled;
     }

     ret = str_parms_get_str(parms, "A2dpSuspended", value, sizeof(value));
     if (ret >= 0) {
         if (a2dp.bt_lib_handle && (a2dp.bt_state != A2DP_STATE_DISCONNECTED)) {
             if (strncmp(value, "true", sizeof(value)) == 0) {
                if (a2dp.a2dp_suspended) {
                    ALOGD("%s: A2DP is already suspended", __func__);
                    goto param_handled;
                }
                ALOGD("%s: Setting A2DP to suspend state", __func__);
                a2dp.a2dp_suspended = true;
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
                if (a2dp.audio_stream_suspend) {
                   a2dp.audio_stream_suspend();
                }
            } else {
                if (!a2dp.a2dp_suspended) {
                    ALOGD("%s: A2DP is already unsuspended", __func__);
                    goto param_handled;
                }
                ALOGD("%s: Resetting A2DP suspend state", __func__);
                struct audio_usecase *uc_info;
                struct listnode *node;
                if (a2dp.clear_a2dp_suspend_flag) {
                    a2dp.clear_a2dp_suspend_flag();
                }
                a2dp.a2dp_suspended = false;
                /*
                 * It is possible that before suspend, A2DP sessions can be active.
                 * For example, during music + voice activation concurrency,
                 * A2DP suspend will be called & Bluetooth will change to SCO mode.
                 * Though music is paused as a part of voice activation,
                 * compress session close happens only after pause timeout(10 secs).
                 * So, if resume request comes before pause timeout, as A2DP session
                 * is already active, IPC start will not be called from APM/audio_hw.
                 * Fix this by calling A2DP start for IPC library post suspend
                 * based on number of active session count.
                 */
                if (a2dp.a2dp_total_active_session_request > 0) {
                    ALOGD("%s: Calling Bluetooth IPC lib start post suspend state", __func__);
                    if (a2dp.audio_stream_start) {
                        status =  a2dp.audio_stream_start();
                        if (status != 0) {
                            ALOGE("%s: Bluetooth controller start failed", __func__);
                            a2dp.a2dp_started = false;
                        } else {
                            if (!configure_a2dp_encoder_format()) {
                                ALOGE("%s: Encoder params configuration failed post suspend", __func__);
                                a2dp.a2dp_started = false;
                                status = -ETIMEDOUT;
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

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_RECONFIG_A2DP, value,
                         sizeof(value));
     if (ret >= 0) {
         if (a2dp.is_a2dp_offload_enabled &&
                a2dp.bt_state != A2DP_STATE_DISCONNECTED) {
             *reconfig = true;
         }
         goto param_handled;
     }

param_handled:
     ALOGV("%s: end of A2DP setparam", __func__);
     return status;
}

void audio_extn_a2dp_set_handoff_mode(bool is_on)
{
    a2dp.is_handoff_in_progress = is_on;
}

bool audio_extn_a2dp_is_force_device_switch()
{
    // During encoder reconfiguration mode, force A2DP device switch
    // Or if A2DP device is selected but earlier start failed as A2DP
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
        goto exit;

    if ((a2dp.bt_state != A2DP_STATE_DISCONNECTED) &&
        (a2dp.is_a2dp_offload_enabled) &&
        (a2dp.audio_check_a2dp_ready))
           ret = a2dp.audio_check_a2dp_ready();

exit:
    return ret;
}

bool audio_extn_a2dp_is_suspended()
{
    return a2dp.a2dp_suspended;
}

void audio_extn_a2dp_init(void *adev)
{
  a2dp.adev = (struct audio_device*)adev;
  a2dp.bt_lib_handle = NULL;
  a2dp_common_init();
  a2dp.enc_sampling_rate = 48000;
  a2dp.is_a2dp_offload_enabled = false;
  a2dp.is_handoff_in_progress = false;
  a2dp.is_aptx_dual_mono_supported = false;
  reset_a2dp_enc_config_params();
  reset_a2dp_dec_config_params();
  update_offload_codec_support();
}

uint32_t audio_extn_a2dp_get_encoder_latency()
{
    uint32_t latency_ms = 0;
    int avsync_runtime_prop = 0;
    int sbc_offset = 0, aptx_offset = 0, aptxhd_offset = 0,
        aac_offset = 0, ldac_offset = 0;
    char value[PROPERTY_VALUE_MAX];

    memset(value, '\0', sizeof(char) * PROPERTY_VALUE_MAX);
    avsync_runtime_prop = property_get(SYSPROP_A2DP_CODEC_LATENCIES, value, NULL);
    if (avsync_runtime_prop > 0) {
        if (sscanf(value, "%d/%d/%d/%d/%d",
            &sbc_offset, &aptx_offset, &aptxhd_offset, &aac_offset,
            &ldac_offset) != 5) {
            ALOGI("%s: Failed to parse avsync offset params from '%s'.", __func__, value);
            avsync_runtime_prop = 0;
        }
    }

    uint32_t slatency_ms = 0;
    if (a2dp.audio_get_a2dp_sink_latency && a2dp.bt_state != A2DP_STATE_DISCONNECTED) {
        slatency_ms = a2dp.audio_get_a2dp_sink_latency();
    }

    switch (a2dp.bt_encoder_format) {
        case ENC_CODEC_TYPE_SBC:
            latency_ms = (avsync_runtime_prop > 0) ? sbc_offset : ENCODER_LATENCY_SBC;
            latency_ms += (slatency_ms == 0) ? DEFAULT_SINK_LATENCY_SBC : slatency_ms;
            break;
        case ENC_CODEC_TYPE_APTX:
            latency_ms = (avsync_runtime_prop > 0) ? aptx_offset : ENCODER_LATENCY_APTX;
            latency_ms += (slatency_ms == 0) ? DEFAULT_SINK_LATENCY_APTX : slatency_ms;
            break;
        case ENC_CODEC_TYPE_APTX_HD:
            latency_ms = (avsync_runtime_prop > 0) ? aptxhd_offset : ENCODER_LATENCY_APTX_HD;
            latency_ms += (slatency_ms == 0) ? DEFAULT_SINK_LATENCY_APTX_HD : slatency_ms;
            break;
        case ENC_CODEC_TYPE_AAC:
            latency_ms = (avsync_runtime_prop > 0) ? aac_offset : ENCODER_LATENCY_AAC;
            latency_ms += (slatency_ms == 0) ? DEFAULT_SINK_LATENCY_AAC : slatency_ms;
            break;
        case ENC_CODEC_TYPE_LDAC:
            latency_ms = (avsync_runtime_prop > 0) ? ldac_offset : ENCODER_LATENCY_LDAC;
            latency_ms += (slatency_ms == 0) ? DEFAULT_SINK_LATENCY_LDAC : slatency_ms;
            break;
        case ENC_CODEC_TYPE_PCM:
            latency_ms = ENCODER_LATENCY_PCM;
            latency_ms += DEFAULT_SINK_LATENCY_PCM;
            break;
        default:
            latency_ms = DEFAULT_ENCODER_LATENCY;
            break;
    }
    return latency_ms;
}

int audio_extn_a2dp_get_parameters(struct str_parms *query,
                                   struct str_parms *reply)
{
    int ret, val = 0;
    char value[32]={0};

    ret = str_parms_get_str(query, AUDIO_PARAMETER_A2DP_RECONFIG_SUPPORTED,
                            value, sizeof(value));
    if (ret >= 0) {
        val = a2dp.is_a2dp_offload_enabled;
        str_parms_add_int(reply, AUDIO_PARAMETER_A2DP_RECONFIG_SUPPORTED, val);
        ALOGV("%s: called ... isReconfigA2dpSupported %d", __func__, val);
    }

    return 0;
}
#endif // A2DP_OFFLOAD_ENABLED
