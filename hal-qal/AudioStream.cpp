/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#define LOG_TAG "AHAL: AudioStream"
#define ATRACE_TAG (ATRACE_TAG_AUDIO | ATRACE_TAG_HAL)
/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include "AudioDevice.h"
#include "AudioStream.h"

#include <log/log.h>
#include <utils/Trace.h>
#include <cutils/properties.h>

#include <chrono>
#include <thread>

#include "QalApi.h"
#include <audio_effects/effect_aec.h>
#include <audio_effects/effect_ns.h>
#include "audio_extn.h"
#include <audio_utils/format.h>

#define COMPRESS_OFFLOAD_FRAGMENT_SIZE (32 * 1024)
#define FLAC_COMPRESS_OFFLOAD_FRAGMENT_SIZE (256 * 1024)

#define MAX_READ_RETRY_COUNT 25

void StreamOutPrimary::GetStreamHandle(audio_stream_out** stream) {
  *stream = (audio_stream_out*)stream_.get();
}

void StreamInPrimary::GetStreamHandle(audio_stream_in** stream) {
  *stream = (audio_stream_in*)stream_.get();
}

uint32_t StreamPrimary::GetSampleRate() {
    return config_.sample_rate;
}

audio_format_t StreamPrimary::GetFormat() {
    return config_.format;
}

uint32_t StreamPrimary::GetChannelMask() {
    return config_.channel_mask;
}

audio_io_handle_t StreamPrimary::GetHandle()
{
    return handle_;
}

int StreamPrimary::GetUseCase()
{
    return usecase_;
}

#if 0
static qal_stream_type_t GetQalStreamType(audio_output_flags_t flags) {
    std::ignore = flags;
    return QAL_STREAM_LOW_LATENCY;
}
#endif
//audio_hw_device_t* AudioDevice::device_ = NULL;
std::shared_ptr<AudioDevice> AudioDevice::adev_ = nullptr;
std::shared_ptr<audio_hw_device_t> AudioDevice::device_ = nullptr;

static int32_t qal_callback(qal_stream_handle_t *stream_handle,
                            uint32_t event_id, uint32_t *event_data,
                            uint32_t event_size, void *cookie)
{
    stream_callback_event_t event;
    StreamOutPrimary *astream_out = static_cast<StreamOutPrimary *> (cookie);

    ALOGD("%s: stream_handle (%p), event_id (%x), event_data (%p), cookie (%p)"
          "event_size (%d)", __func__, stream_handle, event_id, event_data,
           cookie, event_size);

    switch (event_id)
    {
        case QAL_STREAM_CBK_EVENT_WRITE_READY:
        {
            std::lock_guard<std::mutex> write_guard (astream_out->write_wait_mutex_);
            astream_out->write_ready_ = true;
            ALOGD("%s: received WRITE_READY event", __func__);
            (astream_out->write_condition_).notify_all();
            event = STREAM_CBK_EVENT_WRITE_READY;
        }
        break;

    case QAL_STREAM_CBK_EVENT_DRAIN_READY:
        {
            std::lock_guard<std::mutex> drain_guard (astream_out->drain_wait_mutex_);
            astream_out->drain_ready_ = true;
            ALOGD("%s: received DRAIN_READY event", __func__);
            (astream_out->drain_condition_).notify_all();
            event = STREAM_CBK_EVENT_DRAIN_READY;
            }
        break;
    case QAL_STREAM_CBK_EVENT_ERROR:
        event = STREAM_CBK_EVENT_ERROR;
        break;
    default:
        ALOGE("%s: Invalid event id:%d", __func__, event_id);
        return -EINVAL;
    }

    if (astream_out && astream_out->client_callback)
        astream_out->client_callback(event, NULL, astream_out->client_cookie);

    return 0;
}


static int astream_out_mmap_noirq_start(const struct audio_stream_out *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        ALOGE("%s: unable to get audio OutStream", __func__);
        return -EINVAL;
    }

    return astream_out->Start();
}

static int astream_out_mmap_noirq_stop(const struct audio_stream_out *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        ALOGE("%s: unable to get audio OutStream", __func__);
        return -EINVAL;
    }

    return astream_out->Stop();
}

static int astream_out_create_mmap_buffer(const struct audio_stream_out *stream,
        int32_t min_size_frames, struct audio_mmap_buffer_info *info)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;
    int ret = 0;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        ALOGE("%s: unable to get audio OutStream", __func__);
        return -EINVAL;
    }

    if (info == NULL || !(min_size_frames > 0 && min_size_frames < INT32_MAX)) {
        ALOGE("%s: info = %p, min_size_frames = %d", __func__, info, min_size_frames);
        return -EINVAL;
    }
    if (astream_out->GetUseCase() != USECASE_AUDIO_PLAYBACK_MMAP) {
         ALOGE("%s: usecase = %d", __func__, astream_out->GetUseCase());
         return -ENOSYS;
    }

    ret = astream_out->CreateMmapBuffer(min_size_frames, info);
    if (ret)
        ALOGE("%s: failed %d\n", __func__, ret);

    return ret;
}

static int astream_out_get_mmap_position(const struct audio_stream_out *stream,
        struct audio_mmap_position *position)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        ALOGE("%s: unable to get audio OutStream", __func__);
        return -EINVAL;
    }
    if (astream_out->GetUseCase() != USECASE_AUDIO_PLAYBACK_MMAP) {
         ALOGE("%s: usecase = %d", __func__, astream_out->GetUseCase());
         return -ENOSYS;
    }

    return astream_out->GetMmapPosition(position);
}

static uint32_t astream_out_get_sample_rate(const struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return 0;
    }

    if (astream_out)
        return astream_out->GetSampleRate();
    else
        return 0;
}

static int astream_set_sample_rate(struct audio_stream *stream __unused,
                                   uint32_t rate __unused) {
    return -ENOSYS;
}

static audio_format_t astream_out_get_format(
                                const struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (adevice)
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    else
        ALOGE("%s: unable to get audio device", __func__);

    if (astream_out)
        return astream_out->GetFormat();
    else
        return AUDIO_FORMAT_DEFAULT;
}

static int astream_out_get_next_write_timestamp(
                                const struct audio_stream_out *stream __unused,
                                int64_t *timestamp __unused) {
    return -ENOSYS;
}

static int astream_set_format(struct audio_stream *stream __unused,
                              audio_format_t format __unused) {
    return -ENOSYS;
}

static size_t astream_out_get_buffer_size(const struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out =
                                    adevice->OutGetStream((audio_stream_t*)stream);

    if (astream_out)
        return astream_out->GetBufferSize();
    else
        return 0;
}

static uint32_t astream_out_get_channels(const struct audio_stream *stream) {

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;
    ALOGD("%s: stream_out(%p)", __func__, stream);
    if (adevice != nullptr) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return 0;
    }

    if (astream_out != nullptr) {
        return astream_out->GetChannelMask();
    } else {
        ALOGE("%s: unable to get audio stream", __func__);
        return 0;
    }
}

static int astream_pause(struct audio_stream_out *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }

    ALOGD("%s: pause", __func__);
    return astream_out->Pause();
}

static int astream_resume(struct audio_stream_out *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }

    return astream_out->Resume();
}

static int astream_flush(struct audio_stream_out *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }

    return astream_out->Flush();
}

static int astream_drain(struct audio_stream_out *stream, audio_drain_type_t type)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }

    return astream_out->Drain(type);
}

static int astream_set_callback(struct audio_stream_out *stream, stream_callback_t callback, void *cookie)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!callback) {
        ALOGE("%s: NULL Callback passed", __func__);
        return -EINVAL;
    }

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }

    astream_out->client_callback = callback;
    astream_out->client_cookie = cookie;

    return 0;
}

static int astream_out_standby(struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    ALOGD("%s: enter: stream (%p), usecase(%d: %s)", __func__, astream_out.get(),
          astream_out->GetUseCase(), use_case_table[astream_out->GetUseCase()]);

    if (astream_out) {
        return astream_out->Standby();
    } else {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }
}

static int astream_dump(const struct audio_stream *stream, int fd) {
    std::ignore = stream;
    std::ignore = fd;
    ALOGD("%s: dump function not implemented", __func__);
    return 0;
}

static uint32_t astream_get_latency(const struct audio_stream_out *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;
    uint32_t period_ms, latency = 0;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    switch (astream_out->GetUseCase()) {
    case USECASE_AUDIO_PLAYBACK_OFFLOAD:
        //TODO: get dsp latency for compress usecase
        latency = COMPRESS_OFFLOAD_PLAYBACK_LATENCY;
        break;
    case USECASE_AUDIO_PLAYBACK_ULL:
    case USECASE_AUDIO_PLAYBACK_MMAP:
        period_ms = (ULL_PERIOD_MULTIPLIER * ULL_PERIOD_SIZE *
                1000) / DEFAULT_OUTPUT_SAMPLING_RATE;
        latency = period_ms +
            StreamOutPrimary::GetRenderLatency(astream_out->flags_) / 1000;
        break;
    case USECASE_AUDIO_PLAYBACK_OFFLOAD2:
        latency = PCM_OFFLOAD_OUTPUT_PERIOD_DURATION;
        latency += StreamOutPrimary::GetRenderLatency(astream_out->flags_) / 1000;
        break;
    case USECASE_AUDIO_PLAYBACK_DEEP_BUFFER:
        latency = DEEP_BUFFER_OUTPUT_PERIOD_DURATION;
        latency += StreamOutPrimary::GetRenderLatency(astream_out->flags_) / 1000;
        break;
    case USECASE_AUDIO_PLAYBACK_LOW_LATENCY:
        latency = LOW_LATENCY_OUTPUT_PERIOD_DURATION;
        latency += StreamOutPrimary::GetRenderLatency(astream_out->flags_) / 1000;
        break;
    case USECASE_AUDIO_PLAYBACK_VOIP:
        latency += (VOIP_PERIOD_COUNT_DEFAULT * DEFAULT_VOIP_BUF_DURATION_MS * DEFAULT_VOIP_BIT_DEPTH_BYTE)/2;
        break;
    default:
        latency += StreamOutPrimary::GetRenderLatency(astream_out->flags_) / 1000;
        break;
    }

    // accounts for A2DP encoding and sink latency
    qal_param_bta2dp_t *param_bt_a2dp = NULL;
    size_t size = 0;
    int32_t ret;

    if (astream_out->isDeviceAvailable(QAL_DEVICE_OUT_BLUETOOTH_A2DP)) {
        ret = qal_get_param(QAL_PARAM_ID_BT_A2DP_ENCODER_LATENCY,
                            (void **)&param_bt_a2dp, &size, nullptr);
        if (!ret && param_bt_a2dp)
            latency += param_bt_a2dp->latency;
    }

    ALOGV("%s: Latency %d", __func__, latency);
    return latency;
}

static int astream_out_get_presentation_position(
                               const struct audio_stream_out *stream,
                               uint64_t *frames, struct timespec *timestamp) {
    std::ignore = stream;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;
    int ret = 0;
    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }
    if (!timestamp) {
       ALOGE("%s: timestamp NULL", __func__);
       return -EINVAL;
    }
    if (astream_out) {
       switch (astream_out->GetQalStreamType(astream_out->flags_)) {
       case QAL_STREAM_COMPRESSED:
          ret = astream_out->GetFrames(frames);
          if (ret != 0) {
             ALOGE("%s: GetTimestamp failed %d", __func__, ret);
             return ret;
          }
          clock_gettime(CLOCK_MONOTONIC, timestamp);
          break;
       default:
          *frames = astream_out->GetFramesWritten(timestamp);
          break;
       }
    } else {
        //ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }
    ALOGV("%s: frames %lld played at %lld ", __func__, ((long long) *frames), timestamp->tv_sec * 1000000LL + timestamp->tv_nsec / 1000);

    return ret;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames) {
    std::ignore = stream;
    std::ignore = dsp_frames;
    ALOGD("%s: enter", __func__);
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;
    int ret = 0;
    uint64_t frames = 0;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }
    if (astream_out) {
        switch (astream_out->GetQalStreamType(astream_out->flags_)) {
        case QAL_STREAM_COMPRESSED:
           ret = astream_out->GetFrames(&frames);
           if (ret != 0) {
              ALOGE("%s: Get DSP Frames failed %d", __func__, ret);
              return ret;
           }
           *dsp_frames = (uint32_t) frames;
           break;
        default:
           break;
        }
    }

    //Temporary fix for Compressed offload SSR
    return -EINVAL;
}

static int astream_out_set_parameters(struct audio_stream *stream,
                                      const char *kvpairs) {
    int ret = 0;
    struct str_parms *parms = (str_parms *)NULL;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;
    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ret = -EINVAL;
        ALOGE("%s: unable to get audio device", __func__);
        goto exit;
    }

    ALOGD("%s: enter: usecase(%d: %s) kvpairs: %s",
          __func__, astream_out->GetUseCase(), use_case_table[astream_out->GetUseCase()], kvpairs);

    ret = astream_out->VoiceSetParameters(adevice, kvpairs);
    if (ret) {
        ALOGE("Voice Stream SetParameters Error (%x)", ret);
        goto exit;
    }

    parms = str_parms_create_str(kvpairs);
    if (!parms) {
       ret = -EINVAL;
       goto exit;
    }


    ret = astream_out->SetParameters(parms);
    if (ret) {
        ALOGE("Stream SetParameters Error (%x)", ret);
        goto exit;
    }
