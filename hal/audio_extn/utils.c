/*
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2014 The Android Open Source Project
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
/* #define LOG_NDEBUG 0 */

#include <errno.h>
#include <cutils/properties.h>
#include <cutils/config_utils.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <cutils/str_parms.h>
#include <cutils/log.h>
#include <cutils/misc.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include "audio_extn.h"
#include "voice.h"

#define AUDIO_OUTPUT_POLICY_VENDOR_CONFIG_FILE "/vendor/etc/audio_output_policy.conf"

#define OUTPUTS_TAG "outputs"

#define DYNAMIC_VALUE_TAG "dynamic"
#define FLAGS_TAG "flags"
#define FORMATS_TAG "formats"
#define SAMPLING_RATES_TAG "sampling_rates"
#define BIT_WIDTH_TAG "bit_width"
#define APP_TYPE_TAG "app_type"

#define STRING_TO_ENUM(string) { #string, string }
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define BASE_TABLE_SIZE 64
#define MAX_BASEINDEX_LEN 256

struct string_to_enum {
    const char *name;
    uint32_t value;
};

const struct string_to_enum s_flag_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_DIRECT),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_PRIMARY),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_FAST),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_DEEP_BUFFER),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_NON_BLOCKING),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_HW_AV_SYNC),
#ifdef INCALL_MUSIC_ENABLED
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_INCALL_MUSIC),
#endif
#ifdef HDMI_PASSTHROUGH_ENABLED
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH),
#endif
};

const struct string_to_enum s_format_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_16_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_24_BIT_PACKED),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_8_24_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_8_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_MP3),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC),
    STRING_TO_ENUM(AUDIO_FORMAT_VORBIS),
    STRING_TO_ENUM(AUDIO_FORMAT_AMR_NB),
    STRING_TO_ENUM(AUDIO_FORMAT_AMR_WB),
    STRING_TO_ENUM(AUDIO_FORMAT_AC3),
    STRING_TO_ENUM(AUDIO_FORMAT_E_AC3),
#ifdef AUDIO_EXTN_FORMATS_ENABLED
    STRING_TO_ENUM(AUDIO_FORMAT_DTS),
    STRING_TO_ENUM(AUDIO_FORMAT_DTS_LBR),
    STRING_TO_ENUM(AUDIO_FORMAT_WMA),
    STRING_TO_ENUM(AUDIO_FORMAT_WMA_PRO),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_ADIF),
    STRING_TO_ENUM(AUDIO_FORMAT_AMR_WB_PLUS),
    STRING_TO_ENUM(AUDIO_FORMAT_EVRC),
    STRING_TO_ENUM(AUDIO_FORMAT_EVRCB),
    STRING_TO_ENUM(AUDIO_FORMAT_EVRCWB),
    STRING_TO_ENUM(AUDIO_FORMAT_QCELP),
    STRING_TO_ENUM(AUDIO_FORMAT_MP2),
    STRING_TO_ENUM(AUDIO_FORMAT_EVRCNW),
    STRING_TO_ENUM(AUDIO_FORMAT_FLAC),
    STRING_TO_ENUM(AUDIO_FORMAT_ALAC),
    STRING_TO_ENUM(AUDIO_FORMAT_APE),
    STRING_TO_ENUM(AUDIO_FORMAT_E_AC3_JOC),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_LC),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_HE_V1),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_HE_V2),
#endif
};

static char bTable[BASE_TABLE_SIZE] = {
            'A','B','C','D','E','F','G','H','I','J','K','L',
            'M','N','O','P','Q','R','S','T','U','V','W','X',
            'Y','Z','a','b','c','d','e','f','g','h','i','j',
            'k','l','m','n','o','p','q','r','s','t','u','v',
            'w','x','y','z','0','1','2','3','4','5','6','7',
            '8','9','+','/'
};

