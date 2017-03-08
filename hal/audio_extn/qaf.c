/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_hw_qaf"
/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#define COMPRESS_OFFLOAD_NUM_FRAGMENTS 2
#define COMPRESS_OFFLOAD_FRAGMENT_SIZE (2 * 1024)
#define COMPRESS_PLAYBACK_VOLUME_MAX 0x2000
#define QAF_DEFAULT_COMPR_AUDIO_HANDLE 1001
#define QAF_DEFAULT_COMPR_PASSTHROUGH_HANDLE 1002
#define QAF_DEFAULT_PASSTHROUGH_HANDLE 1003
/*
 * MS12 Latency (Input Buffer Processing latency)+
 * Kernel Latency (Calculated based on the available offload buffer size) +
 * DSP Latency (Calculated based on the Platform render latency)
*/
#define COMPRESS_OFFLOAD_PLAYBACK_LATENCY 300
#define PCM_OFFLOAD_PLAYBACK_LATENCY 248

/*
 * Buffer size for compress passthrough is 8192 bytes
 */
#define COMPRESS_PASSTHROUGH_BUFFER_SIZE \
     (COMPRESS_OFFLOAD_NUM_FRAGMENTS * COMPRESS_OFFLOAD_FRAGMENT_SIZE)
/*
 * Frame size for DD/DDP is 1536 samples corresponding to 32ms.
 */
#define DD_FRAME_SIZE 1536
/*
 * DD encoder output size for 32ms.
 */
#define DD_ENCODER_OUTPUT_SIZE 2560
/*
 * DDP encoder output size for 32ms.
 */
#define DDP_ENCODER_OUTPUT_SIZE 4608

/*
 * Frame size for DD/DDP is 1536 samples.
 * For a bit rate of 640 bps, DD encoder output size is 2560 bytes of
 * 32ms;
 * DDP encoder output size is 4608 bytes of 32 ms.
 * Kernel buffer buffer allocation for compress passthrough is
 * 2 x 2048 bytes = 4096 bytes
 * The Latency for DD (measured in samples) is calculated as:
 * Time taken to play 8192 bytes (for DD) = 4096 x 32/2560 = 51.2ms
 * Samples for 51.2ms = 51.2 x 1536/32 = 2457 samples.
 * Latency calculated similarly for DPP is 1365 samples.
 */
#define TRANSCODE_LATENCY(buffer_size, frame_size, encoder_output_in_bytes) ((buffer_size * frame_size) / encoder_output_in_bytes)

/*
 * QAF Latency to process buffers since out_write from primary HAL
 */
#define QAF_COMPRESS_OFFLOAD_PROCESSING_LATENCY 18
#define QAF_PCM_OFFLOAD_PROCESSING_LATENCY 48

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include "audio_utils/primitives.h"
#include "audio_hw.h"
#include "platform_api.h"
#include <platform.h>
#include <system/thread_defs.h>
#include <cutils/sched_policy.h>
#include "audio_extn.h"
#include <qti_audio.h>
#include "sound/compress_params.h"

#define QAF_OUTPUT_SAMPLING_RATE 48000
#define PCM_OFFLOAD_BUFFER_SIZE 7680

#ifdef QAF_DUMP_ENABLED
FILE *fp_output_writer_hdmi = NULL;
#endif

struct qaf {
    struct audio_device *adev;
    audio_session_handle_t session_handle;
    void *qaf_lib;
    int (*qaf_audio_session_open)(audio_session_handle_t* session_handle, void *p_data, void* license_data);
    int (*qaf_audio_session_close)(audio_session_handle_t session_handle);
    int (*qaf_audio_stream_open)(audio_session_handle_t session_handle, audio_stream_handle_t* stream_handle,
         audio_stream_config_t input_config, audio_devices_t devices, stream_type_t flags);
    int (*qaf_audio_stream_close)(audio_stream_handle_t stream_handle);
    int (*qaf_audio_stream_set_param)(audio_stream_handle_t stream_handle, const char* kv_pairs);
    int (*qaf_audio_session_set_param)(audio_session_handle_t handle, const char* kv_pairs);
    char* (*qaf_audio_stream_get_param)(audio_stream_handle_t stream_handle, const char* key);
    char* (*qaf_audio_session_get_param)(audio_session_handle_t handle, const char* key);
    int (*qaf_audio_stream_start)(audio_stream_handle_t handle);
    int (*qaf_audio_stream_stop)(audio_stream_handle_t stream_handle);
    int (*qaf_audio_stream_pause)(audio_stream_handle_t stream_handle);
    int (*qaf_audio_stream_flush)(audio_stream_handle_t stream_handle);
    int (*qaf_audio_stream_write)(audio_stream_handle_t stream_handle, const void* buf, int size);
    void (*qaf_register_event_callback)(audio_session_handle_t session_handle, void *priv_data,
          notify_event_callback_t event_callback, audio_event_id_t event_id);
    pthread_mutex_t lock;
    struct stream_out *stream_drain_main;
    struct stream_out *qaf_compr_offload_out;
    struct stream_out *qaf_compr_offload_out_mch;
    struct stream_out *qaf_compr_passthrough_out;
    struct stream_out *qaf_passthrough_out;
    void *bt_hdl;
    bool hdmi_connect;
    int passthrough_enabled;
    int hdmi_sink_channels;
    bool main_output_active;
    bool assoc_output_active;
    bool qaf_msmd_enabled;
    float vol_left;
    float vol_right;
};

static struct qaf *qaf_mod = NULL;
static int qaf_stream_set_param(struct stream_out *out, const char *kv_pair) __attribute__ ((unused));

