/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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
 *
 * This file was modified by DTS, Inc. The portions of the
 * code modified by DTS, Inc are copyrighted and
 * licensed separately, as follows:
 *
 * (C) 2014 DTS, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "AHAL: AudioDevice"
#define ATRACE_TAG (ATRACE_TAG_AUDIO|ATRACE_TAG_HAL)
#define AUDIO_CAPTURE_PERIOD_DURATION_MSEC 20
#define MIN_CHANNEL_COUNT 1
#define MAX_CHANNEL_COUNT 8

#include "AudioCommon.h"

#include "AudioDevice.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <cutils/str_parms.h>

#include <vector>
#include <map>
#include<algorithm>

#include "PalApi.h"
#include "PalDefs.h"

#include <audio_extn/AudioExtn.h>
#include "audio_extn.h"
#include "battery_listener.h"

#define MIC_CHARACTERISTICS_XML_FILE "/vendor/etc/microphone_characteristics.xml"
static pal_device_id_t in_snd_device = PAL_DEVICE_NONE;
microphone_characteristics_t AudioDevice::microphones;
snd_device_to_mic_map_t AudioDevice::microphone_maps[PAL_MAX_INPUT_DEVICES];
bool AudioDevice::mic_characteristics_available = false;

card_status_t AudioDevice::sndCardState = CARD_STATUS_ONLINE;

struct audio_string_to_enum {
    const char* name;
    unsigned int value;
};

static const struct audio_string_to_enum mic_locations[AUDIO_MICROPHONE_LOCATION_CNT] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_UNKNOWN),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_MAINBODY),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_MAINBODY_MOVABLE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_PERIPHERAL),
};

static const struct audio_string_to_enum mic_directionalities[AUDIO_MICROPHONE_DIRECTIONALITY_CNT] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_OMNI),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_BI_DIRECTIONAL),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_UNKNOWN),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_CARDIOID),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_HYPER_CARDIOID),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_SUPER_CARDIOID),
};

static const struct audio_string_to_enum mic_channel_mapping[AUDIO_MICROPHONE_CHANNEL_MAPPING_CNT] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_CHANNEL_MAPPING_DIRECT),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_CHANNEL_MAPPING_PROCESSED),
};

static const struct audio_string_to_enum device_in_types[] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_AMBIENT),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_COMMUNICATION),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BUILTIN_MIC),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_WIRED_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_AUX_DIGITAL),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_HDMI),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_VOICE_CALL),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_TELEPHONY_RX),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BACK_MIC),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_REMOTE_SUBMIX),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_DGTL_DOCK_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_USB_ACCESSORY),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_USB_DEVICE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_FM_TUNER),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_TV_TUNER),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_LINE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_SPDIF),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BLUETOOTH_A2DP),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_LOOPBACK),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_IP),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BUS),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_PROXY),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_USB_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BLUETOOTH_BLE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_DEFAULT),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_ECHO_REFERENCE),
};

enum {
    AUDIO_MICROPHONE_CHARACTERISTIC_NONE = 0u, // 0x0
    AUDIO_MICROPHONE_CHARACTERISTIC_SENSITIVITY = 1u, // 0x1
    AUDIO_MICROPHONE_CHARACTERISTIC_MAX_SPL = 2u, // 0x2
    AUDIO_MICROPHONE_CHARACTERISTIC_MIN_SPL = 4u, // 0x4
    AUDIO_MICROPHONE_CHARACTERISTIC_ORIENTATION = 8u, // 0x8
    AUDIO_MICROPHONE_CHARACTERISTIC_GEOMETRIC_LOCATION = 16u, // 0x10
    AUDIO_MICROPHONE_CHARACTERISTIC_ALL = 31u, /* ((((SENSITIVITY | MAX_SPL) | MIN_SPL)
                                                  | ORIENTATION) | GEOMETRIC_LOCATION) */
};

static bool hdr_set_parameters(std::shared_ptr<AudioDevice> adev,
    struct str_parms *parms) {

    if (adev == nullptr || parms == nullptr) {
        AHAL_ERR("%s Invalid arguments", __func__);
        return false;
    }

    int ret = 0, val = 0;
    char value[32];
    bool changes = false;

    /* HDR Audio Parameters */
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HDR, value,
              sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "true", 4) == 0)
            adev->hdr_record_enabled = true;
        else
            adev->hdr_record_enabled = false;

        changes = true;
        AHAL_INFO("HDR Enabled: %d", adev->hdr_record_enabled);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_WNR, value,
              sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "true", 4) == 0)
            adev->wnr_enabled = true;
        else
            adev->wnr_enabled = false;

        AHAL_INFO("WNR Enabled: %d", adev->wnr_enabled);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_ANS, value,
              sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "true", 4) == 0)
            adev->ans_enabled = true;
        else
            adev->ans_enabled = false;

        AHAL_INFO("ANS Enabled: %d", adev->ans_enabled);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_ORIENTATION, value,
              sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "landscape", 9) == 0)
            adev->orientation_landscape = true;
        else if (strncmp(value, "portrait", 8) == 0)
            adev->orientation_landscape = false;

        changes = true;
        AHAL_INFO("Orientation %s",
            adev->orientation_landscape ? "landscape" : "portrait");
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_INVERTED, value,
              sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "true", 4) == 0)
            adev->inverted = true;
        else
            adev->inverted = false;

        changes = true;
        AHAL_INFO("Orientation inverted: %d", adev->inverted);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FACING, value,
              sizeof(value));
    if (ret >= 0) {
        /*0-none, 1-back, 2-front/selfie*/
        if (strncmp(value, "front", 5) == 0)
            adev->facing = 2;
        else if (strncmp(value, "back", 4) == 0)
            adev->facing = 1;
        else if (strncmp(value, "none", 4) == 0)
            adev->facing = 0;

        changes = true;
        AHAL_INFO("Device facing %s", value);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HDR_CHANNELS, value,
              sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        if (val != 4) {
           AHAL_DBG("Invalid HDR channels: %d", val);
        } else {
            adev->hdr_channel_count = val;
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HDR_SAMPLERATE, value,
              sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        if (val != 48000) {
            AHAL_DBG("Invalid HDR sample rate: %d", val);
        } else {
            adev->hdr_sample_rate = val;
        }
    }

    return changes;
}

static void hdr_get_parameters(std::shared_ptr<AudioDevice> adev,
    struct str_parms *query, struct str_parms *reply) {

    if (adev == nullptr || query == nullptr || reply == nullptr) {
        AHAL_ERR("%s Invalid arguments", __func__);
        return;
    }

    int32_t ret;
    char value[256]={0};
    size_t size = 0;

    /* HDR Audio Parameters */
    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_HDR, value,
              sizeof(value));
    if (ret >= 0) {
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_HDR,
            adev->hdr_record_enabled ? "true" : "false");
        AHAL_VERBOSE("%s=%s", AUDIO_PARAMETER_KEY_HDR,
            adev->hdr_record_enabled ? "true" : "false");
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_WNR, value,
              sizeof(value));
    if (ret >= 0) {
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_WNR, adev->wnr_enabled
            ? "true" : "false");
        AHAL_VERBOSE("%s=%s", AUDIO_PARAMETER_KEY_WNR, adev->wnr_enabled
            ? "true" : "false");
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_ANS, value,
              sizeof(value));
    if (ret >= 0) {
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_ANS, adev->ans_enabled
            ? "true" : "false");
        AHAL_VERBOSE("%s=%s", AUDIO_PARAMETER_KEY_ANS, adev->ans_enabled
            ? "true" : "false");
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_ORIENTATION, value,
              sizeof(value));
    if (ret >= 0) {
        str_parms_add_str(reply,AUDIO_PARAMETER_KEY_ORIENTATION,
            adev->orientation_landscape ? "landscape" : "portrait");
        AHAL_VERBOSE("%s=%s", AUDIO_PARAMETER_KEY_ORIENTATION,
            adev->orientation_landscape ? "landscape" : "portrait");
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_INVERTED, value,
              sizeof(value));
    if (ret >= 0) {
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_INVERTED, adev->inverted
            ? "true" : "false");
        AHAL_VERBOSE("%s=%s", AUDIO_PARAMETER_KEY_INVERTED, adev->inverted
            ? "true" : "false");
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_FACING, value,
              sizeof(value));
    if (ret >= 0) {
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_FACING,
            (adev->facing == 0) ? "none" : ((adev->facing == 1) ?  "back"
                : "front"));
        AHAL_VERBOSE("%s=%s", AUDIO_PARAMETER_KEY_FACING, (adev->facing == 0)
            ? "none" : ((adev->facing == 1) ?  "back" : "front"));
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_HDR_CHANNELS, value,
              sizeof(value));
    if (ret >= 0) {
        str_parms_add_int(reply, AUDIO_PARAMETER_KEY_HDR_CHANNELS,
            adev->hdr_channel_count);
        AHAL_VERBOSE("%s=%d",AUDIO_PARAMETER_KEY_HDR_CHANNELS,
            adev->hdr_channel_count);
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_HDR_SAMPLERATE, value,
              sizeof(value));
    if (ret >= 0) {
        str_parms_add_int(reply, AUDIO_PARAMETER_KEY_HDR_SAMPLERATE,
            adev->hdr_sample_rate);
        AHAL_INFO("%s=%d", AUDIO_PARAMETER_KEY_HDR_SAMPLERATE, adev->hdr_sample_rate);
    }
}

AudioDevice::~AudioDevice() {
    audio_extn_gef_deinit(adev_);
    audio_extn_sound_trigger_deinit(adev_);
    AudioExtn::battery_properties_listener_deinit();
    pal_deinit();
}


std::shared_ptr<AudioDevice> AudioDevice::GetInstance() {
    if (!adev_) {
        adev_ = std::shared_ptr<AudioDevice> (new AudioDevice());
        device_ = std::shared_ptr<audio_hw_device_t> (new audio_hw_device_t());
    }

    return adev_;
}

std::shared_ptr<AudioDevice> AudioDevice::GetInstance(audio_hw_device_t* device) {
    if (device == (audio_hw_device_t*)device_.get())
        return AudioDevice::adev_;
    else
        return NULL;
}

std::shared_ptr<StreamOutPrimary> AudioDevice::CreateStreamOut(
                        audio_io_handle_t handle,
                        const std::set<audio_devices_t>& devices,
                        audio_output_flags_t flags,
                        struct audio_config *config,
                        audio_stream_out **stream_out,
                        const char *address) {
    std::shared_ptr<StreamOutPrimary> astream = nullptr;

    try {
        astream = std::shared_ptr<StreamOutPrimary> (new StreamOutPrimary(handle,
                                       devices, flags, config, address,
                                       fnp_offload_effect_start_output_,
                                       fnp_offload_effect_stop_output_,
                                       fnp_visualizer_start_output_,
                                       fnp_visualizer_stop_output_));
    } catch (const std::exception& e) {
        AHAL_ERR("Failed to create StreamOutPrimary");
        return nullptr;
    }
    astream->GetStreamHandle(stream_out);
    out_list_mutex.lock();
    stream_out_list_.push_back(astream);
    AHAL_DBG("output stream %d %p",(int)stream_out_list_.size(), stream_out);
    if (flags & AUDIO_OUTPUT_FLAG_PRIMARY) {
        if (voice_)
            voice_->stream_out_primary_ = astream;
    }
    out_list_mutex.unlock();
    return astream;
}

void AudioDevice::CloseStreamOut(std::shared_ptr<StreamOutPrimary> stream) {
    out_list_mutex.lock();
    auto iter =
        std::find(stream_out_list_.begin(), stream_out_list_.end(), stream);
    if (iter == stream_out_list_.end()) {
        AHAL_ERR("invalid output stream");
    } else {
        stream_out_list_.erase(iter);
    }
    out_list_mutex.unlock();
}

