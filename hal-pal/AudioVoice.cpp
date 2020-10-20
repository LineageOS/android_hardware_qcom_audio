/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "AHAL: AudioVoice"
#define ATRACE_TAG (ATRACE_TAG_AUDIO|ATRACE_TAG_HAL)
#define LOG_NDEBUG 0
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <log/log.h>
#include <stdio.h>
#include <cutils/str_parms.h>
#include "audio_extn.h"
#include "AudioVoice.h"
#include "PalApi.h"

#ifndef AUDIO_MODE_CALL_SCREEN
#define AUDIO_MODE_CALL_SCREEN 4
#endif

int AudioVoice::SetMode(const audio_mode_t mode) {
    int ret = 0;

    ALOGD("%s: enter: %d", __func__, mode);
    if (mode_ != mode) {
        /*start a new session for full voice call*/
        if ((mode ==  AUDIO_MODE_CALL_SCREEN && mode_ == AUDIO_MODE_IN_CALL)||
           (mode == AUDIO_MODE_IN_CALL && mode_ == AUDIO_MODE_CALL_SCREEN)){
            mode_ = mode;
            ALOGD("%s: call screen device switch called: %d", __func__, mode);
            VoiceSetDevice(voice_.session);
        } else {
            mode_ = mode;
            if (voice_.in_call && mode == AUDIO_MODE_NORMAL)
                ret = StopCall();
            else
                UpdateCalls(voice_.session);
        }
    }
    return ret;
}

int AudioVoice::VoiceSetParameters(const char *kvpairs) {
    int value, i;
    char c_value[32];
    int ret = 0, err;
    struct str_parms *parms;
    pal_param_payload *params = nullptr;
    uint32_t tty_mode;
    bool volume_boost;
    bool slow_talk;

    ALOGD("%s: Enter", __func__);

    parms = str_parms_create_str(kvpairs);
    if (!parms)
       return  -EINVAL;

    err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_VSID, &value);
    if (err >= 0) {
        uint32_t vsid = value;
        int call_state = -1;
        err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_CALL_STATE, &value);
        if (err >= 0) {
            call_state = value;
        } else {
            ALOGE("%s: call_state key not found", __func__);
            ret = -EINVAL;
            goto done;
        }

        if (is_valid_vsid(vsid) && is_valid_call_state(call_state)) {
            ret = UpdateCallState(vsid, call_state);
        } else {
            ALOGE("%s: invalid vsid:%x or call_state:%d",
                __func__, vsid, call_state);
            ret = -EINVAL;
            goto done;
        }
    }
    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_TTY_MODE, c_value, sizeof(c_value));
    if (err >= 0) {
        if (strcmp(c_value, AUDIO_PARAMETER_VALUE_TTY_OFF) == 0)
            tty_mode = PAL_TTY_OFF;
        else if (strcmp(c_value, AUDIO_PARAMETER_VALUE_TTY_VCO) == 0)
            tty_mode = PAL_TTY_VCO;
        else if (strcmp(c_value, AUDIO_PARAMETER_VALUE_TTY_HCO) == 0)
            tty_mode = PAL_TTY_HCO;
        else if (strcmp(c_value, AUDIO_PARAMETER_VALUE_TTY_FULL) == 0)
            tty_mode = PAL_TTY_FULL;
        else {
            ret = -EINVAL;
            goto done;
        }

        for ( i = 0; i < max_voice_sessions_; i++) {
            voice_.session[i].tty_mode = tty_mode;
            if (IsCallActive(&voice_.session[i])) {
                params = (pal_param_payload *)calloc(1,
                                   sizeof(pal_param_payload) + sizeof(tty_mode));
                if (!params) {
                    ALOGE("%s:%d calloc failed for size %zu", __func__, __LINE__,
                            sizeof(pal_param_payload) + sizeof(tty_mode));
                    continue;
                }
                params->payload_size = sizeof(tty_mode);
                memcpy(params->payload, &tty_mode, params->payload_size);
                pal_stream_set_param(voice_.session[i].pal_voice_handle,
                                     PAL_PARAM_ID_TTY_MODE, params);
                free(params);
                params = nullptr;
            }
        }
    }
    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VOLUME_BOOST, c_value, sizeof(c_value));
    if (err >= 0) {
        if (strcmp(c_value, "on") == 0)
            volume_boost = true;
        else if (strcmp(c_value, "off") == 0) {
            volume_boost = false;
        }
        else {
            ret = -EINVAL;
            goto done;
        }
        params = (pal_param_payload *)calloc(1, sizeof(pal_param_payload) +
                                                sizeof(volume_boost));
        if (!params) {
            ALOGE("%s:%d calloc failed for size %zu", __func__, __LINE__,
                   sizeof(pal_param_payload) + sizeof(volume_boost));
        } else {
            params->payload_size = sizeof(volume_boost);
            params->payload[0] = volume_boost;

            for ( i = 0; i < max_voice_sessions_; i++) {
                voice_.session[i].volume_boost = volume_boost;
                if (IsCallActive(&voice_.session[i])) {
                    pal_stream_set_param(voice_.session[i].pal_voice_handle,
                                        PAL_PARAM_ID_VOLUME_BOOST, params);
                }
            }
            free(params);
            params = nullptr;
        }
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_SLOWTALK, c_value, sizeof(c_value));
    if (err >= 0) {
        if (strcmp(c_value, "true") == 0)
            slow_talk = true;
        else if (strcmp(c_value, "false") == 0) {
            slow_talk = false;
        }
        else {
            ret = -EINVAL;
            goto done;
        }
        params = (pal_param_payload *)calloc(1, sizeof(pal_param_payload) +
                                                sizeof(slow_talk));
        if (!params) {
            ALOGE("%s:%d calloc failed for size %zu", __func__, __LINE__,
                   sizeof(pal_param_payload) + sizeof(slow_talk));
        } else {
            params->payload_size = sizeof(slow_talk);
            params->payload[0] = slow_talk;

            for ( i = 0; i < max_voice_sessions_; i++) {
                voice_.session[i].slow_talk = slow_talk;
                if (IsCallActive(&voice_.session[i])) {
                    pal_stream_set_param(voice_.session[i].pal_voice_handle,
                                         PAL_PARAM_ID_SLOW_TALK, params);
                }
            }
            free(params);
            params = nullptr;
        }
    }