static bool is_ms12_format(audio_format_t format)
{
    if((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AC3)
        return true;
    if((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_E_AC3)
        return true;
    if((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC)
        return true;
    if((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC_ADTS)
        return true;
    if(format == AUDIO_FORMAT_PCM_16_BIT)
        return true;

    return false;
}

static void lock_output_stream(struct stream_out *out)
{
    pthread_mutex_lock(&out->pre_lock);
    pthread_mutex_lock(&out->lock);
    pthread_mutex_unlock(&out->pre_lock);
}

static bool audio_extn_qaf_passthrough_enabled(struct stream_out *out)
{
    ALOGV("%s %d ", __func__, __LINE__);
    if ((!property_get_bool("audio.qaf.reencode", false)) &&
        property_get_bool("audio.qaf.passthrough", false)) {
        if (property_get_bool("audio.offload.passthrough", false)) {
            if (((out->format == AUDIO_FORMAT_AC3) && platform_is_edid_supported_format(qaf_mod->adev->platform, AUDIO_FORMAT_AC3)) ||
                ((out->format == AUDIO_FORMAT_E_AC3) && platform_is_edid_supported_format(qaf_mod->adev->platform, AUDIO_FORMAT_E_AC3)) ||
                ((out->format == AUDIO_FORMAT_DTS) && platform_is_edid_supported_format(qaf_mod->adev->platform, AUDIO_FORMAT_DTS)) ||
                ((out->format == AUDIO_FORMAT_DTS_HD) && platform_is_edid_supported_format(qaf_mod->adev->platform, AUDIO_FORMAT_DTS_HD))) {
                return true;
            }
        } else {
            if ((out->format == AUDIO_FORMAT_PCM_16_BIT) && (popcount(out->channel_mask) > 2)) {
                return true;
            }
        }
    }
    return false;
}

static int qaf_out_callback(stream_callback_event_t event, void *param __unused, void *cookie)
{
    struct stream_out *out = (struct stream_out *)cookie;

    out->offload_callback(event, NULL, out->offload_cookie);
    return 0;
}

static int create_output_stream(struct stream_out *out, struct audio_config *config, audio_output_flags_t flags, audio_devices_t devices, int handle_id)
{
    int ret = 0;

    ALOGV("%s %d", __func__, __LINE__);
    if ((handle_id == QAF_DEFAULT_PASSTHROUGH_HANDLE) &&
        (NULL == qaf_mod->qaf_passthrough_out)) {
        pthread_mutex_lock(&qaf_mod->lock);
        lock_output_stream(out);
        ret = adev_open_output_stream((struct audio_hw_device *) qaf_mod->adev, handle_id, devices,
                                         flags, config, (struct audio_stream_out **) &(qaf_mod->qaf_passthrough_out), NULL);
        if (ret < 0) {
            pthread_mutex_unlock(&out->lock);
            pthread_mutex_unlock(&qaf_mod->lock);
            ALOGE("%s: adev_open_output_stream failed with ret = %d!", __func__, ret);
            return -EINVAL;
        }
        qaf_mod->qaf_passthrough_out->stream.set_callback((struct audio_stream_out *)qaf_mod->qaf_passthrough_out, (stream_callback_t) qaf_out_callback, out);
        pthread_mutex_unlock(&out->lock);
        pthread_mutex_unlock(&qaf_mod->lock);
    }
    return ret;
}

static int qaf_send_offload_cmd_l(struct stream_out* out, int command)
{
    struct offload_cmd *cmd = (struct offload_cmd *)calloc(1, sizeof(struct offload_cmd));

    if (!cmd) {
        ALOGE("failed to allocate mem for command 0x%x", command);
        return -ENOMEM;
    }

    ALOGV("%s %d", __func__, command);

    cmd->cmd = command;
    list_add_tail(&out->qaf_offload_cmd_list, &cmd->node);
    pthread_cond_signal(&out->qaf_offload_cond);
    return 0;
}

static int audio_extn_qaf_stream_stop(struct stream_out *out)
{
    ALOGV("%s: %d start", __func__, __LINE__);
    if (!qaf_mod->qaf_audio_stream_stop)
        return -EINVAL;

    return qaf_mod->qaf_audio_stream_stop(out->qaf_stream_handle);
}

static int qaf_out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;

    ALOGD("%s: enter: stream (%p) usecase(%d: %s)", __func__,
          stream, out->usecase, use_case_table[out->usecase]);

    lock_output_stream(out);
    if (qaf_mod->qaf_passthrough_out && qaf_mod->hdmi_connect) {
        if ((out->format == AUDIO_FORMAT_PCM_16_BIT) && (popcount(out->channel_mask) <= 2)) {
            pthread_mutex_unlock(&out->lock);
            return status;
        }
        status = qaf_mod->qaf_passthrough_out->stream.common.standby((struct audio_stream *) qaf_mod->qaf_passthrough_out);
        if (!out->standby) {
            out->standby = true;
        }
        pthread_mutex_unlock(&out->lock);
        return status;
    }

    if (!out->standby) {
        out->standby = true;
        status = audio_extn_qaf_stream_stop(out);
    }
    pthread_mutex_unlock(&out->lock);
    return status;
}

static int qaf_stream_set_param(struct stream_out *out, const char *kv_pair)
{
    ALOGV("%s %d kvpair: %s", __func__, __LINE__, kv_pair);
    if (!qaf_mod->qaf_audio_stream_set_param)
        return -EINVAL;

    return qaf_mod->qaf_audio_stream_set_param(out->qaf_stream_handle, kv_pair);
}

static int qaf_write_input_buffer(struct stream_out *out, const void *buffer, int bytes)
{
    int ret = 0;
    ALOGVV("%s bytes = %d [%p]", __func__, bytes, out->qaf_stream_handle);
    if (!qaf_mod->qaf_audio_stream_write)
        return -EINVAL;

    if (out->qaf_stream_handle)
        ret = qaf_mod->qaf_audio_stream_write(out->qaf_stream_handle, buffer, bytes);
    return ret;
}

static int qaf_out_set_volume(struct audio_stream_out *stream __unused, float left,
                          float right)
{
    /* For ms12 formats, qaf_mod->qaf_compr_offload_out is allocated during the first
     * call of notify_event_callback(). Therefore, the volume levels set during session
     * open have to be cached and applied later */
    qaf_mod->vol_left = left;
    qaf_mod->vol_right = right;

    if (qaf_mod->qaf_compr_offload_out != NULL) {
        return qaf_mod->qaf_compr_offload_out->stream.set_volume(
            (struct audio_stream_out *)qaf_mod->qaf_compr_offload_out, left, right);
    }
    return -ENOSYS;
}

static int qaf_stream_start(struct stream_out *out)
{
    if (!qaf_mod->qaf_audio_stream_start)
        return -EINVAL;

    return qaf_mod->qaf_audio_stream_start(out->qaf_stream_handle);
}

static int qaf_start_output_stream(struct stream_out *out)
{
    int ret = 0;
    struct audio_device *adev = out->dev;
    int snd_card_status = get_snd_card_state(adev);

    if ((out->usecase < 0) || (out->usecase >= AUDIO_USECASE_MAX)) {
        ret = -EINVAL;
        usleep(50000);
        return ret;
    }

    ALOGD("%s: enter: stream(%p)usecase(%d: %s) devices(%#x)",
          __func__, &out->stream, out->usecase, use_case_table[out->usecase],
          out->devices);

    if (SND_CARD_STATE_OFFLINE == snd_card_status) {
        ALOGE("%s: sound card is not active/SSR returning error", __func__);
        ret = -EIO;
        usleep(50000);
        return ret;
    }

    return qaf_stream_start(out);
}

static ssize_t qaf_out_write(struct audio_stream_out *stream, const void *buffer,
                         size_t bytes)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    ssize_t ret = 0;

    ALOGV("qaf_out_write bytes = %d, usecase[%d] and flags[%x] for handle[%p]",(int)bytes, out->usecase, out->flags, out);
    lock_output_stream(out);

    if (qaf_mod->qaf_passthrough_out && qaf_mod->hdmi_connect) {
        if ((out->format == AUDIO_FORMAT_PCM_16_BIT) && (popcount(out->channel_mask) <= 2)) {
            ALOGD(" %s : Drop data as compress passthrough session is going on", __func__);
            usleep(bytes * 1000000 / audio_stream_out_frame_size(stream) /
                            out->stream.common.get_sample_rate(&out->stream.common));
            goto exit;
        }
        ret = qaf_mod->qaf_passthrough_out->stream.write((struct audio_stream_out *)(qaf_mod->qaf_passthrough_out), buffer, bytes);
        pthread_mutex_unlock(&out->lock);
        return ret;
    }

    if (out->standby) {
        out->standby = false;
        pthread_mutex_lock(&adev->lock);
        ret = qaf_start_output_stream(out);
        pthread_mutex_unlock(&adev->lock);
        /* ToDo: If use case is compress offload should return 0 */
        if (ret != 0) {
            out->standby = true;
            goto exit;
        }
    }

    if (adev->is_channel_status_set == false && (out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)){
        audio_utils_set_hdmi_channel_status(out, (char *)buffer, bytes);
        adev->is_channel_status_set = true;
    }

    ret = qaf_write_input_buffer(out, buffer, bytes);
    ALOGV("%s, ret [%d] ", __func__, (int)ret);
    if (ret < 0) {
        goto exit;
    }
    out->written += bytes / ((popcount(out->channel_mask) * sizeof(short)));

exit:

    pthread_mutex_unlock(&out->lock);

    if (ret < 0) {
        if (ret == -EAGAIN) {
            ALOGV("No space available in ms12 driver, post msg to cb thread");
            lock_output_stream(out);
            ret = qaf_send_offload_cmd_l(out, OFFLOAD_CMD_WAIT_FOR_BUFFER);
            pthread_mutex_unlock(&out->lock);
            bytes = 0;
        }
        if(ret == -ENOMEM || ret == -EPERM){
            if (out->pcm)
                ALOGE("%s: error %d, %s", __func__, (int)ret, pcm_get_error(out->pcm));
            qaf_out_standby(&out->stream.common);
            usleep(bytes * 1000000 / audio_stream_out_frame_size(stream) /
                            out->stream.common.get_sample_rate(&out->stream.common));
        }
    }
    return bytes;
}

static int qaf_get_timestamp(struct stream_out *out, uint64_t *frames, struct timespec *timestamp)
{
    int ret = 0;
    struct str_parms *parms;
    int value = 0;
    int latency = 0;
    int signed_frames = 0;
    char* kvpairs = NULL;

    ALOGV("%s out->format %d", __func__, out->format);
    kvpairs = qaf_mod->qaf_audio_stream_get_param(out->qaf_stream_handle, "get_latency");
    if (kvpairs) {
        parms = str_parms_create_str(kvpairs);
        ret = str_parms_get_int(parms, "get_latency", &latency);
        if (ret >= 0) {
            str_parms_destroy(parms);
            parms = NULL;
        }
        free(kvpairs);
        kvpairs = NULL;
    }
    // MS12 Latency + Kernel Latency + Dsp Latency
    if (qaf_mod->qaf_compr_offload_out != NULL) {
        out->platform_latency = latency + (COMPRESS_OFFLOAD_NUM_FRAGMENTS * PCM_OFFLOAD_BUFFER_SIZE \
                                       /(popcount(qaf_mod->qaf_compr_offload_out->channel_mask) * sizeof(short))) \
                                       +((platform_render_latency(qaf_mod->qaf_compr_offload_out->usecase) * qaf_mod->qaf_compr_offload_out->sample_rate) / 1000000LL);
    } else if (audio_extn_bt_hal_get_output_stream(qaf_mod->bt_hdl) != NULL) {
        out->platform_latency = latency + audio_extn_bt_hal_get_latency(qaf_mod->bt_hdl);
    } else if (NULL != qaf_mod->qaf_compr_passthrough_out) {
        out->platform_latency = latency + ((qaf_mod->qaf_compr_passthrough_out->format == AUDIO_FORMAT_AC3) ? TRANSCODE_LATENCY(COMPRESS_PASSTHROUGH_BUFFER_SIZE, DD_FRAME_SIZE, DD_ENCODER_OUTPUT_SIZE) : TRANSCODE_LATENCY(COMPRESS_PASSTHROUGH_BUFFER_SIZE, DD_FRAME_SIZE, DDP_ENCODER_OUTPUT_SIZE)) \
                                        + (COMPRESS_OFFLOAD_PLAYBACK_LATENCY *  qaf_mod->qaf_compr_passthrough_out->sample_rate/1000);
    }

    if(out->format & AUDIO_FORMAT_PCM_16_BIT) {
       *frames = 0;
       signed_frames = out->written - out->platform_latency;
       // It would be unusual for this value to be negative, but check just in case ...
       if (signed_frames >= 0) {
           *frames = signed_frames;
       }
       clock_gettime(CLOCK_MONOTONIC, timestamp);
    } else if (qaf_mod->qaf_audio_stream_get_param) {
        kvpairs = qaf_mod->qaf_audio_stream_get_param(out->qaf_stream_handle, "position");
        if (kvpairs) {
            parms = str_parms_create_str(kvpairs);
            ret = str_parms_get_int(parms, "position", &value);
            if (ret >= 0) {
                *frames = value;
                signed_frames = value - out->platform_latency;
                // It would be unusual for this value to be negative, but check just in case ...
                if (signed_frames >= 0) {
                    *frames = signed_frames;
                }
                clock_gettime(CLOCK_MONOTONIC, timestamp);
            }
            str_parms_destroy(parms);
        }
    } else {
        ret = -EINVAL;
    }
    return ret;
}

static int qaf_out_get_presentation_position(const struct audio_stream_out *stream,
                                   uint64_t *frames, struct timespec *timestamp)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret = -1;
    lock_output_stream(out);

    if (qaf_mod->qaf_passthrough_out && qaf_mod->hdmi_connect) {
        ret = qaf_mod->qaf_passthrough_out->stream.get_presentation_position((struct audio_stream_out *)qaf_mod->qaf_passthrough_out, frames, timestamp);
        pthread_mutex_unlock(&out->lock);
        return ret;
    }

    ret = qaf_get_timestamp(out, frames, timestamp);
    pthread_mutex_unlock(&out->lock);

    return ret;
}

static int qaf_stream_pause(struct stream_out *out)
{
    ALOGV("%s: %d start", __func__, __LINE__);
    if (!qaf_mod->qaf_audio_stream_pause)
        return -EINVAL;

    return qaf_mod->qaf_audio_stream_pause(out->qaf_stream_handle);
}

static int qaf_out_pause(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGE("%s", __func__);

    lock_output_stream(out);
    if (qaf_mod->qaf_passthrough_out && qaf_mod->hdmi_connect) {
        status = qaf_mod->qaf_passthrough_out->stream.pause((struct audio_stream_out *) qaf_mod->qaf_passthrough_out);
        out->offload_state = OFFLOAD_STATE_PAUSED;
        pthread_mutex_unlock(&out->lock);
        return status;
    }

    status = qaf_stream_pause(out);
    pthread_mutex_unlock(&out->lock);
    return status;
}

static int qaf_out_drain(struct audio_stream_out* stream, audio_drain_type_t type)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;
    ALOGV("%s stream_handle = %p , format = %x", __func__, out->qaf_stream_handle, out->format);

    lock_output_stream(out);
    if (qaf_mod->qaf_passthrough_out && qaf_mod->hdmi_connect) {
        status = qaf_mod->qaf_passthrough_out->stream.drain((struct audio_stream_out*)(qaf_mod->qaf_passthrough_out), type);
        pthread_mutex_unlock(&out->lock);
        return status;
    }


    if (out->offload_callback && out->qaf_stream_handle) {
        /* Stream stop will trigger EOS and on EOS_EVENT received
           from callback DRAIN_READY command is sent */
        status = audio_extn_qaf_stream_stop(out);
        if (out->format != AUDIO_FORMAT_PCM_16_BIT)
            qaf_mod->stream_drain_main = out;
    }
    pthread_mutex_unlock(&out->lock);
    return status;
}