exit:
    if (parms)
        str_parms_destroy(parms);
    return ret;
}

static char* astream_out_get_parameters(const struct audio_stream *stream,
                                        const char *keys) {
    int ret = 0;
    struct str_parms *query = str_parms_create_str(keys);
    char value[256];
    char *str = (char*) nullptr;
    std::shared_ptr<StreamOutPrimary> astream_out;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    struct str_parms *reply = str_parms_create();
    //int index = 0;
    //int table_size = 0;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ret = -EINVAL;
        ALOGE("%s: unable to get audio device", __func__);
        goto exit;
    }

    if (!query || !reply) {
        if (reply)
            str_parms_destroy(reply);
        if (query)
            str_parms_destroy(query);
        ALOGE("out_get_parameters: failed to allocate mem for query or reply");
        return nullptr;
    }
    ALOGD("%s: keys: %s", __func__, keys);

    ret = str_parms_get_str(query, "is_direct_pcm_track", value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';

        if (astream_out->flags_ & AUDIO_OUTPUT_FLAG_DIRECT &&
             !(astream_out->flags_ & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
            ALOGV("in direct_pcm");
            strlcat(value, "true", sizeof(value));
        } else {
            ALOGV("not in direct_pcm");
            strlcat(value, "false", sizeof(value));
        }
        str_parms_add_str(reply, "is_direct_pcm_track", value);
        if (str)
            free(str);
        str = str_parms_to_str(reply);
    }

#if 0
    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        int stream_format = astream_out->GetFormat();
        table_size = sizeof(formats_name_to_enum_table) / sizeof(struct string_to_enum);
        index = astream_out->GetLookupTableIndex(formats_name_to_enum_table,
                                    table_size, stream_format);
        strlcat(value, formats_name_to_enum_table[index].name, sizeof(value));
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value);
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value, sizeof(value));
    if (ret >= 0) {
        int stream_chn_mask = astream_out->GetChannelMask();

        table_size = sizeof(channels_name_to_enum_table) / sizeof(struct string_to_enum);
        index = astream_out->GetLookupTableIndex(channels_name_to_enum_table,
                                    table_size, stream_chn_mask);
        value[0] = '\0';
        strlcat(value, "AUDIO_CHANNEL_OUT_STEREO", sizeof(value));
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value);
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        int stream_sample_rate = astream_out->GetSampleRate();
        int cursor = 0;
        int avail = sizeof(value) - cursor;
        ret = snprintf(value + cursor, avail, "%s%d",
                       cursor > 0 ? "|" : "",
                       stream_sample_rate);
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES,
                          value);
    }

exit:
    /* do we need new hooks inside qal? */
    str = str_parms_to_str(reply);
    return str;
#endif
exit:
    return 0;
}

static int astream_out_set_volume(struct audio_stream_out *stream,
                                  float left, float right) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    if (astream_out) {
        return astream_out->SetVolume(left, right);
    } else {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }
}

static int astream_out_add_audio_effect(
                                const struct audio_stream *stream __unused,
                                effect_handle_t effect __unused) {
    return 0;
}

static int astream_out_remove_audio_effect(
                                const struct audio_stream *stream __unused,
                                effect_handle_t effect __unused) {
    return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void *buffer,
                       size_t bytes) {

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    if (astream_in) {
        return astream_in->Read(buffer, bytes);
    } else {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }

    return 0;
}

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer,
                         size_t bytes) {

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    if (astream_out) {
        return astream_out->Write(buffer, bytes);
    } else {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }

    return 0;
}

static int astream_in_mmap_noirq_start(const struct audio_stream_in *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_in = adevice->InGetStream((audio_stream_t*)stream);
    if (!astream_in) {
        ALOGE("%s: unable to get audio InStream", __func__);
        return -EINVAL;
    }

    return astream_in->Start();
}

static int astream_in_mmap_noirq_stop(const struct audio_stream_in *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_in = adevice->InGetStream((audio_stream_t*)stream);
    if (!astream_in) {
        ALOGE("%s: unable to get audio InStream", __func__);
        return -EINVAL;
    }

    return astream_in->Stop();
}

static int astream_in_create_mmap_buffer(const struct audio_stream_in *stream,
        int32_t min_size_frames, struct audio_mmap_buffer_info *info)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_in = adevice->InGetStream((audio_stream_t*)stream);
    if (!astream_in) {
        ALOGE("%s: unable to get audio InStream", __func__);
        return -EINVAL;
    }

    if (info == NULL || !(min_size_frames > 0 && min_size_frames < INT32_MAX)) {
        ALOGE("%s: info = %p, min_size_frames = %d", __func__, info, min_size_frames);
        return -EINVAL;
    }
    if (astream_in->GetUseCase() != USECASE_AUDIO_RECORD_MMAP) {
         ALOGE("%s: usecase = %d", __func__, astream_in->GetUseCase());
         return -ENOSYS;
    }

    return astream_in->CreateMmapBuffer(min_size_frames, info);
}

static int astream_in_get_mmap_position(const struct audio_stream_in *stream,
        struct audio_mmap_position *position)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (!adevice) {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    astream_in = adevice->InGetStream((audio_stream_t*)stream);
    if (!astream_in) {
        ALOGE("%s: unable to get audio InStream", __func__);
        return -EINVAL;
    }
    if (astream_in->GetUseCase() != USECASE_AUDIO_RECORD_MMAP) {
         ALOGE("%s: usecase = %d", __func__, astream_in->GetUseCase());
         return -ENOSYS;
    }

    return astream_in->GetMmapPosition(position);
}

static int astream_in_set_microphone_direction(
                        const struct audio_stream_in *stream,
                        audio_microphone_direction_t dir) {
    std::ignore = stream;
    std::ignore = dir;
    ALOGV("%s: function not implemented", __func__);
    //No plans to implement audiozoom
    return -1;
}

static int in_set_microphone_field_dimension(
                        const struct audio_stream_in *stream,
                        float zoom) {
    std::ignore = stream;
    std::ignore = zoom;
    ALOGV("%s: function not implemented", __func__);
    //No plans to implement audiozoom
    return -1;
}

static int astream_in_add_audio_effect(
                                const struct audio_stream *stream,
                                effect_handle_t effect)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;
    ALOGD("%s: Enter ", __func__);
    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }
    if (astream_in) {
        return astream_in->addRemoveAudioEffect(stream, effect, true);
    } else {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }
}

static int astream_in_remove_audio_effect(const struct audio_stream *stream,
                                          effect_handle_t effect)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;
    ALOGD("%s: Enter ", __func__);
    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }
    if (astream_in) {
        return astream_in->addRemoveAudioEffect(stream, effect, false);
    } else {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }
}

static int astream_in_get_capture_position(const struct audio_stream_in *stream,
                                           int64_t *frames, int64_t *time) {
    std::ignore = stream;
    std::ignore = frames;
    std::ignore = time;
    //TODO: get timestamp for input streams
    ALOGV("%s: position not implemented currently supported in qal", __func__);
    return 0;
}

static uint32_t astream_in_get_input_frames_lost(
                                struct audio_stream_in *stream __unused) {
    return 0;
}

static void in_update_sink_metadata(
                                struct audio_stream_in *stream,
                                const struct sink_metadata *sink_metadata) {
    std::ignore = stream;
    std::ignore = sink_metadata;

    ALOGV("%s: sink meta data update not  supported in qal", __func__);
}

static int astream_in_get_active_microphones(
                        const struct audio_stream_in *stream,
                        struct audio_microphone_characteristic_t *mic_array,
                        size_t *mic_count) {
    std::ignore = stream;
    std::ignore = mic_array;
    std::ignore = mic_count;
    ALOGV("%s: get active mics not currently supported in qal", __func__);
    return 0;
}

static uint32_t astream_in_get_sample_rate(const struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;
    ALOGE("%s: Inside", __func__);

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return 0;
    }

    if (astream_in)
        return astream_in->GetSampleRate();
    else
        return 0;
}

static uint32_t astream_in_get_channels(const struct audio_stream *stream) {

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return 0;
    }

    if (astream_in) {
        return astream_in->GetChannelMask();
    } else {
        ALOGE("%s: unable to get audio stream", __func__);
        return 0;
    }
}

static audio_format_t astream_in_get_format(const struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice)
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    else
        ALOGE("%s: unable to get audio device", __func__);

    if (astream_in)
        return astream_in->GetFormat();
    else
        return AUDIO_FORMAT_DEFAULT;
}

static int astream_in_standby(struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    ALOGD("%s: enter: stream (%p) usecase(%d: %s)", __func__, astream_in.get(),
          astream_in->GetUseCase(), use_case_table[astream_in->GetUseCase()]);

    if (astream_in) {
        return astream_in->Standby();
    } else {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }
}

static int astream_in_set_parameters(struct audio_stream *stream, const char *kvpairs) {
    int ret = -EINVAL;

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;


    if (!stream || !kvpairs) {
        ret = 0;
        goto error;
    }

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    if (astream_in) {
        return astream_in->SetParameters(kvpairs);
    }

error:
    return ret;
}

static char* astream_in_get_parameters(const struct audio_stream *stream,
                                       const char *keys) {
    std::ignore = stream;
    std::ignore = keys;
    ALOGD("%s: function not implemented", __func__);
    return 0;
}

static int astream_in_set_gain(struct audio_stream_in *stream, float gain) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device", __func__);
        return -EINVAL;
    }

    if (astream_in) {
        return astream_in->SetGain(gain);
    } else {
        ALOGE("%s: unable to get audio stream", __func__);
        return -EINVAL;
    }
}

static size_t astream_in_get_buffer_size(const struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in =
                            adevice->InGetStream((audio_stream_t*)stream);

    if (astream_in)
        return astream_in->GetBufferSize();
    else
        return 0;
}

bool platform_supports_true_32bit() {
    //TODO: Remove the hardcoding.
    return true;
}

int StreamPrimary::getQalDeviceIds(const audio_devices_t halDeviceIds, qal_device_id_t* qualIds) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    return adevice->GetQalDeviceIds(halDeviceIds, qualIds);
}

int StreamPrimary::GetDeviceAddress(struct str_parms *parms, int *card_id,
                                      int *device_num) {
    int ret = -EINVAL;
    char value[64];

    ret = str_parms_get_str(parms, "card", value, sizeof(value));
    if (ret >= 0) {
        *card_id = atoi(value);
        ret = str_parms_get_str(parms, "device", value, sizeof(value));
        if (ret >= 0) {
            *device_num = atoi(value);
        }
    }

    return ret;
}

int StreamPrimary::GetLookupTableIndex(const struct string_to_enum *table,
                                    int table_size, int value) {
    int index = -EINVAL;
    int i = 0;

    for (i = 0; i < table_size; i++) {
        if (value == table[i].value) {
            index = i;
            break;
        }
    }

    return index;
}

qal_stream_type_t StreamInPrimary::GetQalStreamType(
                                        audio_input_flags_t halStreamFlags,
                                        uint32_t sample_rate) {
    qal_stream_type_t qalStreamType = QAL_STREAM_LOW_LATENCY;
    if ((halStreamFlags & AUDIO_INPUT_FLAG_VOIP_TX)!=0) {
         qalStreamType = QAL_STREAM_VOIP_TX;
         return qalStreamType;
    }

    if (sample_rate == LOW_LATENCY_CAPTURE_SAMPLE_RATE &&
            (halStreamFlags & AUDIO_INPUT_FLAG_TIMESTAMP) == 0 &&
            (halStreamFlags & AUDIO_INPUT_FLAG_COMPRESS) == 0 &&
            (halStreamFlags & AUDIO_INPUT_FLAG_FAST) != 0) {
        if (isDeviceAvailable(QAL_DEVICE_IN_PROXY))
            qalStreamType = QAL_STREAM_PROXY;
        else
            qalStreamType = QAL_STREAM_ULTRA_LOW_LATENCY;

        return qalStreamType;
    }
    switch (halStreamFlags) {
        case AUDIO_INPUT_FLAG_FAST:
            qalStreamType = QAL_STREAM_LOW_LATENCY;
            break;
        case AUDIO_INPUT_FLAG_RAW:
        case AUDIO_INPUT_FLAG_DIRECT:
            qalStreamType = QAL_STREAM_RAW;
            break;
        case AUDIO_INPUT_FLAG_VOIP_TX:
            qalStreamType = QAL_STREAM_VOIP_TX;
            break;
        case AUDIO_INPUT_FLAG_MMAP_NOIRQ:
            qalStreamType = QAL_STREAM_ULTRA_LOW_LATENCY;
            break;
        case AUDIO_INPUT_FLAG_NONE:
            qalStreamType = QAL_STREAM_DEEP_BUFFER;
            break;
        default:
            /*
            unsupported from QAL
            AUDIO_INPUT_FLAG_HW_HOTWORD = 0x2,
            AUDIO_INPUT_FLAG_SYNC        = 0x8,
            AUDIO_INPUT_FLAG_HW_AV_SYNC = 0x40,
            */
            ALOGE("%s: flag %#x is not supported from QAL." ,
                      __func__, halStreamFlags);
            break;
    }

    return qalStreamType;
}

