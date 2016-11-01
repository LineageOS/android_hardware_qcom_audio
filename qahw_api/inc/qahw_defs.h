/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2011 The Android Open Source Project *
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

#include <sys/cdefs.h>
#include <stdint.h>

#ifndef QTI_AUDIO_HAL_DEFS_H
#define QTI_AUDIO_HAL_DEFS_H

__BEGIN_DECLS

/**************************************/

/**
 *  standard audio parameters that the HAL may need to handle
 */

/**
 *  audio device parameters
 */

/* BT SCO Noise Reduction + Echo Cancellation parameters */
#define QAHW_PARAMETER_KEY_BT_NREC "bt_headset_nrec"
#define QAHW_PARAMETER_VALUE_ON "on"
#define QAHW_PARAMETER_VALUE_OFF "off"

/* TTY mode selection */
#define QAHW_PARAMETER_KEY_TTY_MODE "tty_mode"
#define QAHW_PARAMETER_VALUE_TTY_OFF "tty_off"
#define QAHW_PARAMETER_VALUE_TTY_VCO "tty_vco"
#define QAHW_PARAMETER_VALUE_TTY_HCO "tty_hco"
#define QAHW_PARAMETER_VALUE_TTY_FULL "tty_full"

/* Hearing Aid Compatibility - Telecoil (HAC-T) mode on/off
   Strings must be in sync with CallFeaturesSetting.java */
#define QAHW_PARAMETER_KEY_HAC "HACSetting"
#define QAHW_PARAMETER_VALUE_HAC_ON "ON"
#define QAHW_PARAMETER_VALUE_HAC_OFF "OFF"

/* A2DP sink address set by framework */
#define QAHW_PARAMETER_A2DP_SINK_ADDRESS "a2dp_sink_address"

/* A2DP source address set by framework */
#define QAHW_PARAMETER_A2DP_SOURCE_ADDRESS "a2dp_source_address"

/* Screen state */
#define QAHW_PARAMETER_KEY_SCREEN_STATE "screen_state"

/* Bluetooth SCO wideband */
#define QAHW_PARAMETER_KEY_BT_SCO_WB "bt_wbs"

/* Get a new HW synchronization source identifier.
 * Return a valid source (positive integer) or AUDIO_HW_SYNC_INVALID if an error occurs
 * or no HW sync is available. */
#define QAHW_PARAMETER_HW_AV_SYNC "hw_av_sync"

/**
 *  audio stream parameters
 */

#define QAHW_PARAMETER_STREAM_ROUTING "routing"             /* audio_devices_t */
#define QAHW_PARAMETER_STREAM_FORMAT "format"               /* audio_format_t */
#define QAHW_PARAMETER_STREAM_CHANNELS "channels"           /* audio_channel_mask_t */
#define QAHW_PARAMETER_STREAM_FRAME_COUNT "frame_count"     /* size_t */
#define QAHW_PARAMETER_STREAM_INPUT_SOURCE "input_source"   /* audio_source_t */
#define QAHW_PARAMETER_STREAM_SAMPLING_RATE "sampling_rate" /* uint32_t */

#define QAHW_PARAMETER_DEVICE_CONNECT "connect"            /* audio_devices_t */
#define QAHW_PARAMETER_DEVICE_DISCONNECT "disconnect"      /* audio_devices_t */

/* Query supported formats. The response is a '|' separated list of strings from
 * audio_format_t enum e.g: "sup_formats=AUDIO_FORMAT_PCM_16_BIT" */
#define QAHW_PARAMETER_STREAM_SUP_FORMATS "sup_formats"

/* Query supported channel masks. The response is a '|' separated list of
 * strings from audio_channel_mask_t enum
 * e.g: "sup_channels=AUDIO_CHANNEL_OUT_STEREO|AUDIO_CHANNEL_OUT_MONO" */
#define QAHW_PARAMETER_STREAM_SUP_CHANNELS "sup_channels"

/* Query supported sampling rates. The response is a '|' separated list of
 * integer values e.g: "sup_sampling_rates=44100|48000" */