static int audio_extn_qaf_stream_flush(struct stream_out *out)
{
    ALOGV("%s: %d exit", __func__, __LINE__);
    if (!qaf_mod->qaf_audio_stream_flush)
        return -EINVAL;

    return qaf_mod->qaf_audio_stream_flush(out->qaf_stream_handle);
}

static int qaf_out_flush(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    ALOGV("%s", __func__);
    int status = -ENOSYS;

    lock_output_stream(out);
    if (qaf_mod->qaf_passthrough_out && qaf_mod->hdmi_connect) {
        status = qaf_mod->qaf_passthrough_out->stream.flush((struct audio_stream_out *)qaf_mod->qaf_passthrough_out);
        out->offload_state = OFFLOAD_STATE_IDLE;
        pthread_mutex_unlock(&out->lock);
        return status;
    }

    status = audio_extn_qaf_stream_flush(out);
    pthread_mutex_unlock(&out->lock);
    ALOGV("%s Exit", __func__);
    return status;
}

static uint32_t qaf_out_get_latency(const struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    uint32_t latency = 0;


    lock_output_stream(out);
    if (qaf_mod->qaf_passthrough_out && qaf_mod->hdmi_connect) {
        latency = qaf_mod->qaf_passthrough_out->stream.get_latency((struct audio_stream_out *)qaf_mod->qaf_passthrough_out);
        ALOGV("%s: latency = %u", __FUNCTION__, latency);
        pthread_mutex_unlock(&out->lock);
        return latency;
    }
    pthread_mutex_unlock(&out->lock);

    if (is_offload_usecase(out->usecase)) {
        latency = COMPRESS_OFFLOAD_PLAYBACK_LATENCY;
    } else {
        latency = PCM_OFFLOAD_PLAYBACK_LATENCY;
    }

    if (audio_extn_bt_hal_get_output_stream(qaf_mod->bt_hdl) != NULL) {

        if (is_offload_usecase(out->usecase)) {
            latency = audio_extn_bt_hal_get_latency(qaf_mod->bt_hdl) + QAF_COMPRESS_OFFLOAD_PROCESSING_LATENCY;
        } else {
            latency = audio_extn_bt_hal_get_latency(qaf_mod->bt_hdl) + QAF_PCM_OFFLOAD_PROCESSING_LATENCY;
        }
    }
    ALOGV("%s: Latency %d", __func__, latency);
    return latency;
}

