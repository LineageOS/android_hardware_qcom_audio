/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Not a contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
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

#define LOG_TAG "voice_extn"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <sys/ioctl.h>
#include <sound/voice_params.h>

#include "audio_hw.h"
#include "voice.h"
#include "platform.h"
#include "platform_api.h"
#include "voice_extn.h"

#define AUDIO_PARAMETER_KEY_VSID        "vsid"
#define AUDIO_PARAMETER_KEY_CALL_STATE  "call_state"

#define VOICE2_VSID 0x10DC1000
#define VOLTE_VSID  0x10C02000
#define QCHAT_VSID  0x10803000
#define ALL_VSID    0xFFFFFFFF

/* Voice Session Indices */
#define VOICE2_SESS_IDX    (VOICE_SESS_IDX + 1)
#define VOLTE_SESS_IDX     (VOICE_SESS_IDX + 2)
#define QCHAT_SESS_IDX     (VOICE_SESS_IDX + 3)

/* Call States */
#define CALL_HOLD           (BASE_CALL_STATE + 2)
#define CALL_LOCAL_HOLD     (BASE_CALL_STATE + 3)

struct pcm_config pcm_config_incall_music = {
    .channels = 1,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

extern int start_call(struct audio_device *adev, audio_usecase_t usecase_id);
extern int stop_call(struct audio_device *adev, audio_usecase_t usecase_id);
int voice_extn_is_in_call(struct audio_device *adev, bool *in_call);

static bool is_valid_call_state(int call_state)
{
    if (call_state < CALL_INACTIVE || call_state > CALL_LOCAL_HOLD)
        return false;
    else
        return true;
}

static bool is_valid_vsid(uint32_t vsid)
{
    if (vsid == VOICE_VSID ||
        vsid == VOICE2_VSID ||
        vsid == VOLTE_VSID ||
        vsid == QCHAT_VSID)
        return true;
    else
        return false;
}

static audio_usecase_t voice_extn_get_usecase_for_session_idx(const int index)
{
    audio_usecase_t usecase_id = -1;

    switch(index) {
    case VOICE_SESS_IDX:
        usecase_id = USECASE_VOICE_CALL;
        break;

    case VOICE2_SESS_IDX:
        usecase_id = USECASE_VOICE2_CALL;
        break;

    case VOLTE_SESS_IDX:
        usecase_id = USECASE_VOLTE_CALL;
        break;

    case QCHAT_SESS_IDX:
        usecase_id = USECASE_QCHAT_CALL;
        break;

    default:
        ALOGE("%s: Invalid voice session index\n", __func__);
    }

    return usecase_id;
}

static uint32_t get_session_id_with_state(struct audio_device *adev,
                                          int call_state)
{
    struct voice_session *session = NULL;
    int i = 0;
    uint32_t session_id = 0;

    for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
        session = &adev->voice.session[i];
        if(session->state.current == call_state){
            session_id = session->vsid;
            break;
        }
    }

    return session_id;
}

