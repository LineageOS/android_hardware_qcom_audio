/*
 * Copyright (c) 2013-2019, The Linux Foundation. All rights reserved.
 * Not a Contribution.
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
 *
 * This file was modified by DTS, Inc. The portions of the
 * code modified by DTS, Inc are copyrighted and
 * licensed separately, as follows:
 *
 * (C) 2014 DTS, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_extn"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <cutils/properties.h>
#include <log/log.h>
#include <unistd.h>
#include <sched.h>
#include "audio_hw.h"
#include "audio_extn.h"
#include "audio_defs.h"
#include "platform.h"
#include "platform_api.h"
#include "edid.h"

#include "sound/compress_params.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_AUDIO_EXTN
#include <log_utils.h>
#endif

#ifndef APTX_DECODER_ENABLED
#define audio_extn_aptx_dec_set_license(adev) (0)
#define audio_extn_set_aptx_dec_bt_addr(adev, parms) (0)
#define audio_extn_parse_aptx_dec_bt_addr(value) (0)
#else
static void audio_extn_aptx_dec_set_license(struct audio_device *adev);
static void audio_extn_set_aptx_dec_bt_addr(struct audio_device *adev, struct str_parms *parms);
static void audio_extn_parse_aptx_dec_bt_addr(char *value);
#endif

#define MAX_SLEEP_RETRY 100
#define WIFI_INIT_WAIT_SLEEP 50
#define MAX_NUM_CHANNELS 8
#define Q14_GAIN_UNITY 0x4000

struct audio_extn_module {
    bool anc_enabled;
    bool aanc_enabled;
    bool custom_stereo_enabled;
    uint32_t proxy_channel_num;
    bool hpx_enabled;
    bool vbat_enabled;
    bool bcl_enabled;
    bool hifi_audio_enabled;
    bool ras_enabled;
    struct aptx_dec_bt_addr addr;
    struct audio_device *adev;
};

static struct audio_extn_module aextnmod;

#define AUDIO_PARAMETER_KEY_AANC_NOISE_LEVEL "aanc_noise_level"
#define AUDIO_PARAMETER_KEY_ANC        "anc_enabled"
#define AUDIO_PARAMETER_KEY_WFD        "wfd_channel_cap"
#define AUDIO_PARAMETER_CAN_OPEN_PROXY "can_open_proxy"
#define AUDIO_PARAMETER_CUSTOM_STEREO  "stereo_as_dual_mono"
/* Query offload playback instances count */
#define AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE "offload_num_active"
#define AUDIO_PARAMETER_HPX            "HPX"
#define AUDIO_PARAMETER_APTX_DEC_BT_ADDR "bt_addr"

/*
* update sysfs node hdmi_audio_cb to enable notification acknowledge feature
* bit(5) set to 1 to enable this feature
* bit(4) set to 1 to enable acknowledgement
* this is done only once at the first connect event
*
* bit(0) set to 1 when HDMI cable is connected
* bit(0) set to 0 when HDMI cable is disconnected
* this is done when device switch happens by setting audioparamter
*/

#define EXT_DISPLAY_PLUG_STATUS_NOTIFY_ENABLE 0x30

static ssize_t update_sysfs_node(const char *path, const char *data, size_t len)
{
    ssize_t err = 0;
    int fd = -1;

    err = access(path, W_OK);
    if (!err) {
        fd = open(path, O_WRONLY);
        errno = 0;
        err = write(fd, data, len);
        if (err < 0) {
            err = -errno;
        }
        close(fd);
    } else {
        ALOGE("%s: Failed to access path: %s error: %s",
                __FUNCTION__, path, strerror(errno));
        err = -errno;
    }

    return err;
}

static int get_ext_disp_sysfs_node_index(int ext_disp_type)
{
    int node_index = -1;
    char fbvalue[80] = {0};
    char fbpath[80] = {0};
    int i = 0;
    FILE *ext_disp_fd = NULL;

    while (1) {
        snprintf(fbpath, sizeof(fbpath),
                  "/sys/class/graphics/fb%d/msm_fb_type", i);
        ext_disp_fd = fopen(fbpath, "r");
        if (ext_disp_fd) {
            if (fread(fbvalue, sizeof(char), 80, ext_disp_fd)) {
                if(((strncmp(fbvalue, "dtv panel", strlen("dtv panel")) == 0) &&
                    (ext_disp_type == EXT_DISPLAY_TYPE_HDMI)) ||
                   ((strncmp(fbvalue, "dp panel", strlen("dp panel")) == 0) &&
                    (ext_disp_type == EXT_DISPLAY_TYPE_DP))) {
                    node_index = i;
                    ALOGD("%s: Ext Disp:%d is at fb%d", __func__, ext_disp_type, i);
                    fclose(ext_disp_fd);
                    return node_index;
                }
            }
            fclose(ext_disp_fd);
            i++;
        } else {
            ALOGE("%s: Scanned till end of fbs or Failed to open fb node %d", __func__, i);
            break;
        }
    }

    return -1;
}

static int update_ext_disp_sysfs_node(const struct audio_device *adev, int node_value)
{
    char ext_disp_ack_path[80] = {0};
    char ext_disp_ack_value[3] = {0};
    int index, ret = -1;
    int ext_disp_type = platform_get_ext_disp_type(adev->platform);

    if (ext_disp_type < 0) {
        ALOGE("%s, Unable to get the external display type, err:%d",
              __func__, ext_disp_type);
        return -EINVAL;
    }

    index = get_ext_disp_sysfs_node_index(ext_disp_type);
    if (index >= 0) {
        snprintf(ext_disp_ack_value, sizeof(ext_disp_ack_value), "%d", node_value);
        snprintf(ext_disp_ack_path, sizeof(ext_disp_ack_path),
                  "/sys/class/graphics/fb%d/hdmi_audio_cb", index);

        ret = update_sysfs_node(ext_disp_ack_path, ext_disp_ack_value,
                sizeof(ext_disp_ack_value));

        ALOGI("update hdmi_audio_cb at fb[%d] to:[%d] %s",
            index, node_value, (ret >= 0) ? "success":"fail");
    }

    return ret;
}

static int update_audio_ack_state(const struct audio_device *adev, int node_value)
{
    const char *mixer_ctl_name = "External Display Audio Ack";
    struct mixer_ctl *ctl;
    int ret = 0;

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    /* If no mixer command support, fall back to sysfs node approach */
    if (!ctl) {
        ALOGI("%s: could not get ctl for mixer cmd(%s), use sysfs node instead\n",
              __func__, mixer_ctl_name);
        ret = update_ext_disp_sysfs_node(adev, node_value);
    } else {
        char *ack_str = NULL;

        if (node_value == EXT_DISPLAY_PLUG_STATUS_NOTIFY_ENABLE)
            ack_str = "Ack_Enable";
        else if (node_value == 1)
            ack_str = "Connect";
        else if (node_value == 0)
            ack_str = "Disconnect";
        else {
            ALOGE("%s: Invalid input parameter - 0x%x\n",
                  __func__, node_value);
            return -EINVAL;
        }

        ret = mixer_ctl_set_enum_by_string(ctl, ack_str);
        if (ret)
            ALOGE("%s: Could not set ctl for mixer cmd - %s ret %d\n",
                  __func__, mixer_ctl_name, ret);
    }
    return ret;
}

static void audio_extn_ext_disp_set_parameters(const struct audio_device *adev,
                                                     struct str_parms *parms)
{
    char value[32] = {0};
    static bool is_hdmi_sysfs_node_init = false;