static void notify_event_callback(audio_session_handle_t session_handle __unused, void *prv_data, void *buf, audio_event_id_t event_id, int size, int device)
{

/*
 For SPKR:
 1. Open pcm device if device_id passed to it SPKR and write the data to pcm device

 For HDMI
 1.Open compress device for HDMI(PCM or AC3) based on current_hdmi_output_format
 2.create offload_callback thread to receive async events
 3.Write the data to compress device. If not all the data is consumed by the driver,
   add a command to offload_callback thread.
*/
    int ret;
    audio_output_flags_t flags;
    struct qaf* qaf_module = (struct qaf* ) prv_data;
    struct audio_stream_out *bt_stream = NULL;

    ALOGV("%s device 0x%X, %d in event = %d", __func__, device, __LINE__, event_id);

    if (event_id == AUDIO_DATA_EVENT) {
        ALOGVV("Device id %x %s %d, bytes to written %d", device, __func__,__LINE__, size);

        if ((qaf_mod->qaf_passthrough_out != NULL) && qaf_mod->hdmi_connect) {
            pthread_mutex_lock(&qaf_module->lock);
            if (qaf_mod->qaf_compr_offload_out != NULL) {
                adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev,
                                             (struct audio_stream_out *) (qaf_mod->qaf_compr_offload_out));
                qaf_mod->qaf_compr_offload_out = NULL;
            }
            if (qaf_mod->qaf_compr_offload_out_mch) {
                adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev,
                                             (struct audio_stream_out *) (qaf_mod->qaf_compr_offload_out_mch));
                qaf_mod->qaf_compr_offload_out_mch = NULL;
            }
            pthread_mutex_unlock(&qaf_module->lock);
            ALOGV("%s %d DROPPING DATA", __func__, __LINE__);
            return;
        } else {
            if (qaf_mod->qaf_passthrough_out != NULL) {
                pthread_mutex_lock(&qaf_module->lock);
                adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev,
                                            (struct audio_stream_out *) (qaf_mod->qaf_passthrough_out));
                qaf_mod->qaf_passthrough_out = NULL;
                qaf_mod->main_output_active = false;
                pthread_mutex_unlock(&qaf_module->lock);
            }
        }
        pthread_mutex_lock(&qaf_module->lock);
        if ((device == (AUDIO_DEVICE_OUT_AUX_DIGITAL | AUDIO_COMPRESSED_OUT_DD)) ||
            (device == (AUDIO_DEVICE_OUT_AUX_DIGITAL | AUDIO_COMPRESSED_OUT_DDP))) {

            if (NULL == qaf_mod->qaf_compr_passthrough_out && qaf_mod->hdmi_connect) {
                struct audio_config config;
                audio_devices_t devices;

                if (qaf_mod->qaf_compr_offload_out_mch) {
                    adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev,
                                                 (struct audio_stream_out *) (qaf_mod->qaf_compr_offload_out_mch));
                    qaf_mod->qaf_compr_offload_out_mch = NULL;
                }

                config.sample_rate = config.offload_info.sample_rate = QAF_OUTPUT_SAMPLING_RATE;
                config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                config.offload_info.size = AUDIO_INFO_INITIALIZER.size;

                if (device == (AUDIO_DEVICE_OUT_AUX_DIGITAL | AUDIO_COMPRESSED_OUT_DDP))
                    config.format = config.offload_info.format = AUDIO_FORMAT_E_AC3;
                else
                    config.format = config.offload_info.format = AUDIO_FORMAT_AC3;

                config.offload_info.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
                config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
                flags = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_DIRECT|AUDIO_OUTPUT_FLAG_DIRECT_PCM;
                devices = AUDIO_DEVICE_OUT_AUX_DIGITAL;

                ret = adev_open_output_stream((struct audio_hw_device *) qaf_mod->adev, QAF_DEFAULT_COMPR_PASSTHROUGH_HANDLE, devices,
                                           flags, &config, (struct audio_stream_out **) &(qaf_mod->qaf_compr_passthrough_out), NULL);
                if (ret < 0) {
                    ALOGE("%s: adev_open_output_stream failed with ret = %d!", __func__, ret);
                    pthread_mutex_unlock(&qaf_module->lock);
                    return;
                }
                qaf_mod->qaf_compr_passthrough_out->compr_config.fragments = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
            }

            if (!qaf_mod->passthrough_enabled)
                qaf_mod->passthrough_enabled = 1;

            if (qaf_mod->qaf_compr_passthrough_out)
                ret = qaf_mod->qaf_compr_passthrough_out->stream.write((struct audio_stream_out *) qaf_mod->qaf_compr_passthrough_out, buf, size);
        } else if ((device & AUDIO_DEVICE_OUT_AUX_DIGITAL) && ((qaf_mod->hdmi_connect) &&
                   (qaf_mod->qaf_passthrough_out == NULL) && (qaf_mod->hdmi_sink_channels > 2))) {
            if (NULL == qaf_mod->qaf_compr_offload_out_mch) {
                struct audio_config config;
                audio_devices_t devices;

                config.sample_rate = config.offload_info.sample_rate = QAF_OUTPUT_SAMPLING_RATE;
                config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
                config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
                config.offload_info.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
                config.format = AUDIO_FORMAT_PCM_16_BIT;
                devices = AUDIO_DEVICE_NONE;

                if (qaf_mod->hdmi_sink_channels == 8) {
                    config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_7POINT1;
                } else if (qaf_mod->hdmi_sink_channels == 6) {
                    config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
                } else {
                    config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                }
                devices = AUDIO_DEVICE_OUT_AUX_DIGITAL;
                flags = AUDIO_OUTPUT_FLAG_DIRECT|AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_DIRECT_PCM;

                ret = adev_open_output_stream((struct audio_hw_device *) qaf_mod->adev, QAF_DEFAULT_COMPR_AUDIO_HANDLE, devices,
                                                 flags, &config, (struct audio_stream_out **) &(qaf_mod->qaf_compr_offload_out_mch), NULL);
                if (ret < 0) {
                    ALOGE("%s: adev_open_output_stream failed with ret = %d!", __func__, ret);
                    pthread_mutex_unlock(&qaf_module->lock);
                    return;
                }
                qaf_mod->qaf_compr_offload_out_mch->compr_config.fragments = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
            }

            if (qaf_mod->qaf_compr_offload_out_mch)
                ret = qaf_mod->qaf_compr_offload_out_mch->stream.write((struct audio_stream_out *) qaf_mod->qaf_compr_offload_out_mch, buf, size);
        } else {
            bt_stream = audio_extn_bt_hal_get_output_stream(qaf_mod->bt_hdl);
            if (bt_stream != NULL) {
                if (qaf_mod->qaf_compr_offload_out) {
                    adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev,
                                                 (struct audio_stream_out *) (qaf_mod->qaf_compr_offload_out));
                    qaf_mod->qaf_compr_offload_out = NULL;
                }

                audio_extn_bt_hal_out_write(qaf_mod->bt_hdl, buf, size);
            }

            if (NULL == qaf_mod->qaf_compr_offload_out && bt_stream == NULL && qaf_mod->qaf_passthrough_out == NULL) {
                struct audio_config config;
                audio_devices_t devices;

                config.sample_rate = config.offload_info.sample_rate = QAF_OUTPUT_SAMPLING_RATE;
                config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
                config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
                config.offload_info.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
                config.format = AUDIO_FORMAT_PCM_16_BIT;
                config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                devices = AUDIO_DEVICE_OUT_SPEAKER;
                flags = AUDIO_OUTPUT_FLAG_DIRECT|AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_DIRECT_PCM;

                /* TODO:: Need to Propagate errors to framework */
                ret = adev_open_output_stream((struct audio_hw_device *) qaf_mod->adev, QAF_DEFAULT_COMPR_AUDIO_HANDLE, devices,
                                             flags, &config, (struct audio_stream_out **) &(qaf_mod->qaf_compr_offload_out), NULL);
                if (ret < 0) {
                    ALOGE("%s: adev_open_output_stream failed with ret = %d!", __func__, ret);
                    pthread_mutex_unlock(&qaf_module->lock);
                    return;
                }
                qaf_mod->qaf_compr_offload_out->compr_config.fragments = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
                qaf_mod->qaf_compr_offload_out->info.channel_mask = config.offload_info.channel_mask;
                qaf_mod->qaf_compr_offload_out->info.format = config.offload_info.format;
                qaf_mod->qaf_compr_offload_out->info.sample_rate = config.offload_info.sample_rate;

                qaf_mod->qaf_compr_offload_out->stream.set_volume((struct audio_stream_out *)qaf_mod->qaf_compr_offload_out, qaf_mod->vol_left, qaf_mod->vol_right);
            }

            if (!qaf_mod->hdmi_connect && (qaf_mod->qaf_compr_passthrough_out || qaf_mod->qaf_compr_offload_out_mch)) {
                qaf_mod->passthrough_enabled = 0;
                if (qaf_mod->qaf_compr_passthrough_out) {
                    adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev,
                                                 (struct audio_stream_out *) (qaf_mod->qaf_compr_passthrough_out));
                    qaf_mod->qaf_compr_passthrough_out = NULL;
                }
                if (qaf_mod->qaf_compr_offload_out_mch) {
                    adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev,
                                                 (struct audio_stream_out *) (qaf_mod->qaf_compr_offload_out_mch));
                    qaf_mod->qaf_compr_offload_out_mch = NULL;
                }
            }

            /*
             * TODO:: Since this is mixed data,
             * need to identify to which stream the error should be sent
             */
            if (bt_stream == NULL && qaf_mod->qaf_compr_offload_out)
                ret = qaf_mod->qaf_compr_offload_out->stream.write((struct audio_stream_out *) qaf_mod->qaf_compr_offload_out, buf, size);
        }

        ALOGVV("%s:%d stream write ret = %d", __func__, __LINE__, ret);
        pthread_mutex_unlock(&qaf_module->lock);
    } else if (event_id == AUDIO_EOS_MAIN_DD_DDP_EVENT || event_id == AUDIO_EOS_MAIN_AAC_EVENT) {
        /* TODO:: Only MAIN Stream EOS Event is added, need to add ASSOC stream EOS Event */
        struct stream_out *out = qaf_module->stream_drain_main;
        if (out != NULL) {
            lock_output_stream(out);
            out->offload_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out->offload_cookie);
            pthread_mutex_unlock(&out->lock);
            qaf_module->stream_drain_main = NULL;
            ALOGV("%s %d sent DRAIN_READY", __func__, __LINE__);
        }
    }
    ALOGV("%s %d", __func__, __LINE__);
}

