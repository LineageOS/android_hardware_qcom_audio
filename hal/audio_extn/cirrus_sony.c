/*
 * Copyright (C) 2020, AngeloGioacchino Del Regno <kholk11@gmail.com>
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

#define LOG_TAG "audio_hw_sony_cirrus_playback"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <math.h>
#include <log/log.h>
#include <fcntl.h>
#include "../audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <sys/stat.h>
#include <linux/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <cutils/properties.h>
#include "audio_extn.h"

// - external function dependency -
static fp_platform_get_snd_device_name_t fp_platform_get_snd_device_name;
static fp_platform_get_pcm_device_id_t fp_platform_get_pcm_device_id;
static fp_get_usecase_from_list_t fp_get_usecase_from_list;
static fp_enable_disable_snd_device_t fp_disable_snd_device;
static fp_enable_disable_snd_device_t  fp_enable_snd_device;
static fp_enable_disable_audio_route_t fp_disable_audio_route;
static fp_enable_disable_audio_route_t fp_enable_audio_route;
static fp_platform_check_and_set_codec_backend_cfg_t fp_platform_check_and_set_codec_backend_cfg;

enum cirrus_playback_state {
    INIT = 0,
    CALIBRATING = 1,
    CALIBRATION_ERROR = 2,
    IDLE = 3,
    PLAYBACK = 4
};

/* Payload struct for getting calibration result from DSP module */
struct __attribute__((__packed__)) cirrus_cal_result_t {
    uint8_t status[4];
    uint8_t checksum[4];
    uint8_t cal_r[4];
    bool cal_ok;
};

#ifdef CIRRUS_DIAG
struct cirrus_cal_diag_t {
    uint8_t diag_f0[4];
    uint8_t diag_f0_status[4];
    uint8_t diag_z_low_diff[4];
};
#endif

struct __attribute__((__packed__)) cirrus_cal_file_t {
    struct cirrus_cal_result_t spkl;
    struct cirrus_cal_result_t spkr;
    uint32_t magicsum;
    uint8_t is_stereo;
};

struct cirrus_playback_session {
    void *adev_handle;
    pthread_mutex_t fb_prot_mutex;
    pthread_t calibration_thread;
    pthread_t failure_detect_thread;
    struct pcm *pcm_rx;
    struct cirrus_cal_result_t spkl;
    struct cirrus_cal_result_t spkr;
    bool cirrus_drv_enabled;
    bool is_stereo;
    volatile int32_t state;
};

/* TA handling */
#define LIB_MISCTA	"libMiscTaWrapper.so"
#define TA_DEBUG 1

/* TA functions */
void *ta_handle = NULL;
int (*miscta_get_unit_size)(uint32_t unit, uint32_t *size) = NULL;
int (*miscta_read_unit)(uint32_t id, void *buf, uint32_t *size) = NULL;
int (*miscta_write_unit)(uint32_t id, const void *buf, uint32_t size) = NULL;

/* TA Units for Cirrus codec params */
#define TA_CIRRUS_CAL_GLOBAL_CAL_AMBIENT	4702
#define TA_CIRRUS_CAL_SPKL_CAL_R		4703
#define TA_CIRRUS_CAL_SPKL_CAL_STATUS		4704
#define TA_CIRRUS_CAL_SPKL_CAL_CHECKSUM		4705
#define TA_CIRRUS_CAL_SPKL_DIAG_F0		4706
#define TA_CIRRUS_CAL_SPKL_DIAG_Z_LOW_DIFF	4707
#define TA_CIRRUS_CAL_SPKL_DIAG_F0_STATUS	4708
#define TA_CIRRUS_CAL_SPKR_CAL_R		4709
#define TA_CIRRUS_CAL_SPKR_CAL_STATUS		4710
#define TA_CIRRUS_CAL_SPKR_CAL_CHECKSUM		4711
#define TA_CIRRUS_CAL_SPKR_DIAG_F0		4712
#define TA_CIRRUS_CAL_SPKR_DIAG_Z_LOW_DIFF	4713
#define TA_CIRRUS_CAL_SPKR_DIAG_F0_STATUS	4714

/* Mixer controls */
#define CIRRUS_CTL_FORCE_WAKE		"Hibernate Force Wake"

#define CIRRUS_CTL_CALI_CAL_AMBIENT	"DSP1 Calibration cd CAL_AMBIENT"
#define CIRRUS_CTL_CALI_DIAG_F0		"DSP1 Calibration cd DIAG_F0"
#define CIRRUS_CTL_CALI_DIAG_F0_STATUS	"DSP1 Calibration cd DIAG_F0_STATUS"
#define CIRRUS_CTL_CALI_DIAG_Z_LOW_DIFF	"DSP1 Calibration cd DIAG_Z_LOW_DIFF"
#define CIRRUS_CTL_CALI_CAL_R		"DSP1 Calibration cd CAL_R"
#define CIRRUS_CTL_CALI_CAL_STATUS	"DSP1 Calibration cd CAL_STATUS"
#define CIRRUS_CTL_CALI_CAL_CHECKSUM	"DSP1 Calibration cd CAL_CHECKSUM"

#define CIRRUS_CTL_PROT_CAL_AMBIENT	"DSP1 Protection cd CAL_AMBIENT"
#define CIRRUS_CTL_PROT_DIAG_F0		"DSP1 Protection cd DIAG_F0"
#define CIRRUS_CTL_PROT_DIAG_F0_STATUS	"DSP1 Protection cd DIAG_F0_STATUS"
#define CIRRUS_CTL_PROT_DIAG_Z_LOW_DIFF	"DSP1 Protection cd DIAG_Z_LOW_DIFF"
#define CIRRUS_CTL_PROT_CAL_R		"DSP1 Protection cd CAL_R"
#define CIRRUS_CTL_PROT_CAL_STATUS	"DSP1 Protection CAL_STATUS"
#define CIRRUS_CTL_PROT_CAL_STATUS_CD	"DSP1 Protection cd CAL_STATUS"
#define CIRRUS_CTL_PROT_CAL_CHECKSUM	"DSP1 Protection CAL_CHECKSUM"
#define CIRRUS_CTL_PROT_CAL_CHECKSUM_CD	"DSP1 Protection cd CAL_CHECKSUM"

#define CIRRUS_CTL_PROT_CSPL_ERRORNO	"DSP1 Protection cd CSPL_ERRORNO"

#define CIRRUS_CTL_NAME_BUF 40
#define CIRRUS_ERROR_DETECT_SLEEP_US	250000

#define CIRRUS_FIRMWARE_LOAD_SLEEP_US	5000
#define CIRRUS_FIRMWARE_MAX_RETRY	30

/* Saved calibrations */
#ifndef CIRRUS_AUDIO_CAL_PATH
 #define CIRRUS_AUDIO_CAL_PATH "/data/vendor/audio/cirrus_sony.cal"
#endif