int AudioDevice::CreateAudioPatch(audio_patch_handle_t *handle,
                                  const std::vector<struct audio_port_config>& sources,
                                  const std::vector<struct audio_port_config>& sinks) {
    int ret = 0;
    bool new_patch = false;
    AudioPatch *patch = NULL;
    std::shared_ptr<StreamPrimary> stream = nullptr;
    AudioPatch::PatchType patch_type = AudioPatch::PATCH_NONE;
    audio_io_handle_t io_handle = AUDIO_IO_HANDLE_NONE;
    audio_source_t input_source = AUDIO_SOURCE_DEFAULT;
    std::set<audio_devices_t> device_types;

    AHAL_DBG("enter: num sources %zu, num_sinks %zu", sources.size(), sinks.size());

    if (!handle || sources.empty() || sources.size() > AUDIO_PATCH_PORTS_MAX ||
        sinks.empty() || sinks.size() > AUDIO_PATCH_PORTS_MAX) {
        AHAL_ERR("Invalid patch arguments");
        ret = -EINVAL;
        goto exit;
    }

    if (sources.size() > 1) {
        AHAL_ERR("error multiple sources are not supported");
        ret = -EINVAL;
        goto exit;
    }

    AHAL_DBG("source role %d, source type %d", sources[0].role, sources[0].type);

    // Populate source/sink information and fetch stream info
    switch (sources[0].type) {
        case AUDIO_PORT_TYPE_DEVICE: // Patch for audio capture or loopback
            device_types.insert(sources[0].ext.device.type);
            if (sinks[0].type == AUDIO_PORT_TYPE_MIX) {
                io_handle = sinks[0].ext.mix.handle;
                input_source = sinks[0].ext.mix.usecase.source;
                patch_type = AudioPatch::PATCH_CAPTURE;
                AHAL_DBG("Capture patch from device %x to mix %d",
                          sources[0].ext.device.type, sinks[0].ext.mix.handle);
            } else {
                /*Device to device patch is not implemented.
                  This space will need changes if audio HAL
                  handles device to device patches in the future.*/
                patch_type = AudioPatch::PATCH_DEVICE_LOOPBACK;
                AHAL_ERR("error device to device patches not supported");
                ret = -ENOSYS;
                goto exit;
            }
            break;
        case AUDIO_PORT_TYPE_MIX: // Patch for audio playback
            io_handle = sources[0].ext.mix.handle;
            for (const auto &sink : sinks)
               device_types.insert(sink.ext.device.type);
            patch_type = AudioPatch::PATCH_PLAYBACK;
            AHAL_DBG("Playback patch from mix handle %d to device %x",
                  io_handle, AudioExtn::get_device_types(device_types));
            break;
        case AUDIO_PORT_TYPE_SESSION:
        case AUDIO_PORT_TYPE_NONE:
            AHAL_ERR("Unsupported source type %d", sources[0].type);
            ret = -EINVAL;
            goto exit;
    }

    if (patch_type == AudioPatch::PATCH_PLAYBACK)
        stream = OutGetStream(io_handle);
    else
        stream = InGetStream(io_handle);

    if(!stream) {
        AHAL_ERR("Failed to fetch stream with io handle %d", io_handle);
        ret = -EINVAL;
        goto exit;
    }

    // empty patch...generate new handle
    if (*handle == AUDIO_PATCH_HANDLE_NONE) {
        patch = new AudioPatch(patch_type, sources, sinks);
        *handle = patch->handle;
        new_patch = true;
    } else {
        std::lock_guard<std::mutex> lock(patch_map_mutex);
        auto it = patch_map_.find(*handle);
        if (it == patch_map_.end()) {
            AHAL_ERR("Unable to fetch patch with handle %d", *handle);
            ret = -EINVAL;
            goto exit;
        }
        patch = &(*it->second);
        patch->type = patch_type;
        patch->sources = sources;
        patch->sinks = sinks;
    }

    if (voice_ && patch_type == AudioPatch::PATCH_PLAYBACK)
        ret = voice_->RouteStream(device_types);
    ret |= stream->RouteStream(device_types);

    if (ret) {
        if (new_patch)
            delete patch;
        AHAL_ERR("Stream routing failed for io_handle %d", io_handle);
    } else if (new_patch) {
        // new patch...add to patch map
        std::lock_guard<std::mutex> lock(patch_map_mutex);
        patch_map_[patch->handle] = patch;
        AHAL_DBG("Added a new patch with handle %d", patch->handle);
    }
exit:
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int AudioDevice::ReleaseAudioPatch(audio_patch_handle_t handle) {
    int ret = 0;
    AudioPatch *patch = NULL;
    std::shared_ptr<StreamPrimary> stream = nullptr;
    audio_io_handle_t io_handle = AUDIO_IO_HANDLE_NONE;
    AudioPatch::PatchType patch_type = AudioPatch::PATCH_NONE;

    AHAL_DBG("Release patch with handle %d", handle);

    if (handle == AUDIO_PATCH_HANDLE_NONE) {
        AHAL_ERR("Invalid patch handle %d", handle);
        return -EINVAL;
    }

    // grab the io_handle from the patch
    patch_map_mutex.lock();
    auto patch_it = patch_map_.find(handle);
    if (patch_it == patch_map_.end() || !patch_it->second) {
        AHAL_ERR("error patch info not found with handle %d", handle);
        patch_map_mutex.unlock();
        return -EINVAL;
    }
    patch = &(*patch_it->second);
    patch_type = patch->type;
    switch (patch->sources[0].type) {
        case AUDIO_PORT_TYPE_MIX:
            io_handle = patch->sources[0].ext.mix.handle;
            break;
        case AUDIO_PORT_TYPE_DEVICE:
            if (patch->type == AudioPatch::PATCH_CAPTURE)
                io_handle = patch->sinks[0].ext.mix.handle;
            break;
        case AUDIO_PORT_TYPE_SESSION:
        case AUDIO_PORT_TYPE_NONE:
            AHAL_DBG("Invalid port type: %d", patch->sources[0].type);
            patch_map_mutex.unlock();
            return -EINVAL;
    }
    patch_map_mutex.unlock();

    if (patch_type == AudioPatch::PATCH_PLAYBACK)
        stream = OutGetStream(io_handle);
    else
        stream = InGetStream(io_handle);

    if (!stream) {
        AHAL_ERR("Failed to fetch stream with io handle %d", io_handle);
        return -EINVAL;
    }

    ret = stream->RouteStream({AUDIO_DEVICE_NONE});

    if (ret)
        AHAL_ERR("Stream routing failed for io_handle %d", io_handle);

    std::lock_guard lock(patch_map_mutex);
    patch_map_.erase(handle);
    delete patch;

    AHAL_DBG("Successfully released patch %d", handle);
    return ret;
}

std::shared_ptr<StreamInPrimary> AudioDevice::CreateStreamIn(
                                        audio_io_handle_t handle,
                                        const std::set<audio_devices_t>& devices,
                                        audio_input_flags_t flags,
                                        struct audio_config *config,
                                        const char *address,
                                        audio_stream_in **stream_in,
                                        audio_source_t source) {
    std::shared_ptr<StreamInPrimary> astream (new StreamInPrimary(handle,
                                              devices, flags, config,
                                              address, source));
    astream->GetStreamHandle(stream_in);
    in_list_mutex.lock();
    stream_in_list_.push_back(astream);
    in_list_mutex.unlock();
    AHAL_DBG("input stream %d %p",(int)stream_in_list_.size(), stream_in);
    return astream;
}

void AudioDevice::CloseStreamIn(std::shared_ptr<StreamInPrimary> stream) {
    in_list_mutex.lock();
    auto iter =
        std::find(stream_in_list_.begin(), stream_in_list_.end(), stream);
    if (iter == stream_in_list_.end()) {
        AHAL_ERR("invalid output stream");
    } else {
        stream_in_list_.erase(iter);
    }
    in_list_mutex.unlock();
}

static int adev_close(hw_device_t *device __unused) {
    return 0;
}

static int adev_init_check(const struct audio_hw_device *dev __unused) {
    return 0;
}

void adev_on_battery_status_changed(bool charging)
{
    std::shared_ptr<AudioDevice>adevice = AudioDevice::GetInstance();
    AHAL_DBG("battery status changed to %scharging",
             charging ? "" : "not ");
    adevice->SetChargingMode(charging);
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume) {
    std::shared_ptr<AudioDevice>adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        AHAL_ERR("invalid adevice object");
        return -EINVAL;
    }

    return adevice->SetVoiceVolume(volume);
}

static int adev_pal_global_callback(uint32_t event_id, uint32_t *event_data,
                                     uint64_t cookie) {
    AHAL_DBG("event_id (%d), event_data (%d), cookie %" PRIu64,
              event_id, *event_data, cookie);
    switch (event_id) {
    case PAL_SND_CARD_STATE :
        AudioDevice::sndCardState = (card_status_t)*event_data;
        AHAL_DBG("sound card status changed %d sndCardState %d",
              *event_data, AudioDevice::sndCardState);
        break;
    default :
       AHAL_ERR("Invalid event id:%d", event_id);
       return -EINVAL;
    }
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                            audio_io_handle_t handle,
                            audio_devices_t devices,
                            audio_output_flags_t flags,
                            struct audio_config *config,
                            struct audio_stream_out **stream_out,
                            const char *address) {
    int32_t ret = 0;
    std::shared_ptr<StreamOutPrimary> astream;

    AHAL_DBG("enter: format(%#x) sample_rate(%d) channel_mask(%#x) devices(%#x)\
        flags(%#x) address(%s)", config->format, config->sample_rate,
        config->channel_mask, devices, flags, address);

    std::shared_ptr<AudioDevice>adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        AHAL_ERR("invalid adevice object");
        goto exit;
    }

    /* This check is added for oflload streams, so that
     * flinger will fallback to DB stream during SSR.
     */
    if (AudioDevice::sndCardState == CARD_STATUS_OFFLINE &&
        (flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD ||
        flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
        AHAL_ERR("error: sound card offline");
        ret = -ENODEV;
        goto exit;
    }

    astream = adevice->OutGetStream(handle);
    if (astream == nullptr) {
        astream = adevice->CreateStreamOut(handle, {devices}, flags, config, stream_out, address);
        if (astream == nullptr) {
            ret = -ENOMEM;
            goto exit;
        }
    }
exit:
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

void adev_close_output_stream(struct audio_hw_device *dev,
                              struct audio_stream_out *stream) {
    std::shared_ptr<StreamOutPrimary> astream_out;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        AHAL_ERR("invalid adevice object");
        return;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        AHAL_ERR("invalid astream_in object");
        return;
    }

    AHAL_DBG("Enter:stream_handle(%p)", astream_out.get());

    adevice->CloseStreamOut(astream_out);

    AHAL_DBG("Exit");
}

void adev_close_input_stream(struct audio_hw_device *dev,
                             struct audio_stream_in *stream)
{
    std::shared_ptr<StreamInPrimary> astream_in;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        AHAL_ERR("invalid adevice object");
        return;
    }

    astream_in = adevice->InGetStream((audio_stream_t*)stream);
    if (!astream_in) {
        AHAL_ERR("invalid astream_in object");
        return;
    }

    AHAL_DBG("Enter:stream_handle(%p)", astream_in.get());

    adevice->CloseStreamIn(astream_in);

    AHAL_DBG("Exit");
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags,
                                  const char *address,
                                  audio_source_t source) {
    int32_t ret = 0;
    bool ret_error = false;
    std::shared_ptr<StreamInPrimary> astream = nullptr;
    AHAL_DBG("enter: sample_rate(%d) channel_mask(%#x) devices(%#x)\
        io_handle(%d) source(%d) format %x", config->sample_rate,
        config->channel_mask, devices, handle, source, config->format);

    std::shared_ptr<AudioDevice>adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        AHAL_ERR("invalid adevice object");
        goto exit;
    }

    /*> 24 bit is restricted to UNPROCESSED source only,also format supported
     * from HAL is 24_packed and 8_24
     *> In case of UNPROCESSED source, for 24 bit, if format requested is other than
     *  24_packed or 8_24 return error indicating supported format is 8_24
     *> In case of any other source requesting 24 bit or float return error
     *  indicating format supported is 16 bit only.
     *> On error flinger will retry with supported format passed
     */
    if ((config->format == AUDIO_FORMAT_PCM_FLOAT) ||
        (config->format == AUDIO_FORMAT_PCM_32_BIT) ||
        (config->format == AUDIO_FORMAT_PCM_24_BIT_PACKED) ||
        (config->format == AUDIO_FORMAT_PCM_8_24_BIT)) {
        if ((source != AUDIO_SOURCE_UNPROCESSED) &&
                (source != AUDIO_SOURCE_CAMCORDER)) {
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            if (config->sample_rate > 48000)
                config->sample_rate = 48000;
            ret_error = true;
        } else if (!(config->format == AUDIO_FORMAT_PCM_24_BIT_PACKED ||
                    config->format == AUDIO_FORMAT_PCM_8_24_BIT)) {
            /*TODO: This can be updated as AUDIO_FORMAT_PCM_24_BIT_PACKED
             * based on what the platform wants to configure.
             */
            config->format = AUDIO_FORMAT_PCM_8_24_BIT;
            ret_error = true;
        }

        if (ret_error) {
            ret = -EINVAL;
            goto exit;
        }
    }

    if (config->format == AUDIO_FORMAT_PCM_FLOAT) {
        AHAL_ERR("format not supported");
        config->format = AUDIO_FORMAT_PCM_16_BIT;
        ret = -EINVAL;
        goto exit;
    }

    astream = adevice->InGetStream(handle);
    if (astream == nullptr)
        astream = adevice->CreateStreamIn(handle, {devices}, flags, config,
                address, stream_in, source);

  exit:
      AHAL_DBG("Exit ret: %d", ret);
      return ret;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    std::shared_ptr<AudioDevice>adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        AHAL_ERR("invalid adevice object");
        return -EINVAL;
    }

    return adevice->SetMode(mode);
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        AHAL_ERR("invalid adevice object");
        return -EINVAL;
    }

    return adevice->SetMicMute(state);
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state) {
    std::shared_ptr<AudioDevice> adevice =
        AudioDevice::GetInstance((audio_hw_device_t *)dev);
    if (!adevice) {
        AHAL_ERR("invalid adevice object");
        return -EINVAL;
    }

    return adevice->GetMicMute(state);
}