static int qaf_session_close()
{
    ALOGV("%s %d", __func__, __LINE__);
    if (qaf_mod != NULL) {
        if (!qaf_mod->qaf_audio_session_close)
            return -EINVAL;

        qaf_mod->qaf_audio_session_close(qaf_mod->session_handle);
        qaf_mod->session_handle = NULL;
        pthread_mutex_destroy(&qaf_mod->lock);
    }
    return 0;
}

static int qaf_stream_close(struct stream_out *out)
{
    int ret = 0;
    ALOGV( "%s %d", __func__, __LINE__);
    if (!qaf_mod->qaf_audio_stream_close)
        return -EINVAL;
    if (out->qaf_stream_handle) {
        ALOGV( "%s %d output active flag is %x and stream handle %p", __func__, __LINE__, out->flags, out->qaf_stream_handle);
        if ((out->flags & AUDIO_OUTPUT_FLAG_ASSOCIATED) && (out->flags & AUDIO_OUTPUT_FLAG_MAIN)) { /* Close for Stream with Main and Associated Content*/
            qaf_mod->main_output_active = false;
            qaf_mod->assoc_output_active = false;
        } else if (out->flags & AUDIO_OUTPUT_FLAG_MAIN) {/*Close for Main Stream*/
            qaf_mod->main_output_active = false;
            qaf_mod->assoc_output_active = false; /* TODO to remove resetting associated stream active flag when main stream is closed*/
        } else if (out->flags & AUDIO_OUTPUT_FLAG_ASSOCIATED) { /*Close for Associated Stream*/
            qaf_mod->assoc_output_active = false;
        } else { /*Close for Local Playback*/
            qaf_mod->main_output_active = false;
        }
        ret = qaf_mod->qaf_audio_stream_close(out->qaf_stream_handle);
        out->qaf_stream_handle = NULL;
    }
    ALOGV( "%s %d", __func__, __LINE__);
    return ret;
}

static int qaf_stream_open(struct stream_out *out, struct audio_config *config, audio_output_flags_t flags, audio_devices_t devices)
{
    int status = 0;
    ALOGV("%s %d", __func__, __LINE__);

    if (!qaf_mod->qaf_audio_stream_open)
        return -EINVAL;

    if (audio_extn_qaf_passthrough_enabled(out) && qaf_mod->hdmi_connect) {
        ALOGV("%s %d passthrough is enabled", __func__, __LINE__);
        status =  create_output_stream(out, config, flags, devices, QAF_DEFAULT_PASSTHROUGH_HANDLE);
        if (status < 0) {
            ALOGE("%s: adev_open_output_stream failed with ret = %d!", __func__, status);
            return -EINVAL;
        }
        return 0;
    }

    audio_stream_config_t input_config;
    input_config.sample_rate = config->sample_rate;
    input_config.channels = popcount(config->channel_mask);
    input_config.format = config->format;

    if ((config->format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC) {
        input_config.format = AUDIO_FORMAT_AAC;
    } else if((config->format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC_ADTS) {
        input_config.format = AUDIO_FORMAT_AAC_ADTS;
    }

    ALOGV("%s %d audio_stream_open sample_rate(%d) channels(%d) devices(%#x) flags(%#x) format(%#x)\
      ",__func__, __LINE__, input_config.sample_rate, input_config.channels, devices, flags, input_config.format);

    /* TODO to send appropriated flags when support for system tones is added */
    if (input_config.format == AUDIO_FORMAT_PCM_16_BIT) {
        status = qaf_mod->qaf_audio_stream_open(qaf_mod->session_handle, &out->qaf_stream_handle, input_config, devices, /*flags*/AUDIO_STREAM_SYSTEM_TONE);
    } else if (input_config.format == AUDIO_FORMAT_AC3 ||
               input_config.format == AUDIO_FORMAT_E_AC3 ||
               input_config.format == AUDIO_FORMAT_AAC ||
               input_config.format == AUDIO_FORMAT_AAC_ADTS) {
        if (qaf_mod->main_output_active == false) {
            if ((flags & AUDIO_OUTPUT_FLAG_MAIN) && (flags & AUDIO_OUTPUT_FLAG_ASSOCIATED)) {
                status = qaf_mod->qaf_audio_stream_open(qaf_mod->session_handle, &out->qaf_stream_handle, input_config, devices, /*flags*/AUDIO_STREAM_MAIN);
                if (status == 0) {
                    ALOGV("%s %d Open stream for Input with both Main and Associated stream contents with flag [%x] and stream handle [%p]", __func__, __LINE__, flags, out->qaf_stream_handle);
                    qaf_mod->main_output_active = true;
                    qaf_mod->assoc_output_active = true;
                }
            } else if (flags & AUDIO_OUTPUT_FLAG_MAIN) {
                status = qaf_mod->qaf_audio_stream_open(qaf_mod->session_handle, &out->qaf_stream_handle, input_config, devices, /*flags*/AUDIO_STREAM_MAIN);
                if (status == 0) {
                    ALOGV("%s %d Open stream for Input with only Main flag [%x] stream handle [%p]", __func__, __LINE__, flags, out->qaf_stream_handle);
                    qaf_mod->main_output_active = true;
                }
            } else if (flags & AUDIO_OUTPUT_FLAG_ASSOCIATED) {
                ALOGE("%s %d Error main input is not active", __func__, __LINE__);
                return -EINVAL;
            } else {
                status = qaf_mod->qaf_audio_stream_open(qaf_mod->session_handle, &out->qaf_stream_handle, input_config, devices, /*flags*/AUDIO_STREAM_MAIN);
                if (status == 0) {
                    ALOGV("%s %d Open stream for Local playback with flag [%x] stream handle [%p] ", __func__, __LINE__, flags, out->qaf_stream_handle);
                    qaf_mod->main_output_active = true;
                }
            }
        } else {
            if (flags & AUDIO_OUTPUT_FLAG_MAIN) {
                ALOGE("%s %d Error main input is already active", __func__, __LINE__);
                return -EINVAL;
            } else if (flags & AUDIO_OUTPUT_FLAG_ASSOCIATED) {
                if (qaf_mod->assoc_output_active) {
                    ALOGE("%s %d Error assoc input is already active", __func__, __LINE__);
                    return -EINVAL;
                } else {
                    status = qaf_mod->qaf_audio_stream_open(qaf_mod->session_handle, &out->qaf_stream_handle, input_config, devices, /*flags*/AUDIO_STREAM_ASSOCIATED);
                    if (status == 0) {
                        ALOGV("%s %d Open stream for Input with only Associated flag [%x] stream handle [%p]", __func__, __LINE__, flags, out->qaf_stream_handle);
                        qaf_mod->assoc_output_active = true;
                    }
                }
            } else {
                ALOGE("%s %d Error main input is already active", __func__, __LINE__);
                return -EINVAL;
            }
        }
    }

    return status;
}

static int audio_extn_qaf_stream_open(struct stream_out *out)
{
    int status = -ENOSYS;
    struct audio_config config;
    audio_devices_t devices;

    config.sample_rate = config.offload_info.sample_rate = QAF_OUTPUT_SAMPLING_RATE;
    config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
    config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
    config.offload_info.format = out->format;
    config.offload_info.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    config.format = out->format;
    config.offload_info.channel_mask = config.channel_mask = out->channel_mask;

    devices = AUDIO_DEVICE_OUT_SPEAKER;
    status = qaf_stream_open(out, &config, out->flags, devices);
    ALOGV("%s %d status %d", __func__, __LINE__, status);
    return status;
}

static int qaf_out_resume(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    lock_output_stream(out);
    if ((!property_get_bool("audio.qaf.reencode", false)) &&
        property_get_bool("audio.qaf.passthrough", false)) {
        if (property_get_bool("audio.offload.passthrough", false)) {
            if (qaf_mod->qaf_passthrough_out && qaf_mod->hdmi_connect &&
                (((out->format == AUDIO_FORMAT_E_AC3) && platform_is_edid_supported_format(qaf_mod->adev->platform, AUDIO_FORMAT_E_AC3)) ||
                ((out->format == AUDIO_FORMAT_AC3) && platform_is_edid_supported_format(qaf_mod->adev->platform, AUDIO_FORMAT_AC3)))) {
                status = qaf_mod->qaf_passthrough_out->stream.resume((struct audio_stream_out*) qaf_mod->qaf_passthrough_out);
                if (!status)
                    out->offload_state = OFFLOAD_STATE_PLAYING;
                pthread_mutex_unlock(&out->lock);
                return status;
            } else {
                if ((out->format == AUDIO_FORMAT_E_AC3) || (out->format == AUDIO_FORMAT_AC3)) {
                    status = audio_extn_qaf_stream_open(out);
                    if (!status)
                        out->offload_state = OFFLOAD_STATE_PLAYING;
                    out->offload_callback(STREAM_CBK_EVENT_WRITE_READY, NULL, out->offload_cookie);
                }
            }
        } else {
            if ((out->format == AUDIO_FORMAT_PCM_16_BIT) && (popcount(out->channel_mask) > 2)) {
                if (qaf_mod->qaf_passthrough_out && qaf_mod->hdmi_connect) {
                    status = qaf_mod->qaf_passthrough_out->stream.resume((struct audio_stream_out*) qaf_mod->qaf_passthrough_out);
                    if (!status)
                        out->offload_state = OFFLOAD_STATE_PLAYING;
                    pthread_mutex_unlock(&out->lock);
                    return status;
                }
            }
        }
    }

    status = qaf_stream_start(out);
    pthread_mutex_unlock(&out->lock);
    ALOGD("%s Exit", __func__);
    return status;
}

static int qaf_deinit()
{
    ALOGV("%s %d", __func__, __LINE__);
    if (qaf_mod != NULL) {
        if (qaf_mod->qaf_compr_offload_out != NULL)
            adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev, (struct audio_stream_out *) (qaf_mod->qaf_compr_offload_out));
        if (qaf_mod->qaf_compr_passthrough_out != NULL)
            adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev, (struct audio_stream_out *) (qaf_mod->qaf_compr_passthrough_out));

        if (qaf_mod->qaf_passthrough_out) {
            adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev, (struct audio_stream_out *) (qaf_mod->qaf_passthrough_out));
            qaf_mod->qaf_passthrough_out = NULL;
        }

        if (qaf_mod->qaf_lib != NULL) {
            dlclose(qaf_mod->qaf_lib);
            qaf_mod->qaf_lib = NULL;
        }
        free(qaf_mod);
        qaf_mod = NULL;
    }
    return 0;
}

