/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define LOG_TAG "audio_hw_utils"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <cutils/properties.h>
#include <cutils/config_utils.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <cutils/str_parms.h>
#include <cutils/log.h>
#include <cutils/misc.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include "audio_extn.h"

#define MAX_LENGTH_MIXER_CONTROL_IN_INT 128

static int set_mixer_ctrl(struct audio_device *adev,
                          int pcm_device_id, int app_type,
                          int acdb_dev_id, int sample_rate, int stream_type)
{

    char mixer_ctl_name[MAX_LENGTH_MIXER_CONTROL_IN_INT];
    struct mixer_ctl *ctl;
    int app_type_cfg[MAX_LENGTH_MIXER_CONTROL_IN_INT], len = 0, rc = 0;

    if (stream_type == PCM_PLAYBACK) {
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Audio Stream %d App Type Cfg", pcm_device_id);
    } else if (stream_type == PCM_CAPTURE) {
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Audio Stream Capture %d App Type Cfg", pcm_device_id);
    }

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
             __func__, mixer_ctl_name);
        rc = -EINVAL;
        goto exit;
    }
    app_type_cfg[len++] = app_type;
    app_type_cfg[len++] = acdb_dev_id;
    app_type_cfg[len++] = sample_rate;
    ALOGV("%s: stream type %d app_type %d, acdb_dev_id %d sample rate %d",
          __func__, stream_type, app_type, acdb_dev_id, sample_rate);
    mixer_ctl_set_array(ctl, app_type_cfg, len);

exit:
    return rc;
}

void audio_extn_utils_send_default_app_type_cfg(void *platform, struct mixer *mixer)
{
    int app_type_cfg[MAX_LENGTH_MIXER_CONTROL_IN_INT] = {-1};
    int length = 0, app_type = 0,rc = 0;
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "App Type Config";

    ctl = mixer_get_ctl_by_name(mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",__func__, mixer_ctl_name);
        return;
    }
    rc = platform_get_default_app_type_v2(platform, PCM_PLAYBACK, &app_type);
    if (rc == 0) {
        app_type_cfg[length++] = 1;
        app_type_cfg[length++] = app_type;
        app_type_cfg[length++] = 48000;
        app_type_cfg[length++] = 16;
        mixer_ctl_set_array(ctl, app_type_cfg, length);
    }
    return;
}

int audio_extn_utils_send_app_type_cfg(struct audio_device *adev,
                                       struct audio_usecase *usecase)
{
    struct mixer_ctl *ctl;
    int pcm_device_id, acdb_dev_id = 0, snd_device = usecase->out_snd_device;
    int32_t sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    int app_type = 0, rc = 0;

    ALOGV("%s", __func__);

    if (usecase->type != PCM_HFP_CALL) {
        ALOGV("%s: not a playback or HFP path, no need to cfg app type", __func__);
        rc = 0;
        goto exit_send_app_type_cfg;
    }
    if ((usecase->id != USECASE_AUDIO_HFP_SCO) &&
        (usecase->id != USECASE_AUDIO_HFP_SCO_WB)) {
        ALOGV("%s: a playback path where app type cfg is not required", __func__);
        rc = 0;
        goto exit_send_app_type_cfg;
    }

    snd_device = usecase->out_snd_device;
    pcm_device_id = platform_get_pcm_device_id(usecase->id, PCM_PLAYBACK);

    snd_device = (snd_device == SND_DEVICE_OUT_SPEAKER) ?
                 audio_extn_get_spkr_prot_snd_device(snd_device) : snd_device;
    acdb_dev_id = platform_get_snd_device_acdb_id(snd_device);
    if (acdb_dev_id < 0) {
        ALOGE("%s: Couldn't get the acdb dev id", __func__);
        rc = -EINVAL;
        goto exit_send_app_type_cfg;
    }

    if (usecase->type == PCM_HFP_CALL) {

        /* config HFP session:1 playback path */
        rc = platform_get_default_app_type_v2(adev->platform, PCM_PLAYBACK, &app_type);
        if (rc < 0)
            goto exit_send_app_type_cfg;

        sample_rate= CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
        rc = set_mixer_ctrl(adev, pcm_device_id, app_type,
               acdb_dev_id, sample_rate, PCM_PLAYBACK);
        if (rc < 0)
            goto exit_send_app_type_cfg;
        /* config HFP session:1 capture path */
        rc = platform_get_default_app_type_v2(adev->platform, PCM_CAPTURE, &app_type);

        if (rc == 0) {
            rc = set_mixer_ctrl(adev, pcm_device_id, app_type,
                   acdb_dev_id, sample_rate, PCM_CAPTURE);
            if (rc < 0)
                goto exit_send_app_type_cfg;
        }
        /* config HFP session:2 capture path */
        pcm_device_id = HFP_ASM_RX_TX;
        snd_device = usecase->in_snd_device;
        acdb_dev_id = platform_get_snd_device_acdb_id(snd_device);
        if (acdb_dev_id <= 0) {
            ALOGE("%s: Couldn't get the acdb dev id", __func__);
            rc = -EINVAL;
            goto exit_send_app_type_cfg;
        }
        rc = platform_get_default_app_type_v2(adev->platform, PCM_CAPTURE, &app_type);
        if (rc == 0) {
            rc = set_mixer_ctrl(adev, pcm_device_id, app_type,
                   acdb_dev_id, sample_rate, PCM_CAPTURE);
            if (rc < 0)
                goto exit_send_app_type_cfg;
        }

        /* config HFP session:2 playback path */
        rc = platform_get_default_app_type_v2(adev->platform, PCM_PLAYBACK, &app_type);
        if (rc == 0) {
            rc = set_mixer_ctrl(adev, pcm_device_id, app_type,
                   acdb_dev_id, sample_rate, PCM_PLAYBACK);
            if (rc < 0)
                goto exit_send_app_type_cfg;
        }
    }

    rc = 0;
exit_send_app_type_cfg:
    return rc;
}
