/* Copyright (c) 2013-2014, 2016-2018 The Linux Foundation. All rights reserved.
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
 *
 */
#define LOG_TAG "soundtrigger"
/* #define LOG_NDEBUG 0 */
#define LOG_NDDEBUG 0

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <cutils/log.h>
#include <unistd.h>
#include "audio_hw.h"
#include "audio_extn.h"
#include "platform.h"
#include "platform_api.h"

/*-------------------- Begin: AHAL-STHAL Interface ---------------------------*/
/*
 * Maintain the proprietary interface between AHAL and STHAL locally to avoid
 * the compilation dependency of interface header file from STHAL.
 */

#define MAKE_HAL_VERSION(maj, min) ((((maj) & 0xff) << 8) | ((min) & 0xff))
#define MAJOR_VERSION(ver) (((ver) & 0xff00) >> 8)
#define MINOR_VERSION(ver) ((ver) & 0x00ff)

/* Proprietary interface version used for compatibility with STHAL */
#define STHAL_PROP_API_VERSION_1_0 MAKE_HAL_VERSION(1, 0)
#define STHAL_PROP_API_CURRENT_VERSION STHAL_PROP_API_VERSION_1_0

#define ST_EVENT_CONFIG_MAX_STR_VALUE 32
#define ST_DEVICE_HANDSET_MIC 1

typedef enum {
    ST_EVENT_SESSION_REGISTER,
    ST_EVENT_SESSION_DEREGISTER,
    ST_EVENT_START_KEEP_ALIVE,
    ST_EVENT_STOP_KEEP_ALIVE,
} sound_trigger_event_type_t;

typedef enum {
    AUDIO_EVENT_CAPTURE_DEVICE_INACTIVE,
    AUDIO_EVENT_CAPTURE_DEVICE_ACTIVE,
    AUDIO_EVENT_PLAYBACK_STREAM_INACTIVE,
    AUDIO_EVENT_PLAYBACK_STREAM_ACTIVE,
    AUDIO_EVENT_STOP_LAB,
    AUDIO_EVENT_SSR,
    AUDIO_EVENT_NUM_ST_SESSIONS,
    AUDIO_EVENT_READ_SAMPLES,
    AUDIO_EVENT_DEVICE_CONNECT,
    AUDIO_EVENT_DEVICE_DISCONNECT,
    AUDIO_EVENT_SVA_EXEC_MODE,
    AUDIO_EVENT_SVA_EXEC_MODE_STATUS,
    AUDIO_EVENT_CAPTURE_STREAM_INACTIVE,
    AUDIO_EVENT_CAPTURE_STREAM_ACTIVE,
    AUDIO_EVENT_BATTERY_STATUS_CHANGED,
} audio_event_type_t;

typedef enum {
    USECASE_TYPE_PCM_PLAYBACK,
    USECASE_TYPE_PCM_CAPTURE,
    USECASE_TYPE_VOICE_CALL,
    USECASE_TYPE_VOIP_CALL,
} audio_stream_usecase_type_t;

typedef enum {
    SND_CARD_STATUS_OFFLINE,
    SND_CARD_STATUS_ONLINE,
    CPE_STATUS_OFFLINE,
    CPE_STATUS_ONLINE
} ssr_event_status_t;

struct sound_trigger_session_info {
    void* p_ses; /* opaque pointer to st_session obj */
    int capture_handle;
    struct pcm *pcm;
    struct pcm_config config;
};

struct audio_read_samples_info {
    struct sound_trigger_session_info *ses_info;
    void *buf;
    size_t num_bytes;
};

struct audio_hal_usecase {
    audio_stream_usecase_type_t type;
};

struct sound_trigger_event_info {
    struct sound_trigger_session_info st_ses;
};
typedef struct sound_trigger_event_info sound_trigger_event_info_t;

struct sound_trigger_device_info {
    int device;
};