static void *qaf_offload_thread_loop(void *context)
{
    struct stream_out *out = (struct stream_out *) context;
    struct listnode *item;
    int ret = 0;
    struct str_parms *parms = NULL;
    int value = 0;
    char* kvpairs = NULL;

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
    set_sched_policy(0, SP_FOREGROUND);
    prctl(PR_SET_NAME, (unsigned long)"Offload Callback", 0, 0, 0);

    ALOGV("%s", __func__);
    lock_output_stream(out);
    for (;;) {
        struct offload_cmd *cmd = NULL;
        stream_callback_event_t event;
        bool send_callback = false;

        ALOGV("%s qaf_offload_cmd_list %d",
              __func__, list_empty(&out->qaf_offload_cmd_list));
        if (list_empty(&out->qaf_offload_cmd_list)) {
            ALOGV("%s SLEEPING", __func__);
            pthread_cond_wait(&out->qaf_offload_cond, &out->lock);
            ALOGV("%s RUNNING", __func__);
            continue;
        }

        item = list_head(&out->qaf_offload_cmd_list);
        cmd = node_to_item(item, struct offload_cmd, node);
        list_remove(item);

        if (cmd->cmd == OFFLOAD_CMD_EXIT) {
            free(cmd);
            break;
        }

        pthread_mutex_unlock(&out->lock);
        send_callback = false;
        switch(cmd->cmd) {
        case OFFLOAD_CMD_WAIT_FOR_BUFFER:
            ALOGV("wait for ms12 buffer availability");
            while (1) {
                kvpairs = qaf_mod->qaf_audio_stream_get_param(out->qaf_stream_handle, "buf_available");
                if (kvpairs) {
                    parms = str_parms_create_str(kvpairs);
                    ret = str_parms_get_int(parms, "buf_available", &value);
                    if (ret >= 0) {
                        if (value >= (int)out->compr_config.fragment_size) {
                            ALOGV("%s buffer available", __func__);
                            str_parms_destroy(parms);
                            parms = NULL;
                            break;
                        } else {
                            ALOGV("%s sleep", __func__);
                            str_parms_destroy(parms);
                            parms = NULL;
                            usleep(10000);
                        }
                    }
                    free(kvpairs);
                    kvpairs = NULL;
                }
            }
            send_callback = true;
            event = STREAM_CBK_EVENT_WRITE_READY;
            break;
        default:
            ALOGV("%s unknown command received: %d", __func__, cmd->cmd);
            break;
        }
        lock_output_stream(out);
        if (send_callback && out->offload_callback) {
            out->offload_callback(event, NULL, out->offload_cookie);
        }
        free(cmd);
    }

    while (!list_empty(&out->qaf_offload_cmd_list)) {
        item = list_head(&out->qaf_offload_cmd_list);
        list_remove(item);
        free(node_to_item(item, struct offload_cmd, node));
    }
    pthread_mutex_unlock(&out->lock);

    return NULL;
}

static int qaf_create_offload_callback_thread(struct stream_out *out)
{
    ALOGV("%s", __func__);
    pthread_cond_init(&out->qaf_offload_cond, (const pthread_condattr_t *) NULL);
    list_init(&out->qaf_offload_cmd_list);
    pthread_create(&out->qaf_offload_thread, (const pthread_attr_t *) NULL,
                    qaf_offload_thread_loop, out);
    return 0;
}

static int qaf_destroy_offload_callback_thread(struct stream_out *out)
{
    ALOGV("%s", __func__);
    lock_output_stream(out);
    qaf_send_offload_cmd_l(out, OFFLOAD_CMD_EXIT);
    pthread_mutex_unlock(&out->lock);

    pthread_join(out->qaf_offload_thread, (void **) NULL);
    pthread_cond_destroy(&out->qaf_offload_cond);

    return 0;
}