qal_stream_type_t StreamOutPrimary::GetQalStreamType(
                                    audio_output_flags_t halStreamFlags) {
    qal_stream_type_t qalStreamType = QAL_STREAM_LOW_LATENCY;
    if ((halStreamFlags & AUDIO_OUTPUT_FLAG_VOIP_RX)!=0) {
        qalStreamType = QAL_STREAM_VOIP_RX;
        return qalStreamType;
    }
    if ((halStreamFlags & AUDIO_OUTPUT_FLAG_RAW) != 0) {
        qalStreamType = QAL_STREAM_ULTRA_LOW_LATENCY;
    } else if ((halStreamFlags & AUDIO_OUTPUT_FLAG_FAST) != 0) {
        qalStreamType = QAL_STREAM_LOW_LATENCY;
    } else if (halStreamFlags ==
                    (AUDIO_OUTPUT_FLAG_FAST|AUDIO_OUTPUT_FLAG_RAW)) {
        qalStreamType = QAL_STREAM_RAW;
    } else if (halStreamFlags == AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
        qalStreamType = QAL_STREAM_DEEP_BUFFER;
    } else if (halStreamFlags ==
                    (AUDIO_OUTPUT_FLAG_DIRECT|AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
        // mmap_no_irq_out: to be confirmed
        qalStreamType = QAL_STREAM_ULTRA_LOW_LATENCY;
    } else if (halStreamFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        qalStreamType = QAL_STREAM_ULTRA_LOW_LATENCY;
    } else if (halStreamFlags & AUDIO_OUTPUT_FLAG_RAW) {
        qalStreamType = QAL_STREAM_ULTRA_LOW_LATENCY;
    } else if (halStreamFlags == (AUDIO_OUTPUT_FLAG_DIRECT|
                                      AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|
                                  AUDIO_OUTPUT_FLAG_NON_BLOCKING)) {
        // hifi: to be confirmed
        qalStreamType = QAL_STREAM_COMPRESSED;
    } else if (halStreamFlags == AUDIO_OUTPUT_FLAG_DIRECT) {
        qalStreamType = QAL_STREAM_PCM_OFFLOAD;
    } else if (halStreamFlags == (AUDIO_OUTPUT_FLAG_DIRECT|
                                      AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|
                                  AUDIO_OUTPUT_FLAG_NON_BLOCKING)) {
        qalStreamType = QAL_STREAM_COMPRESSED;
    } else if (halStreamFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        // dsd_compress_passthrough
        qalStreamType = QAL_STREAM_COMPRESSED;
    } else if (halStreamFlags == (AUDIO_OUTPUT_FLAG_DIRECT|
                                      AUDIO_OUTPUT_FLAG_VOIP_RX)) {
        // voice rx
        qalStreamType = QAL_STREAM_VOIP_RX;
    } else if (halStreamFlags == AUDIO_OUTPUT_FLAG_VOIP_RX) {
        qalStreamType = QAL_STREAM_VOIP_RX;
    } else if (halStreamFlags == AUDIO_OUTPUT_FLAG_INCALL_MUSIC) {
        // incall_music_uplink
        qalStreamType = QAL_STREAM_VOICE_CALL_MUSIC;
    } else {
        qalStreamType = QAL_STREAM_GENERIC;
    }
    return qalStreamType;
}

int StreamOutPrimary::FillHalFnPtrs() {
    int ret = 0;

    stream_.get()->common.get_sample_rate = astream_out_get_sample_rate;
    stream_.get()->common.set_sample_rate = astream_set_sample_rate;
    stream_.get()->common.get_buffer_size = astream_out_get_buffer_size;
    stream_.get()->common.get_channels = astream_out_get_channels;
    stream_.get()->common.get_format = astream_out_get_format;
    stream_.get()->common.set_format = astream_set_format;
    stream_.get()->common.standby = astream_out_standby;
    stream_.get()->common.dump = astream_dump;
    stream_.get()->common.set_parameters = astream_out_set_parameters;
    stream_.get()->common.get_parameters = astream_out_get_parameters;
    stream_.get()->common.add_audio_effect = astream_out_add_audio_effect;
    stream_.get()->common.remove_audio_effect =
                                            astream_out_remove_audio_effect;
    stream_.get()->get_latency = astream_get_latency;
    stream_.get()->set_volume = astream_out_set_volume;
    stream_.get()->write = out_write;
    stream_.get()->get_render_position = out_get_render_position;
    stream_.get()->get_next_write_timestamp =
                                            astream_out_get_next_write_timestamp;
    stream_.get()->get_presentation_position =
                                            astream_out_get_presentation_position;
    stream_.get()->update_source_metadata = NULL;
    stream_.get()->pause = astream_pause;
    stream_.get()->resume = astream_resume;
    stream_.get()->drain = astream_drain;
    stream_.get()->flush = astream_flush;
    stream_.get()->set_callback = astream_set_callback;
    return ret;
}

int StreamOutPrimary::GetMmapPosition(struct audio_mmap_position *position)
{
    struct qal_mmap_position qal_mmap_pos;
    int32_t ret = 0;

    if (qal_stream_handle_ == nullptr) {
        ALOGE("%s: qal handle is null\n", __func__);
        return -EINVAL;
    }

    ret = qal_stream_get_mmap_position(qal_stream_handle_, &qal_mmap_pos);
    if (ret) {
        ALOGE("%s: failed to get mmap position %d\n", __func__, ret);
        return ret;
    }
    position->position_frames = qal_mmap_pos.position_frames;
    position->time_nanoseconds = qal_mmap_pos.time_nanoseconds;

#if 0
    /** Check if persist vendor property is available */
    const int32_t kDefaultOffsetMicros = 0;
    int32_t mmap_time_offset_micros = property_get_int32(
            "persist.vendor.audio.out_mmap_delay_micros", kDefaultOffsetMicros);

    position->time_nanoseconds += mmap_time_offset_micros * (int64_t)1000;
#endif

    return 0;
}

bool StreamOutPrimary::isDeviceAvailable(qal_device_id_t deviceId)
{
    for (int i = 0; i < mNoOfOutDevices; i++) {
        if (mQalOutDevice[i].id == deviceId)
            return true;
    }

    return false;
}

int StreamOutPrimary::CreateMmapBuffer(int32_t min_size_frames,
        struct audio_mmap_buffer_info *info)
{
    int ret;
    struct qal_mmap_buffer qalMmapBuf;

    if (qal_stream_handle_) {
        ALOGE("%s: qal handle already created\n", __func__);
        return -EINVAL;
    }

    ret = Open();
    if (ret) {
        ALOGE("%s: failed to open stream.", __func__);
        return ret;
    }
    ret = qal_stream_create_mmap_buffer(qal_stream_handle_,
            min_size_frames, &qalMmapBuf);
    if (ret) {
        ALOGE("%s: failed to create mmap buffer: %d", __func__, ret);
        Standby();
        return ret;
    }
    info->shared_memory_address = qalMmapBuf.buffer;
    info->shared_memory_fd = qalMmapBuf.fd;
    info->buffer_size_frames = qalMmapBuf.buffer_size_frames;
    info->burst_size_frames = qalMmapBuf.burst_size_frames;
    info->flags = (audio_mmap_buffer_flag) AUDIO_MMAP_APPLICATION_SHAREABLE;

    return ret;
}

int StreamOutPrimary::Stop() {
    int ret = -ENOSYS;

    if (usecase_ == USECASE_AUDIO_PLAYBACK_MMAP &&
            qal_stream_handle_ && stream_started_) {

        ret = qal_stream_stop(qal_stream_handle_);
        if (ret == 0) {
            stream_started_ = false;
            stream_paused_ = false;
        }
    }
    return ret;
}

int StreamOutPrimary::Start() {
    int ret = -ENOSYS;

    if (usecase_ == USECASE_AUDIO_PLAYBACK_MMAP &&
            qal_stream_handle_ && !stream_started_) {

        ret = qal_stream_start(qal_stream_handle_);
        if (ret == 0)
            stream_started_ = true;
    }
    return ret;
}

int StreamOutPrimary::Pause() {
    int ret = 0;

    if (qal_stream_handle_) {
        ret = qal_stream_pause(qal_stream_handle_);
    }
    if (ret)
        return -EINVAL;
    else
    {
        stream_paused_ = true;
        return ret;
    }
}

int StreamOutPrimary::Resume() {
    int ret = 0;

    if (qal_stream_handle_) {
        ret = qal_stream_resume(qal_stream_handle_);
    }
    if (ret)
        return -EINVAL;
    else {
        stream_paused_ = false;
        return ret;
    }
}

int StreamOutPrimary::Flush() {
    int ret = 0;
    ALOGD("%s: Enter", __func__);
    if (qal_stream_handle_) {
        if(stream_paused_ == true)
        {
            ret = qal_stream_flush(qal_stream_handle_);
            if (!ret) {
                ret = qal_stream_resume(qal_stream_handle_);
                if (!ret)
                    stream_paused_ = false;
            }
        } else {
            ALOGI("%s: called in invalid state (stream not paused)", __func__ );
        }
        total_bytes_written_ = 0;
    }

    if (ret)
        return -EINVAL;
    else {
        return ret;
    }
}

int StreamOutPrimary::Drain(audio_drain_type_t type) {
    int ret = 0;
    qal_drain_type_t qalDrainType;

    switch (type) {
      case AUDIO_DRAIN_ALL:
           qalDrainType = QAL_DRAIN;
           break;
      case AUDIO_DRAIN_EARLY_NOTIFY:
           qalDrainType = QAL_DRAIN_PARTIAL;
           break;
    default:
           ALOGE("%s: Invalid drain type:%d", __func__, type);
           return -EINVAL;
    }

    if (qal_stream_handle_)
        ret = qal_stream_drain(qal_stream_handle_, qalDrainType);

    if (ret) {
        ALOGE("%s: Invalid drain type:%d", __func__, type);
    }

    return ret;
}

int StreamOutPrimary::Standby() {
    int ret = 0;

    if (qal_stream_handle_) {
        ret = qal_stream_stop(qal_stream_handle_);
        if (ret) {
            ALOGE("%s: failed to stop stream.", __func__);
            return -EINVAL;
        }
    }

    stream_started_ = false;
    stream_paused_ = false;
    if (CheckOffloadEffectsType(streamAttributes_.type)) {
        ret = StopOffloadEffects(handle_, qal_stream_handle_);
    }

    if (qal_stream_handle_) {
        ret = qal_stream_close(qal_stream_handle_);
        qal_stream_handle_ = NULL;
    }

    if (ret)
        return -EINVAL;
    else
        return ret;
}

int StreamOutPrimary::SetParameters(struct str_parms *parms) {
    char value[64];
    int ret = 0, val = 0, noQalDevices = 0;
    qal_device_id_t * deviceId;
    struct qal_device* deviceIdConfigs;
    int err =  -EINVAL;
    int controller = -1, stream = -1;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    ALOGD("%s: enter ", __func__);

    if (!mInitialized)
        goto error;

#if 0
    if (!qal_stream_handle_) {
        ALOGD("%s: No stream handle, going to call open", __func__);
        ret = Open();
        if (ret) {
            ALOGE("%s: failed to open stream.", __func__);
            return -EINVAL;
        }
    }
#endif

    err = AudioExtn::get_controller_stream_from_params(parms, &controller, &stream);
    if ( err >= 0) {
        adevice->dp_controller = controller;
        adevice->dp_stream = stream;
        ALOGE("%s: plugin device cont %d stream %d",__func__, controller, stream);
    }


    err = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (err >= 0) {
        val = atoi(value);
        ALOGV("%s: Found routing for output stream with value %x", __func__, val);
        ALOGD("%s: mAndroidOutDevicese %d, mNoOfOutDevices %d", __func__, mAndroidOutDevices, mNoOfOutDevices);
        /* If its the same device as what was already routed to, dont bother */
        if ((mAndroidOutDevices != val) && (val != 0)) {
            //re-allocate mQalOutDevice and mQalOutDeviceIds
            if (popcount(val) != mNoOfOutDevices) {
                deviceId = (qal_device_id_t*) realloc(mQalOutDeviceIds, popcount(val)* sizeof(qal_device_id_t));
                deviceIdConfigs = (struct qal_device*) realloc(mQalOutDevice, popcount(val) * sizeof(struct qal_device));
                if (!deviceId || !deviceIdConfigs) {
                    ret = -ENOMEM;
                    goto error;
                }
                mQalOutDeviceIds = deviceId;
                mQalOutDevice = deviceIdConfigs;
            }
            noQalDevices = getQalDeviceIds(val, mQalOutDeviceIds);

            if (noQalDevices != popcount(val)) {
                ret = -EINVAL;
                goto error;
            }
            ALOGD("%s: noQalDevices %d", __func__, noQalDevices);
            mNoOfOutDevices = noQalDevices;
            for (int i = 0; i < mNoOfOutDevices; i++) {
                mQalOutDevice[i].id = mQalOutDeviceIds[i];
                mQalOutDevice[i].config.sample_rate = mQalOutDevice[0].config.sample_rate;
                mQalOutDevice[i].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
                mQalOutDevice[i].config.ch_info = {0, {0}}; //is there a reason to have two different ch_info for device/stream?
                mQalOutDevice[i].config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
                if ((mQalOutDeviceIds[i] == QAL_DEVICE_OUT_USB_DEVICE) ||
                   (mQalOutDeviceIds[i] == QAL_DEVICE_OUT_USB_HEADSET)) {
                    mQalOutDevice[i].address.card_id = adevice->usb_card_id_;
                    mQalOutDevice[i].address.device_num = adevice->usb_dev_num_;
                }
            }
            mAndroidOutDevices = val;
            ret = qal_stream_set_device(qal_stream_handle_, mNoOfOutDevices, mQalOutDevice);
            if (!ret) {
                audio_extn_gef_notify_device_config(mAndroidOutDevices, config_.channel_mask, config_.sample_rate);
            } else {
                ALOGE("%s: failed to set device. Error %d", __func__ ,ret);
            }
        }
    }
    //TBD: check if its offload and check call the following
    ret = AudioExtn::audio_extn_parse_compress_metadata(&config_, &qalSndDec, parms, &msample_rate, &mchannels);
    if (ret) {
        ALOGE("parse_compress_metadata Error (%x)", ret);
    }
error:
    ALOGE("%s: exit %d", __func__, ret);
    return ret;
}

int StreamOutPrimary::VoiceSetParameters(std::shared_ptr<AudioDevice> adevice, const char *kvpairs) {
    int ret = 0;

    ALOGD("%s Enter", __func__);
    if (adevice->voice_)
        ret = adevice->voice_->VoiceOutSetParameters(kvpairs);

    return ret;
}

int StreamOutPrimary::SetVolume(float left , float right) {
    int ret = 0;
    ALOGD("%s: left %f, right %f", __func__, left, right);

    /* free previously cached volume if any */
    if (volume_) {
        free(volume_);
        volume_ = NULL;
    }

    if (left == right) {
        volume_ = (struct qal_volume_data *)malloc(sizeof(struct qal_volume_data)
                    +sizeof(struct qal_channel_vol_kv));
        volume_->no_of_volpair = 1;
        volume_->volume_pair[0].channel_mask = 0x03;
        volume_->volume_pair[0].vol = left;
    } else {
        volume_ = (struct qal_volume_data *)malloc(sizeof(struct qal_volume_data)
                    +sizeof(struct qal_channel_vol_kv) * 2);
        volume_->no_of_volpair = 2;
        volume_->volume_pair[0].channel_mask = 0x01;
        volume_->volume_pair[0].vol = left;
        volume_->volume_pair[1].channel_mask = 0x10;
        volume_->volume_pair[1].vol = right;
    }

    /* if stream is not opened already cache the volume and set on open */
    if (qal_stream_handle_) {
        ret = qal_stream_set_volume(qal_stream_handle_, volume_);
        if (ret) {
            ALOGE("Qal Stream volume Error (%x)", ret);
        }
    }
    return ret;
}

/* Delay in Us */
/* Delay in Us, only to be used for PCM formats */
int64_t StreamOutPrimary::GetRenderLatency(audio_output_flags_t halStreamFlags)
{
    struct qal_stream_attributes streamAttributes_;
    streamAttributes_.type = StreamOutPrimary::GetQalStreamType(halStreamFlags);
    ALOGV("%s:%d type %d", __func__, __LINE__, streamAttributes_.type);
    switch (streamAttributes_.type) {
         case QAL_STREAM_DEEP_BUFFER:
             return DEEP_BUFFER_PLATFORM_DELAY;
         case QAL_STREAM_LOW_LATENCY:
             return LOW_LATENCY_PLATFORM_DELAY;
         case QAL_STREAM_COMPRESSED:
         case QAL_STREAM_PCM_OFFLOAD:
              return PCM_OFFLOAD_PLATFORM_DELAY;
         case QAL_STREAM_ULTRA_LOW_LATENCY:
              return ULL_PLATFORM_DELAY;
         //TODO: Add more usecases/type as in current hal, once they are available in qal
         default:
             return 0;
     }
}

uint64_t StreamOutPrimary::GetFramesWritten(struct timespec *timestamp)
{
    uint64_t signed_frames = 0;
    uint64_t written_frames = 0;
    uint64_t kernel_frames = 0;
    uint64_t dsp_frames = 0;
    uint64_t bt_extra_frames = 0;
    qal_param_bta2dp_t *param_bt_a2dp = NULL;
    size_t size = 0, kernel_buffer_size = 0;
    int32_t ret;

    /* This adjustment accounts for buffering after app processor
     * It is based on estimated DSP latency per use case, rather than exact.
     */
    dsp_frames = StreamOutPrimary::GetRenderLatency(flags_) *
        (streamAttributes_.out_media_config.sample_rate) / 1000000LL;

    if (!timestamp) {
       ALOGE("%s: timestamp NULL", __func__);
       return 0;
    }
    written_frames = total_bytes_written_ / audio_bytes_per_frame(
        audio_channel_count_from_out_mask(config_.channel_mask),
        config_.format);

    /* not querying actual state of buffering in kernel as it would involve an ioctl call
     * which then needs protection, this causes delay in TS query for pcm_offload usecase
     * hence only estimate.
     */
    kernel_buffer_size = fragment_size_ * fragments_;
    kernel_frames = kernel_buffer_size /
        audio_bytes_per_frame(
        audio_channel_count_from_out_mask(config_.channel_mask),
        config_.format);


    // kernel_frames = (kernel_buffer_size - avail) / (bitwidth * channel count);
    if (written_frames >= (kernel_frames + dsp_frames))
        signed_frames = written_frames - (kernel_frames + dsp_frames);

    // Adjustment accounts for A2dp encoder latency with non offload usecases
    // Note: Encoder latency is returned in ms, while platform_render_latency in us.
    if (isDeviceAvailable(QAL_DEVICE_OUT_BLUETOOTH_A2DP)) {
        ret = qal_get_param(QAL_PARAM_ID_BT_A2DP_ENCODER_LATENCY,
                            (void **)&param_bt_a2dp, &size, nullptr);
        if (!ret && param_bt_a2dp) {
            bt_extra_frames = param_bt_a2dp->latency *
                (streamAttributes_.out_media_config.sample_rate) / 1000;
            if (signed_frames >= bt_extra_frames)
                signed_frames -= bt_extra_frames;

        }
    }

    if (signed_frames <= 0) {
       clock_gettime(CLOCK_MONOTONIC, timestamp);
       signed_frames = 0;
    } else {
       *timestamp = writeAt;
    }

    ALOGV("%s signed frames %lld written frames %lld kernel frames %lld dsp frames %lld, bt extra frames %lld",
            __func__, (long long)signed_frames, (long long)written_frames, (long long)kernel_frames,
            (long long)dsp_frames, (long long)bt_extra_frames);

    return signed_frames;
}

int StreamOutPrimary::get_compressed_buffer_size()
{
    int fragment_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    ALOGD("%s:%d config_ %x", __func__, __LINE__, config_.format);
    if(config_.format ==  AUDIO_FORMAT_FLAC ) {
        fragment_size = FLAC_COMPRESS_OFFLOAD_FRAGMENT_SIZE;
        ALOGD("%s:%d aud_fmt_id: 0x%x  FLAC buffer size:%d", __func__, __LINE__,
            streamAttributes_.out_media_config.aud_fmt_id,
            fragment_size);
    } else {
        fragment_size =  COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    }
    return fragment_size;
}

int StreamOutPrimary::get_pcm_buffer_size()
{
    uint8_t channels = audio_channel_count_from_out_mask(config_.channel_mask);
    uint8_t bytes_per_sample = audio_bytes_per_sample(config_.format);
    uint32_t fragment_size = 0;

    ALOGD("%s:%d config_ format:%x, SR %d ch_mask 0x%x",
            __func__, __LINE__, config_.format, config_.sample_rate,
            config_.channel_mask);
    fragment_size = PCM_OFFLOAD_OUTPUT_PERIOD_DURATION *
        config_.sample_rate * bytes_per_sample * channels;
    fragment_size /= 1000;

    if (fragment_size < MIN_PCM_FRAGMENT_SIZE)
        fragment_size = MIN_PCM_FRAGMENT_SIZE;
    else if (fragment_size > MAX_PCM_FRAGMENT_SIZE)
        fragment_size = MAX_PCM_FRAGMENT_SIZE;

    fragment_size = ALIGN(fragment_size, (bytes_per_sample * channels * 32));

    ALOGD("%s: fragment size: %d", __func__, fragment_size);
    return fragment_size;
}

static int voip_get_buffer_size(uint32_t sample_rate)
{
    if (sample_rate == 48000)
        return COMPRESS_VOIP_IO_BUF_SIZE_FB;
    else if (sample_rate == 32000)
        return COMPRESS_VOIP_IO_BUF_SIZE_SWB;
    else if (sample_rate == 16000)
        return COMPRESS_VOIP_IO_BUF_SIZE_WB;
    else
        return COMPRESS_VOIP_IO_BUF_SIZE_NB;

}

uint32_t StreamOutPrimary::GetBufferSize() {
    struct qal_stream_attributes streamAttributes_;

    streamAttributes_.type = StreamOutPrimary::GetQalStreamType(flags_);
    ALOGD("%s:%d type %d", __func__, __LINE__, streamAttributes_.type);
    if (streamAttributes_.type == QAL_STREAM_VOIP_RX) {
        return voip_get_buffer_size(config_.sample_rate);
    } else if (streamAttributes_.type == QAL_STREAM_COMPRESSED) {
        return get_compressed_buffer_size();
    } else if (streamAttributes_.type == QAL_STREAM_PCM_OFFLOAD
              || streamAttributes_.type == QAL_STREAM_DEEP_BUFFER) {
        return get_pcm_buffer_size();
    } else if (streamAttributes_.type == QAL_STREAM_LOW_LATENCY) {
        return LOW_LATENCY_PLAYBACK_PERIOD_SIZE *
            audio_bytes_per_frame(
                    audio_channel_count_from_out_mask(config_.channel_mask),
                    config_.format);
    } else if (streamAttributes_.type == QAL_STREAM_ULTRA_LOW_LATENCY) {
        return ULL_PERIOD_SIZE * ULL_PERIOD_MULTIPLIER *
            audio_bytes_per_frame(
                    audio_channel_count_from_out_mask(config_.channel_mask),
                    config_.format);
    } else {
       return BUF_SIZE_PLAYBACK * NO_OF_BUF;
    }
}

/*Translates PCM formats to AOSP formats*/
audio_format_t StreamOutPrimary::AlsatoHalFormat(uint32_t pcm_format) {
    audio_format_t format = AUDIO_FORMAT_INVALID;

    switch(pcm_format) {
    case PCM_FORMAT_S16_LE:
        format = AUDIO_FORMAT_PCM_16_BIT;
        break;
    case PCM_FORMAT_S24_3LE:
        format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
        break;
    case PCM_FORMAT_S24_LE:
        format = AUDIO_FORMAT_PCM_8_24_BIT;
        break;
    case PCM_FORMAT_S32_LE:
        format = AUDIO_FORMAT_PCM_32_BIT;
        break;
    default:
        ALOGW("Incorrect PCM format");
        format = AUDIO_FORMAT_INVALID;
    }
    return format;
}

/*Translates hal format (AOSP) to alsa formats*/
uint32_t StreamOutPrimary::HaltoAlsaFormat(audio_format_t hal_format) {
    uint32_t pcm_format;

    switch (hal_format) {
    case AUDIO_FORMAT_PCM_32_BIT:
    case AUDIO_FORMAT_PCM_FLOAT: {
        if (platform_supports_true_32bit())
            pcm_format = PCM_FORMAT_S32_LE;
        else
            pcm_format = PCM_FORMAT_S24_3LE;
        }
        break;
    case AUDIO_FORMAT_PCM_8_24_BIT:
        pcm_format = PCM_FORMAT_S24_3LE;
        break;
    case AUDIO_FORMAT_PCM_8_BIT:
        pcm_format = PCM_FORMAT_S8;
        break;
    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
        pcm_format = PCM_FORMAT_S24_3LE;
        break;
    default:
    case AUDIO_FORMAT_PCM_16_BIT:
        pcm_format = PCM_FORMAT_S16_LE;
        break;
    }
    return pcm_format;
}

int StreamOutPrimary::Open() {
    int ret = -EINVAL;
    uint8_t channels = 0;
    struct qal_channel_info ch_info = {0, {0}};
    uint32_t inBufSize = 0;
    uint32_t outBufSize = 0;
    uint32_t inBufCount = NO_OF_BUF;
    uint32_t outBufCount = NO_OF_BUF;
    uint32_t pcmFormat;

    if (!mInitialized) {
        ALOGE("%s: Not initialized, returning error", __func__);
        goto error_open;
    }
    ALOGD("%s: no_of_devices %d, android Device id %x ", __func__, mNoOfOutDevices, mAndroidOutDevices);
    //need to convert channel mask to qal channel mask
    // Stream channel mask
    channels = audio_channel_count_from_out_mask(config_.channel_mask);

    ch_info.channels = channels;
    ch_info.ch_map[0] = QAL_CHMAP_CHANNEL_FL;
    if (ch_info.channels > 1)
        ch_info.ch_map[1] = QAL_CHMAP_CHANNEL_FR;

    streamAttributes_.type = StreamOutPrimary::GetQalStreamType(flags_);
    streamAttributes_.flags = (qal_stream_flags_t)0;
    streamAttributes_.direction = QAL_AUDIO_OUTPUT;
    streamAttributes_.out_media_config.sample_rate = config_.sample_rate;
    streamAttributes_.out_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    streamAttributes_.out_media_config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM;
    streamAttributes_.out_media_config.ch_info = ch_info;

    if (streamAttributes_.type == QAL_STREAM_COMPRESSED) {
        streamAttributes_.flags = (qal_stream_flags_t)(QAL_STREAM_FLAG_NON_BLOCKING);
        if (config_.offload_info.format == 0)
            config_.offload_info.format = config_.format;
        if (config_.offload_info.sample_rate == 0)
            config_.offload_info.sample_rate = config_.sample_rate;
        streamAttributes_.out_media_config.sample_rate = config_.offload_info.sample_rate;
        if (msample_rate)
            streamAttributes_.out_media_config.sample_rate = msample_rate;
        if (mchannels)
            streamAttributes_.out_media_config.ch_info.channels = mchannels;
        streamAttributes_.out_media_config.aud_fmt_id = getFormatId.at(config_.format & AUDIO_FORMAT_MAIN_MASK);
    } else if (streamAttributes_.type == QAL_STREAM_PCM_OFFLOAD ||
               streamAttributes_.type == QAL_STREAM_DEEP_BUFFER) {
        halInputFormat = config_.format;
        pcmFormat = HaltoAlsaFormat(halInputFormat);
        halOutputFormat = AlsatoHalFormat(pcmFormat);
        ALOGD("halInputFormat %d halOutputFormat %d", halInputFormat, halOutputFormat);
        streamAttributes_.out_media_config.bit_width = format_to_bitwidth_table[halOutputFormat];
        if (streamAttributes_.out_media_config.bit_width == 0)
            streamAttributes_.out_media_config.bit_width = 16;
    } else if ((streamAttributes_.type == QAL_STREAM_ULTRA_LOW_LATENCY) &&
            (usecase_ == USECASE_AUDIO_PLAYBACK_MMAP)) {
        streamAttributes_.flags = (qal_stream_flags_t)(QAL_STREAM_FLAG_MMAP_NO_IRQ);
    } else if ((streamAttributes_.type == QAL_STREAM_ULTRA_LOW_LATENCY) &&
            (usecase_ == USECASE_AUDIO_PLAYBACK_ULL)) {
        streamAttributes_.flags = (qal_stream_flags_t)(QAL_STREAM_FLAG_MMAP);
    }

    ALOGD("%s:(%x:ret)%d", __func__, ret, __LINE__);
    ALOGD("channels %d samplerate %d format id %d, stream type %d  stream bitwidth %d",
           streamAttributes_.out_media_config.ch_info.channels, streamAttributes_.out_media_config.sample_rate,
           streamAttributes_.out_media_config.aud_fmt_id, streamAttributes_.type,
           streamAttributes_.out_media_config.bit_width);
    ALOGD("msample_rate %d mchannels %d", msample_rate, mchannels);
    ALOGD("mNoOfOutDevices %d", mNoOfOutDevices);
    ret = qal_stream_open (&streamAttributes_,
                          mNoOfOutDevices,
                          mQalOutDevice,
                          0,
                          NULL,
                          &qal_callback,
                          (void *)this,
                          &qal_stream_handle_);

    ALOGD("%s:(%x:ret)%d",__func__,ret, __LINE__);
    if (ret) {
        ALOGE("Qal Stream Open Error (%x)", ret);
        ret = -EINVAL;
        goto error_open;
    }
    if (streamAttributes_.type == QAL_STREAM_COMPRESSED) {
        qal_param_payload *param_payload = nullptr;
        param_payload = (qal_param_payload *) calloc (1,
                                              sizeof(qal_param_payload) +
                                              sizeof(qal_snd_dec_t));
        if (!param_payload) {
            ALOGE("%s:%d calloc failed for size %zu", __func__, __LINE__,
                   sizeof(qal_param_payload) + sizeof(qal_snd_dec_t));
        } else {
            param_payload->payload_size = sizeof(qal_snd_dec_t);
            memcpy(param_payload->payload, &qalSndDec, param_payload->payload_size);

            ret = qal_stream_set_param(qal_stream_handle_,
                                       QAL_PARAM_ID_CODEC_CONFIGURATION,
                                       param_payload);
            if (ret)
               ALOGE("Qal Set Param Error (%x)", ret);
        }
    }

    if (usecase_ == USECASE_AUDIO_PLAYBACK_MMAP) {
        outBufSize = MMAP_PERIOD_SIZE * audio_bytes_per_frame(
                    audio_channel_count_from_out_mask(config_.channel_mask),
                    config_.format);
        outBufCount = MMAP_PERIOD_COUNT_DEFAULT;
    } else if (usecase_ == USECASE_AUDIO_PLAYBACK_ULL) {
        outBufSize = ULL_PERIOD_SIZE * audio_bytes_per_frame(
                    audio_channel_count_from_out_mask(config_.channel_mask),
                    config_.format);
        outBufCount = ULL_PERIOD_COUNT_DEFAULT;
    } else
        outBufSize = StreamOutPrimary::GetBufferSize();

    if (usecase_ == USECASE_AUDIO_PLAYBACK_LOW_LATENCY)
        outBufCount = LOW_LATENCY_PLAYBACK_PERIOD_COUNT;
    else if (usecase_ == USECASE_AUDIO_PLAYBACK_OFFLOAD2)
         outBufCount = PCM_OFFLOAD_PLAYBACK_PERIOD_COUNT;
    else if (usecase_ == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER)
         outBufCount = DEEP_BUFFER_PLAYBACK_PERIOD_COUNT;

    if (halInputFormat != halOutputFormat) {
        convertBufSize =  PCM_OFFLOAD_OUTPUT_PERIOD_DURATION *
                         config_.sample_rate * audio_bytes_per_frame(
                         audio_channel_count_from_out_mask(config_.channel_mask),
                         halOutputFormat);
        convertBufSize /= 1000;
        convertBuffer = realloc(convertBuffer, convertBufSize);
        if (!convertBuffer) {
            ret = -ENOMEM;
            ALOGE("convert Buffer allocation failed. ret %d", ret);
            goto error_open;
        }
        outBufSize = convertBufSize;
        ALOGD("convert buffer allocated for size %d", convertBufSize);
    }

    fragment_size_ = outBufSize;
    fragments_ = outBufCount;

    ALOGD("%s:fragment_size_ %d fragments_ %d",__func__, fragment_size_, fragments_);

    ret = qal_stream_set_buffer_size(qal_stream_handle_, (size_t*)&inBufSize,
            inBufCount, (size_t*)&outBufSize, outBufCount);
    if (ret) {
        ALOGE("Qal Stream set buffer size Error  (%x)", ret);
    }

error_open:
    return ret;
}


int StreamOutPrimary::GetFrames(uint64_t *frames) {
    int ret = 0;
    if (!qal_stream_handle_) {
        ALOGE("%s: qal_stream_handle_ NULL", __func__);
        *frames = 0;
        return 0;
    }
    qal_session_time tstamp;
    uint64_t timestamp = 0;
    ret = qal_get_timestamp(qal_stream_handle_, &tstamp);
    if (ret != 0) {
       ALOGE("%s: qal_get_timestamp failed %d", __func__, ret);
       goto exit;
    }
    timestamp = (uint64_t)tstamp.session_time.value_msw;
    timestamp = timestamp  << 32 | tstamp.session_time.value_lsw;
    ALOGI("%s: session msw %u", __func__, tstamp.session_time.value_msw);
    ALOGI("%s: session lsw %u", __func__, tstamp.session_time.value_lsw);
    ALOGI("%s: session timespec %lld", __func__, ((long long) timestamp));
    timestamp *= (streamAttributes_.out_media_config.sample_rate);
    ALOGI("%s: timestamp %lld", __func__, ((long long) timestamp));
    *frames = timestamp/1000000;
exit:
    return ret;
}

int StreamOutPrimary::GetOutputUseCase(audio_output_flags_t halStreamFlags)
{
    // TODO: just covered current supported usecases in QAL
    // need to update other usecases in future
    int usecase = USECASE_AUDIO_PLAYBACK_LOW_LATENCY;
    if (halStreamFlags & AUDIO_OUTPUT_FLAG_VOIP_RX)
        usecase = USECASE_AUDIO_PLAYBACK_VOIP;
    else if ((halStreamFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) ||
             (halStreamFlags == AUDIO_OUTPUT_FLAG_DIRECT)) {
        if (halStreamFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)
            usecase = USECASE_AUDIO_PLAYBACK_OFFLOAD;
        else
            usecase = USECASE_AUDIO_PLAYBACK_OFFLOAD2;
    } else if (halStreamFlags & AUDIO_OUTPUT_FLAG_RAW)
        usecase = USECASE_AUDIO_PLAYBACK_ULL;
    else if (halStreamFlags & AUDIO_OUTPUT_FLAG_FAST)
        usecase = USECASE_AUDIO_PLAYBACK_LOW_LATENCY;
    else if (halStreamFlags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER)
        usecase = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
    else if (halStreamFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)
        usecase = USECASE_AUDIO_PLAYBACK_MMAP;

    return usecase;
}

ssize_t StreamOutPrimary::Write(const void *buffer, size_t bytes) {
    int ret = 0;
    struct qal_buffer qalBuffer;
    int local_bytes_written = 0;
    uint32_t frames;

    qalBuffer.buffer = (void*)buffer;
    qalBuffer.size = bytes;
    qalBuffer.offset = 0;

    ALOGV("%s: handle_ %x Bytes:(%zu)", __func__, handle_, bytes);
    if (!qal_stream_handle_) {
        ret = Open();
        if (ret) {
            ALOGE("%s: failed to open stream.", __func__);
            return -EINVAL;
        }
    }

    if (!stream_started_) {
        /* set cached volume if any, dont return failure back up */
        if (volume_) {
            ret = qal_stream_set_volume(qal_stream_handle_, volume_);
            if (ret) {
                ALOGE("Qal Stream volume Error (%x)", ret);
            }
        }

        ret = qal_stream_start(qal_stream_handle_);
        if (ret) {
            ALOGE("%s:failed to start stream. ret=%d", __func__, ret);
            qal_stream_close(qal_stream_handle_);
            qal_stream_handle_ = NULL;
            return -EINVAL;
        }

        stream_started_ = true;

        if (CheckOffloadEffectsType(streamAttributes_.type)) {
            ret = StartOffloadEffects(handle_, qal_stream_handle_);
        }

        if (CheckOffloadEffectsType(streamAttributes_.type)) {
            ret = StartOffloadVisualizer(handle_, qal_stream_handle_);
        }

    }

    if (halInputFormat != halOutputFormat && convertBuffer != NULL) {
        frames = bytes / (format_to_bitwidth_table[halInputFormat]/8);
        memcpy_by_audio_format(convertBuffer, halOutputFormat, buffer, halInputFormat,
                               frames);
        qalBuffer.buffer = convertBuffer;
        qalBuffer.size = frames * (format_to_bitwidth_table[halOutputFormat]/8);
        local_bytes_written = qal_stream_write(qal_stream_handle_, &qalBuffer);
        local_bytes_written = (local_bytes_written * (format_to_bitwidth_table[halInputFormat]/8)) /
                               (format_to_bitwidth_table[halOutputFormat]/8);
    } else {
        local_bytes_written = qal_stream_write(qal_stream_handle_, &qalBuffer);
    }
    total_bytes_written_ += local_bytes_written;
    clock_gettime(CLOCK_MONOTONIC, &writeAt);
    return local_bytes_written;
}

bool StreamOutPrimary::CheckOffloadEffectsType(qal_stream_type_t qal_stream_type) {
    if (qal_stream_type == QAL_STREAM_COMPRESSED  ||
        qal_stream_type == QAL_STREAM_PCM_OFFLOAD) {
        return true;
    }

    return false;
}

int StreamOutPrimary::StartOffloadEffects(
                                    audio_io_handle_t ioHandle,
                                    qal_stream_handle_t* qal_stream_handle) {
    int ret  = 0;
    if (fnp_offload_effect_start_output_) {
        ret = fnp_offload_effect_start_output_(ioHandle, qal_stream_handle);
        if (ret) {
            ALOGE("%s: failed to start offload effect.", __func__);
        }
    } else {
        ALOGE("%s: function pointer is null.", __func__);
        return -EINVAL;
    }

    return ret;
}

int StreamOutPrimary::StopOffloadEffects(
                                    audio_io_handle_t ioHandle,
                                    qal_stream_handle_t* qal_stream_handle) {
    int ret  = 0;
    if (fnp_offload_effect_stop_output_) {
        ret = fnp_offload_effect_stop_output_(ioHandle, qal_stream_handle);
        if (ret) {
            ALOGE("%s: failed to stop offload effect.\n", __func__);
        }
    } else {
        ALOGE("%s: function pointer is null.", __func__);
        return -EINVAL;
    }

    return ret;
}


int StreamOutPrimary::StartOffloadVisualizer(
                                    audio_io_handle_t ioHandle,
                                    qal_stream_handle_t* qal_stream_handle) {
    int ret  = 0;
    if (fnp_visualizer_start_output_) {
        ret = fnp_visualizer_start_output_(ioHandle, qal_stream_handle);
        if (ret) {
            ALOGE("%s: failed to visualizer_start.", __func__);
        }
    } else {
        ALOGE("%s: function pointer is null.", __func__);
        return -EINVAL;
    }

    return ret;
}

int StreamOutPrimary::StopOffloadVisualizer(
                                    audio_io_handle_t ioHandle,
                                    qal_stream_handle_t* qal_stream_handle) {
    int ret  = 0;
    if (fnp_visualizer_stop_output_) {
        ret = fnp_visualizer_stop_output_(ioHandle, qal_stream_handle);
        if (ret) {
            ALOGE("%s: failed to visualizer_stop.\n", __func__);
        }
    } else {
        ALOGE("%s: function pointer is null.", __func__);
        return -EINVAL;
    }

    return ret;
}

StreamOutPrimary::StreamOutPrimary(
                        audio_io_handle_t handle,
                        audio_devices_t devices,
                        audio_output_flags_t flags,
                        struct audio_config *config,
                        const char *address __unused,
                        offload_effects_start_output start_offload_effect,
                        offload_effects_stop_output stop_offload_effect,
                        visualizer_hal_start_output visualizer_start_output,
                        visualizer_hal_stop_output visualizer_stop_output):
    StreamPrimary(handle, devices, config),
    flags_(flags)
{
    stream_ = std::shared_ptr<audio_stream_out> (new audio_stream_out());
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    mInitialized = false;
    int noQalDevices = 0;
    int ret = 0;

    if (!stream_) {
        ALOGE("%s: No memory allocated for stream_", __func__);
        goto error;
    }
    ALOGE("%s: enter: handle (%x) format(%#x) sample_rate(%d) channel_mask(%#x) devices(%#x) flags(%#x)\
          address(%s)", __func__, handle, config->format, config->sample_rate, config->channel_mask,
          devices, flags, address);

    //TODO: check if USB device is connected or not
    if (audio_is_usb_out_device(devices & AUDIO_DEVICE_OUT_ALL_USB)) {
        if (!config->sample_rate) {
            // get capability from device of USB
            qal_param_device_capability_t *device_cap_query = new qal_param_device_capability_t();
            dynamic_media_config_t dynamic_media_config;
            size_t payload_size = 0;
            device_cap_query->id = QAL_DEVICE_OUT_USB_DEVICE;
            device_cap_query->addr.card_id = adevice->usb_card_id_;
            device_cap_query->addr.device_num = adevice->usb_dev_num_;
            device_cap_query->config = &dynamic_media_config;
            device_cap_query->is_playback = true;
            ret = qal_get_param(QAL_PARAM_ID_DEVICE_CAPABILITY,
                                (void **)&device_cap_query,
                                &payload_size, nullptr);
            delete device_cap_query;

            config->sample_rate = dynamic_media_config.sample_rate;
            config->channel_mask = dynamic_media_config.mask;
            config->format = (audio_format_t)dynamic_media_config.format;
            memcpy(&config_, config, sizeof(struct audio_config));
            ALOGI("%s: sample rate = %#x channel_mask=%#x fmt=%#x %d",
                  __func__, config->sample_rate, config->channel_mask,
                  config->format, __LINE__);

        }
    }

    if (devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
        ALOGD("AUDIO_DEVICE_OUT_AUX_DIGITAL and DIRECT | OFFLOAD, check hdmi caps");
        if (config->sample_rate == 0) {
            config->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
            config_.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        }
        if (config->channel_mask == AUDIO_CHANNEL_NONE) {
            config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
            config_.channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
        }
        if (config->format == AUDIO_FORMAT_DEFAULT) {
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            config_.format = AUDIO_FORMAT_PCM_16_BIT;
        }
    }

    usecase_ = GetOutputUseCase(flags);
    if (address) {
        strlcpy((char *)&address_, address, AUDIO_DEVICE_MAX_ADDRESS_LEN);
    } else {
        ALOGD("%s: invalid address", __func__);
    }

    fnp_offload_effect_start_output_ = start_offload_effect;
    fnp_offload_effect_stop_output_ = stop_offload_effect;

    fnp_visualizer_start_output_ = visualizer_start_output;
    fnp_visualizer_stop_output_ = visualizer_stop_output;

    writeAt.tv_sec = 0;
    writeAt.tv_nsec = 0;
    total_bytes_written_ = 0;
    convertBuffer = NULL;

    mNoOfOutDevices = popcount(devices);
    if (!mNoOfOutDevices) {
        mNoOfOutDevices = 1;
        devices = AUDIO_DEVICE_OUT_DEFAULT;
    }
    ALOGD("%s: No of Android devices %d", __func__, mNoOfOutDevices);

    mQalOutDeviceIds = new qal_device_id_t[mNoOfOutDevices];
    if (!mQalOutDeviceIds) {
           goto error;
    }
    noQalDevices = getQalDeviceIds(devices, mQalOutDeviceIds);
    if (noQalDevices != mNoOfOutDevices) {
        ALOGE("%s: mismatched qal no of devices %d and hal devices %d", __func__, noQalDevices, mNoOfOutDevices);
        goto error;
    }
    mQalOutDevice = new qal_device[mNoOfOutDevices];
    if (!mQalOutDevice) {
        goto error;
    }

    /* TODO: how to update based on stream parameters and see if device is supported */
    for (int i = 0; i < mNoOfOutDevices; i++) {
        mQalOutDevice[i].id = mQalOutDeviceIds[i];
        if (audio_is_usb_out_device(devices & AUDIO_DEVICE_OUT_ALL_USB))
            mQalOutDevice[i].config.sample_rate = config_.sample_rate;
        else
            mQalOutDevice[i].config.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        mQalOutDevice[i].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        mQalOutDevice[i].config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
        ALOGI("%s: device rate = %#x width=%#x fmt=%#x %d",
            __func__, mQalOutDevice[i].config.sample_rate,
            mQalOutDevice[i].config.bit_width,
            mQalOutDevice[i].config.aud_fmt_id, __LINE__);
            mQalOutDevice[i].config.ch_info = {0, {0}};
        if ((mQalOutDeviceIds[i] == QAL_DEVICE_OUT_USB_DEVICE) ||
           (mQalOutDeviceIds[i] == QAL_DEVICE_OUT_USB_HEADSET)) {
            mQalOutDevice[i].address.card_id = adevice->usb_card_id_;
            mQalOutDevice[i].address.device_num = adevice->usb_dev_num_;
        }
    }

    if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        stream_.get()->start = astream_out_mmap_noirq_start;
        stream_.get()->stop = astream_out_mmap_noirq_stop;
        stream_.get()->create_mmap_buffer = astream_out_create_mmap_buffer;
        stream_.get()->get_mmap_position = astream_out_get_mmap_position;
    }
    (void)FillHalFnPtrs();
    mInitialized = true;
    audio_extn_gef_notify_device_config(devices, config_.channel_mask, config_.sample_rate);

error:
    return;
}