#define QAHW_PARAMETER_STREAM_SUP_SAMPLING_RATES "sup_sampling_rates"

/* Set the HW synchronization source for an output stream. */
#define QAHW_PARAMETER_STREAM_HW_AV_SYNC "hw_av_sync"

/* Enable mono audio playback if 1, else should be 0. */
#define QAHW_PARAMETER_MONO_OUTPUT "mono_output"

/**
 * audio codec parameters
 */

#define QAHW_OFFLOAD_CODEC_PARAMS           "music_offload_codec_param"
#define QAHW_OFFLOAD_CODEC_BIT_PER_SAMPLE   "music_offload_bit_per_sample"
#define QAHW_OFFLOAD_CODEC_BIT_RATE         "music_offload_bit_rate"
#define QAHW_OFFLOAD_CODEC_AVG_BIT_RATE     "music_offload_avg_bit_rate"
#define QAHW_OFFLOAD_CODEC_ID               "music_offload_codec_id"
#define QAHW_OFFLOAD_CODEC_BLOCK_ALIGN      "music_offload_block_align"
#define QAHW_OFFLOAD_CODEC_SAMPLE_RATE      "music_offload_sample_rate"
#define QAHW_OFFLOAD_CODEC_ENCODE_OPTION    "music_offload_encode_option"
#define QAHW_OFFLOAD_CODEC_NUM_CHANNEL      "music_offload_num_channels"
#define QAHW_OFFLOAD_CODEC_DOWN_SAMPLING    "music_offload_down_sampling"
#define QAHW_OFFLOAD_CODEC_DELAY_SAMPLES    "delay_samples"
#define QAHW_OFFLOAD_CODEC_PADDING_SAMPLES  "padding_samples"

/**
 * extended audio codec parameters
 */

#define QAHW_OFFLOAD_CODEC_WMA_FORMAT_TAG "music_offload_wma_format_tag"
#define QAHW_OFFLOAD_CODEC_WMA_BLOCK_ALIGN "music_offload_wma_block_align"
#define QAHW_OFFLOAD_CODEC_WMA_BIT_PER_SAMPLE "music_offload_wma_bit_per_sample"
#define QAHW_OFFLOAD_CODEC_WMA_CHANNEL_MASK "music_offload_wma_channel_mask"
#define QAHW_OFFLOAD_CODEC_WMA_ENCODE_OPTION "music_offload_wma_encode_option"
#define QAHW_OFFLOAD_CODEC_WMA_ENCODE_OPTION1 "music_offload_wma_encode_option1"
#define QAHW_OFFLOAD_CODEC_WMA_ENCODE_OPTION2 "music_offload_wma_encode_option2"

#define QAHW_OFFLOAD_CODEC_FLAC_MIN_BLK_SIZE "music_offload_flac_min_blk_size"
#define QAHW_OFFLOAD_CODEC_FLAC_MAX_BLK_SIZE "music_offload_flac_max_blk_size"
#define QAHW_OFFLOAD_CODEC_FLAC_MIN_FRAME_SIZE "music_offload_flac_min_frame_size"
#define QAHW_OFFLOAD_CODEC_FLAC_MAX_FRAME_SIZE "music_offload_flac_max_frame_size"

#define QAHW_OFFLOAD_CODEC_ALAC_FRAME_LENGTH "music_offload_alac_frame_length"
#define QAHW_OFFLOAD_CODEC_ALAC_COMPATIBLE_VERSION "music_offload_alac_compatible_version"
#define QAHW_OFFLOAD_CODEC_ALAC_BIT_DEPTH "music_offload_alac_bit_depth"
#define QAHW_OFFLOAD_CODEC_ALAC_PB "music_offload_alac_pb"
#define QAHW_OFFLOAD_CODEC_ALAC_MB "music_offload_alac_mb"
#define QAHW_OFFLOAD_CODEC_ALAC_KB "music_offload_alac_kb"
#define QAHW_OFFLOAD_CODEC_ALAC_NUM_CHANNELS "music_offload_alac_num_channels"
#define QAHW_OFFLOAD_CODEC_ALAC_MAX_RUN "music_offload_alac_max_run"
#define QAHW_OFFLOAD_CODEC_ALAC_MAX_FRAME_BYTES "music_offload_alac_max_frame_bytes"
#define QAHW_OFFLOAD_CODEC_ALAC_AVG_BIT_RATE "music_offload_alac_avg_bit_rate"
#define QAHW_OFFLOAD_CODEC_ALAC_SAMPLING_RATE "music_offload_alac_sampling_rate"
#define QAHW_OFFLOAD_CODEC_ALAC_CHANNEL_LAYOUT_TAG "music_offload_alac_channel_layout_tag"

