/* ALSADevice.cpp
 **
 ** Copyright 2009 Wind River Systems
 ** Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

#define LOG_TAG "ALSADevice"
#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0
#include <utils/Log.h>
#include <cutils/properties.h>
#include <linux/ioctl.h>
#include "AudioUtil.h"
#include "AudioHardwareALSA.h"
#include <media/AudioRecord.h>
#include <dlfcn.h>
#include <math.h>
#ifdef USE_A2220
#include <sound/a2220.h>
#endif

extern "C" {
#ifdef QCOM_CSDCLIENT_ENABLED
static int (*csd_disable_device)();
static int (*csd_enable_device)(int, int, uint32_t);
static int (*csd_enable_device_config)(int, int);
static int (*csd_volume)(uint32_t, int);
static int (*csd_mic_mute)(uint32_t, int);
static int (*csd_wide_voice)(uint32_t, uint8_t);
static int (*csd_slow_talk)(uint32_t, uint8_t);
static int (*csd_fens)(uint32_t, uint8_t);
static int (*csd_start_voice)(uint32_t);
static int (*csd_stop_voice)(uint32_t);
#endif
#ifdef QCOM_ACDB_ENABLED
static int (*acdb_loader_get_ecrx_device)(int acdb_id);
#endif
}

#define SAMPLE_RATE_8KHZ 8000

#define BTSCO_RATE_16KHZ 16000
#define USECASE_TYPE_RX 1
#define USECASE_TYPE_TX 2
#define MAX_HDMI_CHANNEL_CNT 8

#define AFE_PROXY_PERIOD_SIZE 3072
#define KILL_A2DP_THREAD 1
#define SIGNAL_A2DP_THREAD 2

// Setting number of periods to 4. If the system is loaded
// and record obtain buffer is seen increase
// PCM_RECORD_PERIOD_COUNT to a value between 4-16.
#define PCM_RECORD_PERIOD_COUNT 4
#define PROXY_CAPTURE_DEVICE_NAME (const char *)("hw:0,8")
namespace sys_close {
    ssize_t lib_close(int fd) {
        return close(fd);
    }
};

namespace android_audio_legacy
{

ALSADevice::ALSADevice() {
#ifdef USES_FLUENCE_INCALL
    mDevSettingsFlag = TTY_OFF | DMIC_FLAG;
#else
    mSSRComplete = false;
    mDevSettingsFlag = TTY_OFF;
#endif
    mBtscoSamplerate = 8000;
    mCallMode = AUDIO_MODE_NORMAL;
    mInChannels = 0;
    mIsFmEnabled = false;
    //Initialize fm volume to value corresponding to unity volume	92
    mFmVolume = lrint((0.0 * 0x2000) + 0.5);
    char value[128], platform[128], baseband[128];

    property_get("persist.audio.handset.mic",value,"0");
    strlcpy(mMicType, value, sizeof(mMicType));
    property_get("persist.audio.fluence.mode",value,"0");
    if (!strcmp("broadside", value)) {
        mFluenceMode = FLUENCE_MODE_BROADSIDE;
    } else {
        mFluenceMode = FLUENCE_MODE_ENDFIRE;
    }
    property_get("ro.board.platform", platform, "");
    property_get("ro.baseband", baseband, "");
    if (!strcmp("msm8960", platform) && !strcmp("sglte", baseband)) {
        mIsSglte = true;
    }
    else {
        mIsSglte = false;
    }
    strlcpy(mCurRxUCMDevice, "None", sizeof(mCurRxUCMDevice));
    strlcpy(mCurTxUCMDevice, "None", sizeof(mCurTxUCMDevice));

    mMixer = mixer_open("/dev/snd/controlC0");

    mProxyParams.mExitRead = false;
    mProxyParams.mPfdProxy[1].fd = -1;
    resetProxyVariables();
    mProxyParams.mCaptureBufferSize = AFE_PROXY_PERIOD_SIZE;
    mProxyParams.mCaptureBuffer = NULL;
    mProxyParams.mProxyState = proxy_params::EProxyClosed;
    mProxyParams.mProxyPcmHandle = NULL;

#ifdef USE_A2220
    mA2220Fd = -1;
    mA2220Mode = A2220_PATH_INCALL_RECEIVER_NSOFF;
#endif

    ALOGD("ALSA module opened");
}

//static int s_device_close(hw_device_t* device)
ALSADevice::~ALSADevice()
{
    if (mMixer) mixer_close(mMixer);
    if(mProxyParams.mCaptureBuffer != NULL) {
        free(mProxyParams.mCaptureBuffer);
        mProxyParams.mCaptureBuffer = NULL;
    }
    mProxyParams.mProxyState = proxy_params::EProxyClosed;

}

static bool isPlatformFusion3() {
    char platform[128], baseband[128], baseband_arch[128];
    property_get("ro.board.platform", platform, "");
    property_get("ro.baseband", baseband, "");
    property_get("ro.baseband.arch", baseband_arch, "");
    if (!strcmp("msm8960", platform) &&
        (!strcmp("mdm", baseband) || !strcmp("sglte2", baseband) ||
        !strcmp("mdm", baseband_arch)))
        return true;
    else
        return false;
}

#ifdef USE_A2220
int ALSADevice::setA2220Mode(int mode)
{
    Mutex::Autolock autoLock(mA2220Lock);
    int rc = -1;

    if (mA2220Mode != mode) {
        if (mA2220Fd < 0) {
            mA2220Fd = ::open("/dev/audience_a2220", O_RDWR);
            if (!mA2220Fd) {
                ALOGE("%s: unable to open a2220 device!", __func__);
                return rc;
            } else {
                ALOGI("%s: device opened, fd=%d", __func__, mA2220Fd);
            }
        }

        rc = ioctl(mA2220Fd, A2220_SET_CONFIG, mode);
        if (rc < 0)
            ALOGE("%s: ioctl failed, errno=%d", __func__, errno);
        else {
            mA2220Mode = mode;
            ALOGD("%s: set mode=%d", __func__, mode);
        }
    }
    return rc;
}
#endif

static bool shouldUseHandsetAnc(int flags, int inChannels)
{
    if (!isPlatformFusion3()) {
        return false;
    }
    return (flags & ANC_FLAG) && (inChannels == 1);
}

static int adjustFlagsForCsd(int flags, const char *rxDevice)
{
    int adjustedFlags = flags;
    if (0 != strcmp(rxDevice, SND_USE_CASE_DEV_ANC_HANDSET)) {
        /* if not using Adaptive ANC, clear the ANC bit; this
           is the only adaptive mode CSD Client cares about */
        adjustedFlags &= ~(ANC_FLAG);
    }
    ALOGD("%s: current Rx device: %s, flags: %x, adjustedFlags: %x",
            __FUNCTION__, rxDevice, flags, adjustedFlags);
    return adjustedFlags;
}

int ALSADevice::deviceName(alsa_handle_t *handle, unsigned flags, char **value)
{
    int ret = 0;
    char ident[70];

    if (flags & PCM_IN) {
        strlcpy(ident, "CapturePCM/", sizeof(ident));
    } else {
        strlcpy(ident, "PlaybackPCM/", sizeof(ident));
    }
    strlcat(ident, handle->useCase, sizeof(ident));
    ret = snd_use_case_get(handle->ucMgr, ident, (const char **)value);
    ALOGD("Device value returned is %s", (*value));
    return ret;
}

status_t ALSADevice::setHDMIChannelCount()
{
    status_t err = NO_ERROR;
    int channel_count = 0;
    const char *channel_cnt_str = NULL;
    EDID_AUDIO_INFO info = { 0 };

#ifdef TARGET_8974
    char hdmiEDIDData[MAX_SHORT_AUDIO_DESC_CNT+1];
    if(!getEDIDData(hdmiEDIDData)) {
        if (AudioUtil::getHDMIAudioSinkCaps(&info, hdmiEDIDData)) {
            for (int i = 0; i < info.nAudioBlocks && i < MAX_EDID_BLOCKS; i++) {
                if (info.AudioBlocksArray[i].nChannels > channel_count &&
                      info.AudioBlocksArray[i].nChannels <= MAX_HDMI_CHANNEL_CNT) {
                    channel_count = info.AudioBlocksArray[i].nChannels;
                }
            }
            pcm_set_channel_map(NULL, mMixer, MAX_HDMI_CHANNEL_CNT, info.channelMap);
            setChannelAlloc(info.channelAllocation);
        }
    }
#else
    if (AudioUtil::getHDMIAudioSinkCaps(&info)) {
        for (int i = 0; i < info.nAudioBlocks && i < MAX_EDID_BLOCKS; i++) {
            if (info.AudioBlocksArray[i].nChannels > channel_count &&
                  info.AudioBlocksArray[i].nChannels <= MAX_HDMI_CHANNEL_CNT) {
                channel_count = info.AudioBlocksArray[i].nChannels;
            }
        }
    }
#endif

    switch (channel_count) {
    case 8: channel_cnt_str = "Eight"; break;
    case 7: channel_cnt_str = "Seven"; break;
    case 6: channel_cnt_str = "Six"; break;
    case 5: channel_cnt_str = "Five"; break;
    case 4: channel_cnt_str = "Four"; break;
    case 3: channel_cnt_str = "Three"; break;
    default: channel_cnt_str = "Two"; break;
    }
    ALOGD("HDMI channel count: %s", channel_cnt_str);
    setMixerControl("HDMI_RX Channels", channel_cnt_str);

    return err;
}

status_t ALSADevice::setHardwareParams(alsa_handle_t *handle)
{
    struct snd_pcm_hw_params *params;
    unsigned long bufferSize, reqBuffSize;
    unsigned int periodTime, bufferTime;
    unsigned int requestedRate = handle->sampleRate;
    int status = 0;
    int channels = handle->channels;
    status_t err;
    snd_pcm_format_t format = SNDRV_PCM_FORMAT_S16_LE;
    struct snd_compr_caps compr_cap;
    struct snd_compr_params compr_params;
    uint32_t codec_id = 0;

    ALOGD("handle->format: 0x%x", handle->format);
    if ((!strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_TUNNEL)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_TUNNEL)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_COMPRESSED_VOICE_DL)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_VERB_CAPTURE_COMPRESSED_VOICE_DL)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_COMPRESSED_VOICE_UL_DL)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_VERB_CAPTURE_COMPRESSED_VOICE_UL_DL))) {
        ALOGV("Tunnel mode detected...");
        //get the list of codec supported by hardware
        if (ioctl(handle->handle->fd, SNDRV_COMPRESS_GET_CAPS, &compr_cap)) {
            ALOGE("SNDRV_COMPRESS_GET_CAPS, failed Error no %d \n", errno);
            err = -errno;
            return err;
        }
        if( (handle->format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC ) {
          codec_id = get_compressed_format("AAC");
          ALOGV("### AAC CODEC codec_id %d",codec_id);
        }
        else if ((handle->format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AMR_WB) {
          codec_id = get_compressed_format("AMR_WB");
          if ((!strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED)) ||
              (!strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED)) ||
              (!strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_COMPRESSED_VOICE_UL_DL)) ||
              (!strcmp(handle->useCase, SND_USE_CASE_VERB_CAPTURE_COMPRESSED_VOICE_UL_DL)) ||
              (!strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_COMPRESSED_VOICE_DL)) ||
              (!strcmp(handle->useCase, SND_USE_CASE_VERB_CAPTURE_COMPRESSED_VOICE_DL))) {
              compr_params.codec.options.generic.reserved[0] = 8; /*band mode - 23.85 kbps*/
              compr_params.codec.options.generic.reserved[1] = 0; /*dtx mode - disable*/
          }
          ALOGV("### AMR WB CODEC codec_id %d",codec_id);
        }
#ifdef QCOM_AUDIO_FORMAT_ENABLED
        else if ((handle->format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AMR_WB_PLUS) {
          codec_id = get_compressed_format("AMR_WB_PLUS");
          ALOGV("### AMR WB+ CODEC codec_id %d",codec_id);
        }
#endif
        else if ((handle->format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_MP3) {
          codec_id = get_compressed_format("MP3");
          ALOGV("### MP3 CODEC codec_id %d",codec_id);
        }
        else {
            return UNKNOWN_ERROR;
        }
        //find if codec_id matches with any of h/w supported codecs.
        for (int i = 0; i < compr_cap.num_codecs; i++) {
          if (compr_cap.codecs[i] == codec_id) {
            ALOGV("### MatchedFcodec_id %u", codec_id);
            compr_params.codec.id = codec_id;
            break;
          }
        }
        if (!compr_params.codec.id) {
          ALOGE("### Codec %u not supported",codec_id);
          return UNKNOWN_ERROR;
        }

        if (ioctl(handle->handle->fd, SNDRV_COMPRESS_SET_PARAMS, &compr_params)) {
            ALOGE("SNDRV_COMPRESS_SET_PARAMS,failed Error no %d \n", errno);
            err = -errno;
            return err;
        }
    }

    params = (snd_pcm_hw_params*) calloc(1, sizeof(struct snd_pcm_hw_params));
    if (!params) {
        ALOGE("Failed to allocate ALSA hardware parameters!");
        return NO_INIT;
    }

    reqBuffSize = handle->bufferSize;
    ALOGD("setHardwareParams: reqBuffSize %d channels %d sampleRate %d",
         (int) reqBuffSize, handle->channels, handle->sampleRate);

#ifdef QCOM_SSR_ENABLED
    if (channels == 6) {
        if (!strncmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC, strlen(SND_USE_CASE_VERB_HIFI_REC))
            || !strncmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED, strlen(SND_USE_CASE_VERB_HIFI_REC_COMPRESSED))
            || !strncmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC))
            || !strncmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED))) {
            channels = 4;
            reqBuffSize = DEFAULT_IN_BUFFER_SIZE*4;
            ALOGV("HWParams: Use 4 channels in kernel for 5.1(%s) recording reqBuffSize:%d", handle->useCase,reqBuffSize);
        }
    }
