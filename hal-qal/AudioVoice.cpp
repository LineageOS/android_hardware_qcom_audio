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

#define LOG_TAG "ahal_AudioVoice"
#define ATRACE_TAG (ATRACE_TAG_AUDIO|ATRACE_TAG_HAL)
#define LOG_NDEBUG 0
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <log/log.h>
#include "audio_extn.h"
#include "AudioVoice.h"
#include "QalApi.h"

int AudioVoice::SetMode(const audio_mode_t mode) {
    int ret = 0;

    ALOGD("%s: enter: %d", __func__, mode);
    if (mode_ != mode) {
        mode_ = mode;
        if (voice_.in_call && mode == AUDIO_MODE_NORMAL)
            ret = StopCall();
    }
    return ret;
}

int AudioVoice::VoiceSetParameters(struct str_parms *parms) {
    int value;
    int ret = 0, err;

    ALOGD("%s: Enter", __func__);

    err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_VSID, &value);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_VSID);
        uint32_t vsid = value;
        int call_state = -1;
        err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_CALL_STATE, &value);
        if (err >= 0) {
            call_state = value;
            str_parms_del(parms, AUDIO_PARAMETER_KEY_CALL_STATE);
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

done:
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

audio_devices_t AudioVoice::GetMatchingTxDevice(audio_devices_t halRxDeviceId) {
    audio_devices_t halTxDeviceId = AUDIO_DEVICE_NONE;

    switch(halRxDeviceId) {
        case AUDIO_DEVICE_OUT_EARPIECE:
            halTxDeviceId = AUDIO_DEVICE_IN_BUILTIN_MIC;
            break;
        case AUDIO_DEVICE_OUT_SPEAKER:
            halTxDeviceId = AUDIO_DEVICE_IN_BACK_MIC ;
            break;
        case AUDIO_DEVICE_OUT_WIRED_HEADSET:
            halTxDeviceId = AUDIO_DEVICE_IN_WIRED_HEADSET;
            break;
        case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
            halTxDeviceId = AUDIO_DEVICE_IN_BUILTIN_MIC;
            break;
        default:
            halTxDeviceId = AUDIO_DEVICE_NONE;
            ALOGE("%s: unsupported Device Id of %d\n", __func__, halRxDeviceId);
            break;
    }

    return halTxDeviceId;
}

int AudioVoice::VoiceOutSetParameters(struct str_parms *parms) {
    char value[32];
    int ret = 0, val = 0, err;
    qal_device_id_t rx_device = (qal_device_id_t) NULL;
    qal_device_id_t tx_device = (qal_device_id_t) NULL;

    ALOGD("%s Enter", __func__);
    err = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (err >= 0) {
        val = atoi(value);
        ALOGD("%s Routing is %d", __func__, val);
        if (stream_out_primary_) {
            stream_out_primary_->getQalDeviceIds(val, &rx_device);
            stream_out_primary_->getQalDeviceIds(GetMatchingTxDevice(val), &tx_device);
        }
        bool same_dev = qal_voice_rx_device_id_ == rx_device;
        qal_voice_rx_device_id_ = rx_device;
        qal_voice_tx_device_id_ = tx_device;

        if(!IsCallActive(voice_.session)) {
            if (mode_ == AUDIO_MODE_IN_CALL) {
                voice_.in_call = true;
                ret = UpdateCalls(voice_.session);
            }
        } else {
            //do device switch here
            if (!same_dev) {
                for (int i = 0; i < max_voice_sessions_; i++) {
                    ret = VoiceSetDevice(&voice_.session[i]);
                    if (ret)
                        ALOGE("%s Device switch failed for session[%d]\n", __func__, i);
                }
            }
        }
    }
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
        is_call_active = IsCallActive(voice_.session);
        ALOGD("%s is_call_active:%d in_call:%d, mode:%d\n",
              __func__, is_call_active, voice_.in_call, mode_);
        if (is_call_active ||
                (voice_.in_call && mode_ == AUDIO_MODE_IN_CALL)) {
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
                    ALOGE("%s: VoiceStart() failed\n", __func__);
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
    int i;
    AudioVoice::voice_session_t *session = NULL;

    for (i = 0; i < max_voice_sessions_; i++) {
        session = &pSession[i];
        if (session->state.current_ != CALL_INACTIVE)
            return true;
    }

    return false;

}

int AudioVoice::VoiceStart(voice_session_t *session) {
    int ret;
    struct qal_stream_attributes streamAttributes;
    struct qal_device qalDevices[2];
    uint8_t channels = 0;
    struct qal_channel_info *out_ch_info = NULL, *in_ch_info = NULL;

    channels = 1;
    in_ch_info = (struct qal_channel_info *) calloc(1,sizeof(uint16_t) + sizeof(uint8_t)*channels);

    if (in_ch_info == NULL) {
        ALOGE("Allocation failed for channel map");
        ret = -ENOMEM;
        goto error_open;
    }

    channels = 2;
    out_ch_info = (struct qal_channel_info *) calloc(1,sizeof(uint16_t) + sizeof(uint8_t)*channels);
    if (out_ch_info == NULL) {
        ALOGE("Allocation failed for channel map");
        ret = -ENOMEM;
        goto error_open;
    }

    in_ch_info->channels = 1;
    in_ch_info->ch_map[0] = QAL_CHMAP_CHANNEL_FL;

    out_ch_info->channels = 2;
    out_ch_info->ch_map[0] = QAL_CHMAP_CHANNEL_FL;
    out_ch_info->ch_map[1] = QAL_CHMAP_CHANNEL_FR;

    qalDevices[0].id = qal_voice_tx_device_id_;
    qalDevices[0].config.ch_info = in_ch_info;
    qalDevices[0].config.sample_rate = 48000;
    qalDevices[0].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    qalDevices[0].config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format

    qalDevices[1].id = qal_voice_rx_device_id_;
    qalDevices[1].config.ch_info = out_ch_info;
    qalDevices[1].config.sample_rate = 48000;
    qalDevices[1].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    qalDevices[1].config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format

    memset(&streamAttributes, 0, sizeof(streamAttributes));
    streamAttributes.type = QAL_STREAM_VOICE_CALL;
    streamAttributes.info.voice_call_info.VSID = session->vsid;
    streamAttributes.direction = QAL_AUDIO_INPUT_OUTPUT;
    streamAttributes.in_media_config.sample_rate = 48000;
    streamAttributes.in_media_config.ch_info = in_ch_info;
    streamAttributes.in_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    streamAttributes.in_media_config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
    streamAttributes.out_media_config.sample_rate = 48000;
    streamAttributes.out_media_config.ch_info = out_ch_info;
    streamAttributes.out_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    streamAttributes.out_media_config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format

    //streamAttributes.in_media_config.ch_info = ch_info;
    ret = qal_stream_open(&streamAttributes,
                          2,
                          qalDevices,
                          0,
                          NULL,
                          NULL,//callback
                          (void *)this,
                          &session->qal_voice_handle);// Need to add this to the audio stream structure.

    ALOGD("%s:(%x:ret)%d",__func__,ret, __LINE__);

    if (ret) {
        ALOGE("%s Qal Stream Open Error (%x)", __func__, ret);
        ret = -EINVAL;
    } else {
        ret = qal_stream_start(session->qal_voice_handle);
        if (ret) {
            ALOGE("%s Qal Stream Start Error (%x)", __func__, ret);
            ret = qal_stream_close(session->qal_voice_handle);
            if (ret)
                ALOGE("%s Qal Stream close failed %x\n", __func__, ret);
            session->qal_voice_handle = NULL;
            ret = -EINVAL;
        }
        else
            ALOGD("%s Qal Stream Start Success", __func__);
    }


error_open:
    if (in_ch_info)
        free(in_ch_info);
    if (out_ch_info)
        free(out_ch_info);

    return ret;
}

int AudioVoice::VoiceStop(voice_session_t *session) {
    int ret = 0;

    if (session && session->qal_voice_handle) {
        ret = qal_stream_stop(session->qal_voice_handle);
        if (ret)
            ALOGE("%s Qal Stream stop failed %x\n", __func__, ret);
        ret = qal_stream_close(session->qal_voice_handle);
        if (ret)
            ALOGE("%s Qal Stream close failed %x\n", __func__, ret);
        session->qal_voice_handle = NULL;
    }

    if (ret)
        return -EINVAL;
    else
        return ret;
}

int AudioVoice::VoiceSetDevice(voice_session_t *session) {
    int ret = 0;
    struct qal_device qalDevices[2];
    uint8_t channels = 0;
    struct qal_channel_info *out_ch_info = NULL, *in_ch_info = NULL;

    channels = 1;
    in_ch_info = (struct qal_channel_info *) calloc(1,sizeof(uint16_t) + sizeof(uint8_t)*channels);

    if (in_ch_info == NULL) {
        ALOGE("Allocation failed for channel map");
        ret = -ENOMEM;
        goto error_open;
    }

    channels = 2;
    out_ch_info = (struct qal_channel_info *) calloc(1,sizeof(uint16_t) + sizeof(uint8_t)*channels);
    if (out_ch_info == NULL) {
        ALOGE("Allocation failed for channel map");
        ret = -ENOMEM;
        goto error_open;
    }


    qalDevices[0].id = qal_voice_tx_device_id_;
    qalDevices[0].config.ch_info = in_ch_info;
    qalDevices[0].config.sample_rate = 48000;
    qalDevices[0].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    qalDevices[0].config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format

    qalDevices[1].id = qal_voice_rx_device_id_;
    qalDevices[1].config.ch_info = out_ch_info;
    qalDevices[1].config.sample_rate = 48000;
    qalDevices[1].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    qalDevices[1].config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format

    if (session && session->qal_voice_handle) {
        ret = qal_stream_set_device(session->qal_voice_handle, 2, qalDevices);
        if (ret)
            ALOGE("%s Qal Stream Set Device failed %x\n", __func__, ret);
    } else {
        ALOGE("%s Voice handle not found \n", __func__);
    }

error_open:
    if (in_ch_info)
        free(in_ch_info);
    if (out_ch_info)
        free(out_ch_info);

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
            if (session[i].qal_voice_handle) {
                ret = qal_stream_set_mute(session[i].qal_voice_handle, mute);
                if (ret)
                    ALOGE("%s Error applying mute %d for voice session %d\n", __func__, mute, i);
            }
        }
    }
    return ret;
}

int AudioVoice::SetVoiceVolume(float volume) {
    int ret = 0;
    struct qal_volume_data *qal_vol;
    voice_session_t *session = voice_.session;


    qal_vol = (struct qal_volume_data*)malloc(sizeof(uint32_t)
                + sizeof(struct qal_channel_vol_kv));
    if (qal_vol && session) {
        qal_vol->no_of_volpair = 1;
        qal_vol->volume_pair[0].channel_mask = 0x01;
        qal_vol->volume_pair[0].vol = volume;

        for (int i = 0; i < max_voice_sessions_; i++) {
            if (session[i].qal_voice_handle) {
                ret = qal_stream_set_volume(session[i].qal_voice_handle, qal_vol);
                ALOGD("%s volume applied on voice session %d", __func__, i);
            }
        }

        free(qal_vol);
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
        voice_.session[i].qal_voice_handle = NULL;
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
    }

    voice_.session[MMODE1_SESS_IDX].vsid = VOICEMMODE1_VSID;
    voice_.session[MMODE2_SESS_IDX].vsid = VOICEMMODE2_VSID;

    stream_out_primary_ = NULL;
    max_voice_sessions_ = 0;
}

