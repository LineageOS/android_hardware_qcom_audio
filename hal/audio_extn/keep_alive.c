/*
* Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "keep_alive"
/*#define LOG_NDEBUG 0*/
#include <stdlib.h>
#include <cutils/log.h>
#include "audio_hw.h"
#include "audio_extn.h"
#include "platform_api.h"
#include <platform.h>

#define SILENCE_MIXER_PATH "silence-playback hdmi"
#define SILENCE_DEV_ID 5            /* index into machine driver */
#define SILENCE_INTERVAL_US 2000000

typedef enum {
    STATE_DEINIT = -1,
    STATE_IDLE,
    STATE_ACTIVE,
} state_t;

typedef enum {
    REQUEST_WRITE,
} request_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    pthread_t thread;
    state_t state;
    struct listnode cmd_list;
    struct pcm *pcm;
    bool done;
    void * userdata;
} keep_alive_t;

struct keep_alive_cmd {
    struct listnode node;
    request_t req;
};

static keep_alive_t ka;

static struct pcm_config silence_config = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = DEEP_BUFFER_OUTPUT_PERIOD_SIZE,
    .period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
};

static void * keep_alive_loop(void * context);

void audio_extn_keep_alive_init(struct audio_device *adev)
{
    ka.userdata = adev;
    ka.state = STATE_IDLE;
    ka.pcm = NULL;
    pthread_mutex_init(&ka.lock, (const pthread_mutexattr_t *) NULL);
    pthread_cond_init(&ka.cond, (const pthread_condattr_t *) NULL);
    list_init(&ka.cmd_list);
    if (pthread_create(&ka.thread,  (const pthread_attr_t *) NULL,
                       keep_alive_loop, NULL) < 0) {
        ALOGW("Failed to create keep_alive_thread");
        /* can continue without keep alive */
        ka.state = STATE_DEINIT;
    }
}

static void send_cmd_l(request_t r)
{
    if (ka.state == STATE_DEINIT)
        return;

    struct keep_alive_cmd *cmd =
        (struct keep_alive_cmd *)calloc(1, sizeof(struct keep_alive_cmd));

    cmd->req = r;
    list_add_tail(&ka.cmd_list, &cmd->node);
    pthread_cond_signal(&ka.cond);
}

static int close_silence_stream()
{
    if (!ka.pcm)
        return -ENODEV;

    pcm_close(ka.pcm);
    ka.pcm = NULL;
    return 0;
}

static int open_silence_stream()
{
    unsigned int flags = PCM_OUT|PCM_MONOTONIC;

    if (ka.pcm)
        return -EEXIST;

    ALOGD("opening silence device %d", SILENCE_DEV_ID);
    struct audio_device * adev = (struct audio_device *)ka.userdata;
    ka.pcm = pcm_open(adev->snd_card, SILENCE_DEV_ID,
                      flags, &silence_config);
    ALOGD("opened silence device %d", SILENCE_DEV_ID);
    if (ka.pcm == NULL || !pcm_is_ready(ka.pcm)) {
        ALOGE("%s: %s", __func__, pcm_get_error(ka.pcm));
        if (ka.pcm != NULL) {
            pcm_close(ka.pcm);
            ka.pcm = NULL;
        }
        return -1;
    }
    return 0;
}

/* must be called with adev lock held */
void audio_extn_keep_alive_start()
{
    struct audio_device * adev = (struct audio_device *)ka.userdata;

    if (ka.state == STATE_DEINIT)
        return;

    if (audio_extn_passthru_is_active())
        return;

    pthread_mutex_lock(&ka.lock);

    if (ka.state == STATE_ACTIVE)
        goto exit;

    ka.done = false;
    //todo: platform_send_audio_calibration is replaced by audio_extn_utils_send_audio_calibration
    //check why audio cal needs to be set
    //platform_send_audio_calibration(adev->platform, SND_DEVICE_OUT_HDMI);
    audio_route_apply_and_update_path(adev->audio_route, SILENCE_MIXER_PATH);

    if (open_silence_stream() == 0) {
        send_cmd_l(REQUEST_WRITE);
        while (ka.state != STATE_ACTIVE) {
            pthread_cond_wait(&ka.cond, &ka.lock);
        }
    }

exit:
    pthread_mutex_unlock(&ka.lock);
}

/* must be called with adev lock held */
void audio_extn_keep_alive_stop()
{
    struct audio_device * adev = (struct audio_device *)ka.userdata;

    if (ka.state == STATE_DEINIT)
        return;

    pthread_mutex_lock(&ka.lock);

    if (ka.state == STATE_IDLE)
        goto exit;

    ka.done = true;
    while (ka.state != STATE_IDLE) {
        pthread_cond_wait(&ka.cond, &ka.lock);
    }
    close_silence_stream();
    audio_route_reset_and_update_path(adev->audio_route, SILENCE_MIXER_PATH);

exit:
    pthread_mutex_unlock(&ka.lock);
}

bool audio_extn_keep_alive_is_active()
{
    return ka.state == STATE_ACTIVE;
}

int audio_extn_keep_alive_set_parameters(struct audio_device *adev __unused,
                                         struct str_parms *parms)
{
    char value[32];
    int ret;

    ret = str_parms_get_str(parms, "connect", value, sizeof(value));
    if (ret >= 0) {
        int val = atoi(value);
        if (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            if (!audio_extn_passthru_is_active()) {
                ALOGV("start keep alive");
                audio_extn_keep_alive_start();
            }
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                            sizeof(value));
    if (ret >= 0) {
        int val = atoi(value);
        if (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            ALOGV("stop keep_alive");
            audio_extn_keep_alive_stop();
        }
    }
    return 0;
}


static void * keep_alive_loop(void * context __unused)
{
    struct audio_device *adev = (struct audio_device *)ka.userdata;
    struct keep_alive_cmd *cmd = NULL;
    struct listnode *item;
    uint8_t * silence = NULL;
    int32_t bytes = 0, count = 0, i;
    struct stream_out * p_out = NULL;

    while (true) {
        pthread_mutex_lock(&ka.lock);
        if (list_empty(&ka.cmd_list)) {
            pthread_cond_wait(&ka.cond, &ka.lock);
            pthread_mutex_unlock(&ka.lock);
            continue;
        }

        item = list_head(&ka.cmd_list);
        cmd = node_to_item(item, struct keep_alive_cmd, node);
        list_remove(item);

        if (cmd->req != REQUEST_WRITE) {
            free(cmd);
            pthread_mutex_unlock(&ka.lock);
            continue;
        }

        free(cmd);
        ka.state = STATE_ACTIVE;
        pthread_cond_signal(&ka.cond);
        pthread_mutex_unlock(&ka.lock);

        if (!silence) {
            /* 50 ms */
            bytes =
                (silence_config.rate * silence_config.channels * sizeof(int16_t)) / 20;
            silence = (uint8_t *)calloc(1, bytes);
        }

        while (!ka.done) {
            ALOGV("write %d bytes of silence", bytes);
            pcm_write(ka.pcm, (void *)silence, bytes);
            /* This thread does not have to write silence continuously.
             * Just something to keep the connection alive is sufficient.
             * Hence a short burst of silence periodically.
             */
            usleep(SILENCE_INTERVAL_US);
        }

        pthread_mutex_lock(&ka.lock);
        ka.state = STATE_IDLE;
        pthread_cond_signal(&ka.cond);
        pthread_mutex_unlock(&ka.lock);
    }
    return 0;
}