static int update_calls(struct audio_device *adev)
{
    int i = 0;
    audio_usecase_t usecase_id = 0;
    enum voice_lch_mode lch_mode;
    struct voice_session *session = NULL;
    int fd = 0;
    int ret = 0;

    ALOGD("%s: enter:", __func__);

    for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
        usecase_id = voice_extn_get_usecase_for_session_idx(i);
        session = &adev->voice.session[i];
        ALOGD("%s: cur_state=%d new_state=%d vsid=%x",
              __func__, session->state.current, session->state.new, session->vsid);

        switch(session->state.new)
        {
        case CALL_ACTIVE:
            switch(session->state.current)
            {
            case CALL_INACTIVE:
                ALOGD("%s: INACTIVE -> ACTIVE vsid:%x", __func__, session->vsid);
                ret = start_call(adev, usecase_id);
                if(ret < 0) {
                    ALOGE("%s: voice_start_call() failed for usecase: %d\n",
                          __func__, usecase_id);
                } else {
                    session->state.current = session->state.new;
                }
                break;

            case CALL_HOLD:
                ALOGD("%s: HOLD -> ACTIVE vsid:%x", __func__, session->vsid);
                session->state.current = session->state.new;
                break;

            case CALL_LOCAL_HOLD:
                ALOGD("%s: LOCAL_HOLD -> ACTIVE vsid:%x", __func__, session->vsid);
                lch_mode = VOICE_LCH_STOP;
                if (pcm_ioctl(session->pcm_tx, SNDRV_VOICE_IOCTL_LCH, &lch_mode) < 0) {
                    ALOGE("LOCAL_HOLD -> ACTIVE failed");
                } else {
                    session->state.current = session->state.new;
                }
                break;

            default:
                ALOGV("%s: CALL_ACTIVE cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        case CALL_INACTIVE:
            switch(session->state.current)
            {
            case CALL_ACTIVE:
            case CALL_HOLD:
            case CALL_LOCAL_HOLD:
                ALOGD("%s: ACTIVE/HOLD/LOCAL_HOLD -> INACTIVE vsid:%x", __func__, session->vsid);
                ret = stop_call(adev, usecase_id);
                if(ret < 0) {
                    ALOGE("%s: voice_end_call() failed for usecase: %d\n",
                          __func__, usecase_id);
                } else {
                    session->state.current = session->state.new;
                }
                break;

            default:
                ALOGV("%s: CALL_INACTIVE cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        case CALL_HOLD:
            switch(session->state.current)
            {
            case CALL_ACTIVE:
                ALOGD("%s: CALL_ACTIVE -> HOLD vsid:%x", __func__, session->vsid);
                session->state.current = session->state.new;
                break;

            case CALL_LOCAL_HOLD:
                ALOGD("%s: CALL_LOCAL_HOLD -> HOLD vsid:%x", __func__, session->vsid);
                lch_mode = VOICE_LCH_STOP;
                if (pcm_ioctl(session->pcm_tx, SNDRV_VOICE_IOCTL_LCH, &lch_mode) < 0) {
                    ALOGE("LOCAL_HOLD -> HOLD failed");
                } else {
                    session->state.current = session->state.new;
                }
                break;

            default:
                ALOGV("%s: CALL_HOLD cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        case CALL_LOCAL_HOLD:
            switch(session->state.current)
            {
            case CALL_ACTIVE:
            case CALL_HOLD:
                ALOGD("%s: ACTIVE/CALL_HOLD -> LOCAL_HOLD vsid:%x", __func__,
                      session->vsid);
                lch_mode = VOICE_LCH_START;
                if (pcm_ioctl(session->pcm_tx, SNDRV_VOICE_IOCTL_LCH, &lch_mode) < 0) {
                    ALOGE("LOCAL_HOLD -> HOLD failed");
                } else {
                    session->state.current = session->state.new;
                }
                break;

            default:
                ALOGV("%s: CALL_LOCAL_HOLD cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        default:
            break;
        } //end out switch loop
    } //end for loop

    return ret;
}

static int update_call_states(struct audio_device *adev,
                                    const uint32_t vsid, const int call_state)
{
    struct voice_session *session = NULL;
    int i = 0;
    bool is_in_call;

    for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
        if (vsid == adev->voice.session[i].vsid) {
            session = &adev->voice.session[i];
            break;
        }
    }

    if (session) {
        session->state.new = call_state;
        voice_extn_is_in_call(adev, &is_in_call);
        ALOGD("%s is_in_call:%d mode:%d\n", __func__, is_in_call, adev->mode);
        /* Dont start voice call before device routing for voice usescases has
         * occured, otherwise voice calls will be started unintendedly on
         * speaker.
         */
        if (is_in_call ||
            (adev->mode == AUDIO_MODE_IN_CALL &&
             adev->primary_output->devices != AUDIO_DEVICE_OUT_SPEAKER)) {
            /* Device routing is not triggered for voice calls on the subsequent
             * subs, Hence update the call states if voice call is already
             * active on other sub.
             */
            update_calls(adev);
        }
    } else {
        return -EINVAL;
    }

    return 0;

}

int voice_extn_get_active_session_id(struct audio_device *adev,
                                     uint32_t *session_id)
{
    *session_id = get_session_id_with_state(adev, CALL_ACTIVE);
    return 0;
}

int voice_extn_is_in_call(struct audio_device *adev, bool *in_call)
{
    struct voice_session *session = NULL;
    int i = 0;
    *in_call = false;

    for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
        session = &adev->voice.session[i];
        if(session->state.current != CALL_INACTIVE){
            *in_call = true;
            break;
        }
    }

    return 0;
}

void voice_extn_init(struct audio_device *adev)
{
    adev->voice.session[VOICE_SESS_IDX].vsid =  VOICE_VSID;
    adev->voice.session[VOICE2_SESS_IDX].vsid = VOICE2_VSID;
    adev->voice.session[VOLTE_SESS_IDX].vsid =  VOLTE_VSID;
    adev->voice.session[QCHAT_SESS_IDX].vsid =  QCHAT_VSID;
}

int voice_extn_get_session_from_use_case(struct audio_device *adev,
                                         const audio_usecase_t usecase_id,
                                         struct voice_session **session)
{

    switch(usecase_id)
    {
    case USECASE_VOICE_CALL:
        *session = &adev->voice.session[VOICE_SESS_IDX];
        break;

    case USECASE_VOICE2_CALL:
        *session = &adev->voice.session[VOICE2_SESS_IDX];
        break;

    case USECASE_VOLTE_CALL:
        *session = &adev->voice.session[VOLTE_SESS_IDX];
        break;

    case USECASE_QCHAT_CALL:
        *session = &adev->voice.session[QCHAT_SESS_IDX];
        break;

    default:
        ALOGE("%s: Invalid usecase_id:%d\n", __func__, usecase_id);
        *session = NULL;
        return -EINVAL;
    }

    return 0;
}

int voice_extn_start_call(struct audio_device *adev)
{
    /* Start voice calls on sessions whose call state has been
     * udpated.
     */
    ALOGV("%s: enter:", __func__);
    return update_calls(adev);
}

int voice_extn_stop_call(struct audio_device *adev)
{
    int i;
    int ret = 0;

    ALOGV("%s: enter:", __func__);

    /* If BT device is enabled and voice calls are ended, telephony will call
     * set_mode(AUDIO_MODE_NORMAL) which will trigger audio policy manager to
     * set routing with device BT A2DP profile. Hence end all voice calls when
     * set_mode(AUDIO_MODE_NORMAL) before BT A2DP profile is selected.
     */
    if (adev->mode == AUDIO_MODE_NORMAL) {
        ALOGD("%s: end all calls", __func__);
        for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
            adev->voice.session[i].state.new = CALL_INACTIVE;
        }

        ret = update_calls(adev);
    }

    return ret;
}

int voice_extn_set_parameters(struct audio_device *adev,
                              struct str_parms *parms)
{
    char *str;
    int value;
    int ret = 0;

    ALOGV("%s: enter: %s", __func__, str_parms_to_str(parms));

    ret = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_VSID, &value);
    if (ret >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_VSID);
        int vsid = value;
        int call_state = -1;
        ret = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_CALL_STATE, &value);
        if (ret >= 0) {
            call_state = value;
        } else {
            ALOGE("%s: call_state key not found", __func__);
            ret = -EINVAL;
            goto done;
        }

        if (is_valid_vsid(vsid) && is_valid_call_state(call_state)) {
            ret = update_call_states(adev, vsid, call_state);
        } else {
            ALOGE("%s: invalid vsid:%x or call_state:%d",
                  __func__, vsid, call_state);
            ret = -EINVAL;
            goto done;
        }
    } else {
        ALOGD("%s: Not handled here", __func__);
    }

done:
    ALOGV("%s: exit with code(%d)", __func__, ret);
    return ret;
}

void voice_extn_get_parameters(const struct audio_device *adev,
                               struct str_parms *query,
                               struct str_parms *reply)
{
    int ret;
    char value[32]={0};
    char *str = NULL;

    ret = str_parms_get_str(query, "audio_mode", value,
                            sizeof(value));
    if (ret >= 0) {
        str_parms_add_int(reply, "audio_mode", adev->mode);
    }

    ALOGV("%s: returns %s", __func__, str_parms_to_str(reply));
}

void voice_extn_out_get_parameters(struct stream_out *out,
                                   struct str_parms *query,
                                   struct str_parms *reply)
{
    voice_extn_compress_voip_out_get_parameters(out, query, reply);
}

void voice_extn_in_get_parameters(struct stream_in *in,
                                  struct str_parms *query,
                                  struct str_parms *reply)
{
    voice_extn_compress_voip_in_get_parameters(in, query, reply);
}

int voice_extn_check_and_set_incall_music_usecase(struct audio_device *adev,
                                                  struct stream_out *out)
{
    uint32_t session_id = 0;

    session_id = get_session_id_with_state(adev, CALL_LOCAL_HOLD);
    if (session_id == VOICE_VSID) {
        out->usecase = USECASE_INCALL_MUSIC_UPLINK;
    } else if (session_id == VOICE2_VSID) {
        out->usecase = USECASE_INCALL_MUSIC_UPLINK2;
    } else {
        ALOGE("%s: Invalid session id %x", __func__, session_id);
        return -EINVAL;
    }

    out->config = pcm_config_incall_music;
    out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_MONO;
    out->channel_mask = AUDIO_CHANNEL_OUT_MONO;

    return 0;
}

