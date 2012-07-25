/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
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

#define LOG_TAG "AudioPolicyManagerALSA"
//#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0
#include <utils/Log.h>

#include "AudioPolicyManagerALSA.h"
#include <media/mediarecorder.h>

namespace android_audio_legacy {

// ----------------------------------------------------------------------------
// AudioPolicyManagerALSA
// ----------------------------------------------------------------------------

// ---  class factory

audio_devices_t AudioPolicyManager::getDeviceForStrategy(routing_strategy strategy,
                                                             bool fromCache)
{
    uint32_t device = 0;

    if (fromCache) {
        ALOGV("getDeviceForStrategy() from cache strategy %d, device %x",
              strategy, mDeviceForStrategy[strategy]);
        return mDeviceForStrategy[strategy];
    }

    switch (strategy) {

    case STRATEGY_DTMF:
        if (!isInCall()) {
            // when off call, DTMF strategy follows the same rules as MEDIA strategy
            break;
        }
        // when in call, DTMF and PHONE strategies follow the same rules
        // FALL THROUGH

    case STRATEGY_PHONE:
        if ( (mForceUse[AudioSystem::FOR_COMMUNICATION] != AudioSystem::FORCE_BT_SCO)
            &&(mForceUse[AudioSystem::FOR_COMMUNICATION] != AudioSystem::FORCE_SPEAKER)) {
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_PROXY;
            if (device) {
                ALOGV("getDeviceForStrategy() proxy device[0x%x] selected for STRATEGY_PHONE[%d]:",device,STRATEGY_PHONE);
                return (audio_devices_t)device;
            }
        } break;

    case STRATEGY_SONIFICATION:
        // If incall, just select the STRATEGY_PHONE device: The rest of the behavior is handled by
        // handleIncallSonification().
        if (isInCall()) {
            break;
        }
        // FALL THROUGH

    case STRATEGY_MEDIA:
        if (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_SPEAKER) {
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_PROXY;
            if(device) {
                ALOGV("getDeviceForStrategy() proxy device[0x%x] selected for STRATEGY_MEDIA[%d]:",device,STRATEGY_MEDIA);
                return (audio_devices_t)device;
            }
        } break;

    default:
        ALOGV("getDeviceForStrategy()strategy handled by AudioPolicyManagerBase: %d", strategy);
        break;
    }

    return AudioPolicyManagerBase::getDeviceForStrategy(strategy, fromCache);
}


extern "C" AudioPolicyInterface* createAudioPolicyManager(AudioPolicyClientInterface *clientInterface)
{
    return new AudioPolicyManager(clientInterface);
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface *interface)
{
    delete interface;
}

}; // namespace androidi_audio_legacy
