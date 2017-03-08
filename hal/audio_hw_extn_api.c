/*
* Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "qahwi"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <cutils/log.h>

#include <hardware/audio.h>
#include "sound/compress_params.h"
#include "audio_hw.h"
#include "audio_extn.h"
#include "audio_hw_extn_api.h"

/* default timestamp metadata definition if not defined in kernel*/
#ifndef COMPRESSED_TIMESTAMP_FLAG
#define COMPRESSED_TIMESTAMP_FLAG 0
struct snd_codec_metadata {
uint64_t timestamp;
};
#endif

static void lock_output_stream(struct stream_out *out)
{
    pthread_mutex_lock(&out->pre_lock);
    pthread_mutex_lock(&out->lock);
    pthread_mutex_unlock(&out->pre_lock);
}

/* API to send playback stream specific config parameters */
int qahwi_out_set_param_data(struct audio_stream_out *stream __unused,
                             audio_extn_param_id param_id __unused,
                             audio_extn_param_payload *payload __unused) {
    return -ENOSYS;
}

/* API to get playback stream specific config parameters */
int qahwi_out_get_param_data(struct audio_stream_out *stream,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload)
{
    int ret = -EINVAL;
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_usecase *uc_info;

    if (!stream || !payload) {
        ALOGE("%s:: Invalid Param",__func__);
        return ret;
    }

    lock_output_stream(out);
    ALOGD("%s: enter: stream (%p) usecase(%d: %s) param_id %d", __func__,
           stream, out->usecase, use_case_table[out->usecase], param_id);

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

    pthread_mutex_unlock(&out->lock);
    return ret;
}

int qahwi_get_param_data(const struct audio_hw_device *adev,
                         audio_extn_param_id param_id,
                         audio_extn_param_payload *payload)
{
    int ret = 0;
    const struct audio_device *dev = (const struct audio_device *)adev;

    if (adev == NULL) {
        ALOGE("%s::INVALID PARAM adev\n",__func__);
        return -EINVAL;
    }

    if (payload == NULL) {
        ALOGE("%s::INVALID PAYLOAD VALUE\n",__func__);
        return -EINVAL;
    }

    switch (param_id) {
        case AUDIO_EXTN_PARAM_SOUND_FOCUS:
              ret = audio_extn_get_soundfocus_data(dev,
                     (struct sound_focus_param *)payload);
              break;
        case AUDIO_EXTN_PARAM_SOURCE_TRACK:
              ret = audio_extn_get_sourcetrack_data(dev,
                     (struct source_tracking_param*)payload);
              break;
       default:
             ALOGE("%s::INVALID PARAM ID:%d\n",__func__,param_id);
             ret = -EINVAL;
             break;
    }
    return ret;
}

int qahwi_set_param_data(struct audio_hw_device *adev,
                         audio_extn_param_id param_id,
                         audio_extn_param_payload *payload)
{
    int ret = 0;
    struct audio_device *dev = (struct audio_device *)adev;

    if (adev == NULL) {
        ALOGE("%s::INVALID PARAM adev\n",__func__);
        return -EINVAL;
    }

    if (payload == NULL) {
        ALOGE("%s::INVALID PAYLOAD VALUE\n",__func__);
        return -EINVAL;
    }

    switch (param_id) {
        case AUDIO_EXTN_PARAM_SOUND_FOCUS:
              ret = audio_extn_set_soundfocus_data(dev,
                     (struct sound_focus_param *)payload);
              break;
        case AUDIO_EXTN_PARAM_APTX_DEC:
              audio_extn_set_aptx_dec_params((struct aptx_dec_param *)payload);
              break;
       default:
             ALOGE("%s::INVALID PARAM ID:%d\n",__func__,param_id);
             ret = -EINVAL;
             break;
    }
    return ret;
}

