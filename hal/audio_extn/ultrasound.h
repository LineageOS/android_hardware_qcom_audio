/*
 * Copyright (c) 2017-2018 The LineageOS Project
 * Copyright (c) 2017 Balázs Triszka <balika011@protonmail.ch>
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

#ifndef ULTRASOUND_H
#define ULTRASOUND_H

#ifndef ELLIPTIC_ULTRASOUND_ENABLED
#define us_init(adev) (0)
#define us_deinit() (0)
#define us_start() (0)
#define us_stop() (0)
#define us_suspend(val) (0)
#define us_set_device_connection(device, connected) (0)
#define us_select_devices_pre() (0)
#define us_select_devices_post() (0)
#else
void us_init(struct audio_device *adev);
void us_deinit(void);
void us_start(void);
void us_stop(void);
void us_suspend(int val);
void us_set_device_connection(audio_devices_t device, bool connected);
bool us_check_and_set_codec_backend_cfg(struct audio_usecase *usecase,
                                        snd_device_t snd_device,
                                        unsigned int *sample_rate,
                                        unsigned int *bit_width);
void us_select_devices_pre(void);
void us_select_devices_post(void);
#endif

#endif
