/*
 * Copyright (c) 2014-2015, 2017, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
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

#ifndef AUDIO_DEFS_H
#define AUDIO_DEFS_H


/**
 * extended audio codec parameters
 */

#define AUDIO_OFFLOAD_CODEC_WMA_FORMAT_TAG "music_offload_wma_format_tag"
#define AUDIO_OFFLOAD_CODEC_WMA_BLOCK_ALIGN "music_offload_wma_block_align"
#define AUDIO_OFFLOAD_CODEC_WMA_BIT_PER_SAMPLE "music_offload_wma_bit_per_sample"
#define AUDIO_OFFLOAD_CODEC_WMA_CHANNEL_MASK "music_offload_wma_channel_mask"
#define AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION "music_offload_wma_encode_option"
#define AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION1 "music_offload_wma_encode_option1"
#define AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION2 "music_offload_wma_encode_option2"
#define AUDIO_OFFLOAD_CODEC_FORMAT  "music_offload_codec_format"
#define AUDIO_OFFLOAD_CODEC_FLAC_MIN_BLK_SIZE "music_offload_flac_min_blk_size"
#define AUDIO_OFFLOAD_CODEC_FLAC_MAX_BLK_SIZE "music_offload_flac_max_blk_size"
#define AUDIO_OFFLOAD_CODEC_FLAC_MIN_FRAME_SIZE "music_offload_flac_min_frame_size"
#define AUDIO_OFFLOAD_CODEC_FLAC_MAX_FRAME_SIZE "music_offload_flac_max_frame_size"

#define AUDIO_OFFLOAD_CODEC_ALAC_FRAME_LENGTH "music_offload_alac_frame_length"
#define AUDIO_OFFLOAD_CODEC_ALAC_COMPATIBLE_VERSION "music_offload_alac_compatible_version"
#define AUDIO_OFFLOAD_CODEC_ALAC_BIT_DEPTH "music_offload_alac_bit_depth"
#define AUDIO_OFFLOAD_CODEC_ALAC_PB "music_offload_alac_pb"
#define AUDIO_OFFLOAD_CODEC_ALAC_MB "music_offload_alac_mb"
#define AUDIO_OFFLOAD_CODEC_ALAC_KB "music_offload_alac_kb"
#define AUDIO_OFFLOAD_CODEC_ALAC_NUM_CHANNELS "music_offload_alac_num_channels"
#define AUDIO_OFFLOAD_CODEC_ALAC_MAX_RUN "music_offload_alac_max_run"
#define AUDIO_OFFLOAD_CODEC_ALAC_MAX_FRAME_BYTES "music_offload_alac_max_frame_bytes"
#define AUDIO_OFFLOAD_CODEC_ALAC_AVG_BIT_RATE "music_offload_alac_avg_bit_rate"
#define AUDIO_OFFLOAD_CODEC_ALAC_SAMPLING_RATE "music_offload_alac_sampling_rate"
#define AUDIO_OFFLOAD_CODEC_ALAC_CHANNEL_LAYOUT_TAG "music_offload_alac_channel_layout_tag"

#define AUDIO_OFFLOAD_CODEC_APE_COMPATIBLE_VERSION "music_offload_ape_compatible_version"
#define AUDIO_OFFLOAD_CODEC_APE_COMPRESSION_LEVEL "music_offload_ape_compression_level"
#define AUDIO_OFFLOAD_CODEC_APE_FORMAT_FLAGS "music_offload_ape_format_flags"
#define AUDIO_OFFLOAD_CODEC_APE_BLOCKS_PER_FRAME "music_offload_ape_blocks_per_frame"
#define AUDIO_OFFLOAD_CODEC_APE_FINAL_FRAME_BLOCKS "music_offload_ape_final_frame_blocks"
#define AUDIO_OFFLOAD_CODEC_APE_TOTAL_FRAMES "music_offload_ape_total_frames"
#define AUDIO_OFFLOAD_CODEC_APE_BITS_PER_SAMPLE "music_offload_ape_bits_per_sample"
#define AUDIO_OFFLOAD_CODEC_APE_NUM_CHANNELS "music_offload_ape_num_channels"
#define AUDIO_OFFLOAD_CODEC_APE_SAMPLE_RATE "music_offload_ape_sample_rate"
#define AUDIO_OFFLOAD_CODEC_APE_SEEK_TABLE_PRESENT "music_offload_seek_table_present"

#define AUDIO_OFFLOAD_CODEC_VORBIS_BITSTREAM_FMT "music_offload_vorbis_bitstream_fmt"

/* Query handle fm parameter*/
#define AUDIO_PARAMETER_KEY_HANDLE_FM "handle_fm"

/* Query fm volume */
#define AUDIO_PARAMETER_KEY_FM_VOLUME "fm_volume"

/* Query Fluence type */
#define AUDIO_PARAMETER_KEY_FLUENCE "fluence"
#define AUDIO_PARAMETER_VALUE_QUADMIC "quadmic"
#define AUDIO_PARAMETER_VALUE_DUALMIC "dualmic"
#define AUDIO_PARAMETER_KEY_NO_FLUENCE "none"

