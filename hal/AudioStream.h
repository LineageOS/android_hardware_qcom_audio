/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *   * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ANDROID_HARDWARE_AHAL_ASTREAM_H_
#define ANDROID_HARDWARE_AHAL_ASTREAM_H_

#include <stdlib.h>
#include <unistd.h>

#include <set>
#include <string>

#include <cutils/properties.h>
#include <hardware/audio.h>
#include <system/audio.h>

#include "PalDefs.h"
#include <audio_extn/AudioExtn.h>
#include <mutex>
#include <map>

#define LOW_LATENCY_PLATFORM_DELAY (13*1000LL)
#define DEEP_BUFFER_PLATFORM_DELAY (29*1000LL)
#define PCM_OFFLOAD_PLATFORM_DELAY (30*1000LL)
#define MMAP_PLATFORM_DELAY        (3*1000LL)
#define ULL_PLATFORM_DELAY         (4*1000LL)

#define DEEP_BUFFER_OUTPUT_PERIOD_DURATION 40
#define PCM_OFFLOAD_OUTPUT_PERIOD_DURATION 80
#define LOW_LATENCY_OUTPUT_PERIOD_DURATION 5
#define VOIP_PERIOD_COUNT_DEFAULT 2
#define DEFAULT_VOIP_BUF_DURATION_MS 20
#define DEFAULT_VOIP_BIT_DEPTH_BYTE sizeof(int16_t)
#define COMPRESS_OFFLOAD_PLAYBACK_LATENCY  50

#define DEFAULT_OUTPUT_SAMPLING_RATE    48000
#define LOW_LATENCY_PLAYBACK_PERIOD_SIZE 240 /** 5ms; frames */
#define LOW_LATENCY_PLAYBACK_PERIOD_COUNT 2

#define PCM_OFFLOAD_PLAYBACK_PERIOD_COUNT 2 /** Direct PCM */
#define DEEP_BUFFER_PLAYBACK_PERIOD_COUNT 2 /** Deep Buffer*/

#define DEEP_BUFFER_PLAYBACK_PERIOD_SIZE 1920 /** 40ms; frames */

#define ULL_PERIOD_SIZE (DEFAULT_OUTPUT_SAMPLING_RATE / 1000) /** 1ms; frames */
#define ULL_PERIOD_COUNT_DEFAULT 512
#define ULL_PERIOD_MULTIPLIER 3
#define BUF_SIZE_PLAYBACK 960
#define BUF_SIZE_CAPTURE 960
#define NO_OF_BUF 4
#define LOW_LATENCY_CAPTURE_SAMPLE_RATE 48000
#define LOW_LATENCY_CAPTURE_PERIOD_SIZE 240
#define LOW_LATENCY_OUTPUT_PERIOD_SIZE 240
#define LOW_LATENCY_CAPTURE_USE_CASE 1
#define MIN_PCM_FRAGMENT_SIZE 512
#define MAX_PCM_FRAGMENT_SIZE (240 * 1024)
#define MMAP_PERIOD_SIZE (DEFAULT_OUTPUT_SAMPLING_RATE/1000)
#define MMAP_PERIOD_COUNT_MIN 32
#define MMAP_PERIOD_COUNT_MAX 512
#define MMAP_PERIOD_COUNT_DEFAULT (MMAP_PERIOD_COUNT_MAX)
#define CODEC_BACKEND_DEFAULT_BIT_WIDTH 16
#define AUDIO_CAPTURE_PERIOD_DURATION_MSEC 20

#define LL_PERIOD_SIZE_FRAMES_160 160
#define LL_PERIOD_SIZE_FRAMES_192 192
#define LL_PERIOD_SIZE_FRAMES_240 240
#define LL_PERIOD_SIZE_FRAMES_320 320
#define LL_PERIOD_SIZE_FRAMES_480 480

#define DIV_ROUND_UP(x, y) (((x) + (y) - 1)/(y))
#define ALIGN(x, y) ((y) * DIV_ROUND_UP((x), (y)))