static int adev_set_master_volume(struct audio_hw_device *dev __unused,
                                  float volume __unused) {
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev __unused,
                                  float *volume __unused) {
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev __unused,
                                bool muted __unused) {
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev __unused,
                                bool *muted __unused) {
    return -ENOSYS;
}

static int adev_set_parameters(struct audio_hw_device *dev,
                               const char *kvpairs) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        AHAL_ERR("invalid adevice object");
        return -EINVAL;
    }

    return adevice->SetParameters(kvpairs);
}

static char* adev_get_parameters(const struct audio_hw_device *dev,
                                 const char *keys) {
    std::shared_ptr<AudioDevice> adevice =
        AudioDevice::GetInstance((audio_hw_device_t*)dev);
    if (!adevice) {
        AHAL_ERR("invalid adevice object");
        return NULL;
    }

    return adevice->GetParameters(keys);
}

static int check_input_parameters(uint32_t sample_rate,
                                  audio_format_t format,
                                  int channel_count)
{

    int ret = 0;
    static std::vector<int> channel_counts_supported = {1,2,3,4,6,8,10,12,14};
    static std::vector<uint32_t> sample_rate_supported = {8000,11025,12000,16000,
                                                    22050,24000,32000,44100,48000,
                                                    88200,96000,176400,192000 };

    if (((format != AUDIO_FORMAT_PCM_16_BIT) && (format != AUDIO_FORMAT_PCM_8_24_BIT) &&
        (format != AUDIO_FORMAT_PCM_24_BIT_PACKED) && (format != AUDIO_FORMAT_PCM_32_BIT) &&
        (format != AUDIO_FORMAT_PCM_FLOAT))) {
            AHAL_ERR("format not supported!!! format:%d", format);
            return -EINVAL;
    }

    if ((channel_count < MIN_CHANNEL_COUNT) || (channel_count > MAX_CHANNEL_COUNT)) {
        ALOGE("%s: unsupported channel count (%d) passed  Min / Max (%d / %d)", __func__,
                channel_count, MIN_CHANNEL_COUNT, MAX_CHANNEL_COUNT);
        return -EINVAL;
    }

    if ( std::find(channel_counts_supported.begin(),
                   channel_counts_supported.end(),
                   channel_count) == channel_counts_supported.end() ) {
        AHAL_ERR("channel count not supported!!! chanel count:%d", channel_count);
        return -EINVAL;
    }

    if ( std::find(sample_rate_supported.begin(), sample_rate_supported.end(),
                   sample_rate) == sample_rate_supported.end() ) {
        AHAL_ERR("sample rate not supported!!! sample_rate:%d", sample_rate);
        return -EINVAL;
    }

    return ret;
 }

static size_t adev_get_input_buffer_size(
                                const struct audio_hw_device *dev __unused,
                                const struct audio_config *config ) {

    size_t size = 0;
    uint32_t bytes_per_period_sample = 0;
    /* input for compress formats */
    if (config && !audio_is_linear_pcm(config->format)) {
        if (config->format == AUDIO_FORMAT_AAC_LC) {
            return COMPRESS_CAPTURE_AAC_MAX_OUTPUT_BUFFER_SIZE;
        }
        return 0;
    }

    if (config != NULL) {
        int channel_count = audio_channel_count_from_in_mask(config->channel_mask);

        /* Don't know if USB HIFI in this context so use true to be conservative */
        if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0) {
            AHAL_ERR("input parameters not supported!!!");
            return 0;
        }

        size = (config->sample_rate * AUDIO_CAPTURE_PERIOD_DURATION_MSEC) / 1000;
        bytes_per_period_sample = audio_bytes_per_sample(config->format) * channel_count;
        size *= bytes_per_period_sample;
    }
         /* make sure the size is multiple of 32 bytes and additionally multiple of
          * the frame_size (required for 24bit samples and non-power-of-2 channel counts)
          * At 48 kHz mono 16-bit PCM:
          *  5.000 ms = 240 frames = 15*16*1*2 = 480, a whole multiple of 32 (15)
          *  3.333 ms = 160 frames = 10*16*1*2 = 320, a whole multiple of 32 (10)
          * Also, make sure the size is multiple of bytes per period sample
          */
    size = nearest_multiple(size, lcm(32, bytes_per_period_sample));

    return size;

}

int adev_release_audio_patch(struct audio_hw_device *dev,
                             audio_patch_handle_t handle) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        AHAL_ERR("GetInstance() failed");
        return -EINVAL;
    }
    return adevice->ReleaseAudioPatch(handle);
}

int adev_create_audio_patch(struct audio_hw_device *dev,
                            unsigned int num_sources,
                            const struct audio_port_config *sources,
                            unsigned int num_sinks,
                            const struct audio_port_config *sinks,
                            audio_patch_handle_t *handle) {

    if (!handle) {
        AHAL_ERR("Invalid handle");
        return -EINVAL;
    }

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        AHAL_ERR("GetInstance() failed");
        return -EINVAL;
    }

    std::vector<struct audio_port_config> source_vec(sources, sources + num_sources);
    std::vector<struct audio_port_config> sink_vec(sinks, sinks + num_sinks);

    return adevice->CreateAudioPatch(handle, source_vec, sink_vec);
}

int get_audio_port_v7(struct audio_hw_device *dev, struct audio_port_v7 *config) {
    std::ignore = dev;
    std::ignore = config;

    return 0;
}

int adev_set_audio_port_config(struct audio_hw_device *dev,
                               const struct audio_port_config *config)
{
    std::ignore = dev;
    std::ignore = config;

    return 0;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    dprintf(fd, " \n");
    dprintf(fd, "PrimaryHal adev: \n");
    int major =  (device->common.version >> 8) & 0xff;
    int minor =   device->common.version & 0xff;
    dprintf(fd, "Device API Version: %d.%d \n", major, minor);

#ifdef PAL_HIDL_ENABLED
    dprintf(fd, "PAL HIDL enabled");
#else
    dprintf(fd, "PAL HIDL disabled");
#endif

    return 0;
}

static int adev_get_microphones(const struct audio_hw_device *dev __unused,
                struct audio_microphone_characteristic_t *mic_array,
                size_t *mic_count) {
    return AudioDevice::get_microphones(mic_array, mic_count);
}

int AudioDevice::Init(hw_device_t **device, const hw_module_t *module) {
    int ret = 0;
    bool is_charging = false;

#ifdef AGM_HIDL_ENABLED
    /*
     * register HIDL services for PAL & AGM
     * pal_init() depends on AGM, so need to initialize
     * hidl interface before calling to pal_init()
     */
    ret = AudioExtn::audio_extn_hidl_init();
    if (ret) {
        AHAL_ERR("audio_extn_hidl_init failed ret=(%d)", ret);
        return ret;
    }
#endif

    ret = pal_init();
    if (ret) {
        AHAL_ERR("pal_init failed ret=(%d)", ret);
        return -EINVAL;
    }

    ret = pal_register_global_callback(&adev_pal_global_callback, (uint64_t)this);
    if (ret) {
        AHAL_ERR("pal register callback failed ret=(%d)", ret);
    }

#ifndef AGM_HIDL_ENABLED
    /*
     *Once PAL init is sucessfull, register the PAL service
     *from HAL process context
     */
    ret = AudioExtn::audio_extn_hidl_init();
    if (ret) {
        AHAL_ERR("audio_extn_hidl_init failed ret=(%d)", ret);
        return ret;
    }
#endif

    adev_->device_.get()->common.tag = HARDWARE_DEVICE_TAG;
    adev_->device_.get()->common.version = AUDIO_DEVICE_API_VERSION_3_2;
    adev_->device_.get()->common.close = adev_close;
    adev_->device_.get()->init_check = adev_init_check;
    adev_->device_.get()->set_voice_volume = adev_set_voice_volume;
    adev_->device_.get()->set_master_volume = adev_set_master_volume;
    adev_->device_.get()->get_master_volume = adev_get_master_volume;
    adev_->device_.get()->set_master_mute = adev_set_master_mute;
    adev_->device_.get()->get_master_mute = adev_get_master_mute;
    adev_->device_.get()->set_mode = adev_set_mode;
    adev_->device_.get()->set_mic_mute = adev_set_mic_mute;
    adev_->device_.get()->get_mic_mute = adev_get_mic_mute;
    adev_->device_.get()->set_parameters = adev_set_parameters;
    adev_->device_.get()->get_parameters = adev_get_parameters;
    adev_->device_.get()->get_input_buffer_size = adev_get_input_buffer_size;
    adev_->device_.get()->open_output_stream = adev_open_output_stream;
    adev_->device_.get()->close_output_stream = adev_close_output_stream;
    adev_->device_.get()->open_input_stream = adev_open_input_stream;
    adev_->device_.get()->close_input_stream = adev_close_input_stream;
    adev_->device_.get()->create_audio_patch = adev_create_audio_patch;
    adev_->device_.get()->release_audio_patch = adev_release_audio_patch;
    adev_->device_.get()->get_audio_port_v7 = get_audio_port_v7;
    adev_->device_.get()->set_audio_port_config = adev_set_audio_port_config;
    adev_->device_.get()->dump = adev_dump;
    adev_->device_.get()->get_microphones = adev_get_microphones;
    adev_->device_.get()->common.module = (struct hw_module_t *)module;
    *device = &(adev_->device_.get()->common);

    // visualizer lib
    if (access(VISUALIZER_LIBRARY_PATH, R_OK) == 0) {
        visualizer_lib_ = dlopen(VISUALIZER_LIBRARY_PATH, RTLD_NOW);
        if (visualizer_lib_ == NULL) {
            AHAL_ERR("DLOPEN failed for %s", VISUALIZER_LIBRARY_PATH);
        } else {
            AHAL_VERBOSE("DLOPEN successful for %s", VISUALIZER_LIBRARY_PATH);
            fnp_visualizer_start_output_ =
                        (int (*)(audio_io_handle_t, pal_stream_handle_t*))dlsym(visualizer_lib_,
                                                        "visualizer_hal_start_output");
            fnp_visualizer_stop_output_ =
                        (int (*)(audio_io_handle_t, pal_stream_handle_t*))dlsym(visualizer_lib_,
                                                        "visualizer_hal_stop_output");
        }
    }

    // offload effect lib
    if (access(OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH, R_OK) == 0) {
        offload_effects_lib_ = dlopen(OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH,
                                      RTLD_NOW);
        if (offload_effects_lib_ == NULL) {
            AHAL_ERR("DLOPEN failed for %s",
                  OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH);
        } else {
            AHAL_VERBOSE("DLOPEN successful for %s",
                  OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH);
            fnp_offload_effect_start_output_ =
                (int (*)(audio_io_handle_t, pal_stream_handle_t*))dlsym(
                                    offload_effects_lib_,
                                    "offload_effects_bundle_hal_start_output");
            fnp_offload_effect_stop_output_ =
                (int (*)(audio_io_handle_t, pal_stream_handle_t*))dlsym(
                                    offload_effects_lib_,
                                    "offload_effects_bundle_hal_stop_output");
        }
    }
    audio_extn_sound_trigger_init(adev_);
    AudioExtn::hfp_feature_init(property_get_bool("vendor.audio.feature.hfp.enable", false));
    AudioExtn::a2dp_source_feature_init(property_get_bool("vendor.audio.feature.a2dp_offload.enable", false));

    AudioExtn::audio_extn_fm_init();
    AudioExtn::audio_extn_kpi_optimize_feature_init(
            property_get_bool("vendor.audio.feature.kpi_optimize.enable", false));
    AudioExtn::battery_listener_feature_init(
            property_get_bool("vendor.audio.feature.battery_listener.enable", false));
    AudioExtn::battery_properties_listener_init(adev_on_battery_status_changed);
    is_charging = AudioExtn::battery_properties_is_charging();
    SetChargingMode(is_charging);
    AudioExtn::audio_extn_perf_lock_init();
    adev_->perf_lock_opts[0] = 0x40400000;
    adev_->perf_lock_opts[1] = 0x1;
    adev_->perf_lock_opts[2] = 0x40C00000;
    adev_->perf_lock_opts[3] = 0x1;
    adev_->perf_lock_opts_size = 4;

    voice_ = VoiceInit();
    mute_ = false;
    current_rotation = PAL_SPEAKER_ROTATION_LR;

    FillAndroidDeviceMap();
    audio_extn_gef_init(adev_);
    adev_init_ref_count += 1;

    memset(&microphones, 0, sizeof(microphone_characteristics_t));
    memset(&microphone_maps, 0, sizeof(PAL_MAX_INPUT_DEVICES*sizeof(snd_device_to_mic_map_t)));
    if (!parse_xml())
        mic_characteristics_available = true;

    return ret;
}

