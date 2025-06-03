/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "audio_hw.h"

typedef int (*lvimfs_create_instance_t)(void**, uint32_t, int32_t, int32_t, int32_t);
typedef int (*lvimfs_destroy_instance_t)(void*);
typedef int (*lvimfs_set_params_file_path_t)(const char*);
typedef void (*lvimfs_set_device_t)(void*, uint32_t);
typedef int (*lvimfs_process_t)(void*, void*, void*, int32_t);

extern struct lvimfs_wrapper_ops* lvimfs_wrapper_ops;

struct lvimfs_wrapper_ops {
    void* lib_handle;
    lvimfs_create_instance_t create_instance;
    lvimfs_destroy_instance_t destroy_instance;
    lvimfs_process_t process;
    lvimfs_set_params_file_path_t set_params_file_path;
    lvimfs_set_device_t set_device;
};

void lvimfs_init(void);
void lvimfs_deinit(void);
void lvimfs_start_input_stream(struct stream_in* in);
void lvimfs_process_input_stream(struct stream_in* in, void* buffer, size_t bytes);
void lvimfs_stop_input_stream(struct stream_in* in);
