/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "qcom_audio_hw_hal"
//#define LOG_NDEBUG 0

#include <stdint.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <hardware/audio_amplifier.h>

#include <hardware_legacy/AudioHardwareInterface.h>
#include <hardware_legacy/AudioSystemLegacy.h>

namespace android_audio_legacy {

extern "C" {

struct qcom_audio_module {
    struct audio_module module;
};

struct qcom_audio_device {
    struct audio_hw_device device;

    struct AudioHardwareInterface *hwif;

    amplifier_device_t *amp;
};

struct qcom_stream_out {
    struct audio_stream_out stream;

    AudioStreamOut *qcom_out;
};

struct qcom_stream_in {
    struct audio_stream_in stream;

    AudioStreamIn *qcom_in;
};


enum {
    HAL_API_REV_1_0,
    HAL_API_REV_2_0,
    HAL_API_REV_NUM
} hal_api_rev;

static struct qcom_audio_device *qadev = NULL;

static uint32_t audio_device_conv_table[][HAL_API_REV_NUM] =
{
        /* output devices */
    { AudioSystem::DEVICE_OUT_EARPIECE, AUDIO_DEVICE_OUT_EARPIECE },
    { AudioSystem::DEVICE_OUT_SPEAKER, AUDIO_DEVICE_OUT_SPEAKER },
    { AudioSystem::DEVICE_OUT_WIRED_HEADSET, AUDIO_DEVICE_OUT_WIRED_HEADSET },
    { AudioSystem::DEVICE_OUT_WIRED_HEADPHONE, AUDIO_DEVICE_OUT_WIRED_HEADPHONE },
    { AudioSystem::DEVICE_OUT_BLUETOOTH_SCO, AUDIO_DEVICE_OUT_BLUETOOTH_SCO },
    { AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET, AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET },
    { AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT, AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT },
    { AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP, AUDIO_DEVICE_OUT_BLUETOOTH_A2DP },
    { AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES, AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES },
    { AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER, AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER },
    { AudioSystem::DEVICE_OUT_AUX_DIGITAL, AUDIO_DEVICE_OUT_AUX_DIGITAL },
#ifdef QCOM_USBAUDIO_ENABLED
    { AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET, AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET },
    { AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET, AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET },
    { AudioSystem::DEVICE_OUT_USB_ACCESSORY, AUDIO_DEVICE_OUT_USB_ACCESSORY },
    { AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET, AUDIO_DEVICE_OUT_USB_DEVICE },
#endif
#ifdef QCOM_PROXY_DEVICE_ENABLED
    { AudioSystem::DEVICE_OUT_PROXY, AUDIO_DEVICE_OUT_PROXY },
#endif
#ifdef QCOM_FM_ENABLED
    { AudioSystem::DEVICE_OUT_FM, AUDIO_DEVICE_OUT_FM },
    { AudioSystem::DEVICE_OUT_FM_TX, AUDIO_DEVICE_OUT_FM_TX },
#endif
    { AudioSystem::DEVICE_OUT_DEFAULT, AUDIO_DEVICE_OUT_DEFAULT },
    /* input devices */
    { AudioSystem::DEVICE_IN_COMMUNICATION, AUDIO_DEVICE_IN_COMMUNICATION },
    { AudioSystem::DEVICE_IN_AMBIENT, AUDIO_DEVICE_IN_AMBIENT },
    { AudioSystem::DEVICE_IN_BUILTIN_MIC, AUDIO_DEVICE_IN_BUILTIN_MIC },
    { AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET, AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET },
    { AudioSystem::DEVICE_IN_WIRED_HEADSET, AUDIO_DEVICE_IN_WIRED_HEADSET },
    { AudioSystem::DEVICE_IN_AUX_DIGITAL, AUDIO_DEVICE_IN_AUX_DIGITAL },
    { AudioSystem::DEVICE_IN_VOICE_CALL, AUDIO_DEVICE_IN_VOICE_CALL },
    { AudioSystem::DEVICE_IN_BACK_MIC, AUDIO_DEVICE_IN_BACK_MIC },
#ifdef QCOM_USBAUDIO_ENABLED
    { AudioSystem::DEVICE_IN_ANLG_DOCK_HEADSET, AUDIO_DEVICE_IN_USB_DEVICE },
#endif
#ifdef QCOM_PROXY_DEVICE_ENABLED
    { AudioSystem::DEVICE_IN_PROXY, AUDIO_DEVICE_IN_PROXY },
#endif
#ifdef QCOM_FM_ENABLED
    { AudioSystem::DEVICE_IN_FM_RX, AUDIO_DEVICE_IN_FM_RX },
    { AudioSystem::DEVICE_IN_FM_RX_A2DP, AUDIO_DEVICE_IN_FM_RX_A2DP },
#endif
    { AudioSystem::DEVICE_IN_DEFAULT, AUDIO_DEVICE_IN_DEFAULT },
};

// the "audio_devices" enumeration defined in hardware/libhardware_legacy is obsolete,
// use type "audio_devices_t" and audio device enumeration from system/audio.h instead.
// Do not use convert_audio_device if audio hal uses device definition from system/core
// There's no need to conver audio device if HAL uses AUDIO_DEVICE_XXX defintions instead
// of AudioSystem::Device_XXX.
static uint32_t convert_audio_device(uint32_t from_device, int from_rev, int to_rev)
{
    const uint32_t k_num_devices = sizeof(audio_device_conv_table)/sizeof(uint32_t)/HAL_API_REV_NUM;
    uint32_t to_device =  AUDIO_DEVICE_NONE;
    uint32_t in_bit = 0;

    if (from_rev != HAL_API_REV_1_0) {
        in_bit = from_device & AUDIO_DEVICE_BIT_IN;
        from_device &= ~AUDIO_DEVICE_BIT_IN;
    }

    while (from_device) {
        uint32_t i = 31 - __builtin_clz(from_device);
        uint32_t cur_device = (1 << i) | in_bit;

        for (i = 0; i < k_num_devices; i++) {
            if (audio_device_conv_table[i][from_rev] == cur_device) {
                to_device |= audio_device_conv_table[i][to_rev];
                break;
            }
        }
        from_device &= ~cur_device;
    }
    return to_device;
}

static amplifier_device_t * get_amplifier_device(void)
{
    int rc;
    amplifier_module_t *module;

    if (qadev->amp)
        return qadev->amp;

    rc = hw_get_module(AMPLIFIER_HARDWARE_MODULE_ID,
            (const hw_module_t **) &module);
    if (rc) {
        ALOGV("%s: Failed to obtain reference to amplifier module: %s\n",
                __func__, strerror(-rc));
        return NULL;
    }

    rc = amplifier_device_open((const hw_module_t *) module, &qadev->amp);
    if (rc) {
        ALOGV("%s: Failed to open amplifier hardware device: %s\n",
                __func__, strerror(-rc));
        return NULL;
    }

    return qadev->amp;
}

static int amplifier_open(void)
{
    amplifier_device_t *amp = get_amplifier_device();

    if (!amp) {
        return -ENODEV;
    }

    return 0;
}

/* Must expose for ALSADevice.cpp */
int amplifier_set_devices(uint32_t devices)
{
    amplifier_device_t *amp = get_amplifier_device();
    bool is_output = devices & AudioSystem::DEVICE_OUT_ALL;

    if (amp && amp->set_output_devices && is_output) {
        amp->set_output_devices(amp, devices);
    }

    if (amp && amp->set_input_devices && !is_output) {
        amp->set_input_devices(amp, devices);
    }

    return 0;
}

/* Must expose for ALSADevice.cpp */
int amplifier_enable_devices(uint32_t devices, bool enable)
{
    amplifier_device_t *amp = get_amplifier_device();
    bool is_output = devices & AudioSystem::DEVICE_OUT_ALL;

    if (amp && amp->enable_output_devices && is_output) {
        amp->enable_output_devices(amp, devices, enable);
    }

    if (amp && amp->enable_input_devices && !is_output) {
        amp->enable_input_devices(amp, devices, enable);
    }

    return 0;
}

static int amplifier_set_mode(audio_mode_t mode)
{
    amplifier_device_t *amp = get_amplifier_device();
    if (amp && amp->set_mode)
        return amp->set_mode(amp, mode);

    return 0;
}

static int amplifier_output_stream_start(struct audio_stream_out *stream,
        bool offload)
{
    amplifier_device_t *amp = get_amplifier_device();
    if (amp && amp->output_stream_start)
        return amp->output_stream_start(amp, stream, offload);

    return 0;
}

static int amplifier_input_stream_start(struct audio_stream_in *stream)
{
    amplifier_device_t *amp = get_amplifier_device();
    if (amp && amp->input_stream_start)
        return amp->input_stream_start(amp, stream);

    return 0;
}

static int amplifier_output_stream_standby(struct audio_stream_out *stream)
{
    amplifier_device_t *amp = get_amplifier_device();
    if (amp && amp->output_stream_standby)
        return amp->output_stream_standby(amp, stream);

    return 0;
}

static int amplifier_input_stream_standby(struct audio_stream_in *stream)
{
    amplifier_device_t *amp = get_amplifier_device();
    if (amp && amp->input_stream_standby)
        return amp->input_stream_standby(amp, stream);

    return 0;
}

static int amplifier_close(void)
{
    amplifier_device_t *amp = get_amplifier_device();
    if (amp)
        amplifier_device_close(amp);

    return 0;
}

/** audio_stream_out implementation **/
static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->sampleRate();
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);

