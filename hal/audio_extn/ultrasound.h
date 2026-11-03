/*
 * Copyright (C) 2026 The LineageOS Project
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

#ifndef AUDIO_EXTN_ULTRASOUND_H
#define AUDIO_EXTN_ULTRASOUND_H

#include <cutils/str_parms.h>
#include "platform.h"

struct audio_device;

#ifdef ULTRASOUND_ENABLED
int audio_extn_ultrasound_init(struct audio_device *adev);
void audio_extn_ultrasound_deinit(void);
void audio_extn_ultrasound_set_parameters(struct audio_device *adev,
                                          struct str_parms *parms);
/* Called before an output sound device is enabled (ref count still 0). */
void audio_extn_ultrasound_pre_enable_snd_device(struct audio_device *adev,
                                                 snd_device_t snd_device);
/* Called after output routing changed (device enabled/disabled). */
void audio_extn_ultrasound_update_routing(struct audio_device *adev);
#else
#define audio_extn_ultrasound_init(adev) (0)
#define audio_extn_ultrasound_deinit() do { } while (0)
#define audio_extn_ultrasound_set_parameters(adev, parms) do { } while (0)
#define audio_extn_ultrasound_pre_enable_snd_device(adev, snd_device) do { } while (0)
#define audio_extn_ultrasound_update_routing(adev) do { } while (0)
#endif

#endif /* AUDIO_EXTN_ULTRASOUND_H */
