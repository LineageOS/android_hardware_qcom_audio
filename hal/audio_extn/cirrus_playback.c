/*
 * Copyright (C) 2017 The Android Open Source Project
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

#define LOG_TAG "audio_hw_cirrus_playback"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <math.h>
#include <cutils/log.h>
#include <fcntl.h>
#include "../audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <cutils/properties.h>
#include "audio_extn.h"

struct cirrus_playback_session {
    void *adev_handle;
    pthread_mutex_t mutex_fb_prot;
    pthread_mutex_t mutex_pcm_stream;
    pthread_t pcm_stream_thread;
    struct pcm *pcm_rx;
    struct pcm *pcm_tx;
    bool spkr_prot_enable;
};

struct crus_sp_ioctl_header {
    uint32_t size;
    uint32_t module_id;
    uint32_t param_id;
    uint32_t data_length;
    void *data;
};

/* Payload struct for getting calibration result from DSP module */
struct cirrus_cal_result_t {
    int32_t status_l;
    int32_t checksum_l;
    int32_t z_l;
    int32_t status_r;
    int32_t checksum_r;
    int32_t z_r;
    int32_t atemp;
};

/* Payload struct for setting the RX and TX use cases */
struct crus_rx_run_case_ctrl_t {
    int32_t value;
    int32_t status_l;
    int32_t checksum_l;
    int32_t z_l;
    int32_t status_r;
    int32_t checksum_r;
    int32_t z_r;
    int32_t atemp;
};

#define CRUS_SP_FILE "/dev/msm_cirrus_playback"
#define CRUS_CAL_FILE "/persist/audio/audio.cal"
#define CRUS_SP_USECASE_MIXER "Cirrus SP Usecase Config"
#define CRUS_SP_EXT_CONFIG_MIXER "Cirrus SP EXT Config"

#define CIRRUS_SP 0x10027053

#define CRUS_MODULE_ID_TX 0x00000002
#define CRUS_MODULE_ID_RX 0x00000001

#define CRUS_PARAM_RX_SET_USECASE 0x00A1AF02
#define CRUS_PARAM_TX_SET_USECASE 0x00A1BF0A

#define CRUS_PARAM_RX_SET_CALIB 0x00A1AF03
#define CRUS_PARAM_TX_SET_CALIB 0x00A1BF03

#define CRUS_PARAM_RX_SET_EXT_CONFIG 0x00A1AF05
#define CRUS_PARAM_TX_SET_EXT_CONFIG 0x00A1BF08

#define CRUS_PARAM_RX_GET_TEMP 0x00A1AF07
#define CRUS_PARAM_TX_GET_TEMP_CAL 0x00A1BF06
// variables based on CSPL tuning file, max parameter length is 96 integers (384 bytes)
#define CRUS_PARAM_TEMP_MAX_LENGTH 384

#define CRUS_AFE_PARAM_ID_ENABLE 0x00010203

#define CRUS_SP_IOCTL_MAGIC 'a'

#define CRUS_SP_IOCTL_GET _IOWR(CRUS_SP_IOCTL_MAGIC, 219, void *)
#define CRUS_SP_IOCTL_SET _IOWR(CRUS_SP_IOCTL_MAGIC, 220, void *)
#define CRUS_SP_IOCTL_GET_CALIB _IOWR(CRUS_SP_IOCTL_MAGIC, 221, void *)
#define CRUS_SP_IOCTL_SET_CALIB _IOWR(CRUS_SP_IOCTL_MAGIC, 222, void *)

#define CRUS_SP_DEFAULT_AMBIENT_TEMP 23