#endif

    param_init(params);
    if ((!strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_LOW_POWER)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_LPA)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_TUNNEL)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_TUNNEL))) {
        param_set_mask(params, SNDRV_PCM_HW_PARAM_ACCESS,
                       SNDRV_PCM_ACCESS_MMAP_INTERLEAVED);
    }
    else {
        param_set_mask(params, SNDRV_PCM_HW_PARAM_ACCESS,
                       SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    }

    if (handle->format != SNDRV_PCM_FORMAT_S16_LE) {
        if (handle->format == AUDIO_FORMAT_AMR_NB
            || handle->format == AUDIO_FORMAT_AMR_WB
#ifdef QCOM_AUDIO_FORMAT_ENABLED
            || handle->format == AUDIO_FORMAT_EVRC
            || handle->format == AUDIO_FORMAT_EVRCB
            || handle->format == AUDIO_FORMAT_EVRCWB
#endif
            ) {
            if ((strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_TUNNEL)) &&
                (strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_TUNNEL)) &&
                (strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED)) &&
                (strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_COMPRESSED_VOICE_UL_DL)) &&
                (strcmp(handle->useCase, SND_USE_CASE_VERB_CAPTURE_COMPRESSED_VOICE_UL_DL)) &&
                (strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_COMPRESSED_VOICE_DL)) &&
                (strcmp(handle->useCase, SND_USE_CASE_VERB_CAPTURE_COMPRESSED_VOICE_DL)) &&
                (strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED))) {
              format = SNDRV_PCM_FORMAT_SPECIAL;
              ALOGW("setting format to SNDRV_PCM_FORMAT_SPECIAL");
            }
        }
    }
    //TODO: Add format setting for tunnel mode using the usecase.
    param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
                   format);
    param_set_mask(params, SNDRV_PCM_HW_PARAM_SUBFORMAT,
                   SNDRV_PCM_SUBFORMAT_STD);
    param_set_int(params, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, reqBuffSize);
    //Setting number of periods to 4. If the system is loaded and record
    // obtain buffer is seen increase PCM_RECORD_PERIOD_COUNT to a value between 4-16.
    if (!strncmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC,
            strlen(SND_USE_CASE_VERB_HIFI_REC)) ||
            !strncmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC,
            strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC))) {
        param_set_int(params, SNDRV_PCM_HW_PARAM_PERIODS, PCM_RECORD_PERIOD_COUNT);
    }
    param_set_int(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16);
    param_set_int(params, SNDRV_PCM_HW_PARAM_FRAME_BITS,
                   channels * 16);
    param_set_int(params, SNDRV_PCM_HW_PARAM_CHANNELS,
                  channels);
    param_set_int(params, SNDRV_PCM_HW_PARAM_RATE, handle->sampleRate);
    param_set_hw_refine(handle->handle, params);

    if (param_set_hw_params(handle->handle, params)) {
        ALOGE("cannot set hw params");
        if(params) {
            free(params);
        }
        return NO_INIT;
    }
    param_dump(params);

    handle->handle->buffer_size = pcm_buffer_size(params);
    handle->handle->period_size = pcm_period_size(params);
    handle->handle->period_cnt = handle->handle->buffer_size/handle->handle->period_size;
    ALOGD("setHardwareParams: buffer_size %d, period_size %d, period_cnt %d",
        handle->handle->buffer_size, handle->handle->period_size,
        handle->handle->period_cnt);
    handle->handle->rate = handle->sampleRate;
    handle->handle->channels = handle->channels;
    handle->periodSize = handle->handle->period_size;
    if (strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC) &&
        strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED) &&
        strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC) &&
        strcmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED) &&
        (6 != handle->channels)) {
        //Do not update buffersize for 5.1 recording
        if (handle->format == AUDIO_FORMAT_AMR_WB &&
            format != SNDRV_PCM_FORMAT_SPECIAL) {
            ALOGV("### format AMWB, set bufsize to 61");
            handle->bufferSize = 61;
        } else {
            handle->bufferSize = handle->handle->period_size;
        }
    }

    return NO_ERROR;
}

status_t ALSADevice::setSoftwareParams(alsa_handle_t *handle)
{
    struct snd_pcm_sw_params* params;
    struct pcm* pcm = handle->handle;

    unsigned long periodSize = pcm->period_size;
    int channels = handle->channels;

    params = (snd_pcm_sw_params*) calloc(1, sizeof(struct snd_pcm_sw_params));
    if (!params) {
        LOG_ALWAYS_FATAL("Failed to allocate ALSA software parameters!");
        return NO_INIT;
    }

#ifdef QCOM_SSR_ENABLED
    if (channels == 6) {
        if (!strncmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC, strlen(SND_USE_CASE_VERB_HIFI_REC))
            || !strncmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED, strlen(SND_USE_CASE_VERB_HIFI_REC_COMPRESSED))
            || !strncmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC))
            || !strncmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED))) {
            ALOGV("SWParams: Use 4 channels in kernel for 5.1(%s) recording ", handle->useCase);
            channels = 4;
        }
    }
#endif

    // Get the current software parameters
    params->tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    params->period_step = 1;
    if(((!strcmp(handle->useCase,SND_USE_CASE_MOD_PLAY_VOIP)) ||
        (!strcmp(handle->useCase,SND_USE_CASE_VERB_IP_VOICECALL)))){
          ALOGV("setparam:  start & stop threshold for Voip ");
          params->avail_min = handle->channels - 1 ? periodSize/4 : periodSize/2;
          params->start_threshold = periodSize/2;
          params->stop_threshold = INT_MAX;
     } else {
         params->avail_min = periodSize/(channels * 2);
         params->start_threshold = periodSize/(channels * 2);
         params->stop_threshold = INT_MAX;
     }
    params->silence_threshold = 0;
    params->silence_size = 0;

    if (param_set_sw_params(handle->handle, params)) {
        ALOGE("cannot set sw params");
        if (params) {
            free(params);
        }
        return NO_INIT;
    }
    return NO_ERROR;
}

void ALSADevice::switchDevice(alsa_handle_t *handle, uint32_t devices, uint32_t mode)
{
    const char **mods_list;
    use_case_t useCaseNode;
    unsigned usecase_type = 0;
    bool inCallDevSwitch = false;
    char *rxDevice, *txDevice, ident[70], *use_case = NULL;
    int err = 0, index, mods_size;
    int rx_dev_id, tx_dev_id;
    ALOGV("%s: device %#x mode:%d", __FUNCTION__, devices, mode);

    if ((mode == AUDIO_MODE_IN_CALL)  || (mode == AUDIO_MODE_IN_COMMUNICATION)) {
        if ((devices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) ||
            (devices & AudioSystem::DEVICE_IN_WIRED_HEADSET)) {
            devices = devices | (AudioSystem::DEVICE_OUT_WIRED_HEADSET |
                      AudioSystem::DEVICE_IN_WIRED_HEADSET);
        } else if (devices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) {
            devices = devices | (AudioSystem::DEVICE_OUT_WIRED_HEADPHONE |
                      AudioSystem::DEVICE_IN_BUILTIN_MIC);
        } else if ((devices & AudioSystem::DEVICE_OUT_EARPIECE) ||
                   (devices & AudioSystem::DEVICE_IN_BUILTIN_MIC) ||
                  (devices & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET)) {
            if ((mode == AudioSystem::MODE_IN_COMMUNICATION) &&
                 (devices & AudioSystem::DEVICE_IN_BUILTIN_MIC)) {
                 if (!strncmp(mCurRxUCMDevice, SND_USE_CASE_DEV_SPEAKER,
                              strlen(SND_USE_CASE_DEV_SPEAKER))) {
                     devices = devices | (AudioSystem::DEVICE_IN_BUILTIN_MIC |
                               AudioSystem::DEVICE_OUT_SPEAKER);
                 }
                 else if (!strncmp(mCurRxUCMDevice, SND_USE_CASE_DEV_HDMI,
                                   strlen(SND_USE_CASE_DEV_HDMI))) {
                           devices = devices | (AudioSystem::DEVICE_OUT_AUX_DIGITAL |
                                     AudioSystem::DEVICE_IN_AUX_DIGITAL);
                 }
                 else {
                     devices = devices | (AudioSystem::DEVICE_IN_BUILTIN_MIC |
                               AudioSystem::DEVICE_OUT_EARPIECE);
                 }
            }
            else {
                devices = devices | (AudioSystem::DEVICE_IN_BUILTIN_MIC |
                AudioSystem::DEVICE_OUT_EARPIECE);
            }
        } else if (devices & AudioSystem::DEVICE_OUT_SPEAKER) {
            devices = devices | (AudioSystem::DEVICE_IN_BUILTIN_MIC |
                       AudioSystem::DEVICE_OUT_SPEAKER);
        } else if ((devices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO) ||
                   (devices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET) ||
                   (devices & AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET) ||
                   (devices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT)) {
            devices = devices | (AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET |
                      AudioSystem::DEVICE_OUT_BLUETOOTH_SCO);
#ifdef QCOM_ANC_HEADSET_ENABLED
        } else if ((devices & AudioSystem::DEVICE_OUT_ANC_HEADSET) ||
                   (devices & AudioSystem::DEVICE_IN_ANC_HEADSET)) {
            devices = devices | (AudioSystem::DEVICE_OUT_ANC_HEADSET |
                      AudioSystem::DEVICE_IN_ANC_HEADSET);
        } else if (devices & AudioSystem::DEVICE_OUT_ANC_HEADPHONE) {
            devices = devices | (AudioSystem::DEVICE_OUT_ANC_HEADPHONE |
                      AudioSystem::DEVICE_IN_BUILTIN_MIC);
#endif
#ifdef QCOM_USBAUDIO_ENABLED
        } else if ((devices & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET ) ||
                  (devices & AudioSystem::DEVICE_IN_ANLG_DOCK_HEADSET )) {
            devices = devices | (AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET |
                      AudioSystem::DEVICE_IN_ANLG_DOCK_HEADSET);
#endif
        } else if ((devices & AudioSystem::DEVICE_OUT_AUX_DIGITAL) ||
                  (devices & AudioSystem::DEVICE_IN_AUX_DIGITAL)) {
            devices = devices | (AudioSystem::DEVICE_OUT_AUX_DIGITAL |
                      AudioSystem::DEVICE_IN_AUX_DIGITAL);
#ifdef QCOM_PROXY_DEVICE_ENABLED
        } else if ((devices & AudioSystem::DEVICE_OUT_PROXY) ||
                  (devices & AudioSystem::DEVICE_IN_PROXY)) {
            devices = devices | (AudioSystem::DEVICE_OUT_PROXY |
                      AudioSystem::DEVICE_IN_PROXY);
#endif
        } else if (devices & AUDIO_DEVICE_OUT_ALL_USB) {
            devices = devices | AudioSystem::DEVICE_IN_BUILTIN_MIC;
        } else if (devices & AudioSystem::DEVICE_OUT_ALL_A2DP) {
            ALOGE("SwitchDevice:: Invalid A2DP Combination for mode %d", mode);
        }
    }
#ifdef QCOM_SSR_ENABLED
    if ((devices & AudioSystem::DEVICE_IN_BUILTIN_MIC) && ( 6 == handle->channels)) {
        if (!strncmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC, strlen(SND_USE_CASE_VERB_HIFI_REC))
            || !strncmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED, strlen(SND_USE_CASE_VERB_HIFI_REC_COMPRESSED))
            || !strncmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC))
            || !strncmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED))) {
            ALOGV(" switchDevice , use ssr devices for channels:%d usecase:%s",handle->channels,handle->useCase);
            setFlags(SSRQMIC_FLAG);
        }
    }
#endif

    rxDevice = getUCMDevice(devices & AudioSystem::DEVICE_OUT_ALL, 0, NULL);
    ALOGV("%s: rxDevice %s devices:0x%x", __FUNCTION__, rxDevice,devices);
    txDevice = getUCMDevice(devices & AudioSystem::DEVICE_IN_ALL, 1, rxDevice);
    ALOGV("%s: txDevice:%s devices:0x%x", __FUNCTION__, txDevice,devices);

    if ((rxDevice != NULL) && (txDevice != NULL)) {
        if (((strncmp(rxDevice, mCurRxUCMDevice, MAX_STR_LEN)) ||
             (strncmp(txDevice, mCurTxUCMDevice, MAX_STR_LEN))) &&
             ((mode == AUDIO_MODE_IN_CALL) ||
             (mode == AUDIO_MODE_IN_COMMUNICATION)))
            inCallDevSwitch = true;
    }

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3() && (inCallDevSwitch == true)) {
        if (csd_disable_device == NULL) {
            ALOGE("csd_client_disable_device is NULL");
        } else {
            err = csd_disable_device();
            if (err < 0)
            {
                ALOGE("csd_client_disable_device, failed, error %d", err);
            }
        }
    }
#endif

    snd_use_case_get(handle->ucMgr, "_verb", (const char **)&use_case);
    mods_size = snd_use_case_get_list(handle->ucMgr, "_enamods", &mods_list);
    if (rxDevice != NULL) {
        if ((strncmp(mCurRxUCMDevice, "None", 4)) &&
            (mSSRComplete || (strncmp(rxDevice, mCurRxUCMDevice, MAX_STR_LEN)) || (inCallDevSwitch == true))) {
            if ((use_case != NULL) && (strncmp(use_case, SND_USE_CASE_VERB_INACTIVE,
                strlen(SND_USE_CASE_VERB_INACTIVE)))) {
                usecase_type = getUseCaseType(use_case);
                if (usecase_type & USECASE_TYPE_RX) {
                    ALOGD("Deroute use case %s type is %d\n", use_case, usecase_type);
                    strlcpy(useCaseNode.useCase, use_case, MAX_STR_LEN);
                    snd_use_case_set(handle->ucMgr, "_verb", SND_USE_CASE_VERB_INACTIVE);
                    mUseCaseList.push_front(useCaseNode);
                }
            }
            if (mods_size) {
                for(index = 0; index < mods_size; index++) {
                    usecase_type = getUseCaseType(mods_list[index]);
                    if (usecase_type & USECASE_TYPE_RX) {
                        ALOGD("Deroute use case %s type is %d\n", mods_list[index], usecase_type);
                        strlcpy(useCaseNode.useCase, mods_list[index], MAX_STR_LEN);
                        snd_use_case_set(handle->ucMgr, "_dismod", mods_list[index]);
                        mUseCaseList.push_back(useCaseNode);
                    }
                }
            }
            snd_use_case_set(handle->ucMgr, "_disdev", mCurRxUCMDevice);
        }
    }
    if (txDevice != NULL) {
        if ((strncmp(mCurTxUCMDevice, "None", 4)) &&
            (mSSRComplete || (strncmp(txDevice, mCurTxUCMDevice, MAX_STR_LEN)) || (inCallDevSwitch == true))) {
            if ((use_case != NULL) && (strncmp(use_case, SND_USE_CASE_VERB_INACTIVE,
                strlen(SND_USE_CASE_VERB_INACTIVE)))) {
                usecase_type = getUseCaseType(use_case);
                if ((usecase_type & USECASE_TYPE_TX) && (!(usecase_type & USECASE_TYPE_RX))) {
                    ALOGD("Deroute use case %s type is %d\n", use_case, usecase_type);
                    strlcpy(useCaseNode.useCase, use_case, MAX_STR_LEN);
                    snd_use_case_set(handle->ucMgr, "_verb", SND_USE_CASE_VERB_INACTIVE);
                    mUseCaseList.push_front(useCaseNode);
                }
            }
            if (mods_size) {
                for(index = 0; index < mods_size; index++) {
                    usecase_type = getUseCaseType(mods_list[index]);
                    if ((usecase_type & USECASE_TYPE_TX) && (!(usecase_type & USECASE_TYPE_RX))) {
                        ALOGD("Deroute use case %s type is %d\n", mods_list[index], usecase_type);
                        strlcpy(useCaseNode.useCase, mods_list[index], MAX_STR_LEN);
                        snd_use_case_set(handle->ucMgr, "_dismod", mods_list[index]);
                        mUseCaseList.push_back(useCaseNode);
                    }
                }
            }
            snd_use_case_set(handle->ucMgr, "_disdev", mCurTxUCMDevice);
       }
    }

    ALOGV("%s,rxDev:%s, txDev:%s, curRxDev:%s, curTxDev:%s\n", __FUNCTION__, rxDevice, txDevice, mCurRxUCMDevice, mCurTxUCMDevice);

    if (rxDevice != NULL) {
        snd_use_case_set(handle->ucMgr, "_enadev", rxDevice);
        strlcpy(mCurRxUCMDevice, rxDevice, sizeof(mCurRxUCMDevice));
    }
    if (txDevice != NULL) {
       snd_use_case_set(handle->ucMgr, "_enadev", txDevice);
       strlcpy(mCurTxUCMDevice, txDevice, sizeof(mCurTxUCMDevice));
    }