static int qaf_out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct str_parms *parms, *new_parms;
    char value[32];
    char *new_kv_pairs;
    int val = 0;
    struct stream_out *out = (struct stream_out *)stream;
    int ret = 0;
    int err = 0;

    ALOGV("%s: enter: usecase(%d: %s) kvpairs: %s",
          __func__, out->usecase, use_case_table[out->usecase], kvpairs);
    if ((NULL != qaf_mod->qaf_compr_offload_out)) {
        if (qaf_mod->qaf_msmd_enabled) {
            if (qaf_mod->passthrough_enabled && qaf_mod->hdmi_connect)
                return 1;

            parms = str_parms_create_str(kvpairs);
            err = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));

            /* usecase : hdmi sink which supports only 2-channel pcm */
            if (err >= 0) {
                val = atoi(value);
                if ((val & AUDIO_DEVICE_OUT_AUX_DIGITAL) &&
                        ((qaf_mod->hdmi_sink_channels == 2) && !(qaf_mod->passthrough_enabled))) {
                    new_parms = str_parms_create();
                    val |= AUDIO_DEVICE_OUT_SPEAKER;
                    str_parms_add_int(new_parms, AUDIO_PARAMETER_STREAM_ROUTING, val);
                    new_kv_pairs = str_parms_to_str(new_parms);
                    qaf_mod->qaf_compr_offload_out->stream.common.set_parameters((struct audio_stream *) qaf_mod->qaf_compr_offload_out, new_kv_pairs);
                    free(new_kv_pairs);
                    str_parms_destroy(new_parms);
                }
            }
            str_parms_destroy(parms);
        } else {
            if (!(qaf_mod->passthrough_enabled && qaf_mod->hdmi_connect))
                qaf_mod->qaf_compr_offload_out->stream.common.set_parameters((struct audio_stream *) qaf_mod->qaf_compr_offload_out, kvpairs);

            parms = str_parms_create_str(kvpairs);
            if (!parms) {
                ALOGE("str_parms_create_str failed!");
            } else {
                err = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
                if (err >= 0) {
                    val = atoi(value);
                    if (val == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) { //BT routing
                        if (audio_extn_bt_hal_get_output_stream(qaf_mod->bt_hdl) == NULL && audio_extn_bt_hal_get_device(qaf_mod->bt_hdl) != NULL) {
                            ret = audio_extn_bt_hal_open_output_stream(qaf_mod->bt_hdl,
                                    QAF_OUTPUT_SAMPLING_RATE,
                                    AUDIO_CHANNEL_OUT_STEREO,
                                    CODEC_BACKEND_DEFAULT_BIT_WIDTH);
                            if (ret != 0) {
                                ALOGE("%s: BT Output stream open failure!", __FUNCTION__);
                            }
                        }
                    } else if (val != 0) {
                        if (audio_extn_bt_hal_get_output_stream(qaf_mod->bt_hdl)!= NULL) {
                            audio_extn_bt_hal_close_output_stream(qaf_mod->bt_hdl);
                        }
                    }
                }
                str_parms_destroy(parms);
            }
        }
    }

    if (audio_extn_qaf_passthrough_enabled(out)) {
        parms = str_parms_create_str(kvpairs);
        if (!parms) {
            ALOGE("str_parms_create_str failed!");
        } else {
            err = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
            if (err >= 0) {
                val = atoi(value);
                if ((val & AUDIO_DEVICE_OUT_AUX_DIGITAL) &&
                    (qaf_mod->qaf_passthrough_out == NULL)) {
                        audio_output_flags_t flags;
                        struct audio_config config;
                        audio_devices_t devices;

                        config.sample_rate = config.offload_info.sample_rate = QAF_OUTPUT_SAMPLING_RATE;
                        config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                        config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
                        config.offload_info.format = out->format;
                        config.offload_info.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
                        config.format = out->format;
                        config.offload_info.channel_mask = config.channel_mask = out->channel_mask;

                        devices = AUDIO_DEVICE_OUT_AUX_DIGITAL;
                        flags = out->flags;

                        if (out->qaf_stream_handle) {
                            qaf_out_pause((struct audio_stream_out*)out);
                            qaf_out_flush((struct audio_stream_out*)out);
                            qaf_out_drain((struct audio_stream_out*)out, (audio_drain_type_t)STREAM_CBK_EVENT_DRAIN_READY);
                            qaf_stream_close(out);
                        }
                        create_output_stream(out, &config, flags, devices, QAF_DEFAULT_PASSTHROUGH_HANDLE);
                        qaf_mod->main_output_active = true;
                }
            }
            str_parms_destroy(parms);
        }
    }

    return ret;
}

int audio_extn_qaf_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address __unused)
{
    int ret = 0;
    struct stream_out *out;

    ret = adev_open_output_stream(dev, handle, devices, flags, config, stream_out, address);
    if (*stream_out == NULL) {
        goto error_open;
    }

    if ( false == is_ms12_format(config->format) ) {
        ALOGV("%s: exiting qaf for non-ms12 format %x", __func__, config->format);
        return ret;
    }

    out = (struct stream_out *) *stream_out;

    /* Override function pointers based on qaf definitions */
    out->stream.set_volume = qaf_out_set_volume;
    out->stream.pause = qaf_out_pause;
    out->stream.resume = qaf_out_resume;
    out->stream.drain = qaf_out_drain;
    out->stream.flush = qaf_out_flush;

    out->stream.common.standby = qaf_out_standby;
    out->stream.common.set_parameters = qaf_out_set_parameters;
    out->stream.get_latency = qaf_out_get_latency;
    out->stream.write = qaf_out_write;
    out->stream.get_presentation_position = qaf_out_get_presentation_position;
    out->platform_latency = 0;
    ret = qaf_stream_open(out, config, flags, devices);
    if (ret < 0) {
        ALOGE("%s, Error opening QAF stream err[%d]!", __func__, ret);
        adev_close_output_stream(dev, *stream_out);
        goto error_open;
    }

    if (out->usecase == USECASE_AUDIO_PLAYBACK_LOW_LATENCY) {
        out->usecase = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
        out->config.period_size = DEEP_BUFFER_OUTPUT_PERIOD_SIZE;
        out->config.period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT;
        out->config.start_threshold = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4;
        out->config.avail_min = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4;
    }

    *stream_out = &out->stream;
    if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        qaf_create_offload_callback_thread(out);
    }
    ALOGV("%s: exit", __func__);
    return 0;
error_open:
    *stream_out = NULL;
    ALOGD("%s: exit: ret %d", __func__, ret);
    return ret;
}

void audio_extn_qaf_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    ALOGV("%s: enter:stream_handle(%p) format = %x", __func__, out, out->format);
    if (qaf_mod->qaf_passthrough_out) {
        ALOGD("%s %d closing stream handle %p", __func__, __LINE__, qaf_mod->qaf_passthrough_out);
        pthread_mutex_lock(&qaf_mod->lock);
        adev_close_output_stream((struct audio_hw_device *) qaf_mod->adev, (struct audio_stream_out *) (qaf_mod->qaf_passthrough_out));
        adev_close_output_stream(dev, stream);
        qaf_mod->qaf_passthrough_out = NULL;
        qaf_mod->main_output_active = false;
        pthread_mutex_unlock(&qaf_mod->lock);
        return;
    }

    if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        qaf_destroy_offload_callback_thread(out);
    }
    qaf_mod->stream_drain_main = NULL;
    lock_output_stream(out);
    qaf_stream_close(out);
    pthread_mutex_unlock(&out->lock);

    adev_close_output_stream(dev, stream);
    ALOGV("%s: exit", __func__);
}

bool audio_extn_qaf_is_enabled()
{
    bool prop_enabled = false;
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("audio.qaf.enabled", value, NULL);
    prop_enabled = atoi(value) || !strncmp("true", value, 4);
    return (prop_enabled);
}

int audio_extn_qaf_session_open(struct qaf *qaf_mod,
                                device_license_config_t* lic_config)
{
    ALOGV("%s %d", __func__, __LINE__);
    int status = -ENOSYS;

    pthread_mutex_init(&qaf_mod->lock, (const pthread_mutexattr_t *) NULL);

    if (!qaf_mod->qaf_audio_session_open)
       return -EINVAL;

    status = qaf_mod->qaf_audio_session_open(&qaf_mod->session_handle,
                                             (void *)(qaf_mod), (void *)lic_config);
    if(status < 0)
        return status;

    if (qaf_mod->session_handle == NULL) {
        ALOGE("%s %d QAF wrapper session handle is NULL", __func__, __LINE__);
        return -ENOMEM;
    }
    if (qaf_mod->qaf_register_event_callback)
        qaf_mod->qaf_register_event_callback(qaf_mod->session_handle,
                                             qaf_mod, &notify_event_callback,
                                             AUDIO_DATA_EVENT);

    if (property_get_bool("audio.qaf.msmd", false)) {
        qaf_mod->qaf_msmd_enabled = 1;
    }

    return status;
}

