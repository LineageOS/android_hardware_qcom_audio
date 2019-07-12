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
    card_status_t card_status;
    struct hostless_config hostless;
} auto_hal_module_t;

/* Auto hal module struct */
static struct auto_hal_module *auto_hal = NULL;

extern struct pcm_config pcm_config_deep_buffer;
extern struct pcm_config pcm_config_low_latency;

static const audio_usecase_t bus_device_usecases[] = {
    USECASE_AUDIO_PLAYBACK_MEDIA,
    USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION,
    USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE,
    USECASE_AUDIO_PLAYBACK_PHONE,
};

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

int32_t audio_extn_auto_hal_get_car_audio_stream_from_address(const char *address)
{
    int32_t bus_num = -1;
    char *str = NULL;
    char *last_r = NULL;
    char local_address[AUDIO_DEVICE_MAX_ADDRESS_LEN];

    /* bus device with null address error out */
    if (address == NULL) {
        ALOGE("%s: null address for car stream", __func__);
        return -1;
    }

    /* strtok will modify the original string. make a copy first */
    strlcpy(local_address, address, AUDIO_DEVICE_MAX_ADDRESS_LEN);

    /* extract bus number from address */
    str = strtok_r(local_address, "BUS_",&last_r);
    if (str != NULL)
        bus_num = (int32_t)strtol(str, (char **)NULL, 10);

    /* validate bus number */
    if ((bus_num < 0) || (bus_num >= MAX_CAR_AUDIO_STREAMS)) {
        ALOGE("%s: invalid bus number %d", __func__, bus_num);
        return -1;
    }

    return (0x1 << bus_num);
}

int32_t audio_extn_auto_hal_open_output_stream(struct stream_out *out)
{
    int ret = 0;
    unsigned int channels = audio_channel_count_from_out_mask(out->channel_mask);

    switch(out->car_audio_stream) {
    case CAR_AUDIO_STREAM_MEDIA:
        /* media bus stream shares pcm device with deep-buffer */
        out->usecase = USECASE_AUDIO_PLAYBACK_MEDIA;
        out->config = pcm_config_deep_buffer;
        out->config.period_size = get_output_period_size(out->sample_rate, out->format,
                                        channels, DEEP_BUFFER_OUTPUT_PERIOD_DURATION);
        if (out->config.period_size <= 0) {
            ALOGE("Invalid configuration period size is not valid");
            ret = -EINVAL;
            goto error;
        }
        break;
    case CAR_AUDIO_STREAM_SYS_NOTIFICATION:
        /* sys notification bus stream shares pcm device with low-latency */
        out->usecase = USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION;
        out->config = pcm_config_low_latency;
        break;
    case CAR_AUDIO_STREAM_NAV_GUIDANCE:
        out->usecase = USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE;
        out->config = pcm_config_deep_buffer;
        out->config.period_size = get_output_period_size(out->sample_rate, out->format,
                                        channels, DEEP_BUFFER_OUTPUT_PERIOD_DURATION);
        if (out->config.period_size <= 0) {
            ALOGE("Invalid configuration period size is not valid");
            ret = -EINVAL;
            goto error;
        }
        break;
    case CAR_AUDIO_STREAM_PHONE:
        out->usecase = USECASE_AUDIO_PLAYBACK_PHONE;
        out->config = pcm_config_low_latency;
        break;
    default:
        ALOGE("%s: Car audio stream %x not supported", __func__,
            out->car_audio_stream);
        ret = -EINVAL;
        goto error;
    }

error:
    return ret;
}

bool audio_extn_auto_hal_is_bus_device_usecase(audio_usecase_t uc_id)
{
    unsigned int i;
    for (i = 0; i < sizeof(bus_device_usecases)/sizeof(bus_device_usecases[0]); i++) {
        if (uc_id == bus_device_usecases[i])
            return true;
    }
    return false;
}

snd_device_t audio_extn_auto_hal_get_snd_device_for_car_audio_stream(struct stream_out *out)
{
    snd_device_t snd_device = SND_DEVICE_NONE;

    switch(out->car_audio_stream) {
    case CAR_AUDIO_STREAM_MEDIA:
        snd_device = SND_DEVICE_OUT_BUS_MEDIA;
        break;
    case CAR_AUDIO_STREAM_SYS_NOTIFICATION:
        snd_device = SND_DEVICE_OUT_BUS_SYS;
        break;
    case CAR_AUDIO_STREAM_NAV_GUIDANCE:
        snd_device = SND_DEVICE_OUT_BUS_NAV;
        break;
    case CAR_AUDIO_STREAM_PHONE:
        snd_device = SND_DEVICE_OUT_BUS_PHN;
        break;
    default:
        ALOGE("%s: Unknown car audio stream (%x)",
            __func__, out->car_audio_stream);
    }
    return snd_device;
}

int audio_extn_auto_hal_get_audio_port(struct audio_hw_device *dev __unused,
                        struct audio_port *config __unused)
{
    return -ENOSYS;
}

/* Volume min/max defined by audio policy configuration in millibel.
 * Support a range of -60dB to 6dB.
 */