#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3() && (inCallDevSwitch == true)) {

        /* Get tx acdb id */
        memset(&ident,0,sizeof(ident));
        strlcpy(ident, "ACDBID/", sizeof(ident));
        strlcat(ident, mCurTxUCMDevice, sizeof(ident));
        tx_dev_id = snd_use_case_get(handle->ucMgr, ident, NULL);

       /* Get rx acdb id */
        memset(&ident,0,sizeof(ident));
        strlcpy(ident, "ACDBID/", sizeof(ident));
        strlcat(ident, mCurRxUCMDevice, sizeof(ident));
        rx_dev_id = snd_use_case_get(handle->ucMgr, ident, NULL);

        if (rx_dev_id == DEVICE_SPEAKER_RX_ACDB_ID && tx_dev_id == DEVICE_HANDSET_TX_ACDB_ID) {
            tx_dev_id = DEVICE_SPEAKER_TX_ACDB_ID;
        }

        /* Parallelize codec configuration on APQ with CSD voice call
         * sequence on MDM. This will reduce in call device switch delay
         */
        if (csd_enable_device_config == NULL) {
            ALOGE("csd_enable_device_config is NULL");
        } else {
            err = csd_enable_device_config(rx_dev_id, tx_dev_id);
            if (err < 0) {
                ALOGE("csd_enable_device_config failed, error %d", err);
            }
        }
    }
#endif

        ALOGD("switchDevice: mCurTxUCMDevivce %s mCurRxDevDevice %s", mCurTxUCMDevice, mCurRxUCMDevice);
        /* Enable the EC ref device before enabling the Rx/Tx devices */
        /* Enabling the Rx/Tx devices is being happened while enabling _verb/_enamod usecases */

#ifdef QCOM_ACDB_ENABLED
    if (((devices & AudioSystem::DEVICE_IN_BUILTIN_MIC) || (devices & AudioSystem::DEVICE_IN_BACK_MIC))
        && (mInChannels == 1)) {
        ALOGD("switchDevice:use device %x for channels:%d usecase:%s",devices,handle->channels,handle->useCase);
        int ec_acdbid;
        char *ec_dev;
        char *ec_rx_dev;
        memset(&ident,0,sizeof(ident));
        strlcpy(ident, "ACDBID/", sizeof(ident));
        strlcat(ident, mCurTxUCMDevice, sizeof(ident));
        tx_dev_id = snd_use_case_get(handle->ucMgr, ident, NULL);
        if (acdb_loader_get_ecrx_device) {
            ec_acdbid = acdb_loader_get_ecrx_device(tx_dev_id);
            ec_dev = getUCMDeviceFromAcdbId(ec_acdbid);
            if (ec_dev) {
                memset(&ident,0,sizeof(ident));
                strlcpy(ident, "EC_REF_RXMixerCTL/", sizeof(ident));
                strlcat(ident, ec_dev, sizeof(ident));
                snd_use_case_get(handle->ucMgr, ident, (const char **)&ec_rx_dev);
                ALOGD("SwitchDevice: ec_ref_rx_acdbid:%d ec_dev:%s ec_rx_dev:%s", ec_acdbid, ec_dev, ec_rx_dev);
                if (ec_rx_dev) {
                    setEcrxDevice(ec_rx_dev);
                    free(ec_rx_dev);
                }
                free(ec_dev);
            }
        } else {
            ALOGE("acdb_loader_get_ecrx_device is NULL");
        }
    }
#endif

    for(ALSAUseCaseList::iterator it = mUseCaseList.begin(); it != mUseCaseList.end(); ++it) {
        ALOGD("Route use case %s\n", it->useCase);
        if ((use_case != NULL) && (strncmp(use_case, SND_USE_CASE_VERB_INACTIVE,
            strlen(SND_USE_CASE_VERB_INACTIVE))) && (!strncmp(use_case, it->useCase, MAX_UC_LEN))) {
            snd_use_case_set(handle->ucMgr, "_verb", it->useCase);
        } else {
            snd_use_case_set(handle->ucMgr, "_enamod", it->useCase);
        }
    }
    if (!mUseCaseList.empty())
        mUseCaseList.clear();
    if (use_case != NULL) {
        free(use_case);
        use_case = NULL;
    }
#ifdef QCOM_FM_ENABLED
    if (rxDevice != NULL) {
        setFmVolume(mFmVolume);
    }
#endif

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3() && (inCallDevSwitch == true)) {
        if (rx_dev_id == DEVICE_SPEAKER_RX_ACDB_ID && tx_dev_id == DEVICE_HANDSET_TX_ACDB_ID) {
            tx_dev_id = DEVICE_SPEAKER_TX_ACDB_ID;
        }
        ALOGV("rx_dev_id=%d, tx_dev_id=%d\n", rx_dev_id, tx_dev_id);

        if (csd_enable_device == NULL) {
            ALOGE("csd_client_enable_device is NULL");
        } else {
            int adjustedFlags = adjustFlagsForCsd(mDevSettingsFlag,
                    mCurRxUCMDevice);
            err = csd_enable_device(rx_dev_id, tx_dev_id, adjustedFlags);
            if (err < 0)
            {
                ALOGE("csd_client_disable_device failed, error %d", err);
            }
        }
    }
#endif

#ifdef USE_A2220
    ALOGI("a2220: txDevice=%s rxDevice=%s", txDevice, rxDevice);
    if (rxDevice != NULL && txDevice != NULL &&
            (!strcmp(txDevice, SND_USE_CASE_DEV_DUAL_MIC_ENDFIRE) ||
            !strcmp(txDevice, SND_USE_CASE_DEV_DUAL_MIC_BROADSIDE)) &&
            (!strcmp(rxDevice, SND_USE_CASE_DEV_VOC_EARPIECE) ||
             !strcmp(rxDevice, SND_USE_CASE_DEV_VOC_EARPIECE_XGAIN))) {
        setA2220Mode(A2220_PATH_INCALL_RECEIVER_NSON);
    } else {
        setA2220Mode(A2220_PATH_INCALL_RECEIVER_NSOFF);
    }
#endif

    if (rxDevice != NULL) {
        free(rxDevice);
        rxDevice = NULL;
    }
    if (txDevice != NULL) {
        free(txDevice);
        txDevice = NULL;
    }
}

// ----------------------------------------------------------------------------
/*
status_t ALSADevice::init(alsa_device_t *module, ALSAHandleList &list)
{
    ALOGD("s_init: Initializing devices for ALSA module");

    list.clear();

    return NO_ERROR;
}
*/
status_t ALSADevice::open(alsa_handle_t *handle)
{
    char *devName = NULL;
    unsigned flags = 0;
    int err = NO_ERROR;

    if(mCurDevice & AudioSystem::DEVICE_OUT_AUX_DIGITAL) {
        err = setHDMIChannelCount();
        if(err != OK) {
            ALOGE("setHDMIChannelCount err = %d", err);
            return err;
        }
    }
    close(handle);

    ALOGD("open: handle %p, format 0x%x", handle, handle->format);

    // ASoC multicomponent requires a valid path (frontend/backend) for
    // the device to be opened

    // The PCM stream is opened in blocking mode, per ALSA defaults.  The
    // AudioFlinger seems to assume blocking mode too, so asynchronous mode
    // should not be used.
    if ((!strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_LOW_POWER)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_LPA)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_TUNNEL)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_TUNNEL))) {
        ALOGV("LPA/tunnel use case");
        flags |= PCM_MMAP;
        flags |= DEBUG_ON;
    } else if ((!strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI2)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_VERB_HIFI_LOWLATENCY_MUSIC)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_LOWLATENCY_MUSIC)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_MUSIC2)) ||
        (!strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_MUSIC))) {
        ALOGV("Music case");
        flags = PCM_OUT;
    } else {
        flags = PCM_IN;
    }

    if (handle->channels == 1) {
        flags |= PCM_MONO;
    }
    else if (handle->channels == 4 ) {
        flags |= PCM_QUAD;
    } else if (handle->channels == 6 ) {
#ifdef QCOM_SSR_ENABLED
        if (!strncmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC, strlen(SND_USE_CASE_VERB_HIFI_REC))
            || !strncmp(handle->useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED, strlen(SND_USE_CASE_VERB_HIFI_REC_COMPRESSED))
            || !strncmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC))
            || !strncmp(handle->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED))) {
            flags |= PCM_QUAD;
        } else
#endif
        {
            flags |= PCM_5POINT1;
        }
    }
    else {
        flags |= PCM_STEREO;
    }

    if (deviceName(handle, flags, &devName) < 0) {
        ALOGE("Failed to get pcm device node: %s", devName);
        return NO_INIT;
    }
    if (devName != NULL) {
        ALOGV("flags %x, devName %s",flags,devName);
        handle->handle = pcm_open(flags, (char*)devName);
    } else {
        ALOGE("Failed to get pcm device node");
        return NO_INIT;
    }
    ALOGV("pcm_open returned fd %d", handle->handle->fd);

    if (!handle->handle || (handle->handle->fd < 0)) {
        ALOGE("open: Failed to initialize ALSA device '%s'", devName);
        if (devName) {
            free(devName);
            devName = NULL;
        }
        return NO_INIT;
    }

    handle->handle->flags = flags;
    err = setHardwareParams(handle);

    if (err == NO_ERROR) {
        err = setSoftwareParams(handle);
    }

    if(err != NO_ERROR) {
        ALOGE("Set HW/SW params failed: Closing the pcm stream");
        standby(handle);
        free(devName);
        devName = NULL;
        return err;
    }

    if (devName) {
        free(devName);
        devName = NULL;
    }

#ifdef TARGET_8974
    if(handle->channels > 2)
        setChannelMap(handle, MAX_HDMI_CHANNEL_CNT);
#endif

    return NO_ERROR;
}

status_t ALSADevice::startVoipCall(alsa_handle_t *handle)
{

    char* devName = NULL;
    unsigned flags = 0;
    int err = NO_ERROR;
    uint8_t voc_pkt[VOIP_BUFFER_MAX_SIZE];

    close(handle);
    flags = PCM_OUT;
    flags |= PCM_MONO;
    ALOGV("startVoipCall  handle %p", handle);

    if (deviceName(handle, flags, &devName) < 0) {
         ALOGE("Failed to get pcm device node");
         return NO_INIT;
    }

    if (devName != NULL) {
        handle->handle = pcm_open(flags, (char*)devName);
    } else {
         ALOGE("Failed to get pcm device node");
         return NO_INIT;
    }

     if (!handle->handle || (handle->handle->fd < 0)) {
          if (devName) {
              free(devName);
              devName = NULL;
          }
          ALOGE("s_open: Failed to initialize ALSA device '%s'", devName);
          return NO_INIT;
     }

     if (!pcm_ready(handle->handle)) {
         ALOGE(" pcm ready failed");
     }

     handle->handle->flags = flags;
     err = setHardwareParams(handle);

     if (err == NO_ERROR) {
         err = setSoftwareParams(handle);
     }

     err = pcm_prepare(handle->handle);
     if(err != NO_ERROR) {
         ALOGE("startVoipCall: pcm_prepare failed");
     }

     /* first write required start dsp */
     memset(&voc_pkt,0,sizeof(voc_pkt));
     pcm_write(handle->handle,&voc_pkt,handle->handle->period_size);
     handle->rxHandle = handle->handle;
     if (devName) {
         free(devName);
         devName = NULL;
     }
     ALOGV("s_open: DEVICE_IN_COMMUNICATION ");
     flags = PCM_IN;
     flags |= PCM_MONO;
     handle->handle = 0;

     if (deviceName(handle, flags, &devName) < 0) {
        ALOGE("Failed to get pcm device node");
        return NO_INIT;
     }
    if (devName != NULL) {
        handle->handle = pcm_open(flags, (char*)devName);
    } else {
         ALOGE("Failed to get pcm device node");
         return NO_INIT;
    }

     if (!handle->handle) {
         if (devName) {
             free(devName);
             devName = NULL;
         }
         ALOGE("s_open: Failed to initialize ALSA device '%s'", devName);
         return NO_INIT;
     }

     if (!pcm_ready(handle->handle)) {
        ALOGE(" pcm ready in failed");
     }

     handle->handle->flags = flags;

     err = setHardwareParams(handle);

     if (err == NO_ERROR) {
         err = setSoftwareParams(handle);
     }


     err = pcm_prepare(handle->handle);
     if(err != NO_ERROR) {
         ALOGE("DEVICE_IN_COMMUNICATION: pcm_prepare failed");
     }

     /* first read required start dsp */
     memset(&voc_pkt,0,sizeof(voc_pkt));
     pcm_read(handle->handle,&voc_pkt,handle->handle->period_size);
     if (devName) {
         free(devName);
         devName = NULL;
     }
     return NO_ERROR;
}