static uint32_t string_to_enum(const struct string_to_enum *table, size_t size,
                               const char *name)
{
    size_t i;
    for (i = 0; i < size; i++) {
        if (strcmp(table[i].name, name) == 0) {
            ALOGV("%s found %s", __func__, table[i].name);
            return table[i].value;
        }
    }
    return 0;
}

static audio_output_flags_t parse_flag_names(char *name)
{
    uint32_t flag = 0;
    char *last_r;
    char *flag_name = strtok_r(name, "|", &last_r);
    while (flag_name != NULL) {
        if (strlen(flag_name) != 0) {
            flag |= string_to_enum(s_flag_name_to_enum_table,
                               ARRAY_SIZE(s_flag_name_to_enum_table),
                               flag_name);
        }
        flag_name = strtok_r(NULL, "|", &last_r);
    }

    ALOGV("parse_flag_names: flag - %d", flag);
    return (audio_output_flags_t)flag;
}

static void parse_format_names(char *name, struct streams_output_cfg *so_info)
{
    struct stream_format *sf_info = NULL;
    char *last_r;
    char *str = strtok_r(name, "|", &last_r);

    if (str != NULL && strcmp(str, DYNAMIC_VALUE_TAG) == 0)
        return;

    list_init(&so_info->format_list);
    while (str != NULL) {
        audio_format_t format = (audio_format_t)string_to_enum(s_format_name_to_enum_table,
                                              ARRAY_SIZE(s_format_name_to_enum_table), str);
        ALOGV("%s: format - %d", __func__, format);
        if (format != 0) {
            sf_info = (struct stream_format *)calloc(1, sizeof(struct stream_format));
            if (sf_info == NULL)
                break; /* return whatever was parsed */

            sf_info->format = format;
            list_add_tail(&so_info->format_list, &sf_info->list);
        }
        str = strtok_r(NULL, "|", &last_r);
    }
}

static void parse_sample_rate_names(char *name, struct streams_output_cfg *so_info)
{
    struct stream_sample_rate *ss_info = NULL;
    uint32_t sample_rate = 48000;
    char *last_r;
    char *str = strtok_r(name, "|", &last_r);

    if (str != NULL && 0 == strcmp(str, DYNAMIC_VALUE_TAG))
        return;

    list_init(&so_info->sample_rate_list);
    while (str != NULL) {
        sample_rate = (uint32_t)strtol(str, (char **)NULL, 10);
        ALOGV("%s: sample_rate - %d", __func__, sample_rate);
        if (0 != sample_rate) {
            ss_info = (struct stream_sample_rate *)calloc(1, sizeof(struct stream_sample_rate));
            if (!ss_info) {
                ALOGE("%s: memory allocation failure", __func__);
                return;
            }
            ss_info->sample_rate = sample_rate;
            list_add_tail(&so_info->sample_rate_list, &ss_info->list);
        }
        str = strtok_r(NULL, "|", &last_r);
    }
}

static int parse_bit_width_names(char *name)
{
    int bit_width = 16;
    char *last_r;
    char *str = strtok_r(name, "|", &last_r);

    if (str != NULL && strcmp(str, DYNAMIC_VALUE_TAG))
        bit_width = (int)strtol(str, (char **)NULL, 10);

    ALOGV("%s: bit_width - %d", __func__, bit_width);
    return bit_width;
}

static int parse_app_type_names(void *platform, char *name)
{
    int app_type = platform_get_default_app_type(platform);
    char *last_r;
    char *str = strtok_r(name, "|", &last_r);

    if (str != NULL && strcmp(str, DYNAMIC_VALUE_TAG))
        app_type = (int)strtol(str, (char **)NULL, 10);

    ALOGV("%s: app_type - %d", __func__, app_type);
    return app_type;
}

static void update_streams_output_cfg_list(cnode *root, void *platform,
                                           struct listnode *streams_output_cfg_list)
{
    cnode *node = root->first_child;
    struct streams_output_cfg *so_info;

    ALOGV("%s", __func__);
    so_info = (struct streams_output_cfg *)calloc(1, sizeof(struct streams_output_cfg));