struct audio_event_info {
    union {
        ssr_event_status_t status;
        int value;
        struct sound_trigger_session_info ses_info;
        struct audio_read_samples_info aud_info;
        char str_value[ST_EVENT_CONFIG_MAX_STR_VALUE];
        struct audio_hal_usecase usecase;
    } u;
    struct sound_trigger_device_info device_info;
};
typedef struct audio_event_info audio_event_info_t;
/* STHAL callback which is called by AHAL */
typedef int (*sound_trigger_hw_call_back_t)(audio_event_type_t,
                                  struct audio_event_info*);

/*---------------- End: AHAL-STHAL Interface ----------------------------------*/

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_SND_TRIGGER
#include <log_utils.h>
#endif

#define XSTR(x) STR(x)
#define STR(x) #x
#define MAX_LIBRARY_PATH 100

#define DLSYM(handle, ptr, symbol, err) \
do {\
    ptr = dlsym(handle, #symbol); \
    if (ptr == NULL) {\
        ALOGW("%s: %s not found. %s", __func__, #symbol, dlerror());\
        err = -ENODEV;\
    }\
} while(0)

#ifdef __LP64__
#define SOUND_TRIGGER_LIBRARY_PATH "/vendor/lib64/hw/sound_trigger.primary.%s.so"
#else
#define SOUND_TRIGGER_LIBRARY_PATH "/vendor/lib/hw/sound_trigger.primary.%s.so"
#endif

/*
 * Current proprietary API version used by AHAL. Queried by STHAL
 * for compatibility check with AHAL
 */
const unsigned int sthal_prop_api_version = STHAL_PROP_API_CURRENT_VERSION;

struct sound_trigger_info  {
    struct sound_trigger_session_info st_ses;
    bool lab_stopped;
    struct listnode list;
};

struct sound_trigger_audio_device {
    void *lib_handle;
    struct audio_device *adev;
    sound_trigger_hw_call_back_t st_callback;
    struct listnode st_ses_list;
    pthread_mutex_t lock;
    unsigned int sthal_prop_api_version;
};

static struct sound_trigger_audio_device *st_dev;

#if LINUX_ENABLED
static void get_library_path(char *lib_path)
{
    snprintf(lib_path, MAX_LIBRARY_PATH,
             "sound_trigger.primary.default.so");
}
#else
static void get_library_path(char *lib_path)
{
    snprintf(lib_path, MAX_LIBRARY_PATH,
             "/vendor/lib/hw/sound_trigger.primary.%s.so",
             XSTR(SOUND_TRIGGER_PLATFORM_NAME));
}
#endif

static struct sound_trigger_info *
get_sound_trigger_info(int capture_handle)
{
    struct sound_trigger_info  *st_ses_info = NULL;
    struct listnode *node;
    ALOGV("%s: list empty %d capture_handle %d", __func__,
           list_empty(&st_dev->st_ses_list), capture_handle);
    list_for_each(node, &st_dev->st_ses_list) {
        st_ses_info = node_to_item(node, struct sound_trigger_info , list);
        if (st_ses_info->st_ses.capture_handle == capture_handle)
            return st_ses_info;
    }
    return NULL;
}

static int populate_usecase(struct audio_hal_usecase *usecase,
                       struct audio_usecase *uc_info)
{
    int status  = 0;

    switch(uc_info->type) {
    case PCM_PLAYBACK:
        if (uc_info->id == USECASE_AUDIO_PLAYBACK_VOIP)
            usecase->type = USECASE_TYPE_VOIP_CALL;
        else
            usecase->type = USECASE_TYPE_PCM_PLAYBACK;
        break;

    case PCM_CAPTURE:
        if (uc_info->id == USECASE_AUDIO_RECORD_VOIP)
            usecase->type = USECASE_TYPE_VOIP_CALL;
        else
            usecase->type = USECASE_TYPE_PCM_CAPTURE;
        break;

    case VOICE_CALL:
        usecase->type = USECASE_TYPE_VOICE_CALL;
        break;

    default:
        ALOGE("%s: unsupported usecase type %d", __func__, uc_info->type);
        status = -EINVAL;
    }
    return status;
}

static void stdev_snd_mon_cb(void * stream __unused, struct str_parms * parms)
{
    if (!parms)
        return;

    audio_extn_sound_trigger_set_parameters(NULL, parms);
    return;
}

int audio_hw_call_back(sound_trigger_event_type_t event,
                       sound_trigger_event_info_t* config)
{
    int status = 0;
    struct sound_trigger_info  *st_ses_info;

    if (!st_dev)
       return -EINVAL;

    pthread_mutex_lock(&st_dev->lock);
    switch (event) {
    case ST_EVENT_SESSION_REGISTER:
        if (!config) {
            ALOGE("%s: NULL config", __func__);
            status = -EINVAL;
            break;
        }
        st_ses_info= calloc(1, sizeof(struct sound_trigger_info ));
        if (!st_ses_info) {
            ALOGE("%s: st_ses_info alloc failed", __func__);
            status = -ENOMEM;
            break;
        }
        memcpy(&st_ses_info->st_ses, &config->st_ses, sizeof (struct sound_trigger_session_info));
        ALOGV("%s: add capture_handle %d st session opaque ptr %p", __func__,
              st_ses_info->st_ses.capture_handle, st_ses_info->st_ses.p_ses);
        list_add_tail(&st_dev->st_ses_list, &st_ses_info->list);
        break;

    case ST_EVENT_START_KEEP_ALIVE:
        pthread_mutex_unlock(&st_dev->lock);
        pthread_mutex_lock(&st_dev->adev->lock);
        audio_extn_keep_alive_start(KEEP_ALIVE_OUT_PRIMARY);
        pthread_mutex_unlock(&st_dev->adev->lock);
        goto done;

    case ST_EVENT_SESSION_DEREGISTER:
        if (!config) {
            ALOGE("%s: NULL config", __func__);
            status = -EINVAL;
            break;
        }
        st_ses_info = get_sound_trigger_info(config->st_ses.capture_handle);
        if (!st_ses_info) {
            ALOGE("%s: st session opaque ptr %p not in the list!", __func__, config->st_ses.p_ses);
            status = -EINVAL;
            break;
        }
        ALOGV("%s: remove capture_handle %d st session opaque ptr %p", __func__,
              st_ses_info->st_ses.capture_handle, st_ses_info->st_ses.p_ses);
        list_remove(&st_ses_info->list);
        free(st_ses_info);
        break;

    case ST_EVENT_STOP_KEEP_ALIVE:
        pthread_mutex_unlock(&st_dev->lock);
        pthread_mutex_lock(&st_dev->adev->lock);
        audio_extn_keep_alive_stop(KEEP_ALIVE_OUT_PRIMARY);
        pthread_mutex_unlock(&st_dev->adev->lock);
        goto done;

    default:
        ALOGW("%s: Unknown event %d", __func__, event);
        break;
    }
    pthread_mutex_unlock(&st_dev->lock);
done:
    return status;
}

int audio_extn_sound_trigger_read(struct stream_in *in, void *buffer,
                       size_t bytes)
{
    int ret = -1;
    struct sound_trigger_info  *st_info = NULL;
    audio_event_info_t event;

    if (!st_dev)
       return ret;

    if (!in->is_st_session_active) {
        ALOGE(" %s: Sound trigger is not active", __func__);
        goto exit;
    }
    if(in->standby)
        in->standby = false;

    pthread_mutex_lock(&st_dev->lock);
    st_info = get_sound_trigger_info(in->capture_handle);
    pthread_mutex_unlock(&st_dev->lock);
    if (st_info) {
        event.u.aud_info.ses_info = &st_info->st_ses;
        event.u.aud_info.buf = buffer;
        event.u.aud_info.num_bytes = bytes;
        ret = st_dev->st_callback(AUDIO_EVENT_READ_SAMPLES, &event);
    }

exit:
    if (ret) {
        if (-ENETRESET == ret)
            in->is_st_session_active = false;
        memset(buffer, 0, bytes);
        ALOGV("%s: read failed status %d - sleep", __func__, ret);
        usleep((bytes * 1000000) / (audio_stream_in_frame_size((struct audio_stream_in *)in) *
                                   in->config.rate));
    }
    return ret;
}

void audio_extn_sound_trigger_stop_lab(struct stream_in *in)
{
    struct sound_trigger_info  *st_ses_info = NULL;
    audio_event_info_t event;

    if (!st_dev || !in || !in->is_st_session_active)
       return;

    pthread_mutex_lock(&st_dev->lock);
    st_ses_info = get_sound_trigger_info(in->capture_handle);
    pthread_mutex_unlock(&st_dev->lock);
    if (st_ses_info) {
        event.u.ses_info = st_ses_info->st_ses;
        ALOGV("%s: AUDIO_EVENT_STOP_LAB st sess %p", __func__, st_ses_info->st_ses.p_ses);
        st_dev->st_callback(AUDIO_EVENT_STOP_LAB, &event);
        in->is_st_session_active = false;
    }
}
void audio_extn_sound_trigger_check_and_get_session(struct stream_in *in)
{
    struct sound_trigger_info  *st_ses_info = NULL;
    struct listnode *node;

    if (!st_dev || !in)
       return;

    pthread_mutex_lock(&st_dev->lock);
    in->is_st_session = false;
    ALOGV("%s: list %d capture_handle %d", __func__,
          list_empty(&st_dev->st_ses_list), in->capture_handle);
    list_for_each(node, &st_dev->st_ses_list) {
        st_ses_info = node_to_item(node, struct sound_trigger_info , list);
        if (st_ses_info->st_ses.capture_handle == in->capture_handle) {
            in->config = st_ses_info->st_ses.config;
            in->channel_mask = audio_channel_in_mask_from_count(in->config.channels);
            in->is_st_session = true;
            in->is_st_session_active = true;
            ALOGD("%s: capture_handle %d is sound trigger", __func__, in->capture_handle);
            break;
        }
    }
    pthread_mutex_unlock(&st_dev->lock);
}


void audio_extn_sound_trigger_update_device_status(snd_device_t snd_device,
                                     st_event_type_t event)
{
    bool raise_event = false;
    int device_type = -1;

    if (!st_dev)
       return;

    if (snd_device >= SND_DEVICE_OUT_BEGIN &&
        snd_device < SND_DEVICE_OUT_END)
        device_type = PCM_PLAYBACK;
    else if (snd_device >= SND_DEVICE_IN_BEGIN &&
        snd_device < SND_DEVICE_IN_END)
        device_type = PCM_CAPTURE;
    else {
        ALOGE("%s: invalid device 0x%x, for event %d",
                           __func__, snd_device, event);
        return;
    }

    raise_event = platform_sound_trigger_device_needs_event(snd_device);
    ALOGI("%s: device 0x%x of type %d for Event %d, with Raise=%d",
        __func__, snd_device, device_type, event, raise_event);
    if (raise_event && (device_type == PCM_CAPTURE)) {
        switch(event) {
        case ST_EVENT_SND_DEVICE_FREE:
            st_dev->st_callback(AUDIO_EVENT_CAPTURE_DEVICE_INACTIVE, NULL);
            break;
        case ST_EVENT_SND_DEVICE_BUSY:
            st_dev->st_callback(AUDIO_EVENT_CAPTURE_DEVICE_ACTIVE, NULL);
            break;
        default:
            ALOGW("%s:invalid event %d for device 0x%x",
                                  __func__, event, snd_device);
        }
    }/*Events for output device, if required can be placed here in else*/
}

void audio_extn_sound_trigger_update_stream_status(struct audio_usecase *uc_info,
                                     st_event_type_t event)
{
    bool raise_event = false;
    struct audio_event_info ev_info;
    audio_event_type_t ev;
    /*Initialize to invalid device*/
    ev_info.device_info.device = -1;

    if (!st_dev)
       return;

    if (uc_info == NULL) {
        ALOGE("%s: usecase is NULL!!!", __func__);
        return;
    }

    if ((st_dev->sthal_prop_api_version < STHAL_PROP_API_VERSION_1_0) &&
        (uc_info->type != PCM_PLAYBACK))
        return;

    raise_event = platform_sound_trigger_usecase_needs_event(uc_info->id);
    ALOGD("%s: uc_info->id %d of type %d for Event %d, with Raise=%d",
        __func__, uc_info->id, uc_info->type, event, raise_event);
    if (raise_event) {
        if (uc_info->type == PCM_PLAYBACK) {
            if (uc_info->stream.out)
                ev_info.device_info.device = uc_info->stream.out->devices;
            else
                ev_info.device_info.device = AUDIO_DEVICE_OUT_SPEAKER;
            switch(event) {
            case ST_EVENT_STREAM_FREE:
                st_dev->st_callback(AUDIO_EVENT_PLAYBACK_STREAM_INACTIVE, &ev_info);
                break;
            case ST_EVENT_STREAM_BUSY:
                st_dev->st_callback(AUDIO_EVENT_PLAYBACK_STREAM_ACTIVE, &ev_info);
                break;
            default:
                ALOGW("%s:invalid event %d, for usecase %d",
                                      __func__, event, uc_info->id);
            }
        } else if ((uc_info->type == PCM_CAPTURE) || (uc_info->type == VOICE_CALL)) {
            if (event == ST_EVENT_STREAM_BUSY)
                ev = AUDIO_EVENT_CAPTURE_STREAM_ACTIVE;
            else
                ev = AUDIO_EVENT_CAPTURE_STREAM_INACTIVE;
            if (!populate_usecase(&ev_info.u.usecase, uc_info)) {
                ALOGD("%s: send event %d: usecase id %d, type %d",
                      __func__, ev, uc_info->id, uc_info->type);
                st_dev->st_callback(ev, &ev_info);
            }
        }
    }
}

void audio_extn_sound_trigger_update_battery_status(bool charging)
{
    struct audio_event_info ev_info;

    if (!st_dev || st_dev->sthal_prop_api_version < STHAL_PROP_API_VERSION_1_0)
        return;

    ev_info.u.value = charging;
    st_dev->st_callback(AUDIO_EVENT_BATTERY_STATUS_CHANGED, &ev_info);
}


void audio_extn_sound_trigger_set_parameters(struct audio_device *adev __unused,
                               struct str_parms *params)
{
    audio_event_info_t event;
    char value[32];
    int ret, val;

    if(!st_dev || !params) {
        ALOGE("%s: str_params NULL", __func__);
        return;
    }

    ret = str_parms_get_str(params, "SND_CARD_STATUS", value,
                            sizeof(value));
    if (ret > 0) {
        if (strstr(value, "OFFLINE")) {
            event.u.status = SND_CARD_STATUS_OFFLINE;
            st_dev->st_callback(AUDIO_EVENT_SSR, &event);
        }
        else if (strstr(value, "ONLINE")) {
            event.u.status = SND_CARD_STATUS_ONLINE;
            st_dev->st_callback(AUDIO_EVENT_SSR, &event);
        }
        else
            ALOGE("%s: unknown snd_card_status", __func__);
    }

    ret = str_parms_get_str(params, "CPE_STATUS", value, sizeof(value));
    if (ret > 0) {
        if (strstr(value, "OFFLINE")) {
            event.u.status = CPE_STATUS_OFFLINE;
            st_dev->st_callback(AUDIO_EVENT_SSR, &event);
        }
        else if (strstr(value, "ONLINE")) {
            event.u.status = CPE_STATUS_ONLINE;
            st_dev->st_callback(AUDIO_EVENT_SSR, &event);
        }
        else
            ALOGE("%s: unknown CPE status", __func__);
    }

    ret = str_parms_get_int(params, "SVA_NUM_SESSIONS", &val);
    if (ret >= 0) {
        event.u.value = val;
        st_dev->st_callback(AUDIO_EVENT_NUM_ST_SESSIONS, &event);
    }

    ret = str_parms_get_int(params, AUDIO_PARAMETER_DEVICE_CONNECT, &val);
    if ((ret >= 0) && audio_is_input_device(val)) {
        event.u.value = val;
        st_dev->st_callback(AUDIO_EVENT_DEVICE_CONNECT, &event);
    }

    ret = str_parms_get_int(params, AUDIO_PARAMETER_DEVICE_DISCONNECT, &val);
    if ((ret >= 0) && audio_is_input_device(val)) {
        event.u.value = val;
        st_dev->st_callback(AUDIO_EVENT_DEVICE_DISCONNECT, &event);
    }

    ret = str_parms_get_str(params, "SVA_EXEC_MODE", value, sizeof(value));
    if (ret >= 0) {
        strlcpy(event.u.str_value, value, sizeof(event.u.str_value));
        st_dev->st_callback(AUDIO_EVENT_SVA_EXEC_MODE, &event);
    }
}

void audio_extn_sound_trigger_get_parameters(const struct audio_device *adev __unused,
                       struct str_parms *query, struct str_parms *reply)
{
    audio_event_info_t event;
    int ret;
    char value[32];

    ret = str_parms_get_str(query, "SVA_EXEC_MODE_STATUS", value,
                                                  sizeof(value));
    if (ret >= 0) {
        st_dev->st_callback(AUDIO_EVENT_SVA_EXEC_MODE_STATUS, &event);
        str_parms_add_int(reply, "SVA_EXEC_MODE_STATUS", event.u.value);
    }
}

int audio_extn_sound_trigger_init(struct audio_device *adev)
{
    int status = 0;
    char sound_trigger_lib[100];
    void *sthal_prop_api_version;

    ALOGI("%s: Enter", __func__);

    st_dev = (struct sound_trigger_audio_device*)
                        calloc(1, sizeof(struct sound_trigger_audio_device));
    if (!st_dev) {
        ALOGE("%s: ERROR. sound trigger alloc failed", __func__);
        return -ENOMEM;
    }

    get_library_path(sound_trigger_lib);
    st_dev->lib_handle = dlopen(sound_trigger_lib, RTLD_NOW);

    if (st_dev->lib_handle == NULL) {
        ALOGE("%s: error %s", __func__, dlerror());
        status = -ENODEV;
        goto cleanup;
    }
    ALOGI("%s: DLOPEN successful for %s", __func__, sound_trigger_lib);

    DLSYM(st_dev->lib_handle, st_dev->st_callback, sound_trigger_hw_call_back,
          status);
    if (status)
        goto cleanup;

    DLSYM(st_dev->lib_handle, sthal_prop_api_version,
          sthal_prop_api_version, status);
    if (status) {
        st_dev->sthal_prop_api_version = 0;
        status  = 0; /* passthru for backward compability */
    } else {
        st_dev->sthal_prop_api_version = *(int*)sthal_prop_api_version;
        if (MAJOR_VERSION(st_dev->sthal_prop_api_version) !=
            MAJOR_VERSION(STHAL_PROP_API_CURRENT_VERSION)) {
            ALOGE("%s: Incompatible API versions ahal:0x%x != sthal:0x%x",
                  __func__, STHAL_PROP_API_CURRENT_VERSION,
                  st_dev->sthal_prop_api_version);
            goto cleanup;
        }
        ALOGD("%s: sthal is using proprietary API version 0x%04x", __func__,
              st_dev->sthal_prop_api_version);
    }

    st_dev->adev = adev;
    list_init(&st_dev->st_ses_list);
    audio_extn_snd_mon_register_listener(st_dev, stdev_snd_mon_cb);

    return 0;

cleanup:
    if (st_dev->lib_handle)
        dlclose(st_dev->lib_handle);
    free(st_dev);
    st_dev = NULL;
    return status;

}

void audio_extn_sound_trigger_deinit(struct audio_device *adev)
{
    ALOGI("%s: Enter", __func__);
    if (st_dev && (st_dev->adev == adev) && st_dev->lib_handle) {
        audio_extn_snd_mon_unregister_listener(st_dev);
        dlclose(st_dev->lib_handle);
        free(st_dev);
        st_dev = NULL;
    }
}
