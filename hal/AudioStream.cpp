/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#define LOG_NDEBUG 0
#define LOG_TAG "AHAL: AudioStream"
#define ATRACE_TAG (ATRACE_TAG_AUDIO | ATRACE_TAG_HAL)
#include "AudioCommon.h"

#include "AudioDevice.h"
#include "AudioStream.h"

#include <log/log.h>
#include <utils/Trace.h>
#include <cutils/properties.h>
#include <inttypes.h>

#include <chrono>
#include <thread>

#include "PalApi.h"
#include <audio_effects/effect_aec.h>
#include <audio_effects/effect_ns.h>
#include "audio_extn.h"
#include <audio_utils/format.h>

#define COMPRESS_OFFLOAD_FRAGMENT_SIZE (32 * 1024)
#define FLAC_COMPRESS_OFFLOAD_FRAGMENT_SIZE (256 * 1024)

#define COMPRESS_CAPTURE_AAC_MAX_OUTPUT_BUFFER_SIZE 2048

#define MAX_READ_RETRY_COUNT 25
#define MAX_ACTIVE_MICROPHONES_TO_SUPPORT 10
#define AFE_PROXY_RECORD_PERIOD_SIZE  768

static bool karaoke = false;
static bool hac_voip = false;

static bool is_pcm_format(audio_format_t format)
{
    if (format == AUDIO_FORMAT_PCM_16_BIT ||
        format == AUDIO_FORMAT_PCM_8_BIT ||
        format == AUDIO_FORMAT_PCM_8_24_BIT ||
        format == AUDIO_FORMAT_PCM_FLOAT ||
        format == AUDIO_FORMAT_PCM_24_BIT_PACKED ||
        format == AUDIO_FORMAT_PCM_32_BIT)
        return true;
    return false;
}

static bool is_hdr_mode_enabled() {
    if (!property_get_bool("vendor.audio.hdr.record.enable", false)) {
        AHAL_INFO("HDR feature is disabled");
        return false;
    }

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    return adevice->hdr_record_enabled;
}

static void setup_hdr_usecase(struct pal_device* palInDevice) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    bool orientationLandscape = adevice->orientation_landscape;
    bool orientationInverted = adevice->inverted;

    if (orientationLandscape && !orientationInverted) {
        strlcpy(palInDevice->custom_config.custom_key,
            "unprocessed-hdr-mic-landscape",
            sizeof(palInDevice->custom_config.custom_key));
    } else if (!orientationLandscape && !orientationInverted) {
        strlcpy(palInDevice->custom_config.custom_key,
            "unprocessed-hdr-mic-portrait",
            sizeof(palInDevice->custom_config.custom_key));
    } else if (orientationLandscape && orientationInverted) {
        strlcpy(palInDevice->custom_config.custom_key,
            "unprocessed-hdr-mic-inverted-landscape",
            sizeof(palInDevice->custom_config.custom_key));
    } else if (!orientationLandscape && orientationInverted) {
        strlcpy(palInDevice->custom_config.custom_key,
            "unprocessed-hdr-mic-inverted-portrait",
            sizeof(palInDevice->custom_config.custom_key));
    }
    AHAL_INFO("Setting custom key as %s",
        palInDevice->custom_config.custom_key);
}

/*
* Scope based implementation of acquiring/releasing PerfLock.
*/
class AutoPerfLock {
public :
    AutoPerfLock() {
        std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
        if (adevice) {
            adevice->adev_perf_mutex.lock();
            ++adevice->perf_lock_acquire_cnt;
            if (adevice->perf_lock_acquire_cnt == 1)
                AudioExtn::audio_extn_perf_lock_acquire(&adevice->perf_lock_handle, 0,
                        adevice->perf_lock_opts, adevice->perf_lock_opts_size);
            AHAL_DBG("(Acquired) perf_lock_handle: 0x%x, count: %d",
                    adevice->perf_lock_handle, adevice->perf_lock_acquire_cnt);
            adevice->adev_perf_mutex.unlock();
        }
    }

    ~AutoPerfLock() {
        std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
        if (adevice) {
            adevice->adev_perf_mutex.lock();
            AHAL_DBG("(release) perf_lock_handle: 0x%x, count: %d",
                    adevice->perf_lock_handle, adevice->perf_lock_acquire_cnt);
            if (adevice->perf_lock_acquire_cnt > 0)
                --adevice->perf_lock_acquire_cnt;
            if (adevice->perf_lock_acquire_cnt == 0) {
                AHAL_DBG("Releasing perf_lock_handle: 0x%x", adevice->perf_lock_handle);
                AudioExtn::audio_extn_perf_lock_release(&adevice->perf_lock_handle);
            }
            adevice->adev_perf_mutex.unlock();
        }
    }
};

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

audio_channel_mask_t StreamPrimary::GetChannelMask() {
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

bool StreamPrimary::GetSupportedConfig(bool isOutStream,
        struct str_parms *query,
        struct str_parms *reply)
{
    char value[256];
    int ret = 0;
    bool found = false;
    int index = 0;
    int table_size = 0;
    int i = 0;

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        int stream_format = 0;
        bool first = true;
        table_size = sizeof(formats_name_to_enum_table) / sizeof(struct string_to_enum);
        if (device_cap_query_) {
            while (device_cap_query_->config->format[i]!= 0) {
                stream_format = device_cap_query_->config->format[i];
                index = GetLookupTableIndex(formats_name_to_enum_table,
                                            table_size, stream_format);
                if (!first) {
                    strlcat(value, "|", sizeof(value));
                }
                if (index >= 0 && index < table_size) {
                    strlcat(value, formats_name_to_enum_table[index].name, sizeof(value));
                    found = true;
                    first = false;
                }
                i++;
            }
        } else {
            stream_format = GetFormat();
            index = GetLookupTableIndex(formats_name_to_enum_table,
                                        table_size, stream_format);
            if (index >= 0 && index < table_size) {
                strlcat(value, formats_name_to_enum_table[index].name, sizeof(value));
                found = true;
            }
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value);
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value, sizeof(value));
    if (ret >= 0) {
        bool first = true;
        value[0] = '\0';
        i = 0;
        if (device_cap_query_) {
            while (device_cap_query_->config->mask[i] != 0) {
                for (int j = 0; j < ARRAY_SIZE(channels_name_to_enum_table); j++) {
                    if (channels_name_to_enum_table[j].value == device_cap_query_->config->mask[i]) {
                        if (!first)
                            strlcat(value, "|", sizeof(value));
                        strlcat(value, channels_name_to_enum_table[j].name, sizeof(value));
                        first = false;
                        break;
                    }
                }
                i++;
            }
        } else {
            if (isOutStream)
                strlcat(value, "AUDIO_CHANNEL_OUT_STEREO", sizeof(value));
            else
                strlcat(value, "AUDIO_CHANNEL_IN_STEREO", sizeof(value));
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value);
        found = true;
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        i = 0;
        int cursor = 0;
        if (device_cap_query_) {
            while (device_cap_query_->config->sample_rate[i] != 0) {
                int avail = sizeof(value) - cursor;
                ret = snprintf(value + cursor, avail, "%s%d",
                               cursor > 0 ? "|" : "",
                               device_cap_query_->config->sample_rate[i]);
                if (ret < 0 || ret >= avail) {
                    // if cursor is at the last element of the array
                    //    overwrite with \0 is duplicate work as
                    //    snprintf already put a \0 in place.
                    // else
                    //    we had space to write the '|' at value[cursor]
                    //    (which will be overwritten) or no space to fill
                    //    the first element (=> cursor == 0)
                    value[cursor] = '\0';
                    break;
                }
                cursor += ret;
                ++i;
            }
        } else {
            int stream_sample_rate = GetSampleRate();
            int avail = sizeof(value);
            ret = snprintf(value, avail, "%d", stream_sample_rate);
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES,
                          value);
        found = true;
    }

    return found;
}

#if 0
static pal_stream_type_t GetPalStreamType(audio_output_flags_t flags) {
    std::ignore = flags;
    return PAL_STREAM_LOW_LATENCY;
}
#endif
//audio_hw_device_t* AudioDevice::device_ = NULL;
std::shared_ptr<AudioDevice> AudioDevice::adev_ = nullptr;
std::shared_ptr<audio_hw_device_t> AudioDevice::device_ = nullptr;

static int32_t pal_callback(pal_stream_handle_t *stream_handle,
                            uint32_t event_id, uint32_t *event_data,
                            uint32_t event_size, uint64_t cookie)
{
    stream_callback_event_t event;
    StreamOutPrimary *astream_out = reinterpret_cast<StreamOutPrimary *> (cookie);

    AHAL_VERBOSE("stream_handle (%p), event_id (%x), event_data (%p), cookie %" PRIu64
          "event_size (%d)", stream_handle, event_id, event_data,
           cookie, event_size);

    switch (event_id)
    {
        case PAL_STREAM_CBK_EVENT_WRITE_READY:
        {
            std::lock_guard<std::mutex> write_guard (astream_out->write_wait_mutex_);
            astream_out->write_ready_ = true;
            AHAL_VERBOSE("received WRITE_READY event");
            (astream_out->write_condition_).notify_all();
            event = STREAM_CBK_EVENT_WRITE_READY;
        }
        break;

    case PAL_STREAM_CBK_EVENT_DRAIN_READY:
        {
            std::lock_guard<std::mutex> drain_guard (astream_out->drain_wait_mutex_);
            astream_out->drain_ready_ = true;
            astream_out->sendGaplessMetadata = false;
            AHAL_DBG("received DRAIN_READY event");
            (astream_out->drain_condition_).notify_all();
            event = STREAM_CBK_EVENT_DRAIN_READY;
        }
        break;
    case PAL_STREAM_CBK_EVENT_PARTIAL_DRAIN_READY:
        {
            std::lock_guard<std::mutex> drain_guard (astream_out->drain_wait_mutex_);
            astream_out->drain_ready_ = true;
            astream_out->sendGaplessMetadata = true;
            AHAL_DBG("received PARTIAL DRAIN_READY event");
            (astream_out->drain_condition_).notify_all();
            event = STREAM_CBK_EVENT_DRAIN_READY;
        }
        break;
    case PAL_STREAM_CBK_EVENT_ERROR:
        AHAL_DBG("received PAL_STREAM_CBK_EVENT_ERROR event");
        event = STREAM_CBK_EVENT_ERROR;
        break;
    default:
        AHAL_ERR("Invalid event id:%d", event_id);
        return -EINVAL;
    }

    if (astream_out && astream_out->client_callback) {
        AHAL_VERBOSE("Callback to Framework");
        astream_out->client_callback(event, NULL, astream_out->client_cookie);
    }

    return 0;
}


static int astream_out_mmap_noirq_start(const struct audio_stream_out *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        AHAL_ERR("unable to get audio OutStream");
        return -EINVAL;
    }

    return astream_out->Start();
}

static int astream_out_mmap_noirq_stop(const struct audio_stream_out *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        AHAL_ERR("unable to get audio OutStream");
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
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        AHAL_ERR("unable to get audio OutStream");
        return -EINVAL;
    }

    if (info == NULL || !(min_size_frames > 0 && min_size_frames < INT32_MAX)) {
        AHAL_ERR("invalid field info = %p, min_size_frames = %d", info, min_size_frames);
        return -EINVAL;
    }
    if (astream_out->GetUseCase() != USECASE_AUDIO_PLAYBACK_MMAP) {
         AHAL_ERR("invalid usecase = %d", astream_out->GetUseCase());
         return -ENOSYS;
    }

    ret = astream_out->CreateMmapBuffer(min_size_frames, info);
    if (ret)
        AHAL_ERR("failed %d\n", ret);

    return ret;
}

static int astream_out_get_mmap_position(const struct audio_stream_out *stream,
        struct audio_mmap_position *position)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        AHAL_ERR("unable to get audio OutStream");
        return -EINVAL;
    }
    if (astream_out->GetUseCase() != USECASE_AUDIO_PLAYBACK_MMAP) {
         AHAL_ERR("invalid usecase = %d", astream_out->GetUseCase());
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
        AHAL_ERR("unable to get audio device");
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
        AHAL_ERR("unable to get audio device");

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

static audio_channel_mask_t astream_out_get_channels(const struct audio_stream *stream) {

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out = nullptr;

    AHAL_VERBOSE("stream_out(%p)", stream);

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return (audio_channel_mask_t) 0;
    }
    astream_out = adevice->OutGetStream((audio_stream_t*)stream);

    if (!astream_out) {
        AHAL_ERR("unable to get audio stream");
        return (audio_channel_mask_t) 0;
    }

    if (astream_out->GetChannelMask())
        return astream_out->GetChannelMask();
    return (audio_channel_mask_t) 0;
}

static int astream_pause(struct audio_stream_out *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        AHAL_ERR("unable to get audio stream");
        return -EINVAL;
    }

    AHAL_DBG("pause");
    return astream_out->Pause();
}

static int astream_resume(struct audio_stream_out *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        AHAL_ERR("unable to get audio stream");
        return -EINVAL;
    }

    return astream_out->Resume();
}

static int astream_flush(struct audio_stream_out *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        AHAL_ERR("unable to get audio stream");
        return -EINVAL;
    }

    return astream_out->Flush();
}

static int astream_drain(struct audio_stream_out *stream, audio_drain_type_t type)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        AHAL_ERR("unable to get audio stream");
        return -EINVAL;
    }

    return astream_out->Drain(type);
}

static int astream_set_callback(struct audio_stream_out *stream, stream_callback_t callback, void *cookie)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (!callback) {
        AHAL_ERR("error: NULL Callback passed");
        return -EINVAL;
    }

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    if (!astream_out) {
        AHAL_ERR("unable to get audio stream");
        return -EINVAL;
    }

    astream_out->client_callback = callback;
    astream_out->client_cookie = cookie;

    return 0;
}

static int astream_out_standby(struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;
    int ret = 0;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    AHAL_DBG("enter: stream (%p), usecase(%d: %s)", astream_out.get(),
          astream_out->GetUseCase(), use_case_table[astream_out->GetUseCase()]);

    if (astream_out) {
        ret = astream_out->Standby();
    } else {
        AHAL_ERR("unable to get audio stream");
        ret = -EINVAL;
    }
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

static int astream_dump(const struct audio_stream *stream, int fd) {
    std::ignore = stream;
    std::ignore = fd;
    AHAL_DBG("dump function not implemented");
    return 0;
}

static uint32_t astream_get_latency(const struct audio_stream_out *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;
    uint32_t period_ms, latency = 0;
    int trial = 0;
    char value[PROPERTY_VALUE_MAX] = {0};
    int low_latency_period_size = LOW_LATENCY_PLAYBACK_PERIOD_SIZE;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
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
        latency = DEEP_BUFFER_OUTPUT_PERIOD_DURATION * DEEP_BUFFER_PLAYBACK_PERIOD_COUNT;
        latency += StreamOutPrimary::GetRenderLatency(astream_out->flags_) / 1000;
        break;
    case USECASE_AUDIO_PLAYBACK_LOW_LATENCY:
        if (property_get("vendor.audio_hal.period_size", value, NULL) > 0) {
            trial = atoi(value);
            if (astream_out->period_size_is_plausible_for_low_latency(trial))
                low_latency_period_size = trial;
        }
        latency = (LOW_LATENCY_PLAYBACK_PERIOD_COUNT * low_latency_period_size * 1000)/ (astream_out->GetSampleRate());
        latency += StreamOutPrimary::GetRenderLatency(astream_out->flags_) / 1000;
        break;
    case USECASE_AUDIO_PLAYBACK_WITH_HAPTICS:
        latency = LOW_LATENCY_OUTPUT_PERIOD_DURATION * LOW_LATENCY_PLAYBACK_PERIOD_COUNT;
        latency += StreamOutPrimary::GetRenderLatency(astream_out->flags_) / 1000;
        break;
    case USECASE_AUDIO_PLAYBACK_VOIP:
        latency += VOIP_PERIOD_COUNT_DEFAULT * DEFAULT_VOIP_BUF_DURATION_MS;
        break;
    default:
        latency += StreamOutPrimary::GetRenderLatency(astream_out->flags_) / 1000;
        break;
    }

    // accounts for A2DP encoding and sink latency
    pal_param_bta2dp_t *param_bt_a2dp = NULL;
    size_t size = 0;
    int32_t ret;

    if (astream_out->isDeviceAvailable(PAL_DEVICE_OUT_BLUETOOTH_A2DP)) {
        ret = pal_get_param(PAL_PARAM_ID_BT_A2DP_ENCODER_LATENCY,
                            (void **)&param_bt_a2dp, &size, nullptr);
        if (!ret && param_bt_a2dp)
            latency += param_bt_a2dp->latency;
    }

    AHAL_VERBOSE("Latency %d", latency);
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
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }
    if (!timestamp) {
       AHAL_ERR("error: timestamp NULL");
       return -EINVAL;
    }
    if (astream_out) {
       switch (astream_out->GetPalStreamType(astream_out->flags_)) {
       case PAL_STREAM_PCM_OFFLOAD:
           if (AudioDevice::sndCardState == CARD_STATUS_OFFLINE) {
               *frames = astream_out->GetFramesWritten(timestamp);
               astream_out->UpdatemCachedPosition(*frames);
               break;
           }
            /* fall through if the card is online for PCM OFFLOAD stream */
       case PAL_STREAM_COMPRESSED:
           ret = astream_out->GetFrames(frames);
           if (ret) {
               AHAL_ERR("GetTimestamp failed %d", ret);
               return ret;
           }
           clock_gettime(CLOCK_MONOTONIC, timestamp);
           break;
       default:
          *frames = astream_out->GetFramesWritten(timestamp);
          break;
       }
    } else {
        //AHAL_ERR("unable to get audio stream");
        return -EINVAL;
    }
    AHAL_VERBOSE("frames %lld played at %lld ", ((long long) *frames), timestamp->tv_sec * 1000000LL + timestamp->tv_nsec / 1000);

    return ret;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames) {
    std::ignore = stream;
    std::ignore = dsp_frames;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;
    int ret = 0;
    uint64_t frames = 0;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }
    if (astream_out) {
        switch (astream_out->GetPalStreamType(astream_out->flags_)) {
        case PAL_STREAM_PCM_OFFLOAD:
            if (AudioDevice::sndCardState == CARD_STATUS_OFFLINE) {
                frames =  astream_out->GetFramesWritten(NULL);
                *dsp_frames = (uint32_t) frames;
                astream_out->UpdatemCachedPosition(*dsp_frames);
                break;
            }
             /* fall through if the card is online for PCM OFFLOAD stream */
        case PAL_STREAM_COMPRESSED:
            ret = astream_out->GetFrames(&frames);
            if (ret) {
                AHAL_ERR("Get DSP Frames failed %d", ret);
                return ret;
            }
            *dsp_frames = (uint32_t) frames;
            break;
        case PAL_STREAM_LOW_LATENCY:
        case PAL_STREAM_DEEP_BUFFER:
            frames =  astream_out->GetFramesWritten(NULL);
            *dsp_frames = (uint32_t) frames;
            break;
        default:
            break;
        }
    }
    return 0;
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
        AHAL_ERR("unable to get audio device");
        return ret;
    }

    AHAL_DBG("enter: usecase(%d: %s) kvpairs: %s",
             astream_out->GetUseCase(), use_case_table[astream_out->GetUseCase()], kvpairs);

    parms = str_parms_create_str(kvpairs);
    if (!parms) {
       ret = -EINVAL;
       goto exit;
    }


    ret = astream_out->SetParameters(parms);
    if (ret) {
        AHAL_ERR("Stream SetParameters Error (%x)", ret);
        goto exit;
    }
exit:
    if (parms)
        str_parms_destroy(parms);
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