    if (str_parms_get_str(parms, "connect", value, sizeof(value)) >= 0
            && (atoi(value) & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        //params = "connect=1024" for external display connection.
        if (is_hdmi_sysfs_node_init == false) {
            //check if this is different for dp and hdmi
            is_hdmi_sysfs_node_init = true;
            update_audio_ack_state(adev, EXT_DISPLAY_PLUG_STATUS_NOTIFY_ENABLE);
        }
        update_audio_ack_state(adev, 1);
    } else if(str_parms_get_str(parms, "disconnect", value, sizeof(value)) >= 0
            && (atoi(value) & AUDIO_DEVICE_OUT_AUX_DIGITAL)){
        //params = "disconnect=1024" for external display disconnection.
        update_audio_ack_state(adev, 0);
        ALOGV("invalidate cached edid");
        platform_invalidate_hdmi_config(adev->platform);
    } else {
        // handle ext disp devices only
        return;
    }
}

#ifndef SOURCE_TRACKING_ENABLED
#define audio_extn_source_track_set_parameters(adev, parms) (0)
#define audio_extn_source_track_get_parameters(adev, query, reply) (0)
#else
void audio_extn_source_track_set_parameters(struct audio_device *adev,
                                            struct str_parms *parms);
void audio_extn_source_track_get_parameters(const struct audio_device *adev,
                                            struct str_parms *query,
                                            struct str_parms *reply);
#endif

#ifndef CUSTOM_STEREO_ENABLED
#define audio_extn_customstereo_set_parameters(adev, parms)         (0)
#else
void audio_extn_customstereo_set_parameters(struct audio_device *adev,
                                           struct str_parms *parms)
{
    int ret = 0;
    char value[32]={0};
    bool custom_stereo_state = false;
    const char *mixer_ctl_name = "Set Custom Stereo OnOff";
    struct mixer_ctl *ctl;

    ALOGV("%s", __func__);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_CUSTOM_STEREO, value,
                            sizeof(value));
    if (ret >= 0) {
        if (!strncmp("true", value, sizeof("true")) || atoi(value))
            custom_stereo_state = true;

        if (custom_stereo_state == aextnmod.custom_stereo_enabled)
            return;

        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return;
        }
        if (mixer_ctl_set_value(ctl, 0, custom_stereo_state) < 0) {
            ALOGE("%s: Could not set custom stereo state %d",
                  __func__, custom_stereo_state);
            return;
        }
        aextnmod.custom_stereo_enabled = custom_stereo_state;
        ALOGV("%s: Setting custom stereo state success", __func__);
    }
}

void audio_extn_send_dual_mono_mixing_coefficients(struct stream_out *out)
{
    struct audio_device *adev = out->dev;
    struct mixer_ctl *ctl;
    char mixer_ctl_name[128];
    int cust_ch_mixer_cfg[128], len = 0;
    int ip_channel_cnt = audio_channel_count_from_out_mask(out->channel_mask);
    int pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);
    int op_channel_cnt = 2;
    int i, j, err;

    ALOGV("%s", __func__);
    if (!out->started) {
        out->set_dual_mono = true;
        goto exit;
    }

    ALOGD("%s: i/p channel count %d, o/p channel count %d, pcm id %d", __func__,
           ip_channel_cnt, op_channel_cnt, pcm_device_id);

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Audio Stream %d Channel Mix Cfg", pcm_device_id);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
        __func__, mixer_ctl_name);
        goto exit;
    }

    /* Output channel count corresponds to backend configuration channels.
     * Input channel count corresponds to ASM session channels.
     * Set params is called with channels that need to be selected from
     * input to generate output.
     * ex: "8,2" to downmix from 8 to 2 i.e. to downmix from 8 to 2,
     *
     * This mixer control takes values in the following sequence:
     * - input channel count(m)
     * - output channel count(n)
     * - weight coeff for [out ch#1, in ch#1]
     * ....
     * - weight coeff for [out ch#1, in ch#m]
     *
     * - weight coeff for [out ch#2, in ch#1]
     * ....
     * - weight coeff for [out ch#2, in ch#m]
     *
     * - weight coeff for [out ch#n, in ch#1]
     * ....
     * - weight coeff for [out ch#n, in ch#m]
     *
     * To get dualmono ouptu weightage coeff is calculated as Unity gain
     * divided by number of input channels.
     */
    cust_ch_mixer_cfg[len++] = ip_channel_cnt;
    cust_ch_mixer_cfg[len++] = op_channel_cnt;
    for (i = 0; i < op_channel_cnt; i++) {
         for (j = 0; j < ip_channel_cnt; j++) {
              cust_ch_mixer_cfg[len++] = Q14_GAIN_UNITY/ip_channel_cnt;
         }
    }

    err = mixer_ctl_set_array(ctl, cust_ch_mixer_cfg, len);
    if (err)
        ALOGE("%s: ERROR. Mixer ctl set failed", __func__);
exit:
    return;
}
#endif /* CUSTOM_STEREO_ENABLED */

static int update_custom_mtmx_coefficients(struct audio_device *adev,
                                           struct audio_custom_mtmx_params *params,
                                           int pcm_device_id)
{
    struct mixer_ctl *ctl = NULL;
    char *mixer_name_prefix = "AudStr";
    char *mixer_name_suffix = "ChMixer Weight Ch";
    char mixer_ctl_name[128] = {0};
    struct audio_custom_mtmx_params_info *pinfo = &params->info;
    int i = 0, err = 0;
    int cust_ch_mixer_cfg[128], len = 0;

    ALOGI("%s: ip_channels %d, op_channels %d, pcm_device_id %d",
          __func__, pinfo->ip_channels, pinfo->op_channels, pcm_device_id);

    if (adev->use_old_pspd_mix_ctrl) {
        /*
         * Below code is to ensure backward compatibilty with older
         * kernel version. Use old mixer control to set mixer coefficients
         */
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
         "Audio Stream %d Channel Mix Cfg", pcm_device_id);

        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return -EINVAL;
        }
        cust_ch_mixer_cfg[len++] = pinfo->ip_channels;
        cust_ch_mixer_cfg[len++] = pinfo->op_channels;
        for (i = 0; i < (int) (pinfo->op_channels * pinfo->ip_channels); i++) {
            ALOGV("%s: coeff[%d] %d", __func__, i, params->coeffs[i]);
            cust_ch_mixer_cfg[len++] = params->coeffs[i];
        }
        err = mixer_ctl_set_array(ctl, cust_ch_mixer_cfg, len);
        if (err) {
            ALOGE("%s: ERROR. Mixer ctl set failed", __func__);
            return -EINVAL;
        }
        ALOGD("%s: Mixer ctl set for %s success", __func__, mixer_ctl_name);
    } else {
        for (i = 0; i < (int)pinfo->op_channels; i++) {
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %d %s %d",
                    mixer_name_prefix, pcm_device_id, mixer_name_suffix, i+1);

            ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
            if (!ctl) {
                ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                      __func__, mixer_ctl_name);
                 return -EINVAL;
            }
            err = mixer_ctl_set_array(ctl,
                                      &params->coeffs[pinfo->ip_channels * i],
                                      pinfo->ip_channels);
            if (err) {
                ALOGE("%s: ERROR. Mixer ctl set failed", __func__);
                return -EINVAL;
            }
        }
    }
    return 0;
}

