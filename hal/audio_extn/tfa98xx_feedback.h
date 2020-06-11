/*
 * Copyright (C) 2020 The LineageOS Project
 * Copyright (C) 2020 Pig <pig.priv@gmail.com>
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

#ifndef AUDIO_EXT_TFA98XX_FEEDBACK_H
#define AUDIO_EXT_TFA98XX_FEEDBACK_H

int audio_extn_tfa98xx_start_feedback(struct audio_device *adev,
                                      snd_device_t snd_device);

void audio_extn_tfa98xx_stop_feedback(struct audio_device *adev,
                                      snd_device_t snd_device);
#endif