#define MIN_VOLUME_VALUE_MB -6000
#define MAX_VOLUME_VALUE_MB 600
#define STEP_VALUE_MB 100
int audio_extn_auto_hal_set_audio_port_config(struct audio_hw_device *dev,
                        const struct audio_port_config *config)
{
    struct audio_device *adev = (struct audio_device *)dev;
    int ret = 0;
    struct listnode *node = NULL;
    float volume = 0.0;

    ALOGV("%s: enter", __func__);

    if (!config) {
        ALOGE("%s: invalid input parameters", __func__);
        return -EINVAL;
    }

    /* For Android automotive, audio port config from car framework
     * allows volume gain to be set to device at audio HAL level, where
     * the gain can be applied in DSP mixer or CODEC amplifier.
     *
     * Following routing should be considered:
     *     MIX -> DEVICE
     *     DEVICE -> MIX
     *     DEVICE -> DEVICE
     *
     * For BUS devices routed to/from mixer, gain will be applied to DSP
     * mixer via kernel control which audio HAL stream is associated with.
     *
     * For external (source) device (FM TUNER/AUX), routing is typically
     * done with AudioPatch to (sink) device (SPKR), thus gain should be
     * applied to CODEC amplifier via codec plugin extention as audio HAL
     * stream may not be available for external audio routing.
     */
    if (config->type == AUDIO_PORT_TYPE_DEVICE) {
        ALOGI("%s: device port: type %x, address %s, gain %d mB", __func__,
            config->ext.device.type,
            config->ext.device.address,
            config->gain.values[0]);
        if (config->role == AUDIO_PORT_ROLE_SINK) {
            /* handle output devices */
            pthread_mutex_lock(&adev->lock);
            list_for_each(node, &adev->active_outputs_list) {
                streams_output_ctxt_t *out_ctxt = node_to_item(node,
                                                    streams_output_ctxt_t,
                                                    list);
                /* limit audio gain support for bus device only */
                if (out_ctxt->output->devices == AUDIO_DEVICE_OUT_BUS &&
                    out_ctxt->output->devices == config->ext.device.type &&
                    strcmp(out_ctxt->output->address,
                        config->ext.device.address) == 0) {
                    /* millibel = 1/100 dB = 1/1000 bel
                     * q13 = (10^(mdb/100/20))*(2^13)
                     */
                    if(config->gain.values[0] <= (MIN_VOLUME_VALUE_MB + STEP_VALUE_MB))
                        volume = 0.0 ;
                    else
                        volume = powf(10.0, ((float)config->gain.values[0] / 2000));
                    ALOGV("%s: set volume to stream: %p", __func__,
                        &out_ctxt->output->stream);
                    /* set gain if output stream is active */
                    out_ctxt->output->stream.set_volume(
                                                &out_ctxt->output->stream,
                                                volume, volume);
                }
            }
            /* NOTE: Ideally audio patch list is a superset of output stream list above.
             *       However, audio HAL does not maintain patches for mix -> device or
             *       device -> mix currently. Thus doing separate lookups for device ->
             *       device in audio patch list.
             * FIXME: Cannot cache the gain if audio patch is not created. Expected gain
             *        to be part of port config upon audio patch creation. If not, need
             *        to create a list of audio port configs in adev context.
             */
#if 0
            list_for_each(node, &adev->audio_patch_record_list) {
                struct audio_patch_record *patch_record = node_to_item(node,
                                                    struct audio_patch_record,
                                                    list);
                /* limit audio gain support for bus device only */
                if (patch_record->sink.type == AUDIO_PORT_TYPE_DEVICE &&
                    patch_record->sink.role == AUDIO_PORT_ROLE_SINK &&
                    patch_record->sink.ext.device.type == AUDIO_DEVICE_OUT_BUS &&
                    patch_record->sink.ext.device.type == config->ext.device.type &&
                    strcmp(patch_record->sink.ext.device.address,
                        config->ext.device.address) == 0) {
                    /* cache / update gain per audio patch sink */
                    patch_record->sink.gain = config->gain;

                    struct audio_usecase *uc_info = get_usecase_from_list(adev,
                                                        patch_record->usecase);
                    if (!uc_info) {
                        ALOGE("%s: failed to find the usecase %d",
                            __func__, patch_record->usecase);
                        ret = -EINVAL;
                    } else {
                        volume = config->gain->values[0];
                        /* linear interpolation from millibel to level */
                        int vol_level = lrint(((volume + (0 - MIN_VOLUME_VALUE_MB)) /
                                               (MAX_VOLUME_VALUE_MB - MIN_VOLUME_VALUE_MB)) * 40);
                        ALOGV("%s: set volume to patch: %p", __func__,
                            patch_record->handle);
                        ret = audio_extn_ext_hw_plugin_set_audio_gain(adev,
                                uc_info, vol_level);
                    }
                }
            }
#endif
            pthread_mutex_unlock(&adev->lock);
        } else if (config->role == AUDIO_PORT_ROLE_SOURCE) {
            // FIXME: handle input devices.
        }
    }

    /* Only handle device port currently. */

    ALOGV("%s: exit", __func__);
    return ret;
}

void audio_extn_auto_hal_set_parameters(struct audio_device *adev __unused,
                                        struct str_parms *parms)
{
    int ret = 0;
    char value[32]={0};

    ALOGV("%s: enter", __func__);

    ret = str_parms_get_str(parms, "SND_CARD_STATUS", value, sizeof(value));
    if (ret >= 0) {
        char *snd_card_status = value+2;
        ALOGV("%s: snd card status %s", __func__, snd_card_status);
        if (strstr(snd_card_status, "OFFLINE")) {
            auto_hal->card_status = CARD_STATUS_OFFLINE;
            audio_extn_auto_hal_disable_hostless();
        }
        else if (strstr(snd_card_status, "ONLINE")) {
            auto_hal->card_status = CARD_STATUS_ONLINE;
            audio_extn_auto_hal_enable_hostless();
        }
    }

    ALOGV("%s: exit", __func__);
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
