/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_adsp_hdlr_event"

/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <cutils/log.h>
#include <cutils/sched_policy.h>
#include <system/thread_defs.h>

#include "audio_hw.h"
#include "audio_defs.h"
#include "platform.h"
#include "platform_api.h"
#include "adsp_hdlr.h"

#define MAX_EVENT_PAYLOAD             512
#define WAIT_EVENT_POLL_TIMEOUT       50

#define MIXER_MAX_BYTE_LENGTH 512

struct adsp_hdlr_inst {
    bool binit;
    struct mixer *mixer;
};

enum {
    EVENT_CMD_EXIT,             /* event thread exit command loop*/
    EVENT_CMD_WAIT,             /* event thread wait on mixer control */
    EVENT_CMD_GET               /* event thread get param data from mixer */
};

struct event_cmd {
    struct listnode list;
    int opcode;
};

enum {
    ADSP_HDLR_STREAM_STATE_OPENED = 0,
    ADSP_HDLR_STREAM_STATE_EVENT_REGISTERED,
    ADSP_HDLR_STREAM_STATE_EVENT_DEREGISTERED,
    ADSP_HDLR_STREAM_STATE_CLOSED
};

static struct adsp_hdlr_inst *adsp_hdlr_inst = NULL;

static void *event_wait_thread_loop(void *context);
static void *event_callback_thread_loop(void *context);

struct adsp_hdlr_stream_data {
    struct adsp_hdlr_stream_cfg config;
    stream_callback_t client_callback;
    void *client_cookie;
    int state;

    pthread_cond_t event_wait_cond;
    pthread_t event_wait_thread;
    struct listnode event_wait_cmd_list;
    pthread_mutex_t event_wait_lock;
    bool event_wait_thread_active;

    pthread_cond_t event_callback_cond;
    pthread_t event_callback_thread;
    struct listnode event_callback_cmd_list;
    pthread_mutex_t event_callback_lock;
    bool event_callback_thread_active;
};

static int send_cmd_event_wait_thread(struct adsp_hdlr_stream_data *stream_data, int opcode)
{
    struct event_cmd *cmd = calloc(1, sizeof(*cmd));

    if (!cmd) {
        ALOGE("Failed to allocate mem for command 0x%x", opcode);
        return -ENOMEM;
    }

    ALOGVV("%s %d", __func__, opcode);

    cmd->opcode = opcode;

    pthread_mutex_lock(&stream_data->event_wait_lock);
    list_add_tail(&stream_data->event_wait_cmd_list, &cmd->list);
    pthread_cond_signal(&stream_data->event_wait_cond);
    pthread_mutex_unlock(&stream_data->event_wait_lock);

    return 0;
}

static int send_cmd_event_callback_thread(struct adsp_hdlr_stream_data *stream_data,
                                          int opcode)
{
    struct event_cmd *cmd = calloc(1, sizeof(*cmd));

    if (!cmd) {
        ALOGE("Failed to allocate mem for command 0x%x", opcode);
        return -ENOMEM;
    }

    ALOGVV("%s %d", __func__, opcode);

    cmd->opcode = opcode;

    pthread_mutex_lock(&stream_data->event_callback_lock);
    list_add_tail(&stream_data->event_callback_cmd_list, &cmd->list);
    pthread_cond_signal(&stream_data->event_callback_cond);
    pthread_mutex_unlock(&stream_data->event_callback_lock);

    return 0;
}

static void create_event_wait_thread(struct adsp_hdlr_stream_data *stream_data)
{
    pthread_cond_init(&stream_data->event_wait_cond,
                        (const pthread_condattr_t *) NULL);
    list_init(&stream_data->event_wait_cmd_list);
    pthread_create(&stream_data->event_wait_thread, (const pthread_attr_t *) NULL,
                    event_wait_thread_loop, stream_data);
    stream_data->event_wait_thread_active = true;
}

static void create_event_callback_thread(struct adsp_hdlr_stream_data *stream_data)
{
    pthread_cond_init(&stream_data->event_callback_cond,
                      (const pthread_condattr_t *) NULL);
    list_init(&stream_data->event_callback_cmd_list);
    pthread_create(&stream_data->event_callback_thread, (const pthread_attr_t *) NULL,
                   event_callback_thread_loop, stream_data);
    stream_data->event_callback_thread_active = true;
}