    if (!so_info) {
        ALOGE("failed to allocate mem for so_info list element");
        return;
    }

    while (node) {
        if (strcmp(node->name, FLAGS_TAG) == 0) {
            so_info->flags = parse_flag_names((char *)node->value);
        } else if (strcmp(node->name, FORMATS_TAG) == 0) {
            parse_format_names((char *)node->value, so_info);
        } else if (strcmp(node->name, SAMPLING_RATES_TAG) == 0) {
            so_info->app_type_cfg.sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
            parse_sample_rate_names((char *)node->value, so_info);
        } else if (strcmp(node->name, BIT_WIDTH_TAG) == 0) {
            so_info->app_type_cfg.bit_width = parse_bit_width_names((char *)node->value);
        } else if (strcmp(node->name, APP_TYPE_TAG) == 0) {
            so_info->app_type_cfg.app_type = parse_app_type_names(platform, (char *)node->value);
        }
        node = node->next;
    }
    list_add_tail(streams_output_cfg_list, &so_info->list);
}

static void load_output(cnode *root, void *platform,
                        struct listnode *streams_output_cfg_list)
{
    cnode *node = config_find(root, OUTPUTS_TAG);
    if (node == NULL) {
        ALOGE("%s: could not load output, node is NULL", __func__);
        return;
    }

    node = node->first_child;
    while (node) {
        ALOGV("%s: loading output %s", __func__, node->name);
        update_streams_output_cfg_list(node, platform, streams_output_cfg_list);
        node = node->next;
    }
}

static void send_app_type_cfg(void *platform, struct mixer *mixer,
                              struct listnode *streams_output_cfg_list)
{
    int app_type_cfg[MAX_LENGTH_MIXER_CONTROL_IN_INT] = {-1};
    int length = 0, i, num_app_types = 0;
    struct listnode *node;
    bool update;
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "App Type Config";
    struct streams_output_cfg *so_info;

    if (!mixer) {
        ALOGE("%s: mixer is null",__func__);
        return;
    }
    ctl = mixer_get_ctl_by_name(mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",__func__, mixer_ctl_name);
        return;
    }
    if (streams_output_cfg_list == NULL) {
        app_type_cfg[length++] = 1;
        app_type_cfg[length++] = platform_get_default_app_type(platform);
        app_type_cfg[length++] = 48000;
        app_type_cfg[length++] = 16;
        mixer_ctl_set_array(ctl, app_type_cfg, length);
        return;
    }

    app_type_cfg[length++] = num_app_types;
    list_for_each(node, streams_output_cfg_list) {
        so_info = node_to_item(node, struct streams_output_cfg, list);
        update = true;
        for (i=0; i<length; i=i+3) {
            if (app_type_cfg[i+1] == -1)
                break;
            else if (app_type_cfg[i+1] == so_info->app_type_cfg.app_type) {
                update = false;
                break;
            }
        }
        if (update && ((length + 3) <= MAX_LENGTH_MIXER_CONTROL_IN_INT)) {
            num_app_types += 1 ;
            app_type_cfg[length++] = so_info->app_type_cfg.app_type;
            app_type_cfg[length++] = so_info->app_type_cfg.sample_rate;
            app_type_cfg[length++] = so_info->app_type_cfg.bit_width;
        }
    }
    ALOGV("%s: num_app_types: %d", __func__, num_app_types);
    if (num_app_types) {
        app_type_cfg[0] = num_app_types;
        mixer_ctl_set_array(ctl, app_type_cfg, length);
    }
}

void audio_extn_utils_update_streams_output_cfg_list(void *platform,
                                       struct mixer *mixer,
                                       struct listnode *streams_output_cfg_list)
{
    cnode *root;
    char *data;

    ALOGV("%s", __func__);
    list_init(streams_output_cfg_list);
    data = (char *)load_file(AUDIO_OUTPUT_POLICY_VENDOR_CONFIG_FILE, NULL);
    if (data == NULL) {
        send_app_type_cfg(platform, mixer, NULL);
        ALOGE("%s: could not load output policy config file", __func__);
        return;
    }