#define QAHW_OFFLOAD_CODEC_APE_COMPATIBLE_VERSION "music_offload_ape_compatible_version"
#define QAHW_OFFLOAD_CODEC_APE_COMPRESSION_LEVEL "music_offload_ape_compression_level"
#define QAHW_OFFLOAD_CODEC_APE_FORMAT_FLAGS "music_offload_ape_format_flags"
#define QAHW_OFFLOAD_CODEC_APE_BLOCKS_PER_FRAME "music_offload_ape_blocks_per_frame"
#define QAHW_OFFLOAD_CODEC_APE_FINAL_FRAME_BLOCKS "music_offload_ape_final_frame_blocks"
#define QAHW_OFFLOAD_CODEC_APE_TOTAL_FRAMES "music_offload_ape_total_frames"
#define QAHW_OFFLOAD_CODEC_APE_BITS_PER_SAMPLE "music_offload_ape_bits_per_sample"
#define QAHW_OFFLOAD_CODEC_APE_NUM_CHANNELS "music_offload_ape_num_channels"
#define QAHW_OFFLOAD_CODEC_APE_SAMPLE_RATE "music_offload_ape_sample_rate"
#define QAHW_OFFLOAD_CODEC_APE_SEEK_TABLE_PRESENT "music_offload_seek_table_present"

#define QAHW_OFFLOAD_CODEC_VORBIS_BITSTREAM_FMT "music_offload_vorbis_bitstream_fmt"

/* Query fm volume */
#define QAHW_PARAMETER_KEY_FM_VOLUME "fm_volume"

/* Query if a2dp  is supported */
#define QAHW_PARAMETER_KEY_HANDLE_A2DP_DEVICE "isA2dpDeviceSupported"

/* type of asynchronous write callback events. Mutually exclusive */
typedef enum {
    QAHW_STREAM_CBK_EVENT_WRITE_READY, /* non blocking write completed */
    QAHW_STREAM_CBK_EVENT_DRAIN_READY  /* drain completed */
} qahw_stream_callback_event_t;

typedef int qahw_stream_callback_t(qahw_stream_callback_event_t event,
                                   void *param,
                                   void *cookie);

/* type of drain requested to audio_stream_out->drain(). Mutually exclusive */
typedef enum {
    QAHW_DRAIN_ALL,            /* drain() returns when all data has been played */
    QAHW_DRAIN_EARLY_NOTIFY    /* drain() returns a short time before all data
                                  from the current track has been played to
                                  give time for gapless track switch */
} qahw_drain_type_t;

/* meta data flags */
/*TBD: Extend this based on stb requirement*/
typedef enum {
 QAHW_META_DATA_FLAGS_NONE = 0,
} qahw_meta_data_flags_t;

typedef struct {
    const void *buffer;    /* write buffer pointer */
    size_t bytes;          /* size of buffer */
    size_t offset;         /* offset in buffer from where valid byte starts */
    int64_t *timestamp;    /* timestmap */
    qahw_meta_data_flags_t flags; /* meta data flags */
    uint32_t reserved[64]; /*reserved for future */
} qahw_out_buffer_t;

typedef struct {
    void *buffer;          /* read buffer pointer */
    size_t bytes;          /* size of buffer */
    size_t offset;         /* offset in buffer from where valid byte starts */
    int64_t *timestamp;    /* timestmap */
    uint32_t reserved[64]; /*reserved for future */
} qahw_in_buffer_t;

__END_DECLS

#endif  // QTI_AUDIO_HAL_DEFS_H

