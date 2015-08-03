/*
 * Copyright (C) 2015 The CyanogenMod Project
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

#define LOG_TAG "msim_voice_extn"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>

#include "audio_hw.h"
#include "voice.h"
#include "platform.h"
#include "platform_api.h"
#include "msim_voice_extn.h"

#define AUDIO_PARAMETER_KEY_PHONETYPE "phone_type"

#define AUDIO_PARAMETER_VALUE_CP1 "cp1"
#define AUDIO_PARAMETER_VALUE_CP2 "cp2"

#ifdef HTC_DUAL_SIM
#define RADIO_PREFER_NETWORK_SLOT0 "persist.radio.prefer.network"
#define RADIO_PREFER_NETWORK_SLOT1 "persist.radio.prefer.nw.sub"
#elif SAMSUNG_DUAL_SIM
#define AUDIO_PROPERTY_SEC_VSID1 "gsm.current.vsid"
#define AUDIO_PROPERTY_SEC_VSID2 "gsm.current.vsid2"
#endif

#define VOICE2_VSID 0x10DC1000

#define VOICE2_SESS_IDX (VOICE_SESS_IDX + 1)

extern int start_call(struct audio_device *adev, audio_usecase_t usecase_id);
extern int stop_call(struct audio_device *adev, audio_usecase_t usecase_id);

static int msim_phone_type = 1;

int msim_voice_extn_start_call(struct audio_device *adev)
{
    audio_usecase_t usecase_id;

    usecase_id = msim_phone_type == 1 ? USECASE_VOICE_CALL : USECASE_VOICE2_CALL;
    return start_call(adev, usecase_id);
}

int msim_voice_extn_stop_call(struct audio_device *adev)
{
    audio_usecase_t usecase_id;

    usecase_id = msim_phone_type == 1 ? USECASE_VOICE_CALL : USECASE_VOICE2_CALL;
    return stop_call(adev, usecase_id);
}

int msim_voice_extn_get_session_from_use_case(struct audio_device *adev,
                                             const audio_usecase_t usecase_id __unused,
                                             struct voice_session **session)
{
    int idx;

    idx = msim_phone_type == 1 ? VOICE_SESS_IDX : VOICE2_SESS_IDX;
    *session = &adev->voice.session[idx];
    return 0;
}

int msim_voice_extn_set_parameters(struct audio_device *adev __unused,
                                  struct str_parms *parms)
{
    int ret;
    int voice_slot = 0;
    char value[32] = {0};

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_PHONETYPE, value,
                            sizeof(value));
    if (ret >= 0) {
#ifdef HTC_DUAL_SIM
        voice_slot = property_get_int32(RADIO_PREFER_NETWORK_SLOT0) != 1 ? 0 : 1;
        if (property_get_int32(RADIO_PREFER_NETWORK_SLOT0) ==
                property_get_int32(RADIO_PREFER_NETWORK_SLOT1)) {
            voice_slot = 0;
        }
        if (strcmp(value, AUDIO_PARAMETER_VALUE_CP2)) {
            msim_phone_type = voice_slot == 0 ? 1 : 0;
        } else {
            msim_phone_type = voice_slot == 0 ? 0 : 1;
        }
#elif SAMSUNG_DUAL_SIM
        msim_phone_type = property_get_int32(
                strcmp(value, AUDIO_PARAMETER_VALUE_CP2) ?
                AUDIO_PROPERTY_SEC_VSID1 : AUDIO_PROPERTY_SEC_VSID2) + 1;
#endif
        ALOGV("%s: phone_type: %d", __func__, msim_phone_type);
    }

    return 0;
}

int msim_voice_extn_is_call_state_active(struct audio_device *adev,
                                        bool *is_call_active)
{
    int idx;

    idx = msim_phone_type == 1 ? VOICE_SESS_IDX : VOICE2_SESS_IDX;
    *is_call_active = (adev->voice.session[idx].state.current == CALL_ACTIVE) ? true : false;
    return 0;
}

int msim_voice_extn_get_active_session_id(struct audio_device *adev __unused,
                                         uint32_t *session_id)
{
    *session_id = msim_phone_type == 1 ? VOICE_VSID : VOICE2_VSID;
    return 0;
}
