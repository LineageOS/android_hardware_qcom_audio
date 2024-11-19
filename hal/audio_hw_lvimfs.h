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