#if LINUX_ENABLED
#ifdef __LP64__
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/usr/lib64/libqcompostprocbundle.so"
#define VISUALIZER_LIBRARY_PATH "/usr/lib64/libqcomvisualizer.so"
#else
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/usr/lib/libqcompostprocbundle.so"
#define VISUALIZER_LIBRARY_PATH "/usr/lib/libqcomvisualizer.so"
#endif
#else
#ifdef __LP64__
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/vendor/lib64/soundfx/libqcompostprocbundle.so"
#define VISUALIZER_LIBRARY_PATH "/vendor/lib64/soundfx/libqcomvisualizer.so"
#else
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/vendor/lib/soundfx/libqcompostprocbundle.so"
#define VISUALIZER_LIBRARY_PATH "/vendor/lib/soundfx/libqcomvisualizer.so"
#endif
#endif

#define AUDIO_PARAMETER_KEY_CAMERA_FACING "cameraFacing"
#define AUDIO_PARAMETER_VALUE_FRONT "front"
#define AUDIO_PARAMETER_VALUE_BACK "back"

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
    USECASE_AUDIO_PLAYBACK_WITH_HAPTICS,
    USECASE_AUDIO_PLAYBACK_HIFI,
    USECASE_AUDIO_PLAYBACK_TTS,

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
    USECASE_AUDIO_DSM_FEEDBACK,

    USECASE_AUDIO_PLAYBACK_SILENCE,

    USECASE_AUDIO_RECORD_ECHO_REF,

    USECASE_AUDIO_TRANSCODE_LOOPBACK_RX,
    USECASE_AUDIO_TRANSCODE_LOOPBACK_TX,

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

    /* car streams usecases */
    USECASE_AUDIO_PLAYBACK_MEDIA,
    USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION,
    USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE,
    USECASE_AUDIO_PLAYBACK_PHONE,

    /*Audio FM Tuner usecase*/
    USECASE_AUDIO_FM_TUNER_EXT,
    AUDIO_USECASE_MAX
};

enum {
    CAMERA_FACING_BACK = 0x0,
    CAMERA_FACING_FRONT = 0x1,
    CAMERA_FACING_MASK = 0x0F,
    CAMERA_ROTATION_LANDSCAPE = 0x0,
    CAMERA_ROTATION_INVERT_LANDSCAPE = 0x10,
    CAMERA_ROTATION_PORTRAIT = 0x20,
    CAMERA_ROTATION_MASK = 0xF0,
    CAMERA_BACK_LANDSCAPE = (CAMERA_FACING_BACK|CAMERA_ROTATION_LANDSCAPE),
    CAMERA_BACK_INVERT_LANDSCAPE = (CAMERA_FACING_BACK|CAMERA_ROTATION_INVERT_LANDSCAPE),
    CAMERA_BACK_PORTRAIT = (CAMERA_FACING_BACK|CAMERA_ROTATION_PORTRAIT),
    CAMERA_FRONT_LANDSCAPE = (CAMERA_FACING_FRONT|CAMERA_ROTATION_LANDSCAPE),
    CAMERA_FRONT_INVERT_LANDSCAPE = (CAMERA_FACING_FRONT|CAMERA_ROTATION_INVERT_LANDSCAPE),
    CAMERA_FRONT_PORTRAIT = (CAMERA_FACING_FRONT|CAMERA_ROTATION_PORTRAIT),
    CAMERA_DEFAULT = CAMERA_BACK_LANDSCAPE,
};

struct string_to_enum {
    const char *name;
    uint32_t value;
};
#define STRING_TO_ENUM(string) { #string, string }

static const struct string_to_enum formats_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_16_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_24_BIT_PACKED),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_32_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_AC3),
    STRING_TO_ENUM(AUDIO_FORMAT_E_AC3),
    STRING_TO_ENUM(AUDIO_FORMAT_E_AC3_JOC),
    STRING_TO_ENUM(AUDIO_FORMAT_DOLBY_TRUEHD),
    STRING_TO_ENUM(AUDIO_FORMAT_DTS),
    STRING_TO_ENUM(AUDIO_FORMAT_DTS_HD),
    STRING_TO_ENUM(AUDIO_FORMAT_IEC61937)
};