struct pcm_config pcm_config_cirrus_rx = {
    .channels = 8,
    .rate = 48000,
    .period_size = 320,
    .period_count = 4,
    .format = PCM_FORMAT_S32_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

static struct cirrus_playback_session handle;
uint8_t cal_ambient[4];

static void *cirrus_do_calibration();
static void *cirrus_failure_detect_thread();

static int get_ta_array(uint32_t unit, void *arr, bool reverse) {
    uint32_t ta_sz = 0;
    uint8_t *array, tmp;
    int i, ret;

    if (unit < 4700 || unit > TA_CIRRUS_CAL_SPKR_DIAG_F0_STATUS) {
        ALOGE("%s: Error: Not a cirrus related unit.", __func__);
        return -EINVAL;
    }

    array = arr;

    ret = miscta_get_unit_size(unit, &ta_sz);
    if (ret) {
        ALOGE("%s: Cannot retrieve TA unit %d size: error %d",
              __func__, unit, ret);
        goto end;
    }

    ret = miscta_read_unit(unit, array, &ta_sz);
    if (ret) {
        ALOGE("%s: Cannot read TA unit %d of size %u: error %d",
              __func__, unit, ta_sz, ret);
        goto end;
    }

    if (!reverse)
        goto end;

    /* Invert the array, because TA has the values inverted... */
    for (i = 0; i <= ta_sz / 2; i++) {
        tmp = array[i];
        array[i] = array[ta_sz - i - 1];
        array[ta_sz - i - 1] = tmp;
    }

end:
#ifdef TA_DEBUG
    ALOGI("%s: Read TA unit %u (size=%u) values: 0x%x 0x%x 0x%x 0x%x",
          __func__, unit, ta_sz, array[0], array[1], array[2], array[3]);
#endif

    return ret;
}

#define CSEED	0xC0FFEE
static unsigned int onecsum(struct cirrus_cal_result_t *cal) {
    unsigned int cs = CSEED;
    uint8_t *data = (uint8_t*)cal;
    int i;

    for (i = 0; i < sizeof(struct cirrus_cal_result_t); i++)
        cs -= 1 + (unsigned int)*data++;

    return cs;
}

static unsigned int calc_magicsum(struct cirrus_cal_result_t *cr,
                                  struct cirrus_cal_result_t *cl) {
    return (onecsum(cr) ^ onecsum(cl));
}

static int cirrus_cal_from_file(struct cirrus_playback_session *hdl) {
    FILE* fp_calparams = NULL;
    struct cirrus_cal_file_t fdata;
    int ret = -EINVAL;

    /* Is calibration done already? */
    fp_calparams = fopen(CIRRUS_AUDIO_CAL_PATH, "rb");
    if (fp_calparams == NULL)
        return -EINVAL;

    if (fread(&fdata, sizeof(fdata), 1, fp_calparams) != 1) {
        ALOGD("%s: Failure: Unexpected calibration file content.", __func__);
        ret = -EINVAL;
        goto end;
    }

    if (calc_magicsum(&fdata.spkl, &fdata.spkr) != fdata.magicsum) {
        ALOGD("%s: Failure: File checksum mismatch", __func__);
        ret = -EINVAL;
        goto end;
    }

    ALOGD("%s: Using stored calibrations", __func__);
    memcpy((void*)&hdl->spkl, (const void*)&fdata.spkl, sizeof(fdata.spkl));
    memcpy((void*)&hdl->spkr, (const void*)&fdata.spkr, sizeof(fdata.spkr));
    hdl->is_stereo = (fdata.is_stereo == 1);
    ret = 0;

end:
    fclose(fp_calparams);
    return ret;
}

static int cirrus_save_calibration(struct cirrus_playback_session *hdl) {
    FILE* fp_calparams = NULL;
    struct cirrus_cal_file_t fdata;
    int ret = 0;

    fp_calparams = fopen(CIRRUS_AUDIO_CAL_PATH, "wb");
    if (fp_calparams == NULL)
        return -EINVAL;

    memcpy((void*)&fdata.spkl, (const void*)&hdl->spkl, sizeof(fdata.spkl));
    memcpy((void*)&fdata.spkr, (const void*)&hdl->spkr, sizeof(fdata.spkr));
    fdata.magicsum = calc_magicsum(&fdata.spkl, &fdata.spkr);
    fdata.is_stereo = (uint8_t)hdl->is_stereo;

    if (fwrite(&fdata, sizeof(fdata), 1, fp_calparams) != 1)
        ret = ferror(fp_calparams);

    fclose(fp_calparams);
    return ret;
}

void spkr_prot_init(void *adev, spkr_prot_init_config_t spkr_prot_init_config_val) {
    int i, ret = 0;
    uint32_t ta_sz = 0;
    uint8_t tmp = 0;

    if (!adev) {
        ALOGE("%s: CIRRUS: Invalid params", __func__);
        return;
    }

    memset(&handle, 0, sizeof(handle));

    handle.cirrus_drv_enabled =
            property_get_bool("vendor.audio.enable.cirrus.speaker", false);

    if (!handle.cirrus_drv_enabled) {
        ALOGD("%s: This device has no cirrus amp+dsp: do not init.", __func__);
        return;
    }

    memset(&cal_ambient, 0, sizeof(cal_ambient));

    ALOGI("%s: Initialize Cirrus Logic Playback module", __func__);

    ta_handle = dlopen(LIB_MISCTA, RTLD_NOW);
    if (!ta_handle) {
        ALOGE("%s: dlopen failed for %s", __func__, LIB_MISCTA);
        return;
    }

    miscta_get_unit_size = dlsym(ta_handle, "miscta_get_unit_size");
    if (!miscta_get_unit_size) {
        ALOGE("%s: Cannot find symbol: miscta_get_unit_size", __func__);
        return;
    }

    miscta_read_unit = dlsym(ta_handle, "miscta_read_unit");
    if (!miscta_read_unit) {
        ALOGE("%s: Cannot find symbol: miscta_read_unit", __func__);
        return;
    }

    handle.adev_handle = adev;
    handle.state = INIT;

    /* Ambient */
    ret = get_ta_array(TA_CIRRUS_CAL_GLOBAL_CAL_AMBIENT, &cal_ambient, true);
    if (ret)
        return;

#ifdef GET_SPEAKER_CALIBRATIONS_FROM_TA
    /* Speaker LEFT */
    ret = get_ta_array(TA_CIRRUS_CAL_SPKL_CAL_R, &handle.spkl.cal_r, true);
    if (ret)
        return;

    ret = get_ta_array(TA_CIRRUS_CAL_SPKL_CAL_STATUS,
                       &handle.spkl.status, true);
    if (ret)
        return;

    ret = get_ta_array(TA_CIRRUS_CAL_SPKL_CAL_CHECKSUM,
                       &handle.spkl.checksum, true);
    if (ret)
        return;

    /* Speaker RIGHT */
    ret = get_ta_array(TA_CIRRUS_CAL_SPKR_CAL_R,
                       &handle.spkr.cal_r, true);
    if (ret)
        return;

    ret = get_ta_array(TA_CIRRUS_CAL_SPKR_CAL_STATUS,
                       &handle.spkr.status, true);
    if (ret)
        return;

    ret = get_ta_array(TA_CIRRUS_CAL_SPKR_CAL_CHECKSUM,
                       &handle.spkr.checksum, true);
    if (ret)
        return;
#endif

    /* Do we want to load or calibrate? */
    ret = cirrus_cal_from_file(&handle);
    if (ret == 0) {
        handle.spkl.cal_ok = true;
        handle.spkr.cal_ok = true;
    } else {
        handle.spkl.cal_ok = false;
        handle.spkr.cal_ok = false;
    }

    // init function pointers
    fp_platform_get_snd_device_name = spkr_prot_init_config_val.fp_platform_get_snd_device_name;
    fp_platform_get_pcm_device_id = spkr_prot_init_config_val.fp_platform_get_pcm_device_id;
    fp_get_usecase_from_list =  spkr_prot_init_config_val.fp_get_usecase_from_list;
    fp_disable_snd_device = spkr_prot_init_config_val.fp_disable_snd_device;
    fp_enable_snd_device = spkr_prot_init_config_val.fp_enable_snd_device;
    fp_disable_audio_route = spkr_prot_init_config_val.fp_disable_audio_route;
    fp_enable_audio_route = spkr_prot_init_config_val.fp_enable_audio_route;
    fp_platform_check_and_set_codec_backend_cfg = spkr_prot_init_config_val.fp_platform_check_and_set_codec_backend_cfg;

    pthread_mutex_init(&handle.fb_prot_mutex, NULL);

    (void)pthread_create(&handle.calibration_thread,
                (const pthread_attr_t *) NULL,
                cirrus_do_calibration, &handle);
}

int spkr_prot_deinit() {
    ALOGV("%s: Entry", __func__);

    if (!handle.cirrus_drv_enabled) {
        ALOGD("%s: This device has no cirrus amp+dsp.", __func__);
        return 0;
    }

    pthread_join(handle.failure_detect_thread, NULL);
    pthread_join(handle.calibration_thread, NULL);
    pthread_mutex_destroy(&handle.fb_prot_mutex);

    ALOGV("%s: Exit", __func__);
    return 0;
}

static int cirrus_format_mixer_name(const char* name, const char* channel,
                                    char *buf_out, int buf_sz)
{
    if (name == NULL)
        return -EINVAL;

    memset(buf_out, 0, buf_sz);

    /*
     * If we have two amps, then we have L and R controls, otherwise
     * in case of mono device (single amp), we have no L/R controls
     * and we don't append anything to the original control names
     *
     * Example:    MONO      STEREO L     STEREO R
     * *******   CCM Reset  L CCM Reset  R CCM Reset
     *
     * So, the "channel" variable may contain:
     * 0 for MONO, L or R for STEREO L/R.
     */
    if (channel == NULL || channel[0] < 'L')
        return snprintf(buf_out, buf_sz, "%s", name);

    return snprintf(buf_out, buf_sz, "%s %s", channel, name);
}

/* TODO: This function assumes that we are always using CARD 0 */
static int cirrus_set_mixer_value_by_name(char* ctl_name, int value) {
    struct mixer *card_mixer = NULL;
    struct mixer_ctl *ctl_config = NULL;
    int sndcard_id = 0, ret = 0;

    card_mixer = mixer_open(sndcard_id);
    if (!card_mixer) {
        ALOGE("%s: Cannot open mixer for card %d.", __func__, sndcard_id);
        return -1;
    }

    ctl_config = mixer_get_ctl_by_name(card_mixer, ctl_name);
    if (!ctl_config) {
        ALOGD("%s: Cannot get mixer control %s", __func__, ctl_name);
        ret = -1;
        goto exit;
    }

    ret = mixer_ctl_set_value(ctl_config, 0, value);
    if (ret < 0)
        ALOGE("%s: Cannot set mixer '%s' to '%d'",
              __func__, ctl_name, value);
exit:
    mixer_close(card_mixer);
    return ret;
}

static int cirrus_set_mixer_value_by_name_lr(char* ctl_base_name, int value) {
    char ctl_name[CIRRUS_CTL_NAME_BUF];
    int ret = 0;

    ret = cirrus_format_mixer_name(ctl_base_name, "L", ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_set_mixer_value_by_name(ctl_name, value);
    if (ret < 0) {
        ALOGE("%s: Cannot set mixer '%s' to '%d'", __func__, ctl_name, value);
        goto end;
    }

    ret = cirrus_format_mixer_name(ctl_base_name, "R", ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_set_mixer_value_by_name(ctl_name, value);
    if (ret < 0)
        ALOGE("%s: Cannot set mixer '%s' to '%d'", __func__, ctl_name, value);
end:
    return ret;
}

static int cirrus_get_mixer_value_by_name(char* ctl_name) {
    struct mixer *card_mixer = NULL;
    struct mixer_ctl *ctl_config = NULL;
    int sndcard_id = 0, ret = -EINVAL;

    card_mixer = mixer_open(sndcard_id);
    if (!card_mixer) {
        ALOGE("%s: Cannot open mixer for card %d.", __func__, sndcard_id);
        return -1;
    }

    ctl_config = mixer_get_ctl_by_name(card_mixer, ctl_name);
    if (!ctl_config) {
        ALOGE("%s: Cannot get mixer control %s", __func__, ctl_name);
        ret = -1;
        goto exit;
    }

    ret = mixer_ctl_get_value(ctl_config, 0);
    if (ret < 0)
        ALOGE("%s: Cannot get mixer %s value: error %d",
              __func__, ctl_name, ret);
exit:
    mixer_close(card_mixer);
    return ret;
}

static int cirrus_set_mixer_array_by_name(char* ctl_name,
                                          void* array, size_t count) {
    struct mixer *card_mixer = NULL;
    struct mixer_ctl *ctl_config = NULL;
    int sndcard_id = 0, ret = 0;

    card_mixer = mixer_open(sndcard_id);
    if (!card_mixer) {
        ALOGE("%s: Cannot open mixer for card %d.", __func__, sndcard_id);
        return -1;
    }

    ctl_config = mixer_get_ctl_by_name(card_mixer, ctl_name);
    if (!ctl_config) {
        ALOGD("%s: Cannot get mixer control %s", __func__, ctl_name);
        ret = -1;
        goto exit;
    }

    ret = mixer_ctl_set_array(ctl_config, array, count);
    if (ret < 0)
        ALOGE("%s: Cannot set mixer %s",
              __func__, ctl_name);
exit:
    mixer_close(card_mixer);
    return ret;
}

static int cirrus_get_mixer_array_by_name(char* ctl_name, void* array,
                                          size_t count) {
    struct mixer *card_mixer = NULL;
    struct mixer_ctl *ctl_config = NULL;
    int sndcard_id = 0, ret = -EINVAL;

    card_mixer = mixer_open(sndcard_id);
    if (!card_mixer) {
        ALOGE("%s: Cannot open mixer for card %d.", __func__, sndcard_id);
        return -1;
    }

    ctl_config = mixer_get_ctl_by_name(card_mixer, ctl_name);
    if (!ctl_config) {
        ALOGE("%s: Cannot get mixer control %s", __func__, ctl_name);
        ret = -1;
        goto exit;
    }

    memset(array, 0, count);

    ret = mixer_ctl_get_array(ctl_config, array, count);
    if (ret < 0)
        ALOGE("%s: Cannot get mixer %s value: error %d",
              __func__, ctl_name, ret);
exit:
    mixer_close(card_mixer);
    return ret;
}

static int cirrus_set_mixer_enum_by_name(char* ctl_name, const char* value) {
    struct mixer *card_mixer = NULL;
    struct mixer_ctl *ctl_config = NULL;
    int sndcard_id = 0, ret = 0;

    card_mixer = mixer_open(sndcard_id);
    if (!card_mixer) {
        ALOGE("%s: Cannot open mixer for card %d.", __func__, sndcard_id);
        return -1;
    }

    ctl_config = mixer_get_ctl_by_name(card_mixer, ctl_name);
    if (!ctl_config) {
        ALOGE("%s: Cannot get mixer control '%s'", __func__, ctl_name);
        ret = -1;
        goto exit;
    }

    ret = mixer_ctl_set_enum_by_string(ctl_config, value);
    if (ret < 0)
        ALOGE("%s: Cannot set mixer '%s' to '%s'",
              __func__, ctl_name, value);
exit:
    mixer_close(card_mixer);
    return ret;
}

static int cirrus_play_silence(int seconds) {
    struct audio_device *adev = handle.adev_handle;
    struct mixer_ctl *ctl_config = NULL;
    struct pcm_config rx_tmp = { 0 };
    struct audio_usecase *uc_info_rx;

    uint8_t *silence = NULL;
    int i, ret = 0, silence_bytes, silence_cnt = 1;
    unsigned int buffer_size = 0, frames_bytes = 0;
    int pcm_dev_rx_id, adev_retry = 5;

    if (!list_empty(&adev->usecase_list)) {
        ALOGD("%s: Usecase present retry speaker protection", __func__);
        return -EAGAIN;
    }

    uc_info_rx = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    if (!uc_info_rx) {
        return -ENOMEM;
    }

    while ((!adev->primary_output || !adev->platform) && adev_retry) {
        ALOGI("%s: Waiting for audio device...", __func__);
        sleep(1);
        adev_retry--;
    }

    uc_info_rx->id = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
    uc_info_rx->type = PCM_PLAYBACK;
    uc_info_rx->in_snd_device = SND_DEVICE_NONE;
    uc_info_rx->stream.out = adev->primary_output;
    list_init(&uc_info_rx->device_list);
    uc_info_rx->out_snd_device = SND_DEVICE_OUT_SPEAKER_PROTECTED;
    list_add_tail(&adev->usecase_list, &uc_info_rx->list);

    fp_platform_check_and_set_codec_backend_cfg(adev, uc_info_rx,
                                             uc_info_rx->out_snd_device);

    fp_enable_snd_device(adev, uc_info_rx->out_snd_device);
    fp_enable_audio_route(adev, uc_info_rx);

    pcm_dev_rx_id = fp_platform_get_pcm_device_id(uc_info_rx->id, PCM_PLAYBACK);
    ALOGV("%s: pcm device id %d", __func__, pcm_dev_rx_id);
    if (pcm_dev_rx_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)",
              __func__, uc_info_rx->id);
        goto exit;
    }

    handle.pcm_rx = pcm_open(adev->snd_card, pcm_dev_rx_id,
                             (PCM_OUT | PCM_MONOTONIC),
                             &pcm_config_cirrus_rx);
    if (!handle.pcm_rx) {
        ALOGE("%s: Cannot open output PCM", __func__);
        ret = -EIO;
        goto exit;
    }

    if (!pcm_is_ready(handle.pcm_rx)) {
        ALOGE("%s: The PCM device is not ready: %s", __func__,
              pcm_get_error(handle.pcm_rx));
        ret = -EIO;
        goto exit;
    }

    buffer_size = pcm_get_buffer_size(handle.pcm_rx);
    frames_bytes = pcm_frames_to_bytes(handle.pcm_rx, buffer_size);

    silence = (uint8_t *)calloc(1, frames_bytes);
    if (silence == NULL) {
        ALOGE("%s: Cannot allocate %d bytes: Memory exhausted.",
              __func__, frames_bytes);
        goto exit;
    }

    silence_cnt = pcm_frames_to_bytes(handle.pcm_rx, pcm_config_cirrus_rx.rate);
    silence_cnt = silence_cnt * seconds / frames_bytes + 1;

    ALOGD("%s: Start playing silence audio: sec=%d, count=%d",
          __func__, seconds, silence_cnt);
    for (i = 0; i <= silence_cnt; i++) {
        ret = pcm_write(handle.pcm_rx, silence, frames_bytes);
        if (ret) {
            ALOGE("%s: Cannot write PCM data: %d", __func__, ret);
            break;
        } else
            ALOGV("%s: Wrote PCM data", __func__);
    }
    ALOGD("%s: Stop playing silence audio", __func__);
    free(silence);

exit:
    if (handle.pcm_rx != NULL) {
        pcm_close(handle.pcm_rx);
        handle.pcm_rx = NULL;
    }

    fp_disable_audio_route(adev, uc_info_rx);
    fp_disable_snd_device(adev, uc_info_rx->out_snd_device);

    list_remove(&uc_info_rx->list);
    free(uc_info_rx);

    return ret;
}

static inline int cirrus_set_force_wake(bool enable) {
    int ret = 0;

    if (handle.is_stereo) {
        ret = cirrus_set_mixer_value_by_name_lr(CIRRUS_CTL_FORCE_WAKE,
                                                (int)enable);
    } else {
        ret = cirrus_set_mixer_value_by_name(CIRRUS_CTL_FORCE_WAKE,
                                             (int)enable);
    }

    if (ret < 0)
        ALOGE("%s: Cannot %s force wakeup", __func__,
              enable ? "enable" : "disable");
    else
        ALOGD("%s: Set %s %s", __func__, CIRRUS_CTL_FORCE_WAKE,
              enable ? "enable" : "disable");
    return ret;
}

static int cirrus_do_reset(const char *channel) {
    char ctl_name[CIRRUS_CTL_NAME_BUF];
    int ret = 0;

    ret = cirrus_format_mixer_name("CCM Reset", channel, ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_get_mixer_value_by_name(ctl_name);
    if (ret < 0) {
        ALOGE("%s: CCM Reset is missing!!!", __func__);
    } else {
        ret = cirrus_set_mixer_value_by_name(ctl_name, 1);
        ALOGI("%s: CCM Reset done.", __func__);
    }

    return ret;
}

static int cirrus_mixer_wait_for_setting(char *ctl, int val, int retry)
{
    int i, ret;

    for (i = 0; i < retry; i++) {
        /* Start firmware download sequence: shut down DSP and reset states */
        ret = cirrus_get_mixer_value_by_name(ctl);
        if (ret < 0 || ret == val)
            break;

        usleep(10000);
    }
    if (ret < 0 && i == retry)
        return -ETIMEDOUT;

    return ret;
}

static int cirrus_exec_fw_download(const char *fw_type, const char *channel,
                                   int do_reset) {
    char ctl_name[CIRRUS_CTL_NAME_BUF];
    uint8_t cspl_ena[4] = { 0 };
    int retry = 0, ret;

    ALOGD("%s: Asking for %s %s firmware %s", __func__, fw_type,
          ((channel && channel[0] != 0) ? channel : "(mono/global)"),
          (do_reset ? "with reset" : "without reset"));
    if (do_reset)
        ret = cirrus_do_reset(channel);

    /* If this one is missing, we're not using our Cirrus codec... */
    ret = cirrus_format_mixer_name("DSP Booted", channel, ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_get_mixer_value_by_name(ctl_name);
    if (ret < 0) {
        ALOGE("%s: %s control is missing. Bailing out.", __func__, ctl_name);
        ret = -ENODEV;
        goto exit;
    }

    /* Start firmware download sequence: shut down DSP and reset states */
    ret = cirrus_set_mixer_value_by_name(ctl_name, 0);
    if (ret < 0) {
        ALOGE("%s: Cannot reset %s status", __func__, ctl_name);
        goto exit;
    }

    ret = cirrus_mixer_wait_for_setting(ctl_name, 0, 10);
    if (ret < 0) {
        ALOGE("%s: %s wait setting error %d", __func__, ctl_name, ret);
        goto exit;
    }

    usleep(10000);

    ret = cirrus_format_mixer_name("DSP1 Preload Switch",
                                   channel, ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_set_mixer_value_by_name(ctl_name, 0);
    if (ret < 0) {
        ALOGE("%s: Cannot reset %s", __func__, ctl_name);
        goto exit;
    }

    ret = cirrus_mixer_wait_for_setting(ctl_name, 0, 10);
    if (ret < 0) {
        ALOGE("%s: %s wait setting error %d", __func__, ctl_name, ret);
        goto exit;
    }

    usleep(10000);

    /* Determine what firmware to load and configure DSP */
    ret = cirrus_format_mixer_name("DSP1 Firmware", channel, ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_set_mixer_enum_by_name(ctl_name, fw_type);
    if (ret < 0) {
        ALOGE("%s: Cannot set %s to %s", __func__, ctl_name, fw_type);
        goto exit;
    }

    ret = cirrus_format_mixer_name("PCM Source", channel, ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_set_mixer_enum_by_name(ctl_name, "DSP");
    if (ret < 0) {
        ALOGE("%s: Cannot set %s to DSP", __func__, ctl_name);
        goto exit;
    }

    /* Send the firmware! */
    ret = cirrus_format_mixer_name("DSP1 Preload Switch",
                                   channel, ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_set_mixer_value_by_name(ctl_name, 1);
    if (ret < 0) {
        ALOGE("%s: Cannot set %s to %s", __func__, ctl_name, fw_type);
        goto exit;
    }

    if (!strcmp(fw_type, "Protection")) {
        ret = cirrus_format_mixer_name("DSP1 Protection cd CSPL_ENABLE",
                                       channel, ctl_name, sizeof(ctl_name));
    } else if (!strcmp(fw_type, "Calibration")) {
        ret = cirrus_format_mixer_name("DSP1 Calibration cd CSPL_ENABLE",
                                       channel, ctl_name, sizeof(ctl_name));
    } else {
        ret = -EINVAL;
        ALOGE("%s: ERROR! Unsupported firmware type passed: %s",
              __func__, fw_type);
        goto exit;
    }

retry_fw:
    /*
     * Sleep for some time: checking right after sending the load command
     * is useless, the firmware at least won't be booted for sure.
     */
    usleep(CIRRUS_FIRMWARE_LOAD_SLEEP_US);

    ret = cirrus_get_mixer_array_by_name(ctl_name, &cspl_ena, 4);
    if (ret < 0) {
        if (retry < CIRRUS_FIRMWARE_MAX_RETRY) {
            retry++;
            ALOGI("%s: Retrying...\n", __func__);
            goto retry_fw;
        } else {
            ALOGE("%s: Cannot get %s stats", __func__, ctl_name);
            goto exit;
        }
    }

    if ((cspl_ena[0] + cspl_ena[1] + cspl_ena[2]) == 0 && cspl_ena[3] == 1) {
        ALOGI("%s: Cirrus %s Firmware Download SUCCESS.", __func__, fw_type);
        /* Wait for the hardware to stabilize */
        usleep(100000);
        ret = 0;
    } else {
        /*
         * Since we are using a poor hack to load the firmware, we cannot know
         * if the firmware was found nor if it finished loading remotely.
         * We also don't know how much time does the chip require to actually
         * boot it, so we will sleep and retry for X times, until it loads and
         * boots, or we assume that something went wrong: in that case the
         * only thing left to do is to return an error, hoping that developers
         * will catch it before going crazy...
         *
         * Perhaps, one day we will rewrite this messy part.
         */
        if (retry < CIRRUS_FIRMWARE_MAX_RETRY) {
            retry++;
            ALOGI("%s: Retrying...\n", __func__);
            goto retry_fw;
        }

        ALOGE("%s: Firmware download failure. CSPL Status: %u %u %u %u",
              __func__, cspl_ena[0], cspl_ena[1], cspl_ena[2], cspl_ena[3]);
        ret = -EINVAL;
    }

exit:
    return ret;
}

static int cirrus_mono_calibration(void) {
    struct audio_device *adev = handle.adev_handle;
#ifdef CIRRUS_DIAG
    struct cirrus_cal_diag_t cal_diag;
#endif
    bool stat_l_nok = true;
    int ret = 0;

    ret = cirrus_set_force_wake(true);
    if (ret < 0) {
        ALOGE("%s: Cannot force wakeup", __func__);
        goto exit;
    }

    ret = cirrus_set_mixer_array_by_name(CIRRUS_CTL_CALI_CAL_AMBIENT,
                                         cal_ambient, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot set ambient calibration", __func__);
        goto exit;
    }

    /* Play silence to run calibration internally */
    ret = cirrus_play_silence(2);
    if (ret < 0)
        ALOGW("%s: Playing silence went wrong, calibration may fail...",
              __func__);

#ifdef CIRRUS_DIAG
    ret = cirrus_get_mixer_array_by_name(CIRRUS_CTL_CALI_DIAG_F0,
                                         &cal_diag.diag_f0, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s stats", __func__, CIRRUS_CTL_CALI_DIAG_F0);
        goto exit;
    }

    ret = cirrus_get_mixer_array_by_name(CIRRUS_CTL_CALI_DIAG_F0_STATUS,
                                         &cal_diag.diag_f0_status, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, CIRRUS_CTL_CALI_DIAG_F0_STATUS);
        goto exit;
    }

    ret = cirrus_get_mixer_array_by_name(CIRRUS_CTL_CALI_DIAG_Z_LOW_DIFF,
                                         &cal_diag.diag_z_low_diff, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, CIRRUS_CTL_CALI_DIAG_Z_LOW_DIFF);
        goto exit;
    }

    ALOGD("%s: Diagnostics -- "
          "F0: 0x%x 0x%x 0x%x 0x%x   "
          "F0_STATUS: 0x%x 0x%x 0x%x 0x%x   "
          "Z_LOW_DIFF: 0x%x 0x%x 0x%x 0x%x", __func__,
          cal_diag.diag_f0[0], cal_diag.diag_f0[1],
          cal_diag.diag_f0[2], cal_diag.diag_f0[3],
          cal_diag.diag_f0_status[0], cal_diag.diag_f0_status[1],
          cal_diag.diag_f0_status[2], cal_diag.diag_f0_status[3],
          cal_diag.diag_z_low_diff[0], cal_diag.diag_z_low_diff[1],
          cal_diag.diag_z_low_diff[2], cal_diag.diag_z_low_diff[3]);
#endif

    ret = cirrus_get_mixer_array_by_name(CIRRUS_CTL_CALI_CAL_STATUS,
                                         &handle.spkr.status, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, CIRRUS_CTL_CALI_CAL_STATUS);
        goto exit;
    }

    stat_l_nok = !!(handle.spkr.status[0] | handle.spkr.status[1] |
                   handle.spkr.status[2]);
    if (stat_l_nok || handle.spkr.status[3] != 1) {
            if (!stat_l_nok && handle.spkr.status[3] == 3)
                ALOGE("%s: The calibration is out of range", __func__);
        ALOGE("%s: Calibration failure, status: 0x%x 0x%x 0x%x 0x%x",
              __func__, handle.spkr.status[0], handle.spkr.status[1],
              handle.spkr.status[2], handle.spkr.status[3]);
        ret = -EINVAL;
        goto exit;
    }

    ret = cirrus_get_mixer_array_by_name(CIRRUS_CTL_CALI_CAL_CHECKSUM,
                                         &handle.spkr.checksum, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, CIRRUS_CTL_CALI_CAL_CHECKSUM);
        goto exit;
    }

    ret = cirrus_get_mixer_array_by_name(CIRRUS_CTL_CALI_CAL_R,
                                         &handle.spkr.cal_r, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, CIRRUS_CTL_CALI_CAL_R);
        goto exit;
    }

#ifdef DEBUG_SHOW_VALUES
    ALOGE("%s: DEBUG! status: 0x%x 0x%x 0x%x 0x%x  "
          "csum: 0x%x 0x%x 0x%x 0x%x  "
          "Z: 0x%x 0x%x 0x%x 0x%x",
          __func__, handle.spkr.status[0], handle.spkr.status[1],
          handle.spkr.status[2], handle.spkr.status[3],
          handle.spkr.checksum[0], handle.spkr.checksum[1],
          handle.spkr.checksum[2], handle.spkr.checksum[3],
          handle.spkr.cal_r[0], handle.spkr.cal_r[1],
          handle.spkr.cal_r[2], handle.spkr.cal_r[3]);
#endif

    /* It HAS TO stay awake until Protection is loaded!!! */
    ret = cirrus_set_force_wake(true);
    if (ret < 0) {
        goto exit;
    }

    /* Calibration is done, smooth sailing! */
    handle.spkr.cal_ok = true;

exit:
    return ret;
}

/* TODO: Implement diagnostics for stereo -- left because too messy now */
static int cirrus_stereo_calibration(void) {
    struct audio_device *adev = handle.adev_handle;
    char ctl_name[CIRRUS_CTL_NAME_BUF];
    bool stat_l_nok = true, stat_r_nok = true;
    int ret = 0;

    ret = cirrus_set_force_wake(true);
    if (ret < 0)
        goto exit;

    /* Same CAL_AMBIENT for both speakers */
    ret = cirrus_set_mixer_value_by_name_lr(CIRRUS_CTL_CALI_CAL_AMBIENT,
                                            4);
    if (ret < 0) {
        ALOGE("%s: Cannot set ambient calibration", __func__);
        goto exit;
    }

    /* Play silence to run calibration internally */
    ret = cirrus_play_silence(2);
    if (ret < 0)
        ALOGW("%s: Playing silence went wrong, calibration may fail...",
              __func__);

    ret = cirrus_format_mixer_name(CIRRUS_CTL_CALI_CAL_STATUS, "L",
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_get_mixer_array_by_name(ctl_name,
                                          &handle.spkl.status, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, ctl_name);
        goto exit;
    }

    ret = cirrus_format_mixer_name(CIRRUS_CTL_CALI_CAL_STATUS, "R",
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_get_mixer_array_by_name(ctl_name,
                                          &handle.spkr.status, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, ctl_name);
        goto exit;
    }

    stat_l_nok = !!(handle.spkl.status[0] | handle.spkl.status[1] |
                   handle.spkl.status[2]);
    if (stat_l_nok || handle.spkl.status[3] != 1) {
            if (!stat_l_nok && handle.spkl.status[3] == 3)
                ALOGE("%s: The SPK L calibration is out of range", __func__);
        ALOGE("%s: SPK L Calibration failure, status: 0x%x 0x%x 0x%x 0x%x",
              __func__, handle.spkl.status[0], handle.spkl.status[1],
              handle.spkl.status[2], handle.spkl.status[3]);
        ret = -EINVAL;
        goto exit;
    }

    stat_r_nok = !!(handle.spkr.status[0] | handle.spkr.status[1] |
                   handle.spkr.status[2]);
    if (stat_r_nok || handle.spkr.status[3] != 1) {
            if (!stat_l_nok && handle.spkr.status[3] == 3)
                ALOGE("%s: The SPK R calibration is out of range", __func__);
        ALOGE("%s: SPK R Calibration failure, status: 0x%x 0x%x 0x%x 0x%x",
              __func__, handle.spkr.status[0], handle.spkr.status[1],
              handle.spkr.status[2], handle.spkr.status[3]);
        ret = -EINVAL;
        goto exit;
    }

    ret = cirrus_format_mixer_name(CIRRUS_CTL_CALI_CAL_CHECKSUM, "L",
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_get_mixer_array_by_name(ctl_name,
                                         &handle.spkl.checksum, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, ctl_name);
        goto exit;
    }

    ret = cirrus_format_mixer_name(CIRRUS_CTL_CALI_CAL_CHECKSUM, "R",
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_get_mixer_array_by_name(ctl_name,
                                         &handle.spkr.checksum, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, ctl_name);
        goto exit;
    }

    ret = cirrus_format_mixer_name(CIRRUS_CTL_CALI_CAL_R, "L",
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_get_mixer_array_by_name(ctl_name,
                                         &handle.spkl.cal_r, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, ctl_name);
        goto exit;
    }

    ret = cirrus_format_mixer_name(CIRRUS_CTL_CALI_CAL_R, "R",
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_get_mixer_array_by_name(ctl_name,
                                         &handle.spkr.cal_r, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, ctl_name);
        goto exit;
    }

    ret = cirrus_set_force_wake(true);
    if (ret < 0)
        goto exit;

    /* Calibration is done, smooth sailing! */
    handle.spkl.cal_ok = true;
    handle.spkr.cal_ok = true;

exit:
    return ret;
}

static int cirrus_write_cal_checksum(struct cirrus_cal_result_t *cal, char *lr)
{
    char ctl_name[CIRRUS_CTL_NAME_BUF];
    int ret;

    ret = cirrus_format_mixer_name(CIRRUS_CTL_PROT_CAL_CHECKSUM, lr,
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;

    ret = cirrus_set_mixer_array_by_name(ctl_name,
                                         cal->checksum, 4);
    if (ret >= 0)
        goto exit;

    /*
     * On some firmwares the creativity level is high and the mixer
     * names will be different.
     */
    ret = cirrus_format_mixer_name(CIRRUS_CTL_PROT_CAL_CHECKSUM_CD, lr,
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;

    ret = cirrus_set_mixer_array_by_name(ctl_name, cal->checksum, 4);
exit:
    return ret;
}

static int cirrus_write_cal_status(struct cirrus_cal_result_t *cal, char *lr)
{
    char ctl_name[CIRRUS_CTL_NAME_BUF];
    int ret;

    ret = cirrus_format_mixer_name(CIRRUS_CTL_PROT_CAL_STATUS, lr,
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;

    ret = cirrus_set_mixer_array_by_name(ctl_name,
                                         cal->status, 4);
    if (ret >= 0)
        goto exit;

    ret = cirrus_format_mixer_name(CIRRUS_CTL_PROT_CAL_STATUS_CD, lr,
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;

    ret = cirrus_set_mixer_array_by_name(ctl_name, cal->status, 4);
exit:
    return ret;
}

static int cirrus_do_fw_mono_download(int do_reset) {
    bool cal_valid = false, status_ok = false, checksum_ok = false;
    int i, max_retries = 32, ret = 0;

    for (i = 0; i < max_retries; i++) {
        ret = cirrus_exec_fw_download("Protection", 0, do_reset);
        if (ret == 0)
            break;
        usleep(500000);
    }
    if (ret != 0) {
        ALOGE("%s: Cannot send Protection firmware: bailing out.",
              __func__);
        return -EINVAL;
    }

    /* If the calibration is not valid, keep the fw loaded but get out. */
    if (!handle.spkr.cal_ok)
        return -EINVAL;

    ret = cirrus_set_force_wake(true);
    if (ret < 0)
        goto exit;

    ret = cirrus_set_mixer_array_by_name(CIRRUS_CTL_PROT_CAL_R,
                                         &handle.spkr.cal_r, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot set Z calibration", __func__);
        goto exit;
    }

    ret = cirrus_write_cal_status(&handle.spkr, 0);
    if (ret < 0) {
        ALOGE("%s: Cannot set calibration status", __func__);
        goto exit;
    }

    ret = cirrus_write_cal_checksum(&handle.spkr, 0);
    if (ret < 0) {
        ALOGE("%s: Cannot set calibration checksum", __func__);
        goto exit;
    }

    /* Time to get some rest: work is done! */
    ret = cirrus_set_force_wake(false);
    if (ret < 0)
        goto exit;

exit:
    ret += cirrus_play_silence(0);
    return ret;
}

static int cirrus_do_fw_stereo_download(int do_reset) {
    char ctl_name[CIRRUS_CTL_NAME_BUF];
    bool cal_valid = false, status_ok = false, checksum_ok = false;
    int i, max_retries = 32, ret = 0;

    ALOGI("%s: Sending speaker protection stereo firmware", __func__);

    for (i = 0; i < max_retries; i++) {
        ret = cirrus_exec_fw_download("Protection", "R", do_reset);
        if (ret == 0)
            break;
        usleep(500000);
    }
    if (ret != 0) {
        ALOGE("%s: Cannot send Protection R firmware: bailing out.",
              __func__);
        return -EINVAL;
    }

    /*
     * Guarantee that we retry for at least 3 seconds... but the
     * other firmware should just load instantly, since we've been
     * waiting for the DSP at R-SPK loading time.
     *
     * This is only to paranoidly account any possible future issue.
     */
    if (max_retries < 6)
        max_retries = 6;

    for (i = 0; i < max_retries; i++) {
        ret = cirrus_exec_fw_download("Protection", "L", do_reset);
        if (ret == 0)
            break;
        usleep(500000);
    }
    if (ret != 0) {
        ALOGE("%s: Cannot send Protection L firmware: bailing out.",
              __func__);
        return -EINVAL;
    }

    /* If the calibration is not valid, keep the fw loaded but get out. */
    if (!handle.spkl.cal_ok || !handle.spkr.cal_ok) {
        return -EINVAL;
    }

    ret = cirrus_set_force_wake(true);
    if (ret < 0)
        goto exit;

    ret = cirrus_format_mixer_name(CIRRUS_CTL_PROT_CAL_R, "R",
                                    ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_set_mixer_array_by_name(ctl_name,
                                         &handle.spkr.cal_r, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot set Z-R calibration", __func__);
        goto exit;
    }

    ret = cirrus_write_cal_status(&handle.spkr, "R");
    if (ret < 0) {
        ALOGE("%s: Cannot set calibration R status", __func__);
        goto exit;
    }

    ret = cirrus_write_cal_checksum(&handle.spkr, "R");
    if (ret < 0) {
        ALOGE("%s: Cannot set checksum R", __func__);
        goto exit;
    }

    ret = cirrus_format_mixer_name(CIRRUS_CTL_PROT_CAL_R, "L",
                                    ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_set_mixer_array_by_name(ctl_name,
                                         &handle.spkl.cal_r, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot set Z-L calibration", __func__);
        goto exit;
    }

    ret = cirrus_write_cal_status(&handle.spkl, "L");
    if (ret < 0) {
        ALOGE("%s: Cannot set calibration L status", __func__);
        goto exit;
    }

    ret = cirrus_write_cal_checksum(&handle.spkl, "L");
    if (ret < 0) {
        ALOGE("%s: Cannot set checksum L", __func__);
        goto exit;
    }

    /* Time to get some rest: work is done! */
    ret = cirrus_set_force_wake(false);
    if (ret < 0)
        goto exit;

exit:
    ret += cirrus_play_silence(0);
    return ret;
}

static int cirrus_do_fw_calibration_download(struct cirrus_playback_session *hdl)
{
    int ret = 0;

    ret = cirrus_exec_fw_download("Calibration", 0, 0);
    if (ret < 0) {
        ret = cirrus_exec_fw_download("Calibration", "L", 0);
        ret += cirrus_exec_fw_download("Calibration", "R", 0);
        if (ret != 0)
            return ret;

        /* Dual amp case */
        hdl->is_stereo = true;
    }

    return ret;
}

static void *cirrus_do_calibration() {
    struct audio_device *adev = handle.adev_handle;
    int ret = 0, dev_file = -1;

    pthread_mutex_lock(&adev->lock);
    handle.state = CALIBRATING;
    pthread_mutex_unlock(&adev->lock);

    if (handle.spkl.cal_ok && handle.spkr.cal_ok)
        goto skip_calibration;

    ALOGI("%s: Calibrating with ambient values 0x%x 0x%x 0x%x 0x%x",
           __func__, cal_ambient[0], cal_ambient[1], cal_ambient[2],
           cal_ambient[3]);

    ret = cirrus_do_fw_calibration_download(&handle);
    if (ret != 0) {
        ALOGE("%s: Cannot send Calibration firmware: bailing out.",
              __func__);
        ret = -EINVAL;
        goto end;
    }

    if (handle.is_stereo)
        ret = cirrus_stereo_calibration();
    else
        ret = cirrus_mono_calibration();

    if (ret < 0) {
        ALOGE("%s: CRITICAL: Calibration failure", __func__);
        goto end;
    }
    ALOGI("%s: Calibration success! Saving state and waiting for DSP...",
          __func__);

    ret = cirrus_save_calibration(&handle);
    if (ret) {
        /* We don't trigger a failure here: audio will still work... */
        ALOGW("%s: Cannot save calibration to file (%d)!!!", __func__, ret);
        ret = 0;
    }

skip_calibration:
    if (handle.is_stereo)
        ret = cirrus_do_fw_stereo_download(0);
    else
        ret = cirrus_do_fw_mono_download(0);
    if (ret < 0)
        ALOGE("%s: Cannot send speaker protection FW", __func__);

end:
    pthread_mutex_lock(&adev->lock);
    if (ret < 0)
        handle.state = CALIBRATION_ERROR;
    else
        handle.state = IDLE;
    pthread_mutex_unlock(&adev->lock);

    pthread_exit(0);
    return NULL;
}

static int cirrus_check_error_state_mono(void) {
    uint8_t cspl_error[4] = { 0 };
    int ret = 0;

    ret = cirrus_set_force_wake(true);
    if (ret < 0)
        return ret;

    ret = cirrus_get_mixer_array_by_name(CIRRUS_CTL_PROT_CSPL_ERRORNO,
                                         &cspl_error, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get CSPL status", __func__);
        goto exit;
    }

    if (cspl_error[3] != 0) {
        ALOGE("%s: Error state detected!", __func__);
        ret = -EREMOTEIO;
    }

exit:
    ret = cirrus_set_force_wake(false);
    return ret;
}

static int cirrus_check_error_state_stereo(void) {
    char ctl_name[CIRRUS_CTL_NAME_BUF];
    uint8_t cspl_error[4] = { 0 };
    int ret = 0;

    ret = cirrus_set_force_wake(true);
    if (ret < 0)
        return ret;

    ret = cirrus_format_mixer_name(CIRRUS_CTL_PROT_CSPL_ERRORNO, "L",
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_get_mixer_array_by_name(ctl_name, &cspl_error, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, ctl_name);
        goto exit;
    }

    if (cspl_error[3] != 0) {
        ALOGE("%s: Error state detected on SPK-L!", __func__);
        ret = -EREMOTEIO;
        goto exit;
    }

    ret = cirrus_format_mixer_name(CIRRUS_CTL_PROT_CSPL_ERRORNO, "R",
                                   ctl_name, sizeof(ctl_name));
    if (ret < 0)
        return ret;
    ret = cirrus_get_mixer_array_by_name(ctl_name, &cspl_error, 4);
    if (ret < 0) {
        ALOGE("%s: Cannot get %s", __func__, ctl_name);
        goto exit;
    }

    if (cspl_error[3] != 0) {
        ALOGE("%s: Error state detected on SPK-R!", __func__);
        ret = -EREMOTEIO;
        goto exit;
    }

exit:
    ret = cirrus_set_force_wake(false);
    return ret;
}

static int cirrus_check_error_state(void) {
    if (handle.is_stereo)
        return cirrus_check_error_state_stereo();

    return cirrus_check_error_state_mono();
}

static int cirrus_check_error_fatal(void) {
    int ret = 0;

    /*
     * If the device doesn't have a cirrus amp+dsp, then it's
     * surely all right from this side of things.... :)))
     */
    if (!handle.cirrus_drv_enabled)
        return 0;

    pthread_mutex_lock(&handle.fb_prot_mutex);

    ret = cirrus_check_error_state();
    if (ret == 0)
        goto success;

    /* Ouch! Error! Let's wait just a lil and retry to be extra sure... */
    usleep(CIRRUS_ERROR_DETECT_SLEEP_US);
    ret = cirrus_check_error_state();
    if (ret == 0)
        goto success;

    /* Error recovery: this should actually never happen, anyway... */
    ALOGE("%s: Cirrus DSP is in error state: resetting device", __func__);
    cirrus_do_reset(0);
    cirrus_do_reset("L");
    cirrus_do_reset("R");

    ALOGE("%s: Reset done, crashing HAL to reload firmware...", __func__);
    ALOGE("%s: Goodbye, infamous world...", __func__);
    abort();

success:
    pthread_mutex_unlock(&handle.fb_prot_mutex);
    return ret;
}

static void *cirrus_failure_detect_thread() {
    ALOGD("%s: Entry", __func__);

    (void)cirrus_check_error_fatal();

    ALOGD("%s: Exit ", __func__);

    pthread_exit(0);
    return NULL;
}

int spkr_prot_start_processing(__unused snd_device_t snd_device) {
    struct audio_device *adev = handle.adev_handle;
    int ret = 0;

    ALOGV("%s: Entry", __func__);

    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return -EINVAL;
    }

    if (pthread_self() == handle.calibration_thread) {
        // Succeed without doing anything; the calibration already
        // selects the right paths, and we do not want the failure
        // detect thread to run just yet.
        ALOGV("%s: We are the calibration thread", __func__);
        goto end;
    }

    pthread_mutex_lock(&handle.fb_prot_mutex);

    ALOGV("%s: current state %d", __func__, handle.state);

    /*
     * If we are still in calibration phase, we cannot play audio...
     * and it's the same if we got an error during the process.
     *
     * Reason is that if we try playing audio during calibration, then
     * the result will be bad and we will end up with a poorly calibrated
     * speaker. Also, the DSP may get left in a bad state and not accept
     * the protection firmware when we're ready for it.
     */
    if (handle.state == CALIBRATING || handle.state == CALIBRATION_ERROR) {
        ALOGI("%s: Forbidden. Calibration %s", __func__,
              handle.state == CALIBRATING ? "is in progress..." : "failed.");
        ret = -1;
        goto end;
    }

    ret = cirrus_set_force_wake(true);

    audio_route_apply_and_update_path(adev->audio_route,
                                      fp_platform_get_snd_device_name(snd_device));

    if (handle.state == IDLE)
        (void)pthread_create(&handle.failure_detect_thread,
                    (const pthread_attr_t *) NULL,
                    cirrus_failure_detect_thread,
                    &handle);

    handle.state = PLAYBACK;
end:
    pthread_mutex_unlock(&handle.fb_prot_mutex);

    ALOGV("%s: Exit", __func__);
    return ret;
}

void spkr_prot_stop_processing(__unused snd_device_t snd_device) {
    struct audio_usecase *uc_info_tx;
    struct audio_device *adev = handle.adev_handle;

    ALOGV("%s: Entry", __func__);

    pthread_mutex_lock(&handle.fb_prot_mutex);

    if (pthread_self() == handle.calibration_thread) {
        // This happens when stopping the device from calibration. We bailed
        // and never set PLAYBACK, so we should also never update the audio
        // route nor unconditionally set the state back to IDLE
        ALOGV("%s: We are the calibration thread", __func__);
        goto end;
    }

    if (handle.state != PLAYBACK) {
        ALOGE("%s: Cannot stop processing, state is not PLAYBACK (but %d)",
              __func__, handle.state);
        goto end;
    }

    handle.state = IDLE;

    audio_route_reset_and_update_path(adev->audio_route,
                                      fp_platform_get_snd_device_name(snd_device));

end:
    pthread_mutex_unlock(&handle.fb_prot_mutex);

    ALOGV("%s: Exit", __func__);
}

bool spkr_prot_is_enabled() {
    return true;
}

int get_spkr_prot_snd_device(snd_device_t snd_device) {
    switch(snd_device) {
    case SND_DEVICE_OUT_SPEAKER:
    case SND_DEVICE_OUT_SPEAKER_REVERSE:
        return SND_DEVICE_OUT_SPEAKER_PROTECTED;
    case SND_DEVICE_OUT_SPEAKER_SAFE:
        return SND_DEVICE_OUT_SPEAKER_SAFE;
    case SND_DEVICE_OUT_VOICE_SPEAKER:
        return SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED;
    default:
        return snd_device;
    }
}

void spkr_prot_calib_cancel(__unused void *adev) {
    return;
}