StreamOutPrimary::~StreamOutPrimary() {
    ALOGD("%s: close stream, handle(%x), qal_stream_handle (%p)", __func__,
          handle_, qal_stream_handle_);

    if (qal_stream_handle_) {
        if (CheckOffloadEffectsType(streamAttributes_.type)) {
            StopOffloadEffects(handle_, qal_stream_handle_);
        }

        if (CheckOffloadEffectsType(streamAttributes_.type)) {
            StopOffloadVisualizer(handle_, qal_stream_handle_);
        }

        qal_stream_close(qal_stream_handle_);
        qal_stream_handle_ = nullptr;
    }
    if (convertBuffer)
        free(convertBuffer);
}

bool StreamInPrimary::isDeviceAvailable(qal_device_id_t deviceId)
{
    for (int i = 0; i < mNoOfInDevices; i++) {
        if (mQalInDevice[i].id == deviceId)
            return true;
    }

    return false;
}

int StreamInPrimary::Stop() {
    int ret = -ENOSYS;

    if (usecase_ == USECASE_AUDIO_RECORD_MMAP &&
            qal_stream_handle_ && stream_started_) {

        ret = qal_stream_stop(qal_stream_handle_);
        if (ret == 0)
            stream_started_ = false;
    }
    return ret;
}