    ALOGV("(%s:%d) %s: Implement me!", __FILE__, __LINE__, __func__);
    /* TODO: implement this */
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->bufferSize();
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->channels();
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return (audio_format_t)out->qcom_out->format();
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    ALOGV("(%s:%d) %s: Implement me!", __FILE__, __LINE__, __func__);
    /* TODO: implement me */
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    amplifier_output_stream_standby((struct audio_stream_out *)stream);
    return out->qcom_out->standby();
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    Vector<String16> args;
    return out->qcom_out->dump(fd, args);
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    int val;
    String8 s8 = String8(kvpairs);
    AudioParameter parms = AudioParameter(String8(kvpairs));

    if (parms.getInt(String8(AUDIO_PARAMETER_STREAM_ROUTING), val) == NO_ERROR) {
        val = convert_audio_device(val, HAL_API_REV_2_0, HAL_API_REV_1_0);
        parms.remove(String8(AUDIO_PARAMETER_STREAM_ROUTING));
        parms.addInt(String8(AUDIO_PARAMETER_STREAM_ROUTING), val);
        s8 = parms.toString();
    }

    return out->qcom_out->setParameters(s8);
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    String8 s8;
    int val;

    s8 = out->qcom_out->getParameters(String8(keys));

    AudioParameter parms = AudioParameter(s8);
    if (parms.getInt(String8(AUDIO_PARAMETER_STREAM_ROUTING), val) == NO_ERROR) {
        val = convert_audio_device(val, HAL_API_REV_1_0, HAL_API_REV_2_0);
        parms.remove(String8(AUDIO_PARAMETER_STREAM_ROUTING));
        parms.addInt(String8(AUDIO_PARAMETER_STREAM_ROUTING), val);
        s8 = parms.toString();
    }