status_t ALSADevice::startVoiceCall(alsa_handle_t *handle, uint32_t vsid)
{
    char* devName = NULL;
    unsigned flags = 0;
    int err = NO_ERROR;

    ALOGD("startVoiceCall: handle %p", handle);
    // ASoC multicomponent requires a valid path (frontend/backend) for
    // the device to be opened

    flags = PCM_OUT | PCM_MONO;
    if (deviceName(handle, flags, &devName) < 0) {
        ALOGE("Failed to get pcm device node");
        return NO_INIT;
    }
    if (devName != NULL) {
        handle->handle = pcm_open(flags, (char*)devName);
    } else {
         ALOGE("Failed to get pcm device node");
         return NO_INIT;
    }

    if (!handle->handle || (handle->handle->fd < 0)) {
        ALOGE("startVoiceCall: could not open PCM device");
        goto Error;
    }

    handle->handle->flags = flags;
    err = setHardwareParams(handle);
    if(err != NO_ERROR) {
        ALOGE("startVoiceCall: setHardwareParams failed");
        goto Error;
    }

    err = setSoftwareParams(handle);
    if(err != NO_ERROR) {
        ALOGE("startVoiceCall: setSoftwareParams failed");
        goto Error;
    }

    err = pcm_prepare(handle->handle);
    if(err != NO_ERROR) {
        ALOGE("startVoiceCall: pcm_prepare failed");
        goto Error;
    }

    if (ioctl(handle->handle->fd, SNDRV_PCM_IOCTL_START)) {
        ALOGE("startVoiceCall:SNDRV_PCM_IOCTL_START failed\n");
        goto Error;
    }

    // Store the PCM playback device pointer in rxHandle
    handle->rxHandle = handle->handle;
    if (devName) {
        free(devName);
        devName = NULL;
    }

    // Open PCM capture device
    flags = PCM_IN | PCM_MONO;
    if (deviceName(handle, flags, &devName) < 0) {
        ALOGE("Failed to get pcm device node");
        goto Error;
    }
    if (devName != NULL) {
        handle->handle = pcm_open(flags, (char*)devName);
    } else {
         ALOGE("Failed to get pcm device node");
         return NO_INIT;
    }
    if (!handle->handle || (handle->handle->fd < 0)) {
        free(devName);
        goto Error;
    }

    handle->handle->flags = flags;
    err = setHardwareParams(handle);
    if(err != NO_ERROR) {
        ALOGE("startVoiceCall: setHardwareParams failed");
        goto Error;
    }

    err = setSoftwareParams(handle);
    if(err != NO_ERROR) {
        ALOGE("startVoiceCall: setSoftwareParams failed");
        goto Error;
    }

    err = pcm_prepare(handle->handle);
    if(err != NO_ERROR) {
        ALOGE("startVoiceCall: pcm_prepare failed");
        goto Error;
    }

    if (ioctl(handle->handle->fd, SNDRV_PCM_IOCTL_START)) {
        ALOGE("startVoiceCall:SNDRV_PCM_IOCTL_START failed\n");
        goto Error;
    }

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3()) {
        if (csd_start_voice == NULL) {
            ALOGE("csd_client_start_voice is NULL");
        } else {
            err = csd_start_voice(vsid);
            if (err < 0){
                ALOGE("s_start_voice_call: csd_client error %d\n", err);
                goto Error;
            }
        }
    }
#endif

    if (devName) {
        free(devName);
        devName = NULL;
    }
    return NO_ERROR;

Error:
    ALOGE("startVoiceCall: Failed to initialize ALSA device '%s'", devName);
    if (devName) {
        free(devName);
        devName = NULL;
    }
    close(handle, vsid);
    return NO_INIT;
}

status_t ALSADevice::startFm(alsa_handle_t *handle)
{
    char *devName = NULL;
    unsigned flags = 0;
    int err = NO_ERROR;

    ALOGV("startFm: handle %p", handle);

    // ASoC multicomponent requires a valid path (frontend/backend) for
    // the device to be opened

    flags = PCM_OUT | PCM_STEREO;
    if (deviceName(handle, flags, &devName) < 0) {
        ALOGE("Failed to get pcm device node");
        goto Error;
    }
    if (devName != NULL) {
        handle->handle = pcm_open(flags, (char*)devName);
    } else {
         ALOGE("Failed to get pcm device node");
         return NO_INIT;
    }
    if (!handle->handle || (handle->handle->fd < 0)) {
        ALOGE("startFm: could not open PCM device");
        goto Error;
    }

    handle->handle->flags = flags;
    err = setHardwareParams(handle);
    if(err != NO_ERROR) {
        ALOGE("startFm: setHardwareParams failed");
        goto Error;
    }

    err = setSoftwareParams(handle);
    if(err != NO_ERROR) {
        ALOGE("startFm: setSoftwareParams failed");
        goto Error;
    }

    err = pcm_prepare(handle->handle);
    if(err != NO_ERROR) {
        ALOGE("startFm: setSoftwareParams failed");
        goto Error;
    }

    if (ioctl(handle->handle->fd, SNDRV_PCM_IOCTL_START)) {
        ALOGE("startFm: SNDRV_PCM_IOCTL_START failed\n");
        goto Error;
    }

    // Store the PCM playback device pointer in rxHandle
    handle->rxHandle = handle->handle;
    if (devName) {
        free(devName);
        devName = NULL;
    }

    // Open PCM capture device
    flags = PCM_IN | PCM_STEREO;
    if (deviceName(handle, flags, &devName) < 0) {
        ALOGE("Failed to get pcm device node");
        goto Error;
    }
    if (devName != NULL) {
        handle->handle = pcm_open(flags, (char*)devName);
    } else {
         ALOGE("Failed to get pcm device node");
         return NO_INIT;
    }
    if (!handle->handle) {
        goto Error;
    }

    handle->handle->flags = flags;
    err = setHardwareParams(handle);
    if(err != NO_ERROR) {
        ALOGE("startFm: setHardwareParams failed");
        goto Error;
    }

    err = setSoftwareParams(handle);
    if(err != NO_ERROR) {
        ALOGE("startFm: setSoftwareParams failed");
        goto Error;
    }

    err = pcm_prepare(handle->handle);
    if(err != NO_ERROR) {
        ALOGE("startFm: pcm_prepare failed");
        goto Error;
    }

    if (ioctl(handle->handle->fd, SNDRV_PCM_IOCTL_START)) {
        ALOGE("startFm: SNDRV_PCM_IOCTL_START failed\n");
        goto Error;
    }

    mIsFmEnabled = true;
    setFmVolume(mFmVolume);
    if (devName) {
        free(devName);
        devName = NULL;
    }
    return NO_ERROR;

Error:
    if (devName) {
        free(devName);
        devName = NULL;
    }
    close(handle);
    return NO_INIT;
}

status_t ALSADevice::setFmVolume(int value)
{
    status_t err = NO_ERROR;

    mFmVolume = value;
    if (!mIsFmEnabled) {
        return INVALID_OPERATION;
    }

    setMixerControl("Internal FM RX Volume",value,0);

    return err;
}

status_t ALSADevice::setLpaVolume(int value)
{
    status_t err = NO_ERROR;

    setMixerControl("LPA RX Volume",value,0);

    return err;
}

status_t ALSADevice::start(alsa_handle_t *handle)
{
    status_t err = NO_ERROR;

    if(!handle->handle) {
        ALOGE("No active PCM driver to start");
        return err;
    }

    err = pcm_prepare(handle->handle);

    return err;
}

status_t ALSADevice::close(alsa_handle_t *handle, uint32_t vsid)
{
    int ret;
    status_t err = NO_ERROR;
    struct pcm *h = handle->rxHandle;

    handle->rxHandle = 0;
    ALOGD("close: handle %p h %p", handle, h);
    if (h) {
#ifdef QCOM_CSDCLIENT_ENABLED
        if ((!strcmp(handle->useCase, SND_USE_CASE_VERB_VOICECALL) ||
             !strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_VOICE)) ||
            (!strcmp(handle->useCase, SND_USE_CASE_VERB_VOLTE) ||
             !strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_VOLTE)) ||
            (!strcmp(handle->useCase, SND_USE_CASE_VERB_VOICE2) ||
             !strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_VOICE2)) &&
            isPlatformFusion3()) {
            if (csd_stop_voice == NULL) {
                ALOGE("csd_client_disable_device is NULL");
            } else {
                err = csd_stop_voice(vsid);
                if (err < 0) {
                    ALOGE("s_close: csd_client error %d\n", err);
                }
            }
        }
#endif

        if ((!strcmp(handle->useCase, SND_USE_CASE_VERB_DIGITAL_RADIO)) ||
            (!strcmp(handle->useCase, SND_USE_CASE_MOD_PLAY_FM))) {
            mIsFmEnabled = false;
        }

        ALOGV("close rxHandle\n");
        err = pcm_close(h);
        if(err != NO_ERROR) {
            ALOGE("close: pcm_close failed for rxHandle with err %d", err);
        }
    }

    h = handle->handle;
    handle->handle = 0;

    if (h) {
          ALOGV("close handle h %p\n", h);
        err = pcm_close(h);
        if(err != NO_ERROR) {
            ALOGE("close: pcm_close failed for handle with err %d", err);
        }
        disableDevice(handle);
    }

    return err;
}

/*
    this is same as s_close, but don't discard
    the device/mode info. This way we can still
    close the device, hit idle and power-save, reopen the pcm
    for the same device/mode after resuming
*/
status_t ALSADevice::standby(alsa_handle_t *handle)
{
    int ret;
    status_t err = NO_ERROR;
    struct pcm *h = handle->rxHandle;
    handle->rxHandle = 0;
    ALOGD("standby: handle %p h %p", handle, h);
    if (h) {
        ALOGV("standby  rxHandle\n");
        err = pcm_close(h);
        if(err != NO_ERROR) {
            ALOGE("standby: pcm_close failed for rxHandle with err %d", err);
        }
    }

    h = handle->handle;
    handle->handle = 0;

    if (h) {
        ALOGV("standby handle h %p\n", h);
        err = pcm_close(h);
        if(err != NO_ERROR) {
            ALOGE("standby: pcm_close failed for handle with err %d", err);
        }
        disableDevice(handle);
    }

    return err;
}

status_t ALSADevice::route(alsa_handle_t *handle, uint32_t devices, int mode)
{
    status_t status = NO_ERROR;

    ALOGD("route: devices 0x%x in mode %d", devices, mode);
    mCallMode = mode;
    switchDevice(handle, devices, mode);
    return status;
}