static void set_custom_mtmx_params(struct audio_device *adev,
                                   struct audio_custom_mtmx_params_info *pinfo,
                                   int pcm_device_id, bool enable)
{
    struct mixer_ctl *ctl = NULL;
    char *mixer_name_prefix = "AudStr";
    char *mixer_name_suffix = "ChMixer Cfg";
    char mixer_ctl_name[128] = {0};
    int chmixer_cfg[5] = {0}, len = 0;
    int be_id = -1, err = 0;

    be_id = platform_get_snd_device_backend_index(pinfo->snd_device);

    ALOGI("%s: ip_channels %d,op_channels %d,pcm_device_id %d,be_id %d",
          __func__, pinfo->ip_channels, pinfo->op_channels, pcm_device_id, be_id);

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "%s %d %s", mixer_name_prefix, pcm_device_id, mixer_name_suffix);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return;
    }
    chmixer_cfg[len++] = enable ? 1 : 0;
    chmixer_cfg[len++] = 0; /* rule index */
    chmixer_cfg[len++] = pinfo->ip_channels;
    chmixer_cfg[len++] = pinfo->op_channels;
    chmixer_cfg[len++] = be_id + 1;

    err = mixer_ctl_set_array(ctl, chmixer_cfg, len);
    if (err)
        ALOGE("%s: ERROR. Mixer ctl set failed", __func__);
}

void audio_extn_set_custom_mtmx_params(struct audio_device *adev,
                                        struct audio_usecase *usecase,
                                        bool enable)
{
    struct audio_custom_mtmx_params_info info = {0};
    struct audio_custom_mtmx_params *params = NULL;
    int num_devices = 0, pcm_device_id = -1, i = 0, ret = 0;
    snd_device_t new_snd_devices[SND_DEVICE_OUT_END] = {0};
    struct audio_backend_cfg backend_cfg = {0};
    uint32_t feature_id = 0;

    switch(usecase->type) {
    case PCM_PLAYBACK:
        if (usecase->stream.out) {
            pcm_device_id =
                platform_get_pcm_device_id(usecase->id, PCM_PLAYBACK);
            if (platform_split_snd_device(adev->platform,
                                          usecase->out_snd_device,
                                          &num_devices, new_snd_devices)) {
                new_snd_devices[0] = usecase->out_snd_device;
                num_devices = 1;
            }
        } else {
            ALOGE("%s: invalid output stream for playback usecase id:%d",
                  __func__, usecase->id);
            return;
        }
        break;
    case PCM_CAPTURE:
        if (usecase->stream.in) {
            pcm_device_id =
                platform_get_pcm_device_id(usecase->id, PCM_CAPTURE);
            if (platform_split_snd_device(adev->platform,
                                          usecase->in_snd_device,
                                          &num_devices, new_snd_devices)) {
                new_snd_devices[0] = usecase->in_snd_device;
                num_devices = 1;
            }
        } else {
            ALOGE("%s: invalid input stream for capture usecase id:%d",
                  __func__, usecase->id);
            return;
        }
        break;
    default:
        ALOGV("%s: unsupported usecase id:%d", __func__, usecase->id);
        return;
    }

    /*
     * check and update feature_id before this assignment,
     * if features like dual_mono is enabled and overrides the default(i.e. 0).
     */
    info.id = feature_id;
    info.usecase_id = usecase->id;
    for (i = 0, ret = 0; i < num_devices; i++) {
         info.snd_device = new_snd_devices[i];
         platform_get_codec_backend_cfg(adev, info.snd_device, &backend_cfg);
         if (usecase->type == PCM_PLAYBACK) {
             info.ip_channels = audio_channel_count_from_out_mask(
                                    usecase->stream.out->channel_mask);
             info.op_channels = backend_cfg.channels;
         } else {
             info.ip_channels = backend_cfg.channels;
             info.op_channels = audio_channel_count_from_in_mask(
                                    usecase->stream.in->channel_mask);
         }

         params = platform_get_custom_mtmx_params(adev->platform, &info);
         if (params) {
             if (enable)
                 ret = update_custom_mtmx_coefficients(adev, params,
                                                       pcm_device_id);
             if (ret < 0)
                 ALOGE("%s: error updating mtmx coeffs err:%d", __func__, ret);
             else
                 set_custom_mtmx_params(adev, &info, pcm_device_id, enable);
         }
    }
}

#ifndef DTS_EAGLE
#define audio_extn_hpx_set_parameters(adev, parms)         (0)
#define audio_extn_hpx_get_parameters(query, reply)  (0)
#define audio_extn_check_and_set_dts_hpx_state(adev)       (0)
#else
void audio_extn_hpx_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms)
{
    int ret = 0;
    char value[32]={0};
    char prop[PROPERTY_VALUE_MAX] = "false";
    bool hpx_state = false;
    const char *mixer_ctl_name = "Set HPX OnOff";
    struct mixer_ctl *ctl = NULL;
    ALOGV("%s", __func__);

    property_get("vendor.audio.use.dts_eagle", prop, "0");
    if (strncmp("true", prop, sizeof("true")))
        return;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HPX, value,
                            sizeof(value));
    if (ret >= 0) {
        if (!strncmp("ON", value, sizeof("ON")))
            hpx_state = true;

        if (hpx_state == aextnmod.hpx_enabled)
            return;

        aextnmod.hpx_enabled = hpx_state;
        /* set HPX state on stream pp */
        if (adev->offload_effects_set_hpx_state != NULL)
            adev->offload_effects_set_hpx_state(hpx_state);

        audio_extn_dts_eagle_fade(adev, aextnmod.hpx_enabled, NULL);
        /* set HPX state on device pp */
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (ctl)
            mixer_ctl_set_value(ctl, 0, aextnmod.hpx_enabled);
    }
}

static int audio_extn_hpx_get_parameters(struct str_parms *query,
                                       struct str_parms *reply)
{
    int ret;
    char value[32]={0};

    ALOGV("%s: hpx %d", __func__, aextnmod.hpx_enabled);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_HPX, value,
                            sizeof(value));
    if (ret >= 0) {
        if (aextnmod.hpx_enabled)
            str_parms_add_str(reply, AUDIO_PARAMETER_HPX, "ON");
        else
            str_parms_add_str(reply, AUDIO_PARAMETER_HPX, "OFF");
    }
    return ret;
}

void audio_extn_check_and_set_dts_hpx_state(const struct audio_device *adev)
{
    char prop[PROPERTY_VALUE_MAX];
    property_get("vendor.audio.use.dts_eagle", prop, "0");
    if (strncmp("true", prop, sizeof("true")))
        return;
    if (adev->offload_effects_set_hpx_state)
        adev->offload_effects_set_hpx_state(aextnmod.hpx_enabled);
}
#endif

