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

#include "acdb.h"
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

static int audio_extn_utils_send_app_type_cfg_hfp(struct audio_device *adev,
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

int audio_extn_utils_send_app_type_cfg(struct audio_device *adev,
                                       struct audio_usecase *usecase)
{
    int len = 0;
    if (usecase->type == PCM_HFP_CALL) {
        return audio_extn_utils_send_app_type_cfg_hfp(adev, usecase);
    }

    if (usecase->type != PCM_PLAYBACK || !platform_supports_app_type_cfg())
        return -1;

    size_t app_type_cfg[MAX_LENGTH_MIXER_CONTROL_IN_INT] = {0};
    int pcm_device_id = platform_get_pcm_device_id(usecase->id, PCM_PLAYBACK);

    char mixer_ctl_name[MAX_LENGTH_MIXER_CONTROL_IN_INT] = {0};
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Audio Stream %d App Type Cfg", pcm_device_id);
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s", __func__,
              mixer_ctl_name);
        return -EINVAL;
    }

    snd_device_t snd_device = usecase->out_snd_device; // add speaker prot changes if needed
    int acdb_dev_id = platform_get_snd_device_acdb_id(snd_device);
    if (acdb_dev_id <= 0) {
        ALOGE("%s: Couldn't get the acdb dev id", __func__);
        return -1;
    }

    if (usecase->stream.out->devices & AUDIO_DEVICE_OUT_SPEAKER) {
        usecase->stream.out->app_type_cfg.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    } else if (snd_device == SND_DEVICE_OUT_USB_HEADSET ||
               snd_device == SND_DEVICE_OUT_USB_HEADPHONES) {
        platform_check_and_update_copp_sample_rate(adev->platform, snd_device,
                                                   usecase->stream.out->sample_rate,
                                                   &usecase->stream.out->app_type_cfg.sample_rate);
    }

    int32_t sample_rate = usecase->stream.out->app_type_cfg.sample_rate;
    int app_type;
    if (!audio_is_linear_pcm(usecase->stream.out->format)) {
        platform_get_default_app_type_v2(adev->platform,
                                         PCM_PLAYBACK,
                                         &app_type);
    } else if (usecase->stream.out->format == AUDIO_FORMAT_PCM_16_BIT) {
        platform_get_app_type_v2(adev->platform,
                                 16,
                                 sample_rate,
                                 PCM_PLAYBACK,
                                 &app_type);
    } else if (usecase->stream.out->format == AUDIO_FORMAT_PCM_24_BIT_PACKED ||
               usecase->stream.out->format == AUDIO_FORMAT_PCM_8_24_BIT) {
        platform_get_app_type_v2(adev->platform,
                                 24,
                                 sample_rate,
                                 PCM_PLAYBACK,
                                 &app_type);
    } else if (usecase->stream.out->format == AUDIO_FORMAT_PCM_32_BIT) {
        platform_get_app_type_v2(adev->platform,
                                 32,
                                 sample_rate,
                                 PCM_PLAYBACK,
                                 &app_type);
    } else {
        ALOGE("%s bad format\n", __func__);
        return -1;
    }

    //XXX this would be set somewhere else
    usecase->stream.out->app_type_cfg.app_type = app_type;
    app_type_cfg[len++] = app_type;
    app_type_cfg[len++] = acdb_dev_id;
    app_type_cfg[len++] = sample_rate;

    // add be_idx once available
    // if (snd_device_be_idx > 0)
    //    app_type_cfg[len++] = snd_device_be_idx;

    ALOGI("%s PLAYBACK app_type %d, acdb_dev_id %d, sample_rate %d",
          __func__, app_type, acdb_dev_id, sample_rate);

    mixer_ctl_set_array(ctl, app_type_cfg, len);
    return 0;
}

void audio_extn_utils_send_audio_calibration(struct audio_device *adev,
                                             struct audio_usecase *usecase)
{
    int type = usecase->type;
    int app_type = 0;

    if (type == PCM_PLAYBACK && usecase->stream.out != NULL) {
        struct stream_out *out = usecase->stream.out;
        ALOGV("%s send cal for app_type %d, rate %d", __func__, out->app_type_cfg.app_type,
              usecase->stream.out->app_type_cfg.sample_rate);
        platform_send_audio_calibration_v2(adev->platform, usecase,
                                        out->app_type_cfg.app_type,
                                        usecase->stream.out->app_type_cfg.sample_rate);
    } else if (type == PCM_CAPTURE && usecase->stream.in != NULL) {
        // TBD
        // platform_send_audio_calibration_v2(adev->platform, usecase,
        // usecase->stream.in->app_type_cfg.app_type,
        // usecase->stream.in->app_type_cfg.sample_rate);
        // uncomment these once send_app_type_cfg and the config entries for
        // non-16 bit capture are figured out.
        platform_get_default_app_type_v2(adev->platform, type, &app_type);
        platform_send_audio_calibration_v2(adev->platform, usecase, app_type, 48000);
    } else {
        /* when app type is default. the sample rate is not used to send cal */
        platform_get_default_app_type_v2(adev->platform, type, &app_type);
        platform_send_audio_calibration_v2(adev->platform, usecase, app_type, 48000);
    }
}