int ALSADevice::getUseCaseType(const char *useCase)
{
    ALOGV("use case is %s\n", useCase);
    if (!strncmp(useCase, SND_USE_CASE_VERB_HIFI,
            MAX_LEN(useCase,SND_USE_CASE_VERB_HIFI)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_HIFI2,
            MAX_LEN(useCase, SND_USE_CASE_VERB_HIFI2)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_HIFI_LOWLATENCY_MUSIC,
            MAX_LEN(useCase,SND_USE_CASE_VERB_HIFI_LOWLATENCY_MUSIC)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_HIFI_LOW_POWER,
            MAX_LEN(useCase,SND_USE_CASE_VERB_HIFI_LOW_POWER)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_HIFI_TUNNEL,
            MAX_LEN(useCase,SND_USE_CASE_VERB_HIFI_TUNNEL)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_HIFI2,
            MAX_LEN(useCase,SND_USE_CASE_VERB_HIFI2)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_DIGITAL_RADIO,
            MAX_LEN(useCase,SND_USE_CASE_VERB_DIGITAL_RADIO)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_MUSIC,
            MAX_LEN(useCase,SND_USE_CASE_MOD_PLAY_MUSIC)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_MUSIC2,
            MAX_LEN(useCase, SND_USE_CASE_MOD_PLAY_MUSIC2)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_LOWLATENCY_MUSIC,
            MAX_LEN(useCase,SND_USE_CASE_MOD_PLAY_LOWLATENCY_MUSIC)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_MUSIC2,
            MAX_LEN(useCase,SND_USE_CASE_MOD_PLAY_MUSIC2)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_LPA,
            MAX_LEN(useCase,SND_USE_CASE_MOD_PLAY_LPA)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_TUNNEL,
            MAX_LEN(useCase,SND_USE_CASE_MOD_PLAY_TUNNEL)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_FM,
            MAX_LEN(useCase,SND_USE_CASE_MOD_PLAY_FM))) {
        return USECASE_TYPE_RX;
    } else if (!strncmp(useCase, SND_USE_CASE_VERB_HIFI_REC,
            MAX_LEN(useCase,SND_USE_CASE_VERB_HIFI_REC)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_HIFI_LOWLATENCY_REC,
            MAX_LEN(useCase,SND_USE_CASE_VERB_HIFI_LOWLATENCY_REC)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED,
            MAX_LEN(useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_FM_REC,
            MAX_LEN(useCase,SND_USE_CASE_VERB_FM_REC)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_FM_A2DP_REC,
            MAX_LEN(useCase,SND_USE_CASE_VERB_FM_A2DP_REC)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC,
            MAX_LEN(useCase,SND_USE_CASE_MOD_CAPTURE_MUSIC)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_CAPTURE_LOWLATENCY_MUSIC,
            MAX_LEN(useCase,SND_USE_CASE_MOD_CAPTURE_LOWLATENCY_MUSIC)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_CAPTURE_FM,
            MAX_LEN(useCase,SND_USE_CASE_MOD_CAPTURE_FM)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_CAPTURE_A2DP_FM,
            MAX_LEN(useCase, SND_USE_CASE_MOD_CAPTURE_A2DP_FM)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED,
            MAX_LEN(useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED))) {
        return USECASE_TYPE_TX;
    } else if (!strncmp(useCase, SND_USE_CASE_VERB_VOICECALL,
            MAX_LEN(useCase,SND_USE_CASE_VERB_VOICECALL)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_IP_VOICECALL,
            MAX_LEN(useCase,SND_USE_CASE_VERB_IP_VOICECALL)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_UL_REC,
            MAX_LEN(useCase,SND_USE_CASE_VERB_UL_REC)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_DL_REC,
            MAX_LEN(useCase,SND_USE_CASE_VERB_DL_REC)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_UL_DL_REC,
            MAX_LEN(useCase,SND_USE_CASE_VERB_UL_DL_REC)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_INCALL_REC,
            MAX_LEN(useCase,SND_USE_CASE_VERB_INCALL_REC)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_VOICE,
            MAX_LEN(useCase,SND_USE_CASE_MOD_PLAY_VOICE)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_VOIP,
            MAX_LEN(useCase,SND_USE_CASE_MOD_PLAY_VOIP)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_CAPTURE_VOICE_UL,
            MAX_LEN(useCase,SND_USE_CASE_MOD_CAPTURE_VOICE_UL)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_CAPTURE_VOICE_DL,
            MAX_LEN(useCase,SND_USE_CASE_MOD_CAPTURE_VOICE_DL)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_CAPTURE_VOICE_UL_DL,
            MAX_LEN(useCase,SND_USE_CASE_MOD_CAPTURE_VOICE_UL_DL)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_CAPTURE_VOICE,
            MAX_LEN(useCase, SND_USE_CASE_MOD_CAPTURE_VOICE)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_VOICE2,
            MAX_LEN(useCase, SND_USE_CASE_VERB_VOICE2)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_VOICE2,
            MAX_LEN(useCase, SND_USE_CASE_MOD_PLAY_VOICE2)) ||
        !strncmp(useCase, SND_USE_CASE_VERB_VOLTE,
            MAX_LEN(useCase,SND_USE_CASE_VERB_VOLTE)) ||
        !strncmp(useCase, SND_USE_CASE_MOD_PLAY_VOLTE,
            MAX_LEN(useCase, SND_USE_CASE_MOD_PLAY_VOLTE))) {
        return (USECASE_TYPE_RX | USECASE_TYPE_TX);
    } else {
        ALOGV("unknown use case %s\n", useCase);
        return 0;
    }
}

void ALSADevice::disableDevice(alsa_handle_t *handle)
{
    unsigned usecase_type = 0;
    int i, mods_size;
    char *useCase;
    const char **mods_list;

    snd_use_case_get(handle->ucMgr, "_verb", (const char **)&useCase);
    if (useCase != NULL) {
        if (!strncmp(useCase, handle->useCase, MAX_UC_LEN)) {
            snd_use_case_set(handle->ucMgr, "_verb", SND_USE_CASE_VERB_INACTIVE);
        } else {
            snd_use_case_set(handle->ucMgr, "_dismod", handle->useCase);
        }
        free(useCase);
        snd_use_case_get(handle->ucMgr, "_verb", (const char **)&useCase);
        if (strncmp(useCase, SND_USE_CASE_VERB_INACTIVE,
               strlen(SND_USE_CASE_VERB_INACTIVE)))
            usecase_type |= getUseCaseType(useCase);
        mods_size = snd_use_case_get_list(handle->ucMgr, "_enamods", &mods_list);
        ALOGV("Number of modifiers %d\n", mods_size);
        if (mods_size) {
            for(i = 0; i < mods_size; i++) {
                ALOGV("index %d modifier %s\n", i, mods_list[i]);
                usecase_type |= getUseCaseType(mods_list[i]);
            }
        }
        ALOGV("usecase_type is %d\n", usecase_type);
        if (!(usecase_type & USECASE_TYPE_TX) && (strncmp(mCurTxUCMDevice, "None", 4)))
            snd_use_case_set(handle->ucMgr, "_disdev", mCurTxUCMDevice);
        if (!(usecase_type & USECASE_TYPE_RX) && (strncmp(mCurRxUCMDevice, "None", 4)))
            snd_use_case_set(handle->ucMgr, "_disdev", mCurRxUCMDevice);
    } else {
        ALOGE("Invalid state, no valid use case found to disable");
    }
    free(useCase);
}

char *ALSADevice::getUCMDeviceFromAcdbId(int acdb_id)
{
     switch(acdb_id) {
        case DEVICE_HANDSET_RX_ACDB_ID:
             return strdup(SND_USE_CASE_DEV_HANDSET);
        case DEVICE_SPEAKER_MONO_RX_ACDB_ID:
        case DEVICE_SPEAKER_RX_ACDB_ID:
             return strdup(SND_USE_CASE_DEV_SPEAKER);
        case DEVICE_HEADSET_RX_ACDB_ID:
             return strdup(SND_USE_CASE_DEV_HEADPHONES);
        case DEVICE_TTY_HEADSET_MONO_RX_ACDB_ID:
             return strdup(SND_USE_CASE_DEV_TTY_HEADSET_RX);
        case DEVICE_ANC_HEADSET_STEREO_RX_ACDB_ID:
             return strdup(SND_USE_CASE_DEV_ANC_HEADSET);
        default:
             return NULL;
     }
}

char* ALSADevice::getUCMDevice(uint32_t devices, int input, char *rxDevice)
{
    if (!input) {
        ALOGV("getUCMDevice for output device: devices:%x is input device:%d",devices,input);
        if (!(mDevSettingsFlag & TTY_OFF) &&
            (mCallMode == AUDIO_MODE_IN_CALL) &&
            ((devices & AudioSystem::DEVICE_OUT_WIRED_HEADSET)
             || (devices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE)
#ifdef QCOM_ANC_HEADSET_ENABLED
             || (devices & AudioSystem::DEVICE_OUT_ANC_HEADSET)
             || (devices & AudioSystem::DEVICE_OUT_ANC_HEADPHONE)
#endif
             )) {
             if (mDevSettingsFlag & TTY_VCO) {
                 return strdup(SND_USE_CASE_DEV_TTY_HEADSET_RX);
             } else if (mDevSettingsFlag & TTY_FULL) {
                 return strdup(SND_USE_CASE_DEV_TTY_FULL_RX);
             } else if (mDevSettingsFlag & TTY_HCO) {
                 return strdup(SND_USE_CASE_DEV_TTY_HANDSET_RX); /* HANDSET RX */
             }
        } else if (devices & AudioSystem::DEVICE_OUT_ALL_A2DP &&
                   devices & AudioSystem::DEVICE_OUT_SPEAKER) {
            return strdup(SND_USE_CASE_DEV_PROXY_RX_SPEAKER);
        } else if (devices & AudioSystem::DEVICE_OUT_ALL_A2DP) {
            return strdup(SND_USE_CASE_DEV_PROXY_RX);
        } else if (devices & AUDIO_DEVICE_OUT_ALL_USB &&
                   devices & AudioSystem::DEVICE_OUT_SPEAKER) {
            return strdup(SND_USE_CASE_DEV_PROXY_RX_SPEAKER);
        } else if (devices & AUDIO_DEVICE_OUT_ALL_USB) {
            return strdup(SND_USE_CASE_DEV_PROXY_RX);
        } else if ((devices & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET ||
                    devices & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET) &&
                    devices & AudioSystem::DEVICE_OUT_SPEAKER) {
             ALOGD("getUCMDevice for output device: devices:%x is input device:%d, reached debug #1",devices,input);
#ifdef DOCK_USBAUDIO_ENABLED
             if (mCallMode == AUDIO_MODE_RINGTONE) {
                 return strdup(SND_USE_CASE_DEV_SPEAKER); /* Voice SPEAKER RX */
             } else {
                 return strdup(SND_USE_CASE_DEV_DOCK);
             }
#else
             return strdup(SND_USE_CASE_DEV_USB_PROXY_RX_SPEAKER); /* USB PROXY RX + SPEAKER */
#endif
        } else if ((devices & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET) ||
                  (devices & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET)) {
             ALOGD("getUCMDevice for output device: devices:%x is input device:%d, reached debug #2",devices,input);
#ifdef DOCK_USBAUDIO_ENABLED
             if (mCallMode == AUDIO_MODE_RINGTONE) {
                 return strdup(SND_USE_CASE_DEV_USB_PROXY_RX); /* PROXY RX */
             } else {
                 return strdup(SND_USE_CASE_DEV_DOCK); /* Dock RX */
             }
#else
             return strdup(SND_USE_CASE_DEV_USB_PROXY_RX); /* PROXY RX */
#endif
#ifdef QCOM_PROXY_DEVICE_ENABLED
        } else if( (devices & AudioSystem::DEVICE_OUT_SPEAKER) &&
                   (devices & AudioSystem::DEVICE_OUT_PROXY) &&
                   ((devices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) ||
                    (devices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) ) ) {
            if (mDevSettingsFlag & ANC_FLAG) {
                return strdup(SND_USE_CASE_DEV_PROXY_RX_SPEAKER_ANC_HEADSET);
            } else {
                return strdup(SND_USE_CASE_DEV_PROXY_RX_SPEAKER_HEADSET);
            }
#endif
        } else if ((devices & AudioSystem::DEVICE_OUT_SPEAKER) &&
            ((devices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) ||
            (devices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE))) {
            if (mDevSettingsFlag & ANC_FLAG) {
                return strdup(SND_USE_CASE_DEV_SPEAKER_ANC_HEADSET); /* COMBO SPEAKER+ANC HEADSET RX */
            } else {
                return strdup(SND_USE_CASE_DEV_SPEAKER_HEADSET); /* COMBO SPEAKER+HEADSET RX */
            }
        } else if ((devices & AudioSystem::DEVICE_OUT_SPEAKER) &&
            ((devices & AudioSystem::DEVICE_OUT_AUX_DIGITAL))) {
            return strdup(SND_USE_CASE_DEV_HDMI_SPEAKER);
#ifdef QCOM_ANC_HEADSET_ENABLED
        } else if ((devices & AudioSystem::DEVICE_OUT_PROXY) &&
                   ((devices & AudioSystem::DEVICE_OUT_ANC_HEADSET)||
                    (devices & AudioSystem::DEVICE_OUT_ANC_HEADPHONE)) ) {
            return strdup(SND_USE_CASE_DEV_PROXY_RX_ANC_HEADSET);
        } else if ((devices & AudioSystem::DEVICE_OUT_SPEAKER) &&
            ((devices & AudioSystem::DEVICE_OUT_ANC_HEADSET) ||
            (devices & AudioSystem::DEVICE_OUT_ANC_HEADPHONE))) {
            return strdup(SND_USE_CASE_DEV_SPEAKER_ANC_HEADSET); /* COMBO SPEAKER+ANC HEADSET RX */
#endif
#ifdef QCOM_FM_ENABLED
        } else if ((devices & AudioSystem::DEVICE_OUT_SPEAKER) &&
                 (devices & AudioSystem::DEVICE_OUT_FM_TX)) {
            return strdup(SND_USE_CASE_DEV_SPEAKER_FM_TX); /* COMBO SPEAKER+FM_TX RX */
#endif
#ifdef QCOM_PROXY_DEVICE_ENABLED
        } else if ((devices & AudioSystem::DEVICE_OUT_SPEAKER) &&
                 (devices & AudioSystem::DEVICE_OUT_PROXY)) {
            return strdup(SND_USE_CASE_DEV_PROXY_RX_SPEAKER); /* COMBO SPEAKER + PROXY RX */
        } else if ((devices & AudioSystem::DEVICE_OUT_EARPIECE) &&
                 (devices & AudioSystem::DEVICE_OUT_PROXY)) {
            return strdup(SND_USE_CASE_DEV_PROXY_RX_HANDSET); /* COMBO EARPIECE + PROXY RX */
#endif
        } else if (devices & AudioSystem::DEVICE_OUT_EARPIECE) {
            if (mCallMode == AUDIO_MODE_IN_CALL ||
                mCallMode == AUDIO_MODE_IN_COMMUNICATION) {
                if (shouldUseHandsetAnc(mDevSettingsFlag, mInChannels)) {
                    return strdup(SND_USE_CASE_DEV_ANC_HANDSET); /* ANC Handset RX */
                } else {
                    return strdup(SND_USE_CASE_DEV_VOC_EARPIECE); /* Voice HANDSET RX */
                }
            } else {
                return strdup(SND_USE_CASE_DEV_EARPIECE); /* HANDSET RX */
            }
        } else if (devices & AudioSystem::DEVICE_OUT_SPEAKER) {
#ifdef SEPERATED_VOICE_SPEAKER
            if (mCallMode == AUDIO_MODE_IN_CALL ||
                mCallMode == AUDIO_MODE_IN_COMMUNICATION) {
                return strdup(SND_USE_CASE_DEV_VOC_SPEAKER); /* Voice SPEAKER RX */
            }
#endif
            return strdup(SND_USE_CASE_DEV_SPEAKER); /* SPEAKER RX */
        } else if ((devices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) ||
                   (devices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE)) {
            if (mDevSettingsFlag & ANC_FLAG) {
                if (mCallMode == AUDIO_MODE_IN_CALL ||
                    mCallMode == AUDIO_MODE_IN_COMMUNICATION) {
                    return strdup(SND_USE_CASE_DEV_VOC_ANC_HEADSET); /* Voice ANC HEADSET RX */
                } else {
                    return strdup(SND_USE_CASE_DEV_ANC_HEADSET); /* ANC HEADSET RX */
                }
            } else {
                if (mCallMode == AUDIO_MODE_IN_CALL ||
                    mCallMode == AUDIO_MODE_IN_COMMUNICATION) {
                    return strdup(SND_USE_CASE_DEV_VOC_HEADPHONE); /* Voice HEADSET RX */
                } else {
                    return strdup(SND_USE_CASE_DEV_HEADPHONES); /* HEADSET RX */
                }
            }
#ifdef QCOM_ANC_HEADSET_ENABLED
        } else if ((devices & AudioSystem::DEVICE_OUT_PROXY) &&
                   ((devices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) ||
                    (devices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE))) {
            if (mDevSettingsFlag & ANC_FLAG) {
                return strdup(SND_USE_CASE_DEV_PROXY_RX_ANC_HEADSET);
            } else {
                return strdup(SND_USE_CASE_DEV_PROXY_RX_HEADSET);
            }
        } else if ((devices & AudioSystem::DEVICE_OUT_ANC_HEADSET) ||
                   (devices & AudioSystem::DEVICE_OUT_ANC_HEADPHONE)) {
            if (mCallMode == AUDIO_MODE_IN_CALL ||
                mCallMode == AUDIO_MODE_IN_COMMUNICATION) {
                return strdup(SND_USE_CASE_DEV_VOC_ANC_HEADSET); /* Voice ANC HEADSET RX */
            } else {
                return strdup(SND_USE_CASE_DEV_ANC_HEADSET); /* ANC HEADSET RX */
            }
#endif
        } else if ((devices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO) ||
                  (devices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET) ||
                  (devices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT)) {
            if (mBtscoSamplerate == BTSCO_RATE_16KHZ)
                return strdup(SND_USE_CASE_DEV_BTSCO_WB_RX); /* BTSCO RX*/
            else
                return strdup(SND_USE_CASE_DEV_BTSCO_NB_RX); /* BTSCO RX*/
        } else if (devices & AudioSystem::DEVICE_OUT_AUX_DIGITAL) {
            return strdup(SND_USE_CASE_DEV_HDMI); /* HDMI RX */
#ifdef QCOM_PROXY_DEVICE_ENABLED
        } else if (devices & AudioSystem::DEVICE_OUT_PROXY) {
            return strdup(SND_USE_CASE_DEV_PROXY_RX); /* PROXY RX */
#endif
#ifdef QCOM_FM_ENABLED
        } else if (devices & AudioSystem::DEVICE_OUT_FM_TX) {
            return strdup(SND_USE_CASE_DEV_FM_TX); /* FM Tx */
#endif
        } else if (devices & AudioSystem::DEVICE_OUT_DEFAULT) {
            return strdup(SND_USE_CASE_DEV_SPEAKER); /* SPEAKER RX */
        } else {
            ALOGD("No valid output device: %u", devices);
        }
    } else {
        ALOGV("getUCMDevice for input device: devices:%x is input device:%d",devices,input);
        if (!(mDevSettingsFlag & TTY_OFF) &&
            (mCallMode == AUDIO_MODE_IN_CALL) &&
            ((devices & AudioSystem::DEVICE_IN_WIRED_HEADSET)
#ifdef QCOM_ANC_HEADSET_ENABLED
             || (devices & AudioSystem::DEVICE_IN_ANC_HEADSET)
#endif
             )) {
             if (mDevSettingsFlag & TTY_HCO) {
                 return strdup(SND_USE_CASE_DEV_TTY_HEADSET_TX);
             } else if (mDevSettingsFlag & TTY_FULL) {
                 return strdup(SND_USE_CASE_DEV_TTY_FULL_TX);
             } else if (mDevSettingsFlag & TTY_VCO) {
                 if (!strncmp(mMicType, "analog", 6)) {
                     return strdup(SND_USE_CASE_DEV_TTY_HANDSET_ANALOG_TX);
                 } else {
                     return strdup(SND_USE_CASE_DEV_TTY_HANDSET_TX);
                 }
             }
        } else if (devices & AudioSystem::DEVICE_IN_BUILTIN_MIC) {
            if (!strncmp(mMicType, "analog", 6)) {
                return strdup(SND_USE_CASE_DEV_HANDSET); /* HANDSET TX */
            } else {
                if ((mDevSettingsFlag & DMIC_FLAG) && (mInChannels == 1)) {
#ifdef USES_FLUENCE_INCALL
                  if (mCallMode == AUDIO_MODE_IN_CALL) {
#endif
                    if (((rxDevice != NULL) &&
                        (!strncmp(rxDevice, SND_USE_CASE_DEV_SPEAKER,
                        (strlen(SND_USE_CASE_DEV_SPEAKER)+1))
#ifdef SEPERATED_VOICE_SPEAKER
                        || !strncmp(rxDevice, SND_USE_CASE_DEV_VOC_SPEAKER,
                        (strlen(SND_USE_CASE_DEV_VOC_SPEAKER)+1))
#endif
                        )) ||
                        ((rxDevice == NULL) &&
                        !strncmp(mCurRxUCMDevice, SND_USE_CASE_DEV_SPEAKER,
                        (strlen(SND_USE_CASE_DEV_SPEAKER)+1)))) {
                        if (mFluenceMode == FLUENCE_MODE_ENDFIRE) {
                            if (mIsSglte == false) {
                                return strdup(SND_USE_CASE_DEV_SPEAKER_DUAL_MIC_ENDFIRE); /* DUALMIC EF TX */
                            }
                            else {
                                return strdup(SND_USE_CASE_DEV_SPEAKER_DUAL_MIC_ENDFIRE_SGLTE); /* DUALMIC EF TX */
                            }
                        } else if (mFluenceMode == FLUENCE_MODE_BROADSIDE) {
                            return strdup(SND_USE_CASE_DEV_SPEAKER_DUAL_MIC_BROADSIDE); /* DUALMIC BS TX */
                        }
                    } else {
                        if (mFluenceMode == FLUENCE_MODE_ENDFIRE) {
                            if (mIsSglte == false) {
                                if ((rxDevice != NULL) &&
                                    !strncmp(rxDevice, SND_USE_CASE_DEV_ANC_HANDSET,
                                        strlen(SND_USE_CASE_DEV_ANC_HANDSET) + 1)) {
                                    /* if using ANC_HANDSET, already in-call */
                                    return strdup(SND_USE_CASE_DEV_AANC_DMIC_ENDFIRE); /* DUALMIC AANC TX */
                                } else {
                                    return strdup(SND_USE_CASE_DEV_DUAL_MIC_ENDFIRE); /* DUALMIC EF TX */
                                }
                            }
                            else {
                                return strdup(SND_USE_CASE_DEV_DUAL_MIC_ENDFIRE_SGLTE); /* DUALMIC EF TX */
                            }
                        } else if (mFluenceMode == FLUENCE_MODE_BROADSIDE) {
                            return strdup(SND_USE_CASE_DEV_DUAL_MIC_BROADSIDE); /* DUALMIC BS TX */
                        }
                    }
#ifdef USES_FLUENCE_INCALL
                  }
#endif
                } else if ((mDevSettingsFlag & DMIC_FLAG) && (mInChannels > 1)) {
                    if (((rxDevice != NULL) &&
                        !strncmp(rxDevice, SND_USE_CASE_DEV_SPEAKER,
                        (strlen(SND_USE_CASE_DEV_SPEAKER)+1))) ||
                        ((rxDevice == NULL) &&
                        !strncmp(mCurRxUCMDevice, SND_USE_CASE_DEV_SPEAKER,
                        (strlen(SND_USE_CASE_DEV_SPEAKER)+1)))) {
                            if (mIsSglte == false) {
                                return strdup(SND_USE_CASE_DEV_SPEAKER_DUAL_MIC_STEREO); /* DUALMIC EF TX */
                            }
                            else {
                                return strdup(SND_USE_CASE_DEV_SPEAKER_DUAL_MIC_STEREO_SGLTE); /* DUALMIC EF TX */
                            }
                    } else {
                            if (mIsSglte == false) {
                                return strdup(SND_USE_CASE_DEV_DUAL_MIC_HANDSET_STEREO); /* DUALMIC EF TX */
                            }
                            else {
                                return strdup(SND_USE_CASE_DEV_DUAL_MIC_HANDSET_STEREO_SGLTE); /* DUALMIC EF TX */
                            }
                    }
                } else if ((mDevSettingsFlag & QMIC_FLAG) && (mInChannels == 1)) {
                    if (((rxDevice != NULL) &&
                        !strncmp(rxDevice, SND_USE_CASE_DEV_SPEAKER,
                        (strlen(SND_USE_CASE_DEV_SPEAKER)+1))) ||
                        ((rxDevice == NULL) &&
                        !strncmp(mCurRxUCMDevice, SND_USE_CASE_DEV_SPEAKER,
                        (strlen(SND_USE_CASE_DEV_SPEAKER)+1)))) {
                            return strdup(SND_USE_CASE_DEV_QUAD_MIC); /* QUADMIC TX */
                    } else {
                        if ((rxDevice != NULL) &&
                            !strncmp(rxDevice, SND_USE_CASE_DEV_ANC_HANDSET,
                                strlen(SND_USE_CASE_DEV_ANC_HANDSET) + 1)) {
                            /* if using ANC_HANDSET, already in-call */
                            return strdup(SND_USE_CASE_DEV_AANC_LINE); /* AANC LINE TX */
                        } else {
                            return strdup(SND_USE_CASE_DEV_LINE);
                        }
                    }
                }
#ifdef QCOM_SSR_ENABLED
                else if ((mDevSettingsFlag & QMIC_FLAG) && (mInChannels > 1)) {
                    return strdup(SND_USE_CASE_DEV_SSR_QUAD_MIC);
                } else if ((mDevSettingsFlag & SSRQMIC_FLAG) && (mInChannels > 1)){
                    ALOGV("return SSRQMIC_FLAG: 0x%x devices:0x%x",mDevSettingsFlag,devices);
                    // Mapping for quad mic input device.
                    return strdup(SND_USE_CASE_DEV_SSR_QUAD_MIC); /* SSR Quad MIC */
                }
#endif
#ifdef SEPERATED_AUDIO_INPUT
                if(mInputSource == AUDIO_SOURCE_VOICE_RECOGNITION) {
                    return strdup(SND_USE_CASE_DEV_VOICE_RECOGNITION ); /* VOICE RECOGNITION TX */
                }
#endif
                else {
                    if ((rxDevice != NULL) &&
                        !strncmp(rxDevice, SND_USE_CASE_DEV_ANC_HANDSET,
                            strlen(SND_USE_CASE_DEV_ANC_HANDSET) + 1)) {
                        return strdup(SND_USE_CASE_DEV_AANC_LINE); /* AANC LINE TX */
                    } else {
                        return strdup(SND_USE_CASE_DEV_LINE); /* BUILTIN-MIC TX */
                    }
                }
            }
        } else if (devices & AudioSystem::DEVICE_IN_AUX_DIGITAL) {
            return strdup(SND_USE_CASE_DEV_HDMI_TX); /* HDMI TX */
        } else if ((devices & AudioSystem::DEVICE_IN_WIRED_HEADSET)) {
            return strdup(SND_USE_CASE_DEV_HEADSET); /* HEADSET TX */
#ifdef QCOM_ANC_HEADSET_ENABLED
        } else if (devices & AudioSystem::DEVICE_IN_ANC_HEADSET) {
            return strdup(SND_USE_CASE_DEV_HEADSET); /* HEADSET TX */
#endif
        } else if (devices & AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
             if (mBtscoSamplerate == BTSCO_RATE_16KHZ)
                 return strdup(SND_USE_CASE_DEV_BTSCO_WB_TX); /* BTSCO TX*/
             else
                 return strdup(SND_USE_CASE_DEV_BTSCO_NB_TX); /* BTSCO TX*/
#ifdef QCOM_USBAUDIO_ENABLED
        } else if (devices & AudioSystem::DEVICE_IN_ANLG_DOCK_HEADSET) {
            if ((mCallMode == AUDIO_MODE_IN_CALL) ||
                (mCallMode == AUDIO_MODE_IN_COMMUNICATION)) {
                if ((rxDevice != NULL) &&
                   (!strncmp(rxDevice, SND_USE_CASE_DEV_USB_PROXY_RX,
                    (strlen(SND_USE_CASE_DEV_USB_PROXY_RX)+1)))) {
                    return strdup(SND_USE_CASE_DEV_USB_PROXY_TX); /* USB PROXY TX */
                } else if ((rxDevice != NULL) &&
                   (!strncmp(rxDevice, SND_USE_CASE_DEV_PROXY_RX,
                    (strlen(SND_USE_CASE_DEV_PROXY_RX)+1)))) {
                    return strdup(SND_USE_CASE_DEV_PROXY_TX); /* PROXY TX */
                } else {
                    return strdup(SND_USE_CASE_DEV_USB_PROXY_TX); /* USB PROXY TX */
                }
            } else {
                return strdup(SND_USE_CASE_DEV_USB_PROXY_TX); /* USB PROXY TX */
            }
#endif
#ifdef QCOM_PROXY_DEVICE_ENABLED
        } else if (devices & AudioSystem::DEVICE_IN_PROXY) {
            return strdup(SND_USE_CASE_DEV_PROXY_TX); /* PROXY TX */
#endif
        } else if ((devices & AudioSystem::DEVICE_IN_COMMUNICATION) ||
                   (devices & AudioSystem::DEVICE_IN_VOICE_CALL)) {
            /* Nothing to be done, use current active device */
            if (strncmp(mCurTxUCMDevice, "None", 4)) {
                return strdup(mCurTxUCMDevice);
            }
#ifdef QCOM_FM_ENABLED
        } else if ((devices & AudioSystem::DEVICE_IN_FM_RX) ||
                   (devices & AudioSystem::DEVICE_IN_FM_RX_A2DP)) {
            /* Nothing to be done, use current tx device or set dummy device */
            if (strncmp(mCurTxUCMDevice, "None", 4)) {
                return strdup(mCurTxUCMDevice);
            } else {
                return strdup(SND_USE_CASE_DEV_DUMMY_TX);
            }
#endif
        } else if ((devices & AudioSystem::DEVICE_IN_AMBIENT) ||
                   (devices & AudioSystem::DEVICE_IN_BACK_MIC)) {
            ALOGI("No proper mapping found with UCM device list, setting default");
            if (!strncmp(mMicType, "analog", 6)) {
                return strdup(SND_USE_CASE_DEV_HANDSET); /* HANDSET TX */
            } else {
#ifdef SEPERATED_AUDIO_INPUT
                if (mCallMode == AUDIO_MODE_IN_CALL) {
                    return strdup(SND_USE_CASE_DEV_VOC_LINE); /* Voice BUILTIN-MIC TX */
                } else if(mInputSource == AUDIO_SOURCE_CAMCORDER) {
                    return strdup(SND_USE_CASE_DEV_CAMCORDER_TX ); /* CAMCORDER TX */
                } else
#endif
                    return strdup(SND_USE_CASE_DEV_LINE); /* BUILTIN-MIC TX */
            }
        } else {
            ALOGD("No valid input device: %u", devices);
        }
    }
    return NULL;
}

void ALSADevice::setVoiceVolume(int vol)
{
    int err = 0;

    ALOGD("setVoiceVolume: volume %d", vol);
    setMixerControl("Voice Rx Volume", vol, 0);

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3()) {
        if (csd_volume == NULL) {
            ALOGE("csd_client_volume is NULL");
        } else {
            err = csd_volume(ALL_SESSION_VSID, vol);
            if (err < 0) {
                ALOGE("s_set_voice_volume: csd_client error %d", err);
            }
        }
    }
#endif
}