    root = config_node("", "");
    if (root == NULL) {
        ALOGE("cfg_list, NULL config root");
        free(data);
        return;
    }

    config_load(root, data);
    load_output(root, platform, streams_output_cfg_list);

    send_app_type_cfg(platform, mixer, streams_output_cfg_list);

    config_free(root);
    free(data);
}

void audio_extn_utils_dump_streams_output_cfg_list(
                                       struct listnode *streams_output_cfg_list)
{
    struct listnode *node_i, *node_j;
    struct streams_output_cfg *so_info;
    struct stream_format *sf_info;
    struct stream_sample_rate *ss_info;
    ALOGV("%s", __func__);
    list_for_each(node_i, streams_output_cfg_list) {
        so_info = node_to_item(node_i, struct streams_output_cfg, list);
        ALOGV("%s: flags-%d, output_sample_rate-%d, output_bit_width-%d, app_type-%d",
               __func__, so_info->flags, so_info->app_type_cfg.sample_rate,
               so_info->app_type_cfg.bit_width, so_info->app_type_cfg.app_type);
        list_for_each(node_j, &so_info->format_list) {
            sf_info = node_to_item(node_j, struct stream_format, list);
            ALOGV("format-%x", sf_info->format);
        }
        list_for_each(node_j, &so_info->sample_rate_list) {
            ss_info = node_to_item(node_j, struct stream_sample_rate, list);
            ALOGV("sample rate-%d", ss_info->sample_rate);
        }
    }
}

void audio_extn_utils_release_streams_output_cfg_list(
                                       struct listnode *streams_output_cfg_list)
{
    struct listnode *node_i, *node_j;
    struct streams_output_cfg *so_info;

    ALOGV("%s", __func__);
    while (!list_empty(streams_output_cfg_list)) {
        node_i = list_head(streams_output_cfg_list);
        so_info = node_to_item(node_i, struct streams_output_cfg, list);
        while (!list_empty(&so_info->format_list)) {
            node_j = list_head(&so_info->format_list);
            list_remove(node_j);
            free(node_to_item(node_j, struct stream_format, list));
        }
        while (!list_empty(&so_info->sample_rate_list)) {
            node_j = list_head(&so_info->sample_rate_list);
            list_remove(node_j);
            free(node_to_item(node_j, struct stream_sample_rate, list));
        }
        list_remove(node_i);
        free(node_to_item(node_i, struct streams_output_cfg, list));
    }
}

static bool set_output_cfg(struct streams_output_cfg *so_info,
                           struct stream_app_type_cfg *app_type_cfg,
                           uint32_t sample_rate, uint32_t bit_width)
 {
    struct listnode *node_i;
    struct stream_sample_rate *ss_info;
    list_for_each(node_i, &so_info->sample_rate_list) {
        ss_info = node_to_item(node_i, struct stream_sample_rate, list);
        if ((sample_rate <= ss_info->sample_rate) &&
            (bit_width == so_info->app_type_cfg.bit_width)) {

            app_type_cfg->app_type = so_info->app_type_cfg.app_type;
            app_type_cfg->sample_rate = ss_info->sample_rate;
            app_type_cfg->bit_width = so_info->app_type_cfg.bit_width;
            ALOGV("%s app_type_cfg->app_type %d, app_type_cfg->sample_rate %d, app_type_cfg->bit_width %d",
                   __func__, app_type_cfg->app_type, app_type_cfg->sample_rate, app_type_cfg->bit_width);
            return true;
        }
    }
    /*
     * Reiterate through the list assuming dafault sample rate.
     * Handles scenario where input sample rate is higher
     * than all sample rates in list for the input bit width.
     */
    sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
    list_for_each(node_i, &so_info->sample_rate_list) {
        ss_info = node_to_item(node_i, struct stream_sample_rate, list);
        if ((sample_rate <= ss_info->sample_rate) &&
            (bit_width == so_info->app_type_cfg.bit_width)) {
            app_type_cfg->app_type = so_info->app_type_cfg.app_type;
            app_type_cfg->sample_rate = sample_rate;
            app_type_cfg->bit_width = so_info->app_type_cfg.bit_width;
            ALOGV("%s Assuming default sample rate. app_type_cfg->app_type %d, app_type_cfg->sample_rate %d, app_type_cfg->bit_width %d",
                   __func__, app_type_cfg->app_type, app_type_cfg->sample_rate, app_type_cfg->bit_width);
            return true;
        }
    }
    return false;
}