    return strdup(s8.string());
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->latency();
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    return out->qcom_out->setVolume(left, right);
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    ssize_t ret = out->qcom_out->write(buffer, bytes);
    if (ret != 0)
        amplifier_output_stream_start(stream, false);
    return ret;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->getRenderPosition(dsp_frames);
}
#ifdef QCOM_TUNNEL_LPA_ENABLED
static int out_set_observer(const struct audio_stream_out *stream,
                                   void *observer)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->setObserver(observer);
}

static int out_get_buffer_info(const struct audio_stream_out *stream,
                                   buf_info ** buf)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->getBufferInfo(buf);
}

static int out_is_buffer_available(const struct audio_stream_out *stream,
                                   int *isAvail)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->isBufferAvailable(isAvail);
}

static status_t out_start(struct audio_stream_out *stream)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    return out->qcom_out->start();
}

static status_t out_pause(struct audio_stream_out *stream)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    return out->qcom_out->pause();
}

static status_t out_flush(struct audio_stream_out *stream)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    return out->qcom_out->flush();
}

static status_t out_stop(struct audio_stream_out *stream)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    return out->qcom_out->stop();
}
#endif //QCOM_TUNNEL_LPA_ENABLED

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->getNextWriteTimestamp(timestamp);
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                         uint64_t *frames, struct timespec *timestamp)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->getPresentationPosition(frames, timestamp);
}


