/*
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
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
 */

#define LOG_TAG "audio_hw_extn"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <stdlib.h>
#include <errno.h>
#include <cutils/properties.h>
#include <cutils/log.h>

#include "audio_hw.h"
#include "audio_extn.h"
#include "platform.h"
#include "platform_api.h"

#define MAX_SLEEP_RETRY 100
#define WIFI_INIT_WAIT_SLEEP 50

struct audio_extn_module {
    bool anc_enabled;
    bool aanc_enabled;
    uint32_t proxy_channel_num;
};

static struct audio_extn_module aextnmod = {
    .anc_enabled = 0,
    .aanc_enabled = 0,
    .proxy_channel_num = 2,
};

#define AUDIO_PARAMETER_KEY_ANC        "anc_enabled"
#define AUDIO_PARAMETER_KEY_WFD        "wfd_channel_cap"
#define AUDIO_PARAMETER_CAN_OPEN_PROXY "can_open_proxy"
/* Query offload playback instances count */
#define AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE "offload_num_active"

#ifndef FM_ENABLED
#define audio_extn_fm_set_parameters(adev, parms) (0)
#else
void audio_extn_fm_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);
#endif
#ifndef HFP_ENABLED
#define audio_extn_hfp_set_parameters(adev, parms) (0)
#else
void audio_extn_hfp_set_parameters(struct audio_device *adev,
                                           struct str_parms *parms);
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

    property_get("persist.aanc.enable", prop_aanc, "0");
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

  property_get("persist.headset.anc.type", prop_anc, "0");
  if (!strncmp("feedback", prop_anc, sizeof("feedback"))) {
    ALOGD("%s: FB ANC headset type enabled\n", __func__);
    return true;
  }
  return false;
}

void audio_extn_set_anc_parameters(struct audio_device *adev,
                                   struct str_parms *parms)
{
    int ret;
    char value[32] ={0};
    struct listnode *node;
    struct audio_usecase *usecase;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_ANC, value,
                            sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, "true") == 0)
            aextnmod.anc_enabled = true;
        else
            aextnmod.anc_enabled = false;

        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (usecase->type == PCM_PLAYBACK) {
                if (usecase->stream.out->devices == \
                    AUDIO_DEVICE_OUT_WIRED_HEADPHONE ||
                    usecase->stream.out->devices ==  \
                    AUDIO_DEVICE_OUT_WIRED_HEADSET) {
                        select_devices(adev, usecase->id);
                        ALOGV("%s: switching device", __func__);
                        break;
                }
            }
        }
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
#define audio_extn_get_afe_proxy_parameters(query, reply) (0)
#else
/* Front left channel. */
#define PCM_CHANNEL_FL    1

/* Front right channel. */
#define PCM_CHANNEL_FR    2

/* Front center channel. */
#define PCM_CHANNEL_FC    3

/* Left surround channel.*/
#define PCM_CHANNEL_LS   4

/* Right surround channel.*/
#define PCM_CHANNEL_RS   5

/* Low frequency effect channel. */
#define PCM_CHANNEL_LFE  6

/* Left back channel; Rear left channel. */
#define PCM_CHANNEL_LB   8

/* Right back channel; Rear right channel. */
#define PCM_CHANNEL_RB   9

static int32_t afe_proxy_set_channel_mapping(struct audio_device *adev,
                                                     int channel_count)
{
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "Playback Channel Map";
    int set_values[8] = {0};
    int ret;
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

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }
    ALOGV("AFE: set mapping(%d %d %d %d %d %d %d %d) for channel:%d",
        set_values[0], set_values[1], set_values[2], set_values[3], set_values[4],
        set_values[5], set_values[6], set_values[7], channel_count);
    ret = mixer_ctl_set_array(ctl, set_values, channel_count);
    return ret;
}

int32_t audio_extn_set_afe_proxy_channel_mixer(struct audio_device *adev,
                                    int channel_count)
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
        ret = afe_proxy_set_channel_mapping(adev, channel_count);
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

int audio_extn_get_afe_proxy_parameters(struct str_parms *query,
                                        struct str_parms *reply)
{
    int ret, val;
    char value[32]={0};
    char *str = NULL;

    ret = str_parms_get_str(query, AUDIO_PARAMETER_CAN_OPEN_PROXY, value,
                            sizeof(value));
    if (ret >= 0) {
        if (audio_extn_usb_is_proxy_inuse())
            val = 0;
        else
            val = 1;
        str_parms_add_int(reply, AUDIO_PARAMETER_CAN_OPEN_PROXY, val);
    }

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

void audio_extn_set_parameters(struct audio_device *adev,
                               struct str_parms *parms)
{
   audio_extn_set_anc_parameters(adev, parms);
   audio_extn_set_fluence_parameters(adev, parms);
   audio_extn_set_afe_proxy_parameters(adev, parms);
   audio_extn_fm_set_parameters(adev, parms);
   audio_extn_listen_set_parameters(adev, parms);
   audio_extn_hfp_set_parameters(adev, parms);
   audio_extn_ddp_set_parameters(adev, parms);
}

void audio_extn_get_parameters(const struct audio_device *adev,
                              struct str_parms *query,
                              struct str_parms *reply)
{
    char *kv_pairs = NULL;
    audio_extn_get_afe_proxy_parameters(query, reply);
    audio_extn_get_fluence_parameters(adev, query, reply);
    get_active_offload_usecases(adev, query, reply);

    kv_pairs = str_parms_to_str(reply);
    ALOGD_IF(kv_pairs != NULL, "%s: returns %s", __func__, kv_pairs);
    free(kv_pairs);
}

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