static struct pcm_config pcm_config_cirrus_tx = {
    .channels = 2,
    .rate = 48000,
    .period_size = 320,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

static struct pcm_config pcm_config_cirrus_rx = {
    .channels = 2,
    .rate = 48000,
    .period_size = 320,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

static struct cirrus_playback_session handle;

static void *audio_extn_cirrus_run_calibration() {
    struct audio_device *adev = handle.adev_handle;
    struct crus_sp_ioctl_header header;
    struct cirrus_cal_result_t result;
    struct mixer_ctl *ctl;
    FILE *cal_file;
    int ret = 0, dev_file;
    char *buffer = NULL;
    uint32_t option = 1;
    struct timespec timeout;

    ALOGI("%s: Calibration thread", __func__);

    timeout.tv_sec = 10;
    timeout.tv_nsec = 0;

    dev_file = open(CRUS_SP_FILE, O_RDWR | O_NONBLOCK);
    if (dev_file < 0) {
        ALOGE("%s: Failed to open Cirrus Playback IOCTL (%d)",
              __func__, dev_file);
        ret = -EINVAL;
        goto exit;
    }

    buffer = calloc(1, CRUS_PARAM_TEMP_MAX_LENGTH);
    if (!buffer) {
        ret = -EINVAL;
        goto exit;
    }

    cal_file = fopen(CRUS_CAL_FILE, "r");
    if (cal_file) {
        size_t bytes;
        bytes = fread(&result, 1, sizeof(result), cal_file);
        if (bytes < sizeof(result)) {
            ALOGE("%s: Cirrus SP calibration file cannot be read (%d)",
                  __func__, ret);
            ret = -EINVAL;
            fclose(cal_file);
            goto exit;
        }

        fclose(cal_file);
    } else {

        ALOGV("%s: Calibrating...", __func__);

        header.size = sizeof(header);
        header.module_id = CRUS_MODULE_ID_RX;
        header.param_id = CRUS_PARAM_RX_SET_CALIB;
        header.data_length = sizeof(option);
        header.data = &option;

        ret = ioctl(dev_file, CRUS_SP_IOCTL_SET, &header);
        if (ret < 0) {
            ALOGE("%s: Cirrus SP calibration IOCTL failure (%d)",
                  __func__, ret);
            ret = -EINVAL;
            goto exit;
        }

        header.size = sizeof(header);
        header.module_id = CRUS_MODULE_ID_TX;
        header.param_id = CRUS_PARAM_TX_SET_CALIB;
        header.data_length = sizeof(option);
        header.data = &option;

        ret = ioctl(dev_file, CRUS_SP_IOCTL_SET, &header);
        if (ret < 0) {
            ALOGE("%s: Cirrus SP calibration IOCTL failure (%d)",
                  __func__, ret);
            ret = -EINVAL;
            goto exit;
        }

        sleep(2);

        header.size = sizeof(header);
        header.module_id = CRUS_MODULE_ID_TX;
        header.param_id = CRUS_PARAM_TX_GET_TEMP_CAL;
        header.data_length = sizeof(result);
        header.data = &result;

        ret = ioctl(dev_file, CRUS_SP_IOCTL_GET, &header);
        if (ret < 0) {
            ALOGE("%s: Cirrus SP calibration IOCTL failure (%d)",
                  __func__, ret);
            ret = -EINVAL;
            goto exit;
        }

        result.atemp = CRUS_SP_DEFAULT_AMBIENT_TEMP;

        // TODO: Save calibrated data to file
    }

    header.size = sizeof(header);
    header.module_id = CRUS_MODULE_ID_TX;
    header.param_id = 0;
    header.data_length = sizeof(result);
    header.data = &result;

    ret = ioctl(dev_file, CRUS_SP_IOCTL_SET_CALIB, &header);

    if (ret < 0) {
        ALOGE("%s: Cirrus SP calibration IOCTL failure (%d)", __func__, ret);
        close(dev_file);
        ret = -EINVAL;
        goto exit;
    }

    ctl = mixer_get_ctl_by_name(adev->mixer,
                                CRUS_SP_USECASE_MIXER);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, CRUS_SP_USECASE_MIXER);
        ret = -EINVAL;
        goto exit;
    }

    mixer_ctl_set_value(ctl, 0, 0); // Set RX external firmware config

    sleep(1);

    header.size = sizeof(header);
    header.module_id = CRUS_MODULE_ID_RX;
    header.param_id = CRUS_PARAM_RX_GET_TEMP;
    header.data_length = sizeof(buffer);
    header.data = buffer;

    ret = ioctl(dev_file, CRUS_SP_IOCTL_GET, &header);
    if (ret < 0) {
        ALOGE("%s: Cirrus SP temperature IOCTL failure (%d)", __func__, ret);
        ret = -EINVAL;
        goto exit;
    }

    ALOGI("%s: Cirrus SP successfully calibrated", __func__);

exit:
    close(dev_file);
    free(buffer);
    ALOGV("%s: Exit", __func__);

    return NULL;
}