static const struct string_to_enum channels_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_2POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_QUAD),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_SURROUND),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_PENTA),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_5POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_6POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_7POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_MONO),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_FRONT_BACK),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_1),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_2),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_3),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_4),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_5),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_6),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_7),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_8),
};

const std::map<uint32_t, pal_audio_fmt_t> getFormatId {
    {AUDIO_FORMAT_PCM_8_BIT,           PAL_AUDIO_FMT_PCM_S8},
    {AUDIO_FORMAT_PCM_16_BIT,          PAL_AUDIO_FMT_PCM_S16_LE},
    {AUDIO_FORMAT_PCM_24_BIT_PACKED,   PAL_AUDIO_FMT_PCM_S24_3LE},
    {AUDIO_FORMAT_PCM_8_24_BIT,        PAL_AUDIO_FMT_PCM_S24_LE},
    {AUDIO_FORMAT_PCM_32_BIT,          PAL_AUDIO_FMT_PCM_S32_LE},
    {AUDIO_FORMAT_MP3,                 PAL_AUDIO_FMT_MP3},
    {AUDIO_FORMAT_AAC,                 PAL_AUDIO_FMT_AAC},
    {AUDIO_FORMAT_AAC_LC,              PAL_AUDIO_FMT_AAC},
    {AUDIO_FORMAT_AAC_ADTS_LC ,        PAL_AUDIO_FMT_AAC},
    {AUDIO_FORMAT_AAC_ADTS_HE_V1,      PAL_AUDIO_FMT_AAC},
    {AUDIO_FORMAT_AAC_ADTS_HE_V2,      PAL_AUDIO_FMT_AAC},
    {AUDIO_FORMAT_AAC_ADTS,            PAL_AUDIO_FMT_AAC_ADTS},
    {AUDIO_FORMAT_AAC_ADIF,            PAL_AUDIO_FMT_AAC_ADIF},
    {AUDIO_FORMAT_AAC_LATM,            PAL_AUDIO_FMT_AAC_LATM},
    {AUDIO_FORMAT_WMA,                 PAL_AUDIO_FMT_WMA_STD},
    {AUDIO_FORMAT_ALAC,                PAL_AUDIO_FMT_ALAC},
    {AUDIO_FORMAT_APE,                 PAL_AUDIO_FMT_APE},
    {AUDIO_FORMAT_WMA_PRO,             PAL_AUDIO_FMT_WMA_PRO},
    {AUDIO_FORMAT_FLAC,                PAL_AUDIO_FMT_FLAC},
    {AUDIO_FORMAT_VORBIS,              PAL_AUDIO_FMT_VORBIS}
};

const uint32_t format_to_bitwidth_table[] = {
    [AUDIO_FORMAT_DEFAULT] = 0,
    [AUDIO_FORMAT_PCM_16_BIT] = 16,
    [AUDIO_FORMAT_PCM_8_BIT] = 8,
    [AUDIO_FORMAT_PCM_32_BIT] = 32,
    [AUDIO_FORMAT_PCM_8_24_BIT] = 32,
    [AUDIO_FORMAT_PCM_FLOAT] = sizeof(float) * 8,
    [AUDIO_FORMAT_PCM_24_BIT_PACKED] = 24,
};

const std::unordered_map<uint32_t, uint32_t> compressRecordBitWidthTable{
    {AUDIO_FORMAT_AAC_LC, 16},
    {AUDIO_FORMAT_AAC_ADTS_LC, 16},
    {AUDIO_FORMAT_AAC_ADTS_HE_V1, 16},
    {AUDIO_FORMAT_AAC_ADTS_HE_V2, 16},
};

const std::map<uint32_t, uint32_t> getAlsaSupportedFmt {
    {AUDIO_FORMAT_PCM_32_BIT,           AUDIO_FORMAT_PCM_32_BIT},
    {AUDIO_FORMAT_PCM_FLOAT,            AUDIO_FORMAT_PCM_32_BIT},
    {AUDIO_FORMAT_PCM_8_24_BIT,         AUDIO_FORMAT_PCM_8_24_BIT},
    {AUDIO_FORMAT_PCM_8_BIT,            AUDIO_FORMAT_PCM_8_BIT},
    {AUDIO_FORMAT_PCM_24_BIT_PACKED,    AUDIO_FORMAT_PCM_24_BIT_PACKED},
    {AUDIO_FORMAT_PCM_16_BIT,           AUDIO_FORMAT_PCM_16_BIT},
};

