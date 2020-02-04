/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#ifndef ANDROID_HARDWARE_AHAL_ADEVICE_H_
#define ANDROID_HARDWARE_AHAL_ADEVICE_H_

#include <stdlib.h>
#include <unistd.h>

#include <vector>

#include <cutils/properties.h>
#include <hardware/audio.h>
#include <system/audio.h>

#include "AudioStream.h"
#include "AudioVoice.h"

#define COMPRESS_VOIP_IO_BUF_SIZE_NB 320
#define COMPRESS_VOIP_IO_BUF_SIZE_WB 640
#define COMPRESS_VOIP_IO_BUF_SIZE_SWB 1280
#define COMPRESS_VOIP_IO_BUF_SIZE_FB 1920

class AudioDevice {
public:
    ~AudioDevice();
    static std::shared_ptr<AudioDevice> GetInstance();
    static std::shared_ptr<AudioDevice> GetInstance(audio_hw_device_t* device);
    int Init(hw_device_t **device, const hw_module_t *module);
    std::shared_ptr<StreamOutPrimary> CreateStreamOut(
            audio_io_handle_t handle,
            audio_devices_t devices,
            audio_output_flags_t flags,
            struct audio_config *config,
            audio_stream_out **stream_out,
            const char *address);
    void CloseStreamOut(std::shared_ptr<StreamOutPrimary> stream);
    std::shared_ptr<StreamOutPrimary> OutGetStream(audio_io_handle_t handle);
    std::shared_ptr<StreamOutPrimary> OutGetStream(audio_stream_t* audio_stream);
    std::shared_ptr<StreamInPrimary> CreateStreamIn(
            audio_io_handle_t handle,
            audio_devices_t devices,
            audio_input_flags_t flags,
            struct audio_config *config,
            const char *address,
            audio_stream_in **stream_in,
            audio_source_t source);
    void CloseStreamIn(std::shared_ptr<StreamInPrimary> stream);
    std::shared_ptr<StreamInPrimary> InGetStream(audio_io_handle_t handle);
    std::shared_ptr<StreamInPrimary> InGetStream(audio_stream_t* stream_in);
    std::shared_ptr<AudioVoice> voice_;
    int SetMicMute(bool state);
    bool mute_;
    int GetMicMute(bool *state);
    int SetParameters(const char *kvpairs);
    char* GetParameters(const char *keys);
    int SetMode(const audio_mode_t mode);
    int SetVoiceVolume(float volume);

    void FillAndroidDeviceMap();
    int GetQalDeviceIds(
            const audio_devices_t hal_device_id,
            qal_device_id_t* qal_device_id);
    int                       usb_card_id_;
    int                       usb_dev_num_;
protected:
    AudioDevice(){
    }

    std::shared_ptr<AudioVoice> VoiceInit();
    static std::shared_ptr<AudioDevice> adev_;
    static std::shared_ptr<audio_hw_device_t> device_;
    std::vector<std::shared_ptr<StreamOutPrimary>> stream_out_list_;
    std::vector<std::shared_ptr<StreamInPrimary>> stream_in_list_;
    void *offload_effects_lib_;
    offload_effects_start_output fnp_offload_effect_start_output_ = nullptr;
    offload_effects_stop_output fnp_offload_effect_stop_output_ = nullptr;

    std::map<audio_devices_t, qal_device_id_t> android_device_map_;
};

#endif //ANDROID_HARDWARE_AHAL_ADEVICE_H_

