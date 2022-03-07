/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef ANDROID_HARDWARE_AHAL_VOICE_H_
#define ANDROID_HARDWARE_AHAL_VOICE_H_

#include "AudioStream.h"

#define BASE_SESS_IDX       0
#define VOICE_SESS_IDX     (BASE_SESS_IDX)

#define MMODE1_SESS_IDX    (VOICE_SESS_IDX)
#define MMODE2_SESS_IDX    (VOICE_SESS_IDX + 1)

#define MAX_VOICE_SESSIONS 2
#define BASE_CALL_STATE     1
#define CALL_INACTIVE       (BASE_CALL_STATE)
#define CALL_ACTIVE         (BASE_CALL_STATE + 1)
#define VOICEMMODE1_VSID         0x11C05000
#define VOICEMMODE2_VSID         0x11DC5000

#define AUDIO_PARAMETER_KEY_VSID                "vsid"
#define AUDIO_PARAMETER_KEY_CALL_STATE          "call_state"
#define AUDIO_PARAMETER_KEY_DEVICE_MUTE         "device_mute"
#define AUDIO_PARAMETER_KEY_DIRECTION           "direction"
#define AUDIO_PARAMETER_KEY_VOLUME_BOOST        "volume_boost"
#define AUDIO_PARAMETER_KEY_SLOWTALK            "st_enable"
#define AUDIO_PARAMETER_KEY_HD_VOICE            "hd_voice"

/* TTY mode selection */
#define AUDIO_PARAMETER_KEY_TTY_MODE "tty_mode"
#define AUDIO_PARAMETER_VALUE_TTY_OFF "tty_off"
#define AUDIO_PARAMETER_VALUE_TTY_VCO "tty_vco"
#define AUDIO_PARAMETER_VALUE_TTY_HCO "tty_hco"
#define AUDIO_PARAMETER_VALUE_TTY_FULL "tty_full"

#define CODEC_BACKEND_DEFAULT_BIT_WIDTH 16

class AudioVoice {
public:
    struct call_state_t {
            int current_;
            int new_;
    };
    struct voice_session_t {
            call_state_t state;
            uint32_t vsid;
            uint32_t tty_mode;
            pal_stream_handle_t*  pal_voice_handle;
            bool volume_boost;
            bool slow_talk;
            bool hd_voice;
            struct pal_volume_data *pal_vol_data;
            pal_device_mute_t device_mute;
            bool hac;
    };
    struct voice_t {
            voice_session_t session[MAX_VOICE_SESSIONS];
            bool mic_mute;
            bool use_device_mute;
            float volume;
            bool in_call;
            uint32_t volume_steps;
    };
    voice_t voice_;
    audio_mode_t mode_;
    std::shared_ptr<StreamOutPrimary> stream_out_primary_;
    struct pal_volume_data *pal_vol_;
    int VoiceSetParameters(const char *kvpairs);
    void VoiceGetParameters(struct str_parms *query, struct str_parms *reply);
    int RouteStream(const std::set<audio_devices_t>&);
    bool is_valid_call_state(int call_state);
    bool is_valid_vsid(uint32_t vsid);
    int max_voice_sessions_;
    std::mutex voice_mutex_;
    int SetMode(const audio_mode_t mode);
    int VoiceStart(voice_session_t *session);
    int VoiceStop(voice_session_t *session);
    int VoiceSetDevice(voice_session_t *session);
    int SetDeviceMute(voice_session_t *session);
    int UpdateCallState(uint32_t vsid, int call_state);
    int UpdateCalls(voice_session_t *pSession);
    int SetMicMute(bool mute);
    int SetVoiceVolume(float volume);
    int GetMatchingTxDevices(const std::set<audio_devices_t>& rx_devs,
                             std::set<audio_devices_t>& tx_devs);
    bool IsCallActive(voice_session_t *pSession);
    bool IsAnyCallActive();
    int StopCall();
    AudioVoice();
    ~AudioVoice();
    pal_device_id_t pal_voice_tx_device_id_;
    pal_device_id_t pal_voice_rx_device_id_;
};
#endif  // ANDROID_HARDWARE_AHAL_VOICE_H_