/** audio_stream_in implementation **/
static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->sampleRate();
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);

    ALOGV("(%s:%d) %s: Implement me!", __FILE__, __LINE__, __func__);
    /* TODO: implement this */
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->bufferSize();
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->channels();
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return (audio_format_t)in->qcom_in->format();
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);
    ALOGV("(%s:%d) %s: Implement me!", __FILE__, __LINE__, __func__);
    /* TODO: implement me */
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct qcom_stream_in *in = reinterpret_cast<struct qcom_stream_in *>(stream);
    amplifier_input_stream_standby((struct audio_stream_in *)stream);
    return in->qcom_in->standby();
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    Vector<String16> args;
    return in->qcom_in->dump(fd, args);
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);
    int val;
    AudioParameter parms = AudioParameter(String8(kvpairs));
    String8 s8 = String8(kvpairs);

    if (parms.getInt(String8(AUDIO_PARAMETER_STREAM_ROUTING), val) == NO_ERROR) {
        val = convert_audio_device(val, HAL_API_REV_2_0, HAL_API_REV_1_0);
        parms.remove(String8(AUDIO_PARAMETER_STREAM_ROUTING));
        parms.addInt(String8(AUDIO_PARAMETER_STREAM_ROUTING), val);
        s8 = parms.toString();
    }

    return in->qcom_in->setParameters(s8);
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    String8 s8;
    int val;

    s8 = in->qcom_in->getParameters(String8(keys));

    AudioParameter parms = AudioParameter(s8);
    if (parms.getInt(String8(AUDIO_PARAMETER_STREAM_ROUTING), val) == NO_ERROR) {
        val = convert_audio_device(val, HAL_API_REV_1_0, HAL_API_REV_2_0);
        parms.remove(String8(AUDIO_PARAMETER_STREAM_ROUTING));
        parms.addInt(String8(AUDIO_PARAMETER_STREAM_ROUTING), val);
        s8 = parms.toString();
    }

    return strdup(s8.string());
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);
    return in->qcom_in->setGain(gain);
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);
    ssize_t ret = in->qcom_in->read(buffer, bytes);
    if (ret != 0)
        amplifier_input_stream_start(stream);
    return ret;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);
    return in->qcom_in->getInputFramesLost();
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->addAudioEffect(effect);
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->removeAudioEffect(effect);
}

/** audio_hw_device implementation **/
static inline struct qcom_audio_device * to_ladev(struct audio_hw_device *dev)
{
    return reinterpret_cast<struct qcom_audio_device *>(dev);
}

static inline const struct qcom_audio_device * to_cladev(const struct audio_hw_device *dev)
{
    return reinterpret_cast<const struct qcom_audio_device *>(dev);
}