static char* astream_out_get_parameters(const struct audio_stream *stream,
                                        const char *keys) {
    int ret = 0, hal_output_suspend_supported = 0;
    struct str_parms *query = str_parms_create_str(keys);
    char value[256];
    char *str = (char*) nullptr;
    std::shared_ptr<StreamOutPrimary> astream_out;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    struct str_parms *reply = str_parms_create();

    AHAL_DBG("enter");

    if (!query || !reply) {
        if (reply)
            str_parms_destroy(reply);
        if (query)
            str_parms_destroy(query);
        AHAL_ERR("out_get_parameters: failed to allocate mem for query or reply");
        return nullptr;
    }

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        goto error;
    }

    if (!astream_out) {
        AHAL_ERR("unable to get audio stream");
        goto error;
    }
    AHAL_DBG("keys: %s", keys);

    ret = str_parms_get_str(query, "is_direct_pcm_track", value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';

        if (astream_out->flags_ & AUDIO_OUTPUT_FLAG_DIRECT &&
             !(astream_out->flags_ & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
            AHAL_VERBOSE("in direct_pcm");
            strlcat(value, "true", sizeof(value));
        } else {
            AHAL_VERBOSE("not in direct_pcm");
            strlcat(value, "false", sizeof(value));
        }
        str_parms_add_str(reply, "is_direct_pcm_track", value);
        if (str)
            free(str);
        str = str_parms_to_str(reply);
    }

    if (str_parms_get_str(query, "supports_hw_suspend", value, sizeof(value)) >= 0) {
        //only low latency track supports suspend_resume
        if (astream_out->flags_ == (AUDIO_OUTPUT_FLAG_FAST|AUDIO_OUTPUT_FLAG_PRIMARY))
            hal_output_suspend_supported = 1;
        str_parms_add_int(reply, "supports_hw_suspend", hal_output_suspend_supported);
        if (str)
            free(str);
        str = str_parms_to_str(reply);
    }

    if (astream_out->GetSupportedConfig(true, query, reply))
        str = str_parms_to_str(reply);

error:
    str_parms_destroy(query);
    str_parms_destroy(reply);

    AHAL_DBG("exit: returns - %s", str);
    return str;
}

static int astream_out_set_volume(struct audio_stream_out *stream,
                                  float left, float right) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    if (astream_out) {
        return astream_out->SetVolume(left, right);
    } else {
        AHAL_ERR("unable to get audio stream");
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
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    if (astream_in) {
        return astream_in->read(buffer, bytes);
    } else {
        AHAL_ERR("unable to get audio stream");
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
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    if (astream_out) {
        return astream_out->write(buffer, bytes);
    } else {
        AHAL_ERR("unable to get audio stream");
        return -EINVAL;
    }

    return 0;
}

static int astream_in_mmap_noirq_start(const struct audio_stream_in *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_in = adevice->InGetStream((audio_stream_t*)stream);
    if (!astream_in) {
        AHAL_ERR("unable to get audio InStream");
        return -EINVAL;
    }

    return astream_in->Start();
}

static int astream_in_mmap_noirq_stop(const struct audio_stream_in *stream)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (!adevice) {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_in = adevice->InGetStream((audio_stream_t*)stream);
    if (!astream_in) {
        AHAL_ERR("unable to get audio InStream");
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
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_in = adevice->InGetStream((audio_stream_t*)stream);
    if (!astream_in) {
        AHAL_ERR("unable to get audio InStream");
        return -EINVAL;
    }

    if (info == NULL || !(min_size_frames > 0 && min_size_frames < INT32_MAX)) {
        AHAL_ERR("invalid field info = %p, min_size_frames = %d", info, min_size_frames);
        return -EINVAL;
    }
    if (astream_in->GetUseCase() != USECASE_AUDIO_RECORD_MMAP) {
         AHAL_ERR("invalid usecase = %d", astream_in->GetUseCase());
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
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    astream_in = adevice->InGetStream((audio_stream_t*)stream);
    if (!astream_in) {
        AHAL_ERR("unable to get audio InStream");
        return -EINVAL;
    }
    if (astream_in->GetUseCase() != USECASE_AUDIO_RECORD_MMAP) {
         AHAL_ERR("usecase = %d", astream_in->GetUseCase());
         return -ENOSYS;
    }

    return astream_in->GetMmapPosition(position);
}

static int astream_in_set_microphone_direction(
                        const struct audio_stream_in *stream,
                        audio_microphone_direction_t dir) {
    std::ignore = stream;
    std::ignore = dir;
    AHAL_VERBOSE("function not implemented");
    //No plans to implement audiozoom
    return -ENOSYS;
}

static int in_set_microphone_field_dimension(
                        const struct audio_stream_in *stream,
                        float zoom) {
    std::ignore = stream;
    std::ignore = zoom;
    AHAL_VERBOSE("function not implemented");
    //No plans to implement audiozoom
    return -ENOSYS;
}

static int astream_in_add_audio_effect(
                                const struct audio_stream *stream,
                                effect_handle_t effect)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;
    int ret = 0;

    AHAL_DBG("Enter ");
    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        ret= -EINVAL;
        goto exit;
    }
    if (astream_in) {
        ret = astream_in->addRemoveAudioEffect(stream, effect, true);
    } else {
        AHAL_ERR("unable to get audio stream");
        ret = -EINVAL;
        goto exit;
    }
exit:
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

static int astream_in_remove_audio_effect(const struct audio_stream *stream,
                                          effect_handle_t effect)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;
    int ret = 0;

    AHAL_DBG("Enter ");
    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        ret = -EINVAL;
        goto exit;
    }
    if (astream_in) {
        ret = astream_in->addRemoveAudioEffect(stream, effect, false);
    } else {
        AHAL_ERR("unable to get audio stream");
        ret = -EINVAL;
    }
exit:
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int64_t StreamInPrimary::GetSourceLatency(audio_input_flags_t halStreamFlags)
{
    // check how to get dsp_latency value from platform info xml instead of hardcoding
    return 0;
    /*struct pal_stream_attributes streamAttributes_;
    streamAttributes_.type = StreamInPrimary::GetPalStreamType(halStreamFlags,
        config_.sample_rate);
    AHAL_VERBOSE(" type %d", streamAttributes_.type);
    switch (streamAttributes_.type) {
    case PAL_STREAM_DEEP_BUFFER:
        return DEEP_BUFFER_PLATFORM_CAPTURE_DELAY;
    case PAL_STREAM_LOW_LATENCY:
        return LOW_LATENCY_PLATFORM_CAPTURE_DELAY;
    case  PAL_STREAM_VOIP_TX:
        return VOIP_TX_PLATFORM_CAPTURE_DELAY;
    case  PAL_STREAM_RAW:
        return RAW_STREAM_PLATFORM_CAPTURE_DELAY;
        //TODO: Add more streamtypes if available in pal
    default:
        return 0;
    }*/
}

uint64_t StreamInPrimary::GetFramesRead(int64_t* time)
{
    uint64_t signed_frames = 0;
    uint64_t kernel_frames = 0;
    size_t kernel_buffer_size = 0;
    int64_t dsp_latency = 0;

    if (!time) {
        AHAL_ERR("timestamp NULL");
        return 0;
    }

    //TODO: need to get this latency from xml instead of hardcoding
    stream_mutex_.lock();
    dsp_latency = StreamInPrimary::GetSourceLatency(flags_);

    if (usecase_ == USECASE_AUDIO_RECORD_COMPRESS) {
        /**
         * TODO get num of pcm frames from below layers
         **/
        signed_frames = mCompressReadCalls * 1024;
    } else {
        signed_frames = mBytesRead / audio_bytes_per_frame(
        audio_channel_count_from_in_mask(config_.channel_mask),
        config_.format);
    }

    *time = (readAt.tv_sec * 1000000000LL) + readAt.tv_nsec - (dsp_latency * 1000LL);

    // Adjustment accounts for A2dp decoder latency
    // Note: Decoder latency is returned in ms, while platform_source_latency in us.
    pal_param_bta2dp_t* param_bt_a2dp = NULL;
    size_t size = 0;
    int32_t ret;

    if (isDeviceAvailable(PAL_DEVICE_IN_BLUETOOTH_A2DP)) {
        ret = pal_get_param(PAL_PARAM_ID_BT_A2DP_DECODER_LATENCY,
            (void**)&param_bt_a2dp, &size, nullptr);
        if (!ret && param_bt_a2dp) {
            *time -= param_bt_a2dp->latency * 1000000LL;
        }
    }
    stream_mutex_.unlock();

    AHAL_VERBOSE("signed frames %lld", (long long)signed_frames);

    return signed_frames;
}

static int astream_in_get_capture_position(const struct audio_stream_in* stream,
    int64_t* frames, int64_t* time) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (stream == NULL || frames == NULL || time == NULL) {
        return -EINVAL;
    }

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        return -ENOSYS;
    }
    if(astream_in)
        *frames = astream_in->GetFramesRead(time);
    else
        return -ENOSYS;
    AHAL_VERBOSE("audio stream(%p) frames %lld played at %lld ",
                 astream_in.get(), ((long long)*frames), ((long long)*time));

    return 0;
}

static uint32_t astream_in_get_input_frames_lost(
                                struct audio_stream_in *stream __unused) {
    return 0;
}

static void in_update_sink_metadata_v7(
                                struct audio_stream_in *stream,
                                const struct sink_metadata_v7 *sink_metadata) {
    if (stream == NULL
            || sink_metadata == NULL
            || sink_metadata->tracks == NULL) {
        AHAL_ERR("%s: stream or sink_metadata is NULL", __func__);
        return;
    }
    audio_devices_t device = sink_metadata->tracks->base.dest_device;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    int ret = 0;


    AHAL_DBG("%s: sink device %d", __func__, device);

    if (device == AUDIO_DEVICE_OUT_HEARING_AID) {
        std::set<audio_devices_t> device_types;
        device_types.insert(device);
        if (adevice && adevice->voice_) {
            ret = adevice->voice_->RouteStream(device_types);
            AHAL_DBG("%s voice RouteStream ret = %d", __func__, ret);
        }
        else {
            AHAL_ERR("%s: voice handle does not exist", __func__);
        }
    }
}

static int astream_in_get_active_microphones(
                        const struct audio_stream_in *stream,
                        struct audio_microphone_characteristic_t *mic_array,
                        size_t *mic_count) {
    int noPalDevices = 0;
    pal_device_id_t palDevs[MAX_ACTIVE_MICROPHONES_TO_SUPPORT];
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;
    uint32_t channels = 0;
    uint32_t in_mic_count = 0;
    uint32_t out_mic_count = 0;
    uint32_t total_mic_count = 0;
    int ret = 0;

    if (mic_count == NULL) {
        AHAL_ERR("Invalid mic_count!!!");
        ret = -EINVAL;
        goto done;
    }
    if (mic_array == NULL) {
        AHAL_ERR("Invalid mic_array!!!");
        ret = -EINVAL;
        goto done;
    }
    if (*mic_count == 0) {
        AHAL_INFO("mic_count is ZERO!!!");
        goto done;
    }

    in_mic_count = (uint32_t)(*mic_count);

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        AHAL_INFO("unable to get audio device");
        goto done;
    }

    if (astream_in) {
        channels = astream_in->GetChannelMask();
        memset(palDevs, 0, MAX_ACTIVE_MICROPHONES_TO_SUPPORT*sizeof(pal_device_id_t));
        if (!(astream_in->GetPalDeviceIds(palDevs, &noPalDevices))) {
            for (int i = 0; i < noPalDevices; i++) {
                if (total_mic_count < in_mic_count) {
                    out_mic_count = in_mic_count - total_mic_count;
                    if (!adevice->get_active_microphones((uint32_t)channels, palDevs[i],
                                                        &mic_array[total_mic_count], &out_mic_count))
                            total_mic_count += out_mic_count;
                }
            }
        }
    }
done:
    if (NULL != mic_count)
        *mic_count = total_mic_count;
    return ret;
}

static uint32_t astream_in_get_sample_rate(const struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        return 0;
    }

    if (astream_in)
        return astream_in->GetSampleRate();
    else
        return 0;
}

static audio_channel_mask_t astream_in_get_channels(const struct audio_stream *stream) {

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        return (audio_channel_mask_t) 0;
    }

    if (astream_in) {
        return astream_in->GetChannelMask();
    } else {
        AHAL_ERR("unable to get audio stream");
        return (audio_channel_mask_t) 0;
    }
}

static audio_format_t astream_in_get_format(const struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice)
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    else
        AHAL_ERR("unable to get audio device");

    if (astream_in)
        return astream_in->GetFormat();
    else
        return AUDIO_FORMAT_DEFAULT;
}

static int astream_in_standby(struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;
    int ret = 0;

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        ret = -EINVAL;
        return ret;
    }

    AHAL_DBG("enter: stream (%p) usecase(%d: %s)", astream_in.get(),
          astream_in->GetUseCase(), use_case_table[astream_in->GetUseCase()]);

    if (astream_in) {
        ret = astream_in->Standby();
    } else {
        AHAL_ERR("unable to get audio stream");
        ret = -EINVAL;
    }
exit:
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

static int astream_in_set_parameters(struct audio_stream *stream, const char *kvpairs) {
    int ret = 0;

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    AHAL_DBG("Enter: %s",kvpairs);

    if (!stream || !kvpairs) {
        ret = 0;
        goto error;
    }

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    if (astream_in) {
        return astream_in->SetParameters(kvpairs);
    }

error:
    AHAL_DBG("Exit: %d",ret);
    return ret;
}

static char* astream_in_get_parameters(const struct audio_stream *stream,
                                       const char *keys) {
    std::ignore = stream;
    std::ignore = keys;
    struct str_parms *query = str_parms_create_str(keys);
    char value[256];
    char *str = (char*) nullptr;
    std::shared_ptr<StreamInPrimary> astream_in;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    struct str_parms *reply = str_parms_create();
    int ret = 0;

    AHAL_DBG("enter");
    if (!query || !reply) {
        if (reply)
            str_parms_destroy(reply);
        if (query)
            str_parms_destroy(query);
        AHAL_ERR("in_get_parameters: failed to allocate mem for query or reply");
        return nullptr;
    }

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        goto error;
    }

    if (!astream_in) {
        AHAL_ERR("unable to get audio stream");
        goto error;
    }
    AHAL_DBG("keys: %s", keys);

    astream_in->GetSupportedConfig(false, query, reply);
    astream_in->getParameters(query,reply);
    str = str_parms_to_str(reply);

error:
    str_parms_destroy(query);
    str_parms_destroy(reply);

    AHAL_DBG("exit: returns - %s", str);
    return str;
}

