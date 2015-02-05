/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Not a Contribution.
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

#ifndef QCOM_AUDIO_PLATFORM_H
#define QCOM_AUDIO_PLATFORM_H

enum {
    FLUENCE_NONE,
    FLUENCE_DUAL_MIC = 0x1,
    FLUENCE_QUAD_MIC = 0x2,
};

#include <hardware/audio.h>
/*
 * Below are the devices for which is back end is same, SLIMBUS_0_RX.
 * All these devices are handled by the internal HW codec. We can
 * enable any one of these devices at any time
 */
#define AUDIO_DEVICE_OUT_ALL_CODEC_BACKEND \
    (AUDIO_DEVICE_OUT_EARPIECE | AUDIO_DEVICE_OUT_SPEAKER | \
     AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE)
/*TODO remove this once define in audio.h */
#define AUDIO_DEVICE_OUT_SPDIF 0x4000

/* Sound devices specific to the platform
 * The DEVICE_OUT_* and DEVICE_IN_* should be mapped to these sound
 * devices to enable corresponding mixer paths
 */
enum {
    SND_DEVICE_NONE = 0,

    /* Playback devices */
    SND_DEVICE_MIN,
    SND_DEVICE_OUT_BEGIN = SND_DEVICE_MIN,
    SND_DEVICE_OUT_HANDSET = SND_DEVICE_OUT_BEGIN,
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_REVERSE,
    SND_DEVICE_OUT_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_VOICE_HANDSET,
    SND_DEVICE_OUT_VOICE_SPEAKER,
    SND_DEVICE_OUT_VOICE_HEADPHONES,
    SND_DEVICE_OUT_HDMI,
    SND_DEVICE_OUT_SPEAKER_AND_HDMI,
    SND_DEVICE_OUT_BT_SCO,
    SND_DEVICE_OUT_BT_SCO_WB,
    SND_DEVICE_OUT_VOICE_TTY_FULL_HEADPHONES,
    SND_DEVICE_OUT_VOICE_TTY_VCO_HEADPHONES,
    SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET,
    SND_DEVICE_OUT_AFE_PROXY,
    SND_DEVICE_OUT_USB_HEADSET,
    SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET,
    SND_DEVICE_OUT_TRANSMISSION_FM,
    SND_DEVICE_OUT_ANC_HEADSET,
    SND_DEVICE_OUT_ANC_FB_HEADSET,
    SND_DEVICE_OUT_VOICE_ANC_HEADSET,
    SND_DEVICE_OUT_VOICE_ANC_FB_HEADSET,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
    SND_DEVICE_OUT_ANC_HANDSET,
    SND_DEVICE_OUT_SPEAKER_PROTECTED,
    SND_DEVICE_OUT_END,

    /*
     * Note: IN_BEGIN should be same as OUT_END because total number of devices
     * SND_DEVICES_MAX should not exceed MAX_RX + MAX_TX devices.
     */
    /* Capture devices */
    SND_DEVICE_IN_BEGIN = SND_DEVICE_OUT_END,
    SND_DEVICE_IN_HANDSET_MIC  = SND_DEVICE_IN_BEGIN,
    SND_DEVICE_IN_HANDSET_MIC_AEC,
    SND_DEVICE_IN_HANDSET_MIC_NS,
    SND_DEVICE_IN_HANDSET_MIC_AEC_NS,
    SND_DEVICE_IN_HANDSET_DMIC,
    SND_DEVICE_IN_HANDSET_DMIC_AEC,
    SND_DEVICE_IN_HANDSET_DMIC_NS,
    SND_DEVICE_IN_HANDSET_DMIC_AEC_NS,
    SND_DEVICE_IN_SPEAKER_MIC,
    SND_DEVICE_IN_SPEAKER_MIC_AEC,
    SND_DEVICE_IN_SPEAKER_MIC_NS,
    SND_DEVICE_IN_SPEAKER_MIC_AEC_NS,
    SND_DEVICE_IN_SPEAKER_DMIC,
    SND_DEVICE_IN_SPEAKER_DMIC_AEC,
    SND_DEVICE_IN_SPEAKER_DMIC_NS,
    SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS,
    SND_DEVICE_IN_HEADSET_MIC,
    SND_DEVICE_IN_HEADSET_MIC_FLUENCE,
    SND_DEVICE_IN_VOICE_SPEAKER_MIC,
    SND_DEVICE_IN_VOICE_HEADSET_MIC,
    SND_DEVICE_IN_HDMI_MIC,
    SND_DEVICE_IN_BT_SCO_MIC,
    SND_DEVICE_IN_BT_SCO_MIC_WB,
    SND_DEVICE_IN_CAMCORDER_MIC,
    SND_DEVICE_IN_VOICE_DMIC,
    SND_DEVICE_IN_VOICE_SPEAKER_DMIC,
    SND_DEVICE_IN_VOICE_SPEAKER_QMIC,
    SND_DEVICE_IN_VOICE_TTY_FULL_HEADSET_MIC,
    SND_DEVICE_IN_VOICE_TTY_VCO_HANDSET_MIC,
    SND_DEVICE_IN_VOICE_TTY_HCO_HEADSET_MIC,
    SND_DEVICE_IN_VOICE_REC_MIC,
    SND_DEVICE_IN_VOICE_REC_MIC_NS,
    SND_DEVICE_IN_VOICE_REC_DMIC_STEREO,
    SND_DEVICE_IN_VOICE_REC_DMIC_FLUENCE,
    SND_DEVICE_IN_USB_HEADSET_MIC,
    SND_DEVICE_IN_CAPTURE_FM,
    SND_DEVICE_IN_AANC_HANDSET_MIC,
    SND_DEVICE_IN_QUAD_MIC,
    SND_DEVICE_IN_HANDSET_STEREO_DMIC,
    SND_DEVICE_IN_SPEAKER_STEREO_DMIC,
    SND_DEVICE_IN_CAPTURE_VI_FEEDBACK,
    SND_DEVICE_IN_END,

