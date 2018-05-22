/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define LOG_TAG "audio_hw_waves"
/*#define LOG_NDEBUG 0*/

#include <audio_hw.h>
#include <cutils/str_parms.h>
#include <dlfcn.h>
#include <log/log.h>
#include <math.h>
#include <platform_api.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <system/audio.h>
#include <unistd.h>

#include "audio_extn.h"
#include "maxxaudio.h"

#define LIB_MA_PARAM "libmaxxaudioqdsp.so"
#define LIB_MA_PATH "vendor/lib/"
#define PRESET_PATH "/vendor/etc"
#define MPS_BASE_STRING "default"
#define USER_PRESET_PATH ""
#define CONFIG_PATH "/vendor/etc/maxx_conf.ini"
#define CAL_PRESIST_STR "cal_persist"
#define CAL_SAMPLERATE_STR "cal_samplerate"

#define MA_QDSP_PARAM_INIT "maxxaudio_qdsp_initialize"
#define MA_QDSP_PARAM_DEINIT "maxxaudio_qdsp_uninitialize"
#define MA_QDSP_SET_LR_SWAP "maxxaudio_qdsp_set_lr_swap"
#define MA_QDSP_SET_MODE "maxxaudio_qdsp_set_sound_mode"
#define MA_QDSP_SET_VOL "maxxaudio_qdsp_set_volume"
#define MA_QDSP_SET_VOLT "maxxaudio_qdsp_set_volume_table"

#define SUPPORT_DEV "Blackbird"
#define SUPPORTED_USB 0x01

struct ma_audio_cal_settings {
    int app_type;
    audio_devices_t device;
};

struct ma_state {
    float vol;
    bool active;
};

typedef enum MA_STREAM_TYPE {
    STREAM_MIN_STREAM_TYPES,
    STREAM_VOICE = STREAM_MIN_STREAM_TYPES,
    STREAM_SYSTEM,
    STREAM_RING,
    STREAM_MUSIC,
    STREAM_ALARM,
    STREAM_NOTIFICATION ,
    STREAM_MAX_TYPES,
} ma_stream_type_t;

typedef enum MA_CMD {
    MA_CMD_VOL,
    MA_CMD_SWAP_ENABLE,
    MA_CMD_SWAP_DISABLE,
} ma_cmd_t;

typedef void *ma_audio_cal_handle_t;
typedef int (*set_audio_cal_t)(const char *);

typedef bool (*ma_param_init_t)(ma_audio_cal_handle_t *, const char *,
                                const char *, const char *, set_audio_cal_t);

typedef bool (*ma_param_deinit_t)(ma_audio_cal_handle_t *);

typedef bool (*ma_set_lr_swap_t)(ma_audio_cal_handle_t,
                                 const struct ma_audio_cal_settings *, bool);

typedef bool (*ma_set_sound_mode_t)(ma_audio_cal_handle_t,
                                    const struct ma_audio_cal_settings *,
                                    unsigned int);

typedef bool (*ma_set_volume_t)(ma_audio_cal_handle_t,
                                const struct ma_audio_cal_settings *, double);

typedef bool (*ma_set_volume_table_t)(ma_audio_cal_handle_t,
                                      const struct ma_audio_cal_settings *,
                                      size_t, struct ma_state *);

struct ma_platform_data {
    void *waves_handle;
    void *platform;
    pthread_mutex_t lock;
    ma_param_init_t          ma_param_init;
    ma_param_deinit_t        ma_param_deinit;
    ma_set_lr_swap_t         ma_set_lr_swap;
    ma_set_sound_mode_t      ma_set_sound_mode;
    ma_set_volume_t          ma_set_volume;
    ma_set_volume_table_t    ma_set_volume_table;
};

ma_audio_cal_handle_t g_ma_audio_cal_handle = NULL;
static uint16_t g_supported_dev = 0;
static struct ma_state ma_cur_state_table[STREAM_MAX_TYPES];
static struct ma_platform_data *my_data = NULL;

static int set_audio_cal(const char *audio_cal)
{
    ALOGV("set_audio_cal: %s", audio_cal);

    return platform_set_parameters(my_data->platform,
                                   str_parms_create_str(audio_cal));
}

