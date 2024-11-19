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

#define LOG_TAG "audio_hw_lvimfs"

#include "audio_hw_lvimfs.h"
#include <dlfcn.h>
#include <log/log.h>

#ifdef __LP64__
#define VENDOR_LIBS "/vendor/lib64/"
#define ODM_LIBS "/odm/lib64/"
#else
#define VENDOR_LIBS "/vendor/lib/"
#define ODM_LIBS "/odm/lib/"
#endif
#define lvimfs_WRAPPER_LIB_NAME "liblvimfs_wrapper.so"
#define VENDOR_LIB_PATH VENDOR_LIBS lvimfs_WRAPPER_LIB_NAME
#define ODM_LIB_PATH ODM_LIBS lvimfs_WRAPPER_LIB_NAME
#define ODM_PARAMS_DIR_PATH "/odm/etc/lvimfs_params"
#define VENDOR_PARAMS_DIR_PATH "/vendor/etc/lvimfs_params"

struct lvimfs_wrapper_ops* lvimfs_wrapper_ops = NULL;
static const char* lvimfs_params_file_path = NULL;

void lvimfs_init(void) {
    if (access(ODM_PARAMS_DIR_PATH, F_OK) == 0) {
        lvimfs_params_file_path = ODM_PARAMS_DIR_PATH;
    } else if (access(VENDOR_PARAMS_DIR_PATH, F_OK) == 0) {
        lvimfs_params_file_path = VENDOR_PARAMS_DIR_PATH;
    } else {
        ALOGE("No params directory found");
        lvimfs_deinit();
        return;
    }

    lvimfs_wrapper_ops = (struct lvimfs_wrapper_ops*)calloc(1, sizeof(struct lvimfs_wrapper_ops));
    if (!lvimfs_wrapper_ops) {
        ALOGE("Failed to allocate memory for lvimfs_wrapper_ops");
        lvimfs_deinit();
        return;
    }

    lvimfs_wrapper_ops->lib_handle = dlopen(ODM_LIB_PATH, RTLD_NOW);
    if (!lvimfs_wrapper_ops->lib_handle) {
        lvimfs_wrapper_ops->lib_handle = dlopen(VENDOR_LIB_PATH, RTLD_NOW);
        if (!lvimfs_wrapper_ops->lib_handle) {
            ALOGE("dlopen failed for %s", lvimfs_WRAPPER_LIB_NAME, dlerror());
            lvimfs_deinit();
            return;
        }
    }

    if (!(lvimfs_wrapper_ops->create_instance = (lvimfs_create_instance_t)dlsym(
                  lvimfs_wrapper_ops->lib_handle, "lvimfs_wrapper_CreateLibraryInstance")) ||
        !(lvimfs_wrapper_ops->destroy_instance = (lvimfs_destroy_instance_t)dlsym(
                  lvimfs_wrapper_ops->lib_handle, "lvimfs_wrapper_DestroyLibraryInstance")) ||
        !(lvimfs_wrapper_ops->process = (lvimfs_process_t)dlsym(lvimfs_wrapper_ops->lib_handle,
                                                                "lvimfs_wrapper_Process")) ||
        !(lvimfs_wrapper_ops->set_params_file_path = (lvimfs_set_params_file_path_t)dlsym(
                  lvimfs_wrapper_ops->lib_handle, "lvimfs_wrapper_SetParamsFilePath")) ||
        !(lvimfs_wrapper_ops->set_device = (lvimfs_set_device_t)dlsym(
                  lvimfs_wrapper_ops->lib_handle, "lvimfs_wrapper_SetDevice"))) {
        ALOGE("dlsym failed for one or more symbols");
        lvimfs_deinit();
        return;
    }

    ALOGI("Feature LVIMFS is Enabled");
}

void lvimfs_deinit(void) {
    ALOGI("Feature LVIMFS is Disabled");
    if (lvimfs_wrapper_ops) {
        if (lvimfs_wrapper_ops->lib_handle) {
            dlclose(lvimfs_wrapper_ops->lib_handle);
        }
        free(lvimfs_wrapper_ops);
        lvimfs_wrapper_ops = NULL;
    }
}

void lvimfs_start_input_stream(struct stream_in* in) {
    in->lvimfs_instance = calloc(1, sizeof(void*));
    if (!in->lvimfs_instance) {
        ALOGE("Failed to allocate memory for in->lvimfs_instance");
        return;
    }
    lvimfs_wrapper_ops->set_params_file_path(lvimfs_params_file_path);
    int channel_count = audio_channel_count_from_in_mask(in->channel_mask);
    int ret = lvimfs_wrapper_ops->create_instance(in->lvimfs_instance, in->source, in->config.rate,
                                                  in->config.format, channel_count);
    if (ret < 0) {
        ALOGE("create instance failed: %d", ret);
        lvimfs_stop_input_stream(in);
    }
}

void lvimfs_process_input_stream(struct stream_in* in, void* buffer, size_t bytes) {
    pthread_mutex_lock(&in->lvimfs_lock);
    uint32_t samples = bytes / (in->config.channels * audio_bytes_per_sample(in->format));
    int ret = lvimfs_wrapper_ops->process(in->lvimfs_instance, buffer, buffer, samples);
    if (ret < 0) {
        ALOGE("process failed: %d", ret);
        lvimfs_stop_input_stream(in);
    }
    pthread_mutex_unlock(&in->lvimfs_lock);
}

void lvimfs_stop_input_stream(struct stream_in* in) {
    if (!in->lvimfs_instance) {
        return;
    }
    int ret = lvimfs_wrapper_ops->destroy_instance(in->lvimfs_instance);
    if (ret < 0) {
        ALOGE("destroy instance failed: %d", ret);
    }
    free(in->lvimfs_instance);
    in->lvimfs_instance = NULL;
}
