/*
 * Copyright (c) 2019 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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
#define LOG_TAG "auto_hal_extn"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <pthread.h>
#include <cutils/log.h>
#include <math.h>
#include <audio_hw.h>
#include "audio_extn.h"
#include "platform_api.h"
#include "platform.h"
#include "audio_hal_plugin.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_AUTO_HAL
#include <log_utils.h>
#endif

#ifdef AUDIO_EXTN_AUTO_HAL_ENABLED

struct hostless_config {
    struct pcm *pcm_tx;
    struct pcm *pcm_rx;
};

typedef struct auto_hal_module {
    struct audio_device *adev;
    struct hostless_config hostless;
} auto_hal_module_t;

/* Auto hal module struct */
static struct auto_hal_module *auto_hal = NULL;

/* Note: Due to ADP H/W design, SoC TERT/SEC TDM CLK and FSYNC lines are
 * both connected with CODEC and a single master is needed to provide
 * consistent CLK and FSYNC to slaves, hence configuring SoC TERT TDM as
 * single master and bring up a dummy hostless from TERT to SEC to ensure
 * both slave SoC SEC TDM and CODEC are driven upon system boot. */
int32_t audio_extn_auto_hal_enable_hostless(void)
{
    int32_t ret = 0;
    char mixer_path[MIXER_PATH_MAX_LENGTH];

    ALOGD("%s: Enable TERT -> SEC Hostless", __func__);

    if (auto_hal == NULL) {
        ALOGE("%s: Invalid device", __func__);
        return -EINVAL;
    }

    strlcpy(mixer_path, "dummy-hostless", MIXER_PATH_MAX_LENGTH);
    ALOGD("%s: apply mixer and update path: %s", __func__, mixer_path);
    if (audio_route_apply_and_update_path(auto_hal->adev->audio_route,
            mixer_path)) {
        ALOGD("%s: %s not supported, continue", __func__, mixer_path);
        return ret;
    }

    /* TERT TDM TX 7 HOSTLESS to SEC TDM RX 7 HOSTLESS */
    int pcm_dev_rx = 48, pcm_dev_tx = 49;
    struct pcm_config pcm_config_lb = {
        .channels = 1,
        .rate = 48000,
        .period_size = 240,
        .period_count = 2,
        .format = PCM_FORMAT_S16_LE,
        .start_threshold = 0,
        .stop_threshold = INT_MAX,
        .avail_min = 0,
    };

    auto_hal->hostless.pcm_tx = pcm_open(auto_hal->adev->snd_card,
                                   pcm_dev_tx,
                                   PCM_IN, &pcm_config_lb);
    if (auto_hal->hostless.pcm_tx &&
        !pcm_is_ready(auto_hal->hostless.pcm_tx)) {
        ALOGE("%s: %s", __func__,
            pcm_get_error(auto_hal->hostless.pcm_tx));
        ret = -EIO;
        goto error;
    }
    auto_hal->hostless.pcm_rx = pcm_open(auto_hal->adev->snd_card,
                                   pcm_dev_rx,
                                   PCM_OUT, &pcm_config_lb);
    if (auto_hal->hostless.pcm_rx &&
        !pcm_is_ready(auto_hal->hostless.pcm_rx)) {
        ALOGE("%s: %s", __func__,
            pcm_get_error(auto_hal->hostless.pcm_rx));
        ret = -EIO;
        goto error;
    }

    if (pcm_start(auto_hal->hostless.pcm_tx) < 0) {
        ALOGE("%s: pcm start for pcm tx failed", __func__);
        ret = -EIO;
        goto error;
    }
    if (pcm_start(auto_hal->hostless.pcm_rx) < 0) {
        ALOGE("%s: pcm start for pcm rx failed", __func__);
        ret = -EIO;
        goto error;
    }
    return ret;

error:
    if (auto_hal->hostless.pcm_rx)
        pcm_close(auto_hal->hostless.pcm_rx);
    if (auto_hal->hostless.pcm_tx)
        pcm_close(auto_hal->hostless.pcm_tx);
    return ret;
}

void audio_extn_auto_hal_disable_hostless(void)
{
    ALOGD("%s: Disable TERT -> SEC Hostless", __func__);

    if (auto_hal == NULL) {
        ALOGE("%s: Invalid device", __func__);
        return;
    }

    if (auto_hal->hostless.pcm_tx) {
        pcm_close(auto_hal->hostless.pcm_tx);
        auto_hal->hostless.pcm_tx = NULL;
    }
    if (auto_hal->hostless.pcm_rx) {
        pcm_close(auto_hal->hostless.pcm_rx);
        auto_hal->hostless.pcm_rx = NULL;
    }
}

int32_t audio_extn_auto_hal_init(struct audio_device *adev)
{
    int32_t ret = 0;

    if (auto_hal != NULL) {
        ALOGD("%s: Auto hal module already exists",
                __func__);
        return ret;
    }

    auto_hal = calloc(1, sizeof(struct auto_hal_module));

    if (auto_hal == NULL) {
        ALOGE("%s: Memory allocation failed for auto hal module",
                __func__);
        return -ENOMEM;
    }

    auto_hal->adev = adev;

    return ret;
}

void audio_extn_auto_hal_deinit(void)
{
    if (auto_hal == NULL) {
        ALOGE("%s: Auto hal module is NULL, cannot deinitialize",
                __func__);
        return;
    }

    free(auto_hal);

    return;
}
#endif /* AUDIO_EXTN_AUTO_HAL_ENABLED */