static bool ma_set_lr_swap_l(
    const struct ma_audio_cal_settings *audio_cal_settings, bool swap)
{
    return my_data->ma_set_lr_swap(g_ma_audio_cal_handle,
                                   audio_cal_settings, swap);
}

static bool ma_set_sound_mode_l(
    const struct ma_audio_cal_settings *audio_cal_settings, int sound_mode)
{
    return my_data->ma_set_sound_mode(g_ma_audio_cal_handle,
                                      audio_cal_settings, sound_mode);
}

static bool ma_set_volume_l(
    const struct ma_audio_cal_settings *audio_cal_settings, double volume)
{
    return my_data->ma_set_volume(g_ma_audio_cal_handle, audio_cal_settings,
                                  volume);
}

static bool ma_set_volume_table_l(
    const struct ma_audio_cal_settings *audio_cal_settings,
    size_t num_streams, struct ma_state *volume_table)
{
    return my_data->ma_set_volume_table(g_ma_audio_cal_handle,
                                        audio_cal_settings, num_streams,
                                        volume_table);
}

static inline bool valid_usecase(struct audio_usecase *usecase)
{
    if ((usecase->type == PCM_PLAYBACK) &&
        /* supported usecases */
        ((usecase->id == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER) ||
         (usecase->id == USECASE_AUDIO_PLAYBACK_LOW_LATENCY) ||
         (usecase->id == USECASE_AUDIO_PLAYBACK_OFFLOAD)) &&
        /* support devices */
        ((usecase->devices & AUDIO_DEVICE_OUT_SPEAKER) ||
         (usecase->devices & AUDIO_DEVICE_OUT_SPEAKER_SAFE) ||
         /* TODO: enable A2DP when it is ready */
         (usecase->devices & AUDIO_DEVICE_OUT_ALL_USB)))

        return true;

    ALOGV("%s: not support type %d usecase %d device %d",
           __func__, usecase->type, usecase->id, usecase->devices);

    return false;
}

// already hold lock
static inline bool is_active()
{
    ma_stream_type_t i = 0;

    for (i = 0; i < STREAM_MAX_TYPES; i++)
        if (ma_cur_state_table[i].active &&
                (ma_cur_state_table[i].vol != 0))
            return true;

    return false;
}

static bool check_and_send_all_audio_cal(struct audio_device *adev, ma_cmd_t cmd)
{
    int i = 0;
    bool ret = false;
    float vol = 0;
    struct listnode *node;
    struct audio_usecase *usecase;
    struct ma_audio_cal_settings *ma_cal = NULL;

    // alloct
    ma_cal = (struct ma_audio_cal_settings *)malloc(sizeof(struct ma_audio_cal_settings));

    if (ma_cal == NULL) {
        ALOGE("%s: ma_cal alloct fail", __func__);
        return ret;
    }

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (valid_usecase(usecase)) {
            ma_cal->app_type = usecase->stream.out->app_type_cfg.app_type;
            ma_cal->device = usecase->stream.out->devices;
            ALOGV("%s: send usecase(%d) app_type(%d) device(%d)",
                      __func__, usecase->id, ma_cal->app_type, ma_cal->device);

            switch (cmd) {
                case MA_CMD_VOL:
                    ret = ma_set_volume_table_l(ma_cal, STREAM_MAX_TYPES,
                                                ma_cur_state_table);
                    if (ret)
                        ALOGV("Waves: ma_set_volume_table_l success");
                    else
                        ALOGE("Waves: ma_set_volume_table_l %f returned with error.", vol);

                    ALOGV("%s: send volume table === Start", __func__);
                    for (i = 0; i < STREAM_MAX_TYPES; i++)
                        ALOGV("%s: stream(%d) volume(%f) active(%s)", __func__,
                              i, ma_cur_state_table[i].vol,
                              ma_cur_state_table[i].active ? "T" : "F");
                    ALOGV("%s: send volume table === End", __func__);
                    break;
                case MA_CMD_SWAP_ENABLE:
                    ret = ma_set_lr_swap_l(ma_cal, true);
                    if (ret)
                        ALOGV("Waves: ma_set_lr_swap_l enable returned with success.");
                    else
                        ALOGE("Waves: ma_set_lr_swap_l enable returned with error.");
                    break;
                case MA_CMD_SWAP_DISABLE:
                    ret = ma_set_lr_swap_l(ma_cal, false);
                    if (ret)
                        ALOGV("Waves: ma_set_lr_swap_l disable returned with success.");
                    else
                        ALOGE("Waves: ma_set_lr_swap_l disable returned with error.");
                    break;
                default:
                    ALOGE("%s: unsupported cmd %d", __func__, cmd);
            }
        }
    }
    free(ma_cal);

    return ret;
}