static void *audio_extn_cirrus_pcm_stream_thread() {
    struct audio_device *adev = handle.adev_handle;
    struct audio_usecase *uc_info_rx = NULL;
    int ret = 0;
    int32_t pcm_dev_rx_id = 0;
    uint32_t rx_use_case = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
    uint32_t retries = 5;

    pthread_mutex_lock(&handle.mutex_pcm_stream);

    ALOGI("%s: PCM Stream thread", __func__);

    while (!adev->platform && retries) {
        sleep(1);
        ALOGI("%s: Waiting...", __func__);
        retries--;
    }

    uc_info_rx = (struct audio_usecase *)calloc(1, sizeof(*uc_info_rx));
    if (!uc_info_rx) {
        ret = -EINVAL;
        goto exit;
    }

    uc_info_rx->id = rx_use_case;
    uc_info_rx->type = PCM_PLAYBACK;
    uc_info_rx->in_snd_device = SND_DEVICE_NONE;
    uc_info_rx->stream.out = adev->primary_output;
    uc_info_rx->out_snd_device = SND_DEVICE_OUT_SPEAKER;
    list_add_tail(&adev->usecase_list, &uc_info_rx->list);

    enable_snd_device(adev, SND_DEVICE_OUT_SPEAKER);
    enable_audio_route(adev, uc_info_rx);
    handle.pcm_rx = pcm_open(adev->snd_card, pcm_dev_rx_id,
                             PCM_OUT, &pcm_config_cirrus_rx);

    if (!handle.pcm_rx || !pcm_is_ready(handle.pcm_rx)) {
        ALOGE("%s: PCM device not ready: %s", __func__,
              pcm_get_error(handle.pcm_rx ? handle.pcm_rx : 0));
        ret = -EINVAL;
        goto close_stream;
    }

    if (pcm_start(handle.pcm_rx) < 0) {
        ALOGE("%s: pcm start for RX failed; error = %s", __func__,
              pcm_get_error(handle.pcm_rx));
        ret = -EINVAL;
        goto close_stream;
    }

    ALOGV("%s: PCM thread streaming", __func__);

    audio_extn_cirrus_run_calibration();

close_stream:
    if (handle.pcm_rx) {
        ALOGV("%s: pcm_rx_close", __func__);
        pcm_close(handle.pcm_rx);
        handle.pcm_rx = NULL;
    }

    disable_audio_route(adev, uc_info_rx);
    disable_snd_device(adev, SND_DEVICE_OUT_SPEAKER);
    list_remove(&uc_info_rx->list);
    free(uc_info_rx);

exit:
    pthread_mutex_unlock(&handle.mutex_pcm_stream);
    ALOGV("%s: Exit", __func__);

    pthread_exit(0);
    return NULL;
}

void audio_extn_spkr_prot_init(void *adev) {
    ALOGI("%s: Initialize Cirrus Logic Playback module", __func__);

    struct snd_card_split *snd_split_handle = NULL;

    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return;
    }

    memset(&handle, 0, sizeof(handle));

    snd_split_handle = audio_extn_get_snd_card_split();

    /* FIXME: REMOVE THIS AFTER B1C1 P1.0 SUPPORT */
    if (!strcmp(snd_split_handle->form_factor, "tdm")) {
        handle.spkr_prot_enable = true;
    } else {
        handle.spkr_prot_enable = false;
    }

    handle.adev_handle = adev;

    pthread_mutex_init(&handle.mutex_fb_prot, NULL);
    pthread_mutex_init(&handle.mutex_pcm_stream, NULL);

    (void)pthread_create(&handle.pcm_stream_thread,
                         (const pthread_attr_t *) NULL,
                         audio_extn_cirrus_pcm_stream_thread, &handle);
}