static void destroy_event_wait_thread(struct adsp_hdlr_stream_data *stream_data)
{
    send_cmd_event_wait_thread(stream_data, EVENT_CMD_EXIT);
    pthread_join(stream_data->event_wait_thread, (void **) NULL);

    pthread_mutex_lock(&stream_data->event_wait_lock);
    pthread_cond_destroy(&stream_data->event_wait_cond);
    stream_data->event_wait_thread_active = false;
    pthread_mutex_unlock(&stream_data->event_wait_lock);
}

static void destroy_event_callback_thread(struct adsp_hdlr_stream_data *stream_data)
{
    send_cmd_event_callback_thread(stream_data, EVENT_CMD_EXIT);
    pthread_join(stream_data->event_callback_thread, (void **) NULL);

    pthread_mutex_lock(&stream_data->event_callback_lock);
    pthread_cond_destroy(&stream_data->event_callback_cond);
    stream_data->event_callback_thread_active = false;
    pthread_mutex_unlock(&stream_data->event_callback_lock);
}

static void destroy_event_threads(struct adsp_hdlr_stream_data *stream_data)
{
    if (stream_data->event_wait_thread_active)
        destroy_event_wait_thread(stream_data);
    if (stream_data->event_callback_thread_active)
        destroy_event_callback_thread(stream_data);
}

static void *event_wait_thread_loop(void *context)
{
    int ret = 0;
    int opcode = 0;
    bool wait = false;
    struct adsp_hdlr_stream_data *stream_data =
                        (struct adsp_hdlr_stream_data *) context;
    struct adsp_hdlr_stream_cfg *config = &stream_data->config;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    struct mixer_ctl *ctl = NULL;
    struct event_cmd *cmd;
    struct listnode *node;

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
    set_sched_policy(0, SP_BACKGROUND);
    prctl(PR_SET_NAME, (unsigned long)"Event Wait", 0, 0, 0);

    ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
            "ADSP Stream Callback Event %d", config->pcm_device_id);
    if (ret < 0) {
        ALOGE("%s: snprintf failed",__func__);
        ret = -EINVAL;
        goto done;
    }

    ctl = mixer_get_ctl_by_name(adsp_hdlr_inst->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s", __func__,
              mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }

    ret = mixer_subscribe_events(adsp_hdlr_inst->mixer, 1);
    if (ret < 0) {
        ALOGE("%s: Could not subscribe for mixer cmd - %s, ret %d",
              __func__, mixer_ctl_name, ret);
        goto done;
    }

    pthread_mutex_lock(&stream_data->event_wait_lock);
    while (1) {
        if (list_empty(&stream_data->event_wait_cmd_list) && !wait) {
            ALOGVV("%s SLEEPING", __func__);
            pthread_cond_wait(&stream_data->event_wait_cond, &stream_data->event_wait_lock);
            ALOGVV("%s RUNNING", __func__);
        }
        /* execute command if available */
        if (!list_empty(&stream_data->event_wait_cmd_list)) {
            node = list_head(&stream_data->event_wait_cmd_list);
            list_remove(node);
            pthread_mutex_unlock(&stream_data->event_wait_lock);
            cmd = node_to_item(node, struct event_cmd, list);
            opcode = cmd->opcode;
       /* wait if no command avialable */
       } else if (wait)
           opcode = EVENT_CMD_WAIT;
       /* check que again and sleep if needed */
       else
           continue;

        ALOGVV("%s command received: %d", __func__, opcode);
        switch(opcode) {
        case EVENT_CMD_EXIT:
            free(cmd);
            goto thread_exit;
        case EVENT_CMD_WAIT:
            ret = mixer_wait_event(adsp_hdlr_inst->mixer, WAIT_EVENT_POLL_TIMEOUT);
            if (ret < 0)
                ALOGE("%s: mixer_wait_event err! mixer %s, ret = %d",
                      __func__, mixer_ctl_name, ret);
            else if (ret > 0) {
                send_cmd_event_callback_thread(stream_data, EVENT_CMD_GET);

                /* Resubscribe to clear flag checked by mixer_wait_event */
                ret = mixer_subscribe_events(adsp_hdlr_inst->mixer, 0);
                if (ret < 0) {
                    ALOGE("%s: Could not unsubscribe for mixer cmd - %s, ret %d",
                          __func__, mixer_ctl_name, ret);
                    goto done;
                }
                ret = mixer_subscribe_events(adsp_hdlr_inst->mixer, 1);
                if (ret < 0) {
                     ALOGE("%s: Could not unsubscribe for mixer cmd - %s, ret %d",
                          __func__, mixer_ctl_name, ret);
                    goto done;
                }
            }
            /* Once wait command has been sent continue to wait for
               events unless something else is in the command que */
            wait = true;
        break;
        default:
            ALOGE("%s unknown command received: %d", __func__, opcode);
            break;
        }

        if (cmd != NULL) {
            free(cmd);
            cmd = NULL;
        }
    }