static bool find_sup_dev(char *name)
{
    char *token;
    const char s[2] = ",";
    bool ret = false;
    char sup_devs[128];

    // the rule of comforming suppored dev's name
    // 1. Both string len are equal
    // 2. Both string content are equal

    strncpy(sup_devs, SUPPORT_DEV, sizeof(sup_devs));
    token = strtok(sup_devs, s);
    while (token != NULL) {
        if (strncmp(token, name, strlen(token)) == 0 &&
            strlen(token) == strlen(name)) {
            ALOGD("%s: support dev %s", __func__, token);
            ret = true;
            break;
        }
        token = strtok(NULL, s);
    }

    return ret;
}

static void ma_set_swap_l(struct audio_device *adev, bool enable)
{
    // do platform LR swap if it enables on Waves effect
    // but there is no Waves implementation
    if (!my_data) {
        platform_check_and_set_swap_lr_channels(adev, enable);
        ALOGV("%s: maxxaudio isn't initialized.", __func__);
        return;
    }

    if (enable)
        check_and_send_all_audio_cal(adev, MA_CMD_SWAP_ENABLE);
    else
        check_and_send_all_audio_cal(adev, MA_CMD_SWAP_DISABLE);
}

static void ma_support_usb(bool enable, int card)
{
    char path[128];
    char id[32];
    int ret = 0;
    int32_t fd = -1;
    char *idd;

    if (enable) {
        ret = snprintf(path, sizeof(path), "/proc/asound/card%u/id", card);
        if (ret < 0) {
            ALOGE("%s: failed on snprintf (%d) to path %s\n",
                  __func__, ret, path);
            goto done;
        }
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            ALOGE("%s: error failed to open id file %s error: %d\n",
                  __func__, path, errno);
            goto done;
        }
        if (read(fd, id, sizeof(id)) < 0) {
            ALOGE("%s: file read error", __func__);
            goto done;
        }
        //replace '\n' to '\0'
        idd = strtok(id, "\n");

        if (find_sup_dev(idd)) {
            ALOGV("%s: support device name is %s", __func__, id);
            g_supported_dev |= SUPPORTED_USB;
        } else
            ALOGV("%s: device %s isn't found from %s", __func__, id, SUPPORT_DEV);
    } else {
        g_supported_dev &= ~SUPPORTED_USB;
    }

done:
    if (fd >= 0) close(fd);
}