done:
    str_parms_destroy(parms);
    return ret;
}

bool AudioVoice::is_valid_vsid(uint32_t vsid)
{
    if (vsid == VOICEMMODE1_VSID ||
        vsid == VOICEMMODE2_VSID)
        return true;
    else
        return false;
}

bool AudioVoice::is_valid_call_state(int call_state)
{
    if (call_state < CALL_INACTIVE || call_state > CALL_ACTIVE)
        return false;
    else
        return true;
}

int AudioVoice::GetMatchingTxDevices(const std::set<audio_devices_t>& rx_devices,
                                     std::set<audio_devices_t>& tx_devices){
    for(auto rx_dev : rx_devices)
        switch(rx_dev) {
            case AUDIO_DEVICE_OUT_EARPIECE:
                tx_devices.insert(AUDIO_DEVICE_IN_BUILTIN_MIC);
                break;
            case AUDIO_DEVICE_OUT_SPEAKER:
                tx_devices.insert(AUDIO_DEVICE_IN_BACK_MIC);
                break;
            case AUDIO_DEVICE_OUT_WIRED_HEADSET:
                tx_devices.insert(AUDIO_DEVICE_IN_WIRED_HEADSET);
                break;
            case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
                tx_devices.insert(AUDIO_DEVICE_IN_BUILTIN_MIC);
                break;
            case AUDIO_DEVICE_OUT_USB_HEADSET:
                tx_devices.insert(AUDIO_DEVICE_IN_USB_HEADSET);
                break;
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
                tx_devices.insert(AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET);
                break;
            default:
                tx_devices.insert(AUDIO_DEVICE_NONE);
                ALOGE("%s: unsupported Device Id of %d", __func__, rx_dev);
                break;
        }

    return tx_devices.size();
}