void ALSADevice::setVoice2Volume(int vol)
{
    int err = 0;

    ALOGD("setVoice2Volume: volume %d", vol);
    setMixerControl("Voice2 Rx Volume", vol, 0);

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3()) {
        if (csd_volume == NULL) {
            ALOGE("csd_client_volume is NULL");
        } else {
            err = csd_volume(ALL_SESSION_VSID, vol);
            if (err < 0) {
                ALOGE("s_set_voice_volume: csd_client error %d", err);
            }
        }
    }
#endif
}

void ALSADevice::setVoLTEVolume(int vol)
{
    int err = 0;

    ALOGD("setVoLTEVolume: volume %d", vol);
    setMixerControl("VoLTE Rx Volume", vol, 0);

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3()) {
        if (csd_volume == NULL) {
            ALOGE("csd_client_volume is NULL");
        } else {
            err = csd_volume(ALL_SESSION_VSID, vol);
            if (err < 0) {
                ALOGE("s_set_voice_volume: csd_client error %d", err);
            }
        }
    }
#endif
}

void ALSADevice::setVoipVolume(int vol)
{
    ALOGD("setVoipVolume: volume %d", vol);
    setMixerControl("Voip Rx Volume", vol, 0);
}

void ALSADevice::setMicMute(int state)
{
    int err = 0;

    ALOGD("setMicMute: state %d", state);
    setMixerControl("Voice Tx Mute", state, 0);

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3()) {
        if (csd_mic_mute == NULL) {
            ALOGE("csd_mic_mute is NULL");
        } else {
            err = csd_mic_mute(ALL_SESSION_VSID, state);
            if (err < 0) {
                ALOGE("s_set_mic_mute: csd_client error %d", err);
            }
        }
    }
#endif
}

void ALSADevice::setVoice2MicMute(int state)
{
    int err = 0;

    ALOGD("setVoice2MicMute: state %d", state);
    setMixerControl("Voice2 Tx Mute", state, 0);

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3()) {
        if (csd_mic_mute == NULL) {
            ALOGE("csd_mic_mute is NULL");
        } else {
            err = csd_mic_mute(ALL_SESSION_VSID, state);
            if (err < 0) {
                ALOGE("s_set_mic_mute: csd_client error %d", err);
            }
        }

    }
#endif
}

void ALSADevice::setVoLTEMicMute(int state)
{
    int err = 0;

    ALOGD("setVolteMicMute: state %d", state);
    setMixerControl("VoLTE Tx Mute", state, 0);

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3()) {
        if (csd_mic_mute == NULL) {
            ALOGE("csd_mic_mute is NULL");
        } else {
            err = csd_mic_mute(ALL_SESSION_VSID, state);
            if (err < 0) {
                ALOGE("s_set_mic_mute: csd_client error %d", err);
            }
        }
    }
