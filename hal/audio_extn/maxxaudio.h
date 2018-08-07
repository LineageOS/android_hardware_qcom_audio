/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef MAXXAUDIO_H_
#define MAXXAUDIO_H_

#ifndef MAXXAUDIO_QDSP_ENABLED
#define audio_extn_ma_init(platform)                                (0)
#define audio_extn_ma_deinit()                                      (0)
#define audio_extn_ma_set_state(adev, type, vol, active)            (false)
#define audio_extn_ma_set_device(usecase)                           (0)
#define audio_extn_ma_set_parameters(adev, param)                   (0)
#define audio_extn_ma_supported_usb()                               (false)
#else
void audio_extn_ma_init(void *platform);
void audio_extn_ma_deinit();
bool audio_extn_ma_set_state(struct audio_device *adev, int stream_type,
                             float vol, bool active);
void audio_extn_ma_set_device(struct audio_usecase *usecase);
void audio_extn_ma_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms);
bool audio_extn_ma_supported_usb();
#endif /* MAXXAUDIO_QDSP_ENABLED */

#endif /* MAXXAUDIO_H_ */