int AudioVoice::RouteStream(const std::set<audio_devices_t>& rx_devices) {
    int ret = 0;
    std::set<audio_devices_t> tx_devices;
    pal_device_id_t pal_rx_device = (pal_device_id_t) NULL;
    pal_device_id_t pal_tx_device = (pal_device_id_t) NULL;
    pal_device_id_t* pal_device_ids = NULL;
    uint16_t device_count = 0;

    ALOGD("%s Enter", __func__);

    if (AudioExtn::audio_devices_empty(rx_devices)){
        ALOGE("%s invalid routing device %d", __func__, AudioExtn::get_device_types(rx_devices));
        return 0;
    }

    GetMatchingTxDevices(rx_devices, tx_devices);
    device_count = tx_devices.size() > rx_devices.size() ? tx_devices.size() : rx_devices.size();

    pal_device_ids = (pal_device_id_t *)calloc(1, device_count * sizeof(pal_device_id_t));
    if (!pal_device_ids) {
        ALOGE("%s fail to allocate memory for pal device array", __func__);
        return -ENOMEM;
    }

    ALOGD("%s Routing is %d", __func__, AudioExtn::get_device_types(rx_devices));

    if (stream_out_primary_) {
        stream_out_primary_->getPalDeviceIds(rx_devices, pal_device_ids);
        pal_rx_device = pal_device_ids[0];
        memset(pal_device_ids, 0, device_count * sizeof(pal_device_id_t));
        stream_out_primary_->getPalDeviceIds(tx_devices, pal_device_ids);
        pal_tx_device = pal_device_ids[0];
    }

    bool same_dev = pal_voice_rx_device_id_ == pal_rx_device;
    pal_voice_rx_device_id_ = pal_rx_device;
    pal_voice_tx_device_id_ = pal_tx_device;

    if (!IsAnyCallActive()) {
        if (mode_ == AUDIO_MODE_IN_CALL || mode_ == AUDIO_MODE_CALL_SCREEN) {
            voice_.in_call = true;
            ret = UpdateCalls(voice_.session);
        }
    } else {
        //do device switch here
        if (!same_dev) {
            for (int i = 0; i < max_voice_sessions_; i++) {
                ret = VoiceSetDevice(&voice_.session[i]);
                if (ret)
                    ALOGE("%s Device switch failed for session[%d]", __func__, i);
            }
        }
    }

    free(pal_device_ids);

    return ret;
}

int AudioVoice::UpdateCallState(uint32_t vsid, int call_state) {
    voice_session_t *session = NULL;
    int i = 0, ret;
    bool is_call_active;


    for (i = 0; i < max_voice_sessions_; i++) {
        if (vsid == voice_.session[i].vsid) {
            session = &voice_.session[i];
            break;
        }
    }

    if (session) {
        session->state.new_ = call_state;
        is_call_active = IsCallActive(session);
        ALOGD("%s is_call_active:%d in_call:%d, mode:%d",
              __func__, is_call_active, voice_.in_call, mode_);
        if (is_call_active ||
                (voice_.in_call && (mode_ == AUDIO_MODE_IN_CALL || mode_ == AUDIO_MODE_CALL_SCREEN))) {
            ret = UpdateCalls(voice_.session);
        }
    } else {
        return -EINVAL;
    }

    return 0;
}