#endif
}

void ALSADevice::setVoipMicMute(int state)
{
    ALOGD("setVoipMicMute: state %d", state);
    setMixerControl("Voip Tx Mute", state, 0);
}

void ALSADevice::setVoipConfig(int mode, int rate)
{
    char** setValues;
    ALOGD("setVoipConfig: mode %d,rate %d", mode, rate);

    setValues = (char**)malloc(2*sizeof(char*));
    if (setValues == NULL) {
          return;
    }
    setValues[0] = (char*)malloc(4*sizeof(char));
    if (setValues[0] == NULL) {
          free(setValues);
          return;
    }

    setValues[1] = (char*)malloc(8*sizeof(char));
    if (setValues[1] == NULL) {
          free(setValues);
          free(setValues[0]);
          return;
    }

    sprintf(setValues[0], "%d",mode);
    sprintf(setValues[1], "%d",rate);

    setMixerControlExt("Voip Mode Rate Config", 2, setValues);
    free(setValues[1]);
    free(setValues[0]);
    free(setValues);
    return;
}

void ALSADevice::setBtscoRate(int rate)
{
    mBtscoSamplerate = rate;
}

void ALSADevice::enableWideVoice(bool flag, uint32_t vsid)
{
    int err = 0;

    ALOGD("enableWideVoice: flag %d", flag);
    if(flag == true) {
        setMixerControl("Widevoice Enable", 1, 0);
    } else {
        setMixerControl("Widevoice Enable", 0, 0);
    }

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3()) {
        if (csd_wide_voice == NULL) {
            ALOGE("csd_wide_voice is NULL");
        } else {
            err = csd_wide_voice(vsid, flag);
            if (err < 0) {
                ALOGE("enableWideVoice: csd_client_wide_voice error %d", err);
            }
        }
    }
#endif
}

void ALSADevice::setVocRecMode(uint8_t mode)
{
    ALOGD("setVocRecMode: mode %d", mode);
    setMixerControl("Incall Rec Mode", mode, 0);
}

void ALSADevice::enableFENS(bool flag, uint32_t vsid)
{
    int err = 0;

    ALOGD("enableFENS: flag %d", flag);
    if(flag == true) {
        setMixerControl("FENS Enable", 1, 0);
        setMixerControl("FENS_VOIP Enable", 1 , 0);
    } else {
        setMixerControl("FENS Enable", 0, 0);
        setMixerControl("FENS_VOIP Enable", 0 , 0);
    }

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3()) {
        if (csd_fens == NULL) {
            ALOGE("csd_fens is NULL");
        } else {
            err = csd_fens(vsid, flag);
            if (err < 0) {
                ALOGE("s_enable_fens: csd_client error %d", err);
            }
        }
    }
#endif
}

void ALSADevice::enableSlowTalk(bool flag, uint32_t vsid)
{
    int err = 0;

    ALOGD("enableSlowTalk: flag %d", flag);
    if(flag == true) {
        setMixerControl("Slowtalk Enable", 1, 0);
    } else {
        setMixerControl("Slowtalk Enable", 0, 0);
    }

#ifdef QCOM_CSDCLIENT_ENABLED
    if (isPlatformFusion3()) {
        if (csd_slow_talk == NULL) {
            ALOGE("csd_slow_talk is NULL");
        } else {
            err = csd_slow_talk(vsid, flag);
            if (err < 0) {
                ALOGE("s_enable_slow_talk: csd_client error %d", err);
            }
        }
    }
#endif
}

void ALSADevice::setFlags(uint32_t flags)
{
    ALOGV("setFlags: flags %d", flags);
    mDevSettingsFlag = flags;
}

status_t ALSADevice::setCompressedVolume(int value)
{
    status_t err = NO_ERROR;

    setMixerControl("COMPRESSED RX Volume",value,0);

    return err;
}

status_t ALSADevice::setChannelMap(alsa_handle_t *handle, int maxChannels)
{
    status_t status = NO_ERROR;
    char channelMap[maxChannels];

    memset(channelMap, 0, sizeof(channelMap));
    switch (handle->channels) {
    case 3:
    case 4:
    case 5:
        ALOGE("TODO: Investigate and add appropriate channel map appropriately");
        break;
    case 6:
        channelMap[0] = PCM_CHANNEL_FL;
        channelMap[1] = PCM_CHANNEL_FR;
        channelMap[2] = PCM_CHANNEL_FC;
        channelMap[3] = PCM_CHANNEL_LFE;
        channelMap[4] = PCM_CHANNEL_LB;
        channelMap[5] = PCM_CHANNEL_RB;
        break;
    case 7:
    case 8:
        channelMap[0] = PCM_CHANNEL_FL;
        channelMap[1] = PCM_CHANNEL_FR;
        channelMap[2] = PCM_CHANNEL_FC;
        channelMap[3] = PCM_CHANNEL_LFE;
        channelMap[4] = PCM_CHANNEL_LB;
        channelMap[5] = PCM_CHANNEL_RB;
        channelMap[6] = PCM_CHANNEL_FLC;
        channelMap[7] = PCM_CHANNEL_FRC;
        break;
    default:
        ALOGE("un supported channels for setting channel map");
        return -1;
    }

    if(handle)
        status = pcm_set_channel_map(handle->handle, mMixer,
                                     maxChannels, channelMap);
    return status;
}

void ALSADevice::setChannelAlloc(int channelAlloc)
{
    ALOGV("channel allocation = 0x%x", channelAlloc);
    char** setValues;
    setValues = (char**)malloc(sizeof(char*));
    if (setValues == NULL) {
          return;
    }
    setValues[0] = (char*)malloc(4*sizeof(char));
    if (setValues[0] == NULL) {
          free(setValues);
          return;
    }
    sprintf(setValues[0], "%d", channelAlloc);
    setMixerControlExt("HDMI RX CA", 1, setValues);
    free(setValues[0]);
    free(setValues);
    return;
}

status_t ALSADevice::getMixerControl(const char *name, unsigned int &value, int index)
{
    struct mixer_ctl *ctl;

    if (!mMixer) {
        ALOGE("Control not initialized");
        return NO_INIT;
    }

    ctl =  mixer_get_control(mMixer, name, index);
    if (!ctl)
        return BAD_VALUE;

    mixer_ctl_get(ctl, &value);
    return NO_ERROR;
}

status_t ALSADevice::getMixerControlExt(const char *name, unsigned **getValues, unsigned *count)
{
    struct mixer_ctl *ctl;

    if (!mMixer) {
        ALOGE("Control not initialized");
        return NO_INIT;
    }

    ctl =  mixer_get_control(mMixer, name, 0);
    if (!ctl)
        return BAD_VALUE;

    mixer_ctl_get_mulvalues(ctl, getValues, count);
    return NO_ERROR;
}

status_t ALSADevice::setMixerControl(const char *name, unsigned int value, int index)
{
    struct mixer_ctl *ctl;
    int ret = 0;
    ALOGD("setMixerControl:: name %s value %d index %d", name, value, index);
    if (!mMixer) {
        ALOGE("Control not initialized");
        return NO_INIT;
    }

    // ToDo: Do we need to send index here? Right now it works with 0
    ctl = mixer_get_control(mMixer, name, 0);
    if(ctl == NULL) {
        ALOGE("Could not get the mixer control");
        return BAD_VALUE;
    }
    ret = mixer_ctl_set(ctl, value);
    return (ret < 0) ? BAD_VALUE : NO_ERROR;
}

status_t ALSADevice::setMixerControl(const char *name, const char *value)
{
    struct mixer_ctl *ctl;
    int ret = 0;
    ALOGD("setMixerControl:: name %s value %s", name, value);

    if (!mMixer) {
        ALOGE("Control not initialized");
        return NO_INIT;
    }

    ctl = mixer_get_control(mMixer, name, 0);
    if(ctl == NULL) {
        ALOGE("Could not get the mixer control");
        return BAD_VALUE;
    }
    ret = mixer_ctl_select(ctl, value);
    return (ret < 0) ? BAD_VALUE : NO_ERROR;
}

status_t ALSADevice::setMixerControlExt(const char *name, int count, char **setValues)
{
    struct mixer_ctl *ctl;
    int ret = 0;
    ALOGD("setMixerControl:: name %s count %d", name, count);
    if (!mMixer) {
        ALOGE("Control not initialized");
        return NO_INIT;
    }

    // ToDo: Do we need to send index here? Right now it works with 0
    ctl = mixer_get_control(mMixer, name, 0);
    if(ctl == NULL) {
        ALOGE("Could not get the mixer control");
        return BAD_VALUE;
    }
    ret = mixer_ctl_set_value(ctl, count, setValues);
    return (ret < 0) ? BAD_VALUE : NO_ERROR;
}

status_t ALSADevice::setEcrxDevice(char *device)
{
    status_t err = NO_ERROR;
    setMixerControl("EC_REF_RX", device);
    return err;
}

void ALSADevice::setInChannels(int channels)
{
    mInChannels = channels;
    ALOGV("mInChannels:%d", mInChannels);
}

status_t ALSADevice::exitReadFromProxy()
{
    ALOGV("exitReadFromProxy");
    mProxyParams.mExitRead = true;
    if(mProxyParams.mPfdProxy[1].fd != -1) {
        uint64_t writeValue = KILL_A2DP_THREAD;
        ALOGD("Writing to mPfdProxy[1].fd %d",mProxyParams.mPfdProxy[1].fd);
        write(mProxyParams.mPfdProxy[1].fd, &writeValue, sizeof(uint64_t));
    }
    return NO_ERROR;
}

void ALSADevice::resetProxyVariables() {

    mProxyParams.mAvail = 0;
    mProxyParams.mFrames = 0;
    mProxyParams.mX.frames = 0;
    if(mProxyParams.mPfdProxy[1].fd != -1) {
        sys_close::lib_close(mProxyParams.mPfdProxy[1].fd);
        mProxyParams.mPfdProxy[1].fd = -1;
    }
}

ssize_t  ALSADevice::readFromProxy(void **captureBuffer , ssize_t *bufferSize) {

    status_t err = NO_ERROR;
    int err_poll = 0;
    initProxyParams();
    err = startProxy();
    if(err) {
        ALOGE("ReadFromProxy-startProxy returned err = %d", err);
        *captureBuffer = NULL;
        *bufferSize = 0;
        return err;
    }
    struct pcm * capture_handle = (struct pcm *)mProxyParams.mProxyPcmHandle;

    while(!mProxyParams.mExitRead) {
        ALOGV("Calling sync_ptr(proxy");
        err = sync_ptr(capture_handle);
        if(err == EPIPE) {
               ALOGE("Failed in sync_ptr \n");
               /* we failed to make our window -- try to restart */
               capture_handle->underruns++;
               capture_handle->running = 0;
               capture_handle->start = 0;
               continue;
        } else if (err != NO_ERROR) {
                ALOGE("Error: Sync ptr returned %d", err);
                break;
        }

        mProxyParams.mAvail = pcm_avail(capture_handle);
        avail_in_ms = (mProxyParams.mAvail*4)*1000/(AFE_PROXY_SAMPLE_RATE*AFE_PROXY_CHANNEL_COUNT*2);
        ALOGV("avail is = %d frames = %ld, avai_min = %d\n",\
                      mProxyParams.mAvail,  mProxyParams.mFrames,(int)capture_handle->sw_p->avail_min);
        if (mProxyParams.mAvail < capture_handle->sw_p->avail_min) {
            err_poll = poll(mProxyParams.mPfdProxy, NUM_FDS, TIMEOUT_INFINITE);
            if (mProxyParams.mPfdProxy[1].revents & POLLIN) {
                ALOGV("Event on userspace fd");
            }
            if ((mProxyParams.mPfdProxy[1].revents & POLLERR) ||
                    (mProxyParams.mPfdProxy[1].revents & POLLNVAL)) {
                ALOGV("POLLERR or INVALID POLL");
                err = BAD_VALUE;
                break;
            }
            if((mProxyParams.mPfdProxy[0].revents & POLLERR) ||
                    (mProxyParams.mPfdProxy[0].revents & POLLNVAL)) {
                ALOGV("POLLERR or INVALID POLL on zero");
                err = BAD_VALUE;
                break;
            }
            if (mProxyParams.mPfdProxy[0].revents & POLLIN) {
                ALOGV("POLLIN on zero");
            }
            ALOGV("err_poll = %d",err_poll);
            continue;
        }
        break;
    }
    if(err != NO_ERROR) {
        ALOGE("Reading from proxy failed = err = %d", err);
        *captureBuffer = NULL;
        *bufferSize = 0;
        return err;
    }

    //Copy only if we have data
    if(mProxyParams.mAvail > 0) {
        /* if we have reached high watermark, flush data */
        if(mProxyParams.mAvail > AFE_PROXY_HIGH_WATER_MARK_FRAME_COUNT) {
            /* throw out everything over here */
            ALOGE("available buffers in proxy %d has reached high water mark %d, throw it out ", mProxyParams.mAvail, AFE_PROXY_HIGH_WATER_MARK_FRAME_COUNT);
            capture_handle->sync_ptr->c.control.appl_ptr += mProxyParams.mAvail;
            capture_handle->sync_ptr->flags = 0;
            err = sync_ptr(capture_handle);
            if(err == EPIPE) {
                ALOGV("Failed in sync_ptr \n");
                capture_handle->running = 0;
                err = sync_ptr(capture_handle);
            }
            err = FAILED_TRANSACTION;
            *captureBuffer = NULL;
            *bufferSize = 0;
            return err;
        }
        if (mProxyParams.mX.frames > mProxyParams.mAvail) {
            mProxyParams.mFrames = mProxyParams.mAvail;
            ALOGE("Error mProxyParams.mFrames = %d", mProxyParams.mFrames);
            /* Always copy only the data thats available */
            /* case when we wake up with lesser no of bytes than 1 period */
        } else {
            mProxyParams.mFrames = mProxyParams.mX.frames;
        }
        void *data  = dst_address(capture_handle);
        //TODO: Return a pointer to AudioHardware
        if(mProxyParams.mCaptureBuffer == NULL)
            mProxyParams.mCaptureBuffer =  malloc(mProxyParams.mCaptureBufferSize);

        memcpy(mProxyParams.mCaptureBuffer, (char *)data,
               mProxyParams.mFrames*2*2);

        capture_handle->sync_ptr->c.control.appl_ptr += mProxyParams.mFrames;
        capture_handle->sync_ptr->flags = 0;
        *bufferSize = (mProxyParams.mFrames*2*2);
        mProxyParams.mFrames -= mProxyParams.mFrames;
        ALOGV("Calling sync_ptr for proxy after sync with MFrames is %d", mProxyParams.mFrames);
        err = sync_ptr(capture_handle);
        if(err == EPIPE) {
            ALOGV("Failed in sync_ptr \n");
            capture_handle->running = 0;
            err = sync_ptr(capture_handle);
        }
        if(err != NO_ERROR ) {
            ALOGE("Error: Sync ptr end returned %d", err);
            *captureBuffer = NULL;
            *bufferSize = 0;
            return err;
        }
        *captureBuffer = mProxyParams.mCaptureBuffer;
    } else {
        /* If we dont have data to copy just return 0 */
        *captureBuffer = NULL;
        *bufferSize = 0;
        err = FAILED_TRANSACTION;
        ALOGE("Error Nothing copied from Proxy");
    }
    return err;
}