static int astream_in_set_gain(struct audio_stream_in *stream, float gain) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        AHAL_ERR("unable to get audio device");
        return -EINVAL;
    }

    if (astream_in) {
        return astream_in->SetGain(gain);
    } else {
        AHAL_ERR("unable to get audio stream");
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

int StreamPrimary::getPalDeviceIds(const std::set<audio_devices_t>& halDeviceIds,
                                   pal_device_id_t* qualIds) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    return adevice->GetPalDeviceIds(halDeviceIds, qualIds);
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

pal_stream_type_t StreamInPrimary::GetPalStreamType(
                                        audio_input_flags_t halStreamFlags,
                                        uint32_t sample_rate) {
    pal_stream_type_t palStreamType = PAL_STREAM_LOW_LATENCY;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    if ((halStreamFlags & AUDIO_INPUT_FLAG_VOIP_TX)!=0) {
         palStreamType = PAL_STREAM_VOIP_TX;
         return palStreamType;
    }

    if (sample_rate == LOW_LATENCY_CAPTURE_SAMPLE_RATE &&
            (halStreamFlags & AUDIO_INPUT_FLAG_TIMESTAMP) == 0 &&
            (halStreamFlags & AUDIO_INPUT_FLAG_COMPRESS) == 0 &&
            (halStreamFlags & AUDIO_INPUT_FLAG_FAST) != 0) {
        if (isDeviceAvailable(PAL_DEVICE_IN_PROXY))
            palStreamType = PAL_STREAM_PROXY;
        else
            palStreamType = PAL_STREAM_ULTRA_LOW_LATENCY;

        return palStreamType;
    }
    /*
     * check for input direct flag which is exclusive
     * meant for compress offload capture.
     */
    if ((halStreamFlags & AUDIO_INPUT_FLAG_DIRECT) != 0) {
        palStreamType = PAL_STREAM_COMPRESSED;
        return palStreamType;
    }

    /*
     *For AUDIO_SOURCE_UNPROCESSED we use LL pal stream as it corresponds to
     *RAW record graphs ( record with no pp)
     */
    if (source_ == AUDIO_SOURCE_UNPROCESSED) {
        palStreamType = PAL_STREAM_RAW;
        return palStreamType;
    } else if (source_ == AUDIO_SOURCE_VOICE_RECOGNITION) {
        palStreamType = PAL_STREAM_VOICE_RECOGNITION;
        if (halStreamFlags & AUDIO_INPUT_FLAG_MMAP_NOIRQ) {
            palStreamType = PAL_STREAM_ULTRA_LOW_LATENCY;
        }
        return palStreamType;
    }

    switch (halStreamFlags) {
        case AUDIO_INPUT_FLAG_FAST:
            palStreamType = PAL_STREAM_LOW_LATENCY;
            break;
        case AUDIO_INPUT_FLAG_RAW:
            palStreamType = PAL_STREAM_RAW;
            break;
        case AUDIO_INPUT_FLAG_VOIP_TX:
            palStreamType = PAL_STREAM_VOIP_TX;
            break;
        case AUDIO_INPUT_FLAG_MMAP_NOIRQ:
            palStreamType = PAL_STREAM_ULTRA_LOW_LATENCY;
            break;
        case AUDIO_INPUT_FLAG_HW_HOTWORD:
        case AUDIO_INPUT_FLAG_NONE:
            palStreamType = PAL_STREAM_DEEP_BUFFER;
            if (source_ == AUDIO_SOURCE_VOICE_UPLINK ||
                source_ == AUDIO_SOURCE_VOICE_DOWNLINK ||
                source_ == AUDIO_SOURCE_VOICE_CALL) {
                if (isDeviceAvailable(PAL_DEVICE_IN_TELEPHONY_RX) ||
                   (adevice && adevice->voice_ && adevice->voice_->IsAnyCallActive())) {
                    palStreamType = PAL_STREAM_VOICE_CALL_RECORD;
                }
#ifdef EC_REF_CAPTURE_ENABLED
            } else if (source_ == AUDIO_SOURCE_ECHO_REFERENCE) {
                   palStreamType = PAL_STREAM_RAW;
#endif
            } else {
                if (isDeviceAvailable(PAL_DEVICE_IN_TELEPHONY_RX)) {
                    palStreamType = PAL_STREAM_PROXY;
                }
            }
            break;
        default:
            /*
            unsupported from PAL
            AUDIO_INPUT_FLAG_SYNC        = 0x8,
            AUDIO_INPUT_FLAG_HW_AV_SYNC = 0x40,
            */
            AHAL_ERR("error flag %#x is not supported from PAL." ,
                      halStreamFlags);
            break;
    }

    return palStreamType;
}

pal_stream_type_t StreamOutPrimary::GetPalStreamType(
                                    audio_output_flags_t halStreamFlags) {
    pal_stream_type_t palStreamType = PAL_STREAM_LOW_LATENCY;
    if ((halStreamFlags & AUDIO_OUTPUT_FLAG_VOIP_RX)!=0) {
        palStreamType = PAL_STREAM_VOIP_RX;
        return palStreamType;
    }
    if ((halStreamFlags & AUDIO_OUTPUT_FLAG_RAW) != 0) {
        palStreamType = PAL_STREAM_ULTRA_LOW_LATENCY;
    } else if ((halStreamFlags & AUDIO_OUTPUT_FLAG_FAST) != 0) {
        palStreamType = PAL_STREAM_LOW_LATENCY;
    } else if (halStreamFlags ==
                    (AUDIO_OUTPUT_FLAG_FAST|AUDIO_OUTPUT_FLAG_RAW)) {
        palStreamType = PAL_STREAM_RAW;
    } else if (halStreamFlags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
        palStreamType = PAL_STREAM_DEEP_BUFFER;
    } else if (halStreamFlags ==
                    (AUDIO_OUTPUT_FLAG_DIRECT|AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
        // mmap_no_irq_out: to be confirmed
        palStreamType = PAL_STREAM_ULTRA_LOW_LATENCY;
    } else if (halStreamFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        palStreamType = PAL_STREAM_ULTRA_LOW_LATENCY;
    } else if (halStreamFlags & AUDIO_OUTPUT_FLAG_RAW) {
        palStreamType = PAL_STREAM_ULTRA_LOW_LATENCY;
    } else if (halStreamFlags == (AUDIO_OUTPUT_FLAG_DIRECT|
                                      AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|
                                  AUDIO_OUTPUT_FLAG_NON_BLOCKING)) {
        // hifi: to be confirmed
        palStreamType = PAL_STREAM_COMPRESSED;
    } else if (halStreamFlags == AUDIO_OUTPUT_FLAG_DIRECT) {
        palStreamType = PAL_STREAM_PCM_OFFLOAD;
    } else if (halStreamFlags == (AUDIO_OUTPUT_FLAG_DIRECT|
                                      AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|
                                  AUDIO_OUTPUT_FLAG_NON_BLOCKING)) {
        palStreamType = PAL_STREAM_COMPRESSED;
    } else if (halStreamFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        // dsd_compress_passthrough
        palStreamType = PAL_STREAM_COMPRESSED;
    } else if (halStreamFlags == (AUDIO_OUTPUT_FLAG_DIRECT|
                                      AUDIO_OUTPUT_FLAG_VOIP_RX)) {
        // voice rx
        palStreamType = PAL_STREAM_VOIP_RX;
    } else if (halStreamFlags == AUDIO_OUTPUT_FLAG_VOIP_RX) {
        palStreamType = PAL_STREAM_VOIP_RX;
    } else if (halStreamFlags == AUDIO_OUTPUT_FLAG_INCALL_MUSIC) {
        // incall_music_uplink
        palStreamType = PAL_STREAM_VOICE_CALL_MUSIC;
    } else {
        palStreamType = PAL_STREAM_GENERIC;
    }
    return palStreamType;
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
    struct pal_mmap_position pal_mmap_pos;
    int32_t ret = 0;

    stream_mutex_.lock();
    if (pal_stream_handle_ == nullptr) {
        AHAL_ERR("error pal handle is null\n");
        stream_mutex_.unlock();
        return -EINVAL;
    }

    ret = pal_stream_get_mmap_position(pal_stream_handle_, &pal_mmap_pos);
    if (ret) {
        AHAL_ERR("failed to get mmap position %d\n", ret);
        stream_mutex_.unlock();
        return ret;
    }
    position->position_frames = pal_mmap_pos.position_frames;
    position->time_nanoseconds = pal_mmap_pos.time_nanoseconds;

#if 0
    /** Check if persist vendor property is available */
    const int32_t kDefaultOffsetMicros = 0;
    int32_t mmap_time_offset_micros = property_get_int32(
            "persist.vendor.audio.out_mmap_delay_micros", kDefaultOffsetMicros);

    position->time_nanoseconds += mmap_time_offset_micros * (int64_t)1000;
#endif

    stream_mutex_.unlock();
    return 0;
}

bool StreamOutPrimary::isDeviceAvailable(pal_device_id_t deviceId)
{
    for (int i = 0; i < mAndroidOutDevices.size(); i++) {
        if (mPalOutDevice[i].id == deviceId)
            return true;
    }

    return false;
}

int StreamOutPrimary::CreateMmapBuffer(int32_t min_size_frames,
        struct audio_mmap_buffer_info *info)
{
    int ret;
    struct pal_mmap_buffer palMmapBuf;

    stream_mutex_.lock();
    if (pal_stream_handle_) {
        AHAL_ERR("error pal handle already created\n");
        stream_mutex_.unlock();
        return -EINVAL;
    }

    ret = Open();
    if (ret) {
        AHAL_ERR("failed to open stream.");
        stream_mutex_.unlock();
        return ret;
    }
    ret = pal_stream_create_mmap_buffer(pal_stream_handle_,
            min_size_frames, &palMmapBuf);
    if (ret) {
        AHAL_ERR("failed to create mmap buffer: %d", ret);
        // release stream lock as Standby will lock/unlock stream mutex
        stream_mutex_.unlock();
        Standby();
        return ret;
    }
    info->shared_memory_address = palMmapBuf.buffer;
    info->shared_memory_fd = palMmapBuf.fd;
    info->buffer_size_frames = palMmapBuf.buffer_size_frames;
    info->burst_size_frames = palMmapBuf.burst_size_frames;
    info->flags = (audio_mmap_buffer_flag) AUDIO_MMAP_APPLICATION_SHAREABLE;
    mmap_shared_memory_fd = info->shared_memory_fd;

    stream_mutex_.unlock();
    return ret;
}

int StreamOutPrimary::Stop() {
    int ret = -ENOSYS;

    AHAL_INFO("Enter: OutPrimary usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);
    stream_mutex_.lock();
    if (usecase_ == USECASE_AUDIO_PLAYBACK_VOIP)
        hac_voip = false;
    if (usecase_ == USECASE_AUDIO_PLAYBACK_MMAP &&
            pal_stream_handle_ && stream_started_) {

        ret = pal_stream_stop(pal_stream_handle_);
        if (ret == 0) {
            stream_started_ = false;
            stream_paused_ = false;
        }
    }
    stream_mutex_.unlock();
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int StreamOutPrimary::Start() {
    int ret = -ENOSYS;

    AHAL_INFO("Enter: OutPrimary usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);
    stream_mutex_.lock();
    if (usecase_ == USECASE_AUDIO_PLAYBACK_MMAP &&
            pal_stream_handle_ && !stream_started_) {

        ret = pal_stream_start(pal_stream_handle_);
        if (ret == 0)
            stream_started_ = true;
    }
    if (karaoke)
        AudExtn.karaoke_start();
    stream_mutex_.unlock();
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int StreamOutPrimary::Pause() {
    int ret = 0;

    AHAL_INFO("Enter: usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);

    stream_mutex_.lock();
    if (!pal_stream_handle_ || !stream_started_) {
        AHAL_DBG("Stream not started yet");
        ret = -1;
        goto exit;
    }
    // only direct stream will receive pause/resume cmd from AudioFlinger,
    // VOIP RX is specified to direct output in qcom audio policy config,
    // which doesn't need pause/resume actually.
    if (streamAttributes_.type == PAL_STREAM_VOIP_RX) {
        AHAL_DBG("no need to pause for VOIP RX: %d");
        ret = -1;
        goto exit;
    }

    if (pal_stream_handle_) {
        ret = pal_stream_pause(pal_stream_handle_);
    }
    if (ret)
        ret = -EINVAL;
    else {
        stream_paused_ = true;
    }

exit:
    stream_mutex_.unlock();
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int StreamOutPrimary::Resume() {
    int ret = 0;

    AHAL_INFO("Enter: usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);

    stream_mutex_.lock();
    if (!pal_stream_handle_ || !stream_started_) {
        AHAL_DBG("Stream not started yet");
        ret = -1;
        goto exit;
    }

    if (pal_stream_handle_) {
        ret = pal_stream_resume(pal_stream_handle_);
    }
    if (ret)
        ret = -EINVAL;
    else {
        stream_paused_ = false;
    }

exit:
    stream_mutex_.unlock();
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int StreamOutPrimary::Flush() {
    int ret = 0;
    AHAL_INFO("Enter: usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);

    stream_mutex_.lock();
    if (pal_stream_handle_) {
        if(stream_paused_ == true)
        {
            ret = pal_stream_flush(pal_stream_handle_);
            if (!ret) {
                ret = pal_stream_resume(pal_stream_handle_);
                if (!ret)
                    stream_paused_ = false;
            }
        } else {
            AHAL_INFO("called in invalid state (stream not paused)" );
        }
        mBytesWritten = 0;
    }
    sendGaplessMetadata = true;
    stream_mutex_.unlock();

    if (ret)
        ret = -EINVAL;

    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int StreamOutPrimary::Drain(audio_drain_type_t type) {
    int ret = 0;
    pal_drain_type_t palDrainType;

    AHAL_INFO("Enter: usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);
    switch (type) {
      case AUDIO_DRAIN_ALL:
           palDrainType = PAL_DRAIN;
           break;
      case AUDIO_DRAIN_EARLY_NOTIFY:
           palDrainType = PAL_DRAIN_PARTIAL;
           break;
    default:
           AHAL_ERR("Invalid drain type:%d", type);
           return -EINVAL;
    }

    stream_mutex_.lock();
    if (pal_stream_handle_)
        ret = pal_stream_drain(pal_stream_handle_, palDrainType);
    stream_mutex_.unlock();

    if (ret) {
        AHAL_ERR("Invalid drain type:%d", type);
    }

    return ret;
}

void StreamOutPrimary::UpdatemCachedPosition(uint64_t val)
{
    mCachedPosition = val;
}

int StreamOutPrimary::Standby() {
    int ret = 0;

    AHAL_DBG("Enter");
    stream_mutex_.lock();
    if (pal_stream_handle_) {
        if (streamAttributes_.type == PAL_STREAM_PCM_OFFLOAD) {
            /*
             * when ssr happens, dsp position for pcm offload could be 0,
             * so get written frames. Else, get frames.
             */
            if (AudioDevice::sndCardState == CARD_STATUS_OFFLINE) {
                struct timespec ts;
                // release stream lock as GetFramesWritten will lock/unlock stream mutex
                stream_mutex_.unlock();
                mCachedPosition = GetFramesWritten(&ts);
                stream_mutex_.lock();
                AHAL_DBG("card is offline, return written frames %lld", (long long)mCachedPosition);
            } else {
                GetFrames(&mCachedPosition);
            }
        }
        ret = pal_stream_stop(pal_stream_handle_);
        if (ret) {
            AHAL_ERR("failed to stop stream.");
            ret = -EINVAL;
        }
        if (usecase_ == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS && pal_haptics_stream_handle) {
            ret = pal_stream_stop(pal_haptics_stream_handle);
            if (ret) {
                AHAL_ERR("failed to stop haptics stream.");
            }
        }
        if (usecase_ == USECASE_AUDIO_PLAYBACK_VOIP)
            hac_voip = false;
     }

    stream_started_ = false;
    stream_paused_ = false;
    sendGaplessMetadata = true;
    if (CheckOffloadEffectsType(streamAttributes_.type)) {
        ret = StopOffloadEffects(handle_, pal_stream_handle_);
        ret = StopOffloadVisualizer(handle_, pal_stream_handle_);
    }

    if (pal_stream_handle_) {
        ret = pal_stream_close(pal_stream_handle_);
        pal_stream_handle_ = NULL;
        if (usecase_ == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS && pal_haptics_stream_handle) {
            ret = pal_stream_close(pal_haptics_stream_handle);
            pal_haptics_stream_handle = NULL;
            if (hapticBuffer) {
                free (hapticBuffer);
                hapticBuffer = NULL;
            }
            hapticsBufSize = 0;
            if (hapticsDevice) {
                free(hapticsDevice);
                hapticsDevice = NULL;
            }
        }
    }
    if (karaoke) {
        ret = AudExtn.karaoke_stop();
        if (ret) {
            AHAL_ERR("failed to stop karaoke path.");
            ret = 0;
        } else {
            ret = AudExtn.karaoke_close();
            if (ret) {
                AHAL_ERR("failed to close karaoke path.");
                ret = 0;
            }
        }
    }

    if (mmap_shared_memory_fd >= 0) {
        close(mmap_shared_memory_fd);
        mmap_shared_memory_fd = -1;
    }

    if (ret)
        ret = -EINVAL;

exit:
    stream_mutex_.unlock();
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int StreamOutPrimary::RouteStream(const std::set<audio_devices_t>& new_devices, bool force_device_switch __unused) {
    int ret = 0, noPalDevices = 0;
    pal_device_id_t * deviceId = nullptr;
    struct pal_device* deviceIdConfigs = nullptr;
    pal_param_device_capability_t *device_cap_query = nullptr;
    size_t payload_size = 0;
    dynamic_media_config_t dynamic_media_config;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    bool isHifiFilterEnabled = false;
    bool *payload_hifiFilter = &isHifiFilterEnabled;
    size_t param_size = 0;

    stream_mutex_.lock();
    if (!mInitialized) {
        AHAL_ERR("Not initialized, returning error");
        ret = -EINVAL;
        goto done;
    }

    AHAL_INFO("enter: usecase(%d: %s) devices 0x%x, num devices %zu",
            this->GetUseCase(), use_case_table[this->GetUseCase()],
            AudioExtn::get_device_types(new_devices), new_devices.size());
    AHAL_DBG("mAndroidOutDevices %d, mNoOfOutDevices %zu",
             AudioExtn::get_device_types(mAndroidOutDevices),
             mAndroidOutDevices.size());

    if (!AudioExtn::audio_devices_empty(new_devices)) {
        // re-allocate mPalOutDevice and mPalOutDeviceIds
        if (new_devices.size() != mAndroidOutDevices.size()) {
            deviceId = (pal_device_id_t*) realloc(mPalOutDeviceIds,
                    new_devices.size() * sizeof(pal_device_id_t));
            deviceIdConfigs = (struct pal_device*) realloc(mPalOutDevice,
                    new_devices.size() * sizeof(struct pal_device));
            if (!deviceId || !deviceIdConfigs) {
                AHAL_ERR("Failed to allocate PalOutDeviceIds or deviceIdConfigs!");
                if (deviceId)
                    mPalOutDeviceIds = deviceId;
                if (deviceIdConfigs)
                    mPalOutDevice = deviceIdConfigs;
                ret = -ENOMEM;
                goto done;
            }

            // init deviceId and deviceIdConfigs
            memset(deviceId, 0, new_devices.size() * sizeof(pal_device_id_t));
            memset(deviceIdConfigs, 0, new_devices.size() * sizeof(struct pal_device));

            mPalOutDeviceIds = deviceId;
            mPalOutDevice = deviceIdConfigs;
        }

        noPalDevices = getPalDeviceIds(new_devices, mPalOutDeviceIds);
        AHAL_DBG("noPalDevices: %d , new_devices: %zu",
                noPalDevices, new_devices.size());

        if (noPalDevices != new_devices.size() ||
            noPalDevices >= PAL_DEVICE_IN_MAX) {
            AHAL_ERR("Device count mismatch! Expected: %zu Got: %d",
                    new_devices.size(), noPalDevices);
            ret = -EINVAL;
            goto done;
        }

        device_cap_query = (pal_param_device_capability_t *)
                malloc(sizeof(pal_param_device_capability_t));
        if (!device_cap_query) {
                AHAL_ERR("Failed to allocate device_cap_query!");
                ret = -ENOMEM;
                goto done;
        }

        ret = pal_get_param(PAL_PARAM_ID_HIFI_PCM_FILTER,
                            (void **)&payload_hifiFilter, &param_size, nullptr);

        for (int i = 0; i < noPalDevices; i++) {
            mPalOutDevice[i].id = mPalOutDeviceIds[i];
            mPalOutDevice[i].config.sample_rate = mPalOutDevice[0].config.sample_rate;
            mPalOutDevice[i].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
            mPalOutDevice[i].config.ch_info = {0, {0}};
            mPalOutDevice[i].config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;
            if (((mPalOutDeviceIds[i] == PAL_DEVICE_OUT_USB_DEVICE) ||
               (mPalOutDeviceIds[i] == PAL_DEVICE_OUT_USB_HEADSET)) && device_cap_query) {

                mPalOutDevice[i].address.card_id = adevice->usb_card_id_;
                mPalOutDevice[i].address.device_num = adevice->usb_dev_num_;
                device_cap_query->id = mPalOutDeviceIds[i];
                device_cap_query->addr.card_id = adevice->usb_card_id_;
                device_cap_query->addr.device_num = adevice->usb_dev_num_;
                device_cap_query->config = &dynamic_media_config;
                device_cap_query->is_playback = true;
                ret = pal_get_param(PAL_PARAM_ID_DEVICE_CAPABILITY,(void **)&device_cap_query,
                        &payload_size, nullptr);

                if (ret<0){
                    AHAL_ERR("Error usb device is not connected");
                    ret = -ENOSYS;
                    goto done;
                }
            }

            strlcpy(mPalOutDevice[i].custom_config.custom_key, "",
                    sizeof(mPalOutDevice[i].custom_config.custom_key));

            if ((AudioExtn::audio_devices_cmp(mAndroidOutDevices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) &&
                                   (mPalOutDeviceIds[i] == PAL_DEVICE_OUT_SPEAKER)) {
                strlcpy(mPalOutDevice[i].custom_config.custom_key, "speaker-safe",
                        sizeof(mPalOutDevice[i].custom_config.custom_key));
                AHAL_INFO("Setting custom key as %s", mPalOutDevice[i].custom_config.custom_key);
            }

            if (!ret && isHifiFilterEnabled &&
                (mPalOutDevice[i].id == PAL_DEVICE_OUT_WIRED_HEADSET ||
                 mPalOutDevice[i].id == PAL_DEVICE_OUT_WIRED_HEADPHONE) &&
                (config_.sample_rate != 384000 && config_.sample_rate != 352800)) {

                AHAL_DBG("hifi-filter custom key sent to PAL (only applicable to certain streams)\n");

                strlcpy(mPalOutDevice[i].custom_config.custom_key,
                       "hifi-filter_custom_key",
                       sizeof(mPalOutDevice[i].custom_config.custom_key));
            }
        }

        mAndroidOutDevices = new_devices;

    if (hac_voip && (mPalOutDevice->id == PAL_DEVICE_OUT_HANDSET)) {
         strlcpy(mPalOutDevice->custom_config.custom_key, "HAC",
                sizeof(mPalOutDevice->custom_config.custom_key));
    }

        if (pal_stream_handle_) {
            ret = pal_stream_set_device(pal_stream_handle_, noPalDevices, mPalOutDevice);
            if (!ret) {
                for (const auto &dev : mAndroidOutDevices)
                    audio_extn_gef_notify_device_config(dev,
                            config_.channel_mask,
                            config_.sample_rate, flags_);
            } else {
                AHAL_ERR("failed to set device. Error %d" ,ret);
            }
        }
    }

done:
    if (device_cap_query) {
        free(device_cap_query);
        device_cap_query = NULL;
    }
    stream_mutex_.unlock();
    AHAL_DBG("exit %d", ret);
    return ret;
}

int StreamOutPrimary::SetParameters(struct str_parms *parms) {
    char value[64];
    int ret =  0, controller = -1, stream = -1;
    int ret1 = 0;

    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    AHAL_DBG("enter ");

    if (!mInitialized)
        goto error;

    ret = AudioExtn::get_controller_stream_from_params(parms, &controller, &stream);
    if (ret >= 0) {
        adevice->dp_controller = controller;
        adevice->dp_stream = stream;
        if (stream >= 0 || controller >= 0)
            AHAL_INFO("ret %d, plugin device cont %d stream %d", ret, controller, stream);
    } else {
        AHAL_ERR("error %d, failed to get stream and controller", ret);
    }

    //Parse below metadata only if it is compress offload usecase.
    if (usecase_ == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        ret = AudioExtn::audio_extn_parse_compress_metadata(&config_, &palSndDec, parms,
                                         &msample_rate, &mchannels, &isCompressMetadataAvail);
        if (ret) {
            AHAL_ERR("parse_compress_metadata Error (%x)", ret);
            goto error;
        }

        ret1 = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_DELAY_SAMPLES, value, sizeof(value));
        if (ret1 >= 0 ) {
            gaplessMeta.encoderDelay = atoi(value);
            AHAL_DBG("new encoder delay %u", gaplessMeta.encoderDelay);
        } else {
            gaplessMeta.encoderDelay = 0;
        }

        ret1 = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_PADDING_SAMPLES, value, sizeof(value));
        if (ret1 >= 0) {
            gaplessMeta.encoderPadding = atoi(value);
            AHAL_DBG("padding %u", gaplessMeta.encoderPadding);
        } else {
            gaplessMeta.encoderPadding = 0;
        }
    }

    ret1 = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HAC, value, sizeof(value));
    if (ret1 >= 0) {
        hac_voip = false;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_HAC_ON) == 0)
            hac_voip = true;
    }

error:
    AHAL_DBG("exit %d", ret);
    return ret;
}

int StreamOutPrimary::SetVolume(float left , float right) {
    int ret = 0;

    AHAL_DBG("Enter: left %f, right %f for usecase(%d: %s)", left, right, GetUseCase(), use_case_table[GetUseCase()]);

    stream_mutex_.lock();
    /* free previously cached volume if any */
    if (volume_) {
        free(volume_);
        volume_ = NULL;
    }

    if (left == right) {
        volume_ = (struct pal_volume_data *)malloc(sizeof(struct pal_volume_data)
                    +sizeof(struct pal_channel_vol_kv));
        if (!volume_) {
            AHAL_ERR("Failed to allocate mem for volume_");
            ret = -ENOMEM;
            goto done;
        }
        volume_->no_of_volpair = 1;
        volume_->volume_pair[0].channel_mask = 0x03;
        volume_->volume_pair[0].vol = left;
    } else {
        volume_ = (struct pal_volume_data *)malloc(sizeof(struct pal_volume_data)
                    +sizeof(struct pal_channel_vol_kv) * 2);
        if (!volume_) {
            AHAL_ERR("Failed to allocate mem for volume_");
            ret = -ENOMEM;
            goto done;
        }
        volume_->no_of_volpair = 2;
        volume_->volume_pair[0].channel_mask = 0x01;
        volume_->volume_pair[0].vol = left;
        volume_->volume_pair[1].channel_mask = 0x02;
        volume_->volume_pair[1].vol = right;
    }

    /* if stream is not opened already cache the volume and set on open */
    if (pal_stream_handle_) {
        ret = pal_stream_set_volume(pal_stream_handle_, volume_);
        if (ret) {
            AHAL_ERR("Pal Stream volume Error (%x)", ret);
        }
    }

done:
    stream_mutex_.unlock();
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

/* Delay in Us */
/* Delay in Us, only to be used for PCM formats */
int64_t StreamOutPrimary::GetRenderLatency(audio_output_flags_t halStreamFlags)
{
    struct pal_stream_attributes streamAttributes_;
    streamAttributes_.type = StreamOutPrimary::GetPalStreamType(halStreamFlags);
    AHAL_VERBOSE(" type %d",streamAttributes_.type);
    switch (streamAttributes_.type) {
         case PAL_STREAM_DEEP_BUFFER:
             return DEEP_BUFFER_PLATFORM_DELAY;
         case PAL_STREAM_LOW_LATENCY:
             return LOW_LATENCY_PLATFORM_DELAY;
         case PAL_STREAM_COMPRESSED:
         case PAL_STREAM_PCM_OFFLOAD:
              return PCM_OFFLOAD_PLATFORM_DELAY;
         case PAL_STREAM_ULTRA_LOW_LATENCY:
              return ULL_PLATFORM_DELAY;
         //TODO: Add more usecases/type as in current hal, once they are available in pal
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
    pal_param_bta2dp_t *param_bt_a2dp = NULL;
    size_t size = 0, kernel_buffer_size = 0;
    int32_t ret;

    stream_mutex_.lock();
    /* This adjustment accounts for buffering after app processor
     * It is based on estimated DSP latency per use case, rather than exact.
     */
    dsp_frames = StreamOutPrimary::GetRenderLatency(flags_) *
        (streamAttributes_.out_media_config.sample_rate) / 1000000LL;

    written_frames = mBytesWritten / audio_bytes_per_frame(
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
    if (isDeviceAvailable(PAL_DEVICE_OUT_BLUETOOTH_A2DP)) {
        ret = pal_get_param(PAL_PARAM_ID_BT_A2DP_ENCODER_LATENCY,
                            (void **)&param_bt_a2dp, &size, nullptr);
        if (!ret && param_bt_a2dp) {
            bt_extra_frames = param_bt_a2dp->latency *
                (streamAttributes_.out_media_config.sample_rate) / 1000;
            if (signed_frames >= bt_extra_frames)
                signed_frames -= bt_extra_frames;

        }
    }
    stream_mutex_.unlock();

    if (signed_frames <= 0) {
       signed_frames = 0;
       if (timestamp != NULL)
           clock_gettime(CLOCK_MONOTONIC, timestamp);
    } else if (timestamp != NULL) {
       *timestamp = writeAt;
    }

    AHAL_VERBOSE("signed frames %lld written frames %lld kernel frames %lld dsp frames %lld, bt extra frames %lld",
                 (long long)signed_frames, (long long)written_frames, (long long)kernel_frames,
                 (long long)dsp_frames, (long long)bt_extra_frames);

    return signed_frames;
}

int StreamOutPrimary::get_compressed_buffer_size()
{
    char value[PROPERTY_VALUE_MAX] = {0};
    int fragment_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    int fsize = 0;

    AHAL_DBG("config_ %x", config_.format);
    if(config_.format ==  AUDIO_FORMAT_FLAC ) {
        fragment_size = FLAC_COMPRESS_OFFLOAD_FRAGMENT_SIZE;
        AHAL_DBG("aud_fmt_id: 0x%x  FLAC buffer size:%d",
            streamAttributes_.out_media_config.aud_fmt_id,
            fragment_size);
    } else {
        fragment_size =  COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    }

    if((property_get("vendor.audio.offload.buffer.size.kb", value, "")) &&
            atoi(value)) {
        fsize = atoi(value) * 1024;
    }
    if (fsize > fragment_size)
        fragment_size = fsize;

    return fragment_size;
}

int StreamOutPrimary::get_pcm_buffer_size()
{
    uint8_t channels = audio_channel_count_from_out_mask(config_.channel_mask);
    uint8_t bytes_per_sample = audio_bytes_per_sample(config_.format);
    audio_format_t src_format = config_.format;
    audio_format_t dst_format = (audio_format_t)(getAlsaSupportedFmt.at(src_format));
    uint32_t hal_op_bytes_per_sample = audio_bytes_per_sample(dst_format);
    uint32_t hal_ip_bytes_per_sample = audio_bytes_per_sample(src_format);
    uint32_t fragment_size = 0;

    AHAL_DBG("config_ format:%x, SR %d ch_mask 0x%x, out format:%x",
            config_.format, config_.sample_rate,
            config_.channel_mask, dst_format);
    fragment_size = PCM_OFFLOAD_OUTPUT_PERIOD_DURATION *
        config_.sample_rate * bytes_per_sample * channels;
    fragment_size /= 1000;

    if (fragment_size < MIN_PCM_FRAGMENT_SIZE)
        fragment_size = MIN_PCM_FRAGMENT_SIZE;
    else if (fragment_size > MAX_PCM_FRAGMENT_SIZE)
        fragment_size = MAX_PCM_FRAGMENT_SIZE;

    fragment_size = ALIGN(fragment_size, (bytes_per_sample * channels * 32));

    if ((src_format != dst_format) &&
         hal_op_bytes_per_sample != hal_ip_bytes_per_sample) {

        fragment_size =
                  (fragment_size * hal_ip_bytes_per_sample) /
                   hal_op_bytes_per_sample;
        AHAL_INFO("enable conversion hal_input_fragment_size: src_format %x dst_format %x",
               src_format, dst_format);
    }

    AHAL_DBG("fragment size: %d", fragment_size);
    return fragment_size;
}

bool StreamOutPrimary:: period_size_is_plausible_for_low_latency(int period_size)
{
     switch (period_size) {
     case LL_PERIOD_SIZE_FRAMES_160:
     case LL_PERIOD_SIZE_FRAMES_192:
     case LL_PERIOD_SIZE_FRAMES_240:
     case LL_PERIOD_SIZE_FRAMES_320:
     case LL_PERIOD_SIZE_FRAMES_480:
         return true;
     default:
         return false;
     }
}

uint32_t StreamOutPrimary::GetBufferSizeForLowLatency() {
    int trial = 0;
    char value[PROPERTY_VALUE_MAX] = {0};
    int configured_low_latency_period_size = LOW_LATENCY_PLAYBACK_PERIOD_SIZE;

    if (property_get("vendor.audio_hal.period_size", value, NULL) > 0) {
        trial = atoi(value);
        if (period_size_is_plausible_for_low_latency(trial))
            configured_low_latency_period_size = trial;
    }

    return configured_low_latency_period_size *
           audio_bytes_per_frame(
                    audio_channel_count_from_out_mask(config_.channel_mask),
                    config_.format);
}

uint32_t StreamOutPrimary::GetBufferSize() {
    struct pal_stream_attributes streamAttributes_;

    streamAttributes_.type = StreamOutPrimary::GetPalStreamType(flags_);
    AHAL_DBG("type %d", streamAttributes_.type);
    if (streamAttributes_.type == PAL_STREAM_VOIP_RX) {
        return (DEFAULT_VOIP_BUF_DURATION_MS * config_.sample_rate / 1000) *
               audio_bytes_per_frame(
                       audio_channel_count_from_out_mask(config_.channel_mask),
                       config_.format);
    } else if (streamAttributes_.type == PAL_STREAM_COMPRESSED) {
        return get_compressed_buffer_size();
    } else if (streamAttributes_.type == PAL_STREAM_PCM_OFFLOAD) {
        return get_pcm_buffer_size();
    } else if (streamAttributes_.type == PAL_STREAM_LOW_LATENCY) {
        return GetBufferSizeForLowLatency();
    } else if (streamAttributes_.type == PAL_STREAM_ULTRA_LOW_LATENCY) {
        return ULL_PERIOD_SIZE * ULL_PERIOD_MULTIPLIER *
            audio_bytes_per_frame(
                    audio_channel_count_from_out_mask(config_.channel_mask),
                    config_.format);
    } else if (streamAttributes_.type == PAL_STREAM_DEEP_BUFFER){
        return DEEP_BUFFER_PLAYBACK_PERIOD_SIZE *
            audio_bytes_per_frame(
                    audio_channel_count_from_out_mask(config_.channel_mask),
                    config_.format);
    } else {
       return BUF_SIZE_PLAYBACK * NO_OF_BUF;
    }
}

int StreamOutPrimary::Open() {
    int ret = -EINVAL;
    uint8_t channels = 0;
    struct pal_channel_info ch_info = {0, {0}};
    uint32_t outBufSize = 0;
    uint32_t outBufCount = NO_OF_BUF;
    struct pal_buffer_config outBufCfg = {0, 0, 0};

    pal_param_device_capability_t *device_cap_query = NULL;
    size_t payload_size = 0;
    dynamic_media_config_t dynamic_media_config;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    bool isHifiFilterEnabled = false;
    bool *payload_hifiFilter = &isHifiFilterEnabled;
    size_t param_size = 0;
    mBypassHaptic = property_get_bool("vendor.audio.gaming.enabled", false);

    AHAL_INFO("Enter: OutPrimary usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);

    if (!mInitialized) {
        AHAL_ERR("Not initialized, returning error");
        goto error_open;
    }
    AHAL_DBG("no_of_devices %zu", mAndroidOutDevices.size());
    //need to convert channel mask to pal channel mask
    // Stream channel mask
    channels = audio_channel_count_from_out_mask(config_.channel_mask);

    if (usecase_ == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
        channels = audio_channel_count_from_out_mask(config_.channel_mask & ~AUDIO_CHANNEL_HAPTIC_ALL);
    }
    ch_info.channels = channels;
    ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
    if (ch_info.channels > 1)
        ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;

    streamAttributes_.type = StreamOutPrimary::GetPalStreamType(flags_);
    streamAttributes_.flags = (pal_stream_flags_t)0;
    streamAttributes_.direction = PAL_AUDIO_OUTPUT;
    streamAttributes_.out_media_config.sample_rate = config_.sample_rate;
    streamAttributes_.out_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    streamAttributes_.out_media_config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;
    streamAttributes_.out_media_config.ch_info = ch_info;

    switch(streamAttributes_.type) {
        case PAL_STREAM_COMPRESSED:
            streamAttributes_.flags = (pal_stream_flags_t)(PAL_STREAM_FLAG_NON_BLOCKING);
            if (config_.offload_info.format == 0)
                config_.offload_info.format = config_.format;
            if (config_.offload_info.sample_rate == 0)
                config_.offload_info.sample_rate = config_.sample_rate;
                streamAttributes_.out_media_config.sample_rate = config_.offload_info.sample_rate;
            if (msample_rate)
                streamAttributes_.out_media_config.sample_rate = msample_rate;
            if (mchannels)
                streamAttributes_.out_media_config.ch_info.channels = mchannels;
            if (getAlsaSupportedFmt.find(config_.format) != getAlsaSupportedFmt.end()) {
                halInputFormat = config_.format;
                halOutputFormat = (audio_format_t)(getAlsaSupportedFmt.at(config_.format));
                streamAttributes_.out_media_config.aud_fmt_id = getFormatId.at(halOutputFormat);
                streamAttributes_.out_media_config.bit_width = format_to_bitwidth_table[halOutputFormat];
                if (streamAttributes_.out_media_config.bit_width == 0)
                    streamAttributes_.out_media_config.bit_width = 16;
                streamAttributes_.type = PAL_STREAM_PCM_OFFLOAD;
            } else {
                streamAttributes_.out_media_config.aud_fmt_id = getFormatId.at(config_.format & AUDIO_FORMAT_MAIN_MASK);
            }
            break;
        case PAL_STREAM_LOW_LATENCY:
        case PAL_STREAM_ULTRA_LOW_LATENCY:
        case PAL_STREAM_DEEP_BUFFER:
        case PAL_STREAM_GENERIC:
        case PAL_STREAM_PCM_OFFLOAD:
            halInputFormat = config_.format;
            halOutputFormat = (audio_format_t)(getAlsaSupportedFmt.at(halInputFormat));
            streamAttributes_.out_media_config.aud_fmt_id = getFormatId.at(halOutputFormat);
            streamAttributes_.out_media_config.bit_width = format_to_bitwidth_table[halOutputFormat];
            AHAL_DBG("halInputFormat %d halOutputFormat %d palformat %d", halInputFormat,
                     halOutputFormat, streamAttributes_.out_media_config.aud_fmt_id);
            if (streamAttributes_.out_media_config.bit_width == 0)
                streamAttributes_.out_media_config.bit_width = 16;

            if (streamAttributes_.type == PAL_STREAM_ULTRA_LOW_LATENCY) {
                if (usecase_ == USECASE_AUDIO_PLAYBACK_MMAP) {
                    streamAttributes_.flags = (pal_stream_flags_t)(PAL_STREAM_FLAG_MMAP_NO_IRQ);
                } else if (usecase_ == USECASE_AUDIO_PLAYBACK_ULL) {
                    streamAttributes_.flags = (pal_stream_flags_t)(PAL_STREAM_FLAG_MMAP);
                }
            }
            break;
        default:
            break;
    }

    ret = pal_get_param(PAL_PARAM_ID_HIFI_PCM_FILTER,
                        (void **)&payload_hifiFilter, &param_size, nullptr);

    if (!ret && isHifiFilterEnabled &&
        (mPalOutDevice->id == PAL_DEVICE_OUT_WIRED_HEADSET ||
         mPalOutDevice->id == PAL_DEVICE_OUT_WIRED_HEADPHONE) &&
        (streamAttributes_.out_media_config.sample_rate != 384000 &&
         streamAttributes_.out_media_config.sample_rate != 352800)) {

        AHAL_DBG("hifi-filter custom key sent to PAL (only applicable to certain streams)\n");

        strlcpy(mPalOutDevice->custom_config.custom_key,
                "hifi-filter_custom_key",
                sizeof(mPalOutDevice->custom_config.custom_key));
    }

    device_cap_query = (pal_param_device_capability_t *)malloc(sizeof(pal_param_device_capability_t));

    if ((mPalOutDevice->id == PAL_DEVICE_OUT_USB_DEVICE || mPalOutDevice->id ==
        PAL_DEVICE_OUT_USB_HEADSET) && device_cap_query && adevice) {

        device_cap_query->id = mPalOutDevice->id;
        device_cap_query->addr.card_id = adevice->usb_card_id_;
        device_cap_query->addr.device_num = adevice->usb_dev_num_;
        device_cap_query->config = &dynamic_media_config;
        device_cap_query->is_playback = true;
        ret = pal_get_param(PAL_PARAM_ID_DEVICE_CAPABILITY,(void **)&device_cap_query,
                &payload_size, nullptr);

        if (ret<0) {
            AHAL_DBG("Error usb device is not connected");
            ret = -ENOSYS;
            goto error_open;
        }
    }

    if (hac_voip && (mPalOutDevice->id == PAL_DEVICE_OUT_HANDSET)) {
        strlcpy(mPalOutDevice->custom_config.custom_key, "HAC",
                sizeof(mPalOutDevice->custom_config.custom_key));
    }

    AHAL_DBG("channels %d samplerate %d format id %d, stream type %d  stream bitwidth %d",
           streamAttributes_.out_media_config.ch_info.channels, streamAttributes_.out_media_config.sample_rate,
           streamAttributes_.out_media_config.aud_fmt_id, streamAttributes_.type,
           streamAttributes_.out_media_config.bit_width);
    AHAL_DBG("msample_rate %d mchannels %d mNoOfOutDevices %zu", msample_rate, mchannels, mAndroidOutDevices.size());
    ret = pal_stream_open(&streamAttributes_,
                          mAndroidOutDevices.size(),
                          mPalOutDevice,
                          0,
                          NULL,
                          &pal_callback,
                          (uint64_t)this,
                          &pal_stream_handle_);

    if (ret) {
        AHAL_ERR("Pal Stream Open Error (%x)", ret);
        ret = -EINVAL;
        goto error_open;
    }
    if (usecase_ == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
        ch_info.channels = audio_channel_count_from_out_mask(config_.channel_mask & AUDIO_CHANNEL_HAPTIC_ALL);
        ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
        if (ch_info.channels > 1)
            ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;

        hapticsStreamAttributes.type = PAL_STREAM_HAPTICS;
        hapticsStreamAttributes.flags = (pal_stream_flags_t)0;
        hapticsStreamAttributes.direction = PAL_AUDIO_OUTPUT;
        hapticsStreamAttributes.out_media_config.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        hapticsStreamAttributes.out_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        hapticsStreamAttributes.out_media_config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;
        hapticsStreamAttributes.out_media_config.ch_info = ch_info;

        if (!hapticsDevice && !mBypassHaptic) {
            hapticsDevice = (struct pal_device*) calloc(1, sizeof(struct pal_device));
        }

        if (hapticsDevice && !mBypassHaptic) {
            hapticsDevice->id = PAL_DEVICE_OUT_HAPTICS_DEVICE;
            hapticsDevice->config.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
            hapticsDevice->config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
            hapticsDevice->config.ch_info = ch_info;
            hapticsDevice->config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;

            ret = pal_stream_open (&hapticsStreamAttributes,
                                   1,
                                   hapticsDevice,
                                   0,
                                   NULL,
                                   &pal_callback,
                                   (uint64_t)this,
                                   &pal_haptics_stream_handle);
            if (ret)
                AHAL_ERR("Pal Haptics Stream Open Error (%x)", ret);
        } else {
            AHAL_ERR("Failed to allocate memory for hapticsDevice");
        }
    }
    if (karaoke) {
        ret = AudExtn.karaoke_open(mPalOutDevice[mAndroidOutDevices.size()-1].id, &pal_callback, ch_info);
        if (ret) {
            AHAL_ERR("Karaoke Open Error (%x)", ret);
            karaoke = false;
        }
    }

    //TODO: Remove below code, once pal_stream_open is moved to
    //adev_open_output_stream
    if (streamAttributes_.type == PAL_STREAM_COMPRESSED) {
        pal_param_payload *param_payload = nullptr;
        param_payload = (pal_param_payload *) calloc (1,
                                              sizeof(pal_param_payload) +
                                              sizeof(pal_snd_dec_t));

        if (!param_payload) {
            AHAL_ERR("calloc failed for size %zu",
                   sizeof(pal_param_payload) + sizeof(pal_snd_dec_t));
        } else {
            param_payload->payload_size = sizeof(pal_snd_dec_t);
            memcpy(param_payload->payload, &palSndDec, param_payload->payload_size);

            ret = pal_stream_set_param(pal_stream_handle_,
                                       PAL_PARAM_ID_CODEC_CONFIGURATION,
                                       param_payload);
            if (ret)
                AHAL_ERR("Pal Set Param Error (%x)", ret);
            free(param_payload);
        }
        isCompressMetadataAvail = false;
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
    } else if (usecase_ == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
        outBufSize = LOW_LATENCY_PLAYBACK_PERIOD_SIZE * audio_bytes_per_frame(
                    channels,
                    config_.format);
        outBufCount = LOW_LATENCY_PLAYBACK_PERIOD_COUNT;
    } else
        outBufSize = StreamOutPrimary::GetBufferSize();

    if (usecase_ == USECASE_AUDIO_PLAYBACK_LOW_LATENCY)
        outBufCount = LOW_LATENCY_PLAYBACK_PERIOD_COUNT;
    else if (usecase_ == USECASE_AUDIO_PLAYBACK_OFFLOAD2)
        outBufCount = PCM_OFFLOAD_PLAYBACK_PERIOD_COUNT;
    else if (usecase_ == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER)
        outBufCount = DEEP_BUFFER_PLAYBACK_PERIOD_COUNT;
    else if (usecase_ == USECASE_AUDIO_PLAYBACK_VOIP)
        outBufCount = VOIP_PERIOD_COUNT_DEFAULT;

    if (halInputFormat != halOutputFormat) {
        convertBuffer = realloc(convertBuffer, outBufSize);
        if (!convertBuffer) {
            ret = -ENOMEM;
            AHAL_ERR("convert Buffer allocation failed. ret %d", ret);
            goto error_open;
        }
        AHAL_DBG("convert buffer allocated for size %d", convertBufSize);
    }

    fragment_size_ = outBufSize;
    fragments_ = outBufCount;

    AHAL_DBG("fragment_size_ %d fragments_ %d", fragment_size_, fragments_);
    outBufCfg.buf_size = fragment_size_;
    outBufCfg.buf_count = fragments_;
    ret = pal_stream_set_buffer_size(pal_stream_handle_, NULL, &outBufCfg);
    if (ret) {
        AHAL_ERR("Pal Stream set buffer size Error  (%x)", ret);
    }
    if (usecase_ == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS &&
        pal_haptics_stream_handle) {
        outBufSize = LOW_LATENCY_PLAYBACK_PERIOD_SIZE * audio_bytes_per_frame(
                    hapticsStreamAttributes.out_media_config.ch_info.channels,
                    config_.format);
        outBufCount = LOW_LATENCY_PLAYBACK_PERIOD_COUNT;

        fragment_size_ += outBufSize;
        AHAL_DBG("fragment_size_ %d fragments_ %d", fragment_size_, fragments_);
        outBufCfg.buf_size = outBufSize;
        outBufCfg.buf_count = fragments_;

        ret = pal_stream_set_buffer_size(pal_haptics_stream_handle, NULL, &outBufCfg);
        if (ret) {
            AHAL_ERR("Pal Stream set buffer size Error  (%x)", ret);
        }
    }

error_open:
    if (device_cap_query) {
        free(device_cap_query);
        device_cap_query = NULL;
    }
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}


int StreamOutPrimary::GetFrames(uint64_t *frames)
{
    int ret = 0;
    pal_session_time tstamp;
    uint64_t timestamp = 0;
    uint64_t dsp_frames = 0;
    uint64_t offset = 0;
    size_t size = 0;
    pal_param_bta2dp_t *param_bt_a2dp = NULL;

    if (!pal_stream_handle_) {
        AHAL_VERBOSE("pal_stream_handle_ NULL");
        *frames = 0;
        return 0;
    }
    if (!stream_started_) {
        AHAL_VERBOSE("stream not in started state");
        *frames = 0;
        return 0;
    }

    ret = pal_get_timestamp(pal_stream_handle_, &tstamp);
    if (ret != 0) {
       AHAL_ERR("pal_get_timestamp failed %d", ret);
       goto exit;
    }
    timestamp = (uint64_t)tstamp.session_time.value_msw;
    timestamp = timestamp  << 32 | tstamp.session_time.value_lsw;
    AHAL_VERBOSE("session msw %u", tstamp.session_time.value_msw);
    AHAL_VERBOSE("session lsw %u", tstamp.session_time.value_lsw);
    AHAL_VERBOSE("session timespec %lld", ((long long) timestamp));
    timestamp *= (streamAttributes_.out_media_config.sample_rate);
    AHAL_VERBOSE("timestamp %lld", ((long long) timestamp));
    dsp_frames = timestamp/1000000;

    // Adjustment accounts for A2dp encoder latency with offload usecases
    // Note: Encoder latency is returned in ms.
    if (isDeviceAvailable(PAL_DEVICE_OUT_BLUETOOTH_A2DP)) {
        ret = pal_get_param(PAL_PARAM_ID_BT_A2DP_ENCODER_LATENCY,
                            (void **)&param_bt_a2dp, &size, nullptr);
        if (!ret && param_bt_a2dp) {
            offset = param_bt_a2dp->latency *
                (streamAttributes_.out_media_config.sample_rate) / 1000;
            dsp_frames = (dsp_frames > offset) ? (dsp_frames - offset) : 0;
        }
    }
    *frames = dsp_frames + mCachedPosition;
exit:
    return ret;
}

int StreamOutPrimary::GetOutputUseCase(audio_output_flags_t halStreamFlags)
{
    // TODO: just covered current supported usecases in PAL
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
    else if (config_.channel_mask & AUDIO_CHANNEL_HAPTIC_ALL)
        usecase = USECASE_AUDIO_PLAYBACK_WITH_HAPTICS;

    return usecase;
}

ssize_t StreamOutPrimary::splitAndWriteAudioHapticsStream(const void *buffer, size_t bytes)
{
     ssize_t ret = 0;
     bool allocHapticsBuffer = false;
     struct pal_buffer audioBuf;
     struct pal_buffer hapticBuf;
     size_t srcIndex = 0, audIndex = 0, hapIndex = 0;
     uint8_t channelCount = audio_channel_count_from_out_mask(config_.channel_mask);
     uint8_t bytesPerSample = audio_bytes_per_sample(config_.format);
     uint32_t frameSize = channelCount * bytesPerSample;
     uint32_t frameCount = bytes / frameSize;

     // Calculate Haptics Buffer size
     uint8_t hapticsChannelCount = hapticsStreamAttributes.out_media_config.ch_info.channels;
     uint32_t hapticsFrameSize = bytesPerSample * hapticsChannelCount;
     uint32_t audioFrameSize = frameSize - hapticsFrameSize;
     uint32_t totalHapticsBufferSize = frameCount * hapticsFrameSize;

     if (!hapticBuffer) {
         allocHapticsBuffer = true;
     } else if (hapticsBufSize < totalHapticsBufferSize) {
         if (hapticBuffer)
             free (hapticBuffer);
         allocHapticsBuffer = true;
         hapticsBufSize = 0;
     }

     if (allocHapticsBuffer) {
         hapticBuffer = (uint8_t *)calloc(1, totalHapticsBufferSize);
         if(!hapticBuffer) {
             AHAL_ERR("Failed to allocate mem for haptic buffer");
             return -ENOMEM;
         }
         hapticsBufSize = totalHapticsBufferSize;
     }

     audioBuf.buffer = (uint8_t *)buffer;
     audioBuf.size = frameCount * audioFrameSize;
     audioBuf.offset = 0;
     hapticBuf.buffer  = hapticBuffer;
     hapticBuf.size = frameCount * hapticsFrameSize;
     hapticBuf.offset = 0;

     for (size_t i = 0; i < frameCount; i++) {
         memcpy((uint8_t *)(audioBuf.buffer) + audIndex, (uint8_t *)(audioBuf.buffer) + srcIndex,
                audioFrameSize);
         audIndex += audioFrameSize;
         srcIndex += audioFrameSize;

         memcpy((uint8_t *)(hapticBuf.buffer) + hapIndex, (uint8_t *)(audioBuf.buffer) + srcIndex,
                    hapticsFrameSize);
         hapIndex += hapticsFrameSize;
         srcIndex += hapticsFrameSize;
     }

     // write audio data
     ret = pal_stream_write(pal_stream_handle_, &audioBuf);
     // write haptics data
     ret = pal_stream_write(pal_haptics_stream_handle, &hapticBuf);

     return (ret < 0 ? ret : bytes);
}

ssize_t StreamOutPrimary::BypassHapticAndWriteAudioStream(const void *buffer, size_t bytes)
{
     ssize_t ret = 0;
     bool allocHapticsBuffer = false;
     struct pal_buffer audioBuf;
     size_t srcIndex = 0, audIndex = 0, hapIndex = 0;
     uint8_t channelCount = audio_channel_count_from_out_mask(config_.channel_mask);
     uint8_t bytesPerSample = audio_bytes_per_sample(config_.format);
     uint32_t frameSize = channelCount * bytesPerSample;
     uint32_t frameCount = bytes / frameSize;

     // Calculate Haptics Buffer size
     uint8_t hapticsChannelCount = hapticsStreamAttributes.out_media_config.ch_info.channels;
     uint32_t hapticsFrameSize = bytesPerSample * hapticsChannelCount;
     uint32_t audioFrameSize = frameSize - hapticsFrameSize;
     uint32_t totalHapticsBufferSize = frameCount * hapticsFrameSize;

     audioBuf.buffer = (uint8_t *)buffer;
     audioBuf.size = frameCount * audioFrameSize;
     audioBuf.offset = 0;

     for (size_t i = 0; i < frameCount; i++) {
         memcpy((uint8_t *)(audioBuf.buffer) + audIndex, (uint8_t *)(audioBuf.buffer) + srcIndex,
                audioFrameSize);
         audIndex += audioFrameSize;
         srcIndex += audioFrameSize;

         // Skip haptic frames
         hapIndex += hapticsFrameSize;
         srcIndex += hapticsFrameSize;
     }

     // write audio data
     ret = pal_stream_write(pal_stream_handle_, &audioBuf);

     return (ret < 0 ? ret : bytes);
}

ssize_t StreamOutPrimary::onWriteError(size_t bytes, ssize_t ret) {
    // standby streams upon write failures and sleep for buffer duration.
    AHAL_ERR("write error %d usecase(%d: %s)", ret, GetUseCase(), use_case_table[GetUseCase()]);
    Standby();

    if (streamAttributes_.type != PAL_STREAM_COMPRESSED) {
        uint32_t byteWidth = streamAttributes_.out_media_config.bit_width / 8;
        uint32_t sampleRate = streamAttributes_.out_media_config.sample_rate;
        uint32_t channelCount = streamAttributes_.out_media_config.ch_info.channels;
        uint32_t frameSize = byteWidth * channelCount;

        if (frameSize == 0 || sampleRate == 0) {
            AHAL_ERR("invalid frameSize=%d, sampleRate=%d", frameSize, sampleRate);
            return -EINVAL;
        } else {
            usleep((uint64_t)bytes * 1000000 / frameSize / sampleRate);
            return bytes;
        }
    }
    // Return error in case of compress offload.
    return ret;
}

ssize_t StreamOutPrimary::configurePalOutputStream() {
    ssize_t ret = 0;
    if (!pal_stream_handle_) {
        AutoPerfLock perfLock;
        ATRACE_BEGIN("hal:open_output");
        ret = Open();
        ATRACE_END();
        if (ret) {
            AHAL_ERR("failed to open stream.");
            return -EINVAL;
        }
    }

    if (!stream_started_) {
        AutoPerfLock perfLock;
        /* set cached volume if any, dont return failure back up */
        if (volume_) {
            ret = pal_stream_set_volume(pal_stream_handle_, volume_);
            if (ret) {
                AHAL_ERR("Pal Stream volume Error (%x)", ret);
            }
        }

        ATRACE_BEGIN("hal: pal_stream_start");
        ret = pal_stream_start(pal_stream_handle_);
        if (ret) {
            AHAL_ERR("failed to start stream. ret=%d", ret);
            pal_stream_close(pal_stream_handle_);
            pal_stream_handle_ = NULL;
            ATRACE_END();
            if (usecase_ == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS &&
                pal_haptics_stream_handle) {
                AHAL_DBG("Close haptics stream");
                pal_stream_close(pal_haptics_stream_handle);
                pal_haptics_stream_handle = NULL;
            }
            return -EINVAL;
        } else {
            AHAL_INFO("notify GEF client of device config");
            for(auto dev : mAndroidOutDevices)
                audio_extn_gef_notify_device_config(dev, config_.channel_mask,
                    config_.sample_rate, flags_);
        }

        if (usecase_ == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS && pal_haptics_stream_handle) {
            ret = pal_stream_start(pal_haptics_stream_handle);
            if (ret) {
                AHAL_ERR("failed to start haptics stream. ret=%d", ret);
                ATRACE_END();
                pal_stream_close(pal_haptics_stream_handle);
                pal_haptics_stream_handle = NULL;
                return -EINVAL;
            }
        }
        if (karaoke) {
            ret = AudExtn.karaoke_start();
            if (ret) {
                AHAL_ERR("failed to start karaoke stream. ret=%d", ret);
                AudExtn.karaoke_close();
                karaoke = false;
                ret = 0; // Not fatal error
            }
        }
        stream_started_ = true;

        if (CheckOffloadEffectsType(streamAttributes_.type)) {
            ret = StartOffloadEffects(handle_, pal_stream_handle_);
            ret = StartOffloadVisualizer(handle_, pal_stream_handle_);
        }
        ATRACE_END();
    }
    if ((streamAttributes_.type == PAL_STREAM_COMPRESSED) && isCompressMetadataAvail) {
        // Send codec params first.
        pal_param_payload *param_payload = nullptr;
        param_payload = (pal_param_payload *) calloc (1,
                                              sizeof(pal_param_payload) +
                                              sizeof(pal_snd_dec_t));

        if (param_payload) {
            param_payload->payload_size = sizeof(pal_snd_dec_t);
            memcpy(param_payload->payload, &palSndDec, param_payload->payload_size);

            ret = pal_stream_set_param(pal_stream_handle_,
                                       PAL_PARAM_ID_CODEC_CONFIGURATION,
                                       param_payload);
            if (ret) {
                AHAL_INFO("Pal Set Param for codec configuration failed (%x)", ret);
                ret = 0;
            }
            free(param_payload);

        } else {
            AHAL_ERR("calloc failed for size %zu",
                   sizeof(pal_param_payload) + sizeof(pal_snd_dec_t));
        }
        isCompressMetadataAvail = false;
    }

    if ((streamAttributes_.type == PAL_STREAM_COMPRESSED) && sendGaplessMetadata) {
        //Send gapless metadata
        pal_param_payload *param_payload = nullptr;
        param_payload = (pal_param_payload *) calloc (1,
                                          sizeof(pal_param_payload) +
                                          sizeof(struct pal_compr_gapless_mdata));
        if (param_payload) {
            AHAL_DBG("sending gapless metadata");
            param_payload->payload_size = sizeof(struct pal_compr_gapless_mdata);
            memcpy(param_payload->payload, &gaplessMeta, param_payload->payload_size);

            ret = pal_stream_set_param(pal_stream_handle_,
                                       PAL_PARAM_ID_GAPLESS_MDATA,
                                       param_payload);
            if (ret) {
                AHAL_INFO("PAL set param for gapless failed, error (%x)", ret);
                ret = 0;
            }
            free(param_payload);
        } else {
            AHAL_ERR("Failed to allocate gapless payload");
        }
        sendGaplessMetadata = false;
    }
    return 0;
}

ssize_t StreamOutPrimary::write(const void *buffer, size_t bytes)
{
    ssize_t ret = 0;
    struct pal_buffer palBuffer;
    uint32_t frames;

    palBuffer.buffer = (uint8_t*)buffer;
    palBuffer.size = bytes;
    palBuffer.offset = 0;

    AHAL_VERBOSE("handle_ %x bytes:(%zu)", handle_, bytes);

    stream_mutex_.lock();
    ret = configurePalOutputStream();
    if (ret < 0)
        goto exit;
    ATRACE_BEGIN("hal: pal_stream_write");
    if (halInputFormat != halOutputFormat && convertBuffer != NULL) {
        if (bytes > fragment_size_) {
            AHAL_ERR("Error written bytes %zu > %d (fragment_size)", bytes, fragment_size_);
            ATRACE_END();
            stream_mutex_.unlock();
            return -EINVAL;
        }
        /* prevent division-by-zero */
        uint32_t inputBitWidth = format_to_bitwidth_table[halInputFormat];
        uint32_t outputBitWidth = format_to_bitwidth_table[halOutputFormat];

        if (inputBitWidth == 0 || outputBitWidth == 0) {
            AHAL_ERR("Error inputBitWidth %u, outputBitWidth %u", inputBitWidth, outputBitWidth);
            ATRACE_END();
            stream_mutex_.unlock();
            return -EINVAL;
        }

        frames = bytes / (inputBitWidth / 8);
        memcpy_by_audio_format(convertBuffer, halOutputFormat, buffer, halInputFormat, frames);
        palBuffer.buffer = (uint8_t *)convertBuffer;
        palBuffer.size = frames * (outputBitWidth / 8);
        ret = pal_stream_write(pal_stream_handle_, &palBuffer);
        if (ret >= 0) {
            ret = (ret * inputBitWidth) / outputBitWidth;
        }
    } else if (usecase_ == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
        if (mBypassHaptic)
            ret = BypassHapticAndWriteAudioStream(buffer, bytes);
        else if (pal_haptics_stream_handle)
            ret = splitAndWriteAudioHapticsStream(buffer, bytes);
    } else {
        ret = pal_stream_write(pal_stream_handle_, &palBuffer);
    }
    ATRACE_END();

exit:
    if (mBytesWritten <= UINT64_MAX - bytes) {
        mBytesWritten += bytes;
    } else {
        mBytesWritten = UINT64_MAX;
    }
    stream_mutex_.unlock();
    clock_gettime(CLOCK_MONOTONIC, &writeAt);

    return (ret < 0 ? onWriteError(bytes, ret) : ret);
}

bool StreamOutPrimary::CheckOffloadEffectsType(pal_stream_type_t pal_stream_type) {
    if (pal_stream_type == PAL_STREAM_COMPRESSED  ||
        pal_stream_type == PAL_STREAM_PCM_OFFLOAD) {
        return true;
    }

    return false;
}

int StreamOutPrimary::StartOffloadEffects(
                                    audio_io_handle_t ioHandle,
                                    pal_stream_handle_t* pal_stream_handle) {
    int ret  = 0;
    if (fnp_offload_effect_start_output_) {
        ret = fnp_offload_effect_start_output_(ioHandle, pal_stream_handle);
        if (ret) {
            AHAL_ERR("failed to start offload effect.");
        }
    } else {
        AHAL_ERR("error function pointer is null.");
        return -EINVAL;
    }

    return ret;
}

int StreamOutPrimary::StopOffloadEffects(
                                    audio_io_handle_t ioHandle,
                                    pal_stream_handle_t* pal_stream_handle) {
    int ret  = 0;
    if (fnp_offload_effect_stop_output_) {
        ret = fnp_offload_effect_stop_output_(ioHandle, pal_stream_handle);
        if (ret) {
            AHAL_ERR("failed to stop offload effect.\n");
        }
    } else {
        AHAL_ERR("error function pointer is null.");
        return -EINVAL;
    }

    return ret;
}


int StreamOutPrimary::StartOffloadVisualizer(
                                    audio_io_handle_t ioHandle,
                                    pal_stream_handle_t* pal_stream_handle) {
    int ret  = 0;
    if (fnp_visualizer_start_output_) {
        ret = fnp_visualizer_start_output_(ioHandle, pal_stream_handle);
        if (ret) {
            AHAL_ERR("failed to visualizer_start.");
        }
    } else {
        AHAL_ERR("function pointer is null.");
        return -EINVAL;
    }

    return ret;
}

int StreamOutPrimary::StopOffloadVisualizer(
                                    audio_io_handle_t ioHandle,
                                    pal_stream_handle_t* pal_stream_handle) {
    int ret  = 0;
    if (fnp_visualizer_stop_output_) {
        ret = fnp_visualizer_stop_output_(ioHandle, pal_stream_handle);
        if (ret) {
            AHAL_ERR("failed to visualizer_stop.\n");
        }
    } else {
        AHAL_ERR("function pointer is null.");
        return -EINVAL;
    }

    return ret;
}

StreamOutPrimary::StreamOutPrimary(
                        audio_io_handle_t handle,
                        const std::set<audio_devices_t> &devices,
                        audio_output_flags_t flags,
                        struct audio_config *config,
                        const char *address __unused,
                        offload_effects_start_output start_offload_effect,
                        offload_effects_stop_output stop_offload_effect,
                        visualizer_hal_start_output visualizer_start_output,
                        visualizer_hal_stop_output visualizer_stop_output):
    StreamPrimary(handle, devices, config),
    mAndroidOutDevices(devices),
    flags_(flags)
{
    stream_ = std::shared_ptr<audio_stream_out> (new audio_stream_out());
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    mInitialized = false;
    pal_stream_handle_ = nullptr;
    pal_haptics_stream_handle = nullptr;
    mPalOutDeviceIds = nullptr;
    mPalOutDevice = nullptr;
    convertBuffer = NULL;
    hapticsDevice = NULL;
    hapticBuffer = NULL;
    hapticsBufSize = 0;
    writeAt.tv_sec = 0;
    writeAt.tv_nsec = 0;
    mBytesWritten = 0;
    int noPalDevices = 0;
    int ret = 0;
    /*Initialize the gaplessMeta value with 0*/
    memset(&gaplessMeta,0,sizeof(struct pal_compr_gapless_mdata));

    if (!stream_) {
        AHAL_ERR("No memory allocated for stream_");
        throw std::runtime_error("No memory allocated for stream_");
    }
    AHAL_DBG("enter: handle (%x) format(%#x) sample_rate(%d) channel_mask(%#x) devices(%zu) flags(%#x)\
          address(%s)", handle, config->format, config->sample_rate, config->channel_mask,
          mAndroidOutDevices.size(), flags, address);

    //TODO: check if USB device is connected or not
    if (AudioExtn::audio_devices_cmp(mAndroidOutDevices, audio_is_usb_out_device)){
        // get capability from device of USB
        device_cap_query_ = (pal_param_device_capability_t *)
                                calloc(1, sizeof(pal_param_device_capability_t));
        if (!device_cap_query_) {
            AHAL_ERR("Failed to allocate mem for device_cap_query_");
            goto error;
        }
        dynamic_media_config_t *dynamic_media_config = (dynamic_media_config_t *)
                                                  calloc(1, sizeof(dynamic_media_config_t));
        if (!dynamic_media_config) {
            free(device_cap_query_);
            AHAL_ERR("Failed to allocate mem for dynamic_media_config");
            goto error;
        }
        size_t payload_size = 0;
        device_cap_query_->id = PAL_DEVICE_OUT_USB_DEVICE;
        device_cap_query_->addr.card_id = adevice->usb_card_id_;
        device_cap_query_->addr.device_num = adevice->usb_dev_num_;
        device_cap_query_->config = dynamic_media_config;
        device_cap_query_->is_playback = true;
        ret = pal_get_param(PAL_PARAM_ID_DEVICE_CAPABILITY,
                            (void **)&device_cap_query_,
                            &payload_size, nullptr);
        if (ret < 0) {
            AHAL_ERR("Error usb device is not connected");
            free(dynamic_media_config);
            free(device_cap_query_);
            dynamic_media_config = NULL;
            device_cap_query_ = NULL;
        }
        if (!config->sample_rate || !config->format || !config->channel_mask) {
            if (dynamic_media_config) {
                config->sample_rate = dynamic_media_config->sample_rate[0];
                config->channel_mask = (audio_channel_mask_t) dynamic_media_config->mask[0];
                config->format = (audio_format_t)dynamic_media_config->format[0];
            }
            if (config->sample_rate == 0)
                config->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
            if (config->channel_mask == AUDIO_CHANNEL_NONE)
                config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
            if (config->format == AUDIO_FORMAT_DEFAULT)
                config->format = AUDIO_FORMAT_PCM_16_BIT;
            memcpy(&config_, config, sizeof(struct audio_config));
            AHAL_INFO("sample rate = %#x channel_mask=%#x fmt=%#x",
                      config->sample_rate, config->channel_mask,
                      config->format);

        }
    }

    if (AudioExtn::audio_devices_cmp(mAndroidOutDevices, AUDIO_DEVICE_OUT_AUX_DIGITAL)){
        AHAL_DBG("AUDIO_DEVICE_OUT_AUX_DIGITAL and DIRECT | OFFLOAD, check hdmi caps");
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
        AHAL_DBG("invalid address");
    }

    fnp_offload_effect_start_output_ = start_offload_effect;
    fnp_offload_effect_stop_output_ = stop_offload_effect;

    fnp_visualizer_start_output_ = visualizer_start_output;
    fnp_visualizer_stop_output_ = visualizer_stop_output;

    if (mAndroidOutDevices.empty())
        mAndroidOutDevices.insert(AUDIO_DEVICE_OUT_DEFAULT);
    AHAL_DBG("No of Android devices %zu", mAndroidOutDevices.size());

    mPalOutDeviceIds = (pal_device_id_t*) calloc(mAndroidOutDevices.size(), sizeof(pal_device_id_t));
    if (!mPalOutDeviceIds) {
           goto error;
    }

    noPalDevices = getPalDeviceIds(mAndroidOutDevices, mPalOutDeviceIds);
    if (noPalDevices != mAndroidOutDevices.size()) {
        AHAL_ERR("mismatched pal no of devices %d and hal devices %zu", noPalDevices, mAndroidOutDevices.size());
        goto error;
    }

    mPalOutDevice = (struct pal_device*) calloc(mAndroidOutDevices.size(), sizeof(struct pal_device));
    if (!mPalOutDevice) {
        goto error;
    }

    /* TODO: how to update based on stream parameters and see if device is supported */
    for (int i = 0; i < mAndroidOutDevices.size(); i++) {
        memset(mPalOutDevice[i].custom_config.custom_key, 0, sizeof(mPalOutDevice[i].custom_config.custom_key));
        mPalOutDevice[i].id = mPalOutDeviceIds[i];
        if (AudioExtn::audio_devices_cmp(mAndroidOutDevices, audio_is_usb_out_device))
            mPalOutDevice[i].config.sample_rate = config_.sample_rate;
        else
            mPalOutDevice[i].config.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        mPalOutDevice[i].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        mPalOutDevice[i].config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE; // TODO: need to convert this from output format
        AHAL_INFO("device rate = %#x width=%#x fmt=%#x",
            mPalOutDevice[i].config.sample_rate,
            mPalOutDevice[i].config.bit_width,
            mPalOutDevice[i].config.aud_fmt_id);
            mPalOutDevice[i].config.ch_info = {0, {0}};
        if ((mPalOutDeviceIds[i] == PAL_DEVICE_OUT_USB_DEVICE) ||
           (mPalOutDeviceIds[i] == PAL_DEVICE_OUT_USB_HEADSET)) {
            mPalOutDevice[i].address.card_id = adevice->usb_card_id_;
            mPalOutDevice[i].address.device_num = adevice->usb_dev_num_;
        }
        strlcpy(mPalOutDevice[i].custom_config.custom_key, "",
                sizeof(mPalOutDevice[i].custom_config.custom_key));

        if ((AudioExtn::audio_devices_cmp(mAndroidOutDevices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) &&
                                   (mPalOutDeviceIds[i] == PAL_DEVICE_OUT_SPEAKER)) {
            strlcpy(mPalOutDevice[i].custom_config.custom_key, "speaker-safe",
                     sizeof(mPalOutDevice[i].custom_config.custom_key));
            AHAL_INFO("Setting custom key as %s", mPalOutDevice[i].custom_config.custom_key);
        }

    }

    if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        stream_.get()->start = astream_out_mmap_noirq_start;
        stream_.get()->stop = astream_out_mmap_noirq_stop;
        stream_.get()->create_mmap_buffer = astream_out_create_mmap_buffer;
        stream_.get()->get_mmap_position = astream_out_get_mmap_position;
    }

    if (usecase_ == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
        AHAL_INFO("Haptics Usecase");
        /* Setting flag here as no flag is being set for haptics from AudioPolicyManager
         * so that audio stream runs as low latency stream.
         */
        flags_ = AUDIO_OUTPUT_FLAG_FAST;
    }

    mInitialized = true;
    for(auto dev : mAndroidOutDevices)
        audio_extn_gef_notify_device_config(dev, config_.channel_mask,
            config_.sample_rate, flags_);

error:
    (void)FillHalFnPtrs();
    AHAL_DBG("Exit");
    return;
}

StreamOutPrimary::~StreamOutPrimary() {
    AHAL_DBG("close stream, handle(%x), pal_stream_handle (%p)",
          handle_, pal_stream_handle_);

    stream_mutex_.lock();
    if (pal_stream_handle_) {
        if (CheckOffloadEffectsType(streamAttributes_.type)) {
            StopOffloadEffects(handle_, pal_stream_handle_);
            StopOffloadVisualizer(handle_, pal_stream_handle_);
        }

        pal_stream_close(pal_stream_handle_);
        pal_stream_handle_ = nullptr;
    }

    if (pal_haptics_stream_handle) {
        pal_stream_close(pal_haptics_stream_handle);
        pal_haptics_stream_handle = NULL;
        if (hapticBuffer) {
            free (hapticBuffer);
            hapticBuffer = NULL;
        }
        hapticsBufSize = 0;
    }

    if (convertBuffer)
        free(convertBuffer);
    if (mPalOutDeviceIds) {
        free(mPalOutDeviceIds);
        mPalOutDeviceIds = NULL;
    }
    if (mPalOutDevice) {
        free(mPalOutDevice);
        mPalOutDevice = NULL;
    }
    if (hapticsDevice) {
        free(hapticsDevice);
        hapticsDevice = NULL;
    }
    stream_mutex_.unlock();
}

bool StreamInPrimary::isDeviceAvailable(pal_device_id_t deviceId)
{
    for (int i = 0; i < mAndroidInDevices.size(); i++) {
        if (mPalInDevice[i].id == deviceId)
            return true;
    }

    return false;
}

int StreamInPrimary::GetPalDeviceIds(pal_device_id_t *palDevIds, int *numPalDevs)
{
    int noPalDevices;

    if (!palDevIds || !numPalDevs)
        return -EINVAL;

    noPalDevices = getPalDeviceIds(mAndroidInDevices, mPalInDeviceIds);
    if (noPalDevices > MAX_ACTIVE_MICROPHONES_TO_SUPPORT)
        return -EINVAL;

    *numPalDevs = noPalDevices;
    for(int i = 0; i < noPalDevices; i++)
        palDevIds[i] = mPalInDeviceIds[i];

    return 0;
}

int StreamInPrimary::Stop() {
    int ret = -ENOSYS;

    AHAL_INFO("Enter: InPrimary usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);
    stream_mutex_.lock();
    if (usecase_ == USECASE_AUDIO_PLAYBACK_VOIP)
        hac_voip = false;
    if (usecase_ == USECASE_AUDIO_RECORD_MMAP &&
            pal_stream_handle_ && stream_started_) {

        ret = pal_stream_stop(pal_stream_handle_);
        if (ret == 0)
            stream_started_ = false;
    }
    stream_mutex_.unlock();
    return ret;
}

int StreamInPrimary::Start() {
    int ret = -ENOSYS;

    AHAL_INFO("Enter: InPrimary usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);
    stream_mutex_.lock();
    if (usecase_ == USECASE_AUDIO_RECORD_MMAP &&
            pal_stream_handle_ && !stream_started_) {

        ret = pal_stream_start(pal_stream_handle_);
        if (ret == 0)
            stream_started_ = true;
    }
    stream_mutex_.unlock();
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int StreamInPrimary::CreateMmapBuffer(int32_t min_size_frames,
        struct audio_mmap_buffer_info *info)
{
    int ret;
    struct pal_mmap_buffer palMmapBuf;

    stream_mutex_.lock();
    if (pal_stream_handle_) {
        AHAL_ERR("error pal handle already created\n");
        stream_mutex_.unlock();
        return -EINVAL;
    }

    ret = Open();
    if (ret) {
        AHAL_ERR("failed to open stream.");
        stream_mutex_.unlock();
        return ret;
    }
    ret = pal_stream_create_mmap_buffer(pal_stream_handle_,
            min_size_frames, &palMmapBuf);
    if (ret) {
        AHAL_ERR("failed to create mmap buffer: %d", ret);
        // release stream lock as Standby will lock/unlock stream mutex
        stream_mutex_.unlock();
        Standby();
        return ret;
    }
    info->shared_memory_address = palMmapBuf.buffer;
    info->shared_memory_fd = palMmapBuf.fd;
    info->buffer_size_frames = palMmapBuf.buffer_size_frames;
    info->burst_size_frames = palMmapBuf.burst_size_frames;
    info->flags = (audio_mmap_buffer_flag)palMmapBuf.flags;
    mmap_shared_memory_fd = info->shared_memory_fd;

    stream_mutex_.unlock();
    return ret;
}

int StreamInPrimary::GetMmapPosition(struct audio_mmap_position *position)
{
    struct pal_mmap_position pal_mmap_pos;
    int32_t ret = 0;

    stream_mutex_.lock();
    if (pal_stream_handle_ == nullptr) {
        AHAL_ERR("error pal handle is null\n");
        stream_mutex_.unlock();
        return -EINVAL;
    }

    ret = pal_stream_get_mmap_position(pal_stream_handle_, &pal_mmap_pos);
    if (ret) {
        AHAL_ERR("failed to get mmap position %d\n", ret);
        stream_mutex_.unlock();
        return ret;
    }
    position->position_frames = pal_mmap_pos.position_frames;
    position->time_nanoseconds = pal_mmap_pos.time_nanoseconds;

    stream_mutex_.unlock();
    return 0;
}

int StreamInPrimary::Standby() {
    int ret = 0;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    AHAL_DBG("Enter");
    stream_mutex_.lock();
    if (pal_stream_handle_) {
        if (!is_st_session) {
            ret = pal_stream_stop(pal_stream_handle_);
        } else if (audio_extn_sound_trigger_check_session_activity(this)) {
            ret = pal_stream_set_param(pal_stream_handle_,
                PAL_PARAM_ID_STOP_BUFFERING, nullptr);
            if (adevice->num_va_sessions_ > 0) {
                adevice->num_va_sessions_--;
            }
        }
    }
    effects_applied_ = true;
    stream_started_ = false;

    if (pal_stream_handle_ && !is_st_session) {
        ret = pal_stream_close(pal_stream_handle_);
        pal_stream_handle_ = NULL;
    }

    if (mmap_shared_memory_fd >= 0) {
        close(mmap_shared_memory_fd);
        mmap_shared_memory_fd = -1;
    }

    if (ret)
        ret = -EINVAL;

    stream_mutex_.unlock();
    AHAL_DBG("Exit ret: %d", ret);
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
                    AHAL_ERR("EC already enabled");
                    goto exit;
                } else if (isNSEnabled) {
                    AHAL_VERBOSE("Got EC enable and NS is already active. Enabling ECNS");
                    status = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_ECNS,true);
                    isECEnabled = true;
                    goto exit;
                } else {
                    AHAL_VERBOSE("Got EC enable. Enabling EC");
                    status = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_EC,true);
                    isECEnabled = true;
                    goto exit;
               }
            } else {
                if (isECEnabled) {
                    if (isNSEnabled) {
                        AHAL_VERBOSE("ECNS is running. Disabling EC and enabling NS alone");
                        status = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_NS,true);
                        isECEnabled = false;
                        goto exit;
                    } else {
                        AHAL_VERBOSE("EC is running. Disabling it");

                        status = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_ECNS,false);

                        isECEnabled = false;
                        goto exit;
                    }
                } else {
                    AHAL_ERR("EC is not enabled");
                    goto exit;
               }
            }
        }

        if (memcmp(&desc.type, FX_IID_NS, sizeof(effect_uuid_t)) == 0) {
            if (enable) {
                if (isNSEnabled) {
                    AHAL_ERR("NS already enabled");
                    goto exit;
                } else if (isECEnabled) {
                    AHAL_VERBOSE("Got NS enable and EC is already active. Enabling ECNS");
                    status = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_ECNS,true);
                    isNSEnabled = true;
                    goto exit;
                } else {
                    AHAL_VERBOSE("Got NS enable. Enabling NS");
                    status = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_NS,true);
                    isNSEnabled = true;
                    goto exit;
               }
            } else {
                if (isNSEnabled) {
                    if (isECEnabled) {
                        AHAL_VERBOSE("ECNS is running. Disabling NS and enabling EC alone");
                        status = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_EC,true);
                        isNSEnabled = false;
                        goto exit;
                    } else {
                        AHAL_VERBOSE("NS is running. Disabling it");

                        status = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_ECNS,false);

                        isNSEnabled = false;
                        goto exit;
                    }
                } else {
                    AHAL_ERR("NS is not enabled");
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
    struct pal_volume_data* volume;
    int ret = 0;

    AHAL_DBG("Enter");
    stream_mutex_.lock();
    volume = (struct pal_volume_data*)malloc(sizeof(uint32_t)
                +sizeof(struct pal_channel_vol_kv));
    if (!volume) {
        AHAL_ERR("Failed to allocate mem for volume");
        ret = -ENOMEM;
        goto done;
    }
    volume->no_of_volpair = 1;
    volume->volume_pair[0].channel_mask = 0x03;
    volume->volume_pair[0].vol = gain;
    if (pal_stream_handle_) {
        ret = pal_stream_set_volume(pal_stream_handle_, volume);
    }

    free(volume);
    if (ret) {
        AHAL_ERR("Pal Stream volume Error (%x)", ret);
    }

done:
    stream_mutex_.unlock();
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

int StreamInPrimary::RouteStream(const std::set<audio_devices_t>& new_devices, bool force_device_switch) {
    bool is_empty, is_input;
    int ret = 0, noPalDevices = 0;
    pal_device_id_t * deviceId = nullptr;
    struct pal_device* deviceIdConfigs = nullptr;
    pal_param_device_capability_t *device_cap_query = nullptr;
    size_t payload_size = 0;
    dynamic_media_config_t dynamic_media_config;
    struct pal_channel_info ch_info = {0, {0}};
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    AHAL_INFO("Enter: InPrimary usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);

    stream_mutex_.lock();
    if (!mInitialized){
        AHAL_ERR("Not initialized, returning error");
        ret = -EINVAL;
        goto done;
    }

    AHAL_DBG("mAndroidInDevices 0x%x, mNoOfInDevices %zu, new_devices 0x%x, num new_devices: %zu",
             AudioExtn::get_device_types(mAndroidInDevices),
             mAndroidInDevices.size(), AudioExtn::get_device_types(new_devices), new_devices.size());

    // TBD: Hard code number of channels to 2 for now.
    // channels = audio_channel_count_from_out_mask(config_.channel_mask);
    // need to convert channel mask to pal channel mask
    ch_info.channels = 2;
    ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
    if (ch_info.channels > 1 )
        ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;

    is_empty = AudioExtn::audio_devices_empty(new_devices);
    is_input = AudioExtn::audio_devices_cmp(new_devices, audio_is_input_device);

    /* If its the same device as what was already routed to, dont bother */
    if (!is_empty && is_input
            && ((mAndroidInDevices != new_devices) || force_device_switch)) {
        //re-allocate mPalInDevice and mPalInDeviceIds
        if (new_devices.size() != mAndroidInDevices.size()) {
            deviceId = (pal_device_id_t*) realloc(mPalInDeviceIds,
                    new_devices.size() * sizeof(pal_device_id_t));
            deviceIdConfigs = (struct pal_device*) realloc(mPalInDevice,
                    new_devices.size() * sizeof(struct pal_device));
            if (!deviceId || !deviceIdConfigs) {
                AHAL_ERR("Failed to allocate PalOutDeviceIds or deviceIdConfigs!");
                if (deviceId)
                    mPalInDeviceIds = deviceId;
                if (deviceIdConfigs)
                    mPalInDevice = deviceIdConfigs;
                ret = -ENOMEM;
                goto done;
            }

            // init deviceId and deviceIdConfigs
            memset(deviceId, 0, new_devices.size() * sizeof(pal_device_id_t));
            memset(deviceIdConfigs, 0, new_devices.size() * sizeof(struct pal_device));

            mPalInDeviceIds = deviceId;
            mPalInDevice = deviceIdConfigs;
        }
        noPalDevices = getPalDeviceIds(new_devices, mPalInDeviceIds);
        AHAL_DBG("noPalDevices: %d , new_devices: %zu",
                noPalDevices, new_devices.size());
        if (noPalDevices != new_devices.size() ||
            noPalDevices >= PAL_DEVICE_IN_MAX) {
            AHAL_ERR("Device count mismatch! Expected: %d Got: %zu", noPalDevices, new_devices.size());
            ret = -EINVAL;
            goto done;
        }

        device_cap_query = (pal_param_device_capability_t *)
                malloc(sizeof(pal_param_device_capability_t));
        if (!device_cap_query) {
                AHAL_ERR("Failed to allocate device_cap_query!");
                ret = -ENOMEM;
                goto done;
        }

        for (int i = 0; i < noPalDevices; i++) {
            mPalInDevice[i].id = mPalInDeviceIds[i];
            if (((mPalInDeviceIds[i] == PAL_DEVICE_IN_USB_DEVICE) ||
               (mPalInDeviceIds[i] == PAL_DEVICE_IN_USB_HEADSET)) && device_cap_query) {

                mPalInDevice[i].address.card_id = adevice->usb_card_id_;
                mPalInDevice[i].address.device_num = adevice->usb_dev_num_;
                device_cap_query->id = mPalInDeviceIds[i];
                device_cap_query->addr.card_id = adevice->usb_card_id_;
                device_cap_query->addr.device_num = adevice->usb_dev_num_;
                device_cap_query->config = &dynamic_media_config;
                device_cap_query->is_playback = true;
                ret = pal_get_param(PAL_PARAM_ID_DEVICE_CAPABILITY,(void **)&device_cap_query,
                        &payload_size, nullptr);

                if (ret<0) {
                    AHAL_ERR("Error usb device is not connected");
                    ret = -ENOSYS;
                    goto done;
                }
            }
            mPalInDevice[i].config.sample_rate = mPalInDevice[0].config.sample_rate;
            mPalInDevice[i].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
            mPalInDevice[i].config.ch_info = ch_info;
            mPalInDevice[i].config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;
            if ((mPalInDeviceIds[i] == PAL_DEVICE_IN_USB_DEVICE) ||
               (mPalInDeviceIds[i] == PAL_DEVICE_IN_USB_HEADSET)) {
                mPalInDevice[i].address.card_id = adevice->usb_card_id_;
                mPalInDevice[i].address.device_num = adevice->usb_dev_num_;
            }
            strlcpy(mPalInDevice[i].custom_config.custom_key, "",
                    sizeof(mPalInDevice[i].custom_config.custom_key));

            /* HDR use case check */
            if (is_hdr_mode_enabled())
                setup_hdr_usecase(&mPalInDevice[i]);

            if (source_ == AUDIO_SOURCE_CAMCORDER && adevice->cameraOrientation == CAMERA_DEFAULT) {
                strlcpy(mPalInDevice[i].custom_config.custom_key, "camcorder_landscape",
                        sizeof(mPalInDevice[i].custom_config.custom_key));
                AHAL_INFO("Setting custom key as %s", mPalInDevice[i].custom_config.custom_key);
            }
        }

        mAndroidInDevices = new_devices;

        if (pal_stream_handle_)
            ret = pal_stream_set_device(pal_stream_handle_, noPalDevices, mPalInDevice);
    }

done:
    if (device_cap_query) {
        free(device_cap_query);
        device_cap_query = NULL;
    }
    stream_mutex_.unlock();
    AHAL_DBG("exit %d", ret);
    return ret;
}

bool StreamInPrimary::getParameters(struct str_parms *query,
                                    struct str_parms *reply) {
    bool found = false;
    char value[256];

    if (usecase_ == USECASE_AUDIO_RECORD_COMPRESS) {
        if (config_.format == AUDIO_FORMAT_AAC_LC ||
            config_.format == AUDIO_FORMAT_AAC_ADTS_LC ||
            config_.format == AUDIO_FORMAT_AAC_ADTS_HE_V1 ||
            config_.format == AUDIO_FORMAT_AAC_ADTS_HE_V2) {
            // query for AAC bitrate
            if (str_parms_get_str(query,
                                  CompressCapture::kAudioParameterDSPAacBitRate,
                                  value, sizeof(value)) >= 0) {
                value[0] = '\0';
                // fill in the AAC bitrate
                if (mIsBitRateSet &&
                    (str_parms_add_int(
                         reply, CompressCapture::kAudioParameterDSPAacBitRate,
                         mCompressStreamAdjBitRate) >= 0)) {
                    mIsBitRateGet = found = true;
                }
            }
        }
    }

    return found;
}

int StreamInPrimary::SetParameters(const char* kvpairs) {
    struct str_parms *parms = (str_parms *)NULL;
    int ret = 0;

    AHAL_DBG("enter: kvpairs: %s", kvpairs);
    if(!mInitialized)
        goto exit;

    parms = str_parms_create_str(kvpairs);
    if (!parms)
        goto exit;

    if (usecase_ == USECASE_AUDIO_RECORD_COMPRESS) {
        if (CompressCapture::parseMetadata(parms, &config_,
                                           mCompressStreamAdjBitRate)) {
            mIsBitRateSet = true;
        } else {
            ret = -EINVAL;
        }
    }

    str_parms_destroy(parms);
exit:
   AHAL_DBG("exit %d", ret);
   return ret;
}

int StreamInPrimary::Open() {
    int ret = 0;
    uint8_t channels = 0;
    struct pal_channel_info ch_info = {0, {0}};
    uint32_t inBufSize = 0;
    uint32_t inBufCount = NO_OF_BUF;
    struct pal_buffer_config inBufCfg = {0, 0, 0};
    void *handle = nullptr;
    pal_param_device_capability_t *device_cap_query = NULL;
    size_t payload_size = 0;
    dynamic_media_config_t dynamic_media_config;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();

    AHAL_INFO("Enter: InPrimary usecase(%d: %s)", GetUseCase(), use_case_table[GetUseCase()]);
    if (!mInitialized) {
        AHAL_ERR("Not initialized, returning error");
        ret = -EINVAL;
        goto exit;
    }

    handle = audio_extn_sound_trigger_check_and_get_session(this);
    if (handle) {
        AHAL_VERBOSE("Found existing pal stream handle associated with capture handle");
        pal_stream_handle_ = (pal_stream_handle_t *)handle;
        goto set_buff_size;
    }

    channels = audio_channel_count_from_in_mask(config_.channel_mask);
    if (channels == 0) {
       AHAL_ERR("invalid channel count");
       ret = -EINVAL;
       goto exit;
    }
    //need to convert channel mask to pal channel mask
    if (channels == 8) {
      ch_info.channels = 8;
      ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = PAL_CHMAP_CHANNEL_C;
      ch_info.ch_map[3] = PAL_CHMAP_CHANNEL_LFE;
      ch_info.ch_map[4] = PAL_CHMAP_CHANNEL_LB;
      ch_info.ch_map[5] = PAL_CHMAP_CHANNEL_RB;
      ch_info.ch_map[6] = PAL_CHMAP_CHANNEL_LS;
      ch_info.ch_map[6] = PAL_CHMAP_CHANNEL_RS;
    } else if (channels == 7) {
      ch_info.channels = 7;
      ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = PAL_CHMAP_CHANNEL_C;
      ch_info.ch_map[3] = PAL_CHMAP_CHANNEL_LFE;
      ch_info.ch_map[4] = PAL_CHMAP_CHANNEL_LB;
      ch_info.ch_map[5] = PAL_CHMAP_CHANNEL_RB;
      ch_info.ch_map[6] = PAL_CHMAP_CHANNEL_LS;
    } else if (channels == 6) {
      ch_info.channels = 6;
      ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = PAL_CHMAP_CHANNEL_C;
      ch_info.ch_map[3] = PAL_CHMAP_CHANNEL_LFE;
      ch_info.ch_map[4] = PAL_CHMAP_CHANNEL_LB;
      ch_info.ch_map[5] = PAL_CHMAP_CHANNEL_RB;
    } else if (channels == 5) {
      ch_info.channels = 5;
      ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = PAL_CHMAP_CHANNEL_C;
      ch_info.ch_map[3] = PAL_CHMAP_CHANNEL_LFE;
      ch_info.ch_map[4] = PAL_CHMAP_CHANNEL_RC;
    } else if (channels == 4) {
      ch_info.channels = 4;
      ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = PAL_CHMAP_CHANNEL_C;
      ch_info.ch_map[3] = PAL_CHMAP_CHANNEL_LFE;
    } else if (channels == 3) {
      ch_info.channels = 3;
      ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;
      ch_info.ch_map[2] = PAL_CHMAP_CHANNEL_C;
    } else if (channels == 2) {
      ch_info.channels = 2;
      ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
      ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;
    } else {
      ch_info.channels = 1;
      ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
    }

    streamAttributes_.type = StreamInPrimary::GetPalStreamType(flags_,
            config_.sample_rate);
    if (source_ == AUDIO_SOURCE_VOICE_UPLINK) {
        streamAttributes_.type = PAL_STREAM_VOICE_CALL_RECORD;
        streamAttributes_.info.voice_rec_info.record_direction = INCALL_RECORD_VOICE_UPLINK;
    } else if (source_ == AUDIO_SOURCE_VOICE_DOWNLINK) {
        streamAttributes_.type = PAL_STREAM_VOICE_CALL_RECORD;
        streamAttributes_.info.voice_rec_info.record_direction = INCALL_RECORD_VOICE_DOWNLINK;
    } else if (source_ == AUDIO_SOURCE_VOICE_CALL) {
        streamAttributes_.type = PAL_STREAM_VOICE_CALL_RECORD;
        streamAttributes_.info.voice_rec_info.record_direction = INCALL_RECORD_VOICE_UPLINK_DOWNLINK;
    }
    streamAttributes_.flags = (pal_stream_flags_t)0;
    streamAttributes_.direction = PAL_AUDIO_INPUT;
    streamAttributes_.in_media_config.sample_rate = config_.sample_rate;
    if (is_pcm_format(config_.format)) {
       streamAttributes_.in_media_config.aud_fmt_id = getFormatId.at(config_.format);
       streamAttributes_.in_media_config.bit_width = format_to_bitwidth_table[config_.format];
    } else if (!is_pcm_format(config_.format) && usecase_ == USECASE_AUDIO_RECORD_COMPRESS) {
        streamAttributes_.in_media_config.aud_fmt_id = getFormatId.at(config_.format);
        streamAttributes_.in_media_config.bit_width = compressRecordBitWidthTable.at(config_.format);
    } else {
       /*TODO:Update this to support compressed capture using hal apis*/
       streamAttributes_.in_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
       streamAttributes_.in_media_config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;
    }
    streamAttributes_.in_media_config.ch_info = ch_info;

    if (streamAttributes_.type == PAL_STREAM_ULTRA_LOW_LATENCY) {
            if (usecase_ == USECASE_AUDIO_RECORD_MMAP)
                streamAttributes_.flags = (pal_stream_flags_t)
                    (PAL_STREAM_FLAG_MMAP_NO_IRQ);
            else if (usecase_ == USECASE_AUDIO_RECORD_LOW_LATENCY)
                streamAttributes_.flags = (pal_stream_flags_t)
                    (PAL_STREAM_FLAG_MMAP);
    }

    if (streamAttributes_.type == PAL_STREAM_PROXY) {
        if (isDeviceAvailable(PAL_DEVICE_IN_PROXY))
            streamAttributes_.info.opt_stream_info.tx_proxy_type = PAL_STREAM_PROXY_TX_WFD;
        else if (isDeviceAvailable(PAL_DEVICE_IN_TELEPHONY_RX))
            streamAttributes_.info.opt_stream_info.tx_proxy_type = PAL_STREAM_PROXY_TX_TELEPHONY_RX;
    }

    device_cap_query = (pal_param_device_capability_t *)malloc(sizeof(pal_param_device_capability_t));

    if ((mPalInDevice->id == PAL_DEVICE_IN_USB_DEVICE || mPalInDevice->id ==
        PAL_DEVICE_IN_USB_HEADSET) && device_cap_query && adevice) {

        device_cap_query->id = mPalInDevice->id;
        device_cap_query->addr.card_id = adevice->usb_card_id_;
        device_cap_query->addr.device_num = adevice->usb_dev_num_;
        device_cap_query->config = &dynamic_media_config;
        device_cap_query->is_playback = true;
        ret = pal_get_param(PAL_PARAM_ID_DEVICE_CAPABILITY,(void **)&device_cap_query,
                &payload_size, nullptr);

         if (ret<0) {
             AHAL_DBG("Error usb device is not connected");
             ret = -ENOSYS;
             goto exit;
         }
    }

    AHAL_DBG("(%x:ret)", ret);

    ret = pal_stream_open(&streamAttributes_,
                         mAndroidInDevices.size(),
                         mPalInDevice,
                         0,
                         NULL,
                         &pal_callback,
                         (uint64_t)this,
                         &pal_stream_handle_);

    if (ret) {
        AHAL_ERR("Pal Stream Open Error (%x)", ret);
        ret = -EINVAL;
        goto exit;
    }
    // TODO configure this for any audio format
    //PAL input compressed stream is used only for compress capture
    if (streamAttributes_.type == PAL_STREAM_COMPRESSED) {
        pal_param_payload *param_payload = nullptr;
        param_payload = (pal_param_payload *)calloc(
            1, sizeof(pal_param_payload) + sizeof(pal_snd_enc_t));

        if (!param_payload) {
            AHAL_ERR("calloc failed for size %zu",
                     sizeof(pal_param_payload) + sizeof(pal_snd_enc_t));
        } else {
            /**
            * encoder mode
            0x2       AAC_AOT_LC
            0x5      AAC_AOT_SBR
            0x1d      AAC_AOT_PS

            * format flag
            0x0       AAC_FORMAT_FLAG_ADTS
            0x1       AAC_FORMAT_FLAG_LOAS
            0x3       AAC_FORMAT_FLAG_RAW
            0x4       AAC_FORMAT_FLAG_LATM
            **/
            param_payload->payload_size = sizeof(pal_snd_enc_t);

            if (config_.format == AUDIO_FORMAT_AAC_LC ||
                config_.format == AUDIO_FORMAT_AAC_ADTS_LC) {
                palSndEnc.aac_enc.enc_cfg.aac_enc_mode = 0x2;
                palSndEnc.aac_enc.enc_cfg.aac_fmt_flag = 0x00;
            } else if (config_.format == AUDIO_FORMAT_AAC_ADTS_HE_V1) {
                palSndEnc.aac_enc.enc_cfg.aac_enc_mode = 0x5;
                palSndEnc.aac_enc.enc_cfg.aac_fmt_flag = 0x00;
            } else if (config_.format == AUDIO_FORMAT_AAC_ADTS_HE_V2) {
                palSndEnc.aac_enc.enc_cfg.aac_enc_mode = 0x1d;
                palSndEnc.aac_enc.enc_cfg.aac_fmt_flag = 0x00;
            } else {
                palSndEnc.aac_enc.enc_cfg.aac_enc_mode = 0x2;
                palSndEnc.aac_enc.enc_cfg.aac_fmt_flag = 0x00;
            }

            if (mIsBitRateSet && mIsBitRateGet) {
                palSndEnc.aac_enc.aac_bit_rate = mCompressStreamAdjBitRate;
                mIsBitRateSet = mIsBitRateGet = false;
                AHAL_DBG("compress aac bitrate configured: %d",
                         palSndEnc.aac_enc.aac_bit_rate);
            } else {
                palSndEnc.aac_enc.aac_bit_rate =
                    CompressCapture::sSampleRateToDefaultBitRate.at(
                        config_.sample_rate);
            }

            memcpy(param_payload->payload, &palSndEnc,
                   param_payload->payload_size);

            ret = pal_stream_set_param(pal_stream_handle_,
                                       PAL_PARAM_ID_CODEC_CONFIGURATION,
                                       param_payload);
            if (ret) AHAL_ERR("Pal Set Param Error (%x)", ret);
            free(param_payload);
        }
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

    if (usecase_ == USECASE_AUDIO_RECORD_VOIP)
        inBufCount = VOIP_PERIOD_COUNT_DEFAULT;

    if (!handle) {
        inBufCfg.buf_size = inBufSize;
        inBufCfg.buf_count = inBufCount;
        ret = pal_stream_set_buffer_size(pal_stream_handle_, &inBufCfg, NULL);
        inBufSize = inBufCfg.buf_size;
        if (ret) {
            AHAL_ERR("Pal Stream set buffer size Error  (%x)", ret);
        }
    }

    fragments_ = inBufCount;
    fragment_size_ = inBufSize;

exit:
    if (device_cap_query) {
        free(device_cap_query);
        device_cap_query = NULL;
    }
    AHAL_DBG("Exit ret: %d", ret);
    return ret;
}

uint32_t StreamInPrimary::GetBufferSizeForLowLatencyRecord() {
     int trial = 0;
     char value[PROPERTY_VALUE_MAX] = {0};
     int configured_low_latency_record_multiplier = ULL_PERIOD_MULTIPLIER;

     if (property_get("vendor.audio.ull_record_period_multiplier", value, NULL) > 0) {
         trial = atoi(value);
         if(trial < ULL_PERIOD_MULTIPLIER && trial > 0)
             configured_low_latency_record_multiplier = trial;
     }
     return ULL_PERIOD_SIZE * configured_low_latency_record_multiplier *
            audio_bytes_per_frame(
                    audio_channel_count_from_in_mask(config_.channel_mask),
                    config_.format);
}

/* in bytes */
uint32_t StreamInPrimary::GetBufferSize() {
    struct pal_stream_attributes streamAttributes_;
#ifdef EC_REF_CAPTURE_ENABLED
    bool isEchoRef = false;
#endif
    size_t size = 0;
    uint32_t bytes_per_period_sample = 0;

    streamAttributes_.type = StreamInPrimary::GetPalStreamType(flags_,
            config_.sample_rate);
#ifdef EC_REF_CAPTURE_ENABLED
    if (streamAttributes_.type == PAL_STREAM_RAW && isDeviceAvailable(PAL_DEVICE_IN_ECHO_REF))
        isEchoRef = true;
#endif
    if (streamAttributes_.type == PAL_STREAM_VOIP_TX) {
        size = (DEFAULT_VOIP_BUF_DURATION_MS * config_.sample_rate / 1000) *
               audio_bytes_per_frame(
                       audio_channel_count_from_in_mask(config_.channel_mask),
                       config_.format);
    } else if (streamAttributes_.type == PAL_STREAM_LOW_LATENCY) {
        size = LOW_LATENCY_CAPTURE_PERIOD_SIZE *
            audio_bytes_per_frame(
                    audio_channel_count_from_in_mask(config_.channel_mask),
                    config_.format);
    } else if (streamAttributes_.type == PAL_STREAM_ULTRA_LOW_LATENCY) {
        return GetBufferSizeForLowLatencyRecord();
    } else if (streamAttributes_.type == PAL_STREAM_VOICE_CALL_RECORD) {
        return (config_.sample_rate * AUDIO_CAPTURE_PERIOD_DURATION_MSEC/ 1000) *
            audio_bytes_per_frame(
                    audio_channel_count_from_in_mask(config_.channel_mask),
                    config_.format);
    } else if (streamAttributes_.type == PAL_STREAM_PROXY) {
        if (isDeviceAvailable(PAL_DEVICE_IN_TELEPHONY_RX)) {
            audio_stream_in* stream_in;
            GetStreamHandle(&stream_in);
            return AFE_PROXY_RECORD_PERIOD_SIZE * audio_stream_in_frame_size(stream_in);
        } else
            return config_.frame_count *
                audio_bytes_per_frame(
                        audio_channel_count_from_in_mask(config_.channel_mask),
                        config_.format);
    } else if (streamAttributes_.type == PAL_STREAM_COMPRESSED) {
        // TODO make this allocation with respect to AUDIO_FORMAT
        return COMPRESS_CAPTURE_AAC_MAX_OUTPUT_BUFFER_SIZE;
    } else {
        /* this else condition will be other stream types like deepbuffer/RAW.. */
        size = (config_.sample_rate * AUDIO_CAPTURE_PERIOD_DURATION_MSEC) /1000;
        size *= audio_bytes_per_frame(
                         audio_channel_count_from_in_mask(config_.channel_mask),
                         config_.format);;
    }
    /* make sure the size is multiple of 32 bytes and additionally multiple of
     * the frame_size (required for 24bit samples and non-power-of-2 channel counts)
     * At 48 kHz mono 16-bit PCM:
     *  5.000 ms = 240 frames = 15*16*1*2 = 480, a whole multiple of 32 (15)
     *  3.333 ms = 160 frames = 10*16*1*2 = 320, a whole multiple of 32 (10)
     * Also, make sure the size is multiple of bytes per period sample
     */
    bytes_per_period_sample = audio_bytes_per_sample(config_.format) *
                              audio_channel_count_from_in_mask(config_.channel_mask);
    size = nearest_multiple(size, lcm(32, bytes_per_period_sample));

    return size;
}

int StreamInPrimary::GetInputUseCase(audio_input_flags_t halStreamFlags, audio_source_t source)
{
    // TODO: cover other usecases
    int usecase = USECASE_AUDIO_RECORD;
    if (config_.sample_rate == LOW_LATENCY_CAPTURE_SAMPLE_RATE &&
        (halStreamFlags & AUDIO_INPUT_FLAG_TIMESTAMP) == 0 &&
        (halStreamFlags & AUDIO_INPUT_FLAG_COMPRESS) == 0 &&
        (halStreamFlags & AUDIO_INPUT_FLAG_FAST) != 0 &&
        (!(isDeviceAvailable(PAL_DEVICE_IN_PROXY))))
        usecase = USECASE_AUDIO_RECORD_LOW_LATENCY;

    if ((halStreamFlags & AUDIO_INPUT_FLAG_MMAP_NOIRQ) != 0)
        usecase = USECASE_AUDIO_RECORD_MMAP;
    else if (source == AUDIO_SOURCE_VOICE_COMMUNICATION &&
             halStreamFlags & AUDIO_INPUT_FLAG_VOIP_TX)
        usecase = USECASE_AUDIO_RECORD_VOIP;
    else if ((halStreamFlags & AUDIO_INPUT_FLAG_DIRECT) != 0)
        usecase = USECASE_AUDIO_RECORD_COMPRESS;

    return usecase;
}

int StreamInPrimary::SetMicMute(bool mute) {
    int ret = 0;
    AHAL_DBG("Enter mute %d for input session", mute);
    stream_mutex_.lock();
    if (pal_stream_handle_) {
        AHAL_DBG("Enter if mute %d for input session", mute);
        ret = pal_stream_set_mute(pal_stream_handle_, mute);
        if (ret)
            AHAL_ERR("Error applying mute %d for input session", mute);
    }
    stream_mutex_.unlock();
    AHAL_DBG("Exit");
    return ret;
}

ssize_t StreamInPrimary::onReadError(size_t bytes, size_t ret) {
    // standby streams upon read failures and sleep for buffer duration.
    AHAL_ERR("read failed %d usecase(%d: %s)", ret, GetUseCase(), use_case_table[GetUseCase()]);
    Standby();
    uint32_t byteWidth = streamAttributes_.in_media_config.bit_width / 8;
    uint32_t sampleRate = streamAttributes_.in_media_config.sample_rate;
    uint32_t channelCount = streamAttributes_.in_media_config.ch_info.channels;
    uint32_t frameSize = byteWidth * channelCount;

    if (frameSize == 0 || sampleRate == 0) {
        AHAL_ERR("invalid frameSize=%d, sampleRate=%d", frameSize, sampleRate);
        return -EINVAL;
    } else {
        usleep((uint64_t)bytes * 1000000 / frameSize / sampleRate);
    }
    return bytes;
}

ssize_t StreamInPrimary::read(const void *buffer, size_t bytes) {
    ssize_t ret = 0;
    int retry_count = MAX_READ_RETRY_COUNT;
    ssize_t size = 0;
    struct pal_buffer palBuffer;

    palBuffer.buffer = (uint8_t *)buffer;
    palBuffer.size = bytes;
    palBuffer.offset = 0;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    AHAL_VERBOSE("requested bytes: %zu", bytes);

    stream_mutex_.lock();
    if (!pal_stream_handle_) {
        AutoPerfLock perfLock;
        ret = Open();
        if (ret < 0)
            goto exit;
    }

    if (is_st_session) {
        ATRACE_BEGIN("hal: lab read");
        memset(palBuffer.buffer, 0, palBuffer.size);
        if (!audio_extn_sound_trigger_check_session_activity(this)) {
            AHAL_DBG("sound trigger session not available");
            ATRACE_END();
            goto exit;
        }
        if (!stream_started_) {
            adevice->num_va_sessions_++;
            stream_started_ = true;
        }
        while (retry_count--) {
            ret = pal_stream_read(pal_stream_handle_, &palBuffer);
            if (ret < 0) {
                memset(palBuffer.buffer, 0, palBuffer.size);
                AHAL_ERR("error, failed to read data from PAL");
                ATRACE_END();
                ret = bytes;
                goto exit;
            } else {
                size += ret;
                if (ret < palBuffer.size) {
                    palBuffer.buffer += ret;
                    palBuffer.size -= ret;
                } else {
                    break;
                }
            }
        }
        ATRACE_END();
        goto exit;
    }

    if (!stream_started_) {
        AutoPerfLock perfLock;
        ret = pal_stream_start(pal_stream_handle_);
        if (ret) {
            AHAL_ERR("failed to start stream. ret=%d", ret);
            pal_stream_close(pal_stream_handle_);
            pal_stream_handle_ = NULL;
            goto exit;
        }
        stream_started_ = true;
        /* set cached volume if any, dont return failure back up */
        if (volume_) {
            ret = pal_stream_set_volume(pal_stream_handle_, volume_);
            if (ret) {
                AHAL_ERR("Pal Stream volume Error (%x)", ret);
            }
        }
        /*apply cached mic mute*/
        if (adevice->mute_) {
            pal_stream_set_mute(pal_stream_handle_, adevice->mute_);
        }
    }

    if (!effects_applied_) {
       if (isECEnabled && isNSEnabled) {
          ret = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_ECNS,true);
       } else if (isECEnabled) {
          ret = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_EC,true);
       } else if (isNSEnabled) {
          ret = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_NS,true);
       } else {
          ret = pal_add_remove_effect(pal_stream_handle_,PAL_AUDIO_EFFECT_ECNS,false);
       }
       effects_applied_ = true;
    }

    ret = pal_stream_read(pal_stream_handle_, &palBuffer);
    AHAL_VERBOSE("received size= %d",palBuffer.size);
    if (usecase_ == USECASE_AUDIO_RECORD_COMPRESS && ret > 0) {
        size = palBuffer.size;
        mCompressReadCalls++;
    }
    // mute pcm data if sva client is reading lab data
    if (adevice->num_va_sessions_ > 0 &&
        source_ != AUDIO_SOURCE_VOICE_RECOGNITION &&
        property_get_bool("persist.vendor.audio.va_concurrency_mute_enabled",
        false)) {
        memset(palBuffer.buffer, 0, palBuffer.size);
    }

exit:
    if (mBytesRead <= UINT64_MAX - bytes) {
        mBytesRead += bytes;
    } else {
        mBytesRead = UINT64_MAX;
    }
    stream_mutex_.unlock();
    clock_gettime(CLOCK_MONOTONIC, &readAt);
    AHAL_DBG("Exit: returning size: %zu size ", size);
    return (ret < 0 ? onReadError(bytes, ret) : (size > 0 ? size : bytes));
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
    stream_.get()->update_sink_metadata_v7 = in_update_sink_metadata_v7;

    return ret;
}

StreamInPrimary::StreamInPrimary(audio_io_handle_t handle,
    const std::set<audio_devices_t> &devices,
    audio_input_flags_t flags,
    struct audio_config *config,
    const char *address __unused,
    audio_source_t source) :
    StreamPrimary(handle, devices, config),
    mAndroidInDevices(devices),
    flags_(flags)
{
    stream_ = std::shared_ptr<audio_stream_in> (new audio_stream_in());
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    pal_stream_handle_ = NULL;
    mInitialized = false;
    int noPalDevices = 0;
    int ret = 0;
    readAt.tv_sec = 0;
    readAt.tv_nsec = 0;
    void *st_handle = nullptr;
    pal_param_payload *payload = nullptr;

    AHAL_DBG("enter: handle (%x) format(%#x) sample_rate(%d) channel_mask(%#x) devices(%zu) flags(%#x)"\
          , handle, config->format, config->sample_rate, config->channel_mask,
          mAndroidInDevices.size(), flags);
    if (!(stream_.get())) {
        AHAL_ERR("stream_ new allocation failed");
        goto error;
    }

    if (AudioExtn::audio_devices_cmp(mAndroidInDevices, audio_is_usb_in_device)) {
        // get capability from device of USB
        device_cap_query_ = (pal_param_device_capability_t *)
                                calloc(1, sizeof(pal_param_device_capability_t));
        if (!device_cap_query_) {
            AHAL_ERR("Failed to allocate mem for device_cap_query_");
            goto error;
        }
        dynamic_media_config_t *dynamic_media_config = (dynamic_media_config_t *)
                                                  calloc(1, sizeof(dynamic_media_config_t));
        if (!dynamic_media_config) {
            free(device_cap_query_);
            AHAL_ERR("Failed to allocate mem for dynamic_media_config");
            goto error;
        }
        size_t payload_size = 0;
        device_cap_query_->id = PAL_DEVICE_IN_USB_HEADSET;
        device_cap_query_->addr.card_id = adevice->usb_card_id_;
        device_cap_query_->addr.device_num = adevice->usb_dev_num_;
        device_cap_query_->config = dynamic_media_config;
        device_cap_query_->is_playback = false;
        ret = pal_get_param(PAL_PARAM_ID_DEVICE_CAPABILITY,
                            (void **)&device_cap_query_,
                            &payload_size, nullptr);
        if (ret < 0) {
            AHAL_ERR("Error usb device is not connected");
            free(dynamic_media_config);
            free(device_cap_query_);
            dynamic_media_config = NULL;
            device_cap_query_ = NULL;
        }
        if (dynamic_media_config) {
            AHAL_DBG("usb fs=%d format=%d mask=%x",
                dynamic_media_config->sample_rate[0],
                dynamic_media_config->format[0], dynamic_media_config->mask[0]);
            if (!config->sample_rate) {
                config->sample_rate = dynamic_media_config->sample_rate[0];
                config->channel_mask = (audio_channel_mask_t) dynamic_media_config->mask[0];
                config->format = (audio_format_t)dynamic_media_config->format[0];
                memcpy(&config_, config, sizeof(struct audio_config));
            }
        }
    }

    /* this is required for USB otherwise adev_open_input_stream is failed */
    if (!config_.sample_rate)
        config_.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    if (!config_.channel_mask)
        config_.channel_mask = AUDIO_CHANNEL_IN_MONO;
    if (!config_.format)
        config_.format = AUDIO_FORMAT_PCM_16_BIT;

    /*
     * Audio config set from client may not be same as config used in pal,
     * update audio config here so that AudioFlinger can acquire correct
     * config used in pal/hal and configure record buffer converter properly.
     */
    st_handle = audio_extn_sound_trigger_check_and_get_session(this);
    if (st_handle) {
        AHAL_VERBOSE("Found existing pal stream handle associated with capture handle");
        pal_stream_handle_ = (pal_stream_handle_t *)st_handle;
        payload = (pal_param_payload *)calloc(1,
            sizeof(pal_param_payload) + sizeof(struct pal_stream_attributes));
        if (!payload) {
            AHAL_ERR("Failed to allocate memory for stream attributes");
            goto error;
        }
        payload->payload_size = sizeof(struct pal_stream_attributes);
        ret = pal_stream_get_param(pal_stream_handle_, PAL_PARAM_ID_STREAM_ATTRIBUTES, &payload);
        if (ret) {
            AHAL_ERR("Failed to get pal stream attributes, ret = %d", ret);
            if (payload)
                free(payload);
            goto error;
        }
        memcpy(&streamAttributes_, payload->payload, payload->payload_size);

        if (streamAttributes_.in_media_config.ch_info.channels == 1)
            config_.channel_mask = AUDIO_CHANNEL_IN_MONO;
        else if (streamAttributes_.in_media_config.ch_info.channels == 2)
            config_.channel_mask = AUDIO_CHANNEL_IN_STEREO;
        config_.format = AUDIO_FORMAT_PCM_16_BIT;
        config_.sample_rate = streamAttributes_.in_media_config.sample_rate;

        /*
         * reset pal_stream_handle in case standby come before
         * read as anyway it will be updated in StreamInPrimary::Open
         */
        if (payload)
            free(payload);
        pal_stream_handle_ = nullptr;
    }

    AHAL_DBG("local : handle (%x) format(%#x) sample_rate(%d) channel_mask(%#x) devices(%#x) flags(%#x)"\
          , handle, config_.format, config_.sample_rate, config_.channel_mask,
          AudioExtn::get_device_types(devices), flags);


    source_ = source;

    mAndroidInDevices = devices;
    if(mAndroidInDevices.empty())
        mAndroidInDevices.insert(AUDIO_DEVICE_IN_DEFAULT);

    AHAL_DBG("No of devices %zu", mAndroidInDevices.size());
    mPalInDeviceIds = (pal_device_id_t*) calloc(mAndroidInDevices.size(), sizeof(pal_device_id_t));
    if (!mPalInDeviceIds) {
        goto error;
    }

    noPalDevices = getPalDeviceIds(devices, mPalInDeviceIds);
    if (noPalDevices != mAndroidInDevices.size()) {
        AHAL_ERR("mismatched pal %d and hal devices %zu", noPalDevices, mAndroidInDevices.size());
        goto error;
    }
    mPalInDevice = (struct pal_device*) calloc(mAndroidInDevices.size(), sizeof(struct pal_device));
    if (!mPalInDevice) {
        goto error;
    }

    for (int i = 0; i < mAndroidInDevices.size(); i++) {
        memset(mPalInDevice[i].custom_config.custom_key, 0, sizeof(mPalInDevice[i].custom_config.custom_key));
        mPalInDevice[i].id = mPalInDeviceIds[i];
        mPalInDevice[i].config.sample_rate = config->sample_rate;
        mPalInDevice[i].config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        // ch_info memory is allocated at resource manager:getdeviceconfig
        mPalInDevice[i].config.ch_info = {0, {0}};
        mPalInDevice[i].config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE; // TODO: need to convert this from output format
        if ((mPalInDeviceIds[i] == PAL_DEVICE_IN_USB_DEVICE) ||
           (mPalInDeviceIds[i] == PAL_DEVICE_IN_USB_HEADSET)) {
            mPalInDevice[i].address.card_id = adevice->usb_card_id_;
            mPalInDevice[i].address.device_num = adevice->usb_dev_num_;
        }
        strlcpy(mPalInDevice[i].custom_config.custom_key, "",
                sizeof(mPalInDevice[i].custom_config.custom_key));

        /* HDR use case check */
        if ((source_ == AUDIO_SOURCE_UNPROCESSED) &&
                (config_.sample_rate == 48000)) {
            uint8_t channels =
                audio_channel_count_from_in_mask(config_.channel_mask);
            if (channels == 4) {
                if (is_hdr_mode_enabled()) {
                    flags = flags_ = AUDIO_INPUT_FLAG_RAW;
                    setup_hdr_usecase(&mPalInDevice[i]);
                }
            }
        }

        if (source_ == AUDIO_SOURCE_CAMCORDER && adevice->cameraOrientation == CAMERA_DEFAULT) {
            strlcpy(mPalInDevice[i].custom_config.custom_key, "camcorder_landscape",
                    sizeof(mPalInDevice[i].custom_config.custom_key));
            AHAL_INFO("Setting custom key as %s", mPalInDevice[i].custom_config.custom_key);
        }
    }

    usecase_ = GetInputUseCase(flags, source);
    if (flags & AUDIO_INPUT_FLAG_MMAP_NOIRQ) {
        stream_.get()->start = astream_in_mmap_noirq_start;
        stream_.get()->stop = astream_in_mmap_noirq_stop;
        stream_.get()->create_mmap_buffer = astream_in_create_mmap_buffer;
        stream_.get()->get_mmap_position = astream_in_get_mmap_position;
    }
    mInitialized = true;
error:
    (void)FillHalFnPtrs();
    AHAL_DBG("Exit");
    return;
}

StreamInPrimary::~StreamInPrimary() {
    stream_mutex_.lock();
    if (pal_stream_handle_ && !is_st_session) {
        AHAL_DBG("close stream, pal_stream_handle (%p)",
             pal_stream_handle_);
        pal_stream_close(pal_stream_handle_);
        pal_stream_handle_ = NULL;
    }
    if (mPalInDeviceIds) {
        free(mPalInDeviceIds);
        mPalInDeviceIds = NULL;
    }
    if (mPalInDevice) {
        free(mPalInDevice);
        mPalInDevice = NULL;
    }
    stream_mutex_.unlock();
}

StreamPrimary::StreamPrimary(audio_io_handle_t handle,
    const std::set<audio_devices_t> &devices __unused, struct audio_config *config):
    pal_stream_handle_(NULL),
    handle_(handle),
    config_(*config),
    volume_(NULL),
    mmap_shared_memory_fd(-1),
    device_cap_query_(NULL)
{
    memset(&streamAttributes_, 0, sizeof(streamAttributes_));
    memset(&address_, 0, sizeof(address_));
    AHAL_DBG("handle: %d channel_mask: %d ", handle_, config_.channel_mask);
}

StreamPrimary::~StreamPrimary(void)
{
    if (volume_) {
        free(volume_);
        volume_ = NULL;
    }
    if (device_cap_query_) {
        if (device_cap_query_->config) {
            free(device_cap_query_->config);
            device_cap_query_->config = NULL;
        }
        free(device_cap_query_);
        device_cap_query_ = NULL;
    }
}

