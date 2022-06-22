/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "AHAL: AudioExtn"
#include <dlfcn.h>
#include <unistd.h>
#include "AudioExtn.h"
#include "AudioDevice.h"
#include "PalApi.h"
#include <cutils/properties.h>
#include "AudioCommon.h"
#define AUDIO_OUTPUT_BIT_WIDTH ((config_->offload_info.bit_width == 32) ? 24:config_->offload_info.bit_width)

#ifdef PAL_HIDL_ENABLED
#include <hidl/HidlTransportSupport.h>
#include <hidl/LegacySupport.h>

#include <pal_server_wrapper.h>

#include <vendor/qti/hardware/pal/1.0/IPAL.h>
using vendor::qti::hardware::pal::V1_0::IPAL;
using vendor::qti::hardware::pal::V1_0::implementation::PAL;
using android::hardware::defaultPassthroughServiceImplementation;
using android::sp;
using namespace android::hardware;
using android::OK;
#endif

#ifdef __LP64__
#define LIBS "/vendor/lib64/"
#else
#define LIBS "/vendor/lib/"
#endif

#define BATTERY_LISTENER_LIB_PATH LIBS"libbatterylistener.so"
#define HFP_LIB_PATH LIBS"libhfp_pal.so"
#define FM_LIB_PATH LIBS"libfmpal.so"

#define BT_IPC_SOURCE_LIB_NAME LIBS"btaudio_offload_if.so"

static batt_listener_init_t batt_listener_init;
static batt_listener_deinit_t batt_listener_deinit;
static batt_prop_is_charging_t batt_prop_is_charging;
static bool battery_listener_enabled;
static void *batt_listener_lib_handle;
static bool audio_extn_kpi_optimize_feature_enabled = false;

std::atomic<bool> AudioExtn::sServicesRegistered = false;

