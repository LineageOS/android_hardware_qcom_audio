/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
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
#define LVIMFS_WRAPPER_LIB_NAME "liblvimfs_wrapper.so"
#define VENDOR_LIB_PATH VENDOR_LIBS LVIMFS_WRAPPER_LIB_NAME
#define ODM_LIB_PATH ODM_LIBS LVIMFS_WRAPPER_LIB_NAME
#define ODM_PARAMS_DIR_PATH "/odm/etc/lvimfs_params"
#define VENDOR_PARAMS_DIR_PATH "/vendor/etc/lvimfs_params"

#define LOAD_SYMBOL(handle, symbol_ptr, symbol_name)                           \
    do {                                                                       \
        (symbol_ptr) = (decltype(symbol_ptr))dlsym((handle), (symbol_name));   \
        if (!(symbol_ptr)) {                                                   \
            ALOGE("Failed to load symbol '%s': %s", (symbol_name), dlerror()); \
            deinit();                                                          \
            return;                                                            \
        }                                                                      \
    } while (0)

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
            ALOGE("dlopen failed for %s: %s", LVIMFS_WRAPPER_LIB_NAME, dlerror());
            lvimfs_deinit();
            return;
        }
    }

    LOAD_SYMBOL(lvimfs_wrapper_ops->lib_handle, lvimfs_wrapper_ops->create_instance,
                "lvimfs_wrapper_CreateLibraryInstance");
    LOAD_SYMBOL(lvimfs_wrapper_ops->lib_handle, lvimfs_wrapper_ops->destroy_instance,
                "lvimfs_wrapper_DestroyLibraryInstance");
    LOAD_SYMBOL(lvimfs_wrapper_ops->lib_handle, lvimfs_wrapper_ops->process,
                "lvimfs_wrapper_Process");
    LOAD_SYMBOL(lvimfs_wrapper_ops->lib_handle, lvimfs_wrapper_ops->set_params_file_path,
                "lvimfs_wrapper_SetParamsFilePath");
    LOAD_SYMBOL(lvimfs_wrapper_ops->lib_handle, lvimfs_wrapper_ops->set_device,
                "lvimfs_wrapper_SetDevice");

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
    in->lvimfs_handle = calloc(1, sizeof(void*));
    if (!in->lvimfs_handle) {
        ALOGE("Failed to allocate memory for in->lvimfs_handle");
        return;
    }
    lvimfs_wrapper_ops->set_params_file_path(lvimfs_params_file_path);
    int channel_count = audio_channel_count_from_in_mask(in->channel_mask);
    int ret = lvimfs_wrapper_ops->create_instance(in->lvimfs_handle, in->source, in->config.rate,
                                                  in->config.format, channel_count);
    if (ret < 0) {
        ALOGE("create instance failed: %d", ret);
        lvimfs_stop_input_stream(in);
    }
}

void lvimfs_process_input_stream(struct stream_in* in, void* buffer, size_t bytes) {
    pthread_mutex_lock(&in->lvimfs_lock);
    uint32_t num_frames = pcm_bytes_to_frames(in->pcm, bytes);
    int ret = lvimfs_wrapper_ops->process(in->lvimfs_handle, buffer, buffer, num_frames);
    if (ret < 0) {
        ALOGE("process failed: %d", ret);
        lvimfs_stop_input_stream(in);
    }
    pthread_mutex_unlock(&in->lvimfs_lock);
}

void lvimfs_stop_input_stream(struct stream_in* in) {
    if (!in->lvimfs_handle) {
        return;
    }
    int ret = lvimfs_wrapper_ops->destroy_instance(in->lvimfs_handle);
    if (ret < 0) {
        ALOGE("destroy instance failed: %d", ret);
    }
    free(in->lvimfs_handle);
    in->lvimfs_handle = NULL;
}