std::shared_ptr<AudioVoice> AudioDevice::VoiceInit() {
    std::shared_ptr<AudioVoice> voice (new AudioVoice());

    return voice;

}

int AudioDevice::SetGEFParam(void *data, int length) {
    return pal_set_param(PAL_PARAM_ID_UIEFFECT, data, length);
}


int AudioDevice::GetGEFParam(void *data, int *length) {
    return pal_get_param(PAL_PARAM_ID_UIEFFECT, nullptr, (size_t *)length, data);
}

std::shared_ptr<StreamOutPrimary> AudioDevice::OutGetStream(audio_io_handle_t handle) {
    std::shared_ptr<StreamOutPrimary> astream_out = NULL;
    out_list_mutex.lock();
    for (int i = 0; i < stream_out_list_.size(); i++) {
        if (stream_out_list_[i]->handle_ == handle) {
            AHAL_INFO("Found existing stream associated with iohandle %d",
                      handle);
            astream_out = stream_out_list_[i];
            break;
        }
    }

    out_list_mutex.unlock();
    return astream_out;
}

std::shared_ptr<StreamOutPrimary> AudioDevice::OutGetStream(audio_stream_t* stream_out) {

    std::shared_ptr<StreamOutPrimary> astream_out;
    AHAL_VERBOSE("stream_out(%p)", stream_out);
    out_list_mutex.lock();
    for (int i = 0; i < stream_out_list_.size(); i++) {
        if (stream_out_list_[i]->stream_.get() ==
                                        (audio_stream_out*) stream_out) {
            AHAL_VERBOSE("Found stream associated with stream_out");
            astream_out = stream_out_list_[i];
            break;
        }
    }
    out_list_mutex.unlock();
    AHAL_VERBOSE("astream_out(%p)", astream_out->stream_.get());

    return astream_out;
}

std::shared_ptr<StreamInPrimary> AudioDevice::InGetStream (audio_io_handle_t handle) {
    std::shared_ptr<StreamInPrimary> astream_in = NULL;
    in_list_mutex.lock();
    for (int i = 0; i < stream_in_list_.size(); i++) {
        if (stream_in_list_[i]->handle_ == handle) {
            AHAL_INFO("Found existing stream associated with iohandle %d",
                      handle);
            astream_in = stream_in_list_[i];
            break;
        }
    }
    in_list_mutex.unlock();
    return astream_in;
}

std::shared_ptr<StreamInPrimary> AudioDevice::InGetStream (audio_stream_t* stream_in) {
    std::shared_ptr<StreamInPrimary> astream_in;

    AHAL_VERBOSE("stream_in(%p)", stream_in);
    in_list_mutex.lock();
    for (int i = 0; i < stream_in_list_.size(); i++) {
        if (stream_in_list_[i]->stream_.get() == (audio_stream_in*) stream_in) {
            AHAL_VERBOSE("Found existing stream associated with astream_in");
            astream_in = stream_in_list_[i];
            break;
        }
    }
    in_list_mutex.unlock();
    AHAL_VERBOSE("astream_in(%p)", astream_in->stream_.get());
    return astream_in;
}

int AudioDevice::SetMicMute(bool state) {
    int ret = 0;
    std::shared_ptr<StreamInPrimary> astream_in;
    mute_ = state;

    AHAL_DBG("%s: enter: %d", __func__, state);
    if (voice_)
        ret = voice_->SetMicMute(state);
    for (int i = 0; i < stream_in_list_.size(); i++) {
         astream_in = stream_in_list_[i];
         if (astream_in) {
             AHAL_VERBOSE("Found existing stream associated with astream_in");
             ret = astream_in->SetMicMute(state);
         }
    }

    AHAL_DBG("exit: ret %d", ret);
    return ret;
}

int AudioDevice::GetMicMute(bool *state) {
    *state = mute_;

    return 0;
}