static uint32_t adev_get_supported_devices(const struct audio_hw_device *dev)
{
    /* XXX: The old AudioHardwareInterface interface is not smart enough to
     * tell us this, so we'll lie and basically tell AF that we support the
     * below input/output devices and cross our fingers. To do things properly,
     * audio hardware interfaces that need advanced features (like this) should
     * convert to the new HAL interface and not use this wrapper. */
    return (/* OUT */
            AUDIO_DEVICE_OUT_EARPIECE |
            AUDIO_DEVICE_OUT_SPEAKER |
            AUDIO_DEVICE_OUT_WIRED_HEADSET |
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
            AUDIO_DEVICE_OUT_BLUETOOTH_SCO |
            AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET |
            AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT |
            AUDIO_DEVICE_OUT_BLUETOOTH_A2DP |
            AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES |
            AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER |
            AUDIO_DEVICE_OUT_AUX_DIGITAL |
            AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_USB_ACCESSORY |
            AUDIO_DEVICE_OUT_USB_DEVICE |
            AUDIO_DEVICE_OUT_REMOTE_SUBMIX |
#ifdef QCOM_PROXY_DEVICE_ENABLED
            AUDIO_DEVICE_OUT_PROXY |
#endif
#ifdef QCOM_FM_ENABLED
            AUDIO_DEVICE_OUT_FM |
            AUDIO_DEVICE_OUT_FM_TX |
#endif
            AUDIO_DEVICE_OUT_DEFAULT |
            /* IN */
            AUDIO_DEVICE_IN_COMMUNICATION |
            AUDIO_DEVICE_IN_AMBIENT |
            AUDIO_DEVICE_IN_BUILTIN_MIC |
            AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET |
            AUDIO_DEVICE_IN_WIRED_HEADSET |
            AUDIO_DEVICE_IN_AUX_DIGITAL |
            AUDIO_DEVICE_IN_VOICE_CALL |
            AUDIO_DEVICE_IN_BACK_MIC |
            AUDIO_DEVICE_IN_REMOTE_SUBMIX |
            AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET |
            AUDIO_DEVICE_IN_DGTL_DOCK_HEADSET |
            AUDIO_DEVICE_IN_USB_ACCESSORY |
            AUDIO_DEVICE_IN_USB_DEVICE |
#ifdef QCOM_PROXY_DEVICE_ENABLED
            AUDIO_DEVICE_IN_PROXY |
#endif
#ifdef QCOM_FM_ENABLED
            AUDIO_DEVICE_IN_FM_RX |
            AUDIO_DEVICE_IN_FM_RX_A2DP |
#endif
            AUDIO_DEVICE_IN_DEFAULT);
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    const struct qcom_audio_device *qadev = to_cladev(dev);

    return qadev->hwif->initCheck();
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->setVoiceVolume(volume);
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->setMasterVolume(volume);
}

static int adev_get_master_volume(struct audio_hw_device *dev, float *volume) {

    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->getMasterVolume(volume);
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct qcom_audio_device *qadev = to_ladev(dev);

    if (amplifier_set_mode(mode) != 0)
        ALOGE("Failed setting amplifier mode");

    return qadev->hwif->setMode(mode);
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->setMicMute(state);
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    const struct qcom_audio_device *qadev = to_cladev(dev);
    return qadev->hwif->getMicMute(state);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->setParameters(String8(kvpairs));
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    const struct qcom_audio_device *qadev = to_cladev(dev);
    String8 s8;

    s8 = qadev->hwif->getParameters(String8(keys));
    return strdup(s8.string());
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    const struct qcom_audio_device *qadev = to_cladev(dev);
    uint8_t channelCount = popcount(config->channel_mask);
    return qadev->hwif->getInputBufferSize(config->sample_rate, config->format, channelCount);
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char * address __unused)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    status_t status;
    struct qcom_stream_out *out;
    int ret;

    out = (struct qcom_stream_out *)calloc(1, sizeof(*out));
    if (!out)
        return -ENOMEM;

    devices = convert_audio_device(devices, HAL_API_REV_2_0, HAL_API_REV_1_0);
    status = static_cast<audio_output_flags_t> (flags);

    out->qcom_out = qadev->hwif->openOutputStream(devices,
                                                    (int *)&config->format,
                                                    &config->channel_mask,
                                                    &config->sample_rate,
                                                    &status);
    if (!out->qcom_out) {
        ret = status;
        goto err_open;
    }

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;
#ifdef QCOM_TUNNEL_LPA_ENABLED
    out->stream.start = out_start;
    out->stream.pause = out_pause;
    out->stream.flush = out_flush;
    out->stream.stop = out_stop;
    out->stream.set_observer = out_set_observer;
    out->stream.get_buffer_info = out_get_buffer_info;
    out->stream.is_buffer_available = out_is_buffer_available;
#endif
    *stream_out = &out->stream;
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out* stream)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    struct qcom_stream_out *out = reinterpret_cast<struct qcom_stream_out *>(stream);

    qadev->hwif->closeOutputStream(out->qcom_out);
    free(out);
}

