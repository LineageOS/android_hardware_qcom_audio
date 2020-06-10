/*
 * Copyright (C) 2020 The LineageOS Project
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

#define LOG_TAG "audio_hw_tfa98xx_feedback"

#include <errno.h>
#include <log/log.h>
#include <stdlib.h>

/* clang-format off */
#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include "tfa98xx_feedback.h"
/* clang-format on */

struct pcm* tfa98xx_out;

struct pcm_config pcm_config_tfa98xx = {
        .channels = 2,
        .rate = 48000,
        .period_size = 256,
        .period_count = 4,
        .format = PCM_FORMAT_S16_LE,
        .start_threshold = 0,
        .stop_threshold = INT_MAX,
        .silence_threshold = 0,
};

static int is_speaker(snd_device_t snd_device) {
    int speaker = 0;
    switch (snd_device) {
        case SND_DEVICE_OUT_SPEAKER:
        case SND_DEVICE_OUT_SPEAKER_REVERSE:
        case SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES:
        case SND_DEVICE_OUT_VOICE_SPEAKER:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2:
        case SND_DEVICE_OUT_SPEAKER_AND_HDMI:
        case SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET:
        case SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET:
            speaker = 1;
            break;
    }

    return speaker;
}

int audio_extn_tfa98xx_start_feedback(struct audio_device* adev, snd_device_t snd_device) {
    struct audio_usecase* usecase_tx = NULL;
    int pcm_dev_tx_id, rc;

    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return -EINVAL;
    }

    if (tfa98xx_out || !is_speaker(snd_device)) return 0;

    usecase_tx = (struct audio_usecase*)calloc(1, sizeof(struct audio_usecase));
    if (!usecase_tx) return -ENOMEM;

    usecase_tx->id = USECASE_AUDIO_SPKR_CALIB_TX;
    usecase_tx->type = PCM_CAPTURE;
    usecase_tx->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;

    list_add_tail(&adev->usecase_list, &usecase_tx->list);
    enable_snd_device(adev, usecase_tx->in_snd_device);
    enable_audio_route(adev, usecase_tx);

    pcm_dev_tx_id = platform_get_pcm_device_id(usecase_tx->id, usecase_tx->type);
    ALOGD("pcm_dev_tx_id = %d", pcm_dev_tx_id);
    if (pcm_dev_tx_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)", __func__, usecase_tx->id);
        rc = -ENODEV;
        goto error;
    }

    tfa98xx_out = pcm_open(adev->snd_card, pcm_dev_tx_id, PCM_IN, &pcm_config_tfa98xx);
    if (!(tfa98xx_out || pcm_is_ready(tfa98xx_out))) {
        ALOGE("%s: %s", __func__, pcm_get_error(tfa98xx_out));
        rc = -EIO;
        goto error;
    }

    rc = pcm_start(tfa98xx_out);
    if (rc < 0) {
        ALOGE("%s: pcm start for TX failed", __func__);
        rc = -EINVAL;
        goto error;
    }
    return 0;

error:
    ALOGE("%s: error case", __func__);
    if (tfa98xx_out != 0) {
        pcm_close(tfa98xx_out);
        tfa98xx_out = NULL;
    }
    disable_snd_device(adev, usecase_tx->in_snd_device);
    list_remove(&usecase_tx->list);
    disable_audio_route(adev, usecase_tx);
    free(usecase_tx);

    return rc;
}

void audio_extn_tfa98xx_stop_feedback(struct audio_device* adev, snd_device_t snd_device) {
    struct audio_usecase* usecase;

    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return;
    }

    if (!is_speaker(snd_device)) return;

    if (tfa98xx_out) {
        pcm_close(tfa98xx_out);
        tfa98xx_out = NULL;
    }

    disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);

    usecase = get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);
    if (usecase) {
        list_remove(&usecase->list);
        disable_audio_route(adev, usecase);
        free(usecase);
    }
    return;
}