    SND_DEVICE_MAX = SND_DEVICE_IN_END,

};

#define MIXER_CARD 0
#define SOUND_CARD 0

#define DEFAULT_OUTPUT_SAMPLING_RATE 48000

#define ALL_SESSION_VSID                0xFFFFFFFF
#define DEFAULT_MUTE_RAMP_DURATION      500
#define DEFAULT_VOLUME_RAMP_DURATION_MS 20
#define MIXER_PATH_MAX_LENGTH 100

#define MAX_VOL_INDEX 5
#define MIN_VOL_INDEX 0
#define percent_to_index(val, min, max) \
            ((val) * ((max) - (min)) * 0.01 + (min) + .5)

/*
 * tinyAlsa library interprets period size as number of frames
 * one frame = channel_count * sizeof (pcm sample)
 * so if format = 16-bit PCM and channels = Stereo, frame size = 2 ch * 2 = 4 bytes
 * DEEP_BUFFER_OUTPUT_PERIOD_SIZE = 1024 means 1024 * 4 = 4096 bytes
 * We should take care of returning proper size when AudioFlinger queries for
 * the buffer size of an input/output stream
 */
#define DEEP_BUFFER_OUTPUT_PERIOD_SIZE 960
#define DEEP_BUFFER_OUTPUT_PERIOD_COUNT 8
#define LOW_LATENCY_OUTPUT_PERIOD_SIZE 240
#define LOW_LATENCY_OUTPUT_PERIOD_COUNT 2

/*******************************************************************************
ADTS HEADER PARSING
*******************************************************************************/
//Required for ADTS Header Parsing
#define ADTS_HEADER_SYNC_RESULT 0xfff0
#define ADTS_HEADER_SYNC_MASK 0xfff6
/*******************************************************************************
HDMI and SPDIF Device Output format control
*******************************************************************************/

#define HDMI_MULTI_PERIOD_SIZE  336
#define HDMI_MULTI_PERIOD_COUNT 8
#define HDMI_MULTI_DEFAULT_CHANNEL_COUNT 6
#define HDMI_MULTI_PERIOD_BYTES (HDMI_MULTI_PERIOD_SIZE * HDMI_MULTI_DEFAULT_CHANNEL_COUNT * 2)

#define AUDIO_CAPTURE_PERIOD_DURATION_MSEC 20
#define AUDIO_CAPTURE_PERIOD_COUNT 2

#define DEVICE_NAME_MAX_SIZE 128
#define HW_INFO_ARRAY_MAX_SIZE 32

#define DEEP_BUFFER_PCM_DEVICE 0
#define AUDIO_RECORD_PCM_DEVICE 0
#define MULTIMEDIA2_PCM_DEVICE 1
#define FM_PLAYBACK_PCM_DEVICE 5
#define FM_CAPTURE_PCM_DEVICE  6
#define INCALL_MUSIC_UPLINK_PCM_DEVICE 1
#define INCALL_MUSIC_UPLINK2_PCM_DEVICE 16
#define SPKR_PROT_CALIB_RX_PCM_DEVICE 5
#define SPKR_PROT_CALIB_TX_PCM_DEVICE 22
#define COMPRESS_VOIP_CALL_PCM_DEVICE 3
#define MULTI_CHANNEL_PCM_DEVICE 1
#define VOICE_CALL_PCM_DEVICE 2
//TODO: update the device number as per the dai links
#define PLAYBACK_OFFLOAD_DEVICE1 2
#define PLAYBACK_OFFLOAD_DEVICE2 3
#define PLAYBACK_OFFLOAD_DEVICE3 4
#define PLAYBACK_OFFLOAD_DEVICE4 19