/* Query if surround sound recording is supported */
#define AUDIO_PARAMETER_KEY_SSR "ssr"

/* Query if a2dp  is supported */
#define AUDIO_PARAMETER_KEY_HANDLE_A2DP_DEVICE "isA2dpDeviceSupported"

/* Query ADSP Status */
#define AUDIO_PARAMETER_KEY_ADSP_STATUS "ADSP_STATUS"

/* Query Sound Card Status */
#define AUDIO_PARAMETER_KEY_SND_CARD_STATUS "SND_CARD_STATUS"

/* Query if Proxy can be Opend */
#define AUDIO_PARAMETER_KEY_CAN_OPEN_PROXY "can_open_proxy"

#define AUDIO_PARAMETER_IS_HW_DECODER_SESSION_ALLOWED  "is_hw_dec_session_allowed"

/* Set or Query stream profile type */
#define AUDIO_PARAMETER_STREAM_PROFILE "audio_stream_profile"

#define AUDIO_PARAMETER_KEY_VR_AUDIO_MODE "vr_audio_mode_on"

/* audio input flags for compress and timestamp mode.
 * check other input flags defined in audio.h for conflicts
 */
#define AUDIO_INPUT_FLAG_TIMESTAMP 0x80000000
#define AUDIO_INPUT_FLAG_COMPRESS  0x40000000

/* MAX SECTORS for sourcetracking feature */
#define MAX_SECTORS 8

struct source_tracking_param {
    uint8_t   vad[MAX_SECTORS];
    uint16_t  doa_speech;
    uint16_t  doa_noise[3];
    uint8_t   polar_activity[360];
};

struct sound_focus_param {
    uint16_t  start_angle[MAX_SECTORS];
    uint8_t   enable[MAX_SECTORS];
    uint16_t  gain_step;
};

struct aptx_dec_bt_addr {
    uint32_t nap;
    uint32_t uap;
    uint32_t lap;
};

struct aptx_dec_param {
   struct aptx_dec_bt_addr bt_addr;
};

struct audio_avt_device_drift_param {
   /* Flag to indicate if resync is required on the client side for
    * drift correction. Flag is set to TRUE for the first get_param response
    * after device interface starts. This flag value can be used by client
    * to identify if device interface restart has happened and if any
    * re-sync is required at their end for drift correction.
    */
    uint32_t        resync_flag;
    /* Accumulated drift value in microseconds. This value is updated
     * every 100th ms.
     * Positive drift value indicates AV timer is running faster than device.
     * Negative drift value indicates AV timer is running slower than device.
     */
    int32_t         avt_device_drift_value;
    /* Lower 32 bits of the 64-bit absolute timestamp of reference
     * timer in microseconds.
     */
    uint32_t        ref_timer_abs_ts_lsw;
    /* Upper 32 bits of the 64-bit absolute timestamp of reference
     * timer in microseconds.
     */
    uint32_t        ref_timer_abs_ts_msw;
};

/*use these for setting infine window.i.e free run mode */
#define AUDIO_MAX_RENDER_START_WINDOW 0x8000000000000000
#define AUDIO_MAX_RENDER_END_WINDOW   0x7FFFFFFFFFFFFFFF

struct audio_out_render_window_param {
   uint64_t        render_ws; /* render window start value in microseconds*/
   uint64_t        render_we; /* render window end value in microseconds*/
};

struct audio_out_start_delay_param {
   uint64_t        start_delay; /* session start delay in microseconds*/
};

/* type of asynchronous write callback events. Mutually exclusive
 * event enums append those defined for stream_callback_event_t in audio.h */
typedef enum {
    AUDIO_EXTN_STREAM_CBK_EVENT_ADSP = 0x100      /* callback event from ADSP PP,
                                                 * corresponding payload will be
                                                 * sent as is to the client
                                                 */
} audio_extn_callback_id;

#define AUDIO_MAX_ADSP_STREAM_CMD_PAYLOAD_LEN 508

/* payload format for HAL parameter
 * AUDIO_EXTN_PARAM_ADSP_STREAM_CMD
 */
struct audio_adsp_event {
 uint32_t payload_length;                    /* length in bytes of the payload */
 void    *payload;                           /* the actual payload */
};

typedef union {
    struct source_tracking_param st_params;
    struct sound_focus_param sf_params;
    struct aptx_dec_param aptx_params;
    struct audio_avt_device_drift_param drift_params;
    struct audio_out_render_window_param render_window_param;
    struct audio_out_start_delay_param start_delay;
    struct audio_adsp_event adsp_event_params;
} audio_extn_param_payload;

typedef enum {
    AUDIO_EXTN_PARAM_SOURCE_TRACK,
    AUDIO_EXTN_PARAM_SOUND_FOCUS,
    AUDIO_EXTN_PARAM_APTX_DEC,
    AUDIO_EXTN_PARAM_AVT_DEVICE_DRIFT,
    AUDIO_EXTN_PARAM_OUT_RENDER_WINDOW, /* PARAM to set render window */
    AUDIO_EXTN_PARAM_OUT_START_DELAY,
    AUDIO_EXTN_PARAM_ADSP_STREAM_CMD
} audio_extn_param_id;

#endif /* AUDIO_DEFS_H */