// adev_init lock held
void audio_extn_ma_init(void *platform)
{
    ma_stream_type_t i = 0;
    int ret = 0;
    char lib_path[128] = {0};
    char mps_path[128] = {0};
    struct snd_card_split *snd_split_handle = NULL;
    snd_split_handle = audio_extn_get_snd_card_split();

    if (platform == NULL) {
        ALOGE("%s: platform is NULL", __func__);
        goto error;
    }

    if (my_data) { free(my_data); }
    my_data = calloc(1, sizeof(struct ma_platform_data));
    if (my_data == NULL) {
        ALOGE("%s: ma_cal alloct fail", __func__);
        goto error;
    }

    pthread_mutex_init(&my_data->lock, NULL);

    my_data->platform = platform;
    ret = snprintf(lib_path, sizeof(lib_path), "%s/%s", LIB_MA_PATH, LIB_MA_PARAM);
    if (ret < 0) {
        ALOGE("%s: snprintf failed for lib %s, ret %d", __func__, LIB_MA_PARAM, ret);
        goto error;
    }

    my_data->waves_handle = dlopen(lib_path, RTLD_NOW);
    if (my_data->waves_handle == NULL) {
         ALOGE("%s: DLOPEN failed for %s", __func__, LIB_MA_PARAM);
         goto error;
    } else {
         ALOGV("%s: DLOPEN successful for %s", __func__, LIB_MA_PARAM);

         my_data->ma_param_init = (ma_param_init_t)dlsym(my_data->waves_handle,
                                   MA_QDSP_PARAM_INIT);
         if (!my_data->ma_param_init) {
             ALOGE("%s: dlsym error %s for ma_param_init", __func__, dlerror());
             goto error;
         }

         my_data->ma_param_deinit = (ma_param_deinit_t)dlsym(
                                     my_data->waves_handle, MA_QDSP_PARAM_DEINIT);
         if (!my_data->ma_param_deinit) {
             ALOGE("%s: dlsym error %s for ma_param_deinit", __func__, dlerror());
             goto error;
         }

         my_data->ma_set_lr_swap = (ma_set_lr_swap_t)dlsym(my_data->waves_handle,
                                    MA_QDSP_SET_LR_SWAP);
         if (!my_data->ma_set_lr_swap) {
             ALOGE("%s: dlsym error %s for ma_set_lr_swap", __func__, dlerror());
             goto error;
         }

         my_data->ma_set_sound_mode = (ma_set_sound_mode_t)dlsym(
                                       my_data->waves_handle, MA_QDSP_SET_MODE);
         if (!my_data->ma_set_sound_mode) {
             ALOGE("%s: dlsym error %s for ma_set_sound_mode", __func__, dlerror());
             goto error;
         }

         my_data->ma_set_volume = (ma_set_volume_t)dlsym(my_data->waves_handle,
                                   MA_QDSP_SET_VOL);
         if (!my_data->ma_set_volume) {
             ALOGE("%s: dlsym error %s for ma_set_volume", __func__, dlerror());
             goto error;
         }

         my_data->ma_set_volume_table = (ma_set_volume_table_t)dlsym(
                                         my_data->waves_handle, MA_QDSP_SET_VOLT);
         if (!my_data->ma_set_volume_table) {
             ALOGE("%s: dlsym error %s for ma_set_volume_table", __func__, dlerror());
             goto error;
         }
    }

    /* get preset table */
    if (snd_split_handle == NULL) {
        snprintf(mps_path, sizeof(mps_path), "%s/%s.mps",
                 PRESET_PATH, MPS_BASE_STRING);
    } else {
        snprintf(mps_path, sizeof(mps_path), "%s/%s_%s.mps",
                 PRESET_PATH, MPS_BASE_STRING, snd_split_handle->form_factor);
    }

    /* check file */
    if (access(mps_path, F_OK) < 0) {
        ALOGW("%s: file %s isn't existed.", __func__, mps_path);
        goto error;
    } else
        ALOGD("%s: Loading mps file: %s", __func__, mps_path);

    /* TODO: check user preset table once the feature is enabled
    if (access(USER_PRESET_PATH, F_OK) < 0 ){
        ALOGW("%s: file %s isn't existed.", __func__, USER_PRESET_PATH);
        goto error;
    }
    */
    if (access(CONFIG_PATH, F_OK) < 0) {
        ALOGW("%s: file %s isn't existed.", __func__, CONFIG_PATH);
        goto error;
    }

    /* init ma parameter */
    if (my_data->ma_param_init(&g_ma_audio_cal_handle,
                               mps_path,
                               USER_PRESET_PATH, /* unused */
                               CONFIG_PATH,
                               &set_audio_cal)) {
        if (!g_ma_audio_cal_handle) {
            ALOGE("%s: ma parameters initialize failed", __func__);
            my_data->ma_param_deinit(&g_ma_audio_cal_handle);
            goto error;
        }
        ALOGD("%s: ma parameters initialize successful", __func__);
    } else {
        ALOGE("%s: ma parameters initialize failed", __func__);
        goto error;
    }

    /* init volume table */
    for (i = 0; i < STREAM_MAX_TYPES; i++) {
        ma_cur_state_table[i].vol = 0.0;
        ma_cur_state_table[i].active = false;
    }

    return;

error:
    if (my_data) { free(my_data); }
    my_data = NULL;
}

//adev_init lock held
void audio_extn_ma_deinit()
{
    if (my_data) {
        /* deinit ma parameter */
        if (my_data->ma_param_deinit &&
            my_data->ma_param_deinit(&g_ma_audio_cal_handle))
            ALOGD("%s: ma parameters uninitialize successful", __func__);
        else
            ALOGD("%s: ma parameters uninitialize failed", __func__);

        pthread_mutex_destroy(&my_data->lock);
        free(my_data);
        my_data = NULL;
    }
}

