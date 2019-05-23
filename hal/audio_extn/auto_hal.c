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
#include <log/log.h>
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

#define MAX_SOURCE_PORTS_PER_PATCH 1
#define MAX_SINK_PORTS_PER_PATCH 1

int audio_extn_auto_hal_create_audio_patch(struct audio_hw_device *dev,
                                unsigned int num_sources,
                                const struct audio_port_config *sources,
                                unsigned int num_sinks,
                                const struct audio_port_config *sinks,
                                audio_patch_handle_t *handle)
{
    struct audio_device *adev = (struct audio_device *)dev;
    int ret = 0;
    char *str = NULL;
    struct str_parms *parms = NULL;
    char *address = NULL;

    ALOGV("%s: enter", __func__);

    if (!dev || !sources || !sinks || !handle ) {
        ALOGE("%s: null audio patch parameters", __func__);
        return -EINVAL;
    }

    /* Port configuration check & validation */
    if (num_sources > MAX_SOURCE_PORTS_PER_PATCH ||
         num_sinks > MAX_SINK_PORTS_PER_PATCH) {
         ALOGE("%s: invalid audio patch parameters, sources %d sinks %d ",
                 __func__, num_sources, num_sources);
         return -EINVAL;
    }

    /* Release patch if valid handle */
    if (*handle != AUDIO_PATCH_HANDLE_NONE) {
        ret = audio_extn_auto_hal_release_audio_patch(dev,
                        *handle);
        if (ret) {
            ALOGE("%s: failed to release audio patch 0x%x", __func__, *handle);
            return ret;
        }
        *handle = AUDIO_PATCH_HANDLE_NONE;
    }

    /* No validation on num of sources and sinks to allow patch with
     * multiple sinks being created, but only the first source and
     * sink are used to create patch.
     *
     * Stream set_parameters for AUDIO_PARAMETER_STREAM_ROUTING and
     * AUDIO_PARAMETER_STREAM_INPUT_SOURCE is replaced with audio_patch
     * callback in audioflinger for AUDIO_DEVICE_API_VERSION_3_0 and above.
     * Need to handle device routing notification in audio HAL for
     *   Capture:  DEVICE -> MIX
     *   Playback: MIX -> DEVICE
     * For DEVICE -> DEVICE patch type, it refers to routing from/to external
     * codec/amplifier and allow Android streams to be mixed at the H/W level.
     */
    if ((sources->type == AUDIO_PORT_TYPE_DEVICE) &&
        (sinks->type == AUDIO_PORT_TYPE_MIX)) {
        pthread_mutex_lock(&adev->lock);
        streams_input_ctxt_t *in_ctxt = in_get_stream(adev,
                        sinks->ext.mix.handle);
        if (!in_ctxt) {
            ALOGE("%s, failed to find input stream", __func__);
            ret = -EINVAL;
        }
        pthread_mutex_unlock(&adev->lock);
        if(ret)
            return ret;

        if (strcmp(sources->ext.device.address, "") != 0) {
            address = audio_device_address_to_parameter(
                                                sources->ext.device.type,
                                                sources->ext.device.address);
        } else {
            address = (char *)calloc(1, 1);
        }
        parms = str_parms_create_str(address);
        if (!parms) {
            ALOGE("%s: failed to allocate mem for parms", __func__);
            ret = -ENOMEM;
            goto error;
        }
        str_parms_add_int(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                        (int)sources->ext.device.type);
        str_parms_add_int(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE,
                        (int)sinks->ext.mix.usecase.source);
        str = str_parms_to_str(parms);
        in_ctxt->input->stream.common.set_parameters(
                        (struct audio_stream *)in_ctxt->input, str);
    } else if ((sources->type == AUDIO_PORT_TYPE_MIX) &&
            (sinks->type == AUDIO_PORT_TYPE_DEVICE)) {
        pthread_mutex_lock(&adev->lock);
        streams_output_ctxt_t *out_ctxt = out_get_stream(adev,
            sources->ext.mix.handle);
        if (!out_ctxt) {
            ALOGE("%s, failed to find output stream", __func__);
            ret = -EINVAL;
        }
        pthread_mutex_unlock(&adev->lock);
        if(ret)
            return ret;

        if (strcmp(sinks->ext.device.address, "") != 0) {
            address = audio_device_address_to_parameter(
                                                sinks->ext.device.type,
                                                sinks->ext.device.address);
        } else {
            address = (char *)calloc(1, 1);
        }
        parms = str_parms_create_str(address);
        if (!parms) {
            ALOGE("%s: failed to allocate mem for parms", __func__);
            ret = -ENOMEM;
            goto error;
        }
        str_parms_add_int(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                        (int)sinks->ext.device.type);
        str = str_parms_to_str(parms);
        out_ctxt->output->stream.common.set_parameters(
                        (struct audio_stream *)out_ctxt->output, str);
    } else {
        ALOGW("%s: create device -> device audio patch", __func__);
    }

error:
    if (parms)
        str_parms_destroy(parms);
    if (address)
        free(address);
    ALOGV("%s: exit: handle 0x%x", __func__, *handle);
    return ret;
}

int audio_extn_auto_hal_release_audio_patch(struct audio_hw_device *dev,
                                audio_patch_handle_t handle)
{
    int ret = 0;

    ALOGV("%s: enter: handle 0x%x", __func__, handle);

    if (!dev) {
        ALOGE("%s: null audio patch parameters", __func__);
        return -EINVAL;
    }

    if (handle != AUDIO_PATCH_HANDLE_NONE) {
        ALOGW("%s: release device -> device audio patch", __func__);
    }

    ALOGV("%s: exit", __func__);
    return ret;
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
