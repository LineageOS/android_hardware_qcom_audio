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

#ifndef ANDROID_HARDWARE_AHAL_ASTREAM_H_
#define ANDROID_HARDWARE_AHAL_ASTREAM_H_

#include <stdlib.h>
#include <unistd.h>

#include <vector>

#include <cutils/properties.h>
#include <hardware/audio.h>
#include <system/audio.h>

#include "QalDefs.h"
#include <audio_extn/AudioExtn.h>
#include <mutex>
#include <map>

#define BUF_SIZE_PLAYBACK 1024
#define BUF_SIZE_CAPTURE 960
#define NO_OF_BUF 4
#define LOW_LATENCY_CAPTURE_SAMPLE_RATE 48000
#define LOW_LATENCY_CAPTURE_PERIOD_SIZE 240
#define LOW_LATENCY_OUTPUT_PERIOD_SIZE 240
#define LOW_LATENCY_CAPTURE_USE_CASE 1
#define MMAP_PERIOD_SIZE (DEFAULT_OUTPUT_SAMPLING_RATE/1000)
#define MMAP_PERIOD_COUNT_MIN 32
#define MMAP_PERIOD_COUNT_MAX 512
#define MMAP_PERIOD_COUNT_DEFAULT (MMAP_PERIOD_COUNT_MAX)
#define CODEC_BACKEND_DEFAULT_BIT_WIDTH 16

#if LINUX_ENABLED
#if defined(__LP64__)
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/usr/lib64/libqcompostprocbundle.so"
#else
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/usr/lib/libqcompostprocbundle.so"
#endif
#else
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/vendor/lib/soundfx/libqcompostprocbundle.so"
#endif

extern "C" typedef void (*hello_t)( const char* text );
extern "C" typedef int (*offload_effects_start_output)(audio_io_handle_t,
                                                       qal_stream_handle_t*);
extern "C" typedef int (*offload_effects_stop_output)(audio_io_handle_t,
                                                      qal_stream_handle_t*);

int adev_open(audio_hw_device_t **device);

class AudioDevice;

class StreamPrimary {
public:
    uint32_t GetSampleRate();
    uint32_t GetBufferSize();
    audio_format_t GetFormat();
    uint32_t GetChannels();
    static qal_device_id_t GetQalDeviceId(audio_devices_t halDeviceId);
    audio_io_handle_t GetHandle();
    std::mutex write_wait_mutex_;
    std::condition_variable write_condition_;
    bool write_ready_;

    std::mutex drain_wait_mutex_;
    std::condition_variable drain_condition_;
    bool drain_ready_;
    stream_callback_t client_callback;
    void *client_cookie;

protected:
    struct qal_stream_attributes streamAttributes;
    qal_stream_handle_t* qal_stream_handle_;
    audio_io_handle_t handle_;
    qal_device_id_t qal_device_id_;
    struct audio_config config_;
    char address_[AUDIO_DEVICE_MAX_ADDRESS_LEN];
    bool stream_started_;
};

class StreamOutPrimary : public StreamPrimary {
public:
    StreamOutPrimary(audio_io_handle_t handle,
                     audio_devices_t devices,
                     audio_output_flags_t flags,
                     struct audio_config *config,
                     const char *address,
                     offload_effects_start_output fnp_start_offload_effect,
                     offload_effects_stop_output fnp_stop_offlod_effect);

    ~StreamOutPrimary();
    int Standby();
    int SetVolume(float left, float right);
    int SetParameters(struct str_parms *parms);
    int GetFramesWritten();
    int Pause();
    int Resume();
    int Drain(audio_drain_type_t type);
    int Flush();
    ssize_t Write(const void *buffer, size_t bytes);
    int Open();
    void GetStreamHandle(audio_stream_out** stream);
    uint32_t GetBufferSize();
    int GetTimestamp(uint64_t *timestp);
    static qal_stream_type_t GetQalStreamType(audio_output_flags_t halStreamFlags);
    int StartOffloadEffects(audio_io_handle_t, qal_stream_handle_t*);
    int StopOffloadEffects(audio_io_handle_t, qal_stream_handle_t*);
    audio_output_flags_t flags_;
protected:
    qal_param_payload qparam_payload;
    uint32_t msample_rate;
    uint16_t mchannels;
    std::shared_ptr<audio_stream_out>   stream_;
    uint64_t total_bytes_written_; /* total frames written, not cleared when entering standby */
    offload_effects_start_output fnp_offload_effect_start_output_ = nullptr;
    offload_effects_stop_output fnp_offload_effect_stop_output_ = nullptr;
    int FillHalFnPtrs();
    friend class AudioDevice;
};

class StreamInPrimary : public StreamPrimary{
public:
    StreamInPrimary(audio_io_handle_t handle,
                    audio_devices_t devices,
                    audio_input_flags_t flags,
                    struct audio_config *config,
                    audio_source_t source);

    ~StreamInPrimary();
    int Standby();
    int SetGain(float gain);
    void GetStreamHandle(audio_stream_in** stream);
    int Open();
    ssize_t Read(const void *buffer, size_t bytes);
    uint32_t GetBufferSize();
    static qal_stream_type_t GetQalStreamType(audio_input_flags_t halStreamFlags);
    int addRemoveAudioEffect(const struct audio_stream *stream, effect_handle_t effect,bool enable);
    bool is_st_session;
    bool is_st_session_active;
protected:
    audio_input_flags_t flags_;
    std::shared_ptr<audio_stream_in> stream_;
    audio_source_t source_;
    friend class AudioDevice;
    uint64_t total_bytes_read_; /* total frames written, not cleared when entering standby */
    int FillHalFnPtrs();
    bool isECEnabled = false;
    bool isNSEnabled = false;
};
#endif  // ANDROID_HARDWARE_AHAL_ASTREAM_H_