void audio_extn_utils_update_stream_app_type_cfg(void *platform,
                                  struct listnode *streams_output_cfg_list,
                                  audio_devices_t devices,
                                  audio_output_flags_t flags,
                                  audio_format_t format,
                                  uint32_t sample_rate,
                                  uint32_t bit_width,
                                  struct stream_app_type_cfg *app_type_cfg)
{
    struct listnode *node_i, *node_j;
    struct streams_output_cfg *so_info;
    struct stream_format *sf_info;

    if ((24 == bit_width) &&
        (devices & AUDIO_DEVICE_OUT_SPEAKER)) {
        int32_t bw = platform_get_snd_device_bit_width(SND_DEVICE_OUT_SPEAKER);
        if (-ENOSYS != bw)
            bit_width = (uint32_t)bw;
        sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        ALOGI("%s Allowing 24-bit playback on speaker ONLY at default sampling rate", __func__);
    }

    ALOGV("%s: flags: %x, format: %x sample_rate %d",
           __func__, flags, format, sample_rate);

    list_for_each(node_i, streams_output_cfg_list) {
        so_info = node_to_item(node_i, struct streams_output_cfg, list);
        if (so_info->flags == flags) {
            list_for_each(node_j, &so_info->format_list) {
                sf_info = node_to_item(node_j, struct stream_format, list);
                if (sf_info->format == format) {
                    if (set_output_cfg(so_info, app_type_cfg, sample_rate, bit_width))
                        return;
                }
            }
        }
    }
    list_for_each(node_i, streams_output_cfg_list) {
        so_info = node_to_item(node_i, struct streams_output_cfg, list);
        if (so_info->flags == AUDIO_OUTPUT_FLAG_PRIMARY) {
            ALOGV("Compatible output profile not found.");
            app_type_cfg->app_type = so_info->app_type_cfg.app_type;
            app_type_cfg->sample_rate = so_info->app_type_cfg.sample_rate;
            app_type_cfg->bit_width = so_info->app_type_cfg.bit_width;
            ALOGV("%s Default to primary output: App type: %d sample_rate %d",
                  __func__, so_info->app_type_cfg.app_type, app_type_cfg->sample_rate);
            return;
        }
    }
    ALOGW("%s: App type could not be selected. Falling back to default", __func__);
    app_type_cfg->app_type = platform_get_default_app_type(platform);
    app_type_cfg->sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
    app_type_cfg->bit_width = 16;
}