char* audio_extn_qaf_stream_get_param(struct stream_out *out __unused, const char *kv_pair __unused)
{
   return NULL;
}

int audio_extn_qaf_set_parameters(struct audio_device *adev, struct str_parms *parms)
{
    int status = 0, val = 0, channels = 0;
    char *format_params, *kv_parirs;
    struct str_parms *qaf_params;
    char value[32];
    char prop_value[PROPERTY_VALUE_MAX];
    bool passth_support = false;

    ALOGV("%s %d ", __func__, __LINE__);
    if (!qaf_mod || !qaf_mod->qaf_audio_session_set_param) {
        return -EINVAL;
    }

    status = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value, sizeof(value));
    if (status >= 0) {
        val = atoi(value);
        if (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            if (property_get_bool("audio.offload.passthrough", false) &&
                property_get_bool("audio.qaf.reencode", false)) {

                qaf_params = str_parms_create();
                property_get("audio.qaf.hdmi.out", prop_value, NULL);
                if (platform_is_edid_supported_format(adev->platform, AUDIO_FORMAT_E_AC3) &&
                                                          (strncmp(prop_value, "ddp", 3) == 0)) {
                    passth_support = true;
                    if (qaf_params) {
                        str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_RENDER_FORMAT,
                                              AUDIO_QAF_PARAMETER_VALUE_REENCODE_EAC3);
                    }
                } else if (platform_is_edid_supported_format(adev->platform, AUDIO_FORMAT_AC3)) {
                    passth_support = true;
                    if (qaf_params) {
                        str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_RENDER_FORMAT,
                                              AUDIO_QAF_PARAMETER_VALUE_REENCODE_AC3);
                    }
                }

                if (passth_support) {
                    qaf_mod->passthrough_enabled = 1;
                    if (qaf_mod->qaf_msmd_enabled) {
                        str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                                    AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI_AND_SPK);
                    } else {
                        str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                              AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI);
                    }
                    format_params = str_parms_to_str(qaf_params);

                    qaf_mod->qaf_audio_session_set_param(qaf_mod->session_handle, format_params);
                }
                str_parms_destroy(qaf_params);
            }

            if (!passth_support) {
                channels = platform_edid_get_max_channels(adev->platform);

                qaf_params = str_parms_create();
                switch (channels) {
                    case 8:
                          ALOGV("%s: Switching Qaf output to 7.1 channels", __func__);
                          str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_CHANNELS,
                                                AUDIO_QAF_PARAMETER_VALUE_8_CHANNELS);
                          if (qaf_mod->qaf_msmd_enabled) {
                              str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                                    AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI_AND_SPK);
                          } else {
                              str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                                    AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI);
                          }
                          qaf_mod->hdmi_sink_channels = channels;
                          break;
                    case 6:
                          ALOGV("%s: Switching Qaf output to 5.1 channels", __func__);
                          str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_CHANNELS,
                                                AUDIO_QAF_PARAMETER_VALUE_6_CHANNELS);
                          if (qaf_mod->qaf_msmd_enabled) {
                              str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                                    AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI_AND_SPK);
                          } else {
                              str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                                    AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI);
                          }
                          qaf_mod->hdmi_sink_channels = channels;
                          break;
                    default:
                          ALOGV("%s: Switching Qaf output to default channels", __func__);
                          str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_CHANNELS,
                                                AUDIO_QAF_PARAMETER_VALUE_DEFAULT_CHANNELS);
                          if (qaf_mod->qaf_msmd_enabled) {
                              str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                                    AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI_AND_SPK);
                          } else {
                              str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                                    AUDIO_QAF_PARAMETER_VALUE_DEVICE_SPEAKER);
                          }
                          qaf_mod->hdmi_sink_channels = 2;
                        break;
                }

                format_params = str_parms_to_str(qaf_params);
                qaf_mod->qaf_audio_session_set_param(qaf_mod->session_handle, format_params);
                str_parms_destroy(qaf_params);
            }
            qaf_mod->hdmi_connect = 1;
        } else if (val & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
            ALOGV("%s: Opening a2dp output...", __FUNCTION__);
            status = audio_extn_bt_hal_load(&qaf_mod->bt_hdl);
            if(status != 0) {
                ALOGE("%s:Error opening BT module", __FUNCTION__);
                return status;
            }
        }
    }

    status = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value, sizeof(value));
    if (status >= 0) {
        val = atoi(value);
        if (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            qaf_params = str_parms_create();
            str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                  AUDIO_QAF_PARAMETER_VALUE_DEVICE_SPEAKER);
            str_parms_add_str(qaf_params, AUDIO_QAF_PARAMETER_KEY_RENDER_FORMAT,
                                  AUDIO_QAF_PARAMETER_VALUE_PCM);
            qaf_mod->hdmi_sink_channels = 0;

            qaf_mod->passthrough_enabled = 0;
            qaf_mod->hdmi_connect = 0;
            format_params = str_parms_to_str(qaf_params);
            qaf_mod->qaf_audio_session_set_param(qaf_mod->session_handle, format_params);
            str_parms_destroy(qaf_params);
        } else if (val & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
            ALOGV("%s: Closing a2dp output...", __FUNCTION__);
            audio_extn_bt_hal_unload(qaf_mod->bt_hdl);
        }
    }

    kv_parirs = str_parms_to_str(parms);
    qaf_mod->qaf_audio_session_set_param(qaf_mod->session_handle, kv_parirs);

    return status;
}

char* audio_extn_qaf_get_param(struct audio_device *adev __unused, const char *kv_pair __unused)
{
    return 0;
}

int audio_extn_qaf_init(struct audio_device *adev)
{
    char value[PROPERTY_VALUE_MAX] = {0};
    char lib_name[PROPERTY_VALUE_MAX] = {0};
    unsigned char* license_data = NULL;
    device_license_config_t* lic_config = NULL;
    ALOGV("%s %d", __func__, __LINE__);
    int ret = 0, size = 0;

    qaf_mod = malloc(sizeof(struct qaf));
    if(qaf_mod == NULL) {
        ALOGE("%s, out of memory", __func__);
        ret = -ENOMEM;
        goto done;
    }
    memset(qaf_mod, 0, sizeof(struct qaf));
    lic_config = (device_license_config_t*) calloc(1, sizeof(device_license_config_t));
    if(lic_config == NULL) {
        ALOGE("%s, out of memory", __func__);
        ret = -ENOMEM;
        goto done;
    }
    qaf_mod->adev = adev;
    property_get("audio.qaf.library", value, NULL);
    snprintf(lib_name, PROPERTY_VALUE_MAX, "%s", value);

    license_data = platform_get_license((struct audio_hw_device *)(qaf_mod->adev->platform), &size);
    if (!license_data) {
        ALOGE("License is not present");
        ret = -EINVAL;
        goto done;
    }
    lic_config->p_license = (unsigned char* ) calloc(1, size);
     if(lic_config->p_license == NULL) {
        ALOGE("%s, out of memory", __func__);
        ret = -ENOMEM;
        goto done;
    }
    lic_config->l_size = size;
    memcpy(lic_config->p_license, license_data, size);

    if (property_get("audio.qaf.manufacturer", value, "") && atoi(value)) {
        lic_config->manufacturer_id = (unsigned long) atoi (value);
    } else {
        ALOGE("audio.qaf.manufacturer id is not set");
        ret = -EINVAL;
        goto done;
    }

    ret = audio_extn_qaf_session_open(qaf_mod, lic_config);
done:
    if (license_data != NULL) {
        free(license_data);
        license_data = NULL;
    }
    if (lic_config->p_license != NULL) {
        free(lic_config->p_license);
        lic_config->p_license = NULL;
    }
    if (lic_config != NULL) {
        free(lic_config);
        lic_config = NULL;
    }
    if (ret != 0) {
        if (qaf_mod != NULL) {
            free(qaf_mod);
            qaf_mod = NULL;
        }
    }
    return ret;
}

void audio_extn_qaf_deinit()
{
    qaf_session_close();
    qaf_deinit();
}