int AudioVoice::UpdateCalls(voice_session_t *pSession) {
    int i, ret = 0;
    voice_session_t *session = NULL;


    for (i = 0; i < max_voice_sessions_; i++) {
        session = &pSession[i];
        ALOGD("%s: cur_state=%d new_state=%d vsid=%x",
              __func__, session->state.current_, session->state.new_, session->vsid);

        switch(session->state.new_)
        {
        case CALL_ACTIVE:
            switch(session->state.current_)
            {
            case CALL_INACTIVE:
                ALOGD("%s: INACTIVE -> ACTIVE vsid:%x", __func__, session->vsid);
                ret = VoiceStart(session);
                if (ret < 0) {
                    ALOGE("%s: VoiceStart() failed", __func__);
                } else {
                    session->state.current_ = session->state.new_;
                }
                break;
            default:
                ALOGE("%s: CALL_ACTIVE cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current_, session->vsid);
                break;
            }
            break;

        case CALL_INACTIVE:
            switch(session->state.current_)
            {
            case CALL_ACTIVE:
                ALOGD("%s: ACTIVE -> INACTIVE vsid:%x", __func__, session->vsid);
                ret = VoiceStop(session);
                if (ret < 0) {
                    ALOGE("%s: VoiceStop() failed", __func__);
                } else {
                    session->state.current_ = session->state.new_;
                }
                break;

            default:
                ALOGE("%s: CALL_INACTIVE cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current_, session->vsid);
                break;
            }
            break;
        default:
            break;
        } //end out switch loop
    } //end for loop

    return ret;
}

int AudioVoice::StopCall() {
    int i;

    voice_.in_call = false;
    for (i = 0; i < max_voice_sessions_; i++)
        voice_.session[i].state.new_ = CALL_INACTIVE;
    return UpdateCalls(voice_.session);
}

bool AudioVoice::IsCallActive(AudioVoice::voice_session_t *pSession) {

    return (pSession->state.current_ != CALL_INACTIVE) ? true : false;
}

bool AudioVoice::IsAnyCallActive()
{
    int i;

    for (i = 0; i < max_voice_sessions_; i++) {
        if (IsCallActive(&voice_.session[i]))
            return true;
    }

    return false;
}

int AudioVoice::VoiceStart(voice_session_t *session) {
    int ret;
    struct pal_stream_attributes streamAttributes;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    struct pal_device palDevices[2];
    struct pal_channel_info out_ch_info = {0, {0}}, in_ch_info = {0, {0}};
    pal_param_payload *param_payload = nullptr;

    in_ch_info.channels = 1;
    in_ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;

    out_ch_info.channels = 2;
    out_ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
    out_ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;

    palDevices[0].id = pal_voice_tx_device_id_;
    palDevices[0].config.ch_info = in_ch_info;
    palDevices[0].config.sample_rate = 48000;
    palDevices[0].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    palDevices[0].config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
    palDevices[0].address.card_id = adevice->usb_card_id_;
    palDevices[0].address.device_num =adevice->usb_dev_num_;

    palDevices[1].id = pal_voice_rx_device_id_;
    palDevices[1].config.ch_info = out_ch_info;
    palDevices[1].config.sample_rate = 48000;
    palDevices[1].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    palDevices[1].config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
    palDevices[1].address.card_id = adevice->usb_card_id_;
    palDevices[1].address.device_num = adevice->usb_dev_num_;

    memset(&streamAttributes, 0, sizeof(streamAttributes));
    streamAttributes.type = PAL_STREAM_VOICE_CALL;
    streamAttributes.info.voice_call_info.VSID = session->vsid;
    streamAttributes.info.voice_call_info.tty_mode = session->tty_mode;
    if (mode_ == AUDIO_MODE_CALL_SCREEN) {
        ALOGD("%s: in call screen mode", __func__);
        palDevices[0].id = PAL_DEVICE_IN_PROXY;  //overwrite the device with proxy dev
        palDevices[1].id = PAL_DEVICE_OUT_PROXY;  //overwrite the device with proxy dev
    }
    streamAttributes.direction = PAL_AUDIO_INPUT_OUTPUT;
    streamAttributes.in_media_config.sample_rate = 48000;
    streamAttributes.in_media_config.ch_info = in_ch_info;
    streamAttributes.in_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    streamAttributes.in_media_config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
    streamAttributes.out_media_config.sample_rate = 48000;
    streamAttributes.out_media_config.ch_info = out_ch_info;
    streamAttributes.out_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    streamAttributes.out_media_config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format

    //streamAttributes.in_media_config.ch_info = ch_info;
    ret = pal_stream_open(&streamAttributes,
                          2,
                          palDevices,
                          0,
                          NULL,
                          NULL,//callback
                          (void *)this,
                          &session->pal_voice_handle);// Need to add this to the audio stream structure.

    ALOGD("%s:pal_stream_open() ret:%d line:%d", __func__, ret, __LINE__);
    if (ret) {
        ALOGE("%s Pal Stream Open Error (%x)", __func__, ret);
        ret = -EINVAL;
        goto error_open;
    }

    /*apply cached voice effects features*/
    if (session->slow_talk) {
        param_payload = (pal_param_payload *)calloc(1, sizeof(pal_param_payload) +
                                             sizeof(session->slow_talk));
        if (!param_payload) {
            ALOGE("%s:%d calloc for size %zu failed", __func__, __LINE__,
                   sizeof(pal_param_payload) + sizeof(session->slow_talk));
        } else {
            param_payload->payload_size = sizeof(session->slow_talk);
            param_payload->payload[0] = session->slow_talk;
            ret = pal_stream_set_param(session->pal_voice_handle,
                                       PAL_PARAM_ID_SLOW_TALK,
                                       param_payload);
            if (ret)
                ALOGE("%s Slow Talk enable failed %x", __func__, ret);
            free(param_payload);
            param_payload = nullptr;
        }
    }

    if (session->volume_boost) {
        param_payload = (pal_param_payload *)calloc(1, sizeof(pal_param_payload) +
                                             sizeof(session->volume_boost));
        if (!param_payload) {
            ALOGE("%s:%d calloc for size %zu failed", __func__, __LINE__,
                  sizeof(pal_param_payload) + sizeof(session->volume_boost));
        } else {
            param_payload->payload_size = sizeof(session->volume_boost);
            param_payload->payload[0] = session->volume_boost;
            ret = pal_stream_set_param(session->pal_voice_handle, PAL_PARAM_ID_VOLUME_BOOST,
                                   param_payload);
            if (ret)
                ALOGE("%s Volume Boost enable failed %x", __func__, ret);
            free(param_payload);
            param_payload = nullptr;
        }
    }

   ret = pal_stream_start(session->pal_voice_handle);
   if (ret) {
       ALOGE("%s Pal Stream Start Error (%x)", __func__, ret);
       ret = pal_stream_close(session->pal_voice_handle);
       if (ret)
           ALOGE("%s Pal Stream close failed %x", __func__, ret);
           session->pal_voice_handle = NULL;
           ret = -EINVAL;
   } else {
      ALOGD("%s Pal Stream Start Success", __func__);
   }

error_open:

    return ret;
}

int AudioVoice::VoiceStop(voice_session_t *session) {
    int ret = 0;

    if (session && session->pal_voice_handle) {
        ret = pal_stream_stop(session->pal_voice_handle);
        if (ret)
            ALOGE("%s Pal Stream stop failed %x", __func__, ret);
        ret = pal_stream_close(session->pal_voice_handle);
        if (ret)
            ALOGE("%s Pal Stream close failed %x", __func__, ret);
        session->pal_voice_handle = NULL;
    }

    if (ret)
        return -EINVAL;
    else
        return ret;
}

int AudioVoice::VoiceSetDevice(voice_session_t *session) {
    int ret = 0;
    struct pal_device palDevices[2];
    struct pal_channel_info out_ch_info = {0, {0}}, in_ch_info = {0, {0}};
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    in_ch_info.channels = 1;
    in_ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;

    out_ch_info.channels = 2;
    out_ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
    out_ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;

    palDevices[0].id = pal_voice_tx_device_id_;
    palDevices[0].config.ch_info = in_ch_info;
    palDevices[0].config.sample_rate = 48000;
    palDevices[0].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    palDevices[0].config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
    palDevices[0].address.card_id = adevice->usb_card_id_;
    palDevices[0].address.device_num =adevice->usb_dev_num_;

    palDevices[1].id = pal_voice_rx_device_id_;
    palDevices[1].config.ch_info = out_ch_info;
    palDevices[1].config.sample_rate = 48000;
    palDevices[1].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    palDevices[1].config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
    palDevices[1].address.card_id = adevice->usb_card_id_;
    palDevices[1].address.device_num =adevice->usb_dev_num_;

    if (mode_ == AUDIO_MODE_CALL_SCREEN) {
        ALOGD("%s: in call screen mode", __func__);
        palDevices[0].id = PAL_DEVICE_IN_PROXY;  //overwrite the device with proxy dev
        palDevices[1].id = PAL_DEVICE_OUT_PROXY;  //overwrite the device with proxy dev
    }

    if (session && session->pal_voice_handle) {
        ret = pal_stream_set_device(session->pal_voice_handle, 2, palDevices);
        if (ret)
            ALOGE("%s Pal Stream Set Device failed %x", __func__, ret);
    } else {
        ALOGE("%s Voice handle not found", __func__);
    }

    if (ret)
        return -EINVAL;
    else
        return ret;
}

int AudioVoice::SetMicMute(bool mute) {
    int ret = 0;
    voice_session_t *session = voice_.session;

    if (session) {
        for (int i = 0; i < max_voice_sessions_; i++) {
            if (session[i].pal_voice_handle) {
                ret = pal_stream_set_mute(session[i].pal_voice_handle, mute);
                if (ret)
                    ALOGE("%s Error applying mute %d for voice session %d", __func__, mute, i);
            }
        }
    }
    return ret;

}

int AudioVoice::SetVoiceVolume(float volume) {
    int ret = 0;
    struct pal_volume_data *pal_vol;
    voice_session_t *session = voice_.session;


    pal_vol = (struct pal_volume_data*)malloc(sizeof(uint32_t)
                + sizeof(struct pal_channel_vol_kv));
    if (pal_vol && session) {
        pal_vol->no_of_volpair = 1;
        pal_vol->volume_pair[0].channel_mask = 0x01;
        pal_vol->volume_pair[0].vol = volume;

        for (int i = 0; i < max_voice_sessions_; i++) {
            if (session[i].pal_voice_handle) {
                ret = pal_stream_set_volume(session[i].pal_voice_handle, pal_vol);
                ALOGD("%s volume applied on voice session %d", __func__, i);
            }
        }

        free(pal_vol);
    }

    return ret;
}

AudioVoice::AudioVoice() {

    voice_.in_call = false;
    max_voice_sessions_ = MAX_VOICE_SESSIONS;

    for (int i = 0; i < max_voice_sessions_; i++) {
        voice_.session[i].state.current_ = CALL_INACTIVE;
        voice_.session[i].state.new_ = CALL_INACTIVE;
        voice_.session[i].vsid = VOICEMMODE1_VSID;
        voice_.session[i].pal_voice_handle = NULL;
        voice_.session[i].tty_mode = PAL_TTY_OFF;
        voice_.session[i].volume_boost = false;
        voice_.session[i].slow_talk = false;
        voice_.session[i].pal_voice_handle = NULL;
    }

    voice_.session[MMODE1_SESS_IDX].vsid = VOICEMMODE1_VSID;
    voice_.session[MMODE2_SESS_IDX].vsid = VOICEMMODE2_VSID;

    stream_out_primary_ = NULL;
}

AudioVoice::~AudioVoice() {

    voice_.in_call = false;

    for (int i = 0; i < max_voice_sessions_; i++) {
        voice_.session[i].state.current_ = CALL_INACTIVE;
        voice_.session[i].state.new_ = CALL_INACTIVE;
        voice_.session[i].vsid = VOICEMMODE1_VSID;
        voice_.session[i].tty_mode = PAL_TTY_OFF;
        voice_.session[i].volume_boost = false;
        voice_.session[i].slow_talk = false;
        voice_.session[i].pal_voice_handle = NULL;
    }

    voice_.session[MMODE1_SESS_IDX].vsid = VOICEMMODE1_VSID;
    voice_.session[MMODE2_SESS_IDX].vsid = VOICEMMODE2_VSID;

    stream_out_primary_ = NULL;
    max_voice_sessions_ = 0;
}

