/* ALSAStreamOps.cpp
 **
 ** Copyright 2008-2009 Wind River Systems
 ** Copyright (c) 2011, Code Aurora Forum. All rights reserved.
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

#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

#define LOG_TAG "ALSAStreamOps"
//#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0
#include <utils/Log.h>
#include <utils/String8.h>

#include <cutils/properties.h>
#include <media/AudioRecord.h>
#include <hardware_legacy/power.h>

#include "AudioHardwareALSA.h"

namespace android_audio_legacy
{

// ----------------------------------------------------------------------------

ALSAStreamOps::ALSAStreamOps(AudioHardwareALSA *parent, alsa_handle_t *handle) :
    mParent(parent),
    mHandle(handle)
{
}

ALSAStreamOps::~ALSAStreamOps()
{
    Mutex::Autolock autoLock(mParent->mLock);

    if((!strcmp(mHandle->useCase, SND_USE_CASE_VERB_IP_VOICECALL)) ||
       (!strcmp(mHandle->useCase, SND_USE_CASE_MOD_PLAY_VOIP))) {
        if((mParent->mVoipStreamCount)) {
            mParent->mVoipStreamCount--;
            if(mParent->mVoipStreamCount > 0) {
                ALOGD("ALSAStreamOps::close() Ignore");
                return ;
            }
       }
       mParent->mVoipStreamCount = 0;
       mParent->mVoipMicMute = 0;
       mParent->mVoipBitRate = 0;
    }
    close();

    for(ALSAHandleList::iterator it = mParent->mDeviceList.begin();
            it != mParent->mDeviceList.end(); ++it) {
            if (mHandle == &(*it)) {
                it->useCase[0] = 0;
                mParent->mDeviceList.erase(it);
                break;
            }
    }
}

// use emulated popcount optimization
// http://www.df.lth.se/~john_e/gems/gem002d.html
static inline uint32_t popCount(uint32_t u)
{
    u = ((u&0x55555555) + ((u>>1)&0x55555555));
    u = ((u&0x33333333) + ((u>>2)&0x33333333));
    u = ((u&0x0f0f0f0f) + ((u>>4)&0x0f0f0f0f));
    u = ((u&0x00ff00ff) + ((u>>8)&0x00ff00ff));
    u = ( u&0x0000ffff) + (u>>16);
    return u;
}

status_t ALSAStreamOps::set(int      *format,
                            uint32_t *channels,
                            uint32_t *rate,
                            uint32_t device)
{
    mDevices = device;
    if (channels && *channels != 0) {
        if (mHandle->channels != popCount(*channels))
            return BAD_VALUE;
    } else if (channels) {
        *channels = 0;
        if (mHandle->devices & AudioSystem::DEVICE_OUT_ALL) {
            switch(mHandle->channels) {
                case 4:
                    *channels |= AudioSystem::CHANNEL_OUT_BACK_LEFT;
                    *channels |= AudioSystem::CHANNEL_OUT_BACK_RIGHT;
                    // Fall through...
                default:
                case 2:
                    *channels |= AudioSystem::CHANNEL_OUT_FRONT_RIGHT;
                    // Fall through...
                case 1:
                    *channels |= AudioSystem::CHANNEL_OUT_FRONT_LEFT;
                    break;
            }
        } else {
            switch(mHandle->channels) {
#ifdef QCOM_SSR_ENABLED
                // For 5.1 recording
                case 6 :
                    *channels |= AudioSystem::CHANNEL_IN_5POINT1;
                    break;
#endif
                    // Do not fall through...
                default:
                case 2:
                    *channels |= AudioSystem::CHANNEL_IN_RIGHT;
                    // Fall through...
                case 1:
                    *channels |= AudioSystem::CHANNEL_IN_LEFT;
                    break;
            }
        }
    }

    if (rate && *rate > 0) {
        if (mHandle->sampleRate != *rate)
            return BAD_VALUE;
    } else if (rate) {
        *rate = mHandle->sampleRate;
    }

    snd_pcm_format_t iformat = mHandle->format;

    if (format) {
        switch(*format) {
            case AudioSystem::FORMAT_DEFAULT:
                break;

            case AudioSystem::PCM_16_BIT:
                iformat = SNDRV_PCM_FORMAT_S16_LE;
                break;
            case AudioSystem::AMR_NB:
            case AudioSystem::AMR_WB:
#ifdef QCOM_QCHAT_ENABLED
            case AudioSystem::EVRC:
            case AudioSystem::EVRCB:
            case AudioSystem::EVRCWB:
#endif
                iformat = *format;
                break;

            case AudioSystem::PCM_8_BIT:
                iformat = SNDRV_PCM_FORMAT_S8;
                break;

            default:
                ALOGE("Unknown PCM format %i. Forcing default", *format);
                break;
        }

        if (mHandle->format != iformat)
            return BAD_VALUE;

        switch(iformat) {
            case SNDRV_PCM_FORMAT_S16_LE:
                *format = AudioSystem::PCM_16_BIT;
                break;
            case SNDRV_PCM_FORMAT_S8:
                *format = AudioSystem::PCM_8_BIT;
                break;
            default:
                break;
        }
    }

    return NO_ERROR;
}

status_t ALSAStreamOps::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    int device;
    if (param.getInt(key, device) == NO_ERROR) {
        // Ignore routing if device is 0.
        ALOGD("setParameters(): keyRouting with device %d", device);
        mDevices = device;
        if(device) {
            mParent->doRouting(device);
        }
        param.remove(key);
    }
#ifdef QCOM_FM_ENABLED
    else {
        key = String8(AudioParameter::keyHandleFm);
        if (param.getInt(key, device) == NO_ERROR) {
        ALOGD("setParameters(): handleFm with device %d", device);
        mDevices = device;
            if(device) {
                mParent->handleFm(device);
            }
            param.remove(key);
        }
    }
#endif

    return NO_ERROR;
}

String8 ALSAStreamOps::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        param.addInt(key, (int)mDevices);
    }
    else {
#ifdef QCOM_VOIP_ENABLED
        key = String8(AudioParameter::keyVoipCheck);
        if (param.get(key, value) == NO_ERROR) {
            if((!strncmp(mHandle->useCase, SND_USE_CASE_VERB_IP_VOICECALL, strlen(SND_USE_CASE_VERB_IP_VOICECALL))) ||
               (!strncmp(mHandle->useCase, SND_USE_CASE_MOD_PLAY_VOIP, strlen(SND_USE_CASE_MOD_PLAY_VOIP))))
                param.addInt(key, true);
            else
                param.addInt(key, false);
        }
#endif
    }
    ALOGV("getParameters() %s", param.toString().string());
    return param.toString();
}

uint32_t ALSAStreamOps::sampleRate() const
{
    return mHandle->sampleRate;
}

//
// Return the number of bytes (not frames)
//
size_t ALSAStreamOps::bufferSize() const
{
    ALOGV("bufferSize() returns %d", mHandle->bufferSize);
    return mHandle->bufferSize;
}

int ALSAStreamOps::format() const
{
    int audioSystemFormat;

    snd_pcm_format_t ALSAFormat = mHandle->format;

    switch(ALSAFormat) {
        case SNDRV_PCM_FORMAT_S8:
             audioSystemFormat = AudioSystem::PCM_8_BIT;
             break;

        case AudioSystem::AMR_NB:
        case AudioSystem::AMR_WB:
#ifdef QCOM_QCHAT_ENABLED
        case AudioSystem::EVRC:
        case AudioSystem::EVRCB:
        case AudioSystem::EVRCWB:
#endif
            audioSystemFormat = mHandle->format;
            break;
        case SNDRV_PCM_FORMAT_S16_LE:
            audioSystemFormat = AudioSystem::PCM_16_BIT;
            break;

        default:
            LOG_FATAL("Unknown AudioSystem bit width %d!", audioSystemFormat);
            audioSystemFormat = AudioSystem::PCM_16_BIT;
            break;
    }

    ALOGD("ALSAFormat:0x%x,audioSystemFormat:0x%x",ALSAFormat,audioSystemFormat);
    return audioSystemFormat;
}

uint32_t ALSAStreamOps::channels() const
{
    unsigned int count = mHandle->channels;
    uint32_t channels = 0;

    if (mDevices & AudioSystem::DEVICE_OUT_ALL)
        switch(count) {
            case 4:
                channels |= AudioSystem::CHANNEL_OUT_BACK_LEFT;
                channels |= AudioSystem::CHANNEL_OUT_BACK_RIGHT;
                // Fall through...
            default:
            case 2:
                channels |= AudioSystem::CHANNEL_OUT_FRONT_RIGHT;
                // Fall through...
            case 1:
                channels |= AudioSystem::CHANNEL_OUT_FRONT_LEFT;
                break;
        }
    else
        switch(count) {
#ifdef QCOM_SSR_ENABLED
            // For 5.1 recording
            case 6 :
                channels |= AudioSystem::CHANNEL_IN_5POINT1;
                break;
                // Do not fall through...
#endif
            default:
            case 2:
                channels |= AudioSystem::CHANNEL_IN_RIGHT;
                // Fall through...
            case 1:
                channels |= AudioSystem::CHANNEL_IN_LEFT;
                break;
        }

    return channels;
}

void ALSAStreamOps::close()
{
    ALOGD("close");
    if((!strncmp(mHandle->useCase, SND_USE_CASE_VERB_IP_VOICECALL, strlen(SND_USE_CASE_VERB_IP_VOICECALL))) ||
       (!strncmp(mHandle->useCase, SND_USE_CASE_MOD_PLAY_VOIP, strlen(SND_USE_CASE_MOD_PLAY_VOIP)))) {
       mParent->mVoipMicMute = false;
       mParent->mVoipBitRate = 0;
       mParent->mVoipStreamCount = 0;
    }
    mParent->mALSADevice->close(mHandle);
}

//
// Set playback or capture PCM device.  It's possible to support audio output
// or input from multiple devices by using the ALSA plugins, but this is
// not supported for simplicity.
//
// The AudioHardwareALSA API does not allow one to set the input routing.
//
// If the "routes" value does not map to a valid device, the default playback
// device is used.
//
status_t ALSAStreamOps::open(int mode)
{
    ALOGD("open");
    return mParent->mALSADevice->open(mHandle);
}

}       // namespace androidi_audio_legacy
