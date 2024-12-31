/*
 * Copyright (C) 2025 The LineageOS Project
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
 */

#define LOG_TAG "audio_hw_lvacfs"

#include "audio_hw_lvacfs.h"
#include <dlfcn.h>
#include <log/log.h>

#ifdef __LP64__
#define VENDOR_LIBS "/vendor/lib64/"
#define ODM_LIBS "/odm/lib64/"
#else
#define VENDOR_LIBS "/vendor/lib/"
#define ODM_LIBS "/odm/lib/"
#endif
#define LVACFS_WRAPPER_LIB_NAME "liblvacfs_wrapper.so"
#define VENDOR_LIB_PATH VENDOR_LIBS LVACFS_WRAPPER_LIB_NAME
#define ODM_LIB_PATH ODM_LIBS LVACFS_WRAPPER_LIB_NAME
#define ODM_PARAMS_DIR_PATH "/odm/etc/lvacfs_params"
#define VENDOR_PARAMS_DIR_PATH "/vendor/etc/lvacfs_params"

struct lvacfs_wrapper_ops* lvacfs_wrapper_ops = NULL;
static const char* lvacfs_params_file_path = NULL;

void lvacfs_init(void) {
    if (access(ODM_PARAMS_DIR_PATH, F_OK) == 0) {
        lvacfs_params_file_path = ODM_PARAMS_DIR_PATH;
    } else if (access(VENDOR_PARAMS_DIR_PATH, F_OK) == 0) {
        lvacfs_params_file_path = VENDOR_PARAMS_DIR_PATH;
    } else {
        ALOGE("No params directory found");
        lvacfs_deinit();
        return;
    }

    lvacfs_wrapper_ops = (struct lvacfs_wrapper_ops*)calloc(1, sizeof(struct lvacfs_wrapper_ops));
    if (!lvacfs_wrapper_ops) {
        ALOGE("Failed to allocate lvacfs_wrapper_ops");
        lvacfs_deinit();
        return;
    }

    lvacfs_wrapper_ops->lib_handle = dlopen(ODM_LIB_PATH, RTLD_NOW);
    if (!lvacfs_wrapper_ops->lib_handle) {
        lvacfs_wrapper_ops->lib_handle = dlopen(VENDOR_LIB_PATH, RTLD_NOW);
        if (!lvacfs_wrapper_ops->lib_handle) {
            ALOGE("dlopen failed for %s", LVACFS_WRAPPER_LIB_NAME, dlerror());
            lvacfs_deinit();
            return;
        }
    }

    if (!(lvacfs_wrapper_ops->create_instance = (lvacfs_create_instance_t)dlsym(
                  lvacfs_wrapper_ops->lib_handle, "lvacfs_wrapper_CreateLibraryInstance")) ||
        !(lvacfs_wrapper_ops->destroy_instance = (lvacfs_destroy_instance_t)dlsym(
                  lvacfs_wrapper_ops->lib_handle, "lvacfs_wrapper_DestroyLibraryInstance")) ||
        !(lvacfs_wrapper_ops->process = (lvacfs_process_t)dlsym(lvacfs_wrapper_ops->lib_handle,
                                                                "lvacfs_wrapper_Process")) ||
        !(lvacfs_wrapper_ops->update_zoom_info = (lvacfs_update_zoom_info_t)dlsym(
                  lvacfs_wrapper_ops->lib_handle, "lvacfs_wrapper_UpdateZoomInfo")) ||
        !(lvacfs_wrapper_ops->update_angle_info = (lvacfs_update_angle_info_t)dlsym(
                  lvacfs_wrapper_ops->lib_handle, "lvacfs_wrapper_UpdateAngleInfo")) ||
        !(lvacfs_wrapper_ops->set_params_file_path = (lvacfs_set_params_file_path_t)dlsym(
                  lvacfs_wrapper_ops->lib_handle, "lvacfs_SetParamsFilePath")) ||
        !(lvacfs_wrapper_ops->set_profile = (lvacfs_set_profile_t)dlsym(
                  lvacfs_wrapper_ops->lib_handle, "lvacfs_wrapper_SetProfile")) ||
        !(lvacfs_wrapper_ops->set_audio_direction = (lvacfs_set_audio_direction_t)dlsym(
                  lvacfs_wrapper_ops->lib_handle, "lvacfs_wrapper_SetAudioDirection")) ||
        !(lvacfs_wrapper_ops->set_device_orientation = (lvacfs_set_device_orientation_t)dlsym(
                  lvacfs_wrapper_ops->lib_handle, "lvacfs_wrapper_SetDeviceOrientation")) ||
        !(lvacfs_wrapper_ops->get_versions = (lvacfs_get_versions_t)dlsym(
                  lvacfs_wrapper_ops->lib_handle, "lvacfs_wrapper_GetVersions"))) {
        ALOGE("dlsym failed for one or more symbols");
        lvacfs_deinit();
        return;
    }

    ALOGI("Feature LVACFS is Enabled");
}

void lvacfs_deinit(void) {
    ALOGI("Feature LVACFS is Disabled");
    if (lvacfs_wrapper_ops) {
        if (lvacfs_wrapper_ops->lib_handle) {
            dlclose(lvacfs_wrapper_ops->lib_handle);
        }
        free(lvacfs_wrapper_ops);
        lvacfs_wrapper_ops = NULL;
    }
}

void lvacfs_start_input_stream(struct stream_in* in) {
    in->lvacfs_instance = calloc(1, sizeof(void*));
    if (!in->lvacfs_instance) {
        ALOGE("Failed to allocate memory for in->lvacfs_instance");
        return;
    }
    lvacfs_wrapper_ops->set_params_file_path(lvacfs_params_file_path);
    int channel_count = audio_channel_count_from_in_mask(in->channel_mask);
    uint32_t channels = ((channel_count & 0xFFFF) << 16) | (in->config.channels & 0xFFFF);
    uint64_t sample_rate_and_format = ((uint64_t)in->config.format << 32) | in->config.rate;
    int ret = lvacfs_wrapper_ops->create_instance(in->lvacfs_instance, in->source,
                                                  sample_rate_and_format, channels);
    if (ret < 0) {
        ALOGE("create instance failed: %d", ret);
        lvacfs_stop_input_stream(in);
    }
}

void lvacfs_process_input_stream(struct stream_in* in, void* buffer, size_t bytes) {
    pthread_mutex_lock(&in->lvacfs_lock);
    uint32_t samples = bytes / (in->config.channels * audio_bytes_per_sample(in->format));
    uint8_t status_buffer[0x160] = {0};
    int ret = lvacfs_wrapper_ops->process(in->lvacfs_instance, buffer, buffer, samples,
                                          status_buffer);
    if (ret < 0) {
        ALOGE("process failed: %d", ret);
        lvacfs_stop_input_stream(in);
    }
    pthread_mutex_unlock(&in->lvacfs_lock);
}

void lvacfs_stop_input_stream(struct stream_in* in) {
    if (!in->lvacfs_instance) {
        return;
    }
    int ret = lvacfs_wrapper_ops->destroy_instance(in->lvacfs_instance);
    if (ret < 0) {
        ALOGE("destroy instance failed: %d", ret);
    }
    free(in->lvacfs_instance);
    in->lvacfs_instance = NULL;
}