/* Affine AHAL thread to CPU core */
void audio_extn_set_cpu_affinity()
{
    cpu_set_t cpuset;
    struct sched_param sched_param;
    int policy = SCHED_FIFO, rc = 0;

    ALOGV("%s: Set CPU affinity for read thread", __func__);
    CPU_ZERO(&cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
        ALOGE("%s: CPU Affinity allocation failed for Capture thread",
               __func__);

    sched_param.sched_priority = sched_get_priority_min(policy);
    rc = sched_setscheduler(0, policy, &sched_param);
    if (rc != 0)
         ALOGE("%s: Failed to set realtime priority", __func__);
}

#ifdef HIFI_AUDIO_ENABLED
bool audio_extn_is_hifi_audio_enabled(void)
{
    ALOGV("%s: status: %d", __func__, aextnmod.hifi_audio_enabled);
    return (aextnmod.hifi_audio_enabled ? true: false);
}

bool audio_extn_is_hifi_audio_supported(void)
{
    /*
     * for internal codec, check for hifiaudio property to enable hifi audio
     */
    if (property_get_bool("persist.vendor.audio.hifi.int_codec", false))
    {
        ALOGD("%s: hifi audio supported on internal codec", __func__);
        aextnmod.hifi_audio_enabled = 1;
    }

    return (aextnmod.hifi_audio_enabled ? true: false);
}
#endif

#ifdef VBAT_MONITOR_ENABLED
bool audio_extn_is_vbat_enabled(void)
{
    ALOGD("%s: status: %d", __func__, aextnmod.vbat_enabled);
    return (aextnmod.vbat_enabled ? true: false);
}

bool audio_extn_can_use_vbat(void)
{
    char prop_vbat_enabled[PROPERTY_VALUE_MAX] = "false";

    property_get("persist.vendor.audio.vbat.enabled", prop_vbat_enabled, "0");
    if (!strncmp("true", prop_vbat_enabled, 4)) {
        aextnmod.vbat_enabled = 1;
    }

    ALOGD("%s: vbat.enabled property is set to %s", __func__, prop_vbat_enabled);
    return (aextnmod.vbat_enabled ? true: false);
}

bool audio_extn_is_bcl_enabled(void)
{
    ALOGD("%s: status: %d", __func__, aextnmod.bcl_enabled);
    return (aextnmod.bcl_enabled ? true: false);
}

bool audio_extn_can_use_bcl(void)
{
    char prop_bcl_enabled[PROPERTY_VALUE_MAX] = "false";

    property_get("persist.vendor.audio.bcl.enabled", prop_bcl_enabled, "0");
    if (!strncmp("true", prop_bcl_enabled, 4)) {
        aextnmod.bcl_enabled = 1;
    }

    ALOGD("%s: bcl.enabled property is set to %s", __func__, prop_bcl_enabled);
    return (aextnmod.bcl_enabled ? true: false);
}
#endif

#ifdef RAS_ENABLED
bool audio_extn_is_ras_enabled(void)
{
    ALOGD("%s: status: %d", __func__, aextnmod.ras_enabled);
    return (aextnmod.ras_enabled ? true: false);
}

bool audio_extn_can_use_ras(void)
{
    if (property_get_bool("persist.vendor.audio.ras.enabled", false))
        aextnmod.ras_enabled = 1;

    ALOGD("%s: ras.enabled property is set to %d", __func__, aextnmod.ras_enabled);
    return (aextnmod.ras_enabled ? true: false);
}
#endif

#ifndef ANC_HEADSET_ENABLED
#define audio_extn_set_anc_parameters(adev, parms)       (0)
#else
bool audio_extn_get_anc_enabled(void)
{
    ALOGD("%s: anc_enabled:%d", __func__, aextnmod.anc_enabled);
    return (aextnmod.anc_enabled ? true: false);
}

bool audio_extn_should_use_handset_anc(int in_channels)
{
    char prop_aanc[PROPERTY_VALUE_MAX] = "false";

    property_get("persist.vendor.audio.aanc.enable", prop_aanc, "0");
    if (!strncmp("true", prop_aanc, 4)) {
        ALOGD("%s: AANC enabled in the property", __func__);
        aextnmod.aanc_enabled = 1;
    }

    return (aextnmod.aanc_enabled && aextnmod.anc_enabled
            && (in_channels == 1));
}

bool audio_extn_should_use_fb_anc(void)
{
  char prop_anc[PROPERTY_VALUE_MAX] = "feedforward";

  property_get("persist.vendor.audio.headset.anc.type", prop_anc, "0");
  if (!strncmp("feedback", prop_anc, sizeof("feedback"))) {
    ALOGD("%s: FB ANC headset type enabled\n", __func__);
    return true;
  }
  return false;
}

void audio_extn_set_aanc_noise_level(struct audio_device *adev,
                                     struct str_parms *parms)
{
    int ret;
    char value[32] = {0};
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "AANC Noise Level";

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_AANC_NOISE_LEVEL, value,
                            sizeof(value));
    if (ret >= 0) {
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (ctl)
            mixer_ctl_set_value(ctl, 0, atoi(value));
        else
            ALOGW("%s: Not able to get mixer ctl: %s",
                  __func__, mixer_ctl_name);
    }
}

void audio_extn_set_anc_parameters(struct audio_device *adev,
                                   struct str_parms *parms)
{
    int ret;
    char value[32] ={0};
    struct listnode *node;
    struct audio_usecase *usecase;
    struct str_parms *query_44_1;
    struct str_parms *reply_44_1;
    struct str_parms *parms_disable_44_1;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_ANC, value,
                            sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, "true") == 0)
            aextnmod.anc_enabled = true;
        else
            aextnmod.anc_enabled = false;

        /* Store current 44.1 configuration and disable it temporarily before
         * changing ANC state.
         * Since 44.1 playback is not allowed with anc on.
         * If ANC switch is done when 44.1 is active three devices would need
         * sequencing 1. "headphones-44.1", 2. "headphones-anc" and
         * 3. "headphones".
         * Note: Enable/diable of anc would affect other two device's state.
         */
        query_44_1 = str_parms_create_str(AUDIO_PARAMETER_KEY_NATIVE_AUDIO);
        reply_44_1 = str_parms_create();
        if (!query_44_1 || !reply_44_1) {
            if (query_44_1) {
                str_parms_destroy(query_44_1);
            }
            if (reply_44_1) {
                str_parms_destroy(reply_44_1);
            }

            ALOGE("%s: param creation failed", __func__);
            return;
        }

        platform_get_parameters(adev->platform, query_44_1, reply_44_1);

        parms_disable_44_1 = str_parms_create();
        if (!parms_disable_44_1) {
            str_parms_destroy(query_44_1);
            str_parms_destroy(reply_44_1);
            ALOGE("%s: param creation failed for parms_disable_44_1", __func__);
            return;
        }

        str_parms_add_str(parms_disable_44_1, AUDIO_PARAMETER_KEY_NATIVE_AUDIO, "false");
        platform_set_parameters(adev->platform, parms_disable_44_1);
        str_parms_destroy(parms_disable_44_1);

        // Refresh device selection for anc playback
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (usecase->type != PCM_CAPTURE) {
                if (usecase->stream.out->devices == \
                    AUDIO_DEVICE_OUT_WIRED_HEADPHONE ||
                    usecase->stream.out->devices ==  \
                    AUDIO_DEVICE_OUT_WIRED_HEADSET ||
                    usecase->stream.out->devices ==  \
                    AUDIO_DEVICE_OUT_EARPIECE) {
                        select_devices(adev, usecase->id);
                        ALOGV("%s: switching device completed", __func__);
                        break;
                }
            }
        }

        // Restore 44.1 configuration on top of updated anc state
        platform_set_parameters(adev->platform, reply_44_1);
        str_parms_destroy(query_44_1);
        str_parms_destroy(reply_44_1);
    }

    ALOGD("%s: anc_enabled:%d", __func__, aextnmod.anc_enabled);
}
#endif /* ANC_HEADSET_ENABLED */

#ifndef FLUENCE_ENABLED
#define audio_extn_set_fluence_parameters(adev, parms) (0)
#define audio_extn_get_fluence_parameters(adev, query, reply) (0)
#else
void audio_extn_set_fluence_parameters(struct audio_device *adev,
                                            struct str_parms *parms)
{
    int ret = 0, err;
    char value[32];
    struct listnode *node;
    struct audio_usecase *usecase;

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE,
                                 value, sizeof(value));
    ALOGV_IF(err >= 0, "%s: Set Fluence Type to %s", __func__, value);
    if (err >= 0) {
        ret = platform_set_fluence_type(adev->platform, value);
        if (ret != 0) {
            ALOGE("platform_set_fluence_type returned error: %d", ret);
        } else {
            /*
             *If the fluence is manually set/reset, devices
             *need to get updated for all the usecases
             *i.e. audio and voice.
             */
             list_for_each(node, &adev->usecase_list) {
                 usecase = node_to_item(node, struct audio_usecase, list);
                 select_devices(adev, usecase->id);
             }
        }
    }
}