#define LOWLATENCY_PCM_DEVICE 15
#define COMPRESS_CAPTURE_DEVICE 19

#ifdef PLATFORM_MSM8x26
#define VOICE_CALL_PCM_DEVICE 2
#define VOICE2_CALL_PCM_DEVICE 14
#define VOLTE_CALL_PCM_DEVICE 17
#define QCHAT_CALL_PCM_DEVICE 18
#elif PLATFORM_APQ8084
#define VOICE_CALL_PCM_DEVICE 20
#define VOICE2_CALL_PCM_DEVICE 13
#define VOLTE_CALL_PCM_DEVICE 21
#define QCHAT_CALL_PCM_DEVICE 06
#else
#define VOICE_CALL_PCM_DEVICE 2
#define VOICE2_CALL_PCM_DEVICE 13
#define VOLTE_CALL_PCM_DEVICE 14
#define QCHAT_CALL_PCM_DEVICE 20
#endif

#define LIB_CSD_CLIENT "libcsd-client.so"
/* CSD-CLIENT related functions */
typedef int (*init_t)();
typedef int (*deinit_t)();
typedef int (*disable_device_t)();
typedef int (*enable_device_t)(int, int, uint32_t);
typedef int (*volume_t)(uint32_t, int);
typedef int (*mic_mute_t)(uint32_t, int);
typedef int (*slow_talk_t)(uint32_t, uint8_t);
typedef int (*start_voice_t)(uint32_t);
typedef int (*stop_voice_t)(uint32_t);
typedef int (*start_playback_t)(uint32_t);
typedef int (*stop_playback_t)(uint32_t);
typedef int (*start_record_t)(uint32_t, int);
typedef int (*stop_record_t)(uint32_t, int);
/* CSD Client structure */
struct csd_data {
    void *csd_client;
    init_t init;
    deinit_t deinit;
    disable_device_t disable_device;
    enable_device_t enable_device;
    volume_t volume;
    mic_mute_t mic_mute;
    slow_talk_t slow_talk;
    start_voice_t start_voice;
    stop_voice_t stop_voice;
    start_playback_t start_playback;
    stop_playback_t stop_playback;
    start_record_t start_record;
    stop_record_t stop_record;
};

void *hw_info_init(const char *snd_card_name);
void hw_info_deinit(void *hw_info);
void hw_info_append_hw_type(void *hw_info, snd_device_t snd_device,
                             char *device_name);

/*******************************************************************************
USECASES AND THE CORRESPONDING DEVICE FORMATS THAT WE SUPPORT IN HAL
*******************************************************************************/
/*
In general max of 2 for pass through. Additional 1 for handling transcode
as the existence of transcode is with a PCM handle followed by transcode handle
So, a (AC3/EAC3) pass through + trancode require - 1 for pas through, 1 - pcm and
1 - transcode
*/
#define NUM_DEVICES_SUPPORT_COMPR_DATA 2+1
#define NUM_SUPPORTED_CODECS           16
#define NUM_COLUMN_FOR_INDEXING        2
#define NUM_STATES_FOR_EACH_DEVICE_FMT 3
#define DECODER_TYPE_IDX               0
#define ROUTE_FORMAT_IDX               1

#define MIN_SIZE_FOR_METADATA    64
#define NUM_OF_PERIODS           8
/*Period size to be a multiple of chanels * bitwidth,
So min period size = LCM (1,2...8) * 4*/
#define PERIOD_SIZE_COMPR        3360
#define MS11_INPUT_BUFFER_SIZE   1536
/*Max Period size which is exposed by the compr driver
The value needs to be modified when the period size is modified*/
#define PLAYBACK_MAX_PERIOD_SIZE (160 * 1024)

#define COMPR_INPUT_BUFFER_SIZE  (PERIOD_SIZE_COMPR - MIN_SIZE_FOR_METADATA)
#define PCM_16_BITS_PER_SAMPLE   2
#define PCM_24_BITS_PER_SAMPLE   3
#define AC3_PERIOD_SIZE          1536 * PCM_16_BITS_PER_SAMPLE
#define TIME_PER_BUFFER          40 //Time duration in ms
#define SAMPLES_PER_CHANNEL             32*1024 //1536*2 /*TODO:correct it
#define MAX_INPUT_CHANNELS_SUPPORTED    8
#define FACTOR_FOR_BUFFERING            2
#define STEREO_CHANNELS                 2
#define MAX_OUTPUT_CHANNELS_SUPPORTED   8
#define PCM_BLOCK_PER_CHANNEL_MS11      1536*2
#define AAC_BLOCK_PER_CHANNEL_MS11      768
#define NUMBER_BITS_IN_A_BYTE           8
#define AC3_BUFFER_SIZE                 1920*2

