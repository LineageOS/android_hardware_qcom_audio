/* Copyright (c) 2013-2014, 2016-2019 The Linux Foundation. All rights reserved.
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
#include <unistd.h>

#include <log/log.h>
#include <cutils/list.h>

#include "AudioDevice.h"
#include "audio_extn.h"

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
#define STHAL_PROP_API_VERSION_1_1 MAKE_HAL_VERSION(1, 1)
#define STHAL_PROP_API_CURRENT_VERSION STHAL_PROP_API_VERSION_1_1

#define ST_EVENT_CONFIG_MAX_STR_VALUE 32
#define ST_DEVICE_HANDSET_MIC 1

typedef enum {
    ST_EVENT_SESSION_REGISTER,
    ST_EVENT_SESSION_DEREGISTER,
    ST_EVENT_START_KEEP_ALIVE,
    ST_EVENT_STOP_KEEP_ALIVE,
    ST_EVENT_UPDATE_ECHO_REF
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
    AUDIO_EVENT_GET_PARAM,
    AUDIO_EVENT_UPDATE_ECHO_REF
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
    CPE_STATUS_ONLINE,
    SLPI_STATUS_OFFLINE,
    SLPI_STATUS_ONLINE
} ssr_event_status_t;

struct sound_trigger_session_info {
    void* p_ses; /* opaque pointer to st_session obj */
    int capture_handle;
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
    bool st_ec_ref_enabled;
};
typedef struct sound_trigger_event_info sound_trigger_event_info_t;

struct sound_trigger_device_info {
    int device;
};

struct sound_trigger_get_param_data {
    char *param;
    int sm_handle;
    struct str_parms *reply;
};

struct audio_event_info {
    union {
        ssr_event_status_t status;
        int value;
        struct sound_trigger_session_info ses_info;
        struct audio_read_samples_info aud_info;
        char str_value[ST_EVENT_CONFIG_MAX_STR_VALUE];
        struct audio_hal_usecase usecase;
        bool audio_ec_ref_enabled;
        struct sound_trigger_get_param_data st_get_param_data;
    } u;
    struct sound_trigger_device_info device_info;
};
typedef struct audio_event_info audio_event_info_t;
/* STHAL callback which is called by AHAL */
typedef int (*sound_trigger_hw_call_back_t)(audio_event_type_t,
                                  struct audio_event_info*);

/*---------------- End: AHAL-STHAL Interface ---------------------------------*/

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
} while (0)

#ifdef __LP64__
#define SOUND_TRIGGER_LIBRARY_PATH "/vendor/lib64/hw/sound_trigger.primary.%s.so"
#else
#define SOUND_TRIGGER_LIBRARY_PATH "/vendor/lib/hw/sound_trigger.primary.%s.so"
#endif

#define SVA_PARAM_DIRECTION_OF_ARRIVAL "st_direction_of_arrival"
#define SVA_PARAM_CHANNEL_INDEX "st_channel_index"
#define MAX_STR_LENGTH_FFV_PARAMS 30
#define MAX_FFV_SESSION_ID 100

/*
 * Current proprietary API version used by AHAL. Queried by STHAL
 * for compatibility check with AHAL
 */
extern "C" const unsigned int sthal_prop_api_version =
    STHAL_PROP_API_CURRENT_VERSION;

struct sound_trigger_info  {
    struct sound_trigger_session_info st_ses;
    bool lab_stopped;
    struct listnode list;
};