int StreamInPrimary::Start() {
    int ret = -ENOSYS;

    if (usecase_ == USECASE_AUDIO_RECORD_MMAP &&
            qal_stream_handle_ && !stream_started_) {

        ret = qal_stream_start(qal_stream_handle_);
        if (ret == 0)
            stream_started_ = true;
    }
    return ret;
}

int StreamInPrimary::CreateMmapBuffer(int32_t min_size_frames,
        struct audio_mmap_buffer_info *info)
{
    int ret;
    struct qal_mmap_buffer qalMmapBuf;

    if (qal_stream_handle_) {
        ALOGE("%s: qal handle already created\n", __func__);
        return -EINVAL;
    }

    ret = Open();
    if (ret) {
        ALOGE("%s: failed to open stream.", __func__);
        return ret;
    }
    ret = qal_stream_create_mmap_buffer(qal_stream_handle_,
            min_size_frames, &qalMmapBuf);
    if (ret) {
        ALOGE("%s: failed to create mmap buffer: %d", __func__, ret);
        Standby();
        return ret;
    }
    info->shared_memory_address = qalMmapBuf.buffer;
    info->shared_memory_fd = qalMmapBuf.fd;
    info->buffer_size_frames = qalMmapBuf.buffer_size_frames;
    info->burst_size_frames = qalMmapBuf.burst_size_frames;
    info->flags = (audio_mmap_buffer_flag)qalMmapBuf.flags;

