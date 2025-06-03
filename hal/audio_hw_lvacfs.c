/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
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

#define LOAD_SYMBOL(handle, symbol_ptr, symbol_name)                           \
    do {                                                                       \
        *(void**)(&(symbol_ptr)) = dlsym((handle), (symbol_name));             \
        if (!(symbol_ptr)) {                                                   \
            ALOGE("Failed to load symbol '%s': %s", (symbol_name), dlerror()); \
            lvacfs_deinit();                                                   \
            return;                                                            \
        }                                                                      \
    } while (0)

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
            ALOGE("dlopen failed for %s: %s", LVACFS_WRAPPER_LIB_NAME, dlerror());
            lvacfs_deinit();
            return;
        }
    }

    LOAD_SYMBOL(lvacfs_wrapper_ops->lib_handle, lvacfs_wrapper_ops->create_instance,
                "lvacfs_wrapper_CreateLibraryInstance");
    LOAD_SYMBOL(lvacfs_wrapper_ops->lib_handle, lvacfs_wrapper_ops->destroy_instance,
                "lvacfs_wrapper_DestroyLibraryInstance");
    LOAD_SYMBOL(lvacfs_wrapper_ops->lib_handle, lvacfs_wrapper_ops->process,
                "lvacfs_wrapper_Process");
    LOAD_SYMBOL(lvacfs_wrapper_ops->lib_handle, lvacfs_wrapper_ops->update_zoom_info,
                "lvacfs_wrapper_UpdateZoomInfo");
    LOAD_SYMBOL(lvacfs_wrapper_ops->lib_handle, lvacfs_wrapper_ops->update_angle_info,
                "lvacfs_wrapper_UpdateAngleInfo");
    LOAD_SYMBOL(lvacfs_wrapper_ops->lib_handle, lvacfs_wrapper_ops->set_params_file_path,
                "lvacfs_SetParamsFilePath");
    LOAD_SYMBOL(lvacfs_wrapper_ops->lib_handle, lvacfs_wrapper_ops->set_profile,
                "lvacfs_wrapper_SetProfile");
    LOAD_SYMBOL(lvacfs_wrapper_ops->lib_handle, lvacfs_wrapper_ops->set_audio_direction,
                "lvacfs_wrapper_SetAudioDirection");
    LOAD_SYMBOL(lvacfs_wrapper_ops->lib_handle, lvacfs_wrapper_ops->set_device_orientation,
                "lvacfs_wrapper_SetDeviceOrientation");
    LOAD_SYMBOL(lvacfs_wrapper_ops->lib_handle, lvacfs_wrapper_ops->get_versions,
                "lvacfs_wrapper_GetVersions");

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
    in->lvacfs_handle = calloc(1, sizeof(void*));
    if (!in->lvacfs_handle) {
        ALOGE("Failed to allocate memory for in->lvacfs_handle");
        return;
    }
    lvacfs_wrapper_ops->set_params_file_path(lvacfs_params_file_path);
    int channel_count = audio_channel_count_from_in_mask(in->channel_mask);
    uint32_t channels = (in->config.channels << 16) | (channel_count & 0xFFFF);
    int ret = lvacfs_wrapper_ops->create_instance(in->lvacfs_handle, in->source, in->config.rate,
                                                  in->config.format, channels);
    if (ret < 0) {
        ALOGE("create instance failed: %d", ret);
        lvacfs_stop_input_stream(in);
    }
}

void lvacfs_process_input_stream(struct stream_in* in, void* buffer, size_t bytes) {
    pthread_mutex_lock(&in->lvacfs_lock);
    uint32_t num_frames = pcm_bytes_to_frames(in->pcm, bytes);
    uint8_t status_buffer[0x160] = {0};
    int ret = lvacfs_wrapper_ops->process(in->lvacfs_handle, buffer, buffer, num_frames,
                                          status_buffer);
    if (ret < 0) {
        ALOGE("process failed: %d", ret);
        lvacfs_stop_input_stream(in);
    }
    pthread_mutex_unlock(&in->lvacfs_lock);
}

void lvacfs_stop_input_stream(struct stream_in* in) {
    if (!in->lvacfs_handle) {
        return;
    }
    int ret = lvacfs_wrapper_ops->destroy_instance(in->lvacfs_handle);
    if (ret < 0) {
        ALOGE("destroy instance failed: %d", ret);
    }
    free(in->lvacfs_handle);
    in->lvacfs_handle = NULL;
}