int audio_extn_get_fluence_parameters(const struct audio_device *adev,
                       struct str_parms *query, struct str_parms *reply)
{
    int ret = 0, err;
    char value[256] = {0};

    err = str_parms_get_str(query, AUDIO_PARAMETER_KEY_FLUENCE, value,
                                                          sizeof(value));
    if (err >= 0) {
        ret = platform_get_fluence_type(adev->platform, value, sizeof(value));
        if (ret >= 0) {
            ALOGV("%s: Fluence Type is %s", __func__, value);
            str_parms_add_str(reply, AUDIO_PARAMETER_KEY_FLUENCE, value);
        } else
            goto done;
    }
done:
    return ret;
}
#endif /* FLUENCE_ENABLED */

#ifndef AFE_PROXY_ENABLED
#define audio_extn_set_afe_proxy_parameters(adev, parms)  (0)
#define audio_extn_get_afe_proxy_parameters(adev, query, reply) (0)
#else
static int32_t afe_proxy_set_channel_mapping(struct audio_device *adev,
                                                     int channel_count,
                                                     snd_device_t snd_device)
{
    struct mixer_ctl *ctl = NULL, *be_ctl = NULL;
    const char *mixer_ctl_name = "Playback Device Channel Map";
    const char *be_mixer_ctl_name = "Backend Device Channel Map";
    long set_values[FCC_8] = {0};
    long be_set_values[FCC_8 + 1] = {0};
    int ret = -1;
    int be_idx = -1;

    ALOGV("%s channel_count:%d",__func__, channel_count);

    switch (channel_count) {
    case 2:
        set_values[0] = PCM_CHANNEL_FL;
        set_values[1] = PCM_CHANNEL_FR;
        break;
    case 6:
        set_values[0] = PCM_CHANNEL_FL;
        set_values[1] = PCM_CHANNEL_FR;
        set_values[2] = PCM_CHANNEL_FC;
        set_values[3] = PCM_CHANNEL_LFE;
        set_values[4] = PCM_CHANNEL_LS;
        set_values[5] = PCM_CHANNEL_RS;
        break;
    case 8:
        set_values[0] = PCM_CHANNEL_FL;
        set_values[1] = PCM_CHANNEL_FR;
        set_values[2] = PCM_CHANNEL_FC;
        set_values[3] = PCM_CHANNEL_LFE;
        set_values[4] = PCM_CHANNEL_LS;
        set_values[5] = PCM_CHANNEL_RS;
        set_values[6] = PCM_CHANNEL_LB;
        set_values[7] = PCM_CHANNEL_RB;
        break;
    default:
        ALOGE("unsupported channels(%d) for setting channel map",
                                                    channel_count);
        return -EINVAL;
    }

    be_idx = platform_get_snd_device_backend_index(snd_device);

    if (be_idx >= 0) {
        be_ctl = mixer_get_ctl_by_name(adev->mixer, be_mixer_ctl_name);
        if (!be_ctl) {
            ALOGD("%s: Could not get ctl for mixer cmd - %s, using default control",
                  __func__, be_mixer_ctl_name);
            ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        } else
            ctl = be_ctl;
    } else
         ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);

    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    ALOGV("AFE: set mapping(%ld %ld %ld %ld %ld %ld %ld %ld) for channel:%d",
        set_values[0], set_values[1], set_values[2], set_values[3], set_values[4],
        set_values[5], set_values[6], set_values[7], channel_count);

    if (!be_ctl)
        ret = mixer_ctl_set_array(ctl, set_values, channel_count);
    else {
       be_set_values[0] = be_idx;
       memcpy(&be_set_values[1], set_values, sizeof(long) * channel_count);
       ret = mixer_ctl_set_array(ctl, be_set_values, ARRAY_SIZE(be_set_values));
    }

    return ret;
}

int32_t audio_extn_set_afe_proxy_channel_mixer(struct audio_device *adev,
                                    int channel_count, snd_device_t snd_device)
{
    int32_t ret = 0;
    const char *channel_cnt_str = NULL;
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "PROXY_RX Channels";

    ALOGD("%s: entry", __func__);
    /* use the existing channel count set by hardware params to
    configure the back end for stereo as usb/a2dp would be
    stereo by default */
    ALOGD("%s: channels = %d", __func__, channel_count);
    switch (channel_count) {
    case 8: channel_cnt_str = "Eight"; break;
    case 7: channel_cnt_str = "Seven"; break;
    case 6: channel_cnt_str = "Six"; break;
    case 5: channel_cnt_str = "Five"; break;
    case 4: channel_cnt_str = "Four"; break;
    case 3: channel_cnt_str = "Three"; break;
    default: channel_cnt_str = "Two"; break;
    }

    if(channel_count >= 2 && channel_count <= 8) {
       ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
       if (!ctl) {
            ALOGE("%s: could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
        return -EINVAL;
       }
    }
    mixer_ctl_set_enum_by_string(ctl, channel_cnt_str);

    if (channel_count == 6 || channel_count == 8 || channel_count == 2) {
        ret = afe_proxy_set_channel_mapping(adev, channel_count, snd_device);
    } else {
        ALOGE("%s: set unsupported channel count(%d)",  __func__, channel_count);
        ret = -EINVAL;
    }

    ALOGD("%s: exit", __func__);
    return ret;
}

void audio_extn_set_afe_proxy_parameters(struct audio_device *adev,
                                         struct str_parms *parms)
{
    int ret, val;
    char value[32]={0};

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_WFD, value,
                            sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        aextnmod.proxy_channel_num = val;
        adev->cur_wfd_channels = val;
        ALOGD("%s: channel capability set to: %d", __func__,
               aextnmod.proxy_channel_num);
    }
}

int audio_extn_get_afe_proxy_parameters(const struct audio_device *adev,
                                        struct str_parms *query,
                                        struct str_parms *reply)
{
    int ret, val = 0;
    char value[32]={0};

    ret = str_parms_get_str(query, AUDIO_PARAMETER_CAN_OPEN_PROXY, value,
                            sizeof(value));
    if (ret >= 0) {
        val = (adev->allow_afe_proxy_usage ? 1: 0);
        str_parms_add_int(reply, AUDIO_PARAMETER_CAN_OPEN_PROXY, val);
    }
    ALOGV("%s: called ... can_use_proxy %d", __func__, val);
    return 0;
}

/* must be called with hw device mutex locked */
int32_t audio_extn_read_afe_proxy_channel_masks(struct stream_out *out)
{
    int ret = 0;
    int channels = aextnmod.proxy_channel_num;

    switch (channels) {
        /*
         * Do not handle stereo output in Multi-channel cases
         * Stereo case is handled in normal playback path
         */
    case 6:
        ALOGV("%s: AFE PROXY supports 5.1", __func__);
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_5POINT1;
        break;
    case 8:
        ALOGV("%s: AFE PROXY supports 5.1 and 7.1 channels", __func__);
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_5POINT1;
        out->supported_channel_masks[1] = AUDIO_CHANNEL_OUT_7POINT1;
        break;
    default:
        ALOGE("AFE PROXY does not support multi channel playback");
        ret = -ENOSYS;
        break;
    }
    return ret;
}

int32_t audio_extn_get_afe_proxy_channel_count()
{
    return aextnmod.proxy_channel_num;
}