const char * const use_case_table[AUDIO_USECASE_MAX] = {
    [USECASE_AUDIO_PLAYBACK_DEEP_BUFFER] = "deep-buffer-playback",
    [USECASE_AUDIO_PLAYBACK_LOW_LATENCY] = "low-latency-playback",
    [USECASE_AUDIO_PLAYBACK_WITH_HAPTICS] = "audio-with-haptics-playback",
    [USECASE_AUDIO_PLAYBACK_ULL]         = "audio-ull-playback",
    [USECASE_AUDIO_PLAYBACK_MULTI_CH]    = "multi-channel-playback",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD] = "compress-offload-playback",
    //Enabled for Direct_PCM
    [USECASE_AUDIO_PLAYBACK_OFFLOAD2] = "compress-offload-playback2",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD3] = "compress-offload-playback3",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD4] = "compress-offload-playback4",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD5] = "compress-offload-playback5",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD6] = "compress-offload-playback6",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD7] = "compress-offload-playback7",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD8] = "compress-offload-playback8",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD9] = "compress-offload-playback9",
    [USECASE_AUDIO_PLAYBACK_FM] = "play-fm",
    [USECASE_AUDIO_PLAYBACK_MMAP] = "mmap-playback",
    [USECASE_AUDIO_PLAYBACK_HIFI] = "hifi-playback",
    [USECASE_AUDIO_PLAYBACK_TTS] = "audio-tts-playback",

    [USECASE_AUDIO_RECORD] = "audio-record",
    [USECASE_AUDIO_RECORD_COMPRESS] = "audio-record-compress",
    [USECASE_AUDIO_RECORD_COMPRESS2] = "audio-record-compress2",
    [USECASE_AUDIO_RECORD_COMPRESS3] = "audio-record-compress3",
    [USECASE_AUDIO_RECORD_COMPRESS4] = "audio-record-compress4",
    [USECASE_AUDIO_RECORD_COMPRESS5] = "audio-record-compress5",
    [USECASE_AUDIO_RECORD_COMPRESS6] = "audio-record-compress6",
    [USECASE_AUDIO_RECORD_LOW_LATENCY] = "low-latency-record",
    [USECASE_AUDIO_RECORD_FM_VIRTUAL] = "fm-virtual-record",
    [USECASE_AUDIO_RECORD_MMAP] = "mmap-record",
    [USECASE_AUDIO_RECORD_HIFI] = "hifi-record",

    [USECASE_AUDIO_HFP_SCO] = "hfp-sco",
    [USECASE_AUDIO_HFP_SCO_WB] = "hfp-sco-wb",
    [USECASE_VOICE_CALL] = "voice-call",

    [USECASE_VOICE2_CALL] = "voice2-call",
    [USECASE_VOLTE_CALL] = "volte-call",
    [USECASE_QCHAT_CALL] = "qchat-call",
    [USECASE_VOWLAN_CALL] = "vowlan-call",
    [USECASE_VOICEMMODE1_CALL] = "voicemmode1-call",
    [USECASE_VOICEMMODE2_CALL] = "voicemmode2-call",
    [USECASE_COMPRESS_VOIP_CALL] = "compress-voip-call",
    [USECASE_INCALL_REC_UPLINK] = "incall-rec-uplink",
    [USECASE_INCALL_REC_DOWNLINK] = "incall-rec-downlink",
    [USECASE_INCALL_REC_UPLINK_AND_DOWNLINK] = "incall-rec-uplink-and-downlink",
    [USECASE_INCALL_REC_UPLINK_COMPRESS] = "incall-rec-uplink-compress",
    [USECASE_INCALL_REC_DOWNLINK_COMPRESS] = "incall-rec-downlink-compress",
    [USECASE_INCALL_REC_UPLINK_AND_DOWNLINK_COMPRESS] = "incall-rec-uplink-and-downlink-compress",

    [USECASE_INCALL_MUSIC_UPLINK] = "incall_music_uplink",
    [USECASE_INCALL_MUSIC_UPLINK2] = "incall_music_uplink2",
    [USECASE_AUDIO_SPKR_CALIB_RX] = "spkr-rx-calib",
    [USECASE_AUDIO_SPKR_CALIB_TX] = "spkr-vi-record",

    [USECASE_AUDIO_PLAYBACK_AFE_PROXY] = "afe-proxy-playback",
    [USECASE_AUDIO_RECORD_AFE_PROXY] = "afe-proxy-record",
    [USECASE_AUDIO_PLAYBACK_SILENCE] = "silence-playback",

    [USECASE_AUDIO_RECORD_ECHO_REF] = "echo-ref-record",


    /* Transcode loopback cases */
    [USECASE_AUDIO_TRANSCODE_LOOPBACK_RX] = "audio-transcode-loopback-rx",
    [USECASE_AUDIO_TRANSCODE_LOOPBACK_TX] = "audio-transcode-loopback-tx",

    [USECASE_AUDIO_PLAYBACK_VOIP] = "audio-playback-voip",
    [USECASE_AUDIO_RECORD_VOIP] = "audio-record-voip",
    /* For Interactive Audio Streams */
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM1] = "audio-interactive-stream1",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM2] = "audio-interactive-stream2",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM3] = "audio-interactive-stream3",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM4] = "audio-interactive-stream4",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM5] = "audio-interactive-stream5",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM6] = "audio-interactive-stream6",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM7] = "audio-interactive-stream7",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM8] = "audio-interactive-stream8",

    [USECASE_AUDIO_EC_REF_LOOPBACK] = "ec-ref-audio-capture",

    [USECASE_AUDIO_A2DP_ABR_FEEDBACK] = "a2dp-abr-feedback",

    [USECASE_AUDIO_PLAYBACK_MEDIA] = "media-playback",
    [USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION] = "sys-notification-playback",
    [USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE] = "nav-guidance-playback",
    [USECASE_AUDIO_PLAYBACK_PHONE] = "phone-playback",
    [USECASE_AUDIO_FM_TUNER_EXT] = "fm-tuner-ext",
};