thread_exit:
    pthread_mutex_lock(&stream_data->event_wait_lock);
    list_for_each(node, &stream_data->event_wait_cmd_list) {
        list_remove(node);
        free(node);
    }
    pthread_mutex_unlock(&stream_data->event_wait_lock);
done:
    return NULL;
}

static void *event_callback_thread_loop(void *context)
{
    int ret = 0;
    size_t count = 0;
    struct adsp_hdlr_stream_data *stream_data =
                            (struct adsp_hdlr_stream_data *)context;
    struct adsp_hdlr_stream_cfg *config = &stream_data->config;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    struct mixer_ctl *ctl = NULL;
    uint8_t param[MAX_EVENT_PAYLOAD] = {0};
    struct event_cmd *cmd;
    struct listnode *node;

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
    set_sched_policy(0, SP_BACKGROUND);
    prctl(PR_SET_NAME, (unsigned long)"Event Callback", 0, 0, 0);

    ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
            "ADSP Stream Callback Event %d", config->pcm_device_id);
    if (ret < 0) {
        ALOGE("%s: snprintf failed",__func__);
        ret = -EINVAL;
        goto done;
    }

    ctl = mixer_get_ctl_by_name(adsp_hdlr_inst->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s", __func__,
              mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }

    pthread_mutex_lock(&stream_data->event_callback_lock);
    while (1) {
        if (list_empty(&stream_data->event_callback_cmd_list)) {
            ALOGVV("%s SLEEPING", __func__);
            pthread_cond_wait(&stream_data->event_callback_cond,
                              &stream_data->event_callback_lock);
            ALOGVV("%s RUNNING", __func__);
            continue;
        }
        node = list_head(&stream_data->event_callback_cmd_list);
        list_remove(node);
        pthread_mutex_unlock(&stream_data->event_callback_lock);
        cmd = node_to_item(node, struct event_cmd, list);

        ALOGVV("%s command received: %d", __func__, cmd->opcode);
        switch(cmd->opcode) {
        case EVENT_CMD_EXIT:
            free(cmd);
            goto thread_exit;
        case EVENT_CMD_GET:
            mixer_ctl_update(ctl);

            count = mixer_ctl_get_num_values(ctl);
            if ((count > MAX_EVENT_PAYLOAD) || (count <= 0)) {
                ALOGE("%s mixer - %s, count is %d",
                      __func__, mixer_ctl_name, count);
                break;
            }

            ret = mixer_ctl_get_array(ctl, param, count);
            if (ret < 0) {
                ALOGE("%s: mixer_ctl_get_array failed! mixer - %s, ret = %d",
                      __func__, mixer_ctl_name, ret);
                break;
            }

            if (stream_data->client_callback != NULL) {
                ALOGVV("%s: sending client callback event %d", __func__,
                       AUDIO_EXTN_STREAM_CBK_EVENT_ADSP);
                stream_data->client_callback((stream_callback_event_t)
                                             AUDIO_EXTN_STREAM_CBK_EVENT_ADSP,
                                             param,
                                             stream_data->client_cookie);
            }
        break;
        default:
            ALOGE("%s unknown command received: %d", __func__, cmd->opcode);
            break;
        }
        free(cmd);
    }