int AudioDevice::SetMode(const audio_mode_t mode) {
    int ret = 0;

    AHAL_DBG("enter: mode: %d", mode);
    ret = voice_->SetMode(mode);
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int AudioDevice::add_input_headset_if_usb_out_headset(int *device_count,
                              pal_device_id_t** pal_device_ids, bool conn_state)
{
    bool is_usb_headset = false;
    int count = *device_count;
    pal_device_id_t* temp = NULL;

    for (int i = 0; i < count; i++) {
         if (*pal_device_ids[i] == PAL_DEVICE_OUT_USB_HEADSET) {
             is_usb_headset = true;
             usb_out_headset = true;
             break;
         }
    }

    if (is_usb_headset) {
        temp = (pal_device_id_t *) realloc(*pal_device_ids,
            (count + 1) * sizeof(pal_device_id_t));
        if (!temp)
            return -ENOMEM;
        *pal_device_ids = temp;
        temp[count] = PAL_DEVICE_IN_USB_HEADSET;
        *device_count = count + 1;
        if (conn_state)
           usb_input_dev_enabled = true;
        else {
           usb_input_dev_enabled = false;
           usb_out_headset = false;
        }
    }
    return 0;
}

int AudioDevice::SetParameters(const char *kvpairs) {
    int ret = 0, val = 0;
    struct str_parms *parms = NULL;
    char value[256];
    int pal_device_count = 0;
    pal_device_id_t* pal_device_ids = NULL;
    char *test_r = NULL;
    char *cfg_str = NULL;
    bool changes_done = false;
    audio_stream_in* stream_in = NULL;
    std::shared_ptr<StreamInPrimary> astream_in = NULL;
    uint8_t channels = 0;
    std::set<audio_devices_t> new_devices;

    AHAL_DBG("enter: %s", kvpairs);
    ret = voice_->VoiceSetParameters(kvpairs);
    if (ret)
        AHAL_ERR("Error in VoiceSetParameters %d", ret);

    parms = str_parms_create_str(kvpairs);
    if (!parms) {
        AHAL_ERR("Error in str_parms_create_str");
        ret = 0;
        return ret;
    }
    AudioExtn::audio_extn_set_parameters(adev_, parms);

    if (property_get_bool("vendor.audio.hdr.record.enable", false)) {
        changes_done = hdr_set_parameters(adev_, parms);
        if (changes_done) {
            for (int i = 0; i < stream_in_list_.size(); i++) {
                stream_in_list_[i]->GetStreamHandle(&stream_in);
                astream_in = adev_->InGetStream((audio_stream_t*)stream_in);
                if ( (astream_in->source_ == AUDIO_SOURCE_UNPROCESSED) &&
                   (astream_in->config_.sample_rate == 48000) ) {
                    AHAL_DBG("Forcing PAL device switch for HDR");
                    channels =
                        audio_channel_count_from_in_mask(astream_in->config_.channel_mask);
                    if (channels == 4) {
                        if (adev_->hdr_record_enabled) {
                            new_devices = astream_in->mAndroidInDevices;
                            astream_in->RouteStream(new_devices, true);
                        }
                    }
                    break;
                }
            }
        }
    }

    ret = str_parms_get_str(parms, "screen_state", value, sizeof(value));
    if (ret >= 0) {
        pal_param_screen_state_t param_screen_st;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0) {
            param_screen_st.screen_state = true;
            AHAL_DBG(" - screen = on");
            ret = pal_set_param( PAL_PARAM_ID_SCREEN_STATE, (void*)&param_screen_st, sizeof(pal_param_screen_state_t));
        } else {
            AHAL_DBG(" - screen = off");
            param_screen_st.screen_state = false;
            ret = pal_set_param( PAL_PARAM_ID_SCREEN_STATE, (void*)&param_screen_st, sizeof(pal_param_screen_state_t));
        }
    }

    ret = str_parms_get_str(parms, "UHQA", value, sizeof(value));
    if (ret >= 0) {
        pal_param_uhqa_t param_uhqa_flag;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0) {
            param_uhqa_flag.uhqa_state = true;
            AHAL_DBG(" - UHQA = on");
            ret = pal_set_param(PAL_PARAM_ID_UHQA_FLAG, (void*)&param_uhqa_flag,
                          sizeof(pal_param_uhqa_t));
        } else {
            param_uhqa_flag.uhqa_state = false;
            AHAL_DBG(" - UHQA = false");
            ret = pal_set_param(PAL_PARAM_ID_UHQA_FLAG, (void*)&param_uhqa_flag,
                          sizeof(pal_param_uhqa_t));
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT,
                            value, sizeof(value));
    if (ret >= 0) {
        pal_param_device_connection_t param_device_connection;
        val = atoi(value);
        audio_devices_t device = (audio_devices_t)val;

        if (audio_is_usb_out_device(device) || audio_is_usb_in_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0) {
                param_device_connection.device_config.usb_addr.card_id = atoi(value);
                if ((usb_card_id_ == param_device_connection.device_config.usb_addr.card_id) &&
                    (audio_is_usb_in_device(device)) && (usb_input_dev_enabled == true)) {
                    AHAL_INFO("plugin card :%d device num=%d already added", usb_card_id_,
                          param_device_connection.device_config.usb_addr.device_num);
                    ret = 0;
                    goto exit;
                }

                usb_card_id_ = param_device_connection.device_config.usb_addr.card_id;
                AHAL_INFO("plugin card=%d",
                    param_device_connection.device_config.usb_addr.card_id);
            }
            ret = str_parms_get_str(parms, "device", value, sizeof(value));
            if (ret >= 0) {
                param_device_connection.device_config.usb_addr.device_num = atoi(value);
                usb_dev_num_ = param_device_connection.device_config.usb_addr.device_num;
                AHAL_INFO("plugin device num=%d",
                    param_device_connection.device_config.usb_addr.device_num);
            }
        } else if (val == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            int controller = -1, stream = -1;
            AudioExtn::get_controller_stream_from_params(parms, &controller, &stream);
            param_device_connection.device_config.dp_config.controller = controller;
            dp_controller = controller;
            param_device_connection.device_config.dp_config.stream = stream;
            dp_stream = stream;
            AHAL_INFO("plugin device cont %d stream %d", controller, stream);
        }

        if (device) {
            pal_device_ids = (pal_device_id_t *) calloc(1, sizeof(pal_device_id_t));
            pal_device_count = GetPalDeviceIds({device}, pal_device_ids);
            ret = add_input_headset_if_usb_out_headset(&pal_device_count, &pal_device_ids, true);
            if (ret) {
                if (pal_device_ids)
                    free(pal_device_ids);
                AHAL_ERR("adding input headset failed, error:%d", ret);
                goto exit;
            }
            for (int i = 0; i < pal_device_count; i++) {
                param_device_connection.connection_state = true;
                param_device_connection.id = pal_device_ids[i];
                ret = pal_set_param(PAL_PARAM_ID_DEVICE_CONNECTION,
                        (void*)&param_device_connection,
                        sizeof(pal_param_device_connection_t));
                if (ret!=0) {
                    AHAL_ERR("pal set param failed for device connection, pal_device_ids:%d",
                             pal_device_ids[i]);
                }
            }
            AHAL_INFO("pal set param success  for device connection");
            /* check if capture profile is supported or not */
           if (audio_is_usb_out_device(device) || audio_is_usb_in_device(device)) {
                pal_param_device_capability_t *device_cap_query = (pal_param_device_capability_t *)
                                                          malloc(sizeof(pal_param_device_capability_t));
                if (device_cap_query) {
                    dynamic_media_config_t dynamic_media_config;
                    size_t payload_size = 0;
                    device_cap_query->id = PAL_DEVICE_IN_USB_HEADSET;
                    device_cap_query->addr.card_id = usb_card_id_;
                    device_cap_query->addr.device_num = usb_dev_num_;
                    device_cap_query->config = &dynamic_media_config;
                    device_cap_query->is_playback = false;
                    ret = pal_get_param(PAL_PARAM_ID_DEVICE_CAPABILITY,
                            (void **)&device_cap_query,
                            &payload_size, nullptr);
                    if ((dynamic_media_config.sample_rate[0] == 0 && dynamic_media_config.format[0] == 0 &&
                            dynamic_media_config.mask[0] == 0) || (dynamic_media_config.jack_status == false))
                        usb_input_dev_enabled = false;
                    else
                        usb_input_dev_enabled = true;
                    free(device_cap_query);
                } else {
                    AHAL_ERR("Failed to allocate mem for device_cap_query");
                }
            }

            if (pal_device_ids) {
                free(pal_device_ids);
                pal_device_ids = NULL;
            }
        }
    }

    /* Checking for Device rotation */
    ret = str_parms_get_int(parms, "rotation", &val);
    if (ret >= 0) {
        int isRotationReq = 0;
        pal_param_device_rotation_t param_device_rotation;
        switch (val) {
        case 270:
        {
            if (PAL_SPEAKER_ROTATION_LR == current_rotation) {
                /* Device rotated from normal position to inverted landscape. */
                current_rotation = PAL_SPEAKER_ROTATION_RL;
                isRotationReq = 1;
                param_device_rotation.rotation_type = PAL_SPEAKER_ROTATION_RL;
            }
        }
        break;
        case 0:
        case 180:
        case 90:
        {
            if (PAL_SPEAKER_ROTATION_RL == current_rotation) {
                /* Phone was in inverted landspace and now is changed to portrait
                 * or inverted portrait. Notify PAL to swap the speaker.
                 */
                current_rotation = PAL_SPEAKER_ROTATION_LR;
                isRotationReq = 1;
                param_device_rotation.rotation_type = PAL_SPEAKER_ROTATION_LR;
            }
        }
        break;
        default:
            AHAL_ERR("error unexpected rotation of %d", val);
            isRotationReq = -EINVAL;
        }
        if (1 == isRotationReq) {
            /* Swap the speakers */
            AHAL_DBG("Swapping the speakers ");
            ret = pal_set_param(PAL_PARAM_ID_DEVICE_ROTATION,
                    (void*)&param_device_rotation,
                    sizeof(pal_param_device_rotation_t));
            AHAL_DBG("Speakers swapped ");
        }
    }

    /* Speaker Protection: Factory Test Mode */
    ret = str_parms_get_str(parms, "fbsp_cfg_wait_time", value, sizeof(value));
    if (ret >= 0) {
        str_parms_del(parms, "fbsp_cfg_wait_time");
        cfg_str = strtok_r(value, ";", &test_r);
        if (cfg_str != NULL) {
            pal_spkr_prot_payload spPayload;
            spPayload.operationMode = PAL_SP_MODE_FACTORY_TEST;
            spPayload.spkrHeatupTime = atoi(cfg_str);

            ret = str_parms_get_str(parms, "fbsp_cfg_ftm_time", value, sizeof(value));
            if (ret >= 0) {
                str_parms_del(parms, "fbsp_cfg_ftm_time");
                cfg_str = strtok_r(value, ";", &test_r);
                if (cfg_str != NULL) {
                    spPayload.operationModeRunTime = atoi(cfg_str);
                    ret = pal_set_param(PAL_PARAM_ID_SP_MODE, (void*)&spPayload,
                                sizeof(pal_spkr_prot_payload));
                } else {
                    AHAL_ERR("Unable to parse the FTM time");
                }
            } else {
                AHAL_ERR("Parameter missing for the FTM time");
            }
        } else {
            AHAL_ERR("Unable to parse the FTM wait time");
        }
    }

    /* Speaker Protection: V-validation mode */
    ret = str_parms_get_str(parms, "fbsp_v_vali_wait_time", value, sizeof(value));
    if (ret >= 0) {
        str_parms_del(parms, "fbsp_v_vali_wait_time");
        cfg_str = strtok_r(value, ";", &test_r);
        if (cfg_str != NULL) {
            pal_spkr_prot_payload spPayload;
            spPayload.operationMode = PAL_SP_MODE_V_VALIDATION;
            spPayload.spkrHeatupTime = atoi(cfg_str);

            ret = str_parms_get_str(parms, "fbsp_v_vali_vali_time", value, sizeof(value));
            if (ret >= 0) {
                str_parms_del(parms, "fbsp_v_vali_vali_time");
                cfg_str = strtok_r(value, ";", &test_r);
                if (cfg_str != NULL) {
                    spPayload.operationModeRunTime = atoi(cfg_str);
                    ret = pal_set_param(PAL_PARAM_ID_SP_MODE, (void*)&spPayload,
                                sizeof(pal_spkr_prot_payload));
                } else {
                    AHAL_ERR("Unable to parse the V_Validation time");
                }
            } else {
                AHAL_ERR("Parameter missing for the V-Validation time");
            }
        } else {
            AHAL_ERR("Unable to parse the V-Validation wait time");
        }
    }

    /* Speaker Protection: Dynamic calibration mode */
    ret = str_parms_get_str(parms, "trigger_spkr_cal", value, sizeof(value));
    if (ret >= 0) {
        if ((strcmp(value, "true") == 0) || (strcmp(value, "yes") == 0)) {
            pal_spkr_prot_payload spPayload;
            spPayload.operationMode = PAL_SP_MODE_DYNAMIC_CAL;
            ret = pal_set_param(PAL_PARAM_ID_SP_MODE, (void*)&spPayload,
                        sizeof(pal_spkr_prot_payload));
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT,
                            value, sizeof(value));
    if (ret >= 0) {
        pal_param_device_connection_t param_device_connection;
        val = atoi(value);
        audio_devices_t device = (audio_devices_t)val;
        if (audio_is_usb_out_device(device) || audio_is_usb_in_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0)
                param_device_connection.device_config.usb_addr.card_id = atoi(value);
            ret = str_parms_get_str(parms, "device", value, sizeof(value));
            if (ret >= 0)
                param_device_connection.device_config.usb_addr.device_num = atoi(value);
            if ((usb_card_id_ == param_device_connection.device_config.usb_addr.card_id) &&
                (audio_is_usb_in_device(device)) && (usb_input_dev_enabled == true)) {
                   usb_input_dev_enabled = false;
                   usb_out_headset = false;
                   AHAL_DBG("usb_input_dev_enabled flag is cleared.");
            }
        } else if (val == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            int controller = -1, stream = -1;
            AudioExtn::get_controller_stream_from_params(parms, &controller, &stream);
            param_device_connection.device_config.dp_config.controller = controller;
            param_device_connection.device_config.dp_config.stream = stream;
            dp_stream = stream;
            AHAL_INFO("plugin device cont %d stream %d", controller, stream);
        }

        if (device) {
            pal_device_ids = (pal_device_id_t *) calloc(1, sizeof(pal_device_id_t));
            pal_device_count = GetPalDeviceIds({device}, pal_device_ids);
            ret = add_input_headset_if_usb_out_headset(&pal_device_count, &pal_device_ids, false);
            if (ret) {
                if (pal_device_ids)
                    free(pal_device_ids);
                AHAL_ERR("adding input headset failed, error:%d", ret);
                goto exit;
            }
            for (int i = 0; i < pal_device_count; i++) {
                param_device_connection.connection_state = false;
                param_device_connection.id = pal_device_ids[i];
                ret = pal_set_param(PAL_PARAM_ID_DEVICE_CONNECTION,
                        (void*)&param_device_connection,
                        sizeof(pal_param_device_connection_t));
                if (ret!=0) {
                    AHAL_ERR("pal set param failed for device disconnect");
                }
                AHAL_INFO("pal set param sucess for device disconnect");
            }
        }
    }

    if (pal_device_ids) {
        free(pal_device_ids);
        pal_device_ids = NULL;
    }

    /* A2DP parameters */
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_RECONFIG_A2DP, value, sizeof(value));
    if (ret >= 0) {
        pal_param_bta2dp_t param_bt_a2dp;
        param_bt_a2dp.reconfig = true;

        AHAL_INFO("BT A2DP Reconfig command received");
        ret = pal_set_param(PAL_PARAM_ID_BT_A2DP_RECONFIG, (void *)&param_bt_a2dp,
                            sizeof(pal_param_bta2dp_t));
    }

    ret = str_parms_get_str(parms, "A2dpSuspended" , value, sizeof(value));
    if (ret >= 0) {
        pal_param_bta2dp_t param_bt_a2dp;

        if (strncmp(value, "true", 4) == 0)
            param_bt_a2dp.a2dp_suspended = true;
        else
            param_bt_a2dp.a2dp_suspended = false;

        AHAL_INFO("BT A2DP Suspended = %s, command received", value);
        ret = pal_set_param(PAL_PARAM_ID_BT_A2DP_SUSPENDED, (void *)&param_bt_a2dp,
                            sizeof(pal_param_bta2dp_t));
    }

    ret = str_parms_get_str(parms, "TwsChannelConfig", value, sizeof(value));
    if (ret >= 0) {
        pal_param_bta2dp_t param_bt_a2dp;

        AHAL_INFO("Setting tws channel mode to %s", value);
        if (!(strncmp(value, "mono", strlen(value))))
            param_bt_a2dp.is_tws_mono_mode_on = true;
        else if (!(strncmp(value,"dual-mono",strlen(value))))
            param_bt_a2dp.is_tws_mono_mode_on = false;
        ret = pal_set_param(PAL_PARAM_ID_BT_A2DP_TWS_CONFIG, (void *)&param_bt_a2dp,
                            sizeof(pal_param_bta2dp_t));
    }

    ret = str_parms_get_str(parms, "LEAMono", value, sizeof(value));
    if (ret >= 0) {
        pal_param_bta2dp_t param_bt_a2dp;

        AHAL_INFO("Setting LC3 channel mode to %s", value);
        if (!(strncmp(value, "true", strlen(value))))
            param_bt_a2dp.is_lc3_mono_mode_on = true;
        else
            param_bt_a2dp.is_lc3_mono_mode_on = false;
        ret = pal_set_param(PAL_PARAM_ID_BT_A2DP_LC3_CONFIG, (void *)&param_bt_a2dp,
                            sizeof(pal_param_bta2dp_t));
    }

    /* SCO parameters */
    ret = str_parms_get_str(parms, "BT_SCO", value, sizeof(value));
    if (ret >= 0) {
        pal_param_btsco_t param_bt_sco;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0) {
            param_bt_sco.bt_sco_on = true;
        } else {
            param_bt_sco.bt_sco_on = false;
        }

        AHAL_INFO("BTSCO on = %d", param_bt_sco.bt_sco_on);
        ret = pal_set_param(PAL_PARAM_ID_BT_SCO, (void *)&param_bt_sco,
                            sizeof(pal_param_btsco_t));
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_SCO_WB, value, sizeof(value));
    if (ret >= 0) {
        pal_param_btsco_t param_bt_sco;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            param_bt_sco.bt_wb_speech_enabled = true;
        else
            param_bt_sco.bt_wb_speech_enabled = false;

        AHAL_INFO("BTSCO WB mode = %d", param_bt_sco.bt_wb_speech_enabled);
        ret = pal_set_param(PAL_PARAM_ID_BT_SCO_WB, (void *)&param_bt_sco,
                            sizeof(pal_param_btsco_t));
    }

    ret = str_parms_get_str(parms, "bt_swb", value, sizeof(value));
    if (ret >= 0) {
        pal_param_btsco_t param_bt_sco;

        val = atoi(value);
        param_bt_sco.bt_swb_speech_mode = val;
        AHAL_INFO("BTSCO SWB mode = 0x%x", val);
        ret = pal_set_param(PAL_PARAM_ID_BT_SCO_SWB, (void *)&param_bt_sco,
                            sizeof(pal_param_btsco_t));
    }

    ret = str_parms_get_str(parms, "bt_ble", value, sizeof(value));
    if (ret >= 0) {
        pal_param_btsco_t param_bt_sco;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0) {
            bt_lc3_speech_enabled = true;

            // turn off wideband, super-wideband
            param_bt_sco.bt_wb_speech_enabled = false;
            ret = pal_set_param(PAL_PARAM_ID_BT_SCO_WB, (void *)&param_bt_sco,
                                sizeof(pal_param_btsco_t));

            param_bt_sco.bt_swb_speech_mode = 0xFFFF;
            ret = pal_set_param(PAL_PARAM_ID_BT_SCO_SWB, (void *)&param_bt_sco,
                                sizeof(pal_param_btsco_t));
        } else {
            bt_lc3_speech_enabled = false;
            param_bt_sco.bt_lc3_speech_enabled = false;
            ret = pal_set_param(PAL_PARAM_ID_BT_SCO_LC3, (void *)&param_bt_sco,
                                sizeof(pal_param_btsco_t));

            // clear btsco_lc3_cfg to avoid stale and partial cfg being used in next round
            memset(&btsco_lc3_cfg, 0, sizeof(btsco_lc3_cfg_t));
        }
        AHAL_INFO("BTSCO LC3 mode = %d", bt_lc3_speech_enabled);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_NREC, value, sizeof(value));
    if (ret >= 0) {
        pal_param_btsco_t param_bt_sco;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0) {
            AHAL_INFO("BTSCO NREC mode = ON");
            param_bt_sco.bt_sco_nrec = true;
        } else {
            AHAL_INFO("BTSCO NREC mode = OFF");
            param_bt_sco.bt_sco_nrec = false;
        }
        ret = pal_set_param(PAL_PARAM_ID_BT_SCO_NREC, (void *)&param_bt_sco,
                            sizeof(pal_param_btsco_t));
    }

    for (auto& key : lc3_reserved_params) {
        ret = str_parms_get_str(parms, key, value, sizeof(value));
        if (ret < 0)
            continue;

        if (!strcmp(key, "Codec") && (!strcmp(value, "LC3"))) {
            btsco_lc3_cfg.fields_map |= LC3_CODEC_BIT;
        } else if (!strcmp(key, "StreamMap")) {
            strlcpy(btsco_lc3_cfg.streamMap, value, PAL_LC3_MAX_STRING_LEN);
            btsco_lc3_cfg.fields_map |= LC3_STREAM_MAP_BIT;
        } else if (!strcmp(key, "FrameDuration")) {
            btsco_lc3_cfg.frame_duration = atoi(value);
            btsco_lc3_cfg.fields_map |= LC3_FRAME_DURATION_BIT;
        } else if (!strcmp(key, "Blocks_forSDU")) {
            btsco_lc3_cfg.num_blocks = atoi(value);
            btsco_lc3_cfg.fields_map |= LC3_BLOCKS_FORSDU_BIT;
        } else if (!strcmp(key, "rxconfig_index")) {
            btsco_lc3_cfg.rxconfig_index = atoi(value);
            btsco_lc3_cfg.fields_map |= LC3_RXCFG_IDX_BIT;
        } else if (!strcmp(key, "txconfig_index")) {
            btsco_lc3_cfg.txconfig_index = atoi(value);
            btsco_lc3_cfg.fields_map |= LC3_TXCFG_IDX_BIT;
        } else if (!strcmp(key, "version")) {
            btsco_lc3_cfg.api_version = atoi(value);
            btsco_lc3_cfg.fields_map |= LC3_VERSION_BIT;
        } else if (!strcmp(key, "vendor")) {
            strlcpy(btsco_lc3_cfg.vendor, value, PAL_LC3_MAX_STRING_LEN);
            btsco_lc3_cfg.fields_map |= LC3_VENDOR_BIT;
        }
    }

    if (((btsco_lc3_cfg.fields_map & LC3_BIT_MASK) == LC3_BIT_VALID) &&
           (bt_lc3_speech_enabled == true)) {
        pal_param_btsco_t param_bt_sco;
        param_bt_sco.bt_lc3_speech_enabled  = bt_lc3_speech_enabled;
        param_bt_sco.lc3_cfg.frame_duration = btsco_lc3_cfg.frame_duration;
        param_bt_sco.lc3_cfg.num_blocks     = btsco_lc3_cfg.num_blocks;
        param_bt_sco.lc3_cfg.rxconfig_index = btsco_lc3_cfg.rxconfig_index;
        param_bt_sco.lc3_cfg.txconfig_index = btsco_lc3_cfg.txconfig_index;
        param_bt_sco.lc3_cfg.api_version    = btsco_lc3_cfg.api_version;
        strlcpy(param_bt_sco.lc3_cfg.streamMap, btsco_lc3_cfg.streamMap, PAL_LC3_MAX_STRING_LEN);
        strlcpy(param_bt_sco.lc3_cfg.vendor, btsco_lc3_cfg.vendor, PAL_LC3_MAX_STRING_LEN);

        AHAL_INFO("BTSCO LC3 mode = on, sending..");
        ret = pal_set_param(PAL_PARAM_ID_BT_SCO_LC3, (void *)&param_bt_sco,
                            sizeof(pal_param_btsco_t));

        memset(&btsco_lc3_cfg, 0, sizeof(btsco_lc3_cfg_t));
    }

    ret = str_parms_get_str(parms, "wfd_channel_cap", value, sizeof(value));
    if (ret >= 0) {
        pal_param_proxy_channel_config_t param_out_proxy;

        val = atoi(value);
        param_out_proxy.num_proxy_channels = val;
        AHAL_INFO("Proxy channels: %d", val);
        ret = pal_set_param(PAL_PARAM_ID_PROXY_CHANNEL_CONFIG, (void *)&param_out_proxy,
                sizeof(pal_param_proxy_channel_config_t));
    }

    ret = str_parms_get_str(parms, "haptics_volume", value, sizeof(value));
    if (ret >= 0) {
        struct pal_volume_data* volume = NULL;
        volume = (struct pal_volume_data *)malloc(sizeof(struct pal_volume_data)
                      +sizeof(struct pal_channel_vol_kv));
        if (volume) {
            volume->no_of_volpair = 1;
            //For haptics, there is only one channel (FL).
            volume->volume_pair[0].channel_mask = 0x01;
            volume->volume_pair[0].vol = atof(value);
            AHAL_INFO("Setting Haptics Volume as %f", volume->volume_pair[0].vol);
            ret = pal_set_param(PAL_PARAM_ID_HAPTICS_VOLUME, (void *)volume,
                     sizeof(pal_volume_data));
            free(volume);
        }
    }

    ret = str_parms_get_str(parms, "haptics_intensity", value, sizeof(value));
    if (ret >=0) {
        pal_param_haptics_intensity_t hIntensity;
        val = atoi(value);
        hIntensity.intensity = val;
        AHAL_INFO("Setting Haptics Volume as %d", hIntensity.intensity);
        ret = pal_set_param(PAL_PARAM_ID_HAPTICS_INTENSITY, (void *)&hIntensity,
                 sizeof(pal_param_haptics_intensity_t));
    }

    ret = str_parms_get_str(parms, "A2dpCaptureSuspend", value, sizeof(value));
    if (ret >= 0) {
        pal_param_bta2dp_t param_bt_a2dp;

        if (strncmp(value, "true", 4) == 0)
            param_bt_a2dp.a2dp_capture_suspended = true;
        else
            param_bt_a2dp.a2dp_capture_suspended = false;

        AHAL_INFO("BT A2DP Capture Suspended = %s, command received", value);
        ret = pal_set_param(PAL_PARAM_ID_BT_A2DP_CAPTURE_SUSPENDED, (void*)&param_bt_a2dp,
            sizeof(pal_param_bta2dp_t));
    }