extern "C" typedef void (*hello_t)( const char* text );
extern "C" typedef int (*offload_effects_start_output)(audio_io_handle_t,
                                                       pal_stream_handle_t*);
extern "C" typedef int (*offload_effects_stop_output)(audio_io_handle_t,
                                                      pal_stream_handle_t*);

extern "C" typedef int (*visualizer_hal_start_output)(audio_io_handle_t,
                                                       pal_stream_handle_t*);
extern "C" typedef int (*visualizer_hal_stop_output)(audio_io_handle_t,
                                                      pal_stream_handle_t*);

int adev_open(audio_hw_device_t **device);

class AudioDevice;

class StreamPrimary {
public:
    StreamPrimary(audio_io_handle_t handle,
        const std::set<audio_devices_t> &devices,
        struct audio_config *config);
    virtual ~StreamPrimary();
    uint32_t        GetSampleRate();
    uint32_t        GetBufferSize();
    audio_format_t  GetFormat();
    audio_channel_mask_t GetChannelMask();
    int getPalDeviceIds(const std::set<audio_devices_t> &halDeviceIds, pal_device_id_t* palOutDeviceIds);
    audio_io_handle_t GetHandle();
    int             GetUseCase();
    std::mutex write_wait_mutex_;
    std::condition_variable write_condition_;
    std::mutex stream_mutex_;
    bool write_ready_;
    std::mutex drain_wait_mutex_;
    std::condition_variable drain_condition_;
    bool drain_ready_;
    stream_callback_t client_callback;
    void *client_cookie;
    static int GetDeviceAddress(struct str_parms *parms, int *card_id,
                                 int *device_num);
    int GetLookupTableIndex(const struct string_to_enum *table,
                                        const int table_size, int value);
    bool GetSupportedConfig(bool isOutStream,
                            struct str_parms *query, struct str_parms *reply);
    virtual int RouteStream(const std::set<audio_devices_t>&, bool force_device_switch = false) = 0;
protected:
    struct pal_stream_attributes streamAttributes_;
    pal_stream_handle_t*      pal_stream_handle_;
    audio_io_handle_t         handle_;
    pal_device_id_t           pal_device_id_;
    struct audio_config       config_;
    char                      address_[AUDIO_DEVICE_MAX_ADDRESS_LEN];
    bool                      stream_started_ = false;
    bool                      stream_paused_ = false;
    int usecase_;
    struct pal_volume_data *volume_; /* used to cache volume */
    std::map <audio_devices_t, pal_device_id_t> mAndroidDeviceMap;
    int mmap_shared_memory_fd;
    pal_param_device_capability_t *device_cap_query_;
};

