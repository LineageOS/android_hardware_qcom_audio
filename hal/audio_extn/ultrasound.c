/*
 * Copyright (c) 2017-2018, 2024 The LineageOS Project
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

#define LOG_TAG "ultrasound"

#include <errno.h>
#include <stdlib.h>
#include <log/log.h>
#include "audio_hw.h"
#include "platform_api.h"
#include <platform.h>
#include "ultrasound.h"

#define ULTRASOUND_CALIBRATION_FILE "/mnt/vendor/persist/audio/us_cal"
#define ULTRASOUND_CALIBRATION_MIXER "Ultrasound Calibration Data"
#define ULTRASOUND_RAMPDOWN_MIXER "Ultrasound RampDown"
#define ULTRASOUND_SUSPEND_MIXER "Ultrasound Suspend"
#define ULTRASOUND_RX_PORT_MIXER "Ultrasound Rx Port"
#define ULTRASOUND_PROXIMITY_PATH "ultrasound-proximity"

enum {
    ULTRASOUND_STATUS_DEFAULT,
    ULTRASOUND_STATUS_STARTED,
    ULTRASOUND_STATUS_STOPPED,
};

static struct pcm_config pcm_config_us = {
    .channels = 1,
    .rate = 96000,
    .period_size = 1024,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
};

struct ultrasound_device {
    struct pcm *rx_pcm, *tx_pcm;
    int state;
    struct audio_device *adev;
};

static struct ultrasound_device *us = NULL;

static void us_cal_load(void)
{
    FILE *f;
    char buff[5] = {0}, us_cal[64];
    struct mixer_ctl *ctl;
    int rc;

    f = fopen(ULTRASOUND_CALIBRATION_FILE, "r");
    if (!f) {
        ALOGE("%s: Cannot open calibration file: %s",
                __func__, ULTRASOUND_CALIBRATION_FILE);
        return;
    }

    for (size_t i = 0; i < sizeof(us_cal); i++) {
        fread(buff, 1, sizeof(buff), f);
        us_cal[i] = strtol(buff, 0, 16);
    }
    fclose(f);

    ctl = mixer_get_ctl_by_name(us->adev->mixer, ULTRASOUND_CALIBRATION_MIXER);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
                __func__, ULTRASOUND_CALIBRATION_MIXER);
        return;
    }

    rc = mixer_ctl_set_array(ctl, us_cal, sizeof(us_cal));
    if (rc < 0)
        ALOGE("%s: Could not set ctl, error:%d ", __func__, rc);
}

static void us_ramp_down(void)
{
    struct mixer_ctl *ctl;
    int rc;

    ALOGD("%s: enter", __func__);

    ctl = mixer_get_ctl_by_name(us->adev->mixer, ULTRASOUND_RAMPDOWN_MIXER);
    if (!ctl) {
        ALOGD("%s: Could not get ctl for mixer cmd - %s",
                __func__, ULTRASOUND_RAMPDOWN_MIXER);
        goto exit;
    }

    rc = mixer_ctl_set_enum_by_string(ctl, "On");
    if (rc < 0) {
        ALOGE("%s: Could not set ctl, error:%d ", __func__, rc);
        goto exit;
    }

    rc = mixer_ctl_set_enum_by_string(ctl, "Off");
    if (rc < 0)
        ALOGE("%s: Could not set ctl, error:%d ", __func__, rc);

    usleep(10000);

exit:
    ALOGD("%s: exit", __func__);
}

static int us_rx_port(bool on)
{
    struct mixer_ctl *ctl;

    ctl = mixer_get_ctl_by_name(us->adev->mixer, ULTRASOUND_RX_PORT_MIXER);
    if (!ctl) {
        ALOGD("%s: Could not get ctl for mixer cmd - %s",
                __func__, ULTRASOUND_RX_PORT_MIXER);
        return -EINVAL;
    }

    return mixer_ctl_set_enum_by_string(ctl, on ? "On" : "Off");
}

void us_suspend(int val)
{
    struct mixer_ctl *ctl;
    int rc;

    ALOGD("%s: enter", __func__);
    ALOGD("%s: suspend: %d", __func__, val);

    if (!us) {
        rc = -ENODEV;
        goto exit;
    }

    ctl = mixer_get_ctl_by_name(us->adev->mixer, ULTRASOUND_SUSPEND_MIXER);
    if (!ctl) {
        ALOGD("%s: Could not get ctl for mixer cmd - %s",
                __func__, ULTRASOUND_SUSPEND_MIXER);
        rc = -EINVAL;
        goto exit;
    }

    if (val)
        us_ramp_down();

    rc = mixer_ctl_set_value(ctl, 0, val);
    if (rc < 0)
        ALOGE("%s: Could not set ctl, error:%d ", __func__, rc);

exit:
    ALOGD("%s: exit", __func__);
}

void us_init(struct audio_device *adev)
{
    int rc = 0;

    ALOGD("%s: enter", __func__);

    if (us) {
        ALOGI("%s: ultrasound has been initialized!", __func__);
        goto exit;
    }

    us = calloc(1, sizeof(struct ultrasound_device));
    if (!us) {
        ALOGE("%s: Out of memory!", __func__);
        rc = -ENOMEM;
        goto exit;
    }

    us->adev = adev;

    us_cal_load();

exit:
    ALOGD("%s: exit, status(%d)", __func__, rc);
}

void us_deinit(void)
{
    ALOGD("%s: enter", __func__);

    if (us) {
        us_stop();
        free(us);
        us = NULL;
    }

    ALOGD("%s: exit", __func__);
}

static void stop_us(void)
{
    struct audio_usecase *rx_usecase, *tx_usecase;
    int rc = 0;

    ALOGD("%s: enter usecase: ultrasound", __func__);

    us->state = ULTRASOUND_STATUS_STOPPED;
    us_ramp_down();

    us_rx_port(false);
    audio_route_reset_and_update_path(us->adev->audio_route, ULTRASOUND_PROXIMITY_PATH);

    if (us->rx_pcm) {
        pcm_close(us->rx_pcm);
        us->rx_pcm = NULL;
    }

    if (us->tx_pcm) {
        pcm_close(us->tx_pcm);
        us->tx_pcm = NULL;
    }

    rx_usecase = get_usecase_from_list(us->adev, USECASE_AUDIO_ULTRASOUND_RX);
    if (!rx_usecase) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
                __func__, USECASE_AUDIO_ULTRASOUND_RX);
        rc = -EINVAL;
    } else {
        disable_audio_route(us->adev, rx_usecase);
        disable_snd_device(us->adev, rx_usecase->out_snd_device);
        list_remove(&rx_usecase->list);
        free(rx_usecase);
    }

    tx_usecase = get_usecase_from_list(us->adev, USECASE_AUDIO_ULTRASOUND_TX);
    if (!tx_usecase) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
                __func__, USECASE_AUDIO_ULTRASOUND_TX);
        rc = -EINVAL;
    } else {
        disable_audio_route(us->adev, tx_usecase);
        disable_snd_device(us->adev, tx_usecase->in_snd_device);
        list_remove(&tx_usecase->list);
        free(tx_usecase);
    }

    us_suspend(0);

    ALOGD("%s: exit: status(%d)", __func__, rc);
}

static snd_device_t us_get_output_snd_device(void)
{
    snd_device_t snd_device = SND_DEVICE_OUT_VOICE_HANDSET;
    struct audio_usecase *usecase;
    struct listnode *node;

    ALOGD("%s: enter", __func__);

    if (!us) {
        ALOGE("%s: us is not ready, fallback to handset!", __func__);
        goto exit;
    }

    list_for_each(node, &us->adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_CAPTURE || usecase->out_snd_device == SND_DEVICE_NONE)
            continue;
        if (platform_check_backends_match(SND_DEVICE_OUT_VOICE_HANDSET,
                                          usecase->out_snd_device)) {
            snd_device = usecase->out_snd_device;
            if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES)
                snd_device = SND_DEVICE_OUT_HEADPHONES;
        }
    }

exit:
    ALOGD("%s: %s", __func__, platform_get_snd_device_name(snd_device));
    return snd_device;
}

void us_start(void)
{
    int rc = 0, rx_device_id, tx_device_id;
    struct audio_usecase *rx_usecase, *tx_usecase;

    ALOGD("%s: enter", __func__);

    if (!us || us->state == ULTRASOUND_STATUS_STARTED) {
        rc = -EPERM;
        goto exit;
    }

    ALOGD("%s: enter usecase: ultrasound", __func__);
    rx_device_id = platform_get_pcm_device_id(USECASE_AUDIO_ULTRASOUND_RX, PCM_PLAYBACK);
    tx_device_id = platform_get_pcm_device_id(USECASE_AUDIO_ULTRASOUND_TX, PCM_CAPTURE);
    if (rx_device_id < 0 || tx_device_id < 0) {
        ALOGE("%s: Invalid PCM devices (rx: %d tx: %d) for the usecase(ultrasound)",
                __func__, rx_device_id, tx_device_id);
        rc = -EINVAL;
        goto exit;
    }

    if (audio_route_apply_and_update_path(us->adev->audio_route, ULTRASOUND_PROXIMITY_PATH)) {
        ALOGE("%s: Could not apply %s", __func__, ULTRASOUND_PROXIMITY_PATH);
        rc = -EINVAL;
        goto exit;
    }

    if (us_rx_port(true) < 0) {
        ALOGE("%s: Could not enable %s", __func__, ULTRASOUND_RX_PORT_MIXER);
        rc = -EINVAL;
        goto exit;
    }

    us_suspend(1);

    rx_usecase = calloc(1, sizeof(struct audio_usecase));
    if (!rx_usecase) {
        ALOGE("%s: Out of memory!", __func__);
        rc = -ENOMEM;
        goto exit;
    }

    rx_usecase->type = PCM_PLAYBACK;
    rx_usecase->out_snd_device = us_get_output_snd_device();
    rx_usecase->id = USECASE_AUDIO_ULTRASOUND_RX;
    rx_usecase->stream.out = us->adev->primary_output;
    list_init(&rx_usecase->device_list);
    assign_devices(&rx_usecase->device_list, &us->adev->primary_output->device_list);
    list_add_tail(&us->adev->usecase_list, &rx_usecase->list);

    enable_snd_device(us->adev, rx_usecase->out_snd_device);
    enable_audio_route(us->adev, rx_usecase);
    ALOGV("%s: Opening PCM playback device card_id(%d) device_id(%d)",
            __func__, us->adev->snd_card, rx_device_id);
    us->rx_pcm = pcm_open(us->adev->snd_card, rx_device_id, PCM_OUT, &pcm_config_us);
    if (us->rx_pcm && !pcm_is_ready(us->rx_pcm)) {
        ALOGE("%s: %s", __func__, pcm_get_error(us->rx_pcm));
        rc = -EIO;
        goto exit;
    }

    tx_usecase = calloc(1, sizeof(struct audio_usecase));
    if (!tx_usecase) {
        ALOGE("%s: Out of memory!", __func__);
        rc = -ENOMEM;
        goto exit;
    }

    tx_usecase->type = PCM_CAPTURE;
    tx_usecase->in_snd_device = SND_DEVICE_IN_ULTRASOUND;
    tx_usecase->id = USECASE_AUDIO_ULTRASOUND_TX;
    list_init(&tx_usecase->device_list);
    reassign_device_list(&tx_usecase->device_list, AUDIO_DEVICE_IN_BUILTIN_MIC, "");
    list_add_tail(&us->adev->usecase_list, &tx_usecase->list);

    enable_snd_device(us->adev, SND_DEVICE_IN_ULTRASOUND);
    enable_audio_route(us->adev, tx_usecase);
    ALOGV("%s: Opening PCM capture device card_id(%d) device_id(%d)",
            __func__, us->adev->snd_card, tx_device_id);
    us->tx_pcm = pcm_open(us->adev->snd_card, tx_device_id, PCM_IN, &pcm_config_us);
    if (us->tx_pcm && !pcm_is_ready(us->tx_pcm)) {
        ALOGE("%s: %s", __func__, pcm_get_error(us->tx_pcm));
        rc = -EIO;
        goto exit;
    }

    if (pcm_start(us->tx_pcm) < 0) {
        ALOGE("%s: pcm start for TX failed; error = %s", __func__,
              pcm_get_error(us->tx_pcm));
        rc = -EINVAL;
        goto exit;
    }

    if (pcm_start(us->rx_pcm) < 0) {
        ALOGE("%s: pcm start for RX failed; error = %s", __func__,
              pcm_get_error(us->rx_pcm));
        rc = -EINVAL;
        goto exit;
    }

    us_suspend(0);

    us->state = ULTRASOUND_STATUS_STARTED;

exit:
    ALOGD("%s: exit, status(%d)", __func__, rc);
    if (rc && rc != -EPERM)
	   stop_us();
}

void us_stop(void)
{
    int rc = 0;

    ALOGD("%s: enter", __func__);

    if (!us || us->state != ULTRASOUND_STATUS_STARTED) {
        rc = -EPERM;
        goto exit;
    }

    stop_us();

exit:
    ALOGD("%s: exit, status(%d)", __func__, rc);
}