exit:
    if (parms)
        str_parms_destroy(parms);

    AHAL_DBG("exit: %s", kvpairs);
    return 0;
}


int AudioDevice::SetVoiceVolume(float volume) {
    return voice_->SetVoiceVolume(volume);
}

char* AudioDevice::GetParameters(const char *keys) {
    int32_t ret;
    char *str;
    char value[256]={0};
    size_t size = 0;
    struct str_parms *reply = str_parms_create();
    struct str_parms *query = str_parms_create_str(keys);

    if (!query || !reply) {
        if (reply) {
            str_parms_destroy(reply);
        }
        if (query) {
            str_parms_destroy(query);
        }
        AHAL_ERR("failed to create query or reply");
        return NULL;
    }

    AHAL_VERBOSE("enter");

    ret = str_parms_get_str(query, AUDIO_PARAMETER_A2DP_RECONFIG_SUPPORTED,
                            value, sizeof(value));
    if (ret >= 0) {
        pal_param_bta2dp_t *param_bt_a2dp;
        int32_t val = 0;

        ret = pal_get_param(PAL_PARAM_ID_BT_A2DP_RECONFIG_SUPPORTED,
                            (void **)&param_bt_a2dp, &size, nullptr);
        if (!ret) {
            if (size < sizeof(pal_param_bta2dp_t)) {
                AHAL_ERR("size returned is smaller for BT_A2DP_RECONFIG_SUPPORTED");
                goto exit;
            }
            val = param_bt_a2dp->reconfig_supported;
            str_parms_add_int(reply, AUDIO_PARAMETER_A2DP_RECONFIG_SUPPORTED, val);
            AHAL_VERBOSE("isReconfigA2dpSupported = %d", val);
        }
    }

    ret = str_parms_get_str(query, "get_ftm_param", value, sizeof(value));
    if (ret >= 0) {
        char ftm_value[255];
        ret = pal_get_param(PAL_PARAM_ID_SP_MODE, (void **)&ftm_value, &size, nullptr);
        if (!ret) {
            if (size > 0) {
                str_parms_add_str(reply, "get_ftm_param", ftm_value);
            }
            else
                AHAL_ERR("Error happened for getting FTM param");
        }

    }

    ret = str_parms_get_str(query, "get_spkr_cal", value, sizeof(value));
    if (ret >= 0) {
        char cal_value[255];
        ret = pal_get_param(PAL_PARAM_ID_SP_GET_CAL, (void **)&cal_value, &size, nullptr);
        if (!ret) {
            if (size > 0) {
                str_parms_add_str(reply, "get_spkr_cal", cal_value);
            }
            else
                AHAL_ERR("Error happened for getting Cal param");
        }
    }

    AudioExtn::audio_extn_get_parameters(adev_, query, reply);
    if (voice_)
        voice_->VoiceGetParameters(query, reply);

    if (property_get_bool("vendor.audio.hdr.record.enable", false))
        hdr_get_parameters(adev_, query, reply);

exit:
    str = str_parms_to_str(reply);
    str_parms_destroy(query);
    str_parms_destroy(reply);

    if (str)
        AHAL_VERBOSE("exit: returns - %s", str);

    return str;
}

void AudioDevice::FillAndroidDeviceMap() {
    android_device_map_.clear();
    /* go through all devices and pushback */

    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_NONE, PAL_DEVICE_NONE));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_EARPIECE, PAL_DEVICE_OUT_HANDSET));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_SPEAKER, PAL_DEVICE_OUT_SPEAKER));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_WIRED_HEADSET, PAL_DEVICE_OUT_WIRED_HEADSET));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_WIRED_HEADPHONE, PAL_DEVICE_OUT_WIRED_HEADPHONE));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_BLUETOOTH_SCO, PAL_DEVICE_OUT_BLUETOOTH_SCO));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET, PAL_DEVICE_OUT_BLUETOOTH_SCO));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT, PAL_DEVICE_OUT_BLUETOOTH_SCO));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP, PAL_DEVICE_OUT_BLUETOOTH_A2DP));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES, PAL_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER, PAL_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_AUX_DIGITAL, PAL_DEVICE_OUT_AUX_DIGITAL));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_HDMI, PAL_DEVICE_OUT_HDMI));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET, PAL_DEVICE_OUT_ANLG_DOCK_HEADSET));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET, PAL_DEVICE_OUT_DGTL_DOCK_HEADSET));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_USB_ACCESSORY, PAL_DEVICE_OUT_USB_ACCESSORY));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_USB_DEVICE, PAL_DEVICE_OUT_USB_DEVICE));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_REMOTE_SUBMIX, PAL_DEVICE_OUT_REMOTE_SUBMIX));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_TELEPHONY_TX, PAL_DEVICE_NONE));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_LINE, PAL_DEVICE_OUT_WIRED_HEADPHONE));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_HDMI_ARC, PAL_DEVICE_OUT_HDMI_ARC));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_SPDIF, PAL_DEVICE_OUT_SPDIF));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_FM, PAL_DEVICE_OUT_FM));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_AUX_LINE, PAL_DEVICE_OUT_AUX_LINE));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_SPEAKER_SAFE, PAL_DEVICE_OUT_SPEAKER));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_IP, PAL_DEVICE_OUT_IP));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_BUS, PAL_DEVICE_OUT_BUS));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_PROXY, PAL_DEVICE_OUT_PROXY));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_USB_HEADSET, PAL_DEVICE_OUT_USB_HEADSET));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_DEFAULT, PAL_DEVICE_OUT_SPEAKER));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_HEARING_AID, PAL_DEVICE_OUT_HEARING_AID));

    /* go through all in devices and pushback */

    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_BUILTIN_MIC, PAL_DEVICE_IN_HANDSET_MIC));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_BACK_MIC, PAL_DEVICE_IN_SPEAKER_MIC));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_COMMUNICATION, PAL_DEVICE_IN_COMMUNICATION));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_AMBIENT, PAL_DEVICE_IN_AMBIENT);
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET, PAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_WIRED_HEADSET, PAL_DEVICE_IN_WIRED_HEADSET));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_AUX_DIGITAL, PAL_DEVICE_IN_AUX_DIGITAL));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_HDMI, PAL_DEVICE_IN_HDMI));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_VOICE_CALL, PAL_DEVICE_IN_HANDSET_MIC));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_TELEPHONY_RX, PAL_DEVICE_IN_TELEPHONY_RX));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_REMOTE_SUBMIX, PAL_DEVICE_IN_REMOTE_SUBMIX);
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET, PAL_DEVICE_IN_ANLG_DOCK_HEADSET);
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_DGTL_DOCK_HEADSET, PAL_DEVICE_IN_DGTL_DOCK_HEADSET);
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_USB_ACCESSORY, PAL_DEVICE_IN_USB_ACCESSORY));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_USB_DEVICE, PAL_DEVICE_IN_USB_HEADSET));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_FM_TUNER, PAL_DEVICE_IN_FM_TUNER));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_TV_TUNER, PAL_DEVICE_IN_TV_TUNER);
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_LINE, PAL_DEVICE_IN_LINE));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_SPDIF, PAL_DEVICE_IN_SPDIF));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_BLUETOOTH_A2DP, PAL_DEVICE_IN_BLUETOOTH_A2DP));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_LOOPBACK, PAL_DEVICE_IN_LOOPBACK);
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_IP, PAL_DEVICE_IN_IP);
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_BUS, PAL_DEVICE_IN_BUS);
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_PROXY, PAL_DEVICE_IN_PROXY));
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_USB_HEADSET, PAL_DEVICE_IN_USB_HEADSET));
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_HDMI_ARC, PAL_DEVICE_IN_HDMI_ARC);
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_BLUETOOTH_BLE, PAL_DEVICE_IN_BLUETOOTH_BLE);
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_DEFAULT, PAL_DEVICE_IN_DEFAULT));
#ifdef EC_REF_CAPTURE_ENABLED
    android_device_map_.insert(std::make_pair(AUDIO_DEVICE_IN_ECHO_REFERENCE, PAL_DEVICE_IN_ECHO_REF));
