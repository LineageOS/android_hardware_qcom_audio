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
 */

#ifndef AUDIOEXTN_H
#define AUDIOEXTN_H
#include <atomic>
#include <cutils/str_parms.h>
#include <set>
#include <atomic>
#include <unordered_map>
#include "PalDefs.h"
#include "audio_defs.h"
#include <log/log.h>
#include "battery_listener.h"
#define DEFAULT_OUTPUT_SAMPLING_RATE 48000

typedef void (*batt_listener_init_t)(battery_status_change_fn_t);
typedef void (*batt_listener_deinit_t)();
typedef bool (*batt_prop_is_charging_t)();
typedef bool (*audio_device_cmp_fn_t)(audio_devices_t);

class AudioDevice;
//HFP
typedef int audio_usecase_t;
typedef void(*hfp_init_t)();
typedef bool(*hfp_is_active_t)(std::shared_ptr<AudioDevice> adev);
typedef audio_usecase_t(*hfp_get_usecase_t)();
typedef int(*hfp_set_mic_mute_t)(bool state);
typedef int(*hfp_set_mic_mute2_t)(std::shared_ptr<AudioDevice> adev, bool state);

typedef void (*set_parameters_t) (std::shared_ptr<AudioDevice>, struct str_parms*);
typedef void (*get_parameters_t) (std::shared_ptr<AudioDevice>, struct str_parms*, struct str_parms*);

typedef enum {
    SESSION_UNKNOWN,
    /** A2DP legacy that AVDTP media is encoded by Bluetooth Stack */
    A2DP_SOFTWARE_ENCODING_DATAPATH,
    /** The encoding of AVDTP media is done by HW and there is control only */
    A2DP_HARDWARE_OFFLOAD_DATAPATH,
    /** Used when encoded by Bluetooth Stack and streaming to Hearing Aid */
    HEARING_AID_SOFTWARE_ENCODING_DATAPATH,
    /** Used when encoded by Bluetooth Stack and streaming to LE Audio device */
    LE_AUDIO_SOFTWARE_ENCODING_DATAPATH,
    /** Used when decoded by Bluetooth Stack and streaming to audio framework */
    LE_AUDIO_SOFTWARE_DECODED_DATAPATH,
    /** Encoding is done by HW an there is control only */
    LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH,
    /** Decoding is done by HW an there is control only */
    LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH,
}tSESSION_TYPE;

// start of CompressCapture
class CompressCapture {
   public:
    constexpr static const char *kAudioParameterDSPAacBitRate =
        "dsp_aac_audio_bitrate";
    static const uint32_t kAacPCMSamplesPerFrame = 1024;

    // min and max bitrates supported for AAC mono and stereo
    static const int32_t kAacMonoMinSupportedBitRate = 4000;
    static const int32_t kAacStereoMinSupportedBitRate = 8000;

    static const int32_t kAacMonoMaxSupportedBitRate = 192000;
    static const int32_t kAacStereoMaxSupportedBitRate = 384000;

    static std::vector<uint32_t> sAacSampleRates;
    static std::unordered_map<uint32_t, int32_t> sSampleRateToDefaultBitRate;

    static bool parseMetadata(str_parms *parms, struct audio_config *config,
                              int32_t &compressStreamAdjBitRate);

    static bool getAACMinBitrateValue(uint32_t sampleRate,
                                      uint32_t channelCount, int32_t &minValue);

    static bool getAACMaxBitrateValue(uint32_t sampleRate,
                                      uint32_t channelCount, int32_t &maxValue);

    static bool getAACMaxBufferSize(struct audio_config *config,
                                    uint32_t &maxBufSize);

    CompressCapture() = delete;
    ~CompressCapture() = delete;

    CompressCapture(const CompressCapture&) = delete;
    CompressCapture(CompressCapture&&) = delete;

    CompressCapture& operator=(const CompressCapture&) = delete;
    CompressCapture& operator=(CompressCapture&&) = delete;
};
// end of CompressCapture
class AudioExtn
{
private:
    static int GetProxyParameters(std::shared_ptr<AudioDevice> adev,
            struct str_parms *query, struct str_parms *reply);

public:
    static int audio_extn_parse_compress_metadata(struct audio_config *config_, pal_snd_dec_t *pal_snd_dec,
                                                  str_parms *parms, uint32_t *sr, uint16_t *ch, bool *isCompressMetadataAvail);
    static void audio_extn_get_parameters(std::shared_ptr<AudioDevice> adev, struct str_parms *query, struct str_parms *reply);
    static void audio_extn_set_parameters(std::shared_ptr<AudioDevice> adev, struct str_parms *params);
    static int get_controller_stream_from_params(struct str_parms *parms, int *controller, int *stream);

    static void battery_listener_feature_init(bool is_feature_enabled);
    static void battery_properties_listener_init(battery_status_change_fn_t fn);
    static void battery_properties_listener_deinit();
    static bool battery_properties_is_charging();

    static int audio_extn_hidl_init();

    //HFP
    static int hfp_feature_init(bool is_feature_enabled);
    static bool audio_extn_hfp_is_active(std::shared_ptr<AudioDevice> adev);
    audio_usecase_t audio_extn_hfp_get_usecase();
    static int audio_extn_hfp_set_mic_mute(bool state);
    static void audio_extn_hfp_set_parameters(std::shared_ptr<AudioDevice> adev, struct str_parms *parms);
    static int audio_extn_hfp_set_mic_mute2(std::shared_ptr<AudioDevice> adev, bool state);

    //A2DP
    static int a2dp_source_feature_init(bool is_feature_enabled);

    /* start device utils */
    static bool audio_devices_cmp(const std::set<audio_devices_t>&, audio_device_cmp_fn_t);
    static bool audio_devices_cmp(const std::set<audio_devices_t>&, audio_devices_t);
    static audio_devices_t get_device_types(const std::set<audio_devices_t>&);
    static bool audio_devices_empty(const std::set<audio_devices_t>&);
    /* end device utils */

    // FM
    static void audio_extn_fm_init(bool enabled=true);
    static void audio_extn_fm_set_parameters(std::shared_ptr<AudioDevice> adev, struct str_parms *params);
    static void audio_extn_fm_get_parameters(std::shared_ptr<AudioDevice> adev, struct str_parms *query, struct str_parms *reply);

    //Karaoke
    int karaoke_open(pal_device_id_t device_out, pal_stream_callback pal_callback, pal_channel_info ch_info);
    int karaoke_start();
    int karaoke_stop();
    int karaoke_close();

    /* start kpi optimize perf apis */
    static void audio_extn_kpi_optimize_feature_init(bool is_feature_enabled);
    static int audio_extn_perf_lock_init(void);
    static void audio_extn_perf_lock_acquire(int *handle, int duration,
            int *perf_lock_opts, int size);
    static void audio_extn_perf_lock_release(int *handle);
    /* end kpi optimize perf apis */
    static bool isServiceRegistered() { return sServicesRegistered; }
protected:
    pal_stream_handle_t *karaoke_stream_handle;
    struct pal_stream_attributes sattr;
private:
    static std::atomic<bool> sServicesRegistered;

};

#endif /* AUDIOEXTN_H */