#endif /* AFE_PROXY_ENABLED */

static int get_active_offload_usecases(const struct audio_device *adev,
                                       struct str_parms *query,
                                       struct str_parms *reply)
{
    int ret, count = 0;
    char value[32]={0};
    struct listnode *node;
    struct audio_usecase *usecase;

    ALOGV("%s", __func__);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE, value,
                            sizeof(value));
    if (ret >= 0) {
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (is_offload_usecase(usecase->id))
                count++;
        }
        ALOGV("%s, number of active offload usecases: %d", __func__, count);
        str_parms_add_int(reply, AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE, count);
    }
    return ret;
}

void audio_extn_init(struct audio_device *adev)
{
    aextnmod.anc_enabled = 0;
    aextnmod.aanc_enabled = 0;
    aextnmod.custom_stereo_enabled = 0;
    aextnmod.proxy_channel_num = 2;
    aextnmod.hpx_enabled = 0;
    aextnmod.vbat_enabled = 0;
    aextnmod.bcl_enabled = 0;
    aextnmod.hifi_audio_enabled = 0;
    aextnmod.addr.nap = 0;
    aextnmod.addr.uap = 0;
    aextnmod.addr.lap = 0;
    aextnmod.adev = adev;

    audio_extn_dolby_set_license(adev);
    audio_extn_aptx_dec_set_license(adev);
}

void audio_extn_set_parameters(struct audio_device *adev,
                               struct str_parms *parms)
{
   audio_extn_set_aanc_noise_level(adev, parms);
   audio_extn_set_anc_parameters(adev, parms);
   audio_extn_set_fluence_parameters(adev, parms);
   audio_extn_set_afe_proxy_parameters(adev, parms);
   audio_extn_fm_set_parameters(adev, parms);
   audio_extn_sound_trigger_set_parameters(adev, parms);
   audio_extn_listen_set_parameters(adev, parms);
   audio_extn_ssr_set_parameters(adev, parms);
   audio_extn_hfp_set_parameters(adev, parms);
   audio_extn_dts_eagle_set_parameters(adev, parms);
   audio_extn_a2dp_set_parameters(parms);
   audio_extn_ddp_set_parameters(adev, parms);
   audio_extn_ds2_set_parameters(adev, parms);
   audio_extn_customstereo_set_parameters(adev, parms);
   audio_extn_hpx_set_parameters(adev, parms);
   audio_extn_pm_set_parameters(parms);
   audio_extn_source_track_set_parameters(adev, parms);
   audio_extn_fbsp_set_parameters(parms);
   audio_extn_keep_alive_set_parameters(adev, parms);
   audio_extn_passthru_set_parameters(adev, parms);
   audio_extn_ext_disp_set_parameters(adev, parms);
   audio_extn_qaf_set_parameters(adev, parms);
   if (audio_extn_qap_is_enabled())
       audio_extn_qap_set_parameters(adev, parms);
   if (adev->offload_effects_set_parameters != NULL)
       adev->offload_effects_set_parameters(parms);
   audio_extn_set_aptx_dec_bt_addr(adev, parms);
   audio_extn_ffv_set_parameters(adev, parms);
   audio_extn_ext_hw_plugin_set_parameters(adev->ext_hw_plugin, parms);
}

void audio_extn_get_parameters(const struct audio_device *adev,
                              struct str_parms *query,
                              struct str_parms *reply)
{
    char *kv_pairs = NULL;
    audio_extn_get_afe_proxy_parameters(adev, query, reply);
    audio_extn_get_fluence_parameters(adev, query, reply);
    audio_extn_ssr_get_parameters(adev, query, reply);
    get_active_offload_usecases(adev, query, reply);
    audio_extn_dts_eagle_get_parameters(adev, query, reply);
    audio_extn_hpx_get_parameters(query, reply);
    audio_extn_source_track_get_parameters(adev, query, reply);
    audio_extn_fbsp_get_parameters(query, reply);
    audio_extn_sound_trigger_get_parameters(adev, query, reply);
    audio_extn_fm_get_parameters(query, reply);
    if (adev->offload_effects_get_parameters != NULL)
        adev->offload_effects_get_parameters(query, reply);
    audio_extn_ext_hw_plugin_get_parameters(adev->ext_hw_plugin, query, reply);

    kv_pairs = str_parms_to_str(reply);
    ALOGD_IF(kv_pairs != NULL, "%s: returns %s", __func__, kv_pairs);
    free(kv_pairs);
}

#ifndef COMPRESS_METADATA_NEEDED
#define audio_extn_parse_compress_metadata(out, parms) (0)
#else
int audio_extn_parse_compress_metadata(struct stream_out *out,
                                       struct str_parms *parms)
{
    int ret = 0;
    char value[32];

    if (out->format == AUDIO_FORMAT_FLAC) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MIN_BLK_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.min_blk_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MAX_BLK_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.max_blk_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MIN_FRAME_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.min_frame_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MAX_FRAME_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.max_frame_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("FLAC metadata: min_blk_size %d, max_blk_size %d min_frame_size %d max_frame_size %d",
              out->compr_config.codec->options.flac_dec.min_blk_size,
              out->compr_config.codec->options.flac_dec.max_blk_size,
              out->compr_config.codec->options.flac_dec.min_frame_size,
              out->compr_config.codec->options.flac_dec.max_frame_size);
    }

    else if (out->format == AUDIO_FORMAT_ALAC) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_FRAME_LENGTH, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.frame_length = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_COMPATIBLE_VERSION, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.compatible_version = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_BIT_DEPTH, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.bit_depth = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_PB, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.pb = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MB, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.mb = atoi(value);
            out->is_compr_metadata_avail = true;
        }

        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_KB, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.kb = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_NUM_CHANNELS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.num_channels = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MAX_RUN, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.max_run = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MAX_FRAME_BYTES, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.max_frame_bytes = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_AVG_BIT_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.avg_bit_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_SAMPLING_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.sample_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_CHANNEL_LAYOUT_TAG, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.channel_layout_tag = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("ALAC CSD values: frameLength %d bitDepth %d numChannels %d"
                " maxFrameBytes %d, avgBitRate %d, sampleRate %d",
                out->compr_config.codec->options.alac.frame_length,
                out->compr_config.codec->options.alac.bit_depth,
                out->compr_config.codec->options.alac.num_channels,
                out->compr_config.codec->options.alac.max_frame_bytes,
                out->compr_config.codec->options.alac.avg_bit_rate,
                out->compr_config.codec->options.alac.sample_rate);
    }

    else if (out->format == AUDIO_FORMAT_APE) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_COMPATIBLE_VERSION, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.compatible_version = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_COMPRESSION_LEVEL, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.compression_level = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_FORMAT_FLAGS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.format_flags = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_BLOCKS_PER_FRAME, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.blocks_per_frame = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_FINAL_FRAME_BLOCKS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.final_frame_blocks = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_TOTAL_FRAMES, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.total_frames = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_BITS_PER_SAMPLE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.bits_per_sample = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_NUM_CHANNELS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.num_channels = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_SAMPLE_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.sample_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_SEEK_TABLE_PRESENT, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.seek_table_present = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("APE CSD values: compatibleVersion %d compressionLevel %d"
                " formatFlags %d blocksPerFrame %d finalFrameBlocks %d"
                " totalFrames %d bitsPerSample %d numChannels %d"
                " sampleRate %d seekTablePresent %d",
                out->compr_config.codec->options.ape.compatible_version,
                out->compr_config.codec->options.ape.compression_level,
                out->compr_config.codec->options.ape.format_flags,
                out->compr_config.codec->options.ape.blocks_per_frame,
                out->compr_config.codec->options.ape.final_frame_blocks,
                out->compr_config.codec->options.ape.total_frames,
                out->compr_config.codec->options.ape.bits_per_sample,
                out->compr_config.codec->options.ape.num_channels,
                out->compr_config.codec->options.ape.sample_rate,
                out->compr_config.codec->options.ape.seek_table_present);
    }

    else if (out->format == AUDIO_FORMAT_VORBIS) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_VORBIS_BITSTREAM_FMT, value, sizeof(value));
        if (ret >= 0) {
        // transcoded bitstream mode
            out->compr_config.codec->options.vorbis_dec.bit_stream_fmt = (atoi(value) > 0) ? 1 : 0;
            out->is_compr_metadata_avail = true;
        }
    }

    else if (out->format == AUDIO_FORMAT_WMA || out->format == AUDIO_FORMAT_WMA_PRO) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_FORMAT_TAG, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->format = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_AVG_BIT_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.avg_bit_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BLOCK_ALIGN, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.super_block_align = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BIT_PER_SAMPLE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.bits_per_sample = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_CHANNEL_MASK, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.channelmask = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.encodeopt = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION1, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.encodeopt1 = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION2, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.encodeopt2 = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("WMA params: fmt %x, bit rate %x, balgn %x, sr %d, chmsk %x"
                " encop %x, op1 %x, op2 %x",
                out->compr_config.codec->format,
                out->compr_config.codec->options.wma.avg_bit_rate,
                out->compr_config.codec->options.wma.super_block_align,
                out->compr_config.codec->options.wma.bits_per_sample,
                out->compr_config.codec->options.wma.channelmask,
                out->compr_config.codec->options.wma.encodeopt,
                out->compr_config.codec->options.wma.encodeopt1,
                out->compr_config.codec->options.wma.encodeopt2);
    }

    return ret;
}
#endif

