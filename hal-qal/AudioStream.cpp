/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "ahal_AudioStream"
#define ATRACE_TAG (ATRACE_TAG_AUDIO|ATRACE_TAG_HAL)
#define LOG_NDEBUG 0
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include "AudioDevice.h"

#include <log/log.h>

#include "QalApi.h"
#include <audio_effects/effect_aec.h>
#include <audio_effects/effect_ns.h>
#include "audio_extn.h"

const std::map<uint32_t, qal_audio_fmt_t> getFormatId {
	{AUDIO_FORMAT_PCM,                 QAL_AUDIO_FMT_DEFAULT_PCM},
	{AUDIO_FORMAT_MP3,                 QAL_AUDIO_FMT_MP3},
	{AUDIO_FORMAT_AAC,                 QAL_AUDIO_FMT_AAC},
	{AUDIO_FORMAT_AAC_ADTS,            QAL_AUDIO_FMT_AAC_ADTS},
	{AUDIO_FORMAT_AAC_ADIF,            QAL_AUDIO_FMT_AAC_ADIF},
	{AUDIO_FORMAT_AAC_LATM,            QAL_AUDIO_FMT_AAC_LATM},
	{AUDIO_FORMAT_WMA,                 QAL_AUDIO_FMT_WMA_STD},
	{AUDIO_FORMAT_ALAC,                QAL_AUDIO_FMT_ALAC},
	{AUDIO_FORMAT_APE,                 QAL_AUDIO_FMT_APE},
	{AUDIO_FORMAT_WMA_PRO,             QAL_AUDIO_FMT_WMA_PRO},
        {AUDIO_FORMAT_FLAC,                QAL_AUDIO_FMT_FLAC}
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

uint32_t StreamPrimary::GetChannels() {
    return config_.channel_mask;
}

audio_io_handle_t StreamPrimary::GetHandle()
{
    return handle_;
}

std::shared_ptr<AudioDevice> AudioDevice::adev_ = nullptr;
std::shared_ptr<audio_hw_device_t> AudioDevice::device_ = nullptr;

AudioDevice::~AudioDevice() {
}

static int32_t qal_callback(qal_stream_handle_t *stream_handle,
                            uint32_t event_id, uint32_t *event_data,
                            void *cookie){
    ALOGD("%s: stream_handle (%p), event_id (%x), event_data (%p), cookie (%p)",
                __func__,
                stream_handle,
                event_id,
                event_data,
                cookie);
  int status = 0;
  StreamOutPrimary *astream_out = static_cast<StreamOutPrimary *> (cookie);
  switch (event_id)
  {
      case QAL_STREAM_CBK_EVENT_WRITE_READY:
      {
         std::lock_guard<std::mutex> write_guard (astream_out->write_wait_mutex_);
         astream_out->write_ready_ = true;
         ALOGE("%s: received WRITE_READY event\n",__func__);
      }
      (astream_out->write_condition_).notify_all();
      break;
      case QAL_STREAM_CBK_EVENT_DRAIN_READY:
      {
         std::lock_guard<std::mutex> drain_guard (astream_out->drain_wait_mutex_);
         astream_out->drain_ready_ = true;
         ALOGE("%s: received DRAIN_READY event\n",__func__);
      }
      (astream_out->drain_condition_).notify_all();
      break;
      case QAL_STREAM_CBK_EVENT_ERROR:
         status = -1;
      break;
  }
  return status;
}


static uint32_t astream_out_get_sample_rate(const struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device",__func__);
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
        ALOGE("%s: unable to get audio device",__func__);

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
    ALOGD("%s: stream_out(%p)",__func__, stream);
    if (adevice != nullptr) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device",__func__);
        return 0;
    }

    if (astream_out != nullptr) {
        return astream_out->GetChannels();
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
        return 0;
    }
}

static int astream_out_standby(struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device",__func__);
        return EINVAL;
    }

    if (astream_out) {
        return astream_out->Standby();
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
        return -EINVAL;
    }
}

static int astream_dump(const struct audio_stream *stream, int fd) {
    std::ignore = stream;
    std::ignore = fd;
    ALOGD("%s: dump function not implemented",__func__);
    return 0;
}

static uint32_t astream_get_latency(const struct audio_stream_out *stream) {
    std::ignore = stream;
    return LOW_LATENCY_OUTPUT_PERIOD_SIZE;
}