struct sound_trigger_audio_device {
    void *lib_handle;
    std::shared_ptr<AudioDevice> adev;
    sound_trigger_hw_call_back_t st_callback;
    struct listnode st_ses_list;
    pthread_mutex_t lock;
    unsigned int sthal_prop_api_version;
    bool st_ec_ref_enabled;
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

extern "C" int audio_hw_call_back(sound_trigger_event_type_t event,
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
        st_ses_info = (struct sound_trigger_info *)calloc(1,
            sizeof(struct sound_trigger_info ));
        if (!st_ses_info) {
            ALOGE("%s: st_ses_info alloc failed", __func__);
            status = -ENOMEM;
            break;
        }
        memcpy(&st_ses_info->st_ses, &config->st_ses,
               sizeof(struct sound_trigger_session_info));
        ALOGV("%s: add capture_handle %d st session opaque ptr %p", __func__,
              st_ses_info->st_ses.capture_handle, st_ses_info->st_ses.p_ses);
        list_add_tail(&st_dev->st_ses_list, &st_ses_info->list);
        break;

    case ST_EVENT_SESSION_DEREGISTER:
        if (!config) {
            ALOGE("%s: NULL config", __func__);
            status = -EINVAL;
            break;
        }
        st_ses_info = get_sound_trigger_info(config->st_ses.capture_handle);
        if (!st_ses_info) {
            ALOGE("%s: st session opaque ptr %p not in the list!", __func__,
                  config->st_ses.p_ses);
            status = -EINVAL;
            break;
        }
        ALOGV("%s: remove capture_handle %d st session opaque ptr %p", __func__,
              st_ses_info->st_ses.capture_handle, st_ses_info->st_ses.p_ses);
        list_remove(&st_ses_info->list);
        free(st_ses_info);
        break;

    default:
        ALOGW("%s: Unknown event %d", __func__, event);
        break;
    }
    pthread_mutex_unlock(&st_dev->lock);

    return status;
}

void* audio_extn_sound_trigger_check_and_get_session(StreamInPrimary *in_stream)
{
    struct sound_trigger_info *st_ses_info = nullptr;
    struct listnode *node = nullptr;
    void *handle = nullptr;

    ALOGV("%s: Enter", __func__);
    if (!st_dev || !in_stream) {
        ALOGE("%s: st_dev %d, in_stream %d", __func__, !st_dev, !in_stream);
        goto exit;
    }

    pthread_mutex_lock(&st_dev->lock);
    in_stream->is_st_session = false;
    ALOGV("%s: list %d capture_handle %d", __func__,
          list_empty(&st_dev->st_ses_list), in_stream->GetHandle());
    list_for_each(node, &st_dev->st_ses_list) {
        st_ses_info = node_to_item(node, struct sound_trigger_info , list);
        if (st_ses_info->st_ses.capture_handle == in_stream->GetHandle()) {
            handle = st_ses_info->st_ses.p_ses;
            in_stream->is_st_session = true;
            in_stream->is_st_session_active = true;
            ALOGD("%s: capture_handle %d is sound trigger", __func__,
                  in_stream->GetHandle());
            break;
        }
    }
    pthread_mutex_unlock(&st_dev->lock);

exit:
    ALOGV("%s: Exit", __func__);

    return handle;
}

int audio_extn_sound_trigger_init(std::shared_ptr<AudioDevice> adev)
{
    int status = 0;
    char sound_trigger_lib[100];
    void *sthalPropApiVersion;

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

    DLSYM(st_dev->lib_handle, sthalPropApiVersion,
          sthal_prop_api_version, status);
    if (status) {
        st_dev->sthal_prop_api_version = 0;
        status  = 0; /* passthru for backward compability */
    } else {
        if (sthalPropApiVersion != NULL) {
            st_dev->sthal_prop_api_version = *(int*)sthalPropApiVersion;
        }
        if (MAJOR_VERSION(st_dev->sthal_prop_api_version) !=
            MAJOR_VERSION(sthal_prop_api_version)) {
            ALOGE("%s: Incompatible API versions ahal:0x%x != sthal:0x%x",
                  __func__, STHAL_PROP_API_CURRENT_VERSION,
                  st_dev->sthal_prop_api_version);
            goto cleanup;
        }
        ALOGD("%s: sthal is using proprietary API version 0x%04x", __func__,
              st_dev->sthal_prop_api_version);
    }

    st_dev->adev = adev;
    st_dev->st_ec_ref_enabled = false;
    list_init(&st_dev->st_ses_list);

    return 0;

cleanup:
    if (st_dev->lib_handle)
        dlclose(st_dev->lib_handle);
    free(st_dev);
    st_dev = NULL;

    return status;
}

void audio_extn_sound_trigger_deinit(std::shared_ptr<AudioDevice> adev)
{
    ALOGI("%s: Enter", __func__);
    if (st_dev && (st_dev->adev == adev) && st_dev->lib_handle) {
        dlclose(st_dev->lib_handle);
        free(st_dev);
        st_dev = NULL;
    }
}