#ifdef AUXPCM_BT_ENABLED
int32_t audio_extn_read_xml(struct audio_device *adev, uint32_t mixer_card,
                            const char* mixer_xml_path,
                            const char* mixer_xml_path_auxpcm)
{
    char bt_soc[128];
    bool wifi_init_complete = false;
    int sleep_retry = 0;

    while (!wifi_init_complete && sleep_retry < MAX_SLEEP_RETRY) {
        property_get("qcom.bluetooth.soc", bt_soc, NULL);
        if (strncmp(bt_soc, "unknown", sizeof("unknown"))) {
            wifi_init_complete = true;
        } else {
            usleep(WIFI_INIT_WAIT_SLEEP*1000);
            sleep_retry++;
        }
    }

    if (!strncmp(bt_soc, "ath3k", sizeof("ath3k")))
        adev->audio_route = audio_route_init(mixer_card, mixer_xml_path_auxpcm);
    else
        adev->audio_route = audio_route_init(mixer_card, mixer_xml_path);

    return 0;
}
#endif /* AUXPCM_BT_ENABLED */

#ifdef KPI_OPTIMIZE_ENABLED
typedef int (*perf_lock_acquire_t)(int, int, int*, int);
typedef int (*perf_lock_release_t)(int);

static void *qcopt_handle;
static perf_lock_acquire_t perf_lock_acq;
static perf_lock_release_t perf_lock_rel;

char opt_lib_path[512] = {0};

int audio_extn_perf_lock_init(void)
{
    int ret = 0;
    if (qcopt_handle == NULL) {
        if (property_get("ro.vendor.extension_library",
                         opt_lib_path, NULL) <= 0) {
            ALOGE("%s: Failed getting perf property \n", __func__);
            ret = -EINVAL;
            goto err;
        }
        if ((qcopt_handle = dlopen(opt_lib_path, RTLD_NOW)) == NULL) {
            ALOGE("%s: Failed to open perf handle \n", __func__);
            ret = -EINVAL;
            goto err;
        } else {
            perf_lock_acq = (perf_lock_acquire_t)dlsym(qcopt_handle,
                                                       "perf_lock_acq");
            if (perf_lock_acq == NULL) {
                ALOGE("%s: Perf lock Acquire NULL \n", __func__);
                dlclose(qcopt_handle);
                ret = -EINVAL;
                goto err;
            }
            perf_lock_rel = (perf_lock_release_t)dlsym(qcopt_handle,
                                                       "perf_lock_rel");
            if (perf_lock_rel == NULL) {
                ALOGE("%s: Perf lock Release NULL \n", __func__);
                dlclose(qcopt_handle);
                ret = -EINVAL;
                goto err;
            }
            ALOGE("%s: Perf lock handles Success \n", __func__);
        }
    }
err:
    return ret;
}

void audio_extn_perf_lock_acquire(int *handle, int duration,
                                 int *perf_lock_opts, int size)
{

    if (!perf_lock_opts || !size || !perf_lock_acq || !handle) {
        ALOGE("%s: Incorrect params, Failed to acquire perf lock, err ",
              __func__);
        return;
    }
    /*
     * Acquire performance lock for 1 sec during device path bringup.
     * Lock will be released either after 1 sec or when perf_lock_release
     * function is executed.
     */
    *handle = perf_lock_acq(*handle, duration, perf_lock_opts, size);
    if (*handle <= 0)
        ALOGE("%s: Failed to acquire perf lock, err: %d\n",
              __func__, *handle);
}

void audio_extn_perf_lock_release(int *handle)
{
    if (perf_lock_rel && handle && (*handle > 0)) {
        perf_lock_rel(*handle);
        *handle = 0;
    } else {
        ALOGE("%s: Perf lock release error \n", __func__);
    }
}
#endif /* KPI_OPTIMIZE_ENABLED */

static int audio_extn_set_multichannel_mask(struct audio_device *adev,
                                            struct stream_in *in,
                                            struct audio_config *config,
                                            bool *channel_mask_updated)
{
    int ret = -EINVAL;
    int channel_count = audio_channel_count_from_in_mask(in->channel_mask);
    *channel_mask_updated = false;

    int max_mic_count = platform_get_max_mic_count(adev->platform);
    /* validate input params*/
    if ((channel_count == 6) &&
        (in->format == AUDIO_FORMAT_PCM_16_BIT)) {

        switch (max_mic_count) {
            case 4:
                config->channel_mask = AUDIO_CHANNEL_INDEX_MASK_4;
                break;
            case 3:
                config->channel_mask = AUDIO_CHANNEL_INDEX_MASK_3;
                break;
            case 2:
                config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
                break;
            default:
                config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
                break;
        }
        ret = 0;
        *channel_mask_updated = true;
    }
    return ret;
}

int audio_extn_check_and_set_multichannel_usecase(struct audio_device *adev,
                                                  struct stream_in *in,
                                                  struct audio_config *config,
                                                  bool *update_params)
{
    bool ssr_supported = false;
    in->config.rate = config->sample_rate;
    in->sample_rate = config->sample_rate;
    ssr_supported = audio_extn_ssr_check_usecase(in);
    if (ssr_supported) {
        return audio_extn_ssr_set_usecase(in, config, update_params);
    } else if (audio_extn_ffv_check_usecase(in)) {
        return audio_extn_ffv_set_usecase(in);
    } else {
        return audio_extn_set_multichannel_mask(adev, in, config,
                                                update_params);
    }
}

#ifdef APTX_DECODER_ENABLED
static void audio_extn_aptx_dec_set_license(struct audio_device *adev)
{
    int ret, key = 0;
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "APTX Dec License";

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return;
    }
    key = platform_get_meta_info_key_from_list(adev->platform, "aptx");

    ALOGD("%s Setting APTX License with key:0x%x",__func__, key);
    ret = mixer_ctl_set_value(ctl, 0, key);
    if (ret)
        ALOGE("%s: cannot set license, error:%d",__func__, ret);
}