// adev_init and adev lock held
bool audio_extn_ma_set_state(struct audio_device *adev, int stream_type,
                             float vol, bool active)
{
    bool ret = false;
    ma_stream_type_t stype = (ma_stream_type_t)stream_type;

    ALOGV("%s: stream[%d] vol[%f] active[%s]",
          __func__, stream_type, vol, active ? "true" : "false");

    if (!my_data) {
        ALOGV("%s: maxxaudio isn't initialized.", __func__);
        return ret;
    }

    // update condition
    // 1. start track: active and volume isn't zero
    // 2. stop track: no tracks are active
    if ((active && vol != 0) ||
        (!active)) {
        pthread_mutex_lock(&my_data->lock);

        ma_cur_state_table[stype].vol = vol;
        ma_cur_state_table[stype].active = active;
        if (is_active())
            ret = check_and_send_all_audio_cal(adev, MA_CMD_VOL);

        pthread_mutex_unlock(&my_data->lock);
    }

    return ret;
}

void audio_extn_ma_set_device(struct audio_usecase *usecase)
{
    int i = 0;
    int u_index = -1;
    float vol = 0;
    struct ma_audio_cal_settings *ma_cal = NULL;

    if (!my_data) {
        ALOGV("%s: maxxaudio isn't initialized.", __func__);
        return;
    }

    if (!valid_usecase(usecase)) {
        ALOGV("%s: %d is not supported usecase", __func__, usecase->id);
        return;
    }

    ma_cal = (struct ma_audio_cal_settings *)malloc(sizeof(struct ma_audio_cal_settings));

    /* update audio_cal and send it */
    if (ma_cal != NULL){
        ma_cal->app_type = usecase->stream.out->app_type_cfg.app_type;
        ma_cal->device = usecase->stream.out->devices;
        ALOGV("%s: send usecase(%d) app_type(%d) device(%d)",
                      __func__, usecase->id, ma_cal->app_type, ma_cal->device);

        pthread_mutex_lock(&my_data->lock);

        if (is_active()) {
            ALOGV("%s: send volume table === Start", __func__);
            for (i = 0; i < STREAM_MAX_TYPES; i++)
                ALOGV("%s: stream(%d) volume(%f) active(%s)", __func__, i,
                    ma_cur_state_table[i].vol,
                    ma_cur_state_table[i].active ? "T" : "F");
            ALOGV("%s: send volume table === End", __func__);

            if (!ma_set_volume_table_l(ma_cal,
                                       STREAM_MAX_TYPES,
                                       ma_cur_state_table))
                ALOGE("Waves: ma_set_volume_table_l %f returned with error.", vol);
            else
                ALOGV("Waves: ma_set_volume_table_l success");

        }
        pthread_mutex_unlock(&my_data->lock);
        free(ma_cal);
    } else {
        ALOGE("%s: ma_cal alloct fail", __func__);
    }
}

void audio_extn_ma_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms)
{
    int ret;
    bool ret_b;
    int val;
    char value[128];

    // do LR swap and usb recognition
    ret = str_parms_get_int(parms, "rotation", &val);
    if (ret >= 0) {
        switch (val) {
        case 270:
            ma_set_swap_l(adev, true);
            break;
        case 0:
        case 90:
        case 180:
            ma_set_swap_l(adev, false);
            break;
        }
    }

    // check connect status
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value,
                            sizeof(value));
    if (ret >= 0) {
        audio_devices_t device = (audio_devices_t)strtoul(value, NULL, 10);
        if (audio_is_usb_out_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0) {
                const int card = atoi(value);
                ma_support_usb(true, card);
            }
        }
    }

    // check disconnect status
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                            sizeof(value));
    if (ret >= 0) {
        audio_devices_t device = (audio_devices_t)strtoul(value, NULL, 10);
        if (audio_is_usb_out_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0) {
                const int card = atoi(value);
                ma_support_usb(false, card /*useless*/);
            }
        }
    }
}

bool audio_extn_ma_supported_usb()
{
    ALOGV("%s: current support 0x%x", __func__, g_supported_dev);
    return (g_supported_dev & SUPPORTED_USB) ? true : false;
}
