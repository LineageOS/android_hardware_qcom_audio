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
#else
int us_init(struct audio_device *adev);
void us_deinit(void);
int us_start(void);
int us_stop(void);
#endif

#endif
