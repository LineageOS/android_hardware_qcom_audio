/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "AudioDevice.h"
#include "AudioStream.h"
#include <tinyalsa/asoundlib.h>

enum st_event_type {
    ST_EVENT_SND_DEVICE_FREE,
    ST_EVENT_SND_DEVICE_BUSY,
    ST_EVENT_STREAM_FREE,
    ST_EVENT_STREAM_BUSY
};
typedef enum st_event_type st_event_type_t;

int audio_extn_sound_trigger_init(std::shared_ptr<AudioDevice> adev);
void audio_extn_sound_trigger_deinit(std::shared_ptr<AudioDevice> adev);
void audio_extn_sound_trigger_update_device_status(std::shared_ptr<audio_hw_device_t> device,
                                     st_event_type_t event);
void audio_extn_sound_trigger_update_stream_status(StreamPrimary *stream,
                                     st_event_type_t event);
void audio_extn_sound_trigger_update_battery_status(bool charging);
void audio_extn_sound_trigger_update_screen_status(bool screen_off);
void audio_extn_sound_trigger_set_parameters(std::shared_ptr<AudioDevice> adev,
                                             struct str_parms *parms);
void audio_extn_sound_trigger_check_and_get_session(StreamInPrimary *in_stream);
void audio_extn_sound_trigger_stop_lab(StreamInPrimary *in_stream);
int audio_extn_sound_trigger_read(StreamInPrimary *in_stream, void *buffer,
                                  size_t bytes);
void audio_extn_sound_trigger_get_parameters(const std::shared_ptr<AudioDevice> adev,
                     struct str_parms *query, struct str_parms *reply);
bool audio_extn_sound_trigger_check_ec_ref_enable();
void audio_extn_sound_trigger_update_ec_ref_status(bool on);