    return ret;
}

int StreamInPrimary::GetMmapPosition(struct audio_mmap_position *position)
{
    struct qal_mmap_position qal_mmap_pos;
    int32_t ret = 0;

    if (qal_stream_handle_ == nullptr) {
        ALOGE("%s: qal handle is null\n", __func__);
        return -EINVAL;
    }

    ret = qal_stream_get_mmap_position(qal_stream_handle_, &qal_mmap_pos);
    if (ret) {
        ALOGE("%s: failed to get mmap position %d\n", __func__, ret);
        return ret;
    }
    position->position_frames = qal_mmap_pos.position_frames;
    position->time_nanoseconds = qal_mmap_pos.time_nanoseconds;

    return 0;
}

int StreamInPrimary::Standby() {
    int ret = 0;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    if (qal_stream_handle_) {
        if (!is_st_session) {
            ret = qal_stream_stop(qal_stream_handle_);
        } else {
            ret = qal_stream_set_param(qal_stream_handle_,
                QAL_PARAM_ID_STOP_BUFFERING, nullptr);
            if (adevice->num_va_sessions_ > 0) {
                adevice->num_va_sessions_--;
            }
        }
    }
    effects_applied_ = true;
    stream_started_ = false;

    if (qal_stream_handle_ && !is_st_session) {
        ret = qal_stream_close(qal_stream_handle_);
        qal_stream_handle_ = NULL;
    }

    if (ret)
        return -EINVAL;
    else
        return ret;
}