static int astream_out_get_presentation_position(
                               const struct audio_stream_out *stream,
                               uint64_t *frames, struct timespec *timestamp){
    std::ignore = stream;
    std::ignore = timestamp;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device",__func__);
        return -EINVAL;
    }

    if (astream_out) {
       *frames = astream_out->GetFramesWritten();
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
        return -EINVAL;
    }

    return 0;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames) {
    std::ignore = stream;
    std::ignore = dsp_frames;

    return 0;
}

static int astream_out_set_parameters(struct audio_stream *stream,
                                      const char *kvpairs) {
    int ret = 0;
    struct str_parms *parms;
    std::shared_ptr<AudioDevice> adevice = AudioDevice::getInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;
	if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ret = -EINVAL;
        ALOGE("%s: unable to get audio device",__func__);
        goto exit;
    }
    parms = str_parms_create_str(kvpairs);
    if (!parms) {
       ret = -EINVAL;
       goto exit;
    }
    if(astream_out->flags_ ==
            (AUDIO_OUTPUT_FLAG_DIRECT | AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD |
             AUDIO_OUTPUT_FLAG_NON_BLOCKING)) {
       ret = astream_out->SetParameters(parms);
       if (ret) {
          ALOGE("Stream SetParameters Error (%x)", ret);
          goto exit;
       }
    }
exit:
    return ret;
}

static char* astream_out_get_parameters(const struct audio_stream *stream,
                                        const char *keys) {
    std::ignore = stream;
    std::ignore = keys;
    ALOGD("%s: function not implemented keys: %s",__func__,keys);

    return 0;
}

static int astream_out_set_volume(struct audio_stream_out *stream,
                                  float left, float right) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamOutPrimary> astream_out;

    if (adevice) {
        astream_out = adevice->OutGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device",__func__);
        return -EINVAL;
    }

    if (astream_out) {
        return astream_out->SetVolume(left, right);
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
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
        ALOGE("%s: unable to get audio device",__func__);
        return -EINVAL;
    }

    if (astream_in) {
        return astream_in->Read(buffer, bytes);
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
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
        ALOGE("%s: unable to get audio device",__func__);
        return -EINVAL;
    }

    if (astream_out) {
        return astream_out->Write(buffer, bytes);
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
        return -EINVAL;
    }

    return 0;
}

static int astream_in_set_microphone_direction(
                        const struct audio_stream_in *stream,
                        audio_microphone_direction_t dir){
    std::ignore = stream;
    std::ignore = dir;
    ALOGD("%s: function not implemented",__func__);
    return 0;
}

static int in_set_microphone_field_dimension(
                        const struct audio_stream_in *stream,
                        float zoom) {
    std::ignore = stream;
    std::ignore = zoom;
    ALOGD("%s: function not implemented",__func__);
    return 0;
}

static int astream_in_add_audio_effect(
                                const struct audio_stream *stream,
                                effect_handle_t effect)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::getInstance();
    std::shared_ptr<StreamInPrimary> astream_in;
    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device",__func__);
        return -EINVAL;
    }
    if (astream_in) {
        return astream_in->addRemoveAudioEffect(stream, effect, true);
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
        return -EINVAL;
    }
}

static int astream_in_remove_audio_effect(const struct audio_stream *stream,
                                          effect_handle_t effect)
{
    std::shared_ptr<AudioDevice> adevice = AudioDevice::getInstance();
    std::shared_ptr<StreamInPrimary> astream_in;
    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device",__func__);
        return -EINVAL;
    }
    if (astream_in) {
        return astream_in->addRemoveAudioEffect(stream, effect, false);
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
        return -EINVAL;
    }
}

static int astream_in_get_capture_position(const struct audio_stream_in *stream,
                                           int64_t *frames, int64_t *time) {
    std::ignore = stream;
    std::ignore = frames;
    std::ignore = time;
    ALOGD("%s: position not implemented currently supported in qal",__func__);
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

    ALOGD("%s: sink meta data update not  supported in qal", __func__);
}

static int astream_in_get_active_microphones(
                        const struct audio_stream_in *stream,
                        struct audio_microphone_characteristic_t *mic_array,
                        size_t *mic_count) {
    std::ignore = stream;
    std::ignore = mic_array;
    std::ignore = mic_count;
    ALOGD("%s: get active mics not currently supported in qal",__func__);
    return 0;
}