#define MAX_SND_CARD 8
#define RETRY_US 500000
#define RETRY_NUMBER 10

#define min(a, b) ((a) < (b) ? (a) : (b))

static const char *kConfigLocationList[] =
        {"/odm/etc", "/vendor/etc", "/system/etc"};
static const int kConfigLocationListSize =
        (sizeof(kConfigLocationList) / sizeof(kConfigLocationList[0]));

bool audio_extn_utils_resolve_config_file(char file_name[MIXER_PATH_MAX_LENGTH])
{
    char full_config_path[MIXER_PATH_MAX_LENGTH];
    for (int i = 0; i < kConfigLocationListSize; i++) {
        snprintf(full_config_path,
                 MIXER_PATH_MAX_LENGTH,
                 "%s/%s",
                 kConfigLocationList[i],
                 file_name);
        if (F_OK == access(full_config_path, 0)) {
            strcpy(file_name, full_config_path);
            return true;
        }
    }
    return false;
}

/* platform_info_file should be size 'MIXER_PATH_MAX_LENGTH' */
void audio_extn_utils_get_platform_info(const char* snd_card_name, char* platform_info_file)
{
    if (NULL == snd_card_name) {
        return;
    }

    struct snd_card_split *snd_split_handle = NULL;

    audio_extn_set_snd_card_split(snd_card_name);
    snd_split_handle = audio_extn_get_snd_card_split();

    snprintf(platform_info_file, MIXER_PATH_MAX_LENGTH, "%s_%s_%s.xml",
                     PLATFORM_INFO_XML_BASE_STRING, snd_split_handle->snd_card,
                     snd_split_handle->form_factor);

    if (!audio_extn_utils_resolve_config_file(platform_info_file)) {
        memset(platform_info_file, 0, MIXER_PATH_MAX_LENGTH);
        snprintf(platform_info_file, MIXER_PATH_MAX_LENGTH, "%s_%s.xml",
                     PLATFORM_INFO_XML_BASE_STRING, snd_split_handle->snd_card);

        if (!audio_extn_utils_resolve_config_file(platform_info_file)) {
            memset(platform_info_file, 0, MIXER_PATH_MAX_LENGTH);
            strlcpy(platform_info_file, PLATFORM_INFO_XML_PATH, MIXER_PATH_MAX_LENGTH);
            audio_extn_utils_resolve_config_file(platform_info_file);
        }
    }
}

int audio_extn_utils_get_snd_card_num()
{

    void *hw_info = NULL;
    struct mixer *mixer = NULL;
    int retry_num = 0;
    int snd_card_num = 0;
    const char* snd_card_name = NULL;
    char platform_info_file[MIXER_PATH_MAX_LENGTH]= {0};

    struct acdb_platform_data *my_data = calloc(1, sizeof(struct acdb_platform_data));

    bool card_verifed[MAX_SND_CARD] = {0};
    const int retry_limit = property_get_int32("audio.snd_card.open.retries", RETRY_NUMBER);

    for (;;) {
        if (snd_card_num >= MAX_SND_CARD) {
            if (retry_num++ >= retry_limit) {
                ALOGE("%s: Unable to find correct sound card, aborting.", __func__);
                snd_card_num = -1;
                goto done;
            }

            snd_card_num = 0;
            usleep(RETRY_US);
            continue;
        }

        if (card_verifed[snd_card_num]) {
            ++snd_card_num;
            continue;
        }

        mixer = mixer_open(snd_card_num);

        if (!mixer) {
            ALOGE("%s: Unable to open the mixer card: %d", __func__,
               snd_card_num);
            ++snd_card_num;
            continue;
        }

        card_verifed[snd_card_num] = true;

        snd_card_name = mixer_get_name(mixer);
        hw_info = hw_info_init(snd_card_name);

        audio_extn_utils_get_platform_info(snd_card_name, platform_info_file);

        /* Initialize snd card name specific ids and/or backends*/
        snd_card_info_init(platform_info_file, my_data, &acdb_set_parameters);

        /* validate the sound card name
         * my_data->snd_card_name can contain
         *     <a> complete sound card name, i.e. <device>-<codec>-<form_factor>-snd-card
         *         example: msm8994-tomtom-mtp-snd-card
         *     <b> or sub string of the card name, i.e. <device>-<codec>
         *         example: msm8994-tomtom
         * snd_card_name is truncated to 32 charaters as per mixer_get_name() implementation
         * so use min of my_data->snd_card_name and snd_card_name length for comparison
         */

        if (my_data->snd_card_name != NULL &&
                strncmp(snd_card_name, my_data->snd_card_name,
                        min(strlen(snd_card_name), strlen(my_data->snd_card_name))) != 0) {
            ALOGI("%s: found valid sound card %s, but not primary sound card %s",
                   __func__, snd_card_name, my_data->snd_card_name);
            ++snd_card_num;
            mixer_close(mixer);
            mixer = NULL;
            hw_info_deinit(hw_info);
            hw_info = NULL;
            continue;
        }

        ALOGI("%s: found sound card %s, primary sound card expeted is %s",
              __func__, snd_card_name, my_data->snd_card_name);
        break;
    }

done:
    mixer_close(mixer);
    hw_info_deinit(hw_info);

    if (my_data)
        free(my_data);

    return snd_card_num;
}