int StreamInPrimary::addRemoveAudioEffect(const struct audio_stream *stream __unused,
                                   effect_handle_t effect,
                                   bool enable)
{
    int status = 0;
    effect_descriptor_t desc;

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        return status;


    if (source_ == AUDIO_SOURCE_VOICE_COMMUNICATION) {
        if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
            if (enable) {
                if (isECEnabled) {
                    ALOGE("%s: EC already enabled", __func__);
                    goto exit;
                } else if (isNSEnabled) {
                    ALOGV("%s: Got EC enable and NS is already active. Enabling ECNS", __func__);
                    status = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_ECNS,true);
                    isECEnabled = true;
                    goto exit;
                } else {
                    ALOGV("%s: Got EC enable. Enabling EC", __func__);
                    status = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_EC,true);
                    isECEnabled = true;
                    goto exit;
               }
            } else {
                if (isECEnabled) {
                    if (isNSEnabled) {
                        ALOGV("%s: ECNS is running. Disabling EC and enabling NS alone", __func__);
                        status = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_NS,true);
                        isECEnabled = false;
                        goto exit;
                    } else {
                        ALOGV("%s: EC is running. Disabling it", __func__);
                        status = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_ECNS,false);
                        isECEnabled = false;
                        goto exit;
                    }
                } else {
                    ALOGE("%s: EC is not enabled", __func__);
                    goto exit;
               }
            }
        }

        if (memcmp(&desc.type, FX_IID_NS, sizeof(effect_uuid_t)) == 0) {
            if (enable) {
                if (isNSEnabled) {
                    ALOGE("%s: NS already enabled", __func__);
                    goto exit;
                } else if (isECEnabled) {
                    ALOGV("%s: Got NS enable and EC is already active. Enabling ECNS", __func__);
                    status = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_ECNS,true);
                    isNSEnabled = true;
                    goto exit;
                } else {
                    ALOGV("%s: Got NS enable. Enabling NS", __func__);
                    status = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_NS,true);
                    isNSEnabled = true;
                    goto exit;
               }
            } else {
                if (isNSEnabled) {
                    if (isECEnabled) {
                        ALOGV("%s: ECNS is running. Disabling NS and enabling EC alone", __func__);
                        status = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_EC,true);
                        isNSEnabled = false;
                        goto exit;
                    } else {
                        ALOGV("%s: NS is running. Disabling it", __func__);
                        status = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_ECNS,false);
                        isNSEnabled = false;
                        goto exit;
                    }
                } else {
                    ALOGE("%s: NS is not enabled", __func__);
                    goto exit;
               }
            }
        }
    }
exit:
    if (status) {
       effects_applied_ = false;
    } else
       effects_applied_ = true;

    return 0;
}


int StreamInPrimary::SetGain(float gain) {
    struct qal_volume_data* volume;
    int ret = 0;

    volume = (struct qal_volume_data*)malloc(sizeof(uint32_t)
                +sizeof(struct qal_channel_vol_kv));
    volume->no_of_volpair = 1;
    volume->volume_pair[0].channel_mask = 0x03;
    volume->volume_pair[0].vol = gain;
    ret = qal_stream_set_volume(qal_stream_handle_, volume);

    free(volume);
    if (ret) {
        ALOGE("Qal Stream volume Error (%x)", ret);
    }

    return ret;
}

int StreamInPrimary::SetParameters(const char* kvpairs) {

    struct str_parms *parms = (str_parms *)NULL;
    char value[64];
    int ret = 0, val = 0, noQalDevices = 0;
    qal_device_id_t * deviceId;
    struct qal_device* deviceIdConfigs;
    int err =  -EINVAL;
    struct qal_channel_info ch_info = {0, {0}};
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    ALOGD("%s: enter: kvpairs=%s", __func__, kvpairs);
    parms = str_parms_create_str(kvpairs);
    if (!parms)
        goto exit;

    if (!mInitialized)
        goto exit;

    err = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (err >= 0) {

        val = atoi(value);
        ALOGV("%s: Found routing for input stream with value %x", __func__, val);
        // TBD: Hard code number of channels to 2 for now.
        //channels = audio_channel_count_from_out_mask(config_.channel_mask);
        // need to convert channel mask to qal channel mask
        ch_info.channels = 2;
        ch_info.ch_map[0] = QAL_CHMAP_CHANNEL_FL;
        if (ch_info.channels > 1 )
            ch_info.ch_map[1] = QAL_CHMAP_CHANNEL_FR;

        /* If its the same device as what was already routed to, dont bother */
        if ((mAndroidInDevices != val) && (val != 0) && audio_is_input_device(val)) {
            //re-allocate mQalOutDevice and mQalOutDeviceIds
            if (popcount(val & ~AUDIO_DEVICE_BIT_IN) != mNoOfInDevices) {
                deviceId = (qal_device_id_t*) realloc(mQalInDeviceIds, popcount(val & ~AUDIO_DEVICE_BIT_IN) * sizeof(qal_device_id_t));
                deviceIdConfigs = (struct qal_device*) realloc(mQalInDevice, popcount(val & ~AUDIO_DEVICE_BIT_IN) * sizeof(struct qal_device));
                if (!deviceId || !deviceIdConfigs) {
                    ret = -ENOMEM;
                    goto exit;
                }
                mQalInDeviceIds = deviceId;
                mQalInDevice = deviceIdConfigs;
            }

            noQalDevices = getQalDeviceIds(val, mQalInDeviceIds);


            if (noQalDevices != popcount(val & ~AUDIO_DEVICE_BIT_IN)) {
                ret = -EINVAL;
                goto exit;
            }

            mNoOfInDevices = noQalDevices;
            for (int i = 0; i < mNoOfInDevices; i++) {
                mQalInDevice[i].id = mQalInDeviceIds[i];
                mQalInDevice[i].config.sample_rate = mQalInDevice[0].config.sample_rate;
                mQalInDevice[i].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
                mQalInDevice[i].config.ch_info = ch_info; //is there a reason to have two different ch_info for device/stream?
                mQalInDevice[i].config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
                if ((mQalInDeviceIds[i] == QAL_DEVICE_IN_USB_DEVICE) ||
                   (mQalInDeviceIds[i] == QAL_DEVICE_IN_USB_HEADSET)) {
                    mQalInDevice[i].address.card_id = adevice->usb_card_id_;
                    mQalInDevice[i].address.device_num = adevice->usb_dev_num_;
                }
            }
            mAndroidInDevices = val;
            ret = qal_stream_set_device(qal_stream_handle_, mNoOfInDevices, mQalInDevice);
        }
    }

#if 0
   //TBD: check if its offload and check call the following

   ret = AudioExtn::audio_extn_parse_compress_metadata(&config_, &qparam_payload, parms);
   if (ret) {
          ALOGE("parse_compress_metadata Error (%x)", ret);
          goto exit;
       }
   ret = qal_stream_set_param(qal_stream_handle_, 0, &qparam_payload);
   if (ret) {
      ALOGE("Qal Set Param Error (%x)", ret);
   }
#endif
exit:
   ALOGE("%s: exit %d", __func__, ret);
   return ret;
}

int StreamInPrimary::Open() {
    int ret = -EINVAL;
    uint8_t channels = 0;
    struct qal_channel_info ch_info = {0, {0}};
    uint32_t inBufSize = 0;
    uint32_t outBufSize = 0;
    uint32_t inBufCount = NO_OF_BUF;
    uint32_t outBufCount = NO_OF_BUF;
    void *handle = nullptr;

    if (!mInitialized) {
        ALOGE("%s: Not initialized, returning error", __func__);
        goto error_open;
    }

    handle = audio_extn_sound_trigger_check_and_get_session(this);
    if (handle) {
        ALOGV("Found existing qal stream handle associated with capture handle");
        qal_stream_handle_ = (qal_stream_handle_t *)handle;
        goto set_buff_size;
    }

    channels = audio_channel_count_from_in_mask(config_.channel_mask);
    if (channels == 0) {
       ALOGE("invalid channel count");
       return -EINVAL;
    }
    //need to convert channel mask to qal channel mask
    if (channels == 8) {
      ch_info.channels = 8;
      ch_info.ch_map[0] = QAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = QAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = QAL_CHMAP_CHANNEL_C;
      ch_info.ch_map[3] = QAL_CHMAP_CHANNEL_LFE;
      ch_info.ch_map[4] = QAL_CHMAP_CHANNEL_LB;
      ch_info.ch_map[5] = QAL_CHMAP_CHANNEL_RB;
      ch_info.ch_map[6] = QAL_CHMAP_CHANNEL_LS;
      ch_info.ch_map[6] = QAL_CHMAP_CHANNEL_RS;
    } else if (channels == 7) {
      ch_info.channels = 7;
      ch_info.ch_map[0] = QAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = QAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = QAL_CHMAP_CHANNEL_C;
      ch_info.ch_map[3] = QAL_CHMAP_CHANNEL_LFE;
      ch_info.ch_map[4] = QAL_CHMAP_CHANNEL_LB;
      ch_info.ch_map[5] = QAL_CHMAP_CHANNEL_RB;
      ch_info.ch_map[6] = QAL_CHMAP_CHANNEL_LS;
    } else if (channels == 6) {
      ch_info.channels = 6;
      ch_info.ch_map[0] = QAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = QAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = QAL_CHMAP_CHANNEL_C;
      ch_info.ch_map[3] = QAL_CHMAP_CHANNEL_LFE;
      ch_info.ch_map[4] = QAL_CHMAP_CHANNEL_LB;
      ch_info.ch_map[5] = QAL_CHMAP_CHANNEL_RB;
    } else if (channels == 5) {
      ch_info.channels = 5;
      ch_info.ch_map[0] = QAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = QAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = QAL_CHMAP_CHANNEL_C;
      ch_info.ch_map[3] = QAL_CHMAP_CHANNEL_LFE;
      ch_info.ch_map[4] = QAL_CHMAP_CHANNEL_RC;
    } else if (channels == 4) {
      ch_info.channels = 4;
      ch_info.ch_map[0] = QAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = QAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = QAL_CHMAP_CHANNEL_C;
      ch_info.ch_map[3] = QAL_CHMAP_CHANNEL_LFE;
    } else if (channels == 3) {
      ch_info.channels = 3;
      ch_info.ch_map[0] = QAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = QAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = QAL_CHMAP_CHANNEL_C;
    } else if (channels == 2) {
      ch_info.channels = 2;
      ch_info.ch_map[0] = QAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = QAL_CHMAP_CHANNEL_FR;
    } else {
      ch_info.channels = 1;
      ch_info.ch_map[0] = QAL_CHMAP_CHANNEL_FL;
    }

    streamAttributes_.type = StreamInPrimary::GetQalStreamType(flags_,
            config_.sample_rate);
    if (source_ == AUDIO_SOURCE_VOICE_UPLINK) {
        streamAttributes_.type = QAL_STREAM_VOICE_CALL_RECORD;
        streamAttributes_.info.voice_rec_info.record_direction = INCALL_RECORD_VOICE_UPLINK;
    } else if (source_ == AUDIO_SOURCE_VOICE_DOWNLINK) {
        streamAttributes_.type = QAL_STREAM_VOICE_CALL_RECORD;
        streamAttributes_.info.voice_rec_info.record_direction = INCALL_RECORD_VOICE_DOWNLINK;
    } else if (source_ == AUDIO_SOURCE_VOICE_CALL) {
        streamAttributes_.type = QAL_STREAM_VOICE_CALL_RECORD;
        streamAttributes_.info.voice_rec_info.record_direction = INCALL_RECORD_VOICE_UPLINK_DOWNLINK;
    }
    streamAttributes_.flags = (qal_stream_flags_t)0;
    streamAttributes_.direction = QAL_AUDIO_INPUT;
    streamAttributes_.in_media_config.sample_rate = config_.sample_rate;
    streamAttributes_.in_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    streamAttributes_.in_media_config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
    streamAttributes_.in_media_config.ch_info = ch_info;

    if (streamAttributes_.type == QAL_STREAM_ULTRA_LOW_LATENCY) {
            if (usecase_ == USECASE_AUDIO_RECORD_MMAP)
                streamAttributes_.flags = (qal_stream_flags_t)
                    (QAL_STREAM_FLAG_MMAP_NO_IRQ);
            else if (usecase_ == USECASE_AUDIO_RECORD_LOW_LATENCY)
                streamAttributes_.flags = (qal_stream_flags_t)
                    (QAL_STREAM_FLAG_MMAP);
    }

    if (streamAttributes_.type == QAL_STREAM_PROXY &&
            (isDeviceAvailable(QAL_DEVICE_IN_PROXY)))
        streamAttributes_.info.opt_stream_info.tx_proxy_type = QAL_STREAM_PROXY_TX_WFD;

    ALOGD("%s:(%x:ret)%d", __func__, ret, __LINE__);

    ret = qal_stream_open(&streamAttributes_,
                         mNoOfInDevices,
                         mQalInDevice,
                         0,
                         NULL,
                         &qal_callback,
                         (void *)this,
                         &qal_stream_handle_);

    ALOGD("%s:(%x:ret)%d", __func__, ret, __LINE__);

    if (ret) {
        ALOGE("Qal Stream Open Error (%x)", ret);
        ret = -EINVAL;
        goto error_open;
    }

set_buff_size:
    if (usecase_ == USECASE_AUDIO_RECORD_MMAP) {
        inBufSize = MMAP_PERIOD_SIZE * audio_bytes_per_frame(
                    audio_channel_count_from_in_mask(config_.channel_mask),
                    config_.format);
        inBufCount = MMAP_PERIOD_COUNT_DEFAULT;
    } else if (usecase_ == USECASE_AUDIO_RECORD_LOW_LATENCY) {
        inBufSize = ULL_PERIOD_SIZE * audio_bytes_per_frame(
                    audio_channel_count_from_in_mask(config_.channel_mask),
                    config_.format);
        inBufCount = ULL_PERIOD_COUNT_DEFAULT;
    } else
        inBufSize = StreamInPrimary::GetBufferSize();
    if (!handle) {
        ret = qal_stream_set_buffer_size(qal_stream_handle_,(size_t*)&inBufSize,inBufCount,(size_t*)&outBufSize,outBufCount);
        if (ret) {
            ALOGE("Qal Stream set buffer size Error  (%x)", ret);
        }
    }

    total_bytes_read_ = 0; // reset at each open

error_open:
    return ret;
}