void ALSADevice::initProxyParams() {
    if(mProxyParams.mPfdProxy[1].fd == -1) {
        ALOGV("Allocating A2Dp poll fd");
        mProxyParams.mPfdProxy[0].fd = mProxyParams.mProxyPcmHandle->fd;
        mProxyParams.mPfdProxy[0].events = (POLLIN | POLLERR | POLLNVAL);
        ALOGV("Allocated A2DP poll fd");
        mProxyParams.mPfdProxy[1].fd = eventfd(0,0);
        mProxyParams.mPfdProxy[1].events = (POLLIN | POLLERR | POLLNVAL);
        mProxyParams.mFrames = (mProxyParams.mProxyPcmHandle->flags & PCM_MONO) ?
            (mProxyParams.mProxyPcmHandle->period_size / 2) :
            (mProxyParams.mProxyPcmHandle->period_size / 4);
        mProxyParams.mX.frames = (mProxyParams.mProxyPcmHandle->flags & PCM_MONO) ?
            (mProxyParams.mProxyPcmHandle->period_size / 2) :
            (mProxyParams.mProxyPcmHandle->period_size / 4);
    }
}

status_t ALSADevice::startProxy() {

    status_t err = NO_ERROR;
    struct pcm * capture_handle = (struct pcm *)mProxyParams.mProxyPcmHandle;
    while(1) {
        if (!capture_handle->start) {
            if(ioctl(capture_handle->fd, SNDRV_PCM_IOCTL_START)) {
                err = -errno;
                if (errno == EPIPE) {
                   ALOGV("Failed in SNDRV_PCM_IOCTL_START\n");
                   /* we failed to make our window -- try to restart */
                   capture_handle->underruns++;
                   capture_handle->running = 0;
                   capture_handle->start = 0;
                   /* sleeping for 10 ms before retrying */
                   usleep(10000);
                   continue;
                } else {
                   ALOGE("IGNORE - IOCTL_START failed for proxy err: %d \n", errno);
                   err = NO_ERROR;
                   break;
                }
           } else {
               ALOGD(" Proxy Driver started(IOCTL_START Success)\n");
               break;
           }
       }
       else {
           ALOGV("Proxy Already started break out of condition");
           break;
       }
   }
   ALOGV("startProxy - Proxy started");
   capture_handle->start = 1;
   capture_handle->sync_ptr->flags = SNDRV_PCM_SYNC_PTR_APPL |
               SNDRV_PCM_SYNC_PTR_AVAIL_MIN;
   return err;
}

status_t ALSADevice::openProxyDevice()
{
    struct snd_pcm_hw_params *params = NULL;
    struct snd_pcm_sw_params *sparams = NULL;
    int flags = (DEBUG_ON | PCM_MMAP| PCM_STEREO | PCM_IN);

    ALOGV("openProxyDevice");
    mProxyParams.mProxyPcmHandle = pcm_open(flags, PROXY_CAPTURE_DEVICE_NAME);
    if (!pcm_ready(mProxyParams.mProxyPcmHandle)) {
        ALOGE("Opening proxy device failed");
        goto bail;
    }
    ALOGV("Proxy device opened successfully: mProxyPcmHandle %p", mProxyParams.mProxyPcmHandle);
    mProxyParams.mProxyPcmHandle->channels = AFE_PROXY_CHANNEL_COUNT;
    mProxyParams.mProxyPcmHandle->rate     = AFE_PROXY_SAMPLE_RATE;
    mProxyParams.mProxyPcmHandle->flags    = flags;
    mProxyParams.mProxyPcmHandle->period_size = AFE_PROXY_PERIOD_SIZE;
    mProxyParams.mBufferTime = (AFE_PROXY_PERIOD_SIZE*1000)/(AFE_PROXY_CHANNEL_COUNT*AFE_PROXY_SAMPLE_RATE*2);

    params = (struct snd_pcm_hw_params*) calloc(1,sizeof(struct snd_pcm_hw_params));
    if (!params) {
         goto bail;
    }

    param_init(params);

    param_set_mask(params, SNDRV_PCM_HW_PARAM_ACCESS,
            (mProxyParams.mProxyPcmHandle->flags & PCM_MMAP)?
            SNDRV_PCM_ACCESS_MMAP_INTERLEAVED
            : SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
            SNDRV_PCM_FORMAT_S16_LE);
    param_set_mask(params, SNDRV_PCM_HW_PARAM_SUBFORMAT,
            SNDRV_PCM_SUBFORMAT_STD);
    param_set_min(params, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
            mProxyParams.mProxyPcmHandle->period_size);
    param_set_int(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16);
    param_set_int(params, SNDRV_PCM_HW_PARAM_FRAME_BITS,
            mProxyParams.mProxyPcmHandle->channels - 1 ? 32 : 16);
    param_set_int(params, SNDRV_PCM_HW_PARAM_CHANNELS,
            mProxyParams.mProxyPcmHandle->channels);
    param_set_int(params, SNDRV_PCM_HW_PARAM_RATE,
            mProxyParams.mProxyPcmHandle->rate);

    param_set_hw_refine(mProxyParams.mProxyPcmHandle, params);

    if (param_set_hw_params(mProxyParams.mProxyPcmHandle, params)) {
        ALOGE("Failed to set hardware params on Proxy device");
        goto bail;
    }

    mProxyParams.mProxyPcmHandle->buffer_size = pcm_buffer_size(params);
    mProxyParams.mProxyPcmHandle->period_size = pcm_period_size(params);
    mProxyParams.mProxyPcmHandle->period_cnt  =
            mProxyParams.mProxyPcmHandle->buffer_size /
            mProxyParams.mProxyPcmHandle->period_size;
    ALOGV("Capture - period_size (%d)",\
            mProxyParams.mProxyPcmHandle->period_size);
    ALOGV("Capture - buffer_size (%d)",\
            mProxyParams.mProxyPcmHandle->buffer_size);
    ALOGV("Capture - period_cnt  (%d)\n",\
            mProxyParams.mProxyPcmHandle->period_cnt);
    sparams = (struct snd_pcm_sw_params*) calloc(1,sizeof(struct snd_pcm_sw_params));
    if (!sparams) {
        ALOGE("Failed to allocated software params for Proxy device");
        goto bail;
    }

   sparams->tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
   sparams->period_step = 1;
   sparams->avail_min = (mProxyParams.mProxyPcmHandle->flags & PCM_MONO) ?
           mProxyParams.mProxyPcmHandle->period_size/2
           : mProxyParams.mProxyPcmHandle->period_size/4;
   sparams->start_threshold = 1;
   sparams->stop_threshold = INT_MAX;
   sparams->xfer_align = (mProxyParams.mProxyPcmHandle->flags & PCM_MONO) ?
           mProxyParams.mProxyPcmHandle->period_size/2
           : mProxyParams.mProxyPcmHandle->period_size/4; /* needed for old kernels */
   sparams->silence_size = 0;
   sparams->silence_threshold = 0;

   if (param_set_sw_params(mProxyParams.mProxyPcmHandle, sparams)) {
        ALOGE("Failed to set software params on Proxy device");
        goto bail;
   }
   mmap_buffer(mProxyParams.mProxyPcmHandle);

   if (pcm_prepare(mProxyParams.mProxyPcmHandle)) {
       ALOGE("Failed to pcm_prepare on Proxy device");
       goto bail;
   }
   mProxyParams.mProxyState = proxy_params::EProxySuspended;
   return NO_ERROR;

bail:
   if(mProxyParams.mProxyPcmHandle)  {
       pcm_close(mProxyParams.mProxyPcmHandle);
       mProxyParams.mProxyPcmHandle = NULL;
   }
   mProxyParams.mProxyState = proxy_params::EProxyClosed;
   return NO_INIT;
}

status_t ALSADevice::closeProxyDevice() {
    status_t err = NO_ERROR;
    if(mProxyParams.mProxyPcmHandle) {
        pcm_close(mProxyParams.mProxyPcmHandle);
        mProxyParams.mProxyPcmHandle = NULL;
    }
    resetProxyVariables();
    mProxyParams.mProxyState = proxy_params::EProxyClosed;
    mProxyParams.mExitRead = false;
    return err;
}

bool ALSADevice::isProxyDeviceOpened() {

   //TODO : Add some intelligence to return appropriate value
   if(mProxyParams.mProxyState == proxy_params::EProxyOpened ||
           mProxyParams.mProxyState == proxy_params::EProxyCapture ||
           mProxyParams.mProxyState == proxy_params::EProxySuspended)
       return true;
   return false;
}

bool ALSADevice::isProxyDeviceSuspended() {

   if(mProxyParams.mProxyState == proxy_params::EProxySuspended)
        return true;
   return false;
}

bool ALSADevice::suspendProxy() {

   status_t err = NO_ERROR;
   if(mProxyParams.mProxyState == proxy_params::EProxyOpened ||
           mProxyParams.mProxyState == proxy_params::EProxyCapture) {
       mProxyParams.mProxyState = proxy_params::EProxySuspended;
   }
   else {
       ALOGE("Proxy already suspend or closed, in state = %d",\
                mProxyParams.mProxyState);
   }
   return err;
}

bool ALSADevice::resumeProxy() {

   status_t err = NO_ERROR;
   struct pcm *capture_handle= mProxyParams.mProxyPcmHandle;
   ALOGD("resumeProxy mProxyParams.mProxyState = %d, capture_handle =%p",\
           mProxyParams.mProxyState, capture_handle);
   if((mProxyParams.mProxyState == proxy_params::EProxyOpened ||
           mProxyParams.mProxyState == proxy_params::EProxySuspended) &&
           capture_handle != NULL) {
       ALOGV("pcm_prepare from Resume");
       capture_handle->start = 0;
       err = pcm_prepare(capture_handle);
       if(err != OK) {
           ALOGE("IGNORE: PCM Prepare - capture failed err = %d", err);
       }
       err = startProxy();
       if(err) {
           ALOGE("IGNORE:startProxy returned error = %d", err);
       }
       mProxyParams.mProxyState = proxy_params::EProxyCapture;
       err = sync_ptr(capture_handle);
       if (err) {
           ALOGE("IGNORE: sync ptr from resumeProxy returned error = %d", err);
       }
       ALOGV("appl_ptr= %d", (int)capture_handle->sync_ptr->c.control.appl_ptr);
   }
   else {
        ALOGV("resume Proxy ignored in invalid state - ignore");
        if(mProxyParams.mProxyState == proxy_params::EProxyClosed ||
                capture_handle == NULL) {
            ALOGE("resumeProxy = BAD_VALUE");
            err = BAD_VALUE;
            return err;
        }
   }
   return NO_ERROR;
}

status_t ALSADevice::getEDIDData(char *hdmiEDIDData)
{
    status_t err = NO_ERROR;
    unsigned **EDIDData;
    EDIDData = (unsigned **)malloc((MAX_SHORT_AUDIO_DESC_CNT + 1)*sizeof(unsigned*));
                              // additional 1 byte for length of the EDID
    unsigned count;
    if(EDIDData) {
        for (int i=0; i<MAX_SHORT_AUDIO_DESC_CNT + 1; i++) {
            EDIDData[i] = (unsigned*)malloc(1 * sizeof(unsigned));
            if(!EDIDData[i])
                err = BAD_VALUE;
        }
    }
    if(err == NO_ERROR) {
        err = getMixerControlExt("HDMI EDID", EDIDData, &count);
        hdmiEDIDData[0] = (char)(count);
        if(err == NO_ERROR) {
            for(int i=0; i<count; i++) {
                hdmiEDIDData[i+1] = (char) (*(EDIDData[i]));
            }
        }
    }

    for(int i=0; i< MAX_SHORT_AUDIO_DESC_CNT+1; i++)
        if(EDIDData[i])
            free(EDIDData[i]);
    if(EDIDData)
        free(EDIDData);
    return err;
}

#ifdef SEPERATED_AUDIO_INPUT
void ALSADevice::setInput(int input) 
{
    mInputSource = input;
    ALOGD("s_setInput() : input_source = %d",mInputSource);
}
#endif

#ifdef QCOM_CSDCLIENT_ENABLED
void  ALSADevice::setCsdHandle(void* handle)
{
    mcsd_handle = static_cast<void*>(handle);
    ALOGI("%s csd_handle: %p", __func__, mcsd_handle);

    csd_disable_device = (int (*)())::dlsym(mcsd_handle,
                                            "csd_client_disable_device");
    csd_enable_device = (int (*)(int,int,uint32_t))::dlsym(mcsd_handle,
                                                    "csd_client_enable_device");
    csd_enable_device_config = (int (*)(int,int))::dlsym(mcsd_handle,
                                                    "csd_client_enable_device_config");
    csd_start_voice = (int (*)(uint32_t))::dlsym(mcsd_handle,
                                                 "csd_client_start_voice");
    csd_stop_voice = (int (*)(uint32_t))::dlsym(mcsd_handle,
                                                "csd_client_stop_voice");
    csd_volume = (int (*)(uint32_t, int))::dlsym(mcsd_handle,
                                                 "csd_client_volume");
    csd_mic_mute = (int (*)(uint32_t, int))::dlsym(mcsd_handle,
                                                   "csd_client_mic_mute");
    csd_wide_voice = (int (*)(uint32_t, uint8_t))::dlsym(mcsd_handle,
                                                    "csd_client_wide_voice");
    csd_fens = (int (*)(uint32_t, uint8_t))::dlsym(mcsd_handle,
                                                   "csd_client_fens");
    csd_slow_talk = (int (*)(uint32_t, uint8_t))::dlsym(mcsd_handle,
                                                        "csd_client_slow_talk");
}
#endif


#ifdef QCOM_ACDB_ENABLED
void  ALSADevice::setACDBHandle(void* handle)
{
    macdb_handle = static_cast<void*>(handle);
    ALOGI("%s ACDB_handle: %p", __func__, macdb_handle);

    acdb_loader_get_ecrx_device = (int (*)(int))::dlsym(macdb_handle,
                                                "acdb_loader_get_ecrx_device");
}
#endif


}