/** This method creates and opens the audio hardware input stream */
static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  audio_config *config,
                                  audio_stream_in **stream_in, audio_input_flags_t flags,
                                  const char * address __unused,
                                  audio_source_t source __unused)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    status_t status;
    struct qcom_stream_in *in;
    int ret;

    in = (struct qcom_stream_in *)calloc(1, sizeof(*in));
    if (!in)
        return -ENOMEM;

    devices = convert_audio_device(devices, HAL_API_REV_2_0, HAL_API_REV_1_0);

    in->qcom_in = qadev->hwif->openInputStream(devices, (int *)&config->format,
                                    &config->channel_mask,
                                    &config->sample_rate,
                                    &status,
                                    (AudioSystem::audio_in_acoustics)0);
    if (!in->qcom_in) {
        ret = status;
        goto err_open;
    }

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    *stream_in = &in->stream;
    return 0;

err_open:
    free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                               struct audio_stream_in *stream)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);

    qadev->hwif->closeInputStream(in->qcom_in);
    free(in);
}

static int adev_dump(const struct audio_hw_device *dev, int fd)
{
    const struct qcom_audio_device *qadev = to_cladev(dev);
    Vector<String16> args;

    return qadev->hwif->dumpState(fd, args);
}

static int qcom_adev_close(hw_device_t* device)
{
    struct audio_hw_device *hwdev =
                        reinterpret_cast<struct audio_hw_device *>(device);
    struct qcom_audio_device *qadev = to_ladev(hwdev);

    if (amplifier_close() != 0)
        ALOGE("Amplifier close failed");

    if (!qadev)
        return 0;

    if (qadev->hwif)
        delete qadev->hwif;

    free(qadev);
    return 0;
}

static int qcom_adev_open(const hw_module_t* module, const char* name,
                            hw_device_t** device)
{
    int ret;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    qadev = (struct qcom_audio_device *)calloc(1, sizeof(*qadev));
    if (!qadev)
        return -ENOMEM;

    qadev->device.common.tag = HARDWARE_DEVICE_TAG;
    qadev->device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    qadev->device.common.module = const_cast<hw_module_t*>(module);
    qadev->device.common.close = qcom_adev_close;

    qadev->device.get_supported_devices = adev_get_supported_devices;
    qadev->device.init_check = adev_init_check;
    qadev->device.set_voice_volume = adev_set_voice_volume;
    qadev->device.set_master_volume = adev_set_master_volume;
    qadev->device.get_master_volume = adev_get_master_volume;
    qadev->device.set_mode = adev_set_mode;
    qadev->device.set_mic_mute = adev_set_mic_mute;
    qadev->device.get_mic_mute = adev_get_mic_mute;
    qadev->device.set_parameters = adev_set_parameters;
    qadev->device.get_parameters = adev_get_parameters;
    qadev->device.get_input_buffer_size = adev_get_input_buffer_size;
    qadev->device.open_output_stream = adev_open_output_stream;
    qadev->device.close_output_stream = adev_close_output_stream;
    qadev->device.open_input_stream = adev_open_input_stream;
    qadev->device.close_input_stream = adev_close_input_stream;
    qadev->device.dump = adev_dump;

    if (amplifier_open() != 0)
        ALOGE("Amplifier initialization failed");

    qadev->hwif = createAudioHardware();
    if (!qadev->hwif) {
        ret = -EIO;
        goto err_create_audio_hw;
    }

    *device = &qadev->device.common;

    return 0;

err_create_audio_hw:
    free(qadev);
    return ret;
}

static struct hw_module_methods_t qcom_audio_module_methods = {
        open: qcom_adev_open
};

struct qcom_audio_module HAL_MODULE_INFO_SYM = {
    module: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            module_api_version: AUDIO_MODULE_API_VERSION_0_1,
            hal_api_version: HARDWARE_HAL_API_VERSION,
            id: AUDIO_HARDWARE_MODULE_ID,
            name: "QCOM Audio HW HAL",
            author: "The Linux Foundation",
            methods: &qcom_audio_module_methods,
            dso : NULL,
            reserved : {0},
        },
    },
};

}; // extern "C"

}; // namespace android_audio_legacy
