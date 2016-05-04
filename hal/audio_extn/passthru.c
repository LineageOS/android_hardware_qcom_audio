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

#define LOG_TAG "passthru"
/*#define LOG_NDEBUG 0*/
#include <stdlib.h>
#include <cutils/atomic.h>
#include <cutils/str_parms.h>
#include <cutils/log.h>
#include "audio_hw.h"
#include "audio_extn.h"
#include "platform_api.h"
#include <platform.h>

static const audio_format_t audio_passthru_formats[] = {
    AUDIO_FORMAT_AC3,
    AUDIO_FORMAT_E_AC3,
    AUDIO_FORMAT_DTS,
    AUDIO_FORMAT_DTS_HD
};

/*
 * This atomic var is incremented/decremented by the offload stream to notify
 * other pcm playback streams that a pass thru session is about to start or has
 * finished. This hint can be used by the other streams to move to standby or
 * start calling pcm_write respectively.
 * This behavior is necessary as the DSP backend can only be configured to one
 * of PCM or compressed.
 */
static volatile int32_t compress_passthru_active;

bool audio_extn_passthru_is_supported_format(audio_format_t format)
{
    int32_t num_passthru_formats = sizeof(audio_passthru_formats) /
                                    sizeof(audio_passthru_formats[0]);
    int32_t i;

    for (i = 0; i < num_passthru_formats; i++) {
        if (format == audio_passthru_formats[i]) {
            return true;
        }
    }
    return false;
}

/*
 * must be called with stream lock held
 * This function decides based on some rules whether the data
 * coming on stream out must be rendered or dropped.
 */
bool audio_extn_passthru_should_drop_data(struct stream_out * out)
{
    /* Make this product specific */
    if (!(out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        ALOGI("drop data as end device 0x%x is unsupported", out->devices);
        return true;
    }

    if (out->usecase != USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        if (android_atomic_acquire_load(&compress_passthru_active) > 0) {
            ALOGI("drop data as pass thru is active");
            return true;
        }
    }

    return false;
}

/* called with adev lock held */
void audio_extn_passthru_on_start(struct stream_out * out)
{

    uint64_t max_period_us = 0;
    uint64_t temp;
    struct audio_usecase * usecase;
    struct listnode *node;
    struct stream_out * o;
    struct audio_device *adev = out->dev;

    if (android_atomic_acquire_load(&compress_passthru_active) > 0) {
        ALOGI("pass thru is already active");
        return;
    }

    ALOGV("inc pass thru count to notify other streams");
    android_atomic_inc(&compress_passthru_active);

    ALOGV("keep_alive_stop");
    audio_extn_keep_alive_stop();

    while (true) {
        /* find max period time among active playback use cases */
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (usecase->type == PCM_PLAYBACK &&
                usecase->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
                o = usecase->stream.out;
                temp = o->config.period_size * 1000000LL / o->sample_rate;
                if (temp > max_period_us)
                    max_period_us = temp;
            }
        }

        if (max_period_us) {
            pthread_mutex_unlock(&adev->lock);
            usleep(2*max_period_us);
            max_period_us = 0;
            pthread_mutex_lock(&adev->lock);
        } else
            break;
    }
}

/* called with adev lock held */
void audio_extn_passthru_on_stop(struct stream_out * out)
{
    if (android_atomic_acquire_load(&compress_passthru_active) > 0) {
        /*
         * its possible the count is already zero if pause was called before
         * stop output stream
         */
        android_atomic_dec(&compress_passthru_active);
    }

    if (out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
        ALOGI("passthru on aux digital, start keep alive");
        audio_extn_keep_alive_start();
    }
}

void audio_extn_passthru_on_pause(struct stream_out * out __unused)
{
    if (android_atomic_acquire_load(&compress_passthru_active) == 0)
        return;

    android_atomic_dec(&compress_passthru_active);
}

int audio_extn_passthru_set_parameters(struct audio_device *adev __unused,
                                       struct str_parms *parms __unused)
{
    return 0;
}

bool audio_extn_passthru_is_active()
{
    return android_atomic_acquire_load(&compress_passthru_active) > 0;
}

bool audio_extn_passthru_is_enabled() { return true; }

void audio_extn_passthru_init(struct audio_device *adev __unused)
{
}

bool audio_extn_passthru_should_standby(struct stream_out * out __unused)
{
    return true;
}