#endif
}

int AudioDevice::GetPalDeviceIds(const std::set<audio_devices_t>& hal_device_ids,
                                 pal_device_id_t* pal_device_id) {
    int device_count = 0;
    if (!pal_device_id) {
        AHAL_ERR("invalid pal device id");
        goto error;
    }

    // pal device ids is supposed to have to space for the new ids
    AHAL_DBG("haldeviceIds: %zu", hal_device_ids.size());

    for(auto hal_device_id : hal_device_ids) {
        auto it = android_device_map_.find(hal_device_id);
        if (it != android_device_map_.end() &&
                audio_is_input_device(it->first) == audio_is_input_device(hal_device_id)) {
            AHAL_DBG("Found haldeviceId: %x and PAL Device ID %d",
                    it->first, it->second);
            if (it->second == PAL_DEVICE_OUT_AUX_DIGITAL ||
                    it->second == PAL_DEVICE_OUT_HDMI) {
               AHAL_DBG("dp_controller: %d dp_stream: %d",
                       dp_controller, dp_stream);
               if (dp_controller * MAX_STREAMS_PER_CONTROLLER + dp_stream) {
                  pal_device_id[device_count] = PAL_DEVICE_OUT_AUX_DIGITAL_1;
               } else {
                  pal_device_id[device_count] = it->second;
               }
            } else {
               pal_device_id[device_count] = it->second;
            }
        }
        ++device_count;
    }

error:
    AHAL_DBG("devices allocated %zu, pal device ids before returning %d",
             hal_device_ids.size(), device_count);
    return device_count;
}

void AudioDevice::SetChargingMode(bool is_charging) {
    int32_t result = 0;
    pal_param_charging_state_t charge_state;

    AHAL_DBG("enter, is_charging %d", is_charging);
    is_charging_ = is_charging;
    charge_state.charging_state = is_charging;

    result = pal_set_param(PAL_PARAM_ID_CHARGING_STATE, (void*)&charge_state,
                        sizeof(pal_param_charging_state_t));
    if (result)
        AHAL_DBG("error while handling charging event result(%d)\n",
                 result);

    AHAL_DBG("exit");
}

hw_device_t* AudioDevice::GetAudioDeviceCommon()
{
    return &(adev_->device_.get()->common);
}

static int adev_open(const hw_module_t *module, const char *name __unused,
                     hw_device_t **device) {
    int32_t ret = 0;
    AHAL_DBG("Enter");

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    if (!adevice) {
        AHAL_ERR("error, GetInstance failed");
    }

    adevice->adev_init_mutex.lock();
    if (adevice->adev_init_ref_count != 0) {
        *device = adevice->GetAudioDeviceCommon();
        adevice->adev_init_ref_count++;
        adevice->adev_init_mutex.unlock();
        AHAL_DBG("returning existing instance of adev, exiting");
        goto exit;
    }

    ret = adevice->Init(device, module);

    if (ret || (*device == NULL)) {
        AHAL_ERR("error, audio device init failed, ret(%d),*device(%p)",
                 ret, *device);
    }
    adevice->adev_init_mutex.unlock();
exit:
    AHAL_DBG("Exit, status %d", ret);
    return ret;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "QTI Audio HAL",
        .author = "The Linux Foundation",
        .methods = &hal_module_methods,
    },
};

audio_patch_handle_t AudioPatch::generate_patch_handle_l() {
    static audio_patch_handle_t handles = AUDIO_PATCH_HANDLE_NONE;
    if (++handles < 0)
        handles = AUDIO_PATCH_HANDLE_NONE + 1;
    return handles;
}

AudioPatch::AudioPatch(PatchType patch_type,
                       const std::vector<struct audio_port_config>& sources,
                       const std::vector<struct audio_port_config>& sinks):
                       type(patch_type), sources(sources), sinks(sinks) {
        static std::mutex patch_lock;
        std::lock_guard<std::mutex> lock(patch_lock);
        handle = AudioPatch::generate_patch_handle_l();
}

bool AudioDevice::find_enum_by_string(const struct audio_string_to_enum * table, const char * name,
        int32_t len, unsigned int *value)
{
    if (table == NULL) {
        ALOGE("%s: table is NULL", __func__);
        return false;
    }

    if (name == NULL) {
        ALOGE("null key");
        return false;
    }

    for (int i = 0; i < len; i++) {
        if (!strcmp(table[i].name, name)) {
            *value = table[i].value;
            return true;
        }
    }
    return false;
}

bool AudioDevice::set_microphone_characteristic(struct audio_microphone_characteristic_t *mic)
{
    if (microphones.declared_mic_count >= AUDIO_MICROPHONE_MAX_COUNT) {
        AHAL_ERR("mic number is more than maximum number");
        return false;
    }
    for (size_t ch = 0; ch < AUDIO_CHANNEL_COUNT_MAX; ch++) {
        mic->channel_mapping[ch] = AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED;
    }
    microphones.microphone[microphones.declared_mic_count++] = *mic;
    return true;
}

int32_t AudioDevice::get_microphones(struct audio_microphone_characteristic_t *mic_array, size_t *mic_count)
{
    if (!mic_characteristics_available)
        return -EIO;

    if (mic_count == NULL) {
        AHAL_ERR("Invalid mic_count!!!");
        return -EINVAL;
    }
    if (mic_array == NULL) {
        AHAL_ERR("Invalid mic_array!!!");
        return -EINVAL;
    }

    if (*mic_count == 0) {
        AHAL_INFO("mic_count is ZERO");
        return 0;
    }

    size_t max_mic_count = *mic_count;
    size_t actual_mic_count = 0;
    for (size_t i = 0; i < max_mic_count && i < microphones.declared_mic_count; i++) {
        mic_array[i] = microphones.microphone[i];
        actual_mic_count++;
    }
    *mic_count = actual_mic_count;
    return 0;
}

void AudioDevice::process_microphone_characteristics(const XML_Char **attr)
{
    struct audio_microphone_characteristic_t microphone = {};
    uint32_t curIdx = 0;
    uint32_t valid_mask;

    if (strcmp(attr[curIdx++], "valid_mask")) {
        AHAL_ERR("valid_mask not found");
        goto done;
    }
    valid_mask = atoi(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "device_id")) {
        AHAL_ERR("device_id not found");
        goto done;
    }
    if (strlen(attr[curIdx]) > AUDIO_MICROPHONE_ID_MAX_LEN) {
        AHAL_ERR("device_id is too long : %s ", attr[curIdx]);
        goto done;
    }
    strlcpy(microphone.device_id, attr[curIdx++], sizeof(microphone.device_id));

    if (strcmp(attr[curIdx++], "type")) {
        AHAL_ERR("device type not found");
        goto done;
    }
    if (!find_enum_by_string(device_in_types, (char*)attr[curIdx++],
                ARRAY_SIZE(device_in_types), (unsigned int *)(&microphone.device))) {
        AHAL_ERR("device type: %s not found", attr[--curIdx]);
        goto done;
    }
    if (strcmp(attr[curIdx++], "address")) {
        AHAL_ERR("address not found");
        goto done;
    }
    if (strlen(attr[curIdx]) > AUDIO_DEVICE_MAX_ADDRESS_LEN) {
        AHAL_ERR("address %s is too long", attr[curIdx]);
        goto done;
    }
    strlcpy(microphone.address, attr[curIdx++], sizeof(microphone.address));
    if (strlen(microphone.address) == 0) {
        // If the address is empty, populate the address according to device type.
        if (microphone.device == AUDIO_DEVICE_IN_BUILTIN_MIC) {
            strlcpy(microphone.address, AUDIO_BOTTOM_MICROPHONE_ADDRESS, sizeof(microphone.address));
        } else if (microphone.device == AUDIO_DEVICE_IN_BACK_MIC) {
            strlcpy(microphone.address, AUDIO_BACK_MICROPHONE_ADDRESS, sizeof(microphone.address));
        }
    }

    if (strcmp(attr[curIdx++], "location")) {
        AHAL_ERR("location not found");
        goto done;
    }
    if (!find_enum_by_string(mic_locations, (char*)attr[curIdx++],
                AUDIO_MICROPHONE_LOCATION_CNT, (unsigned int *)(&microphone.location))) {
        AHAL_ERR("location: %s not found", attr[--curIdx]);
        goto done;
    }

    if (strcmp(attr[curIdx++], "group")) {
        AHAL_ERR("group not found");
        goto done;
    }
    microphone.group = atoi(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "index_in_the_group")) {
        AHAL_ERR("index_in_the_group not found");
        goto done;
    }
    microphone.index_in_the_group = atoi(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "directionality")) {
        AHAL_ERR("directionality not found");
        goto done;
    }
    if (!find_enum_by_string(mic_directionalities, (char*)attr[curIdx++],
                AUDIO_MICROPHONE_DIRECTIONALITY_CNT, (unsigned int *)(&microphone.directionality))) {
        AHAL_ERR("directionality : %s not found", attr[--curIdx]);
        goto done;
    }

    if (strcmp(attr[curIdx++], "num_frequency_responses")) {
        AHAL_ERR("num_frequency_responses not found");
        goto done;
    }
    microphone.num_frequency_responses = atoi(attr[curIdx++]);
    if (microphone.num_frequency_responses > AUDIO_MICROPHONE_MAX_FREQUENCY_RESPONSES) {
        AHAL_ERR("num_frequency_responses is too large");
        goto done;
    }
    if (microphone.num_frequency_responses > 0) {
        if (strcmp(attr[curIdx++], "frequencies")) {
            AHAL_ERR("frequencies not found");
            goto done;
        }
        char *context = NULL;
        char *token = strtok_r((char *)attr[curIdx++], " ", &context);
        uint32_t num_frequencies = 0;
        while (token) {
            microphone.frequency_responses[0][num_frequencies++] = atof(token);
            if (num_frequencies >= AUDIO_MICROPHONE_MAX_FREQUENCY_RESPONSES) {
                break;
            }
            token = strtok_r(NULL, " ", &context);
        }

        if (strcmp(attr[curIdx++], "responses")) {
            AHAL_ERR("responses not found");
            goto done;
        }
        token = strtok_r((char *)attr[curIdx++], " ", &context);
        uint32_t num_responses = 0;
        while (token) {
            microphone.frequency_responses[1][num_responses++] = atof(token);
            if (num_responses >= AUDIO_MICROPHONE_MAX_FREQUENCY_RESPONSES) {
                break;
            }
            token = strtok_r(NULL, " ", &context);
        }

        if (num_frequencies != num_responses
                || num_frequencies != microphone.num_frequency_responses) {
            AHAL_ERR("num of frequency and response not match: %u, %u, %u",
                    num_frequencies, num_responses, microphone.num_frequency_responses);
            goto done;
        }
    }

    if (valid_mask & AUDIO_MICROPHONE_CHARACTERISTIC_SENSITIVITY) {
        if (strcmp(attr[curIdx++], "sensitivity")) {
            AHAL_ERR("sensitivity not found");
            goto done;
        }
        microphone.sensitivity = atof(attr[curIdx++]);
    } else {
        microphone.sensitivity = AUDIO_MICROPHONE_SENSITIVITY_UNKNOWN;
    }

    if (valid_mask & AUDIO_MICROPHONE_CHARACTERISTIC_MAX_SPL) {
        if (strcmp(attr[curIdx++], "max_spl")) {
            AHAL_ERR("max_spl not found");
            goto done;
        }
        microphone.max_spl = atof(attr[curIdx++]);
    } else {
        microphone.max_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    }

    if (valid_mask & AUDIO_MICROPHONE_CHARACTERISTIC_MIN_SPL) {
        if (strcmp(attr[curIdx++], "min_spl")) {
            AHAL_ERR("min_spl not found");
            goto done;
        }
        microphone.min_spl = atof(attr[curIdx++]);
    } else {
        microphone.min_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    }

    if (valid_mask & AUDIO_MICROPHONE_CHARACTERISTIC_ORIENTATION) {
        if (strcmp(attr[curIdx++], "orientation")) {
            AHAL_ERR("orientation not found");
            goto done;
        }
        char *context = NULL;
        char *token = strtok_r((char *)attr[curIdx++], " ", &context);
        float orientation[3];
        uint32_t idx = 0;
        while (token) {
            orientation[idx++] = atof(token);
            if (idx >= 3) {
                break;
            }
            token = strtok_r(NULL, " ", &context);
        }
        if (idx != 3) {
            AHAL_ERR("orientation invalid");
            goto done;
        }
        microphone.orientation.x = orientation[0];
        microphone.orientation.y = orientation[1];
        microphone.orientation.z = orientation[2];
    } else {
        microphone.orientation.x = 0.0f;
        microphone.orientation.y = 0.0f;
        microphone.orientation.z = 0.0f;
    }

    if (valid_mask & AUDIO_MICROPHONE_CHARACTERISTIC_GEOMETRIC_LOCATION) {
        if (strcmp(attr[curIdx++], "geometric_location")) {
            AHAL_ERR("geometric_location not found");
            goto done;
        }
        char *context = NULL;
        char *token = strtok_r((char *)attr[curIdx++], " ", &context);
        float geometric_location[3];
        uint32_t idx = 0;
        while (token) {
            geometric_location[idx++] = atof(token);
            if (idx >= 3) {
                break;
            }
            token = strtok_r(NULL, " ", &context);
        }
        if (idx != 3) {
            AHAL_ERR("geometric_location invalid");
            goto done;
        }
        microphone.geometric_location.x = geometric_location[0];
        microphone.geometric_location.y = geometric_location[1];
        microphone.geometric_location.z = geometric_location[2];
    } else {
        microphone.geometric_location.x = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
        microphone.geometric_location.y = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
        microphone.geometric_location.z = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    }

    set_microphone_characteristic(&microphone);

