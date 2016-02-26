/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
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

#endif /* AUDIO_DEFS_H */