#define MAX_OUTPUT_CHANNELS_SUPPORTED   8

#define PCM_2CH_OUT                 0
#define PCM_MCH_OUT                 1
#define SPDIF_OUT                   2
#define COMPRESSED_OUT              2 // should be same as SPDIF_OUT
#define TRANSCODE_OUT               3
#define FACTOR_FOR_BUFFERING        2

#define NUM_DEVICES_SUPPORT_COMPR_DATA 2+1
#define NUM_SUPPORTED_CODECS           16
#define NUM_COLUMN_FOR_INDEXING        2
#define NUM_STATES_FOR_EACH_DEVICE_FMT 3
#define DECODER_TYPE_IDX               0
#define ROUTE_FORMAT_IDX               1
#define NUM_OF_PERIODS                 8

enum {
    LPCM,
    MULTI_CH_PCM,
    COMPR,
    TRANSCODE
};


/*
List of indexes of the supported formats
Redundant formats such as (AAC-LC, HEAAC) are removed from the indexes as they
are treated with the AAC format
*/
enum {
    PCM_IDX = 0,
    AAC_IDX,
    AC3_IDX,
    EAC3_IDX,
    DTS_IDX,
    DTS_LBR_IDX,
    MP3_IDX,
    WMA_IDX,
    WMA_PRO_IDX,
    MP2_IDX,
    ALL_FORMATS_IDX
};
/*
List of pass through's supported in the current usecases
*/
enum {
    NO_PASSTHROUGH = 0,
    AC3_PASSTHR,
    EAC3_PASSTHR,
    DTS_PASSTHR
};
/*
List of transcoder's supported in the current usecases
*/
enum {
    NO_TRANSCODER = 0,
    AC3_TRANSCODER,
    DTS_TRANSCODER
};
/*
Requested end device format by user/app through set parameters
*/
enum {
    UNCOMPRESSED = 0,
    COMPRESSED,
    COMPRESSED_CONVERT_EAC3_AC3,
    COMPRESSED_CONVERT_ANY_AC3,
    COMPRESSED_CONVERT_ANY_DTS,
    AUTO_DEVICE_FORMAT,
    UNCOMPRESSED_MCH,  /* not to be exposed, internal use only */
    COMPRESSED_CONVERT_AC3_ASSOC, /* not to be exposed, internal use only */
    ALL_DEVICE_FORMATS
};
/*
List of type of data routed on end device
*/
typedef enum {
    ROUTE_NONE = 0x0,
    ROUTE_UNCOMPRESSED = 0x1,
    ROUTE_COMPRESSED = 0x2,
    ROUTE_SW_TRANSCODED = 0x10,   //route sub-format, not to be used directly
    ROUTE_DSP_TRANSCODED = 0x20,  //route sub-format, not to be used directly
    ROUTE_MCH = 0x40,             //route sub-format, not to be used directly
    ROUTE_UNCOMPRESSED_MCH = (ROUTE_UNCOMPRESSED | ROUTE_MCH),
    ROUTE_SW_TRANSCODED_COMPRESSED = (ROUTE_COMPRESSED | ROUTE_SW_TRANSCODED),
    ROUTE_DSP_TRANSCODED_COMPRESSED = (ROUTE_COMPRESSED | ROUTE_DSP_TRANSCODED)
}route_format_t;
/*
List of end device formats
*/
enum {
    FORMAT_INVALID = -1,
    FORMAT_PCM,
    FORMAT_COMPR
};
/*
Below are the only different types of decode that we perform
*/
enum {
    DSP_DECODE = 1,      // render uncompressed
    DSP_PASSTHROUGH = 2, // render compressed
    DSP_TRANSCODE = 4,   // render as compressed
    SW_DECODE = 8,       // render as uncompressed
    SW_DECODE_MCH = 16,   // render as uncompressed
    SW_PASSTHROUGH = 32, // render compressed
    SW_TRANSCODE = 64,    // render compressed
    NUM_DECODE_PATH = 7
};
/*
Modes of buffering that we can support
As of now, we only support input buffering to an extent specified by usecase
*/
enum {
    NO_BUFFERING_MODE = 0,
    INPUT_BUFFERING_MODE,
    OUTPUT_BUFFEING_MODE
};
/*
playback controls
*/
enum {
    PLAY = 1,
    PAUSE = (1<<1),
    RESUME = (1<<2),
    SEEK = (1<<3),
    EOS = (1<<4),
    STOP = (1<<5),
    STANDBY = (1<<6),
    INIT = (1<<7),
};
/*
Multiple instance of use case
*/
enum {
    STEREO_DRIVER = 0,
    MULTI_CHANNEL_DRIVER,
    COMRPESSED_DRIVER,
};
/*
Instance bits
*/
enum {
    MULTI_CHANNEL_1_BIT = 1<<4,
    MULTI_CHANNEL_2_BIT = 1<<5,
    MULTI_CHANNEL_3_BIT = 1<<6,
    COMPRESSED_1_BIT    = 1<<12,
    COMPRESSED_2_BIT    = 1<<13,
    COMPRESSED_3_BIT    = 1<<14,
    COMPRESSED_4_BIT    = 1<<15,
    COMPRESSED_5_BIT    = 1<<16,
    COMPRESSED_6_BIT    = 1<<17
};