int audio_extn_utils_send_app_type_cfg(struct audio_usecase *usecase)
{
    char mixer_ctl_name[MAX_LENGTH_MIXER_CONTROL_IN_INT];
    int app_type_cfg[MAX_LENGTH_MIXER_CONTROL_IN_INT], len = 0, rc;
    struct stream_out *out;
    struct audio_device *adev;
    struct mixer_ctl *ctl;
    int pcm_device_id, acdb_dev_id, snd_device = usecase->out_snd_device;
    int32_t sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;

    ALOGV("%s", __func__);

    if (usecase->type != PCM_PLAYBACK) {
        ALOGV("%s: not a playback path, no need to cfg app type", __func__);
        rc = 0;
        goto exit_send_app_type_cfg;
    }
    if ((usecase->id != USECASE_AUDIO_PLAYBACK_DEEP_BUFFER) &&
        (usecase->id != USECASE_AUDIO_PLAYBACK_LOW_LATENCY) &&
        (usecase->id != USECASE_AUDIO_PLAYBACK_MULTI_CH) &&
        (!is_offload_usecase(usecase->id))) {
        ALOGV("%s: a playback path where app type cfg is not required %d", __func__, usecase->id);
        rc = 0;
        goto exit_send_app_type_cfg;
    }
    out = usecase->stream.out;
    adev = out->dev;

    snd_device = usecase->out_snd_device;

    pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Audio Stream %d App Type Cfg", pcm_device_id);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s", __func__,
              mixer_ctl_name);
        rc = -EINVAL;
        goto exit_send_app_type_cfg;
    }
    snd_device = (snd_device == SND_DEVICE_OUT_SPEAKER) ?
                 audio_extn_get_spkr_prot_snd_device(snd_device) : snd_device;
    acdb_dev_id = platform_get_snd_device_acdb_id(snd_device);
    if (acdb_dev_id < 0) {
        ALOGE("%s: Couldn't get the acdb dev id", __func__);
        rc = -EINVAL;
        goto exit_send_app_type_cfg;
    }

    if (usecase->stream.out->devices & AUDIO_DEVICE_OUT_SPEAKER) {
        sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    } else {
        sample_rate = out->app_type_cfg.sample_rate;
    }

    app_type_cfg[len++] = out->app_type_cfg.app_type;
    app_type_cfg[len++] = acdb_dev_id;
    if (((out->format == AUDIO_FORMAT_E_AC3) ||
        (out->format == AUDIO_FORMAT_E_AC3_JOC))
#ifdef HDMI_PASSTHROUGH_ENABLED
        &&(out->flags  & AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH)
#endif
        )
        app_type_cfg[len++] = sample_rate * 4;
    else
        app_type_cfg[len++] = sample_rate;
    mixer_ctl_set_array(ctl, app_type_cfg, len);
    ALOGI("%s app_type %d, acdb_dev_id %d, sample_rate %d",
           __func__, out->app_type_cfg.app_type, acdb_dev_id, sample_rate);
    rc = 0;
exit_send_app_type_cfg:
    return rc;
}

void audio_extn_utils_send_audio_calibration(struct audio_device *adev,
                                             struct audio_usecase *usecase)
{
    int type = usecase->type;

    if (type == PCM_PLAYBACK) {
        struct stream_out *out = usecase->stream.out;
        int snd_device = usecase->out_snd_device;
        snd_device = (snd_device == SND_DEVICE_OUT_SPEAKER) ?
                     audio_extn_get_spkr_prot_snd_device(snd_device) : snd_device;
        platform_send_audio_calibration(adev->platform, usecase->out_snd_device,
                                        out->app_type_cfg.app_type,
                                        out->app_type_cfg.sample_rate);
    }
    if ((type == PCM_HFP_CALL) || (type == PCM_CAPTURE)) {
        if ((type == PCM_CAPTURE) & voice_is_in_call_rec_stream(usecase->stream.in)) {
            snd_device_t incall_record_snd_device =
                        voice_get_incall_rec_snd_device(usecase->in_snd_device);
            platform_send_audio_calibration(adev->platform, incall_record_snd_device,
                                            platform_get_default_app_type(adev->platform),
                                            48000);
        } else {
            /* when app type is default. the sample rate is not used to send cal */
            platform_send_audio_calibration(adev->platform, usecase->in_snd_device,
                                            platform_get_default_app_type(adev->platform),
                                            48000);
        }
    }
}