thread_exit:
    pthread_mutex_lock(&stream_data->event_callback_lock);
    list_for_each(node, &stream_data->event_callback_cmd_list) {
        list_remove(node);
        free(node);
    }
    pthread_mutex_unlock(&stream_data->event_callback_lock);
done:
    return NULL;
}

static int adsp_hdlr_stream_deregister_event(
                struct adsp_hdlr_stream_data *stream_data)
{
    destroy_event_threads((struct adsp_hdlr_stream_data *)stream_data);
    stream_data->state = ADSP_HDLR_STREAM_STATE_EVENT_DEREGISTERED;
    return 0;
}

static int adsp_hdlr_stream_register_event(
                struct adsp_hdlr_stream_data *stream_data,
                struct audio_adsp_event *param)
{
    int ret = 0;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    struct mixer_ctl *ctl = NULL;
    uint8_t payload[AUDIO_MAX_ADSP_STREAM_CMD_PAYLOAD_LEN] = {0};
    struct adsp_hdlr_stream_cfg *config = &stream_data->config;

    /* check if param size exceeds max size supported by mixer */
    if (param->payload_length > AUDIO_MAX_ADSP_STREAM_CMD_PAYLOAD_LEN) {
        ALOGE("%s: Invalid payload_length %d",__func__, param->payload_length);
        return -EINVAL;
    }

    ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
            "ADSP Stream Cmd %d", config->pcm_device_id);
    if (ret < 0) {
        ALOGE("%s: snprintf failed",__func__);
        ret = -EINVAL;
        goto done;
    }

    ctl = mixer_get_ctl_by_name(adsp_hdlr_inst->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s", __func__,
              mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }

    ALOGD("%s: payload_length %d",__func__, param->payload_length);

    /*copy payload size and param */
    memcpy(payload, &param->payload_length,
                    sizeof(param->payload_length));
    memcpy(payload + sizeof(param->payload_length),
           param->payload, param->payload_length);
    ret = mixer_ctl_set_array(ctl, payload,
                 sizeof(param->payload_length) + param->payload_length);
    if (ret < 0) {
        ALOGE("%s: Could not set ctl for mixer cmd - %s, ret %d", __func__,
              mixer_ctl_name, ret);
        goto done;
    }

    pthread_mutex_lock(&stream_data->event_wait_lock);
    if (!stream_data->event_wait_thread_active)
        create_event_wait_thread(stream_data);
    pthread_mutex_unlock(&stream_data->event_wait_lock);

    pthread_mutex_lock(&stream_data->event_callback_lock);
    if (!stream_data->event_callback_thread_active)
        create_event_callback_thread(stream_data);
    pthread_mutex_unlock(&stream_data->event_callback_lock);

    send_cmd_event_wait_thread(stream_data, EVENT_CMD_WAIT);
    stream_data->state = ADSP_HDLR_STREAM_STATE_EVENT_REGISTERED;
done:
        return ret;
}

int audio_extn_adsp_hdlr_stream_set_param(void *handle,
                    adsp_hdlr_cmd_t cmd,
                    void *param)
{
    int ret = 0;
    struct adsp_hdlr_stream_data *stream_data;

    if (handle == NULL) {
        ALOGE("%s: Invalid handle",__func__);
        return -EINVAL;
    }

    stream_data = (struct adsp_hdlr_stream_data *)handle;
    switch (cmd) {
        case ADSP_HDLR_STREAM_CMD_REGISTER_EVENT :
            if (!param) {
                ret = -EINVAL;
                ALOGE("%s: Invalid handle",__func__);
                break;
            }
            ret = adsp_hdlr_stream_register_event(stream_data, param);
            if (ret)
                ALOGE("%s:adsp_hdlr_stream_register_event failed error %d",
                       __func__, ret);
            break;
        case ADSP_HDLR_STREAM_CMD_DEREGISTER_EVENT:
            ret = adsp_hdlr_stream_deregister_event(stream_data);
            if (ret)
                ALOGE("%s:adsp_hdlr_stream_deregister_event failed error %d",
                       __func__, ret);
            break;
        default:
            ret = -EINVAL;
            ALOGE("%s: Unsupported command %d",__func__, cmd);
    }
    return ret;
}