static uint32_t astream_in_get_sample_rate(const struct audio_stream *stream) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device",__func__);
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
        ALOGE("%s: unable to get audio device",__func__);
        return 0;
    }

    if (astream_in) {
        return astream_in->GetChannels();
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
        return 0;
    }
}

static audio_format_t astream_in_get_format(const struct audio_stream *stream){
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice)
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    else
        ALOGE("%s: unable to get audio device",__func__);

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
        ALOGE("%s: unable to get audio device",__func__);
        return EINVAL;
    }

    if (astream_in) {
        return astream_in->Standby();
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
        return -EINVAL;
    }
}

static int astream_in_set_parameters(struct audio_stream *stream,
                                     const char *kvpairs) {
    std::ignore = stream;
    std::ignore = kvpairs;
    ALOGD("%s: function not implemented",__func__);
    return 0;
}

static char* astream_in_get_parameters(const struct audio_stream *stream,
                                       const char *keys) {
    std::ignore = stream;
    std::ignore = keys;
    ALOGD("%s: function not implemented",__func__);
    return 0;
}

static int astream_in_set_gain(struct audio_stream_in *stream, float gain) {
    std::shared_ptr<AudioDevice> adevice = AudioDevice::GetInstance();
    std::shared_ptr<StreamInPrimary> astream_in;

    if (adevice) {
        astream_in = adevice->InGetStream((audio_stream_t*)stream);
    } else {
        ALOGE("%s: unable to get audio device",__func__);
        return EINVAL;
    }

    if (astream_in) {
        return astream_in->SetGain(gain);
    } else {
        ALOGE("%s: unable to get audio stream",__func__);
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

qal_device_id_t StreamPrimary::GetQalDeviceId(audio_devices_t halDeviceId) {
    qal_device_id_t qalDeviceId = QAL_DEVICE_NONE;
    switch (halDeviceId) {
        case AUDIO_DEVICE_OUT_SPEAKER:
            qalDeviceId = QAL_DEVICE_OUT_SPEAKER;
            break;
        case AUDIO_DEVICE_OUT_EARPIECE:
            qalDeviceId = QAL_DEVICE_OUT_EARPIECE;
            break;
        case AUDIO_DEVICE_OUT_WIRED_HEADSET:
            qalDeviceId = QAL_DEVICE_OUT_WIRED_HEADSET;
            break;
        case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
            qalDeviceId = QAL_DEVICE_OUT_WIRED_HEADPHONE;
            break;
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
            qalDeviceId = QAL_DEVICE_OUT_BLUETOOTH_SCO;
            break;
        case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP:
            qalDeviceId = QAL_DEVICE_OUT_BLUETOOTH_A2DP;
            break;
        case AUDIO_DEVICE_OUT_HDMI:
            qalDeviceId = QAL_DEVICE_OUT_HDMI;
            break;
        case AUDIO_DEVICE_OUT_USB_DEVICE:
            qalDeviceId = QAL_DEVICE_OUT_USB_DEVICE;
            break;
        case AUDIO_DEVICE_OUT_LINE:
            qalDeviceId = QAL_DEVICE_OUT_LINE;
            break;
        case AUDIO_DEVICE_OUT_AUX_LINE:
            qalDeviceId = QAL_DEVICE_OUT_AUX_LINE;
            break;
        case AUDIO_DEVICE_OUT_PROXY:
            qalDeviceId = QAL_DEVICE_OUT_PROXY;
            break;
        case AUDIO_DEVICE_OUT_USB_HEADSET:
            qalDeviceId = QAL_DEVICE_OUT_USB_HEADSET;
            break;
        case AUDIO_DEVICE_OUT_FM:
            qalDeviceId = QAL_DEVICE_OUT_FM;
            break;
        case AUDIO_DEVICE_IN_BUILTIN_MIC:
            qalDeviceId = QAL_DEVICE_IN_HANDSET_MIC;
            break;
        case AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET:
            qalDeviceId = QAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
            break;
        case AUDIO_DEVICE_IN_WIRED_HEADSET:
            qalDeviceId = QAL_DEVICE_IN_WIRED_HEADSET;
            break;
        case AUDIO_DEVICE_IN_HDMI:
            qalDeviceId =QAL_DEVICE_IN_HDMI;
            break;
        case AUDIO_DEVICE_IN_BACK_MIC:
            qalDeviceId = QAL_DEVICE_IN_SPEAKER_MIC;
            break;
        case AUDIO_DEVICE_IN_USB_ACCESSORY:
            qalDeviceId = QAL_DEVICE_IN_USB_ACCESSORY;
            break;
        case AUDIO_DEVICE_IN_USB_DEVICE:
            qalDeviceId = QAL_DEVICE_IN_USB_DEVICE;
            break;
        case AUDIO_DEVICE_IN_FM_TUNER:
            qalDeviceId = QAL_DEVICE_IN_FM_TUNER;
            break;
        case AUDIO_DEVICE_IN_LINE:
            qalDeviceId = QAL_DEVICE_IN_LINE;
            break;
        case AUDIO_DEVICE_IN_SPDIF:
            qalDeviceId = QAL_DEVICE_IN_SPDIF;
            break;
        case AUDIO_DEVICE_IN_PROXY:
            qalDeviceId = QAL_DEVICE_IN_PROXY;
            break;
        case AUDIO_DEVICE_IN_USB_HEADSET:
            qalDeviceId = QAL_DEVICE_IN_USB_HEADSET;
            break;
        default:
            qalDeviceId = QAL_DEVICE_NONE;
            ALOGE("%s: unsupported Device Id of %d\n", __func__, halDeviceId);
            break;
     }

     return qalDeviceId;
}

qal_stream_type_t StreamInPrimary::GetQalStreamType(
                                        audio_input_flags_t halStreamFlags) {
    qal_stream_type_t qalStreamType = QAL_STREAM_LOW_LATENCY;
    switch (halStreamFlags) {
        case AUDIO_INPUT_FLAG_FAST:
        case AUDIO_INPUT_FLAG_MMAP_NOIRQ:
            qalStreamType = QAL_STREAM_LOW_LATENCY;
            break;
        case AUDIO_INPUT_FLAG_RAW:
        case AUDIO_INPUT_FLAG_DIRECT:
            qalStreamType = QAL_STREAM_RAW;
            break;
        case AUDIO_INPUT_FLAG_VOIP_TX:
            qalStreamType = QAL_STREAM_VOIP_TX;
            break;
        default:
            /*
            unsupported from QAL
            AUDIO_INPUT_FLAG_NONE        = 0x0,
            AUDIO_INPUT_FLAG_HW_HOTWORD = 0x2,
            AUDIO_INPUT_FLAG_SYNC        = 0x8,
            AUDIO_INPUT_FLAG_HW_AV_SYNC = 0x40,
            */
            ALOGE("%s: flag %#x is not supported from QAL.\n" ,
                      __func__, halStreamFlags);
            break;
    }

    return qalStreamType;
}

qal_stream_type_t StreamOutPrimary::GetQalStreamType(
                                    audio_output_flags_t halStreamFlags) {
    qal_stream_type_t qalStreamType = QAL_STREAM_LOW_LATENCY;

    if (halStreamFlags == (AUDIO_OUTPUT_FLAG_FAST|AUDIO_OUTPUT_FLAG_PRIMARY)) {
        qalStreamType = QAL_STREAM_DEEP_BUFFER;
    } else if (halStreamFlags ==
                    (AUDIO_OUTPUT_FLAG_FAST|AUDIO_OUTPUT_FLAG_RAW)) {
        qalStreamType = QAL_STREAM_RAW;
    } else if (halStreamFlags == AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
        qalStreamType = QAL_STREAM_DEEP_BUFFER;
    } else if (halStreamFlags ==
                    (AUDIO_OUTPUT_FLAG_DIRECT|AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
        // mmap_no_irq_out: to be confirmed
        qalStreamType = QAL_STREAM_LOW_LATENCY;
    } else if (halStreamFlags == (AUDIO_OUTPUT_FLAG_DIRECT|
                                      AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|
                                  AUDIO_OUTPUT_FLAG_NON_BLOCKING)) {
        // hifi: to be confirmed
        qalStreamType = QAL_STREAM_COMPRESSED;
    } else if (halStreamFlags == AUDIO_OUTPUT_FLAG_DIRECT) {
        // low latency for now as a workaround
        qalStreamType = QAL_STREAM_LOW_LATENCY;//QAL_STREAM_COMPRESSED
    } else if (halStreamFlags == (AUDIO_OUTPUT_FLAG_DIRECT|
                                      AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|
                                  AUDIO_OUTPUT_FLAG_NON_BLOCKING)) {
        // low latency for now
        qalStreamType = QAL_STREAM_COMPRESSED;
    } else if (halStreamFlags == (AUDIO_OUTPUT_FLAG_DIRECT|
                                      AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|
                                  AUDIO_OUTPUT_FLAG_NON_BLOCKING)) {
        // dsd_compress_passthrough
        qalStreamType = QAL_STREAM_COMPRESSED;
    } else if (halStreamFlags == (AUDIO_OUTPUT_FLAG_DIRECT|
                                      AUDIO_OUTPUT_FLAG_VOIP_RX)) {
        // voice rx
        qalStreamType = QAL_STREAM_VOICE_CALL_RX;
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

    return ret;
}

int StreamOutPrimary::Standby() {
    int ret = 0;

    if (qal_stream_handle_) {
        ret = qal_stream_stop(qal_stream_handle_);
        if (ret) {
            ALOGE("%s: failed to stop stream.\n", __func__);
            return -EINVAL;
        }
    }

    stream_started_ = false;
    if (streamAttributes_.type == QAL_STREAM_COMPRESSED)
        ret = StopOffloadEffects(handle_, qal_stream_handle_);

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
   int ret = -EINVAL;
   ALOGE("%s: g\n", __func__);

   ret = AudioExtn::audio_extn_parse_compress_metadata(&config_, &qparam_payload, parms, &msample_rate, &mchannels);
   if (ret) {
      ALOGE("parse_compress_metadata Error (%x)", ret);
   }
   ALOGE("%s: exit %d\n", __func__, ret);
   return ret;
}

int StreamOutPrimary::SetVolume(float left , float right) {
    if (!qal_stream_handle_) {
        ALOGE("%s: handle is null. abort\n", __func__);
        return 0;
    }
    if (flags_ == (AUDIO_OUTPUT_FLAG_DIRECT|AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_NON_BLOCKING)) {
       return 0;
    }
    struct qal_volume_data* volume;
    int ret = 0;
    if (left == right) {
        volume = (struct qal_volume_data*)malloc(sizeof(struct qal_volume_data)
                    +sizeof(struct qal_channel_vol_kv));
        volume->no_of_volpair = 1;
        volume->volume_pair[0].channel_mask = 0x03;
        volume->volume_pair[0].vol = left;
        ret = qal_stream_set_volume(qal_stream_handle_, volume);
    } else {
        volume = (struct qal_volume_data*)malloc(sizeof(struct qal_volume_data)
                    +sizeof(struct qal_channel_vol_kv) * 2);
        volume->no_of_volpair = 2;
        volume->volume_pair[0].channel_mask = 0x01;
        volume->volume_pair[0].vol = left;
        volume->volume_pair[1].channel_mask = 0x10;
        volume->volume_pair[1].vol = right;
        ret = qal_stream_set_volume(qal_stream_handle_, volume);
    }

    free(volume);
    if (ret) {
        ALOGE("Qal Stream volume Error (%x)", ret);
        return -EINVAL;
    }
    return ret;
}

int StreamOutPrimary::GetFramesWritten() {
    return total_bytes_written_/audio_bytes_per_frame(
        audio_channel_mask_get_bits(config_.channel_mask), config_.format);
}

int StreamOutPrimary::Open() {
    int ret = -EINVAL;

    uint8_t channels = 0;
    struct qal_device qalDevice;
    struct qal_channel_info *ch_info;

    channels = audio_channel_count_from_out_mask(config_.channel_mask);
    ch_info = (struct qal_channel_info *)calloc(
                            1, sizeof(uint16_t) + sizeof(uint8_t)*channels);
    if (ch_info == NULL) {
      ALOGE("Allocation failed for channel map");
      ret = -ENOMEM;
      goto error_open;
    }

    ch_info->channels = channels;
    ch_info->ch_map[0] = QAL_CHMAP_CHANNEL_FL;
    if (ch_info->channels > 1 )
      ch_info->ch_map[1] = QAL_CHMAP_CHANNEL_FR;

    qalDevice.id = qal_device_id_;
    qalDevice.config.sample_rate = config_.sample_rate;
    qalDevice.config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    qalDevice.config.ch_info = ch_info;
    qalDevice.config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM;
    streamAttributes_.type = StreamOutPrimary::GetQalStreamType(flags_);
    streamAttributes_.flags = (qal_stream_flags_t)flags_;
    streamAttributes_.direction = QAL_AUDIO_OUTPUT;
    streamAttributes_.out_media_config.sample_rate = config_.sample_rate;
    streamAttributes_.out_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    streamAttributes_.out_media_config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM;
    streamAttributes_.out_media_config.ch_info = ch_info;

    if (streamAttributes.type == QAL_STREAM_COMPRESSED) {
       streamAttributes.flags = (qal_stream_flags_t)(1 << QAL_STREAM_FLAG_NON_BLOCKING);
       if (config_.offload_info.format == 0)
          config_.offload_info.format = config_.format;
       if (config_.offload_info.sample_rate == 0)
          config_.offload_info.sample_rate = config_.sample_rate;
       streamAttributes.out_media_config.sample_rate = config_.offload_info.sample_rate;
       if (msample_rate)
          streamAttributes.out_media_config.sample_rate = msample_rate;
       if (mchannels)
          streamAttributes.out_media_config.ch_info->channels = mchannels;
       streamAttributes.out_media_config.aud_fmt_id = getFormatId.at(config_.format);
    }
    ALOGE("channels %d samplerate %d format id %d \n",
            streamAttributes.out_media_config.ch_info->channels,
            streamAttributes.out_media_config.sample_rate,
          streamAttributes.out_media_config.aud_fmt_id);
    ALOGE("chanels %d \n", streamAttributes.out_media_config.ch_info->channels);
    ALOGE("msample_rate %d mchannels %d \n", msample_rate, mchannels);
    ret = qal_stream_open (&streamAttributes,
                          1,
                          &qalDevice,
                          0,
                          NULL,
                          &qal_callback,
                          (void *)this,
                          &qal_stream_handle_);

    ALOGD("%s:(%x:ret)%d",__func__,ret, __LINE__);
    if (ret) {
        ALOGE("Qal Stream Open Error (%x)", ret);
        ret = -EINVAL;
    }
    if (streamAttributes.type == QAL_STREAM_COMPRESSED) {
       ret = qal_stream_set_param(qal_stream_handle_, 0, &qparam_payload);
       if (ret) {
          ALOGE("Qal Set Param Error (%x)\n", ret);
       }
    }
    total_bytes_written_ = 0; // reset at each open

error_open:
    if (ch_info)
        free(ch_info);
    return ret;
}

uint32_t StreamOutPrimary::GetBufferSize() {
    return BUF_SIZE_PLAYBACK * NO_OF_BUF;
}

ssize_t StreamOutPrimary::Write(const void *buffer, size_t bytes){
    int ret = 0;
    struct qal_buffer qalBuffer;
    int local_bytes_written = 0;

    qalBuffer.buffer = (void*)buffer;
    qalBuffer.size = bytes;
    qalBuffer.offset = 0;

    ALOGD("%s: handle_ %x Bytes:(%zu)",__func__,handle_, bytes);
    if (!qal_stream_handle_){
        ret = Open();
        if (ret) {
            ALOGE("%s: failed to open stream.\n", __func__);
            return -EINVAL;
        }
    }

    if (!stream_started_) {
        ret = qal_stream_start(qal_stream_handle_);
        if (ret) {
            ALOGE("%s:failed to start stream. ret=%d\n", __func__, ret);
            qal_stream_close(qal_stream_handle_);
            qal_stream_handle_ = NULL;
            return -EINVAL;
        }

        stream_started_ = true;
        if (streamAttributes_.type == QAL_STREAM_COMPRESSED)
            ret = StartOffloadEffects(handle_, qal_stream_handle_);
    }

    local_bytes_written = qal_stream_write(qal_stream_handle_, &qalBuffer);
    total_bytes_written_ += local_bytes_written;

    return local_bytes_written;
}

int StreamOutPrimary::StartOffloadEffects(
                                    audio_io_handle_t ioHandle,
                                    qal_stream_handle_t* qal_stream_handle) {
    int ret  = 0;
    if (fnp_offload_effect_start_output_) {
        ret = fnp_offload_effect_start_output_(ioHandle, qal_stream_handle);
        if (ret) {
            ALOGE("%s: failed to start offload effect.\n", __func__);
        }
    } else {
        ALOGE("%s: function pointer is null.\n", __func__);
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
            ALOGE("%s: failed to start offload effect.\n", __func__);
        }
    } else {
        ALOGE("%s: function pointer is null.\n", __func__);
        return -EINVAL;
    }

    return ret;
}

StreamOutPrimary::StreamOutPrimary(
                        audio_io_handle_t handle,
                        audio_devices_t devices,
                        audio_output_flags_t flags,
                        struct audio_config *config,
                        const char *address,
                        offload_effects_start_output start_offload_effect,
                        offload_effects_stop_output stop_offload_effect) {

    stream_ = std::shared_ptr<audio_stream_out> (new audio_stream_out);
    qal_stream_handle_ = NULL;
    if (!stream_) {
        ALOGE("%s: No memory allocated for stream_",__func__);
    }

    handle_ = handle;
    qal_device_id_ = GetQalDeviceId(devices);
    flags_ = flags;

    if (config) {
        ALOGD("%s: enter: handle (%x) format(%#x) sample_rate(%d)\
            channel_mask(%#x) devices(%#x) flags(%#x) address(%s)",
            __func__, handle, config->format, config->sample_rate,
            config->channel_mask, devices, flags, address);
        memcpy(&config_, config, sizeof(struct audio_config));
    } else {
        ALOGD("%s: enter: devices(%#x) flags(%#x)", __func__,devices, flags);
    }

    if (address) {
        strlcpy((char *)&address_, address, AUDIO_DEVICE_MAX_ADDRESS_LEN);
    } else {
        ALOGD("%s: invalid address", __func__);
    }

    fnp_offload_effect_start_output_ = start_offload_effect;
    fnp_offload_effect_stop_output_ = stop_offload_effect;

    (void)FillHalFnPtrs();
}

StreamOutPrimary::~StreamOutPrimary() {
    ALOGD("%s: close stream, handle(%x), qal_stream_handle (%p)", __func__,
          handle_, qal_stream_handle_);

    if (streamAttributes_.type == QAL_STREAM_COMPRESSED)
        StopOffloadEffects(handle_, qal_stream_handle_);

    if (qal_stream_handle_) {
        qal_stream_close(qal_stream_handle_);
        qal_stream_handle_ = nullptr;
    }
}

int StreamInPrimary::Standby() {
    int ret = 0;

    if (is_st_session && is_st_session_active) {
        audio_extn_sound_trigger_stop_lab(this);
        return ret;
    }

    if (qal_stream_handle_) {
        ret = qal_stream_stop(qal_stream_handle_);
    }

    stream_started_ = false;

    if (qal_stream_handle_) {
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
                    status  = -EINVAL;
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
                        status = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_EC,false);
                        isECEnabled = false;
                        goto exit;
                    }
                } else {
                    ALOGE("%s: EC is not enabled", __func__);
                    status = -EINVAL;
                    goto exit;
               }
            }
        }

        if (memcmp(&desc.type, FX_IID_NS, sizeof(effect_uuid_t)) == 0) {
            if (enable) {
                if (isNSEnabled) {
                    ALOGE("%s: NS already enabled", __func__);
                    status  = -EINVAL;
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
                        status = qal_add_remove_effect(qal_stream_handle_,QAL_AUDIO_EFFECT_NS,false);
                        isNSEnabled = false;
                        goto exit;
                    }
                } else {
                    ALOGE("%s: NS is not enabled", __func__);
                    status = -EINVAL;
                    goto exit;
               }
            }
        }
    }