// Base64 Encode and Decode
// Not all features supported. This must be used only with following conditions.
// Decode Modes: Support with and without padding
//         CRLF not handling. So no CRLF in string to decode.
// Encode Modes: Supports only padding
int b64decode(char *inp, int ilen, uint8_t* outp)
{
    int i, j, k, ii, num;
    int rem, pcnt;
    uint32_t res=0;
    uint8_t getIndex[MAX_BASEINDEX_LEN];
    uint8_t tmp, cflag;

    if(inp == NULL || outp == NULL || ilen <= 0) {
        ALOGE("[%s] received NULL pointer or zero length",__func__);
        return -1;
    }

    memset(getIndex, MAX_BASEINDEX_LEN-1, sizeof(getIndex));
    for(i=0;i<BASE_TABLE_SIZE;i++) {
        getIndex[(uint8_t)bTable[i]] = (uint8_t)i;
    }
    getIndex[(uint8_t)'=']=0;

    j=0;k=0;
    num = ilen/4;
    rem = ilen%4;
    if(rem==0)
        num = num-1;
    cflag=0;
    for(i=0; i<num; i++) {
        res=0;
        for(ii=0;ii<4;ii++) {
            res = res << 6;
            tmp = getIndex[(uint8_t)inp[j++]];
            res = res | tmp;
            cflag = cflag | tmp;
        }
        outp[k++] = (res >> 16)&0xFF;
        outp[k++] = (res >> 8)&0xFF;
        outp[k++] = res & 0xFF;
    }

    // Handle last bytes special
    pcnt=0;
    if(rem == 0) {
        //With padding or full data
        res = 0;
        for(ii=0;ii<4;ii++) {
            if(inp[j] == '=')
                pcnt++;
            res = res << 6;
            tmp = getIndex[(uint8_t)inp[j++]];
            res = res | tmp;
            cflag = cflag | tmp;
        }
        outp[k++] = res >> 16;
        if(pcnt == 2)
            goto done;
        outp[k++] = (res>>8)&0xFF;
        if(pcnt == 1)
            goto done;
        outp[k++] = res&0xFF;
    } else {
        //without padding
        res = 0;
        for(i=0;i<rem;i++) {
            res = res << 6;
            tmp = getIndex[(uint8_t)inp[j++]];
            res = res | tmp;
            cflag = cflag | tmp;
        }
        for(i=rem;i<4;i++) {
            res = res << 6;
            pcnt++;
        }
        outp[k++] = res >> 16;
        if(pcnt == 2)
            goto done;
        outp[k++] = (res>>8)&0xFF;
        if(pcnt == 1)
            goto done;
        outp[k++] = res&0xFF;
    }
done:
    if(cflag == 0xFF) {
        ALOGE("[%s] base64 decode failed. Invalid character found %s",
            __func__, inp);
        return 0;
    }
    return k;
}

int b64encode(uint8_t *inp, int ilen, char* outp)
{
    int i,j,k, num;
    int rem=0;
    uint32_t res=0;

    if(inp == NULL || outp == NULL || ilen<=0) {
        ALOGE("[%s] received NULL pointer or zero input length",__func__);
        return -1;
    }

    num = ilen/3;
    rem = ilen%3;
    j=0;k=0;
    for(i=0; i<num; i++) {
        //prepare index
        res = inp[j++]<<16;
        res = res | inp[j++]<<8;
        res = res | inp[j++];
        //get output map from index
        outp[k++] = (char) bTable[(res>>18)&0x3F];
        outp[k++] = (char) bTable[(res>>12)&0x3F];
        outp[k++] = (char) bTable[(res>>6)&0x3F];
        outp[k++] = (char) bTable[res&0x3F];
    }

    switch(rem) {
        case 1:
            res = inp[j++]<<16;
            outp[k++] = (char) bTable[res>>18];
            outp[k++] = (char) bTable[(res>>12)&0x3F];
            //outp[k++] = '=';
            //outp[k++] = '=';
            break;
        case 2:
            res = inp[j++]<<16;
            res = res | inp[j++]<<8;
            outp[k++] = (char) bTable[res>>18];
            outp[k++] = (char) bTable[(res>>12)&0x3F];
            outp[k++] = (char) bTable[(res>>6)&0x3F];
            //outp[k++] = '=';
            break;
        default:
            break;
    }
    outp[k] = '\0';
    return k;
}