class StreamOutPrimary : public StreamPrimary {
private:
    // Helper function for write to open pal stream & configure.
    ssize_t configurePalOutputStream();
    //Helper method to standby streams upon write failures and sleep for buffer duration.
    ssize_t onWriteError(size_t bytes, ssize_t ret);
    struct pal_device* mPalOutDevice;
    pal_device_id_t* mPalOutDeviceIds;
    std::set<audio_devices_t> mAndroidOutDevices;
    bool mInitialized;
    bool mBypassHaptic;

public:
    StreamOutPrimary(audio_io_handle_t handle,
                     const std::set<audio_devices_t>& devices,
                     audio_output_flags_t flags,
                     struct audio_config *config,
                     const char *address,
                     offload_effects_start_output fnp_start_offload_effect,
                     offload_effects_stop_output fnp_stop_offload_effect,
                     visualizer_hal_start_output fnp_visualizer_start_output_,
                     visualizer_hal_stop_output fnp_visualizer_stop_output_);

    ~StreamOutPrimary();
    bool sendGaplessMetadata = true;
    bool isCompressMetadataAvail = false;
    void UpdatemCachedPosition(uint64_t val);
    int Standby();
    int SetVolume(float left, float right);
    uint64_t GetFramesWritten(struct timespec *timestamp);
    int SetParameters(struct str_parms *parms);
    int Pause();
    int Resume();
    int Drain(audio_drain_type_t type);
    int Flush();
    int Start();
    int Stop();
    ssize_t write(const void *buffer, size_t bytes);
    int Open();
    void GetStreamHandle(audio_stream_out** stream);
    uint32_t GetBufferSize();
    uint32_t GetBufferSizeForLowLatency();
    int GetFrames(uint64_t *frames);
    static pal_stream_type_t GetPalStreamType(audio_output_flags_t halStreamFlags);
    static int64_t GetRenderLatency(audio_output_flags_t halStreamFlags);
    int GetOutputUseCase(audio_output_flags_t halStreamFlags);
    int StartOffloadEffects(audio_io_handle_t, pal_stream_handle_t*);
    int StopOffloadEffects(audio_io_handle_t, pal_stream_handle_t*);
    bool CheckOffloadEffectsType(pal_stream_type_t pal_stream_type);
    int StartOffloadVisualizer(audio_io_handle_t, pal_stream_handle_t*);
    int StopOffloadVisualizer(audio_io_handle_t, pal_stream_handle_t*);
    audio_output_flags_t flags_;
    int CreateMmapBuffer(int32_t min_size_frames, struct audio_mmap_buffer_info *info);
    int GetMmapPosition(struct audio_mmap_position *position);
    bool isDeviceAvailable(pal_device_id_t deviceId);
    int RouteStream(const std::set<audio_devices_t>&, bool force_device_switch = false);
    ssize_t splitAndWriteAudioHapticsStream(const void *buffer, size_t bytes);
    ssize_t BypassHapticAndWriteAudioStream(const void *buffer, size_t bytes);
    bool period_size_is_plausible_for_low_latency(int period_size);
protected:
    struct timespec writeAt;
    int get_compressed_buffer_size();
    int get_pcm_buffer_size();
    audio_format_t halInputFormat = AUDIO_FORMAT_DEFAULT;
    audio_format_t halOutputFormat = AUDIO_FORMAT_DEFAULT;
    uint32_t convertBufSize;
    uint32_t fragments_ = 0;
    uint32_t fragment_size_ = 0;
    pal_snd_dec_t palSndDec;
    struct pal_compr_gapless_mdata gaplessMeta;
    uint32_t msample_rate;
    uint16_t mchannels;
    std::shared_ptr<audio_stream_out>   stream_;
    uint64_t mBytesWritten; /* total bytes written, not cleared when entering standby */
    uint64_t mCachedPosition = 0; /* cache pcm offload position when entering standby */
    offload_effects_start_output fnp_offload_effect_start_output_ = nullptr;
    offload_effects_stop_output fnp_offload_effect_stop_output_ = nullptr;
    visualizer_hal_start_output fnp_visualizer_start_output_ = nullptr;
    visualizer_hal_stop_output fnp_visualizer_stop_output_ = nullptr;
    void *convertBuffer;
    //Haptics Usecase
    struct pal_stream_attributes hapticsStreamAttributes;
    pal_stream_handle_t* pal_haptics_stream_handle;
    AudioExtn AudExtn;
    struct pal_device* hapticsDevice;
    uint8_t* hapticBuffer;
    size_t hapticsBufSize;