/*
List of support formats configured from frameworks
*/
static const int supportedFormats[NUM_SUPPORTED_CODECS] = {
    AUDIO_FORMAT_PCM_16_BIT,
    AUDIO_FORMAT_PCM_24_BIT,
    AUDIO_FORMAT_AAC,
    AUDIO_FORMAT_HE_AAC_V1,
    AUDIO_FORMAT_HE_AAC_V2,
    AUDIO_FORMAT_AAC_ADIF,
    AUDIO_FORMAT_AC3,
    AUDIO_FORMAT_AC3_DM,
    AUDIO_FORMAT_EAC3,
    AUDIO_FORMAT_EAC3_DM,
    AUDIO_FORMAT_DTS,
    AUDIO_FORMAT_DTS_LBR,
    AUDIO_FORMAT_MP3,
    AUDIO_FORMAT_WMA,
    AUDIO_FORMAT_WMA_PRO,
    AUDIO_FORMAT_MP2
};
/*
we can only have 6 types of decoder type stored with bit masks.
*/
static const int route_to_driver[NUM_DECODE_PATH][NUM_COLUMN_FOR_INDEXING] = {
    {DSP_DECODE,      ROUTE_UNCOMPRESSED_MCH},
    {DSP_PASSTHROUGH, ROUTE_COMPRESSED},
    {DSP_TRANSCODE,   ROUTE_DSP_TRANSCODED_COMPRESSED},
    {SW_DECODE,       ROUTE_UNCOMPRESSED},
    {SW_DECODE_MCH,   ROUTE_UNCOMPRESSED_MCH},
    {SW_PASSTHROUGH,  ROUTE_COMPRESSED},
    {SW_TRANSCODE,    ROUTE_SW_TRANSCODED_COMPRESSED}
};
/*
table to query index based on the format
*/
static const int format_index[NUM_SUPPORTED_CODECS][NUM_COLUMN_FOR_INDEXING] = {
/*---------------------------------------------
|    FORMAT                  | INDEX           |
----------------------------------------------*/
    {AUDIO_FORMAT_PCM_16_BIT,  PCM_IDX},
    {AUDIO_FORMAT_PCM_24_BIT,  PCM_IDX},
    {AUDIO_FORMAT_AAC,         AAC_IDX},
    {AUDIO_FORMAT_HE_AAC_V1,   AAC_IDX},
    {AUDIO_FORMAT_HE_AAC_V2,   AAC_IDX},
    {AUDIO_FORMAT_AAC_ADIF,    AAC_IDX},
    {AUDIO_FORMAT_AC3,         AC3_IDX},
    {AUDIO_FORMAT_AC3_DM,      AC3_IDX},
    {AUDIO_FORMAT_EAC3,        EAC3_IDX},
    {AUDIO_FORMAT_EAC3_DM,     EAC3_IDX},
    {AUDIO_FORMAT_DTS,         DTS_IDX},
    {AUDIO_FORMAT_DTS_LBR,     DTS_LBR_IDX},
    {AUDIO_FORMAT_MP3,         MP3_IDX},
    {AUDIO_FORMAT_WMA,         WMA_IDX},
    {AUDIO_FORMAT_WMA_PRO,     WMA_PRO_IDX},
    {AUDIO_FORMAT_MP2,         MP2_IDX}
};