int audio_extn_spkr_prot_start_processing(snd_device_t snd_device) {
    struct audio_usecase *uc_info_tx;
    struct audio_device *adev = handle.adev_handle;
    int32_t pcm_dev_tx_id = -1, ret = 0;

    ALOGV("%s: Entry", __func__);

    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return -EINVAL;
    }

    uc_info_tx = (struct audio_usecase *)calloc(1, sizeof(*uc_info_tx));
    if (!uc_info_tx) {
        return -ENOMEM;
    }

    audio_route_apply_and_update_path(adev->audio_route,
                                      platform_get_snd_device_name(snd_device));

    pthread_mutex_lock(&handle.mutex_fb_prot);
    uc_info_tx->id = USECASE_AUDIO_SPKR_CALIB_TX;
    uc_info_tx->type = PCM_CAPTURE;
    uc_info_tx->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
    uc_info_tx->out_snd_device = SND_DEVICE_NONE;
    handle.pcm_tx = NULL;

    list_add_tail(&adev->usecase_list, &uc_info_tx->list);

    enable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
    enable_audio_route(adev, uc_info_tx);

    pcm_dev_tx_id = platform_get_pcm_device_id(uc_info_tx->id, PCM_CAPTURE);

    if (pcm_dev_tx_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)",
              __func__, uc_info_tx->id);
        ret = -ENODEV;
        goto exit;
    }

    handle.pcm_tx = pcm_open(adev->snd_card,
                             pcm_dev_tx_id,
                             PCM_IN, &pcm_config_cirrus_tx);

    if (!handle.pcm_tx || !pcm_is_ready(handle.pcm_tx)) {
        ALOGD("%s: PCM device not ready: %s", __func__,
              pcm_get_error(handle.pcm_tx ? handle.pcm_tx : 0));
        ret = -EIO;
        goto exit;
    }

    if (pcm_start(handle.pcm_tx) < 0) {
        ALOGI("%s: retrying pcm_start...", __func__);
	usleep(500 * 1000);
        if (pcm_start(handle.pcm_tx) < 0) {
            ALOGI("%s: pcm start for TX failed; error = %s", __func__,
                  pcm_get_error(handle.pcm_tx));
            ret = -EINVAL;
        }
    }

exit:
    if (ret) {
        ALOGI("%s: Disable and bail out", __func__);
        if (handle.pcm_tx) {
            ALOGI("%s: pcm_tx_close", __func__);
            pcm_close(handle.pcm_tx);
            handle.pcm_tx = NULL;
        }

        disable_audio_route(adev, uc_info_tx);
        disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
        list_remove(&uc_info_tx->list);
        free(uc_info_tx);
    }

    pthread_mutex_unlock(&handle.mutex_fb_prot);
    ALOGV("%s: Exit", __func__);
    return ret;
}

void audio_extn_spkr_prot_stop_processing(snd_device_t snd_device) {
    struct audio_usecase *uc_info_tx;
    struct audio_device *adev = handle.adev_handle;

    ALOGV("%s: Entry", __func__);

    pthread_mutex_lock(&handle.mutex_fb_prot);

    uc_info_tx = get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);

    if (uc_info_tx) {
        if (handle.pcm_tx) {
            ALOGI("%s: pcm_tx_close", __func__);
            pcm_close(handle.pcm_tx);
            handle.pcm_tx = NULL;
        }

        disable_audio_route(adev, uc_info_tx);
        disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
        list_remove(&uc_info_tx->list);
        free(uc_info_tx);

        audio_route_reset_path(adev->audio_route,
                               platform_get_snd_device_name(snd_device));
    }

    pthread_mutex_unlock(&handle.mutex_fb_prot);

    ALOGV("%s: Exit", __func__);
}

bool audio_extn_spkr_prot_is_enabled() {
    return handle.spkr_prot_enable;
}

int audio_extn_spkr_prot_get_acdb_id(snd_device_t snd_device) {
    int acdb_id;

    switch (snd_device) {
    case SND_DEVICE_OUT_SPEAKER:
        acdb_id = platform_get_snd_device_acdb_id(SND_DEVICE_OUT_SPEAKER_PROTECTED);
        break;
    case SND_DEVICE_OUT_VOICE_SPEAKER:
        acdb_id = platform_get_snd_device_acdb_id(SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED);
        break;
    default:
        acdb_id = -EINVAL;
        break;
    }
    return acdb_id;
}

int audio_extn_get_spkr_prot_snd_device(snd_device_t snd_device) {
    switch(snd_device) {
    case SND_DEVICE_OUT_SPEAKER:
        return SND_DEVICE_OUT_SPEAKER_PROTECTED;
    case SND_DEVICE_OUT_VOICE_SPEAKER:
        return SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED;
    default:
        return snd_device;
    }
}

void audio_extn_spkr_prot_calib_cancel(__unused void *adev) {
    // FIXME: wait or cancel audio_extn_cirrus_run_calibration
}