/* in bytes */
uint32_t StreamInPrimary::GetBufferSize() {
    struct qal_stream_attributes streamAttributes_;

    streamAttributes_.type = StreamInPrimary::GetQalStreamType(flags_,
            config_.sample_rate);
    if (streamAttributes_.type == QAL_STREAM_VOIP_TX) {
        return voip_get_buffer_size(config_.sample_rate);
    } else if (streamAttributes_.type == QAL_STREAM_LOW_LATENCY) {
        return LOW_LATENCY_CAPTURE_PERIOD_SIZE *
            audio_bytes_per_frame(
                    audio_channel_count_from_in_mask(config_.channel_mask),
                    config_.format);
    } else if (streamAttributes_.type == QAL_STREAM_ULTRA_LOW_LATENCY) {
        return ULL_PERIOD_SIZE * ULL_PERIOD_MULTIPLIER *
            audio_bytes_per_frame(
                    audio_channel_count_from_in_mask(config_.channel_mask),
                    config_.format);
    } else if (streamAttributes_.type == QAL_STREAM_DEEP_BUFFER) {
        return (config_.sample_rate * AUDIO_CAPTURE_PERIOD_DURATION_MSEC/ 1000) *
            audio_bytes_per_frame(
                    audio_channel_count_from_in_mask(config_.channel_mask),
                    config_.format);
    } else if (streamAttributes_.type == QAL_STREAM_PROXY) {
        return config_.frame_count *
            audio_bytes_per_frame(
                    audio_channel_count_from_in_mask(config_.channel_mask),
                    config_.format);
    } else {
        return BUF_SIZE_CAPTURE * NO_OF_BUF;
    }
}

int StreamInPrimary::GetInputUseCase(audio_input_flags_t halStreamFlags, audio_source_t source)
{
    // TODO: cover other usecases
    int usecase = USECASE_AUDIO_RECORD;
    if (config_.sample_rate == LOW_LATENCY_CAPTURE_SAMPLE_RATE &&
        (halStreamFlags & AUDIO_INPUT_FLAG_TIMESTAMP) == 0 &&
        (halStreamFlags & AUDIO_INPUT_FLAG_COMPRESS) == 0 &&
        (halStreamFlags & AUDIO_INPUT_FLAG_FAST) != 0 &&
        (!(isDeviceAvailable(QAL_DEVICE_IN_PROXY))))
        usecase = USECASE_AUDIO_RECORD_LOW_LATENCY;

    if ((halStreamFlags & AUDIO_INPUT_FLAG_MMAP_NOIRQ) != 0)
        usecase = USECASE_AUDIO_RECORD_MMAP;
    else if (source == AUDIO_SOURCE_VOICE_COMMUNICATION &&
             halStreamFlags & AUDIO_INPUT_FLAG_VOIP_TX)
        usecase = USECASE_AUDIO_RECORD_VOIP;

    return usecase;
}

ssize_t StreamInPrimary::Read(const void *buffer, size_t bytes) {
    int ret = 0;
    int retry_count = MAX_READ_RETRY_COUNT;
    ssize_t size = 0;
    struct qal_buffer qalBuffer;
    uint32_t local_bytes_read = 0;
    qalBuffer.buffer = (void*)buffer;
    qalBuffer.size = bytes;
    qalBuffer.offset = 0;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    ALOGV("%s: Bytes:(%zu)", __func__, bytes);
    if (!qal_stream_handle_) {
        ret = Open();
    }

    if (is_st_session) {
        ATRACE_BEGIN("hal: lab read");
        if (!stream_started_) {
            adevice->num_va_sessions_++;
            stream_started_ = true;
        }
        while (retry_count--) {
            size = qal_stream_read(qal_stream_handle_, &qalBuffer);
            if (size < 0) {
                memset(qalBuffer.buffer, 0, qalBuffer.size);
                local_bytes_read = qalBuffer.size;
                total_bytes_read_ += local_bytes_read;
                ALOGE("%s: error, failed to read data from QAL", __func__);
                ATRACE_END();
                goto exit;
            } else if (size == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } else {
                break;
            }
        }
        local_bytes_read = size;
        total_bytes_read_ += local_bytes_read;
        ATRACE_END();
        goto exit;
    }

    if (!stream_started_) {
        ret = qal_stream_start(qal_stream_handle_);
        if (ret) {
            ALOGE("%s:failed to start stream. ret=%d", __func__, ret);
            qal_stream_close(qal_stream_handle_);
            qal_stream_handle_ = NULL;
            return -EINVAL;
        }
        stream_started_ = true;
        /* set cached volume if any, dont return failure back up */
        if (volume_) {
            ret = qal_stream_set_volume(qal_stream_handle_, volume_);
            if (ret) {
                ALOGE("Qal Stream volume Error (%x)", ret);
            }
        }
    }

    if (!effects_applied_) {
       if (isECEnabled && isNSEnabled) {
          ret = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_ECNS,true);
       } else if (isECEnabled) {
          ret = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_EC,true);
       } else if (isNSEnabled) {
          ret = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_NS,true);
       } else {
          ret = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_ECNS,false);
       }
       effects_applied_ = true;
    }

    local_bytes_read = qal_stream_read(qal_stream_handle_, &qalBuffer);

    // mute pcm data if sva client is reading lab data
    if (adevice->num_va_sessions_ > 0 &&
        source_ != AUDIO_SOURCE_VOICE_RECOGNITION &&
        property_get_bool("persist.vendor.audio.va_concurrency_mute_enabled",
        false)) {
        memset(qalBuffer.buffer, 0, qalBuffer.size);
        local_bytes_read = qalBuffer.size;
    }

    total_bytes_read_ += local_bytes_read;

exit:
    ALOGV("%s: Exit, bytes read %u", __func__, local_bytes_read);

    return local_bytes_read;
}

int StreamInPrimary::FillHalFnPtrs() {
    int ret = 0;

    stream_.get()->common.get_sample_rate = astream_in_get_sample_rate;
    stream_.get()->common.set_sample_rate = astream_set_sample_rate;
    stream_.get()->common.get_buffer_size = astream_in_get_buffer_size;
    stream_.get()->common.get_channels = astream_in_get_channels;
    stream_.get()->common.get_format = astream_in_get_format;
    stream_.get()->common.set_format = astream_set_format;
    stream_.get()->common.standby = astream_in_standby;
    stream_.get()->common.dump = astream_dump;
    stream_.get()->common.set_parameters = astream_in_set_parameters;
    stream_.get()->common.get_parameters = astream_in_get_parameters;
    stream_.get()->common.add_audio_effect = astream_in_add_audio_effect;
    stream_.get()->common.remove_audio_effect = astream_in_remove_audio_effect;
    stream_.get()->set_gain = astream_in_set_gain;
    stream_.get()->read = in_read;
    stream_.get()->get_input_frames_lost = astream_in_get_input_frames_lost;
    stream_.get()->get_capture_position = astream_in_get_capture_position;
    stream_.get()->get_active_microphones = astream_in_get_active_microphones;
    stream_.get()->set_microphone_direction =
                                            astream_in_set_microphone_direction;
    stream_.get()->set_microphone_field_dimension =
                                            in_set_microphone_field_dimension;
    stream_.get()->update_sink_metadata = in_update_sink_metadata;

    return ret;
}

StreamInPrimary::StreamInPrimary(audio_io_handle_t handle,
    audio_devices_t devices,
    audio_input_flags_t flags,
    struct audio_config *config,
    const char *address __unused,
    audio_source_t source) :
    StreamPrimary(handle, devices, config),
    flags_(flags)
{
    stream_ = std::shared_ptr<audio_stream_in> (new audio_stream_in());
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    qal_stream_handle_ = NULL;
    mInitialized = false;
    int noQalDevices = 0;
    int ret = 0;

    ALOGD("%s: enter: handle (%x) format(%#x) sample_rate(%d) channel_mask(%#x) devices(%#x) flags(%#x)"\
          , __func__, handle, config->format, config->sample_rate, config->channel_mask,
          devices, flags);
    if (audio_is_usb_in_device(devices)) {
        if (!config->sample_rate) {
            // get capability from device of USB
            qal_param_device_capability_t *device_cap_query = new qal_param_device_capability_t();
            dynamic_media_config_t dynamic_media_config;
            size_t payload_size = 0;
            device_cap_query->id = QAL_DEVICE_IN_USB_HEADSET;
            device_cap_query->addr.card_id = adevice->usb_card_id_;
            device_cap_query->addr.device_num = adevice->usb_dev_num_;
            device_cap_query->config = &dynamic_media_config;
            device_cap_query->is_playback = false;
            ret = qal_get_param(QAL_PARAM_ID_DEVICE_CAPABILITY,
                                (void **)&device_cap_query,
                                &payload_size, nullptr);
            ALOGD("%s: usb fs=%d format=%d mask=%x", __func__,
                dynamic_media_config.sample_rate,
                dynamic_media_config.format, dynamic_media_config.mask);
            delete device_cap_query;
            config->sample_rate = dynamic_media_config.sample_rate;
            config->channel_mask = dynamic_media_config.mask;
            config->format = (audio_format_t)dynamic_media_config.format;
            memcpy(&config_, config, sizeof(struct audio_config));
        }
    }

            /* this is required for USB otherwise adev_open_input_stream is failed */
    if (!config_.sample_rate)
        config_.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    if (!config_.channel_mask)
        config_.channel_mask = AUDIO_CHANNEL_IN_MONO;
    if (!config_.format)
        config_.format = AUDIO_FORMAT_PCM_16_BIT;

    ALOGD("%s: local : handle (%x) format(%#x) sample_rate(%d) channel_mask(%#x) devices(%#x) flags(%#x)"\
          , __func__, handle, config_.format, config_.sample_rate, config_.channel_mask,
          devices, flags);


    source_ = source;

    mAndroidInDevices = devices;
    mNoOfInDevices = popcount(devices & ~AUDIO_DEVICE_BIT_IN);
    if (!mNoOfInDevices) {
        mNoOfInDevices = 1;
        devices = AUDIO_DEVICE_IN_DEFAULT;
    }

    ALOGD("%s: No of devices %d", __func__, mNoOfInDevices);
    mQalInDeviceIds = new qal_device_id_t[mNoOfInDevices];
    if (!mQalInDeviceIds) {
        goto error;
    }

    noQalDevices = getQalDeviceIds(devices, mQalInDeviceIds);
    if (noQalDevices != mNoOfInDevices) {
        ALOGE("%s: mismatched qal %d and hal devices %d", __func__, noQalDevices, mNoOfInDevices);
        goto error;
    }
    mQalInDevice = new qal_device [mNoOfInDevices];
    if (!mQalInDevice) {
        goto error;
    }

    for (int i = 0; i < mNoOfInDevices; i++) {
        mQalInDevice[i].id = mQalInDeviceIds[i];
        mQalInDevice[i].config.sample_rate = config->sample_rate;
        mQalInDevice[i].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        // ch_info memory is allocated at resource manager:getdeviceconfig
        mQalInDevice[i].config.ch_info = {0, {0}};
        mQalInDevice[i].config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM; // TODO: need to convert this from output format
        if ((mQalInDeviceIds[i] == QAL_DEVICE_IN_USB_DEVICE) ||
           (mQalInDeviceIds[i] == QAL_DEVICE_IN_USB_HEADSET)) {
            mQalInDevice[i].address.card_id = adevice->usb_card_id_;
            mQalInDevice[i].address.device_num = adevice->usb_dev_num_;
        }
    }

    usecase_ = GetInputUseCase(flags, source);
    if (flags & AUDIO_INPUT_FLAG_MMAP_NOIRQ) {
        stream_.get()->start = astream_in_mmap_noirq_start;
        stream_.get()->stop = astream_in_mmap_noirq_stop;
        stream_.get()->create_mmap_buffer = astream_in_create_mmap_buffer;
        stream_.get()->get_mmap_position = astream_in_get_mmap_position;
    }
    (void)FillHalFnPtrs();
    mInitialized = true;
error:
    return;
}

StreamInPrimary::~StreamInPrimary() {
    if (qal_stream_handle_) {
        ALOGD("%s: close stream, qal_stream_handle (%p)", __func__,
             qal_stream_handle_);
        qal_stream_close(qal_stream_handle_);
        qal_stream_handle_ = NULL;
    }
}

StreamPrimary::StreamPrimary(audio_io_handle_t handle,
    audio_devices_t devices __unused, struct audio_config *config):
    qal_stream_handle_(NULL),
    handle_(handle),
    config_(*config),
    volume_(NULL)
{
    memset(&streamAttributes_, 0, sizeof(streamAttributes_));
    memset(&address_, 0, sizeof(address_));
    ALOGE("%s: handle: %d channel_mask: %d ", __func__, handle_, config_.channel_mask);
}

StreamPrimary::~StreamPrimary(void)
{
    if (volume_) {
        free(volume_);
        volume_ = NULL;
    }
}