ssize_t qahwi_in_read_v2(struct audio_stream_in *stream, void* buffer,
                          size_t bytes, uint64_t *timestamp)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct snd_codec_metadata *mdata = NULL;
    size_t mdata_size = 0, bytes_read = 0;
    char *buf = NULL;
    size_t ret = 0;

    if (!in->qahwi_in.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
        return -EINVAL;
    }
    if (COMPRESSED_TIMESTAMP_FLAG &&
        (in->flags & AUDIO_INPUT_FLAG_TIMESTAMP)) {
        if (bytes != in->stream.common.get_buffer_size(&stream->common)) {
            ALOGE("%s: bytes requested must be fragment size in timestamp mode!", __func__);
            return -EINVAL;
        }
        mdata_size = sizeof(struct snd_codec_metadata);
        buf = (char *) in->qahwi_in.ibuf;
        ret = in->qahwi_in.base.read(&in->stream, (void *)buf, bytes + mdata_size);
        if (ret == bytes + mdata_size) {
           bytes_read = bytes;
           memcpy(buffer, buf + mdata_size, bytes);
           if (timestamp) {
               mdata = (struct snd_codec_metadata *) buf;
               *timestamp = mdata->timestamp;
           }
        } else {
           ALOGE("%s: error! read returned %zd", __func__, ret);
        }
    } else {
        bytes_read = in->qahwi_in.base.read(stream, buffer, bytes);
        if (timestamp)
            *timestamp = (uint64_t ) -1;
    }
    ALOGV("%s: flag 0x%x, bytes %zd, read %zd, ret %zd",
          __func__, in->flags, bytes, bytes_read, ret);
    return bytes_read;
}

static void qahwi_close_input_stream(struct audio_hw_device *dev,
                               struct audio_stream_in *stream_in)
{
    struct audio_device *adev = (struct audio_device *) dev;
    struct stream_in *in = (struct stream_in *)stream_in;

    ALOGV("%s", __func__);
    if (!adev->qahwi_dev.is_inititalized || !in->qahwi_in.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
        return;
    }
    if (in->qahwi_in.ibuf)
        free(in->qahwi_in.ibuf);
    adev->qahwi_dev.base.close_input_stream(dev, stream_in);
}

static int qahwi_open_input_stream(struct audio_hw_device *dev,
                             audio_io_handle_t handle,
                             audio_devices_t devices,
                             struct audio_config *config,
                             struct audio_stream_in **stream_in,
                             audio_input_flags_t flags,
                             const char *address,
                             audio_source_t source)
{
    struct audio_device *adev = (struct audio_device *) dev;
    struct stream_in *in = NULL;
    size_t buf_size = 0, mdata_size = 0;
    int ret = 0;

    ALOGV("%s: dev_init %d, flags 0x%x", __func__,
              adev->qahwi_dev.is_inititalized, flags);
    if (!adev->qahwi_dev.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
        return -EINVAL;
    }

    ret = adev->qahwi_dev.base.open_input_stream(dev, handle, devices, config,
                                                 stream_in, flags, address, source);
    if (ret)
        return ret;

    in = (struct stream_in *)*stream_in;
    // keep adev fptrs before overriding
    in->qahwi_in.base = in->stream;

    in->qahwi_in.is_inititalized = true;

    if (COMPRESSED_TIMESTAMP_FLAG &&
        (flags & AUDIO_INPUT_FLAG_TIMESTAMP)) {
        // set read to NULL as this is not supported in timestamp mode
        in->stream.read = NULL;

        mdata_size = sizeof(struct snd_codec_metadata);
        buf_size = mdata_size +
                   in->qahwi_in.base.common.get_buffer_size(&in->stream.common);

        in->qahwi_in.ibuf = malloc(buf_size);
        if (!in->qahwi_in.ibuf) {
            ALOGE("%s: allocation failed for timestamp metadata!", __func__);
            qahwi_close_input_stream(dev, &in->stream);
            *stream_in = NULL;
            ret = -ENOMEM;
        }
        ALOGD("%s: ibuf %p, buff_size %zd",
              __func__, in->qahwi_in.ibuf, buf_size);
    }
    return ret;
}

void qahwi_init(hw_device_t *device)
{
    struct audio_device *adev = (struct audio_device *) device;

    ALOGD("%s", __func__);

    // keep adev fptrs before overriding,
    // as it might be used internally by overriding implementation
    adev->qahwi_dev.base = adev->device;

    adev->device.open_input_stream = qahwi_open_input_stream;
    adev->device.close_input_stream = qahwi_close_input_stream;

    adev->qahwi_dev.is_inititalized = true;
}
void qahwi_deinit(hw_device_t *device)
{
    struct audio_device *adev = (struct audio_device *) device;

    ALOGV("%s: is_initialized %d", __func__, adev->qahwi_dev.is_inititalized);
    if (!adev->qahwi_dev.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
    }
}

