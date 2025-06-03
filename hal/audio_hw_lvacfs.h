/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "audio_hw.h"

typedef int (*lvacfs_create_instance_t)(void**, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int (*lvacfs_destroy_instance_t)(void*);
typedef int (*lvacfs_process_t)(void*, void*, void*, int32_t, void*);
typedef void (*lvacfs_set_params_file_path_t)(const char*);
typedef void (*lvacfs_set_profile_t)(void*, uint32_t);
typedef void (*lvacfs_update_zoom_info_t)(void*, float);
typedef void (*lvacfs_update_angle_info_t)(void*, float);
typedef void (*lvacfs_set_audio_direction_t)(void*, uint32_t);
typedef void (*lvacfs_set_device_orientation_t)(void*, uint32_t);
typedef void (*lvacfs_get_versions_t)(char*, size_t);

extern struct lvacfs_wrapper_ops* lvacfs_wrapper_ops;

struct lvacfs_wrapper_ops {
    void* lib_handle;
    lvacfs_create_instance_t create_instance;
    lvacfs_destroy_instance_t destroy_instance;
    lvacfs_process_t process;
    lvacfs_update_zoom_info_t update_zoom_info;
    lvacfs_update_angle_info_t update_angle_info;
    lvacfs_set_params_file_path_t set_params_file_path;
    lvacfs_set_profile_t set_profile;
    lvacfs_set_audio_direction_t set_audio_direction;
    lvacfs_set_device_orientation_t set_device_orientation;
    lvacfs_get_versions_t get_versions;
};

void lvacfs_init(void);
void lvacfs_deinit(void);
void lvacfs_start_input_stream(struct stream_in* in);
void lvacfs_process_input_stream(struct stream_in* in, void* buffer, size_t bytes);
void lvacfs_stop_input_stream(struct stream_in* in);
