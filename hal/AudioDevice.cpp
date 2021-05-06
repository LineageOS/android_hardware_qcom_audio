/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
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
#include "AudioCommon.h"

#include "AudioDevice.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <cutils/str_parms.h>

#include <vector>
#include <map>

#include "PalApi.h"
#include "PalDefs.h"

#include <audio_extn/AudioExtn.h>
#include "audio_extn.h"
#include "battery_listener.h"

card_status_t AudioDevice::sndCardState = CARD_STATUS_ONLINE;

static void hdr_set_parameters(std::shared_ptr<AudioDevice> adev,
    struct str_parms *parms) {

    if (adev == nullptr || parms == nullptr) {
        AHAL_ERR("%s Invalid arguments", __func__);
        return;
    }

    int ret = 0, val = 0;
    char value[32];

    /* HDR Audio Parameters */
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HDR, value,
              sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "true", 4) == 0)
            adev->hdr_record_enabled = true;
        else
            adev->hdr_record_enabled = false;

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
    AHAL_ERR("output stream %d %p",(int)stream_out_list_.size(), stream_out);
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
        AHAL_ERR("exit: Invalid patch arguments");
        ret = -EINVAL;
        goto exit;
    }

    if (sources.size() > 1) {
        AHAL_ERR("Multiple sources are not supported");
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
                AHAL_ERR("Device to device patches not supported");
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

    if(!stream){
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

    ret = stream->RouteStream(device_types);
    if (voice_ && patch_type == AudioPatch::PATCH_PLAYBACK)
        ret |= voice_->RouteStream(device_types);

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

int AudioDevice::ReleaseAudioPatch(audio_patch_handle_t handle){
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
        AHAL_ERR("Patch info not found with handle %d", handle);
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

    if (!stream){
        AHAL_ERR("Failed to fetch stream with io handle %d", io_handle);
        return -EINVAL;
    }

    ret = stream->RouteStream({AUDIO_DEVICE_NONE});
    if (patch_type == AudioPatch::PATCH_PLAYBACK)
        ret |= voice_->RouteStream({AUDIO_DEVICE_NONE});

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
        AHAL_ERR("sound card offline");
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

    AHAL_DBG("enter:stream_handle(%p)", astream_out.get());

    adevice->CloseStreamOut(astream_out);

    AHAL_DBG("exit");
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

    AHAL_DBG("enter:stream_handle(%p)", astream_in.get());

    adevice->CloseStreamIn(astream_in);

    AHAL_DBG("exit");
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

static size_t adev_get_input_buffer_size(
                                const struct audio_hw_device *dev __unused,
                                const struct audio_config *config __unused) {
    return BUF_SIZE_CAPTURE * NO_OF_BUF;
}

int adev_release_audio_patch(struct audio_hw_device *dev,
                             audio_patch_handle_t handle) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice){
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

    if (!handle){
        AHAL_ERR("Invalid handle");
        return -EINVAL;
    }

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice){
        AHAL_ERR("GetInstance() failed");
        return -EINVAL;
    }

    std::vector<struct audio_port_config> source_vec(sources, sources + num_sources);
    std::vector<struct audio_port_config> sink_vec(sinks, sinks + num_sinks);

    return adevice->CreateAudioPatch(handle, source_vec, sink_vec);
}

int adev_get_audio_port(struct audio_hw_device *dev,
                        struct audio_port *config) {
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

static int adev_dump(const audio_hw_device_t *device __unused, int fd __unused)
{
    return 0;
}

static int adev_get_microphones(const struct audio_hw_device *dev __unused,
                struct audio_microphone_characteristic_t *mic_array __unused,
                size_t *mic_count __unused) {
    return -ENOSYS;
}

int AudioDevice::Init(hw_device_t **device, const hw_module_t *module) {
    int ret = 0;
    /* default audio HAL major version */
    uint32_t maj_version = 3;

    ret = pal_init();
    if (ret) {
        AHAL_ERR("pal_init failed ret=(%d)", ret);
        return -EINVAL;
    }

    ret = pal_register_global_callback(&adev_pal_global_callback, (uint64_t)this);
    if (ret) {
        AHAL_ERR("pal register callback failed ret=(%d)", ret);
    }
    /*
     *Once PAL init is sucessfull, register the PAL service
     *from HAL process context
     */
    ALOGE("Register Pal service");
    AudioExtn::audio_extn_hidl_init();

    adev_->device_.get()->common.tag = HARDWARE_DEVICE_TAG;
    adev_->device_.get()->common.version =
                                HARDWARE_DEVICE_API_VERSION(maj_version, 0);
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
    adev_->device_.get()->get_audio_port = adev_get_audio_port;
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
    /* no feature configurations yet */
    AudioExtn::battery_listener_feature_init(true);
    AudioExtn::battery_properties_listener_init(adev_on_battery_status_changed);
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
    int ret;
    std::shared_ptr<StreamInPrimary> astream_in;
    mute_ = state;

    ALOGD("%s: enter: %d", __func__, state);
    if (voice_)
        ret = voice_->SetMicMute(state);
    for (int i = 0; i < stream_in_list_.size(); i++) {
         astream_in = stream_in_list_[i];
         if (astream_in) {
             ALOGV("%s: Found existing stream associated with astream_in", __func__);
             ret = astream_in->SetMicMute(state);
         }
    }
    return 0;
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
                                                      pal_device_id_t** pal_device_ids)
{
    bool is_usb_headset = false;
    int count = *device_count;
    pal_device_id_t* temp = NULL;

    for (int i = 0; i < count; i++) {
         if (*pal_device_ids[i] == PAL_DEVICE_OUT_USB_HEADSET) {
             is_usb_headset = true;
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
        usb_input_dev_enabled = true;
    }
    return 0;
}

int AudioDevice::SetParameters(const char *kvpairs) {
    int ret = 0, val = 0;
    struct str_parms *parms;
    char value[32];
    int pal_device_count = 0;
    pal_device_id_t* pal_device_ids = NULL;
    char *test_r = NULL;
    char *cfg_str = NULL;

    AHAL_DBG("enter: %s", kvpairs);
    ret = voice_->VoiceSetParameters(kvpairs);
    if (ret)
        AHAL_ERR("Error in VoiceSetParameters %d", ret);

    parms = str_parms_create_str(kvpairs);
    if (!parms) {
        AHAL_ERR("Error in str_parms_create_str");
        return 0;
    }
    AudioExtn::audio_extn_set_parameters(adev_, parms);

    if (property_get_bool("vendor.audio.hdr.record.enable", false))
        hdr_set_parameters(adev_, parms);

    ret = str_parms_get_str(parms, "screen_state", value, sizeof(value));
    if (ret >= 0) {
        pal_param_screen_state_t param_screen_st;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0) {
            param_screen_st.screen_state = true;
            AHAL_DBG(" - screen = on");
            ret = pal_set_param( PAL_PARAM_ID_SCREEN_STATE, (void*)&param_screen_st, sizeof(pal_param_screen_state_t));
        }
        else {
            AHAL_DBG(" - screen = off");
            param_screen_st.screen_state = false;
            ret = pal_set_param( PAL_PARAM_ID_SCREEN_STATE, (void*)&param_screen_st, sizeof(pal_param_screen_state_t));
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
                    AHAL_INFO("Exit plugin card :%d device num=%d already added", usb_card_id_,
                          param_device_connection.device_config.usb_addr.device_num);
                    return 0;
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
            ret = add_input_headset_if_usb_out_headset(&pal_device_count, &pal_device_ids);
            if (ret) {
                free(pal_device_ids);
                AHAL_ERR("Exit adding input headset failed, error:%d", ret);
                return ret;
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
                AHAL_INFO("pal set param success  for device connection");
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
            AHAL_ERR("unexpected rotation of %d", val);
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
                }
                else {
                    AHAL_ERR ("Unable to parse the FTM time");
                }
            }
            else {
                AHAL_ERR ("Parameter missing for the FTM time");
            }
        }
        else {
            AHAL_ERR ("Unable to parse the FTM wait time");
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
                }
                else {
                    AHAL_ERR ("Unable to parse the V_Validation time");
                }
            }
            else {
                AHAL_ERR ("Parameter missing for the V-Validation time");
            }
        }
        else {
            AHAL_ERR ("Unable to parse the V-Validation wait time");
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
            if (pal_device_ids) {
                free(pal_device_ids);
                pal_device_ids = NULL;
            }
        }
    }

    if (pal_device_ids) {
        free(pal_device_ids);
        pal_device_ids = NULL;
    }

    ret = str_parms_get_str(parms, "BT_SCO", value, sizeof(value));
    if (ret >= 0) {
        pal_param_btsco_t param_bt_sco;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            param_bt_sco.bt_sco_on = true;
        else
            param_bt_sco.bt_sco_on = false;

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

    ret = str_parms_get_str(parms, "bt_swb", value, sizeof(value));
    if (ret >= 0) {
        pal_param_btsco_t param_bt_sco;

        val = atoi(value);
        param_bt_sco.bt_swb_speech_mode = val;
        AHAL_INFO("BTSCO SWB mode = 0x%x", val);
        ret = pal_set_param(PAL_PARAM_ID_BT_SCO_SWB, (void *)&param_bt_sco,
                            sizeof(pal_param_btsco_t));
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
        volume->no_of_volpair = 1;
        //For haptics, there is only one channel (FL).
        volume->volume_pair[0].channel_mask = 0x01;
        volume->volume_pair[0].vol = atof(value);
        AHAL_INFO("Setting Haptics Volume as %f", volume->volume_pair[0].vol);
        ret = pal_set_param(PAL_PARAM_ID_HAPTICS_VOLUME, (void *)volume,
                 sizeof(pal_volume_data));
        if (volume)
            free(volume);
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

    ret = str_parms_get_str(query, AUDIO_PARAMETER_A2DP_RECONFIG_SUPPORTED,
                            value, sizeof(value));
    if (ret >= 0) {
        pal_param_bta2dp_t *param_bt_a2dp;
        int32_t val = 0;

        ret = pal_get_param(PAL_PARAM_ID_BT_A2DP_RECONFIG_SUPPORTED,
                            (void **)&param_bt_a2dp, &size, nullptr);
        if (!ret) {
            if (size < sizeof(pal_param_bta2dp_t)) {
                AHAL_ERR("Size returned is smaller for BT_A2DP_RECONFIG_SUPPORTED");
                goto exit;
            }
            val = param_bt_a2dp->reconfig_supported;
            str_parms_add_int(reply, AUDIO_PARAMETER_A2DP_RECONFIG_SUPPORTED, val);
            AHAL_VERBOSE("isReconfigA2dpSupported = %d", val);
        }
    }

    ret = str_parms_get_str(query, "get_ftm_param", value, sizeof(value));
    if (ret >=0 ) {
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
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT, PAL_DEVICE_OUT_BLUETOOTH_SCO_CARKIT));
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
    //android_device_map_.insert(std::make_pair(AUDIO_DEVICE_OUT_SPEAKER_SAFE, PAL_DEVICE_OUT_SPEAKER_SAFE));
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
               AHAL_ERR("dp_controller: %d dp_stream: %d",
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
    AHAL_DBG("enter");

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
    AHAL_DBG("exit");
    return 0;
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

audio_patch_handle_t AudioPatch::generate_patch_handle_l(){
    static audio_patch_handle_t handles = AUDIO_PATCH_HANDLE_NONE;
    if (++handles < 0)
        handles = AUDIO_PATCH_HANDLE_NONE + 1;
    return handles;
}

AudioPatch::AudioPatch(PatchType patch_type,
                       const std::vector<struct audio_port_config>& sources,
                       const std::vector<struct audio_port_config>& sinks):
                       type(patch_type), sources(sources), sinks(sinks){
        static std::mutex patch_lock;
        std::lock_guard<std::mutex> lock(patch_lock);
        handle = AudioPatch::generate_patch_handle_l();
}