static void audio_extn_set_aptx_dec_bt_addr(struct audio_device *adev __unused, struct str_parms *parms)
{
    int ret = 0;
    char value[256];

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_APTX_DEC_BT_ADDR, value,
                            sizeof(value));
    if (ret >= 0) {
        audio_extn_parse_aptx_dec_bt_addr(value);
    }
}

int audio_extn_set_aptx_dec_params(struct aptx_dec_param *payload)
{
    struct aptx_dec_param *aptx_cfg = payload;

    aextnmod.addr.nap = aptx_cfg->bt_addr.nap;
    aextnmod.addr.uap = aptx_cfg->bt_addr.uap;
    aextnmod.addr.lap = aptx_cfg->bt_addr.lap;
    return 0;
}

static void audio_extn_parse_aptx_dec_bt_addr(char *value)
{
    int ba[6];
    char *str, *tok;
    uint32_t addr[3];
    int i = 0;

    ALOGV("%s: value %s", __func__, value);
    tok = strtok_r(value, ":", &str);
    while (tok != NULL) {
        ba[i] = strtol(tok, NULL, 16);
        i++;
        tok = strtok_r(NULL, ":", &str);
    }
    addr[0] = (ba[0] << 8) | ba[1];
    addr[1] = ba[2];
    addr[2] = (ba[3] << 16) | (ba[4] << 8) | ba[5];

    aextnmod.addr.nap = addr[0];
    aextnmod.addr.uap = addr[1];
    aextnmod.addr.lap = addr[2];
}

void audio_extn_send_aptx_dec_bt_addr_to_dsp(struct stream_out *out)
{
    ALOGD("%s", __func__);
    out->compr_config.codec->options.aptx_dec.nap = aextnmod.addr.nap;
    out->compr_config.codec->options.aptx_dec.uap = aextnmod.addr.uap;
    out->compr_config.codec->options.aptx_dec.lap = aextnmod.addr.lap;
}

#endif //APTX_DECODER_ENABLED

int audio_extn_out_set_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload) {
    int ret = -EINVAL;

    if (!out || !payload) {
        ALOGE("%s:: Invalid Param",__func__);
        return ret;
    }

    ALOGD("%s: enter: stream (%p) usecase(%d: %s) param_id %d", __func__,
            out, out->usecase, use_case_table[out->usecase], param_id);

    switch (param_id) {
        case AUDIO_EXTN_PARAM_OUT_RENDER_WINDOW:
            ret = audio_extn_utils_compress_set_render_window(out,
                    (struct audio_out_render_window_param *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_START_DELAY:
            ret = audio_extn_utils_compress_set_start_delay(out,
                    (struct audio_out_start_delay_param *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_ENABLE_DRIFT_CORRECTION:
            ret = audio_extn_utils_compress_enable_drift_correction(out,
                    (struct audio_out_enable_drift_correction *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_CORRECT_DRIFT:
            ret = audio_extn_utils_compress_correct_drift(out,
                    (struct audio_out_correct_drift *)(payload));
            break;
        case AUDIO_EXTN_PARAM_ADSP_STREAM_CMD:
            ret = audio_extn_adsp_hdlr_stream_set_param(out->adsp_hdlr_stream_handle,
                    ADSP_HDLR_STREAM_CMD_REGISTER_EVENT,
                    (void *)&payload->adsp_event_params);
            break;
        case AUDIO_EXTN_PARAM_OUT_CHANNEL_MAP:
            ret = audio_extn_utils_set_channel_map(out,
                    (struct audio_out_channel_map_param *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_MIX_MATRIX_PARAMS:
            ret = audio_extn_utils_set_pan_scale_params(out,
                    (struct mix_matrix_params *)(payload));
            break;
        case AUDIO_EXTN_PARAM_CH_MIX_MATRIX_PARAMS:
            ret = audio_extn_utils_set_downmix_params(out,
                    (struct mix_matrix_params *)(payload));
            break;
        default:
            ALOGE("%s:: unsupported param_id %d", __func__, param_id);
            break;
    }
    return ret;
}

/* API to get playback stream specific config parameters */
int audio_extn_out_get_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload)
{
    int ret = -EINVAL;
    struct audio_usecase *uc_info;

    if (!out || !payload) {
        ALOGE("%s:: Invalid Param",__func__);
        return ret;
    }

    switch (param_id) {
        case AUDIO_EXTN_PARAM_AVT_DEVICE_DRIFT:
            uc_info = get_usecase_from_list(out->dev, out->usecase);
            if (uc_info == NULL) {
                ALOGE("%s: Could not find the usecase (%d) in the list",
                       __func__, out->usecase);
                ret = -EINVAL;
            } else {
                ret = audio_extn_utils_get_avt_device_drift(uc_info,
                        (struct audio_avt_device_drift_param *)payload);
                if(ret)
                    ALOGE("%s:: avdrift query failed error %d", __func__, ret);
            }
            break;
        default:
            ALOGE("%s:: unsupported param_id %d", __func__, param_id);
            break;
    }

    return ret;
}

int audio_extn_set_device_cfg_params(struct audio_device *adev,
                                     struct audio_device_cfg_param *payload)
{
    struct audio_device_cfg_param *device_cfg_params = payload;
    int ret = -EINVAL;
    struct stream_out out;
    uint32_t snd_device = 0, backend_idx = 0;
    struct audio_device_config_param *adev_device_cfg_ptr;

    ALOGV("%s", __func__);

    if (!device_cfg_params || !adev || !adev->device_cfg_params) {
        ALOGE("%s:: Invalid Param", __func__);
        return ret;
    }

    /* Config is not supported for combo devices */
    if (popcount(device_cfg_params->device) != 1) {
        ALOGE("%s:: Invalid Device (%#x) - Config is ignored", __func__, device_cfg_params->device);
        return ret;
    }

    adev_device_cfg_ptr = adev->device_cfg_params;
    /* Create an out stream to get snd device from audio device */
    out.devices = device_cfg_params->device;
    out.sample_rate = device_cfg_params->sample_rate;
    snd_device = platform_get_output_snd_device(adev->platform, &out);
    backend_idx = platform_get_backend_index(snd_device);

    ALOGV("%s:: device %d sample_rate %d snd_device %d backend_idx %d",
                __func__, out.devices, out.sample_rate, snd_device, backend_idx);

    ALOGV("%s:: Device Config Params from Client samplerate %d  channels %d"
          " bit_width %d  format %d  device %d  channel_map[0] %d channel_map[1] %d"
          " channel_map[2] %d channel_map[3] %d channel_map[4] %d channel_map[5] %d"
          " channel_allocation %d\n", __func__, device_cfg_params->sample_rate,
          device_cfg_params->channels, device_cfg_params->bit_width,
          device_cfg_params->format, device_cfg_params->device,
          device_cfg_params->channel_map[0], device_cfg_params->channel_map[1],
          device_cfg_params->channel_map[2], device_cfg_params->channel_map[3],
          device_cfg_params->channel_map[4], device_cfg_params->channel_map[5],
          device_cfg_params->channel_allocation);

    /* Copy the config values into adev structure variable */
    adev_device_cfg_ptr += backend_idx;
    adev_device_cfg_ptr->use_client_dev_cfg = true;
    memcpy(&adev_device_cfg_ptr->dev_cfg_params, device_cfg_params, sizeof(struct audio_device_cfg_param));

    return 0;
}