/*
Table to query non HDMI and SPDIF devices and their states such as type of
decode, type of data routed to end device and type of transcoding needed
*/
static const int usecase_decode_format[ALL_FORMATS_IDX*NUM_STATES_FOR_EACH_DEVICE_FMT] = {
/*-----------------
|    UNCOMPR      |
-----------------*/
/*      PCM    */
    DSP_DECODE,   //PCM_IDX
    FORMAT_PCM,   //ROUTE_FORMAT
    NO_TRANSCODER,//TRANSCODE_FORMAT
/*      PCM    */
    SW_DECODE,    // AAC_IDX
    FORMAT_PCM,   //ROUTE_FORMAT
    NO_TRANSCODER,//TRANSCODE_FORMAT
/*      PCM    */
    SW_DECODE,    //AC3_IDX
    FORMAT_PCM,   //ROUTE_FORMAT
    NO_TRANSCODER,//TRANSCODE_FORMAT
/*      PCM    */
    SW_DECODE,    //EAC3_IDX
    FORMAT_PCM,   //ROUTE_FORMAT
    NO_TRANSCODER,//TRANSCODE_FORMAT
/*      PCM    */
    DSP_DECODE,   //DTS_IDX
    FORMAT_PCM,   //ROUTE_FORMAT
    NO_TRANSCODER,//TRANSCODE_FORMAT
/*      PCM    */
    DSP_DECODE,   //DTS_LBR_IDX
    FORMAT_PCM,   //ROUTE_FORMAT
    NO_TRANSCODER,//TRANSCODE_FORMAT
/*      PCM    */
    DSP_DECODE,   //MP3_IDX
    FORMAT_PCM,   //ROUTE_FORMAT
    NO_TRANSCODER,//TRANSCODE_FORMAT
/*      PCM    */
    DSP_DECODE,   //WMA_IDX
    FORMAT_PCM,   //ROUTE_FORMAT
    NO_TRANSCODER,//TRANSCODE_FORMAT
/*      PCM    */
    DSP_DECODE,   //WMA_PRO_IDX
    FORMAT_PCM,   //ROUTE_FORMAT
    NO_TRANSCODER,//TRANSCODE_FORMAT
/*      PCM    */
    DSP_DECODE,  //MP2_IDX
    FORMAT_PCM,  //ROUTE_FORMAT
    NO_TRANSCODER//TRANSCODE_FORMAT
};
/*
Table to query HDMI and SPDIF devices and their states such as type of
decode, type of data routed to end device and type of transcoding needed
*/
static const int usecase_docode_hdmi_spdif[ALL_FORMATS_IDX*NUM_STATES_FOR_EACH_DEVICE_FMT]
                                [ALL_DEVICE_FORMATS] = {
/*-------------------------------------------------------------------------------------------------------------------------------------------------------
|   UNCOMPRESSED   |     COMPR      |  COMPR_CONV  | COMPR_CONV    |   COMPR_CONV              |      AUTO         | UNCOMPR_MCH    |      AC3_AC3      |
|                  |                |   EAC3_AC3   |   ANY_AC3     |     ANY_DTS               |                   |                |                   |
--------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*   PCM            PCM              PCM             PCM             PCM                         PCM               PCM                 PCM   */
    {DSP_DECODE,    DSP_DECODE,      DSP_DECODE,     DSP_DECODE,     DSP_DECODE|DSP_TRANSCODE,   DSP_DECODE,       DSP_DECODE,         DSP_DECODE},     //PCM_IDX
    {FORMAT_PCM,    FORMAT_PCM,      FORMAT_PCM,     FORMAT_PCM,     FORMAT_COMPR,               FORMAT_PCM,       FORMAT_PCM,         FORMAT_PCM},   //ROUTE_FORMAT
    {NO_TRANSCODER, NO_TRANSCODER,   NO_TRANSCODER,  NO_TRANSCODER,  DTS_TRANSCODER,             NO_TRANSCODER,    NO_TRANSCODER,      NO_TRANSCODER}, //TRANSCODE_FMT
/*   PCM            PCM              PCM             AC3             PCM                         PCM               PCM                 PCM   */
    {SW_DECODE,     SW_DECODE,       SW_DECODE,      SW_TRANSCODE,   DSP_DECODE,                 SW_DECODE,        SW_DECODE_MCH,      SW_DECODE},     //AAC_IDX
    {FORMAT_PCM,    FORMAT_PCM,      FORMAT_PCM,     FORMAT_COMPR,   FORMAT_PCM,                 FORMAT_PCM,       FORMAT_PCM,         FORMAT_PCM},     //ROUTE_FORMAT
    {NO_TRANSCODER, NO_TRANSCODER,   NO_TRANSCODER,  AC3_TRANSCODER, NO_TRANSCODER,              NO_TRANSCODER,    NO_TRANSCODER,      NO_TRANSCODER}, //TRANSCODE_FMT
/*   PCM            AC3              AC3             AC3             PCM                         AC3               PCM                 AC3   */
    {SW_DECODE,     SW_PASSTHROUGH,  SW_PASSTHROUGH, SW_PASSTHROUGH, DSP_DECODE,                 SW_PASSTHROUGH,   SW_DECODE_MCH,      SW_TRANSCODE},     //AC3_IDX
    {FORMAT_PCM,    FORMAT_COMPR,    FORMAT_COMPR,   FORMAT_COMPR,   FORMAT_PCM,                 FORMAT_COMPR,     FORMAT_PCM,         FORMAT_COMPR},     //ROUTE_FORMAT
    {NO_TRANSCODER, AC3_PASSTHR,     AC3_PASSTHR,    AC3_PASSTHR,    NO_TRANSCODER,              AC3_PASSTHR,      NO_TRANSCODER,      AC3_TRANSCODER},  //TRANSCODE_FMT
/*   PCM            EAC3             AC3             AC3             PCM                         EAC3              PCM                 PCM   */
    {SW_DECODE,     SW_PASSTHROUGH,  SW_TRANSCODE,   SW_TRANSCODE,   DSP_DECODE,                 SW_PASSTHROUGH,   SW_DECODE_MCH,      SW_TRANSCODE},     //EAC3_IDX
    {FORMAT_PCM,    FORMAT_COMPR,    FORMAT_COMPR,   FORMAT_COMPR,   FORMAT_PCM,                 FORMAT_COMPR,     FORMAT_PCM,         FORMAT_COMPR},     //ROUTE_FORMAT
    {NO_TRANSCODER, EAC3_PASSTHR,    AC3_TRANSCODER, AC3_TRANSCODER, NO_TRANSCODER,              EAC3_PASSTHR,     NO_TRANSCODER,      AC3_TRANSCODER},  //TRANSCODE_FMT
/*   PCM            DTS              PCM             PCM             DTS                         DTS               PCM                 PCM    */
    {DSP_DECODE,    DSP_PASSTHROUGH, DSP_DECODE,     DSP_DECODE,     DSP_PASSTHROUGH,            DSP_PASSTHROUGH,  DSP_DECODE,         DSP_DECODE},//DTS_IDX
    {FORMAT_PCM,    FORMAT_COMPR,    FORMAT_PCM,     FORMAT_PCM,     FORMAT_COMPR,               FORMAT_COMPR,     FORMAT_PCM,         FORMAT_PCM},   //ROUTE_FORMAT
    {NO_TRANSCODER, DTS_PASSTHR,     NO_TRANSCODER,  NO_TRANSCODER,  DTS_PASSTHR,                DTS_PASSTHR,      NO_TRANSCODER,      NO_TRANSCODER},    //TRANSCODE_FMT
/*   PCM            DTS_LBR          PCM             PCM             DTS                         DTS               PCM                 PCM    */
    {DSP_DECODE,    DSP_PASSTHROUGH, DSP_DECODE,     DSP_DECODE,     DSP_PASSTHROUGH,            DSP_PASSTHROUGH,  DSP_DECODE,         DSP_DECODE},//DTS_LBR_IDX
    {FORMAT_PCM,    FORMAT_COMPR,    FORMAT_PCM,     FORMAT_PCM,     FORMAT_COMPR,               FORMAT_COMPR,     FORMAT_PCM,         FORMAT_PCM},   //ROUTE_FORMAT
    {NO_TRANSCODER, DTS_PASSTHR,     NO_TRANSCODER,  NO_TRANSCODER,  DTS_PASSTHR,                DTS_PASSTHR,      NO_TRANSCODER,      NO_TRANSCODER},    //TRANSCODE_FMT
/*   PCM            PCM              PCM             PCM             DTS                         PCM               PCM                 PCM    */
    {DSP_DECODE,    DSP_DECODE,      DSP_DECODE,     DSP_DECODE,     DSP_DECODE|DSP_TRANSCODE,   DSP_DECODE,       DSP_DECODE,         DSP_DECODE},  //MP3_IDX
    {FORMAT_PCM,    FORMAT_PCM,      FORMAT_PCM,     FORMAT_PCM,     FORMAT_COMPR,               FORMAT_PCM,       FORMAT_PCM,         FORMAT_PCM},   //ROUTE_FORMAT
    {NO_TRANSCODER, NO_TRANSCODER,   NO_TRANSCODER,  NO_TRANSCODER,  DTS_TRANSCODER,             NO_TRANSCODER,    NO_TRANSCODER,      NO_TRANSCODER}, //TRANSCODE_FMT
/*   PCM            PCM              PCM             PCM             DTS                         PCM               PCM                 PCM    */
    {DSP_DECODE,    DSP_DECODE,      DSP_DECODE,     DSP_DECODE,     DSP_DECODE|DSP_TRANSCODE,   DSP_DECODE,       DSP_DECODE,         DSP_DECODE},  //WMA_IDX
    {FORMAT_PCM,    FORMAT_PCM,      FORMAT_PCM,     FORMAT_PCM,     FORMAT_COMPR,               FORMAT_PCM,       FORMAT_PCM,         FORMAT_PCM},   //ROUTE_FORMAT
    {NO_TRANSCODER, NO_TRANSCODER,   NO_TRANSCODER,  NO_TRANSCODER,  DTS_TRANSCODER,             NO_TRANSCODER,    NO_TRANSCODER,      NO_TRANSCODER}, //TRANSCODE_FMT
/*   PCM            PCM              PCM             PCM             DTS                         PCM               PCM                 PCM    */
    {DSP_DECODE,    DSP_DECODE,      DSP_DECODE,     DSP_DECODE,     DSP_DECODE|DSP_TRANSCODE,   DSP_DECODE,       DSP_DECODE,         DSP_DECODE},  //WMA_PRO_IDX
    {FORMAT_PCM,    FORMAT_PCM,      FORMAT_PCM,     FORMAT_PCM,     FORMAT_COMPR,               FORMAT_PCM,       FORMAT_PCM,         FORMAT_PCM},   //ROUTE_FORMAT
    {NO_TRANSCODER, NO_TRANSCODER,   NO_TRANSCODER,  NO_TRANSCODER,  DTS_TRANSCODER,             NO_TRANSCODER,    NO_TRANSCODER,      NO_TRANSCODER}, //TRANSCODE_FMT
/*   PCM            PCM              PCM             PCM             DTS                         PCM               PCM                 PCM    */
    {DSP_DECODE,    DSP_DECODE,      DSP_DECODE,     DSP_DECODE,     DSP_DECODE|DSP_TRANSCODE,   DSP_DECODE,       DSP_DECODE,         DSP_DECODE},  //MP2_IDX
    {FORMAT_PCM,    FORMAT_PCM,      FORMAT_PCM,     FORMAT_PCM,     FORMAT_COMPR,               FORMAT_PCM,       FORMAT_PCM,         FORMAT_PCM},   //ROUTE_FORMAT
    {NO_TRANSCODER, NO_TRANSCODER,   NO_TRANSCODER,  NO_TRANSCODER,  DTS_TRANSCODER,             NO_TRANSCODER,    NO_TRANSCODER,      NO_TRANSCODER}  //TRANSCODE_FMT
};
/*
List of decoders which require config as part of first buffer
*/
static const int decodersRequireConfig[] = {
    AUDIO_FORMAT_AAC,
    AUDIO_FORMAT_HE_AAC_V1,
    AUDIO_FORMAT_HE_AAC_V2,
    AUDIO_FORMAT_AAC_ADIF,
    AUDIO_FORMAT_WMA,
    AUDIO_FORMAT_WMA_PRO
};
/*
List of enum that are used in Broadcast.
NOTE: Need to be removed once broadcast is moved with updated states as above
*/
enum {
    INVALID_FORMAT               = -1,
    PCM_FORMAT                   = 0,
    COMPRESSED_FORMAT            = 1,
    COMPRESSED_FORCED_PCM_FORMAT = 2,
    COMPRESSED_PASSTHROUGH_FORMAT = 3
};


struct audio_bitstream_sm {
    int                buffering_factor;
    int                buffering_factor_cnt;
    // Buffer pointers for input and output
    char               *inp_buf;
    char               *inp_buf_curr_ptr;
    char               *inp_buf_write_ptr;
    uint32_t           inp_buf_size;

    char               *enc_out_buf;
    char               *enc_out_buf_write_ptr;
    uint32_t           enc_out_buf_size;

    char               *pcm_2_out_buf;
    char               *pcm_2_out_buf_write_ptr;
    uint32_t           pcm_2_out_buf_size;

    char               *pcm_mch_out_buf;
    char               *pcm_mch_out_buf_write_ptr;
    uint32_t           pcm_mch_out_buf_size;

    char               *passt_out_buf;
    char               *passt_out_buf_write_ptr;
    uint32_t           passt_out_buf_size;
};

/*
Meta data structure for handling compressed output
*/
struct output_metadata {
    uint32_t            metadataLength;
    uint32_t            bufferLength;
    uint64_t            timestamp;
    uint32_t            reserved[12];
};

#endif // QCOM_AUDIO_PLATFORM_H