exit:
    return status;
}


int StreamInPrimary::SetGain(float gain) {
    struct qal_volume_data* volume;
    int ret = 0;

    volume = (struct qal_volume_data*)malloc(sizeof(uint32_t)
                +sizeof(struct qal_channel_vol_kv));
    volume->no_of_volpair = 1;
    volume->volume_pair[0].channel_mask = 0x03;
    volume->volume_pair[0].vol = gain;
    ret = qal_stream_set_volume(&qal_stream_handle_, volume);

    free(volume);
    if (ret) {
        ALOGE("Qal Stream volume Error (%x)", ret);
    }

    return ret;
}

int StreamInPrimary::Open() {
    int ret = -EINVAL;
    struct qal_stream_attributes streamAttributes;
    uint8_t channels = 0;
    struct qal_device qalDevice;
    struct qal_channel_info *ch_info =
        (struct qal_channel_info *)calloc(
                1, sizeof(uint16_t) + sizeof(uint8_t)*channels);
    if (ch_info == NULL) {
        ALOGE("Allocation failed for channel map");
        ret = -ENOMEM;
        goto error_open;
    }

    audio_extn_sound_trigger_check_and_get_session(this);
    if (is_st_session) {
        return 0;
    }

    channels = audio_channel_count_from_out_mask(config_.channel_mask);
    ch_info->channels = channels;
    ch_info->ch_map[0] = QAL_CHMAP_CHANNEL_FL;
    if (ch_info->channels > 1 )
      ch_info->ch_map[1] = QAL_CHMAP_CHANNEL_FR;

    qalDevice.id = QAL_DEVICE_IN_HANDSET_MIC;
    qalDevice.config.sample_rate = config_.sample_rate;
    qalDevice.config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    qalDevice.config.ch_info = ch_info;
    qalDevice.config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM;

    streamAttributes.type = QAL_STREAM_LOW_LATENCY;
    streamAttributes_.flags = (qal_stream_flags_t)0;
    streamAttributes_.direction = QAL_AUDIO_INPUT;
    streamAttributes_.in_media_config.sample_rate = config_.sample_rate;
    streamAttributes_.in_media_config.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    streamAttributes_.in_media_config.aud_fmt_id = QAL_AUDIO_FMT_DEFAULT_PCM;
    streamAttributes_.in_media_config.ch_info = ch_info;
    ALOGD("%s:(%x:ret)%d",__func__,ret, __LINE__);
    ret = qal_stream_open(&streamAttributes_,
                          1,
                          &qalDevice,
                          0,
                          NULL,
                          &qal_callback,
                          (void *)this,
                          &qal_stream_handle_);

    ALOGD("%s:(%x:ret)%d",__func__,ret, __LINE__);

    if (ret) {
        ALOGE("Qal Stream Open Error (%x)", ret);
        ret = -EINVAL;
    }

    total_bytes_read_ = 0; // reset at each open

error_open:
    if (ch_info)
        free(ch_info);
    return ret;
}