int AudioExtn::audio_extn_parse_compress_metadata(struct audio_config *config_, pal_snd_dec_t *pal_snd_dec,
                               str_parms *parms, uint32_t *sr, uint16_t *ch, bool *isCompressMetadataAvail) {
   int ret = 0;
   char value[32];
   *sr = 0;
   *ch = 0;
   uint16_t flac_sample_size = ((config_->offload_info.bit_width == 32) ? 24:config_->offload_info.bit_width);

   if (config_->offload_info.format == AUDIO_FORMAT_FLAC) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MIN_BLK_SIZE, value, sizeof(value));
        if (ret >= 0) {
            pal_snd_dec->flac_dec.min_blk_size = atoi(value);
            *isCompressMetadataAvail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MAX_BLK_SIZE, value, sizeof(value));
        if (ret >= 0) {
            pal_snd_dec->flac_dec.max_blk_size = atoi(value);
            *isCompressMetadataAvail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MIN_FRAME_SIZE, value, sizeof(value));
        if (ret >= 0) {
            pal_snd_dec->flac_dec.min_frame_size = atoi(value);
            *isCompressMetadataAvail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MAX_FRAME_SIZE, value, sizeof(value));
        if (ret >= 0) {
            pal_snd_dec->flac_dec.max_frame_size = atoi(value);
            *isCompressMetadataAvail = true;
        }
        pal_snd_dec->flac_dec.sample_size = flac_sample_size;
        AHAL_DBG("FLAC metadata: sample_size %d min_blk_size %d, max_blk_size %d min_frame_size %d max_frame_size %d",
              pal_snd_dec->flac_dec.sample_size,
              pal_snd_dec->flac_dec.min_blk_size,
              pal_snd_dec->flac_dec.max_blk_size,
              pal_snd_dec->flac_dec.min_frame_size,
              pal_snd_dec->flac_dec.max_frame_size);
    }

    else if (config_->offload_info.format == AUDIO_FORMAT_ALAC) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_FRAME_LENGTH, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.frame_length = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_COMPATIBLE_VERSION, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.compatible_version = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_BIT_DEPTH, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.bit_depth = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_PB, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.pb = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MB, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.mb = atoi(value);
        }

        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_KB, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.kb = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_NUM_CHANNELS, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.num_channels = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MAX_RUN, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.max_run = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MAX_FRAME_BYTES, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.max_frame_bytes = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_AVG_BIT_RATE, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.avg_bit_rate = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_SAMPLING_RATE, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.sample_rate = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_CHANNEL_LAYOUT_TAG, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->alac_dec.channel_layout_tag = atoi(value);
        }
        *sr = pal_snd_dec->alac_dec.sample_rate;
        *ch = pal_snd_dec->alac_dec.num_channels;
        AHAL_DBG("ALAC CSD values: frameLength %d bitDepth %d numChannels %d"
                " maxFrameBytes %d, avgBitRate %d, sampleRate %d",
                pal_snd_dec->alac_dec.frame_length,
                pal_snd_dec->alac_dec.bit_depth,
                pal_snd_dec->alac_dec.num_channels,
                pal_snd_dec->alac_dec.max_frame_bytes,
                pal_snd_dec->alac_dec.avg_bit_rate,
                pal_snd_dec->alac_dec.sample_rate);
    }

    else if (config_->offload_info.format == AUDIO_FORMAT_APE) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_COMPATIBLE_VERSION, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->ape_dec.compatible_version = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_COMPRESSION_LEVEL, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->ape_dec.compression_level = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_FORMAT_FLAGS, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->ape_dec.format_flags = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_BLOCKS_PER_FRAME, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->ape_dec.blocks_per_frame = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_FINAL_FRAME_BLOCKS, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->ape_dec.final_frame_blocks = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_TOTAL_FRAMES, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->ape_dec.total_frames = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_BITS_PER_SAMPLE, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->ape_dec.bits_per_sample = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_NUM_CHANNELS, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->ape_dec.num_channels = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_SAMPLE_RATE, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->ape_dec.sample_rate = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_SEEK_TABLE_PRESENT, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->ape_dec.seek_table_present = atoi(value);
        }
        *sr = pal_snd_dec->ape_dec.sample_rate;
        *ch = pal_snd_dec->ape_dec.num_channels;
        AHAL_DBG("APE CSD values: compatibleVersion %d compressionLevel %d"
                " formatFlags %d blocksPerFrame %d finalFrameBlocks %d"
                " totalFrames %d bitsPerSample %d numChannels %d"
                " sampleRate %d seekTablePresent %d",
                pal_snd_dec->ape_dec.compatible_version,
                pal_snd_dec->ape_dec.compression_level,
                pal_snd_dec->ape_dec.format_flags,
                pal_snd_dec->ape_dec.blocks_per_frame,
                pal_snd_dec->ape_dec.final_frame_blocks,
                pal_snd_dec->ape_dec.total_frames,
                pal_snd_dec->ape_dec.bits_per_sample,
                pal_snd_dec->ape_dec.num_channels,
                pal_snd_dec->ape_dec.sample_rate,
                pal_snd_dec->ape_dec.seek_table_present);
    }
    else if (config_->offload_info.format == AUDIO_FORMAT_VORBIS) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_VORBIS_BITSTREAM_FMT, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->vorbis_dec.bit_stream_fmt = atoi(value);
        }
    }
    else if (config_->offload_info.format == AUDIO_FORMAT_WMA || config_->offload_info.format == AUDIO_FORMAT_WMA_PRO) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_FORMAT_TAG, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->wma_dec.fmt_tag = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_AVG_BIT_RATE, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->wma_dec.avg_bit_rate = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BLOCK_ALIGN, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->wma_dec.super_block_align = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BIT_PER_SAMPLE, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->wma_dec.bits_per_sample = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_CHANNEL_MASK, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->wma_dec.channelmask = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->wma_dec.encodeopt = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION1, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->wma_dec.encodeopt1 = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION2, value, sizeof(value));
        if (ret >= 0) {
            *isCompressMetadataAvail = true;
            pal_snd_dec->wma_dec.encodeopt2 = atoi(value);
        }
        AHAL_DBG("WMA params: fmt %x, bit rate %x, balgn %x, sr %d, chmsk %x"
                " encop %x, op1 %x, op2 %x",
                pal_snd_dec->wma_dec.fmt_tag,
                pal_snd_dec->wma_dec.avg_bit_rate,
                pal_snd_dec->wma_dec.super_block_align,
                pal_snd_dec->wma_dec.bits_per_sample,
                pal_snd_dec->wma_dec.channelmask,
                pal_snd_dec->wma_dec.encodeopt,
                pal_snd_dec->wma_dec.encodeopt1,
                pal_snd_dec->wma_dec.encodeopt2);
    }

    else if ((config_->offload_info.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC ||
             (config_->offload_info.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC_ADTS ||
             (config_->offload_info.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC_ADIF ||
             (config_->offload_info.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC_LATM) {

       *isCompressMetadataAvail = true;
       pal_snd_dec->aac_dec.audio_obj_type = 29;
       pal_snd_dec->aac_dec.pce_bits_size = 0;
       AHAL_VERBOSE("AAC params: aot %d pce %d", pal_snd_dec->aac_dec.audio_obj_type, pal_snd_dec->aac_dec.pce_bits_size);
       AHAL_VERBOSE("format %x", config_->offload_info.format);
    }
    return 0;
}

int AudioExtn::GetProxyParameters(std::shared_ptr<AudioDevice> adev __unused,
                struct str_parms *query, struct str_parms *reply)
{
    int ret, val = 0;
    char value[32] = {0};

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_CAN_OPEN_PROXY, value,
            sizeof(value));
    if (ret >= 0) {
        val = 1;
        str_parms_add_int(reply, AUDIO_PARAMETER_KEY_CAN_OPEN_PROXY, val);
    }
    AHAL_VERBOSE("called ... can_use_proxy %d", val);
    return 0;
}

void AudioExtn::audio_extn_get_parameters(std::shared_ptr<AudioDevice> adev,
       struct str_parms *query, struct str_parms *reply)
{
    char *kv_pairs = NULL;

    audio_extn_fm_get_parameters(adev, query, reply);
    GetProxyParameters(adev, query, reply);
    kv_pairs = str_parms_to_str(reply);
    if (kv_pairs != NULL) {
        AHAL_VERBOSE("returns %s", kv_pairs);
    }
    free(kv_pairs);
}

void AudioExtn::audio_extn_set_parameters(std::shared_ptr<AudioDevice> adev,
                                     struct str_parms *params){
    audio_extn_hfp_set_parameters(adev, params);
    audio_extn_fm_set_parameters(adev, params);
}

int AudioExtn::get_controller_stream_from_params(struct str_parms *parms,
                                           int *controller, int *stream) {
    if ((str_parms_get_int(parms, "controller", controller) >= 0)
       && (str_parms_get_int(parms, "stream", stream) >=0 )) {
        if (*controller < 0 || *controller >= MAX_CONTROLLERS ||
            *stream < 0 || *stream >= MAX_STREAMS_PER_CONTROLLER) {
            *controller = 0;
            *stream = 0;
            return -EINVAL;
        }
    } else {
        *controller = -1;
        *stream = -1;
    }
    return 0;
}

// START: BATTERY_LISTENER ==================================================

void AudioExtn::battery_listener_feature_init(bool is_feature_enabled) {
    battery_listener_enabled = is_feature_enabled;
    if (is_feature_enabled) {
        batt_listener_lib_handle = dlopen(BATTERY_LISTENER_LIB_PATH, RTLD_NOW);

        if (!batt_listener_lib_handle) {
            AHAL_ERR("dlopen failed");
            goto feature_disabled;
        }
        if (!(batt_listener_init = (batt_listener_init_t)dlsym(
                            batt_listener_lib_handle, "battery_properties_listener_init")) ||
                !(batt_listener_deinit =
                     (batt_listener_deinit_t)dlsym(
                        batt_listener_lib_handle, "battery_properties_listener_deinit")) ||
                !(batt_prop_is_charging =
                     (batt_prop_is_charging_t)dlsym(
                        batt_listener_lib_handle, "battery_properties_is_charging"))) {
             AHAL_ERR("dlsym failed");
                goto feature_disabled;
        }
        AHAL_DBG("---- Feature BATTERY_LISTENER is enabled ----");
        return;
    }

    feature_disabled:
    if (batt_listener_lib_handle) {
        dlclose(batt_listener_lib_handle);
        batt_listener_lib_handle = NULL;
    }

    batt_listener_init = NULL;
    batt_listener_deinit = NULL;
    batt_prop_is_charging = NULL;
    AHAL_INFO("---- Feature BATTERY_LISTENER is disabled ----");
}

void AudioExtn::battery_properties_listener_init(battery_status_change_fn_t fn)
{
    if(batt_listener_init)
        batt_listener_init(fn);
}
void AudioExtn::battery_properties_listener_deinit()
{
    if(batt_listener_deinit)
        batt_listener_deinit();
}
bool AudioExtn::battery_properties_is_charging()
{
    return (batt_prop_is_charging)? batt_prop_is_charging(): false;
}
// END: BATTERY_LISTENER ================================================================

// START: HFP ======================================================================

static void *hfp_lib_handle = NULL;
static hfp_init_t hfp_init;
static hfp_is_active_t hfp_is_active;
static hfp_get_usecase_t hfp_get_usecase;
static hfp_set_mic_mute_t hfp_set_mic_mute;
static set_parameters_t hfp_set_parameters;
static hfp_set_mic_mute2_t hfp_set_mic_mute2;

int AudioExtn::hfp_feature_init(bool is_feature_enabled)
{
    AHAL_DBG("Called with feature %s",
        is_feature_enabled ? "Enabled" : "NOT Enabled");
    if (is_feature_enabled) {
        // dlopen lib
        hfp_lib_handle = dlopen(HFP_LIB_PATH, RTLD_NOW);

        if (!hfp_lib_handle) {
            AHAL_ERR("dlopen failed with: %s", dlerror());
            goto feature_disabled;
        }

        if (!(hfp_init = (hfp_init_t)dlsym(
            hfp_lib_handle, "hfp_init")) ||
            !(hfp_is_active =
            (hfp_is_active_t)dlsym(
                hfp_lib_handle, "hfp_is_active")) ||
            !(hfp_get_usecase =
            (hfp_get_usecase_t)dlsym(
                hfp_lib_handle, "hfp_get_usecase")) ||
            !(hfp_set_mic_mute =
            (hfp_set_mic_mute_t)dlsym(
                hfp_lib_handle, "hfp_set_mic_mute")) ||
            !(hfp_set_mic_mute2 =
            (hfp_set_mic_mute2_t)dlsym(
                hfp_lib_handle, "hfp_set_mic_mute2")) ||
            !(hfp_set_parameters =
            (set_parameters_t)dlsym(
                hfp_lib_handle, "hfp_set_parameters"))) {
            AHAL_ERR("dlsym failed");
            goto feature_disabled;
        }

        AHAL_DBG("---- Feature HFP is Enabled ----");

        return 0;
    }

feature_disabled:
    if (hfp_lib_handle) {
        dlclose(hfp_lib_handle);
        hfp_lib_handle = NULL;
    }

    hfp_init = NULL;
    hfp_is_active = NULL;
    hfp_get_usecase = NULL;
    hfp_set_mic_mute = NULL;
    hfp_set_mic_mute2 = NULL;
    hfp_set_parameters = NULL;

    AHAL_INFO("---- Feature HFP is disabled ----");
    return -ENOSYS;
}

bool AudioExtn::audio_extn_hfp_is_active(std::shared_ptr<AudioDevice> adev)
{
    return ((hfp_is_active) ?
        hfp_is_active(adev) : false);
}

audio_usecase_t AudioExtn::audio_extn_hfp_get_usecase()
{
    return ((hfp_get_usecase) ?
        hfp_get_usecase() : -1);
}

int AudioExtn::audio_extn_hfp_set_mic_mute(bool state)
{
    return ((hfp_set_mic_mute) ?
        hfp_set_mic_mute(state) : -1);
}

void AudioExtn::audio_extn_hfp_set_parameters(std::shared_ptr<AudioDevice> adev,
    struct str_parms *parms)
{
    if (hfp_set_parameters)
        hfp_set_parameters(adev, parms);
}

int AudioExtn::audio_extn_hfp_set_mic_mute2(std::shared_ptr<AudioDevice> adev, bool state)
{
    return ((hfp_set_mic_mute2) ?
        hfp_set_mic_mute2(adev, state) : -1);
}
// END: HFP ========================================================================

// START: A2DP ======================================================================
// Need to call this init for BT HIDL registration. It is expected that Audio HAL
// do need to do this initialization. Hence -
typedef void (*a2dp_bt_audio_pre_init_t)(void);
static void *a2dp_bt_lib_source_handle = NULL;
static a2dp_bt_audio_pre_init_t a2dp_bt_audio_pre_init = nullptr;

int AudioExtn::a2dp_source_feature_init(bool is_feature_enabled)
{
    AHAL_DBG("Called with feature %s",
        is_feature_enabled ? "Enabled" : "NOT Enabled");

    if (is_feature_enabled &&
        (access(BT_IPC_SOURCE_LIB_NAME, R_OK) == 0)) {
        // dlopen lib
        a2dp_bt_lib_source_handle = dlopen(BT_IPC_SOURCE_LIB_NAME, RTLD_NOW);

        if (!a2dp_bt_lib_source_handle) {
            AHAL_ERR("dlopen %s failed with: %s", BT_IPC_SOURCE_LIB_NAME, dlerror());
            goto feature_disabled;
        }

        if (!(a2dp_bt_audio_pre_init = (a2dp_bt_audio_pre_init_t)dlsym(
            a2dp_bt_lib_source_handle, "bt_audio_pre_init")) ) {
            AHAL_ERR("dlsym failed");
            goto feature_disabled;
        }

        if (a2dp_bt_lib_source_handle && a2dp_bt_audio_pre_init) {
            AHAL_DBG("calling BT module preinit");
            // fwk related check's will be done in the BT layer
            a2dp_bt_audio_pre_init();
        }
        AHAL_DBG("---- Feature A2DP offload is Enabled ----");
        return 0;
    }

feature_disabled:
    if (a2dp_bt_lib_source_handle) {
        dlclose(a2dp_bt_lib_source_handle);
        a2dp_bt_lib_source_handle = NULL;
    }

    a2dp_bt_audio_pre_init = nullptr;
    AHAL_INFO("---- Feature A2DP offload is disabled ----");
    return -ENOSYS;
}
// END: A2DP

// START: DEVICE UTILS =============================================================
bool AudioExtn::audio_devices_cmp(const std::set<audio_devices_t>& devs, audio_device_cmp_fn_t fn){
    for(auto dev : devs)
        if(!fn(dev))
            return false;
    return true;
}

bool AudioExtn::audio_devices_cmp(const std::set<audio_devices_t>& devs, audio_devices_t dev){
    for(auto d : devs)
        if(d != dev)
            return false;
    return true;
}

audio_devices_t AudioExtn::get_device_types(const std::set<audio_devices_t>& devs){
    audio_devices_t device = AUDIO_DEVICE_NONE;
    for(auto d : devs)
        device = (audio_devices_t) (device | d);
    return device;
}

bool AudioExtn::audio_devices_empty(const std::set<audio_devices_t>& devs){
    return devs.empty();
}
// END: DEVICE UTILS ===============================================================

// START: KARAOKE ==================================================================
int AudioExtn::karaoke_open(pal_device_id_t device_out, pal_stream_callback pal_callback, pal_channel_info ch_info) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    const int num_pal_devs = 2;
    struct pal_device pal_devs[num_pal_devs];
    karaoke_stream_handle = NULL;
    pal_device_id_t device_in;
    dynamic_media_config_t dynamic_media_config;
    size_t payload_size = 0;

    // Configuring Hostless Loopback
    if (device_out == PAL_DEVICE_OUT_WIRED_HEADSET)
        device_in = PAL_DEVICE_IN_WIRED_HEADSET;
    else if (device_out == PAL_DEVICE_OUT_USB_HEADSET) {
        device_in = PAL_DEVICE_IN_USB_HEADSET;
        // get capability from device of USB
    } else
        return 0;

    sattr.type = PAL_STREAM_LOOPBACK;
    sattr.info.opt_stream_info.loopback_type = PAL_STREAM_LOOPBACK_KARAOKE;
    sattr.direction = PAL_AUDIO_INPUT_OUTPUT;
    sattr.in_media_config.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    sattr.in_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    sattr.in_media_config.ch_info = ch_info;
    sattr.in_media_config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
    sattr.out_media_config.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    sattr.out_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    sattr.out_media_config.ch_info = ch_info;
    sattr.out_media_config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
    for (int i = 0; i < num_pal_devs; ++i) {
        pal_devs[i].id = i ? device_in : device_out;
        if (device_out == PAL_DEVICE_OUT_USB_HEADSET || device_in == PAL_DEVICE_IN_USB_HEADSET) {
            //Configure USB Digital Headset parameters
            pal_param_device_capability_t *device_cap_query = (pal_param_device_capability_t *)
                                                       malloc(sizeof(pal_param_device_capability_t));
            if (!device_cap_query) {
                AHAL_ERR("Failed to allocate mem for device_cap_query");
                return 0;
            }

            if (pal_devs[i].id == PAL_DEVICE_OUT_USB_HEADSET) {
                device_cap_query->id = PAL_DEVICE_OUT_USB_DEVICE;
                device_cap_query->is_playback = true;
            } else {
                device_cap_query->id = PAL_DEVICE_IN_USB_DEVICE;
                device_cap_query->is_playback = false;
            }
            device_cap_query->addr.card_id = adevice->usb_card_id_;
            device_cap_query->addr.device_num = adevice->usb_dev_num_;
            device_cap_query->config = &dynamic_media_config;
            pal_get_param(PAL_PARAM_ID_DEVICE_CAPABILITY,
                                 (void **)&device_cap_query,
                                 &payload_size, nullptr);
            pal_devs[i].address.card_id = adevice->usb_card_id_;
            pal_devs[i].address.device_num = adevice->usb_dev_num_;
            pal_devs[i].config.sample_rate = dynamic_media_config.sample_rate[0];
            pal_devs[i].config.ch_info = ch_info;
            pal_devs[i].config.aud_fmt_id = (pal_audio_fmt_t)dynamic_media_config.format[0];
            free(device_cap_query);
        } else {
            pal_devs[i].config.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
            pal_devs[i].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
            pal_devs[i].config.ch_info = ch_info;
            pal_devs[i].config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
        }
    }
    return pal_stream_open(&sattr,
            num_pal_devs, pal_devs,
            0,
            NULL,
            pal_callback,
            (uint64_t) this,
            &karaoke_stream_handle);
}

int AudioExtn::karaoke_start() {
    return pal_stream_start(karaoke_stream_handle);
}

int AudioExtn::karaoke_stop() {
    return pal_stream_stop(karaoke_stream_handle);
}

int AudioExtn::karaoke_close(){
    return pal_stream_close(karaoke_stream_handle);
}
// END: KARAOKE ====================================================================

// START: PAL HIDL =================================================

int AudioExtn::audio_extn_hidl_init() {

#ifdef PAL_HIDL_ENABLED
   /* register audio PAL HIDL */
    sp<IPAL> service = new PAL();
    /*
     *We request for more threads as the same number of threads would be divided
     *between PAL and audio HAL HIDL
     */
    configureRpcThreadpool(32, false /*callerWillJoin*/);
    if(android::OK !=  service->registerAsService()) {
        AHAL_ERR("Could not register PAL service");
        return -EINVAL;
    } else {
        AHAL_DBG("successfully registered PAL service");
    }
#endif
    /* to register other hidls */
    sServicesRegistered = true;
    return 0;
}


// END: PAL HIDL ===================================================
static set_parameters_t fm_set_params;
static get_parameters_t fm_get_params;
static void* libfm;

void AudioExtn::audio_extn_fm_init(bool enabled)
{

    AHAL_DBG("Enter: enabled: %d", enabled);

    if(enabled){
        if(!libfm)
            libfm = dlopen(FM_LIB_PATH, RTLD_NOW);

        if (!libfm) {
            AHAL_ERR("dlopen failed with: %s", dlerror());
            return;
        }

        fm_set_params = (set_parameters_t) dlsym(libfm, "fm_set_parameters");
        fm_get_params = (get_parameters_t) dlsym(libfm, "fm_get_parameters");

        if(!fm_set_params || !fm_get_params){
            AHAL_ERR("%s", dlerror());
            dlclose(libfm);
        }
    }
    AHAL_DBG("Exit");
}


void AudioExtn::audio_extn_fm_set_parameters(std::shared_ptr<AudioDevice> adev, struct str_parms *params){
    if(fm_set_params)
        fm_set_params(adev, params);
}

void AudioExtn::audio_extn_fm_get_parameters(std::shared_ptr<AudioDevice> adev, struct str_parms *query, struct str_parms *reply){
   if(fm_get_params)
        fm_get_params(adev, query, reply);
}

//START: KPI_OPTIMIZE =============================================================================
void AudioExtn::audio_extn_kpi_optimize_feature_init(bool is_feature_enabled)
{
    audio_extn_kpi_optimize_feature_enabled = is_feature_enabled;
    AHAL_DBG("---- Feature KPI_OPTIMIZE is %s ----", is_feature_enabled? "ENABLED": " NOT ENABLED");
}

typedef int (*perf_lock_acquire_t)(int, int, int*, int);
typedef int (*perf_lock_release_t)(int);

static void *qcopt_handle;
static perf_lock_acquire_t perf_lock_acq;
static perf_lock_release_t perf_lock_rel;

char opt_lib_path[512] = {0};

int AudioExtn::audio_extn_perf_lock_init(void)
{
    int ret = 0;

    //if feature is disabled, exit immediately
    if(!audio_extn_kpi_optimize_feature_enabled)
        goto err;

    if (qcopt_handle == NULL) {
        if (property_get("ro.vendor.extension_library",
                         opt_lib_path, NULL) <= 0) {
            AHAL_ERR("Failed getting perf property");
            ret = -EINVAL;
            goto err;
        }
        if ((qcopt_handle = dlopen(opt_lib_path, RTLD_NOW)) == NULL) {
            AHAL_ERR("Failed to open perf handle");
            ret = -EINVAL;
            goto err;
        } else {
            perf_lock_acq = (perf_lock_acquire_t)dlsym(qcopt_handle,
                                                       "perf_lock_acq");
            if (perf_lock_acq == NULL) {
                AHAL_ERR("Perf lock Acquire NULL");
                dlclose(qcopt_handle);
                ret = -EINVAL;
                goto err;
            }
            perf_lock_rel = (perf_lock_release_t)dlsym(qcopt_handle,
                                                       "perf_lock_rel");
            if (perf_lock_rel == NULL) {
                AHAL_ERR("Perf lock Release NULL");
                dlclose(qcopt_handle);
                ret = -EINVAL;
                goto err;
            }
            ALOGE("%s: Perf lock handles Success \n", __func__);
        }
    }
err:
    return ret;
}

void AudioExtn::audio_extn_perf_lock_acquire(int *handle, int duration,
                                 int *perf_lock_opts, int size)
{
    if (audio_extn_kpi_optimize_feature_enabled)
    {
        if (!perf_lock_opts || !size || !perf_lock_acq || !handle) {
            AHAL_ERR("Incorrect params, Failed to acquire perf lock, err ");
            return;
        }
        /*
         * Acquire performance lock for 1 sec during device path bringup.
         * Lock will be released either after 1 sec or when perf_lock_release
         * function is executed.
         */
        *handle = perf_lock_acq(*handle, duration, perf_lock_opts, size);
        if (*handle <= 0)
            AHAL_ERR("Failed to acquire perf lock, err: %d\n", *handle);
    }
}

void AudioExtn::audio_extn_perf_lock_release(int *handle)
{
    if (audio_extn_kpi_optimize_feature_enabled) {
         if (perf_lock_rel && handle && (*handle > 0)) {
            perf_lock_rel(*handle);
            *handle = 0;
        } else
            AHAL_ERR("Perf lock release error");
    }
}

//END: KPI_OPTIMIZE =============================================================================

