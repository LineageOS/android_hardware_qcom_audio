/*
 * Copyright (c) 2013-2019, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "ahal_AudioDevice"
#define ATRACE_TAG (ATRACE_TAG_AUDIO|ATRACE_TAG_HAL)
/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include "AudioDevice.h"

#include <dlfcn.h>
#include <log/log.h>

#include "QalApi.h"
#include "audio_extn.h"

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
                        audio_devices_t devices,
                        audio_output_flags_t flags,
                        struct audio_config *config,
                        audio_stream_out **stream_out,
                        const char *address) {
    std::shared_ptr<StreamOutPrimary> astream (new StreamOutPrimary(handle,
                                              devices, flags, config, address,
                                              fnp_offload_effect_start_output_,
                                              fnp_offload_effect_stop_output_));
    astream->GetStreamHandle(stream_out);
    stream_out_list_.push_back(astream);
    ALOGD("%s: output stream %d %p", __func__,(int)stream_out_list_.size(), stream_out);
    return astream;
}

void AudioDevice::CloseStreamOut(std::shared_ptr<StreamOutPrimary> stream) {
    auto iter =
        std::find(stream_out_list_.begin(), stream_out_list_.end(), stream);

    if (iter == stream_out_list_.end()) {
        ALOGE("%s: invalid output stream", __func__);
    } else {
        stream_out_list_.erase(iter);
    }
}

std::shared_ptr<StreamInPrimary> AudioDevice::CreateStreamIn(
                                        audio_io_handle_t handle,
                                        audio_devices_t devices,
                                        audio_input_flags_t flags,
                                        struct audio_config *config,
                                        audio_stream_in **stream_in,
                                        audio_source_t source) {
    std::shared_ptr<StreamInPrimary> astream (new StreamInPrimary(handle,
                                              devices, flags, config, source));
    astream->GetStreamHandle(stream_in);
    stream_in_list_.push_back(astream);
    ALOGD("%s: input stream %d %p", __func__,(int)stream_in_list_.size(), stream_in); 
    return astream;
}

void AudioDevice::CloseStreamIn(std::shared_ptr<StreamInPrimary> stream) {
    auto iter =
        std::find(stream_in_list_.begin(), stream_in_list_.end(), stream);
    if (iter == stream_in_list_.end()) {
        ALOGE("%s: invalid output stream", __func__);
    } else {
        stream_in_list_.erase(iter);
    }
}

static int adev_close(hw_device_t *device __unused) {
    return 0;
}

static int adev_init_check(const struct audio_hw_device *dev __unused) {
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume) {
    std::ignore = dev;
    std::ignore = volume;

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

    ALOGD("%s: enter: format(%#x) sample_rate(%d) channel_mask(%#x) devices(%#x)\
        flags(%#x) address(%s)", __func__, config->format, config->sample_rate,
        config->channel_mask, devices, flags, address);

    std::shared_ptr<AudioDevice>adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        ALOGE("%s: invalid adevice object",__func__);
        goto exit;
    }
    astream = adevice->OutGetStream(handle);
 
    if (astream == nullptr) {
        adevice->CreateStreamOut(handle, devices, flags, config,
                                 stream_out, address);
    }

exit:
    return ret;
}

void adev_close_output_stream(struct audio_hw_device *dev,
                              struct audio_stream_out *stream) {

    std::shared_ptr<StreamOutPrimary> astream_out;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        ALOGE("%s: invalid adevice object",__func__);
        return;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        ALOGE("%s: invalid astream_in object",__func__);
        return;
    }

    ALOGD("%s: enter:stream_handle(%p)", __func__, astream_out.get());
    adevice->CloseStreamOut(astream_out);
}

void adev_close_input_stream(struct audio_hw_device *dev,
                             struct audio_stream_in *stream)
{
    std::shared_ptr<StreamInPrimary> astream_in;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        ALOGE("%s: invalid adevice object",__func__);
        return;
    }

    astream_in = adevice->InGetStream((audio_stream_t*)stream);

    if (!astream_in) {
        ALOGE("%s: invalid astream_in object",__func__);
        return;
    }

    ALOGD("%s: enter:stream_handle(%p)", __func__, astream_in.get());
    adevice->CloseStreamIn(astream_in);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags,
                                  const char *address __unused,
                                  audio_source_t source) {
    int32_t ret = 0;
    bool ret_error = false;
    std::shared_ptr<StreamInPrimary> astream = nullptr;
    ALOGD("%s: enter: sample_rate(%d) channel_mask(%#x) devices(%#x)\
        io_handle(%d) source(%d) format %x",__func__, config->sample_rate,
        config->channel_mask, devices, handle, source, config->format);

    std::shared_ptr<AudioDevice>adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        ALOGE("%s: invalid adevice object",__func__);
        goto exit;
    }
    if ((config->format == AUDIO_FORMAT_PCM_FLOAT) ||
        (config->format == AUDIO_FORMAT_PCM_32_BIT) ||
        (config->format == AUDIO_FORMAT_PCM_24_BIT_PACKED) ||
        (config->format == AUDIO_FORMAT_PCM_8_24_BIT)) {
    //astream->bit_width = 24;
    if ((source != AUDIO_SOURCE_UNPROCESSED) &&
            (source != AUDIO_SOURCE_CAMCORDER)) {
        config->format = AUDIO_FORMAT_PCM_16_BIT;
        if (config->sample_rate > 48000)
            config->sample_rate = 48000;
        ret_error = true;
    } else if (!(config->format == AUDIO_FORMAT_PCM_24_BIT_PACKED ||
                config->format == AUDIO_FORMAT_PCM_8_24_BIT)) {
        config->format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
        ret_error = true;
    }

    if (ret_error) {
        ret = -EINVAL;
        goto exit;
    }
    }

    if (config->format == AUDIO_FORMAT_PCM_FLOAT){
        ALOGE("%s: format not supported\n",__func__);
        config->format = AUDIO_FORMAT_PCM_16_BIT;
        ret = -EINVAL;
        goto exit;
    }

    astream = adevice->InGetStream(handle);
    if (astream == nullptr) {
        adevice->CreateStreamIn(handle, devices, flags, config,
                                stream_in, source);
    }
    //Need keep track of the list of streams that are allocated
  exit:
      return ret;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    std::ignore = dev;
    std::ignore = mode;
    ALOGD("%s: function not implemented",__func__);

    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance(dev);
    if (!adevice) {
        ALOGE("%s: invalid adevice object",__func__);
        return -EINVAL;
    }

    return adevice->SetMicMute(state);
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state) {
    std::shared_ptr<AudioDevice> adevice =
        AudioDevice::GetInstance((audio_hw_device_t *)dev);
    if (!adevice) {
        ALOGE("%s: invalid adevice object",__func__);
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
        ALOGE("%s: invalid adevice object",__func__);
        return -EINVAL;
    }

    return adevice->SetParameters(kvpairs);
}

static char* adev_get_parameters(const struct audio_hw_device *dev,
                                 const char *keys) {
    std::shared_ptr<AudioDevice> adevice =
        AudioDevice::GetInstance((audio_hw_device_t*)dev);
    if (!adevice) {
        ALOGE("%s: invalid adevice object",__func__);
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
    std::ignore = dev;
    std::ignore = handle;

    return 0;
}

int adev_create_audio_patch(struct audio_hw_device *dev,
                            unsigned int num_sources,
                            const struct audio_port_config *sources,
                            unsigned int num_sinks,
                            const struct audio_port_config *sinks,
                            audio_patch_handle_t *handle) {
    std::ignore = dev;
    std::ignore = num_sources;
    std::ignore = sources;
    std::ignore = num_sinks;
    std::ignore = sinks;
    std::ignore = handle;

    return 0;
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
    uint32_t maj_version = 2;

    ret = qal_init();
    if (ret) {
        ALOGE("%s:(%d) qal_init failed ret=(%d)",__func__,__LINE__, ret);
        return -EINVAL;
    }

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

    // offload effect lib
    if (access(OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH, R_OK) == 0) {
        offload_effects_lib_ = dlopen(OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH,
                                      RTLD_NOW);
        if (offload_effects_lib_ == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__,
                  OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH);
        } else {
            ALOGV("%s: DLOPEN successful for %s", __func__,
                  OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH);
            fnp_offload_effect_start_output_ =
                (int (*)(audio_io_handle_t, qal_stream_handle_t*))dlsym(
                                    offload_effects_lib_,
                                    "offload_effects_bundle_hal_start_output");
            fnp_offload_effect_stop_output_ =
                (int (*)(audio_io_handle_t, qal_stream_handle_t*))dlsym(
                                    offload_effects_lib_,
                                    "offload_effects_bundle_hal_stop_output");
        }
    }
    audio_extn_sound_trigger_init(adev_);

    return ret;
}

std::shared_ptr<StreamOutPrimary> AudioDevice::OutGetStream(
                                              audio_io_handle_t handle) {

    std::shared_ptr<StreamOutPrimary> astream_out = NULL;

    for (int i = 0; i < stream_out_list_.size(); i++){

        if (stream_out_list_[i]->handle_ == handle) {
            ALOGI("%s: Found existing stream associated with iohandle %d",
                  __func__, handle);
            astream_out = stream_out_list_[i];
            break;
        }
    }

    return astream_out;
}

std::shared_ptr<StreamOutPrimary> AudioDevice::OutGetStream(audio_stream_t* stream_out) {

    std::shared_ptr<StreamOutPrimary> astream_out;
    ALOGV("%s: stream_out(%p)",__func__, stream_out);
    for (int i = 0; i < stream_out_list_.size(); i++) {
        if (stream_out_list_[i]->stream_.get() ==
                                        (audio_stream_out*) stream_out) {
            ALOGV("%s: Found stream associated with stream_out", __func__);
            astream_out = stream_out_list_[i];
            break;
        }
    }
    ALOGV("%s: astream_out(%p)",__func__, astream_out->stream_.get());

    return astream_out;
}

std::shared_ptr<StreamInPrimary> AudioDevice::InGetStream (audio_io_handle_t handle) {
    std::shared_ptr<StreamInPrimary> astream_in = NULL;
    for (int i = 0; i < stream_in_list_.size(); i++){
        if (stream_in_list_[i]->handle_ == handle) {
            ALOGI("%s: Found existing stream associated with iohandle %d",
                  __func__, handle);
            astream_in = stream_in_list_[i];
            break;
        }
    }

    return astream_in;
}

std::shared_ptr<StreamInPrimary> AudioDevice::InGetStream (audio_stream_t* stream_in) {
    std::shared_ptr<StreamInPrimary> astream_in;

    ALOGV("%s: stream_in(%p)",__func__, stream_in);
    for (int i = 0; i < stream_in_list_.size(); i++) {
        if (stream_in_list_[i]->stream_.get() == (audio_stream_in*) stream_in) {
            ALOGI("%s: Found existing stream associated with astream_in", __func__);
            astream_in = stream_in_list_[i];
            break;
        }
    }
    ALOGV("%s: astream_in(%p)",__func__, astream_in->stream_.get());
    return astream_in;
}

int AudioDevice::SetMicMute(bool state) {
    std::ignore = state;
    return 0; //currently not implemented
}

int AudioDevice::GetMicMute(bool *state) {
    std::ignore = state;
    return 0; //currently not implemented
}

int AudioDevice::SetParameters(const char *kvpairs) {
    int ret = 0;
    ALOGD("%s: enter: %s", __func__, kvpairs);
    ALOGD("%s: exit: %s", __func__, kvpairs);
    return ret;
}

char* AudioDevice::GetParameters(const char *keys __unused) {

    return NULL;
}

static int adev_open(const hw_module_t *module, const char *name __unused,
                     hw_device_t **device) {
    int32_t ret = 0;
    ALOGD("%s: enter", __func__);

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    if(!adevice){
        ALOGE("%s: error, GetInstance failed",__func__);
    }

    ret = adevice->Init(device, module);

    if (ret || (*device == NULL)) {
      ALOGE("%s: error, audio device init failed, ret(%d),*device(%p)",
            __func__, ret, *device);
    }

    ALOGV("%s: exit", __func__);
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