uint32_t StreamInPrimary::GetBufferSize() {
    return BUF_SIZE_CAPTURE * NO_OF_BUF;
}

ssize_t StreamInPrimary::Read(const void *buffer, size_t bytes){
    int ret = 0;
    struct qal_buffer qalBuffer;
    uint32_t local_bytes_read = 0;
    qalBuffer.buffer = (void*)buffer;
    qalBuffer.size = bytes;
    qalBuffer.offset = 0;

    ALOGD("%s: Bytes:(%zu)",__func__,bytes);
    if (!qal_stream_handle_){
        ret = Open();
    }

    if (is_st_session) {
        audio_extn_sound_trigger_read(this, (void *)buffer, bytes);
        return bytes;
    }

    if (!stream_started_) {
        ret = qal_stream_start(qal_stream_handle_);
        stream_started_ = true;
    }

    local_bytes_read = qal_stream_read(qal_stream_handle_, &qalBuffer);
    total_bytes_read_ += local_bytes_read;

    return local_bytes_read;
}

StreamInPrimary::StreamInPrimary(audio_io_handle_t handle,
                                 audio_devices_t devices,
                                 audio_input_flags_t flags,
                                 struct audio_config *config,
                                 audio_source_t source) {

    stream_ = std::shared_ptr<audio_stream_in> (new audio_stream_in);
    qal_stream_handle_ = NULL;

    if (config) {
        ALOGD("%s: enter: handle (%x) format(%#x) sample_rate(%d)\
            channel_mask(%#x) devices(%#x) flags(%#x)", __func__,
            handle, config->format, config->sample_rate, config->channel_mask,
            devices, flags);
        memcpy(&config_, config, sizeof(struct audio_config));
    } else {
        ALOGD("%s: enter: devices(%#x) flags(%#x)", __func__,devices, flags);
    }

    handle_ = handle;
    qal_device_id_ = GetQalDeviceId(devices);
    flags_ = flags;
    source_ = source;
    config_ = *config;

    (void)FillHalFnPtrs();
}

StreamInPrimary::~StreamInPrimary() {
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