done:
    return;
}

bool AudioDevice::is_input_pal_dev_id(int deviceId)
{
    if ((deviceId > PAL_DEVICE_IN_MIN) && (deviceId < PAL_DEVICE_IN_MAX))
        return true;

    return false;
}

void AudioDevice::process_snd_dev(const XML_Char **attr)
{
    uint32_t curIdx = 0;
    in_snd_device = PAL_DEVICE_NONE;

    if (strcmp(attr[curIdx++], "in_snd_device")) {
        AHAL_ERR("snd_device not found");
        return;
    }
    in_snd_device = deviceIdLUT.at((char *)attr[curIdx++]);
    if (!is_input_pal_dev_id(in_snd_device)) {
        AHAL_ERR("Sound device not valid");
        in_snd_device = PAL_DEVICE_NONE;
    }
    return;
}

bool AudioDevice::set_microphone_map(pal_device_id_t in_snd_device,
                                         const mic_info_t *info)
{
    uint32_t map_idx;
    uint32_t count;

    if (!is_input_pal_dev_id(in_snd_device)) {
        AHAL_ERR("Sound device not valid");
        return false;
    }

    map_idx = (MIC_INFO_MAP_INDEX(in_snd_device));
    count = microphone_maps[map_idx].mic_count++;
    if (count >= AUDIO_MICROPHONE_MAX_COUNT) {
        AHAL_ERR("Microphone count is greater than max allowed value");
        microphone_maps[map_idx].mic_count--;
        return false;
    }
    microphone_maps[map_idx].microphones[count] = *info;
    return true;
}

bool AudioDevice::is_built_in_input_dev(pal_device_id_t deviceId)
{
    if ((PAL_DEVICE_IN_HANDSET_MIC == deviceId) || (PAL_DEVICE_IN_SPEAKER_MIC == deviceId) ||
        (PAL_DEVICE_IN_HANDSET_VA_MIC == deviceId) || (PAL_DEVICE_IN_ULTRASOUND_MIC == deviceId))
        return true;

    return false;
}

void AudioDevice::process_mic_info(const XML_Char **attr)
{
    uint32_t curIdx = 0;
    mic_info_t microphone;
    char *context = NULL;
    uint32_t idx = 0;
    const char *token;

    memset(&microphone.channel_mapping, AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED,
                   sizeof(microphone.channel_mapping));

    if (strcmp(attr[curIdx++], "mic_device_id")) {
        AHAL_ERR("mic_device_id not found");
        goto on_error;
    }
    strlcpy(microphone.device_id, (char *)attr[curIdx++],
                            AUDIO_MICROPHONE_ID_MAX_LEN);

    if (strcmp(attr[curIdx++], "channel_mapping")) {
        AHAL_ERR("channel_mapping not found");
        goto on_error;
    }
    token = strtok_r((char *)attr[curIdx++], " ", &context);
    while (token) {
        if (!find_enum_by_string(mic_channel_mapping, token,
                    AUDIO_MICROPHONE_CHANNEL_MAPPING_CNT,
                    (unsigned int *)(&(microphone.channel_mapping[idx++])))) {
            AHAL_ERR("channel_mapping: %s not found", attr[--curIdx]);
            goto on_error;
        }
        token = strtok_r(NULL, " ", &context);
    }
    microphone.channel_count = idx;

    set_microphone_map(in_snd_device, &microphone);
    return;

on_error:
    in_snd_device = PAL_DEVICE_NONE;
    return;
}

int32_t AudioDevice::get_active_microphones(uint32_t channels, pal_device_id_t id,
                                            struct audio_microphone_characteristic_t *mic_array,
                                            uint32_t *mic_count)
{
    uint32_t actual_mic_count = 0;
    mic_info_t *m_info;
    uint32_t count = 0;
    uint32_t idx;
    uint32_t max_mic_count = microphones.declared_mic_count;

    if (!mic_characteristics_available)
        return -EIO;

    if (mic_count == NULL) {
        AHAL_ERR("Invalid mic_count!!!");
        return -EINVAL;
    }
    if (mic_array == NULL) {
        AHAL_ERR("Invalid mic_array!!!");
        return -EINVAL;
    }

    if (!is_input_pal_dev_id(id)) {
        AHAL_ERR("Invalid input sound device!!!");
        return -EINVAL;
    }

    if (*mic_count == 0) {
        AHAL_INFO("mic_count is ZERO");
        return 0;
    }

    if (is_input_pal_dev_id(id)) {
        idx = MIC_INFO_MAP_INDEX(id);
        count = microphone_maps[idx].mic_count;
        m_info = microphone_maps[idx].microphones;
        for (size_t i = 0; i < count; i++) {
            unsigned int channels_for_active_mic = channels;
            if (channels_for_active_mic > m_info[i].channel_count) {
                channels_for_active_mic = m_info[i].channel_count;
            }
            for (size_t j = 0; j < max_mic_count; j++) {
                if (strcmp(microphones.microphone[j].device_id,
                            m_info[i].device_id) == 0) {
                    mic_array[actual_mic_count] = microphones.microphone[j];
                    for (size_t ch = 0; ch < channels_for_active_mic; ch++) {
                        mic_array[actual_mic_count].channel_mapping[ch] =
                            m_info[i].channel_mapping[ch];
                    }
                    actual_mic_count++;
                    break;
                }
            }
        }
    }
    *mic_count = actual_mic_count;
    return 0;
}

void AudioDevice::xml_start_tag(void *userdata, const XML_Char *tag_name,
                         const XML_Char **attr)
{
    xml_userdata_t *data = (xml_userdata_t *)userdata;

    if (!strcmp(tag_name, "microphone")) {
        if (TAG_MICROPHONE_CHARACTERISTIC != data->tag) {
            AHAL_ERR("microphone tag only supported with MICROPHONE_CHARACTERISTIC section");
            return;
        }
        process_microphone_characteristics(attr);
        return;
    } else if (!strcmp(tag_name, "snd_dev")) {
        if (TAG_INPUT_SND_DEVICE_TO_MIC_MAPPING != data->tag) {
            AHAL_ERR("snd_dev tag only supported with INPUT_SND_DEVICE_TO_MIC_MAPPING section");
            return;
        }
        process_snd_dev(attr);
        return;
    } else if (!strcmp(tag_name, "mic_info")) {
        if (TAG_INPUT_SND_DEVICE_TO_MIC_MAPPING != data->tag) {
            AHAL_ERR("mic_info tag only supported with INPUT_SND_DEVICE_TO_MIC_MAPPING section");
            return;
        }
        if (PAL_DEVICE_NONE == in_snd_device) {
            AHAL_ERR("Error in previous tags, do not process mic info");
            return;
        }
        process_mic_info(attr);
        return;
    }

    if (!strcmp(tag_name, "microphone_characteristics")) {
        data->tag = TAG_MICROPHONE_CHARACTERISTIC;
    } else if (!strcmp(tag_name, "snd_devices")) {
        data->tag = TAG_SND_DEVICES;
    } else if (!strcmp(tag_name, "input_snd_device")) {
        if (TAG_SND_DEVICES != data->tag) {
            AHAL_ERR("input_snd_device tag only supported with SND_DEVICES section");
            return;
        }
        data->tag = TAG_INPUT_SND_DEVICE;
    } else if (!strcmp(tag_name, "input_snd_device_mic_mapping")) {
        if (TAG_INPUT_SND_DEVICE != data->tag) {
            AHAL_ERR("input_snd_device_mic_mapping tag only supported with INPUT_SND_DEVICE section");
            return;
        }
        data->tag = TAG_INPUT_SND_DEVICE_TO_MIC_MAPPING;;
    }
}

void AudioDevice::xml_end_tag(void *userdata, const XML_Char *tag_name)
{
    xml_userdata_t *data = (xml_userdata_t *)userdata;

    if (!strcmp(tag_name, "input_snd_device"))
        data->tag = TAG_SND_DEVICES;
    else if (!strcmp(tag_name, "input_snd_device_mic_mapping"))
        data->tag = TAG_INPUT_SND_DEVICE;
}

void AudioDevice::xml_char_data_handler(void *userdata, const XML_Char *s, int len)
{
   xml_userdata_t *data = (xml_userdata_t *)userdata;

   if (len + data->offs >= sizeof(data->data_buf) ) {
       data->offs += len;
       /* string length overflow, return */
       return;
   } else {
       memcpy(data->data_buf + data->offs, s, len);
       data->offs += len;
   }
}

int AudioDevice::parse_xml()
{
    XML_Parser parser;
    FILE *file = NULL;
    int ret = 0;
    int bytes_read;
    void *buf = NULL;
    xml_userdata_t xml_user_data;
    memset(&xml_user_data, 0, sizeof(xml_user_data));

    file = fopen(MIC_CHARACTERISTICS_XML_FILE, "r");
    if(!file) {
        ret = -EIO;
        AHAL_ERR("Failed to open xml file name %s", MIC_CHARACTERISTICS_XML_FILE);
        goto done;
    }

    parser = XML_ParserCreate(NULL);
    if (!parser) {
        ret = -EINVAL;
        AHAL_ERR("Failed to create XML parser ret %d", ret);
        goto closeFile;
    }
    XML_SetUserData(parser, &xml_user_data);
    XML_SetElementHandler(parser, xml_start_tag, xml_end_tag);
    XML_SetCharacterDataHandler(parser, xml_char_data_handler);

    while (1) {
        buf = XML_GetBuffer(parser, XML_READ_BUFFER_SIZE);
        if(buf == NULL) {
            ret = -EINVAL;
            AHAL_ERR("XML_Getbuffer failed ret %d", ret);
            goto freeParser;
        }

        bytes_read = fread(buf, 1, XML_READ_BUFFER_SIZE, file);
        if(bytes_read < 0) {
            ret = -EINVAL;
            AHAL_ERR("fread failed ret %d", ret);
            goto freeParser;
        }

        if(XML_ParseBuffer(parser, bytes_read, bytes_read == 0) == XML_STATUS_ERROR) {
            ret = -EINVAL;
            AHAL_ERR("XML ParseBuffer failed for %s file ret %d", MIC_CHARACTERISTICS_XML_FILE, ret);
            goto freeParser;
        }
        if (bytes_read == 0)
            break;
    }

freeParser:
    XML_ParserFree(parser);
closeFile:
    fclose(file);
    file = NULL;
done:
    return ret;
}