int audio_extn_adsp_hdlr_stream_set_callback(void *handle,
                    stream_callback_t callback,
                    void *cookie)
{
    int ret = 0;
    struct adsp_hdlr_stream_data *stream_data;

    ALOGV("%s:: handle %p", __func__, handle);

    if (!handle) {
        ALOGE("%s:Invalid handle", __func__);
        ret = -EINVAL;
    } else {
        stream_data = (struct adsp_hdlr_stream_data *)handle;
        stream_data->client_callback = callback;
        stream_data->client_cookie = cookie;
    }
    return ret;
}

int audio_extn_adsp_hdlr_stream_close(void *handle)
{
    int ret = 0;
    struct adsp_hdlr_stream_data *stream_data;

    ALOGV("%s:: handle %p", __func__, handle);

    if (!handle) {
        ALOGE("%s:Invalid handle", __func__);
        ret = -EINVAL;
    } else {
        stream_data = (struct adsp_hdlr_stream_data *)handle;
        if (stream_data->state == ADSP_HDLR_STREAM_STATE_EVENT_REGISTERED) {
            ret = adsp_hdlr_stream_deregister_event(stream_data);
            if (ret)
                ALOGE("%s:adsp_hdlr_stream_deregister_event failed error %d",
                        __func__, ret);
        }
        stream_data->state = ADSP_HDLR_STREAM_STATE_CLOSED;
        pthread_mutex_destroy(&stream_data->event_wait_lock);
        pthread_mutex_destroy(&stream_data->event_wait_lock);
        free(stream_data);
        stream_data = NULL;
    }
    return ret;
}

int audio_extn_adsp_hdlr_stream_open(void **handle,
                struct adsp_hdlr_stream_cfg *config)
{

    int ret = 0;
    struct adsp_hdlr_stream_data *stream_data;

    if (!adsp_hdlr_inst) {
        ALOGE("%s: Not Inited", __func__);
        return -ENODEV;;
    }

    if ((!config) || (config->type != PCM_PLAYBACK)) {
        ALOGE("%s: Invalid config param", __func__);
        return -EINVAL;
    }

    ALOGV("%s::pcm_device_id %d, flags %x type %d ", __func__,
          config->pcm_device_id, config->flags, config->type);

    *handle = NULL;

    stream_data = (struct adsp_hdlr_stream_data *) calloc(1,
                                   sizeof(struct adsp_hdlr_stream_data));
    if (stream_data == NULL) {
        ret = -ENOMEM;
    }

    stream_data->config = *config;
    pthread_mutex_init(&stream_data->event_wait_lock,
                       (const pthread_mutexattr_t *) NULL);
    pthread_mutex_init(&stream_data->event_callback_lock,
                       (const pthread_mutexattr_t *) NULL);
    stream_data->state = ADSP_HDLR_STREAM_STATE_OPENED;

    *handle = (void **)stream_data;
    return ret;
}

int audio_extn_adsp_hdlr_init(struct mixer *mixer)
{
    ALOGV("%s", __func__);

    if (!mixer) {
        ALOGE("%s: invalid mixer", __func__);
        return -EINVAL;
    }

    if (adsp_hdlr_inst) {
        ALOGD("%s: Already initialized", __func__);
        return 0;
    }
    adsp_hdlr_inst = (struct adsp_hdlr_inst *)calloc(1,
                                  sizeof(struct adsp_hdlr_inst *));
    if (!adsp_hdlr_inst) {
        ALOGE("%s: calloc failed for adsp_hdlr_inst", __func__);
        return -EINVAL;
    }
    adsp_hdlr_inst->mixer = mixer;

   return 0;
}

int audio_extn_adsp_hdlr_deinit(void)
{
    if (adsp_hdlr_inst) {
        free(adsp_hdlr_inst);
        adsp_hdlr_inst = NULL;
    } else {
        ALOGD("%s: Already Deinitialized", __func__);
    }
    return 0;
}