    int FillHalFnPtrs();
    friend class AudioDevice;
};

class StreamInPrimary : public StreamPrimary{

private:
     struct pal_device* mPalInDevice;
     pal_device_id_t* mPalInDeviceIds;
     std::set<audio_devices_t> mAndroidInDevices;
     bool mInitialized;
    //Helper method to standby streams upon read failures and sleep for buffer duration.
    ssize_t onReadError(size_t bytes, size_t ret);
public:
    StreamInPrimary(audio_io_handle_t handle,
                    const std::set<audio_devices_t> &devices,
                    audio_input_flags_t flags,
                    struct audio_config *config,
                    const char *address,
                    audio_source_t source);

    ~StreamInPrimary();
    int Standby();
    int SetGain(float gain);
    void GetStreamHandle(audio_stream_in** stream);
    int Open();
    int Start();
    int Stop();
    int SetMicMute(bool mute);
    ssize_t read(const void *buffer, size_t bytes);
    uint32_t GetBufferSize();
    uint32_t GetBufferSizeForLowLatencyRecord();
    pal_stream_type_t GetPalStreamType(audio_input_flags_t halStreamFlags,
            uint32_t sample_rate);
    int GetInputUseCase(audio_input_flags_t halStreamFlags, audio_source_t source);
    int addRemoveAudioEffect(const struct audio_stream *stream, effect_handle_t effect,bool enable);
    int SetParameters(const char *kvpairs);
    bool getParameters(struct str_parms *query, struct str_parms *reply);
    bool is_st_session;
    audio_input_flags_t                 flags_;
    int CreateMmapBuffer(int32_t min_size_frames, struct audio_mmap_buffer_info *info);
    int GetMmapPosition(struct audio_mmap_position *position);
    bool isDeviceAvailable(pal_device_id_t deviceId);
    int RouteStream(const std::set<audio_devices_t>& new_devices, bool force_device_switch = false);
    int64_t GetSourceLatency(audio_input_flags_t halStreamFlags);
    uint64_t GetFramesRead(int64_t *time);
    int GetPalDeviceIds(pal_device_id_t *palDevIds, int *numPalDevs);
protected:
    struct timespec readAt;
    uint32_t fragments_ = 0;
    uint32_t fragment_size_ = 0;
    int FillHalFnPtrs();
    std::shared_ptr<audio_stream_in>    stream_;
    audio_source_t                      source_;
    friend class AudioDevice;
    uint64_t mBytesRead = 0; /* total bytes read, not cleared when entering standby */
    /**
     * number of successful compress read calls
     * correlate to number of PCM frames read in
     * compress record usecase
     * */
    uint64_t mCompressReadCalls = 0;
    int32_t mCompressStreamAdjBitRate;
    bool mIsBitRateSet =false;
    bool mIsBitRateGet = false;
    bool isECEnabled = false;
    bool isNSEnabled = false;
    bool effects_applied_ = true;
    pal_snd_enc_t palSndEnc{};
};
#endif  // ANDROID_HARDWARE_AHAL_ASTREAM_H_
