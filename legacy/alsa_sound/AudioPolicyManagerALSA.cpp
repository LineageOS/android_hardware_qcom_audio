/*
 * Copyright (C) 2009 The Android Open Source Project
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

#define LOG_TAG "AudioPolicyManagerALSA"
//#define LOG_NDEBUG 0
//#define LOG_NDDEBUG 0

//#define VERY_VERBOSE_LOGGING
#ifdef VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

// A device mask for all audio input devices that are considered "virtual" when evaluating
// active inputs in getActiveInput()
#define APM_AUDIO_IN_DEVICE_VIRTUAL_ALL  AUDIO_DEVICE_IN_REMOTE_SUBMIX

#include <utils/Log.h>

#include "AudioPolicyManagerALSA.h"
#include <hardware/audio_effect.h>
#include <hardware/audio.h>
#include <hardware_legacy/audio_policy_conf.h>
#include <math.h>
#include <media/mediarecorder.h>
#include <stdio.h>
#include <cutils/properties.h>

namespace android_audio_legacy {

// ----------------------------------------------------------------------------
// AudioPolicyManagerALSA
// ----------------------------------------------------------------------------

AudioParameter param;

void AudioPolicyManager::setStrategyMute(routing_strategy strategy,
                                             bool on,
                                             audio_io_handle_t output,
                                             int delayMs,
                                             audio_devices_t device)
{
    ALOGVV("setStrategyMute() strategy %d, mute %d, output %d", strategy, on, output);
    for (int stream = 0; stream < AudioSystem::NUM_STREAM_TYPES; stream++) {
        if (getStrategy((AudioSystem::stream_type)stream) == strategy) {
            setStreamMute(stream, on, output, delayMs, device);
        }
    }
}

void AudioPolicyManager::releaseOutput(audio_io_handle_t output)
{
    ALOGV("releaseOutput() %d", output);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        ALOGW("releaseOutput() releasing unknown output %d", output);
        return;
    }

#ifdef AUDIO_POLICY_TEST
    int testIndex = testOutputIndex(output);
    if (testIndex != 0) {
        AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);
        if (outputDesc->isActive()) {
            mpClientInterface->closeOutput(output);
            delete mOutputs.valueAt(index);
            mOutputs.removeItem(output);
            mTestOutputs[testIndex] = 0;
        }
        return;
    }
#endif //AUDIO_POLICY_TEST

    AudioOutputDescriptor *desc = mOutputs.valueAt(index);
    if (desc->mFlags & AudioSystem::OUTPUT_FLAG_DIRECT) {
        if ((desc->mDirectOpenCount <= 0) && !(desc->mFlags & AUDIO_OUTPUT_FLAG_LPA || desc->mFlags & AUDIO_OUTPUT_FLAG_TUNNEL ||
                desc->mFlags & AUDIO_OUTPUT_FLAG_VOIP_RX)) {
            ALOGW("releaseOutput() invalid open count %d for output %d",
                                                              desc->mDirectOpenCount, output);
            return;
        }
        if ((--desc->mDirectOpenCount == 0) && ((desc->mFlags & AUDIO_OUTPUT_FLAG_LPA || desc->mFlags & AUDIO_OUTPUT_FLAG_TUNNEL ||
                desc->mFlags & AUDIO_OUTPUT_FLAG_VOIP_RX))) {
            ALOGV("releaseOutput() closing output");
            closeOutput(output);
        }
    }

}

uint32_t AudioPolicyManager::checkDeviceMuteStrategies(AudioOutputDescriptor *outputDesc,
                                                       audio_devices_t prevDevice,
                                                       uint32_t delayMs)
{
    // mute/unmute strategies using an incompatible device combination
    // if muting, wait for the audio in pcm buffer to be drained before proceeding
    // if unmuting, unmute only after the specified delay
    if (outputDesc->isDuplicated()) {
        return 0;
    }

    uint32_t muteWaitMs = 0;
    audio_devices_t device = outputDesc->device();
    bool shouldMute = outputDesc->isActive() && (AudioSystem::popCount(device) >= 2);
    // temporary mute output if device selection changes to avoid volume bursts due to
    // different per device volumes
    bool tempMute = outputDesc->isActive() && (device != prevDevice);

    for (size_t i = 0; i < NUM_STRATEGIES; i++) {
        audio_devices_t curDevice = getDeviceForStrategy((routing_strategy)i, false /*fromCache*/);
        bool mute = shouldMute && (curDevice & device) && (curDevice != device);
        bool doMute = false;

        if (mute && !outputDesc->mStrategyMutedByDevice[i]) {
            doMute = true;
            outputDesc->mStrategyMutedByDevice[i] = true;
        } else if (!mute && outputDesc->mStrategyMutedByDevice[i]){
            doMute = true;
            outputDesc->mStrategyMutedByDevice[i] = false;
        }
        if (doMute || tempMute) {
            for (size_t j = 0; j < mOutputs.size(); j++) {
                AudioOutputDescriptor *desc = mOutputs.valueAt(j);
                // skip output if it does not share any device with current output
                if ((desc->supportedDevices() & outputDesc->supportedDevices())
                        == AUDIO_DEVICE_NONE) {
                    continue;
                }
                audio_io_handle_t curOutput = mOutputs.keyAt(j);
                ALOGVV("checkDeviceMuteStrategies() %s strategy %d (curDevice %04x) on output %d",
                      mute ? "muting" : "unmuting", i, curDevice, curOutput);
                setStrategyMute((routing_strategy)i, mute, curOutput, mute ? 0 : delayMs);
                if (desc->isStrategyActive((routing_strategy)i)) {
                    // do tempMute only for current output
                    if (tempMute && !mute) {
                        if ((desc != outputDesc) && (desc->device() == device)) {
                            ALOGD("avoid tempmute on curOutput %d as device is same", curOutput);
                        } else {
                            setStrategyMute((routing_strategy)i, true, curOutput);

                            //Add IsFMEnabled to maitain FM status when FM device enabled
                            AudioParameter param = AudioParameter(mpClientInterface->getParameters(0, String8("Fm-radio")));
                            int IsFMEnabled;
                            if (param.getInt(String8 ("isFMON"), IsFMEnabled) == NO_ERROR){
                                ALOGD("getParameters(): Fm-radio with IsFMEnabled %d", IsFMEnabled);
                            }
                            //Change latency for tunnel/LPA player to make sure no noise on device switch
                            //Routing to  BTA2DP,  USB device ,  Proxy(WFD) will take time, increasing latency time
                            if((desc->mFlags & AUDIO_OUTPUT_FLAG_LPA) || (desc->mFlags & AUDIO_OUTPUT_FLAG_TUNNEL)
                                || (device & AUDIO_DEVICE_OUT_USB_DEVICE) || (device & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP)
                                || (device & AUDIO_DEVICE_OUT_PROXY) || IsFMEnabled)
                            {
                               setStrategyMute((routing_strategy)i, false, curOutput,desc->latency()*4 ,device);
                            }
                            else
                               setStrategyMute((routing_strategy)i, false, curOutput,desc->latency()*2 ,device);
                        }
                    }
                    if ((tempMute && (desc == outputDesc)) || mute) {
                        if (muteWaitMs < desc->latency()) {
                            muteWaitMs = desc->latency();
                        }
                    }
                }
            }
        }
    }

    // FIXME: should not need to double latency if volume could be applied immediately by the
    // audioflinger mixer. We must account for the delay between now and the next time
    // the audioflinger thread for this output will process a buffer (which corresponds to
    // one buffer size, usually 1/2 or 1/4 of the latency).
    muteWaitMs *= 2;
    // wait for the PCM output buffers to empty before proceeding with the rest of the command
    if (muteWaitMs > delayMs) {
        muteWaitMs -= delayMs;
        usleep(muteWaitMs * 1000);
        return muteWaitMs;
    }
    return 0;
}

void AudioPolicyManager::setStreamMute(int stream,
                                           bool on,
                                           audio_io_handle_t output,
                                           int delayMs,
                                           audio_devices_t device)
{
    StreamDescriptor &streamDesc = mStreams[stream];
    AudioOutputDescriptor *outputDesc = mOutputs.valueFor(output);
    if (device == AUDIO_DEVICE_NONE) {
        device = outputDesc->device();
    }

    ALOGVV("setStreamMute() stream %d, mute %d, output %d, mMuteCount %d device %04x",
          stream, on, output, outputDesc->mMuteCount[stream], device);

    if (on) {
        if (outputDesc->mMuteCount[stream] == 0) {
            if (streamDesc.mCanBeMuted &&
                    ((stream != AudioSystem::ENFORCED_AUDIBLE) ||
                     (mForceUse[AudioSystem::FOR_SYSTEM] == AudioSystem::FORCE_NONE))) {
                checkAndSetVolume(stream, 0, output, device, delayMs);
            }
        }
        // increment mMuteCount after calling checkAndSetVolume() so that volume change is not ignored
        outputDesc->mMuteCount[stream]++;
    } else {
        if (outputDesc->mMuteCount[stream] == 0) {
            ALOGV("setStreamMute() unmuting non muted stream!");
            return;
        }
        if (--outputDesc->mMuteCount[stream] == 0) {
            checkAndSetVolume(stream,
                              streamDesc.getVolumeIndex(device),
                              output,
                              device,
                              delayMs);
        }
    }
}


status_t AudioPolicyManager::setDeviceConnectionState(audio_devices_t device,
                                                  AudioSystem::device_connection_state state,
                                                  const char *device_address)
{
    SortedVector <audio_io_handle_t> outputs;

    ALOGV("setDeviceConnectionState() device: %x, state %d, address %s", device, state, device_address);

    // connect/disconnect only 1 device at a time
    if (!audio_is_output_device(device) && !audio_is_input_device(device)) return BAD_VALUE;

    if (strlen(device_address) >= MAX_DEVICE_ADDRESS_LEN) {
        ALOGE("setDeviceConnectionState() invalid address: %s", device_address);
        return BAD_VALUE;
    }

    // handle output devices
    if (audio_is_output_device(device)) {

        //Use QCOM's a2dp and usb audio solution, no need to check here
        /*if (!mHasA2dp && audio_is_a2dp_device(device)) {
            ALOGE("setDeviceConnectionState() invalid A2DP device: %x", device);
            return BAD_VALUE;
        }
        if (!mHasUsb && audio_is_usb_device(device)) {
            ALOGE("setDeviceConnectionState() invalid USB audio device: %x", device);
            return BAD_VALUE;
        }*/
        if (!mHasRemoteSubmix && audio_is_remote_submix_device((audio_devices_t)device)) {
            ALOGE("setDeviceConnectionState() invalid remote submix audio device: %x", device);
            return BAD_VALUE;
        }

        // save a copy of the opened output descriptors before any output is opened or closed
        // by checkOutputsForDevice(). This will be needed by checkOutputForAllStrategies()
        mPreviousOutputs = mOutputs;
        switch (state)
        {
        // handle output device connection
        case AudioSystem::DEVICE_STATE_AVAILABLE:
            if (mAvailableOutputDevices & device) {
                ALOGW("setDeviceConnectionState() device already connected: %x", device);
                return INVALID_OPERATION;
            }
            ALOGV("setDeviceConnectionState() connecting device %x", device);

            if (checkOutputsForDevice(device, state, outputs) != NO_ERROR) {
                return INVALID_OPERATION;
            }
            ALOGV("setDeviceConnectionState() checkOutputsForDevice() returned %d outputs",
                  outputs.size());
            // register new device as available
            mAvailableOutputDevices = (audio_devices_t)(mAvailableOutputDevices | device);

            if (audio_is_a2dp_device(device)) {
               AudioParameter param;
               param.add(String8("a2dp_connected"), String8("true"));
               mpClientInterface->setParameters(0, param.toString());
            }
            if ( audio_is_usb_device(device)) {
               AudioParameter param;
               param.add(String8("usb_connected"), String8("true"));
               mpClientInterface->setParameters(0, param.toString());
            }
            if (!outputs.isEmpty()) {
                String8 paramStr;
                if (audio_is_a2dp_device(device)) {
                    // handle A2DP device connection
                    AudioParameter param;
                    param.add(String8(AUDIO_PARAMETER_A2DP_SINK_ADDRESS), String8(device_address));
                    paramStr = param.toString();
                    mA2dpDeviceAddress = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                    mA2dpSuspended = false;
                } else if (audio_is_bluetooth_sco_device(device)) {
                    // handle SCO device connection
                    mScoDeviceAddress = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                } else if (audio_is_usb_device(device)) {
                    // handle USB device connection
                    mUsbOutCardAndDevice = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                    paramStr = mUsbOutCardAndDevice;
                }
                // not currently handling multiple simultaneous submixes: ignoring remote submix
                //   case and address
                if (!paramStr.isEmpty()) {
                    for (size_t i = 0; i < outputs.size(); i++) {
                        mpClientInterface->setParameters(outputs[i], paramStr);
                    }
                }
            }
            break;
        // handle output device disconnection
        case AudioSystem::DEVICE_STATE_UNAVAILABLE: {
            if (!(mAvailableOutputDevices & device)) {
                ALOGW("setDeviceConnectionState() device not connected: %x", device);
                return INVALID_OPERATION;
            }

            ALOGV("setDeviceConnectionState() disconnecting device %x", device);
            mForceDeviceChange = true;
            // remove device from available output devices
            mAvailableOutputDevices = (audio_devices_t)(mAvailableOutputDevices & ~device);

            checkOutputsForDevice(device, state, outputs);
            if (audio_is_a2dp_device(device)) {
                // handle A2DP device disconnection
                mA2dpDeviceAddress = "";
                mA2dpSuspended = false;

                AudioParameter param;
                param.add(String8("a2dp_connected"), String8("false"));
                mpClientInterface->setParameters(0, param.toString());

            } else if (audio_is_bluetooth_sco_device(device)) {
                // handle SCO device disconnection
                mScoDeviceAddress = "";
            } else if (audio_is_usb_device(device)) {
                // handle USB device disconnection
                mUsbOutCardAndDevice = "";

                AudioParameter param;
                param.add(String8("usb_connected"), String8("false"));
                mpClientInterface->setParameters(0, param.toString());
            }
            // not currently handling multiple simultaneous submixes: ignoring remote submix
            //   case and address
            } break;

        default:
            ALOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

        checkA2dpSuspend();
        checkOutputForAllStrategies();
        // outputs must be closed after checkOutputForAllStrategies() is executed
        if (!outputs.isEmpty()) {
            for (size_t i = 0; i < outputs.size(); i++) {
                AudioOutputDescriptor *desc = mOutputs.valueFor(outputs[i]);
                // close unused outputs after device disconnection or direct outputs that have been
                // opened by checkOutputsForDevice() to query dynamic parameters
                if ((state == AudioSystem::DEVICE_STATE_UNAVAILABLE) ||
                        (((desc->mFlags & AUDIO_OUTPUT_FLAG_DIRECT) != 0) &&
                         (desc->mDirectOpenCount == 0))) {
                    closeOutput(outputs[i]);
                }
            }
        }

        updateDevicesAndOutputs();
#ifdef QCOM_PROXY_DEVICE_ENABLED
        if (state == AudioSystem::DEVICE_STATE_AVAILABLE &&
                audio_is_a2dp_device(device) &&
                (mAvailableOutputDevices & AUDIO_DEVICE_OUT_PROXY)) {
            ALOGV("Delay the proxy device open");
            return NO_ERROR;
        }
#endif

        audio_devices_t newDevice = getNewDevice(mPrimaryOutput, false /*fromCache*/);
#ifdef QCOM_FM_ENABLED
        if(device == AUDIO_DEVICE_OUT_FM) {
            if (state == AudioSystem::DEVICE_STATE_AVAILABLE) {
                ALOGV("setDeviceConnectionState() changeRefCount Inc");
                mOutputs.valueFor(mPrimaryOutput)->changeRefCount(AudioSystem::MUSIC, 1);
                newDevice = (audio_devices_t)(AudioPolicyManagerBase::getNewDevice(mPrimaryOutput, false) | AUDIO_DEVICE_OUT_FM);
            }
            else {
                ALOGV("setDeviceConnectionState() changeRefCount Dec");
                mOutputs.valueFor(mPrimaryOutput)->changeRefCount(AudioSystem::MUSIC, -1);
            }

            AudioParameter param = AudioParameter();
            param.addInt(String8(AudioParameter::keyHandleFm), (int)newDevice);
            ALOGV("setDeviceConnectionState() setParameters handle_fm");
            mpClientInterface->setParameters(mPrimaryOutput, param.toString());
        }
#endif
       if(isInCall() && isTunnelOutputEnabled()) {
           //On each keyRouting call, HAL is routing new device to all open outputs
           //Incall mode device switch is taking time than normal
           //start with tunnel mode to avoid current device mismatch with APM to HAL(which effects temp mute)
           for (int i = mOutputs.size() -1; i >= 0; i--) {
               audio_devices_t newDevice = getNewDevice(mOutputs.keyAt(i), true /*fromCache*/);
#ifdef QCOM_ANC_HEADSET_ENABLED
               if(device == AUDIO_DEVICE_OUT_ANC_HEADPHONE ||
                  device == AUDIO_DEVICE_OUT_ANC_HEADSET) {
                  if(newDevice == 0){
                      newDevice = getDeviceForStrategy(STRATEGY_MEDIA, false);
                  }
              }
#endif
              setOutputDevice(mOutputs.keyAt(i),
                            getNewDevice(mOutputs.keyAt(i), true /*fromCache*/),
                            true,
                            0);
           }
       }
       else {
           for (int i = 0; i < mOutputs.size(); i++) {
               audio_devices_t newDevice = getNewDevice(mOutputs.keyAt(i), true /*fromCache*/);
#ifdef QCOM_ANC_HEADSET_ENABLED
               if(device == AUDIO_DEVICE_OUT_ANC_HEADPHONE ||
                  device == AUDIO_DEVICE_OUT_ANC_HEADSET) {
                  if(newDevice == 0){
                      newDevice = getDeviceForStrategy(STRATEGY_MEDIA, false);
                  }
              }
#endif
              setOutputDevice(mOutputs.keyAt(i),
                            getNewDevice(mOutputs.keyAt(i), true /*fromCache*/),
                            true,
                            0);
           }
       }
        if (device == AUDIO_DEVICE_OUT_WIRED_HEADSET) {
            device = AUDIO_DEVICE_IN_WIRED_HEADSET;
        } else if (device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO ||
                   device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET ||
                   device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            device = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else if(device == AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET){
            device = AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET;
#ifdef QCOM_ANC_HEADSET_ENABLED
        } else if(device == AUDIO_DEVICE_OUT_ANC_HEADSET){
               device = AUDIO_DEVICE_IN_ANC_HEADSET; //wait for actual ANC device
#endif
        } else {
            return NO_ERROR;
        }
    }
    // handle input devices
    if (audio_is_input_device(device)) {

        switch (state)
        {
        // handle input device connection
        case AudioSystem::DEVICE_STATE_AVAILABLE: {
            if (mAvailableInputDevices & device) {
                ALOGW("setDeviceConnectionState() device already connected: %d", device);
                return INVALID_OPERATION;
            }
            mAvailableInputDevices = mAvailableInputDevices | (device & ~AUDIO_DEVICE_BIT_IN);
            }
            break;

        // handle input device disconnection
        case AudioSystem::DEVICE_STATE_UNAVAILABLE: {
            if (!(mAvailableInputDevices & device)) {
                ALOGW("setDeviceConnectionState() device not connected: %d", device);
                return INVALID_OPERATION;
            }
            mAvailableInputDevices = (audio_devices_t) (mAvailableInputDevices & ~device);
            } break;

        default:
            ALOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

        audio_io_handle_t activeInput = getActiveInput();
        if (activeInput != 0) {
            AudioInputDescriptor *inputDesc = mInputs.valueFor(activeInput);
            audio_devices_t newDevice = getDeviceForInputSource(inputDesc->mInputSource);
            if ((newDevice != AUDIO_DEVICE_NONE) && (newDevice != inputDesc->mDevice)) {
                ALOGV("setDeviceConnectionState() changing device from %x to %x for input %d",
                        inputDesc->mDevice, newDevice, activeInput);
                inputDesc->mDevice = newDevice;
                AudioParameter param = AudioParameter();
                param.addInt(String8(AudioParameter::keyRouting), (int)newDevice);
                mpClientInterface->setParameters(activeInput, param.toString());
            }
        }

        return NO_ERROR;
    }

    ALOGW("setDeviceConnectionState() invalid device: %x", device);
    return BAD_VALUE;
}


AudioSystem::device_connection_state AudioPolicyManager::getDeviceConnectionState(audio_devices_t device,
                                                  const char *device_address)
{
    AudioSystem::device_connection_state state = AudioSystem::DEVICE_STATE_UNAVAILABLE;
    String8 address = String8(device_address);
    if (audio_is_output_device(device)) {
        if (device & mAvailableOutputDevices) {
            if (audio_is_a2dp_device(device) &&
                ((address != "" && mA2dpDeviceAddress != address))) {
                return state;
            }
            if (audio_is_bluetooth_sco_device(device) &&
                address != "" && mScoDeviceAddress != address) {
                return state;
            }
            if (audio_is_usb_device(device) &&
                ((address != "" && mUsbOutCardAndDevice != address))) {
                ALOGE("getDeviceConnectionState() invalid device: %x", device);
                return state;
            }
            if (audio_is_remote_submix_device((audio_devices_t)device) && !mHasRemoteSubmix) {
                return state;
            }
            state = AudioSystem::DEVICE_STATE_AVAILABLE;
        }
    } else if (audio_is_input_device(device)) {
        if (device & mAvailableInputDevices) {
            state = AudioSystem::DEVICE_STATE_AVAILABLE;
        }
    }

    return state;
}


void AudioPolicyManager::setPhoneState(int state)
{
    ALOGV("setPhoneState() state %d", state);
    audio_devices_t newDevice = AUDIO_DEVICE_NONE;
    if (state < 0 || state >= AudioSystem::NUM_MODES) {
        ALOGW("setPhoneState() invalid state %d", state);
        return;
    }

    if (state == mPhoneState ) {
        ALOGW("setPhoneState() setting same state %d", state);
        return;
    }

    // if leaving call state, handle special case of active streams
    // pertaining to sonification strategy see handleIncallSonification()
    if (isInCall()) {
        ALOGV("setPhoneState() in call state management: new state is %d", state);
        for (int stream = 0; stream < AudioSystem::NUM_STREAM_TYPES; stream++) {
            handleIncallSonification(stream, false, true);
        }
    }

    // store previous phone state for management of sonification strategy below
    int oldState = mPhoneState;
    mPhoneState = state;
    bool force = false;

    // are we entering or starting a call
    if (!isStateInCall(oldState) && isStateInCall(state)) {
        ALOGV("  Entering call in setPhoneState()");
        // force routing command to audio hardware when starting a call
        // even if no device change is needed
        force = true;
    } else if (isStateInCall(oldState) && !isStateInCall(state)) {
        ALOGV("  Exiting call in setPhoneState()");
        // force routing command to audio hardware when exiting a call
        // even if no device change is needed
        force = true;
    } else if (isStateInCall(state) && (state != oldState)) {
        ALOGV("  Switching between telephony and VoIP in setPhoneState()");
        // force routing command to audio hardware when switching between telephony and VoIP
        // even if no device change is needed
        force = true;
    }

    // check for device and output changes triggered by new phone state
    // Need to update A2DP suspend first then getNewDevice(from cache)
    checkA2dpSuspend();
    checkOutputForAllStrategies();
    updateDevicesAndOutputs();
    newDevice = getNewDevice(mPrimaryOutput, false /*fromCache*/);

    AudioOutputDescriptor *hwOutputDesc = mOutputs.valueFor(mPrimaryOutput);

    // force routing command to audio hardware when ending call
    // even if no device change is needed
    if (isStateInCall(oldState) && newDevice == AUDIO_DEVICE_NONE) {
        newDevice = hwOutputDesc->device();
    }

    int delayMs = 0;
    if (state == AUDIO_MODE_IN_CALL &&
#ifdef QCOM_CSDCLIENT_ENABLED
        platform_is_Fusion3() &&
#endif
        oldState == AUDIO_MODE_RINGTONE) {
        delayMs = 40;
    }

    if (isStateInCall(state)) {
        nsecs_t sysTime = systemTime();
        for (size_t i = 0; i < mOutputs.size(); i++) {
            AudioOutputDescriptor *desc = mOutputs.valueAt(i);
            // mute media and sonification strategies and delay device switch by the largest
            // latency of any output where either strategy is active.
            // This avoid sending the ring tone or music tail into the earpiece or headset.
            if ((desc->isStrategyActive(STRATEGY_MEDIA,
                                     SONIFICATION_HEADSET_MUSIC_DELAY,
                                     sysTime) ||
                    desc->isStrategyActive(STRATEGY_SONIFICATION,
                                         SONIFICATION_HEADSET_MUSIC_DELAY,
                                         sysTime)) &&
                    (delayMs < (int)desc->mLatency*2)) {
                delayMs = desc->mLatency*2;
            }
            setStrategyMute(STRATEGY_MEDIA, true, mOutputs.keyAt(i));
            setStrategyMute(STRATEGY_MEDIA, false, mOutputs.keyAt(i), MUTE_TIME_MS,
                getDeviceForStrategy(STRATEGY_MEDIA, true /*fromCache*/));
            setStrategyMute(STRATEGY_SONIFICATION, true, mOutputs.keyAt(i));
            setStrategyMute(STRATEGY_SONIFICATION, false, mOutputs.keyAt(i), MUTE_TIME_MS,
                getDeviceForStrategy(STRATEGY_SONIFICATION, true /*fromCache*/));
        }
    }

    // change routing is necessary
    setOutputDevice(mPrimaryOutput, newDevice, force, delayMs);

    //update device for all non-primary outputs
    for (size_t i = 0; i < mOutputs.size(); i++) {
        audio_io_handle_t output = mOutputs.keyAt(i);
        if (output != mPrimaryOutput) {
            newDevice = getNewDevice(output, false /*fromCache*/);
            setOutputDevice(output, newDevice, (newDevice != AUDIO_DEVICE_NONE));
        }
    }

    // if entering in call state, handle special case of active streams
    // pertaining to sonification strategy see handleIncallSonification()
    if (isStateInCall(state)) {
        ALOGV("setPhoneState() in call state management: new state is %d", state);
        for (int stream = 0; stream < AudioSystem::NUM_STREAM_TYPES; stream++) {
            handleIncallSonification(stream, true, true);
        }
    }

    // Flag that ringtone volume must be limited to music volume until we exit MODE_RINGTONE
    if (state == AudioSystem::MODE_RINGTONE &&
        isStreamActive(AudioSystem::MUSIC, SONIFICATION_HEADSET_MUSIC_DELAY)) {
        mLimitRingtoneVolume = true;
    } else {
        mLimitRingtoneVolume = false;
    }
}


void AudioPolicyManager::setForceUse(AudioSystem::force_use usage, AudioSystem::forced_config config)
{
    ALOGV("setForceUse() usage %d, config %d, mPhoneState %d", usage, config, mPhoneState);

    bool forceVolumeReeval = false;
    switch(usage) {
    case AudioSystem::FOR_COMMUNICATION:
        if (config != AudioSystem::FORCE_SPEAKER && config != AudioSystem::FORCE_BT_SCO &&
            config != AudioSystem::FORCE_NONE) {
            ALOGW("setForceUse() invalid config %d for FOR_COMMUNICATION", config);
            return;
        }
        forceVolumeReeval = true;
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_MEDIA:
        if (config != AudioSystem::FORCE_HEADPHONES && config != AudioSystem::FORCE_BT_A2DP &&
            config != AudioSystem::FORCE_WIRED_ACCESSORY &&
            config != AudioSystem::FORCE_ANALOG_DOCK &&
            config != AudioSystem::FORCE_DIGITAL_DOCK && config != AudioSystem::FORCE_NONE &&
            config != AudioSystem::FORCE_NO_BT_A2DP && config != AudioSystem::FORCE_SPEAKER) {
            ALOGW("setForceUse() invalid config %d for FOR_MEDIA", config);
            return;
        }
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_RECORD:
        if (config != AudioSystem::FORCE_BT_SCO && config != AudioSystem::FORCE_WIRED_ACCESSORY &&
            config != AudioSystem::FORCE_NONE) {
            ALOGW("setForceUse() invalid config %d for FOR_RECORD", config);
            return;
        }
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_DOCK:
        if (config != AudioSystem::FORCE_NONE && config != AudioSystem::FORCE_BT_CAR_DOCK &&
            config != AudioSystem::FORCE_BT_DESK_DOCK &&
            config != AudioSystem::FORCE_WIRED_ACCESSORY &&
            config != AudioSystem::FORCE_ANALOG_DOCK &&
            config != AudioSystem::FORCE_DIGITAL_DOCK) {
            ALOGW("setForceUse() invalid config %d for FOR_DOCK", config);
        }
        forceVolumeReeval = true;
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_SYSTEM:
        if (config != AudioSystem::FORCE_NONE &&
            config != AudioSystem::FORCE_SYSTEM_ENFORCED) {
            ALOGW("setForceUse() invalid config %d for FOR_SYSTEM", config);
        }
        forceVolumeReeval = true;
        mForceUse[usage] = config;
        break;
    default:
        ALOGW("setForceUse() invalid usage %d", usage);
        break;
    }

    // check for device and output changes triggered by new force usage
    checkA2dpSuspend();
    checkOutputForAllStrategies();
    updateDevicesAndOutputs();
    for (int i = mOutputs.size() -1; i >= 0; i--) {
        audio_io_handle_t output = mOutputs.keyAt(i);
        audio_devices_t newDevice = getNewDevice(output, true /*fromCache*/);
        setOutputDevice(output, newDevice, (newDevice != AUDIO_DEVICE_NONE));
        if (forceVolumeReeval && (newDevice != AUDIO_DEVICE_NONE)) {
            applyStreamVolumes(output, newDevice, 0, true);
        }
    }

    audio_io_handle_t activeInput = getActiveInput();
    if (activeInput != 0) {
        AudioInputDescriptor *inputDesc = mInputs.valueFor(activeInput);
        audio_devices_t newDevice = getDeviceForInputSource(inputDesc->mInputSource);
        if ((newDevice != AUDIO_DEVICE_NONE) && (newDevice != inputDesc->mDevice)) {
            ALOGV("setForceUse() changing device from %x to %x for input %d",
                    inputDesc->mDevice, newDevice, activeInput);
            inputDesc->mDevice = newDevice;
            AudioParameter param = AudioParameter();
            param.addInt(String8(AudioParameter::keyRouting), (int)newDevice);
            mpClientInterface->setParameters(activeInput, param.toString());
        }
    }

}

audio_io_handle_t AudioPolicyManager::getOutput(AudioSystem::stream_type stream,
                                    uint32_t samplingRate,
                                    uint32_t format,
                                    uint32_t channelMask,
                                    AudioSystem::output_flags flags,
                                    const audio_offload_info_t *offloadInfo)
{
    audio_io_handle_t output = 0;
    uint32_t latency = 0;
    routing_strategy strategy = getStrategy((AudioSystem::stream_type)stream);
    audio_devices_t device = getDeviceForStrategy(strategy, false /*fromCache*/);
    ALOGV("getOutput() device %d, stream %d, samplingRate %d, format %x, channelMask %x, flags %x",
          device, stream, samplingRate, format, channelMask, flags);

#ifdef AUDIO_POLICY_TEST
    if (mCurOutput != 0) {
        ALOGV("getOutput() test output mCurOutput %d, samplingRate %d, format %d, channelMask %x, mDirectOutput %d",
                mCurOutput, mTestSamplingRate, mTestFormat, mTestChannels, mDirectOutput);

        if (mTestOutputs[mCurOutput] == 0) {
            ALOGV("getOutput() opening test output");
            AudioOutputDescriptor *outputDesc = new AudioOutputDescriptor(NULL);
            outputDesc->mDevice = mTestDevice;
            outputDesc->mSamplingRate = mTestSamplingRate;
            outputDesc->mFormat = mTestFormat;
            outputDesc->mChannelMask = mTestChannels;
            outputDesc->mLatency = mTestLatencyMs;
            outputDesc->mFlags = (audio_output_flags_t)(mDirectOutput ? AudioSystem::OUTPUT_FLAG_DIRECT : 0);
            outputDesc->mRefCount[stream] = 0;
            mTestOutputs[mCurOutput] = mpClientInterface->openOutput(0, &outputDesc->mDevice,
                                            &outputDesc->mSamplingRate,
                                            &outputDesc->mFormat,
                                            &outputDesc->mChannelMask,
                                            &outputDesc->mLatency,
                                            outputDesc->mFlags,
                                            offloadInfo);
            if (mTestOutputs[mCurOutput]) {
                AudioParameter outputCmd = AudioParameter();
                outputCmd.addInt(String8("set_id"),mCurOutput);
                mpClientInterface->setParameters(mTestOutputs[mCurOutput],outputCmd.toString());
                addOutput(mTestOutputs[mCurOutput], outputDesc);
            }
        }
        return mTestOutputs[mCurOutput];
    }
#endif //AUDIO_POLICY_TEST

    // open a direct output if required by specified parameters
    //force direct flag if offload flag is set: offloading implies a direct output stream
    // and all common behaviors are driven by checking only the direct flag
    // this should normally be set appropriately in the policy configuration file
    if ((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) != 0) {
        flags = (AudioSystem::output_flags)(flags | AUDIO_OUTPUT_FLAG_DIRECT);
    }

    IOProfile *profile = getProfileForDirectOutput(device,
                                            samplingRate,
                                            format,
                                            channelMask,
                                            (audio_output_flags_t)flags);

    if (profile != NULL) {
        AudioOutputDescriptor *outputDesc = NULL;

        for (size_t i = 0; i < mOutputs.size(); i++) {
            AudioOutputDescriptor *desc = mOutputs.valueAt(i);
            if (!desc->isDuplicated() && (profile == desc->mProfile)) {
                outputDesc = desc;
                // reuse direct output if currently open and configured with same parameters
                if ((samplingRate == outputDesc->mSamplingRate) &&
                        (format == outputDesc->mFormat) &&
                        (channelMask == outputDesc->mChannelMask)) {
                    outputDesc->mDirectOpenCount++;
                    ALOGV("getOutput() reusing direct output %d", mOutputs.keyAt(i));
                    return mOutputs.keyAt(i);
                }
            }
        }
        // close direct output if currently open and configured with different parameters
        if (outputDesc != NULL) {
            closeOutput(outputDesc->mId);
        }
        outputDesc = new AudioOutputDescriptor(profile);
        outputDesc->mDevice = device;
        outputDesc->mSamplingRate = samplingRate;
        outputDesc->mFormat = (audio_format_t)format;
        outputDesc->mChannelMask = (audio_channel_mask_t)channelMask;
        outputDesc->mLatency = 0;
        outputDesc->mFlags =(audio_output_flags_t) (outputDesc->mFlags | flags);
        outputDesc->mRefCount[stream] = 0;
        outputDesc->mStopTime[stream] = 0;
        outputDesc->mDirectOpenCount = 1;
        output = mpClientInterface->openOutput(profile->mModule->mHandle,
                                        &outputDesc->mDevice,
                                        &outputDesc->mSamplingRate,
                                        &outputDesc->mFormat,
                                        &outputDesc->mChannelMask,
                                        &outputDesc->mLatency,
                                        outputDesc->mFlags,
                                        offloadInfo);

        // only accept an output with the requested parameters
        if (output == 0 ||
            (samplingRate != 0 && samplingRate != outputDesc->mSamplingRate) ||
            (format != 0 && format != outputDesc->mFormat) ||
            (channelMask != 0 && channelMask != outputDesc->mChannelMask)) {
            ALOGV("getOutput() failed opening direct output: output %d samplingRate %d %d,"
                    "format %d %d, channelMask %04x %04x", output, samplingRate,
                    outputDesc->mSamplingRate, format, outputDesc->mFormat, channelMask,
                    outputDesc->mChannelMask);
            if (output != 0) {
                mpClientInterface->closeOutput(output);
            }
            delete outputDesc;
            return 0;
        }
        addOutput(output, outputDesc);
        mPreviousOutputs = mOutputs;
        ALOGV("getOutput() returns new direct output %d", output);
        return output;
    }

    // ignoring channel mask due to downmix capability in mixer

    // open a non direct output

    // for non direct outputs, only PCM is supported
    if (audio_is_linear_pcm((audio_format_t)format)) {
        // get which output is suitable for the specified stream. The actual
        // routing change will happen when startOutput() will be called
    SortedVector<audio_io_handle_t> outputs = getOutputsForDevice(device, mOutputs);

    output = selectOutput(outputs, flags);
    }
    ALOGW_IF((output ==0), "getOutput() could not find output for stream %d, samplingRate %d,"
            "format %d, channels %x, flags %x", stream, samplingRate, format, channelMask, flags);

    ALOGV("getOutput() returns output %d", output);

    return output;
}
status_t AudioPolicyManager::startOutput(audio_io_handle_t output,
                                             AudioSystem::stream_type stream,
                                             int session)
{
    ALOGV("startOutput() output %d, stream %d, session %d", output, stream, session);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        ALOGW("startOutput() unknow output %d", output);
        return BAD_VALUE;
    }

    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);

    // increment usage count for this stream on the requested output:
    // NOTE that the usage count is the same for duplicated output and hardware output which is
    // necessary for a correct control of hardware output routing by startOutput() and stopOutput()
    outputDesc->changeRefCount(stream, 1);

    if (outputDesc->mRefCount[stream] == 1) {
        audio_devices_t newDevice = getNewDevice(output, false /*fromCache*/);
        routing_strategy strategy = getStrategy(stream);
        bool shouldWait = (strategy == STRATEGY_SONIFICATION) ||
                            (strategy == STRATEGY_SONIFICATION_RESPECTFUL);
        uint32_t waitMs = 0;
        uint32_t muteWaitMs = 0;
        bool force = false;
        for (size_t i = 0; i < mOutputs.size(); i++) {
            AudioOutputDescriptor *desc = mOutputs.valueAt(i);
            if (desc != outputDesc) {
                // force a device change if any other output is managed by the same hw
                // module and has a current device selection that differs from selected device.
                // In this case, the audio HAL must receive the new device selection so that it can
                // change the device currently selected by the other active output.
                if (mForceDeviceChange || (outputDesc->sharesHwModuleWith(desc) &&
                    desc->device() != newDevice)) {
                    force = true;
                    mForceDeviceChange=false;
                }
                // wait for audio on other active outputs to be presented when starting
                // a notification so that audio focus effect can propagate.
                uint32_t latency = desc->latency();
                if (shouldWait && desc->isActive(latency * 2) && (waitMs < latency)) {
                    waitMs = latency;
                }
            }
        }

#ifdef QCOM_FM_ENABLED
        if(stream == AudioSystem::MUSIC && output == getA2dpOutput()) {
            muteWaitMs = setOutputDevice(output, newDevice, true);
        } else
#endif
        {
            muteWaitMs = setOutputDevice(output, newDevice, force);
        }

        // handle special case for sonification while in call
        if (isInCall()) {
            handleIncallSonification(stream, true, false);
        }

        // apply volume rules for current stream and device if necessary
        checkAndSetVolume(stream,
                          mStreams[stream].getVolumeIndex(newDevice),
                          output,
                          newDevice);

        // update the outputs if starting an output with a stream that can affect notification
        // routing
        handleNotificationRoutingForStream(stream);
        if (waitMs > muteWaitMs) {
            usleep((waitMs - muteWaitMs) * 2 * 1000);
        }
    } else {
        // handle special case for sonification while in call
        if (isInCall()) {
            handleIncallSonification(stream, true, false);
        }
    }
    return NO_ERROR;
}

status_t AudioPolicyManager::stopOutput(audio_io_handle_t output,
                                            AudioSystem::stream_type stream,
                                            int session)
{
    ALOGV("stopOutput() output %d, stream %d, session %d", output, stream, session);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        ALOGW("stopOutput() unknow output %d", output);
        return BAD_VALUE;
    }

    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);

    // handle special case for sonification while in call
    if (isInCall()) {
        handleIncallSonification(stream, false, false);
    }

    if (outputDesc->mRefCount[stream] > 0) {
        // decrement usage count of this stream on the output
        outputDesc->changeRefCount(stream, -1);
        // store time at which the stream was stopped - see isStreamActive()
        if (outputDesc->mRefCount[stream] == 0) {
            outputDesc->mStopTime[stream] = systemTime();

            if ((outputDesc->mRefCount[AUDIO_STREAM_RING]!= 0) && (stream == AUDIO_STREAM_VOICE_CALL)) {
                 // When AUDIO_STREAM_RING is present, Send Mute on RING
                 // if it gets stopOutput on  AUDIO_STREAM_VOICE_CALL
                 setStreamMute(AudioSystem::RING, true, mPrimaryOutput);
            }

            audio_devices_t newDevice = getNewDevice(output, false /*fromCache*/);
            // delay the device switch by twice the latency because stopOutput() is executed when
            // the track stop() command is received and at that time the audio track buffer can
            // still contain data that needs to be drained. The latency only covers the audio HAL
            // and kernel buffers. Also the latency does not always include additional delay in the
            // audio path (audio DSP, CODEC ...)
            setOutputDevice(output, newDevice, false, outputDesc->mLatency*2);

            // force restoring the device selection on other active outputs if it differs from the
            // one being selected for this output
            for (size_t i = 0; i < mOutputs.size(); i++) {
                audio_io_handle_t curOutput = mOutputs.keyAt(i);
                AudioOutputDescriptor *desc = mOutputs.valueAt(i);
                if (curOutput != output &&
                        desc->isActive() &&
                        outputDesc->sharesHwModuleWith(desc) &&
                        (newDevice != desc->device())) {
                    setOutputDevice(curOutput,
                                    getNewDevice(curOutput, false /*fromCache*/),
                                    true,
                                    outputDesc->mLatency*2);
                }
            }
            // update the outputs if stopping one with a stream that can affect notification routing
            handleNotificationRoutingForStream(stream);
        }
        return NO_ERROR;
    } else {
        ALOGW("stopOutput() refcount is already 0 for output %d", output);
        return INVALID_OPERATION;
    }
}


audio_io_handle_t AudioPolicyManager::getInput(int inputSource,
                                    uint32_t samplingRate,
                                    audio_format_t format,
                                    audio_channel_mask_t channelMask,
                                    AudioSystem::audio_in_acoustics acoustics)
{
    audio_io_handle_t input = 0;
    audio_devices_t device = getDeviceForInputSource(inputSource);

    ALOGV("getInput() inputSource %d, samplingRate %d, format %d, channelMask %x, acoustics %x",
          inputSource, samplingRate, format, channelMask, acoustics);

    if (device == AUDIO_DEVICE_NONE) {
        ALOGW("getInput() could not find device for inputSource %d", inputSource);
        return 0;
    }

    // adapt channel selection to input source
    switch(inputSource) {
    case AUDIO_SOURCE_VOICE_UPLINK:
        channelMask |= AudioSystem::CHANNEL_IN_VOICE_UPLINK;
        break;
    case AUDIO_SOURCE_VOICE_DOWNLINK:
        channelMask |= AudioSystem::CHANNEL_IN_VOICE_DNLINK;
        break;
    case AUDIO_SOURCE_VOICE_CALL:
        channelMask |= (AudioSystem::CHANNEL_IN_VOICE_UPLINK | AudioSystem::CHANNEL_IN_VOICE_DNLINK);
        break;
    default:
        break;
    }

    IOProfile *profile = getInputProfile(device,
                                         samplingRate,
                                         format,
                                         channelMask);
    if (profile == NULL) {
        ALOGW("getInput() could not find profile for device %04x, samplingRate %d, format %d,"
                "channelMask %04x",
                device, samplingRate, format, channelMask);
        return 0;
    }

    if (profile->mModule->mHandle == 0) {
        ALOGE("getInput(): HW module %s not opened", profile->mModule->mName);
        return 0;
    }

    AudioInputDescriptor *inputDesc = new AudioInputDescriptor(profile);

    inputDesc->mInputSource = inputSource;
    inputDesc->mDevice = device;
    inputDesc->mSamplingRate = samplingRate;
    inputDesc->mFormat = (audio_format_t)format;
    inputDesc->mChannelMask = (audio_channel_mask_t)channelMask;
    inputDesc->mRefCount = 0;
    input = mpClientInterface->openInput(profile->mModule->mHandle,
                                    &inputDesc->mDevice,
                                    &inputDesc->mSamplingRate,
                                    &inputDesc->mFormat,
                                    &inputDesc->mChannelMask);

    // only accept input with the exact requested set of parameters
    if (input == 0 ||
        (samplingRate != inputDesc->mSamplingRate) ||
        (format != inputDesc->mFormat) ||
        (channelMask != inputDesc->mChannelMask)) {
        ALOGV("getInput() failed opening input: samplingRate %d, format %d, channelMask %d",
                samplingRate, format, channelMask);
        if (input != 0) {
            mpClientInterface->closeInput(input);
        }
        delete inputDesc;
        return 0;
    }
    mInputs.add(input, inputDesc);
    return input;
}

/*
Overwriting this function from base class to allow 2 acitve AudioRecord clients in case of FM.
One for FM A2DP playbck and other for FM recording.
*/
status_t AudioPolicyManager::startInput(audio_io_handle_t input)
{
    ALOGV("startInput() input %d", input);
    ssize_t index = mInputs.indexOfKey(input);
    if (index < 0) {
        ALOGW("startInput() unknow input %d", input);
        return BAD_VALUE;
    }
    AudioInputDescriptor *inputDesc = mInputs.valueAt(index);


#ifdef AUDIO_POLICY_TEST
    if (mTestInput == 0)
#endif //AUDIO_POLICY_TEST
    {
        // refuse 2 active AudioRecord clients at the same time except if the active input
        // uses AUDIO_SOURCE_HOTWORD in which case it is closed.
        audio_io_handle_t activeInput = getActiveInput();
        if (!isVirtualInputDevice(inputDesc->mDevice) && activeInput != 0) {
            AudioInputDescriptor *activeDesc = mInputs.valueFor(activeInput);
            if (activeDesc->mInputSource == AUDIO_SOURCE_HOTWORD) {
                ALOGW("startInput() preempting already started low-priority input %d", activeInput);
                stopInput(activeInput);
                releaseInput(activeInput);
            } else {
                ALOGW("startInput() input %d failed: other input already started..", input);
                return INVALID_OPERATION;
            }
        }
    }

    audio_devices_t newDevice = getDeviceForInputSource(inputDesc->mInputSource);
    if ((newDevice != AUDIO_DEVICE_NONE) && (newDevice != inputDesc->mDevice)) {
        inputDesc->mDevice = newDevice;
    }
    // automatically enable the remote submix output when input is started
    if (audio_is_remote_submix_device(inputDesc->mDevice)) {
        setDeviceConnectionState(AUDIO_DEVICE_OUT_REMOTE_SUBMIX,
                AudioSystem::DEVICE_STATE_AVAILABLE, AUDIO_REMOTE_SUBMIX_DEVICE_ADDRESS);
    }
    AudioParameter param = AudioParameter();
    param.addInt(String8(AudioParameter::keyRouting), (int)inputDesc->mDevice);

    int aliasSource = (inputDesc->mInputSource == AUDIO_SOURCE_HOTWORD) ?
                                        AUDIO_SOURCE_VOICE_RECOGNITION : inputDesc->mInputSource;

    param.addInt(String8(AudioParameter::keyInputSource), aliasSource);
    ALOGV("AudioPolicyManager::startInput() input source = %d", inputDesc->mInputSource);

    //to pass on if camcorder mode is enabled to HAL
    int camcorder_enabled = inputDesc->mInputSource == AUDIO_SOURCE_CAMCORDER ? 1 : 0;
    param.addInt(String8("camcorder_mode"), camcorder_enabled);

    mpClientInterface->setParameters(input, param.toString());

    inputDesc->mRefCount = 1;
    return NO_ERROR;
}

status_t AudioPolicyManager::stopInput(audio_io_handle_t input)
{
    ALOGV("stopInput() input %d", input);
    ssize_t index = mInputs.indexOfKey(input);
    if (index < 0) {
        ALOGW("stopInput() unknow input %d", input);
        return BAD_VALUE;
    }
    AudioInputDescriptor *inputDesc = mInputs.valueAt(index);

    if (inputDesc->mRefCount == 0) {
        ALOGW("stopInput() input %d already stopped", input);
        return INVALID_OPERATION;
    } else {
        // automatically disable the remote submix output when input is stopped
        if (audio_is_remote_submix_device(inputDesc->mDevice)) {
            setDeviceConnectionState(AUDIO_DEVICE_OUT_REMOTE_SUBMIX,
                    AudioSystem::DEVICE_STATE_UNAVAILABLE, AUDIO_REMOTE_SUBMIX_DEVICE_ADDRESS);
        }

        AudioParameter param = AudioParameter();
        param.addInt(String8(AudioParameter::keyRouting), 0);
        mpClientInterface->setParameters(input, param.toString());
        setOutputDevice(mPrimaryOutput, getNewDevice(mPrimaryOutput, true), true);
        inputDesc->mRefCount = 0;
        return NO_ERROR;
    }
}


status_t AudioPolicyManager::setStreamVolumeIndex(AudioSystem::stream_type stream,
                                                      int index,
                                                      audio_devices_t device)
{

    if ((index < mStreams[stream].mIndexMin) || (index > mStreams[stream].mIndexMax)) {
        return BAD_VALUE;
    }
    if (!audio_is_output_device(device)) {
        return BAD_VALUE;
    }

    // Force max volume if stream cannot be muted
    if (!mStreams[stream].mCanBeMuted) index = mStreams[stream].mIndexMax;

    ALOGV("setStreamVolumeIndex() stream %d, device %04x, index %d",
          stream, device, index);

    // if device is AUDIO_DEVICE_OUT_DEFAULT set default value and
    // clear all device specific values
    if ((device == AUDIO_DEVICE_OUT_DEFAULT) && (AUDIO_DEVICE_OUT_DEFAULT != AUDIO_DEVICE_OUT_SPEAKER))  {
        mStreams[stream].mIndexCur.clear();
    }
    mStreams[stream].mIndexCur.add(device, index);

    // compute and apply stream volume on all outputs according to connected device
    status_t status = NO_ERROR;
    for (size_t i = 0; i < mOutputs.size(); i++) {
        audio_devices_t curDevice =
                getDeviceForVolume(mOutputs.valueAt(i)->device());
        if (device == curDevice) {
            status_t volStatus = checkAndSetVolume(stream, index, mOutputs.keyAt(i), curDevice);
            if (volStatus != NO_ERROR) {
                status = volStatus;
            }
        }
    }
    return status;
}


status_t AudioPolicyManager::checkOutputsForDevice(audio_devices_t device,
                                                       AudioSystem::device_connection_state state,
                                                       SortedVector<audio_io_handle_t>& outputs)
{
    AudioOutputDescriptor *desc;

    if (state == AudioSystem::DEVICE_STATE_AVAILABLE) {
        // first list already open outputs that can be routed to this device
        for (size_t i = 0; i < mOutputs.size(); i++) {
            desc = mOutputs.valueAt(i);
            if (!desc->isDuplicated() && (desc->mProfile->mSupportedDevices & device)) {
                ALOGV("checkOutputsForDevice(): adding opened output %d", mOutputs.keyAt(i));
                outputs.add(mOutputs.keyAt(i));
            }
        }
        // then look for output profiles that can be routed to this device
        SortedVector<IOProfile *> profiles;
        for (size_t i = 0; i < mHwModules.size(); i++)
        {
            if (mHwModules[i]->mHandle == 0) {
                continue;
            }
            for (size_t j = 0; j < mHwModules[i]->mOutputProfiles.size(); j++)
            {
                if (mHwModules[i]->mOutputProfiles[j]->mSupportedDevices & device) {
                    ALOGV("checkOutputsForDevice(): adding profile %d from module %d", j, i);
                    profiles.add(mHwModules[i]->mOutputProfiles[j]);
                }
            }
        }

        if (profiles.isEmpty() && outputs.isEmpty()) {
            ALOGW("checkOutputsForDevice(): No output available for device %04x", device);
            return BAD_VALUE;
        }

        // open outputs for matching profiles if needed. Direct outputs are also opened to
        // query for dynamic parameters and will be closed later by setDeviceConnectionState()
        for (ssize_t profile_index = 0; profile_index < (ssize_t)profiles.size(); profile_index++) {
            IOProfile *profile = profiles[profile_index];

            // nothing to do if one output is already opened for this profile
            size_t j;
            for (j = 0; j < mOutputs.size(); j++) {
                desc = mOutputs.valueAt(j);
                if (!desc->isDuplicated() && desc->mProfile == profile) {
                    break;
                }
            }
            if (j != mOutputs.size()) {
                continue;
            }

            ALOGV("opening output for device %08x", device);
            desc = new AudioOutputDescriptor(profile);
            desc->mDevice = device;
            audio_io_handle_t output = 0;
#ifdef QCOM_OUTPUT_FLAGS_ENABLED
            if (!(desc->mFlags & AUDIO_OUTPUT_FLAG_LPA || desc->mFlags & AUDIO_OUTPUT_FLAG_TUNNEL ||
                desc->mFlags & AUDIO_OUTPUT_FLAG_VOIP_RX)) {
#endif
                output =  mpClientInterface->openOutput(profile->mModule->mHandle,
                                                        &desc->mDevice,
                                                        &desc->mSamplingRate,
                                                        &desc->mFormat,
                                                        &desc->mChannelMask,
                                                        &desc->mLatency,
                                                        desc->mFlags);
#ifdef QCOM_OUTPUT_FLAGS_ENABLED
            }
#endif
            if (output != 0) {
                if (desc->mFlags & AUDIO_OUTPUT_FLAG_DIRECT) {
                    String8 reply;
                    char *value;
                    if (profile->mSamplingRates[0] == 0) {
                        reply = mpClientInterface->getParameters(output,
                                                String8(AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES));
                        ALOGV("checkOutputsForDevice() direct output sup sampling rates %s",
                                  reply.string());
                        value = strpbrk((char *)reply.string(), "=");
                        if (value != NULL) {
                            loadSamplingRates(value, profile);
                        }
                    }
                    if (profile->mFormats[0] == 0) {
                        reply = mpClientInterface->getParameters(output,
                                                       String8(AUDIO_PARAMETER_STREAM_SUP_FORMATS));
                        ALOGV("checkOutputsForDevice() direct output sup formats %s",
                                  reply.string());
                        value = strpbrk((char *)reply.string(), "=");
                        if (value != NULL) {
                            loadFormats(value, profile);
                        }
                    }
                    if (profile->mChannelMasks[0] == 0) {
                        reply = mpClientInterface->getParameters(output,
                                                      String8(AUDIO_PARAMETER_STREAM_SUP_CHANNELS));
                        ALOGV("checkOutputsForDevice() direct output sup channel masks %s",
                                  reply.string());
                        value = strpbrk((char *)reply.string(), "=");
                        if (value != NULL) {
                            loadOutChannels(value + 1, profile);
                        }
                    }
                    if (((profile->mSamplingRates[0] == 0) &&
                             (profile->mSamplingRates.size() < 2)) ||
                         ((profile->mFormats[0] == 0) &&
                             (profile->mFormats.size() < 2)) ||
                         ((profile->mFormats[0] == 0) &&
                             (profile->mChannelMasks.size() < 2))) {
                        ALOGW("checkOutputsForDevice() direct output missing param");
                        output = 0;
                    } else {
                        addOutput(output, desc);
                    }
                } else {
                    audio_io_handle_t duplicatedOutput = 0;
                    // add output descriptor
                    addOutput(output, desc);
                    // set initial stream volume for device
                    applyStreamVolumes(output, device, 0, true);

                    //TODO: configure audio effect output stage here

                    // open a duplicating output thread for the new output and the primary output
                    duplicatedOutput = mpClientInterface->openDuplicateOutput(output,
                                                                              mPrimaryOutput);
                    if (duplicatedOutput != 0) {
                        // add duplicated output descriptor
                        AudioOutputDescriptor *dupOutputDesc = new AudioOutputDescriptor(NULL);
                        dupOutputDesc->mOutput1 = mOutputs.valueFor(mPrimaryOutput);
                        dupOutputDesc->mOutput2 = mOutputs.valueFor(output);
                        dupOutputDesc->mSamplingRate = desc->mSamplingRate;
                        dupOutputDesc->mFormat = desc->mFormat;
                        dupOutputDesc->mChannelMask = desc->mChannelMask;
                        dupOutputDesc->mLatency = desc->mLatency;
                        addOutput(duplicatedOutput, dupOutputDesc);
                        applyStreamVolumes(duplicatedOutput, device, 0, true);
                    } else {
                        ALOGW("checkOutputsForDevice() could not open dup output for %d and %d",
                                mPrimaryOutput, output);
                        mpClientInterface->closeOutput(output);
                        mOutputs.removeItem(output);
                        output = 0;
                    }
                }
            }
            if (output == 0) {
                ALOGW("checkOutputsForDevice() could not open output for device %x", device);
                delete desc;
                profiles.removeAt(profile_index);
                profile_index--;
            } else {
                outputs.add(output);
                ALOGV("checkOutputsForDevice(): adding output %d", output);
            }
        }

        if (profiles.isEmpty()) {
            ALOGW("checkOutputsForDevice(): No output available for device %04x", device);
            return BAD_VALUE;
        }
    } else {
        // check if one opened output is not needed any more after disconnecting one device
        for (size_t i = 0; i < mOutputs.size(); i++) {
            desc = mOutputs.valueAt(i);
            if (!desc->isDuplicated() &&
                    !(desc->mProfile->mSupportedDevices & mAvailableOutputDevices)) {
                ALOGV("checkOutputsForDevice(): disconnecting adding output %d", mOutputs.keyAt(i));
                outputs.add(mOutputs.keyAt(i));
            }
        }
        for (size_t i = 0; i < mHwModules.size(); i++)
        {
            if (mHwModules[i]->mHandle == 0) {
                continue;
            }
            for (size_t j = 0; j < mHwModules[i]->mOutputProfiles.size(); j++)
            {
                IOProfile *profile = mHwModules[i]->mOutputProfiles[j];
                if ((profile->mSupportedDevices & device) &&
                        (profile->mFlags & AUDIO_OUTPUT_FLAG_DIRECT)) {
                    ALOGV("checkOutputsForDevice(): clearing direct output profile %d on module %d",
                          j, i);
                    if (profile->mSamplingRates[0] == 0) {
                        profile->mSamplingRates.clear();
                        profile->mSamplingRates.add(0);
                    }
                    if (profile->mFormats[0] == 0) {
                        profile->mFormats.clear();
                        profile->mFormats.add((audio_format_t)0);
                    }
                    if (profile->mChannelMasks[0] == 0) {
                        profile->mChannelMasks.clear();
                        profile->mChannelMasks.add((audio_channel_mask_t)0);
                    }
                }
            }
        }
    }
    return NO_ERROR;
}

audio_devices_t AudioPolicyManager::getNewDevice(audio_io_handle_t output, bool fromCache)
{
    audio_devices_t device = AUDIO_DEVICE_NONE;

    AudioOutputDescriptor *outputDesc = mOutputs.valueFor(output);
    AudioOutputDescriptor *primaryOutputDesc = mOutputs.valueFor(mPrimaryOutput);
    // check the following by order of priority to request a routing change if necessary:
    // 1: the strategy enforced audible is active on the output:
    //      use device for strategy enforced audible
    // 2: we are in call or the strategy phone is active on the output:
    //      use device for strategy phone
    // 3: the strategy sonification is active on the output:
    //      use device for strategy sonification
    // 4: the strategy "respectful" sonification is active on the output:
    //      use device for strategy "respectful" sonification
    // 5: the strategy media is active on the output:
    //      use device for strategy media
    // 6: the strategy DTMF is active on the output:
    //      use device for strategy DTMF
    if (outputDesc->isStrategyActive(STRATEGY_ENFORCED_AUDIBLE)) {
        device = getDeviceForStrategy(STRATEGY_ENFORCED_AUDIBLE, fromCache);
    } else if (isInCall() ||
                    outputDesc->isStrategyActive(STRATEGY_PHONE)) {
        device = getDeviceForStrategy(STRATEGY_PHONE, fromCache);
    } else if (outputDesc->isStrategyActive(STRATEGY_SONIFICATION)||
                (primaryOutputDesc->isStrategyActive(STRATEGY_SONIFICATION)&& !primaryOutputDesc->isStrategyActive(STRATEGY_MEDIA))){
        device = getDeviceForStrategy(STRATEGY_SONIFICATION, fromCache);
    } else if (outputDesc->isStrategyActive(STRATEGY_SONIFICATION_RESPECTFUL)) {
        device = getDeviceForStrategy(STRATEGY_SONIFICATION_RESPECTFUL, fromCache);
    } else if (outputDesc->isStrategyActive(STRATEGY_MEDIA)) {
        device = getDeviceForStrategy(STRATEGY_MEDIA, fromCache);
    } else if (outputDesc->isStrategyActive(STRATEGY_DTMF)) {
        device = getDeviceForStrategy(STRATEGY_DTMF, fromCache);
    }

    ALOGV("getNewDevice() selected device %x", device);
    return device;
}

AudioPolicyManager::routing_strategy AudioPolicyManager::getStrategy(
        AudioSystem::stream_type stream) {
    // stream to strategy mapping
    switch (stream) {
    case AudioSystem::VOICE_CALL:
    case AudioSystem::BLUETOOTH_SCO:
        return STRATEGY_PHONE;
    case AudioSystem::RING:
    case AudioSystem::ALARM:
        return STRATEGY_SONIFICATION;
    case AudioSystem::NOTIFICATION:
        return STRATEGY_SONIFICATION_RESPECTFUL;
    case AudioSystem::DTMF:
        return STRATEGY_DTMF;
    default:
        ALOGE("unknown stream type");
    case AudioSystem::SYSTEM:
        // NOTE: SYSTEM stream uses MEDIA strategy because muting music and switching outputs
        // while key clicks are played produces a poor result
    case AudioSystem::TTS:
    case AudioSystem::MUSIC:
        return STRATEGY_MEDIA;
    case AudioSystem::ENFORCED_AUDIBLE:
        return STRATEGY_ENFORCED_AUDIBLE;
    }
}
audio_devices_t AudioPolicyManager::getDeviceForStrategy(routing_strategy strategy,
                                                             bool fromCache)
{
    uint32_t device = AUDIO_DEVICE_NONE;

    if (fromCache) {
        ALOGVV("getDeviceForStrategy() from cache strategy %d, device %x",
              strategy, mDeviceForStrategy[strategy]);
        return mDeviceForStrategy[strategy];
    }

    switch (strategy) {

    case STRATEGY_SONIFICATION_RESPECTFUL:
        if (isInCall()) {
            device = getDeviceForStrategy(STRATEGY_SONIFICATION, false /*fromCache*/);
        } else if (isStreamActiveRemotely(AudioSystem::MUSIC,
                SONIFICATION_RESPECTFUL_AFTER_MUSIC_DELAY)) {
            // while media is playing on a remote device, use the the sonification behavior.
            // Note that we test this usecase before testing if media is playing because
            //   the isStreamActive() method only informs about the activity of a stream, not
            //   if it's for local playback. Note also that we use the same delay between both tests
            device = getDeviceForStrategy(STRATEGY_SONIFICATION, false /*fromCache*/);
        } else if (isStreamActive(AudioSystem::MUSIC, SONIFICATION_RESPECTFUL_AFTER_MUSIC_DELAY)) {
            // while media is playing (or has recently played), use the same device
            device = getDeviceForStrategy(STRATEGY_MEDIA, false /*fromCache*/);
        } else {
            // when media is not playing anymore, fall back on the sonification behavior
            device = getDeviceForStrategy(STRATEGY_SONIFICATION, false /*fromCache*/);
        }

        break;

    case STRATEGY_DTMF:
        if (!isInCall()) {
            // when off call, DTMF strategy follows the same rules as MEDIA strategy
            device = getDeviceForStrategy(STRATEGY_MEDIA, false /*fromCache*/);
            break;
        }
        // when in call, DTMF and PHONE strategies follow the same rules
        // FALL THROUGH

    case STRATEGY_PHONE:
        // for phone strategy, we first consider the forced use and then the available devices by order
        // of priority
        switch (mForceUse[AudioSystem::FOR_COMMUNICATION]) {
        case AudioSystem::FORCE_BT_SCO:
            if (!isInCall() || strategy != STRATEGY_DTMF) {
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT;
                if (device) break;
            }
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_SCO;
            if (device) break;
            // if SCO device is requested but no SCO device is available, fall back to default case
            // FALL THROUGH

        default:    // FORCE_NONE
            // when not in a phone call, phone strategy should route STREAM_VOICE_CALL to A2DP
            if (!isInCall())
            {
                if ((mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                        !mA2dpSuspended) {
                    device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP;
                    if (device) break;
                    device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
                    if (device) break;
                }
            }
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
            if (device) break;
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET;
            if (device) break;
#ifdef QCOM_ANC_HEADSET_ENABLED
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_ANC_HEADPHONE;
            if (device) break;
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_ANC_HEADSET;
            if (device) break;
#endif
            if (mPhoneState != AudioSystem::MODE_IN_CALL) {
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_ACCESSORY;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_DEVICE;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET;
                if (device) break;
            }
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_EARPIECE;
            if (device) break;
            device = mDefaultOutputDevice;
            if (device == AUDIO_DEVICE_NONE) {
                ALOGE("getDeviceForStrategy() no device found for STRATEGY_PHONE");
            }
            break;

        case AudioSystem::FORCE_SPEAKER:
            // when not in a phone call, phone strategy should route STREAM_VOICE_CALL to
            // A2DP speaker when forcing to speaker output
            if (!isInCall() &&
                    (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                    !mA2dpSuspended) {
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
                if (device) break;
            }
            if (mPhoneState != AudioSystem::MODE_IN_CALL && mPhoneState != AudioSystem::MODE_IN_COMMUNICATION) {
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_ACCESSORY;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_DEVICE;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET;
                if (device) break;
            }
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_SPEAKER;
            if (device) break;
            device = mDefaultOutputDevice;
            if (device == AUDIO_DEVICE_NONE) {
                ALOGE("getDeviceForStrategy() no device found for STRATEGY_PHONE, FORCE_SPEAKER");
            }
            break;
        }
#ifdef QCOM_FM_ENABLED
        if (mAvailableOutputDevices & AUDIO_DEVICE_OUT_FM) {
            if (mForceUse[AudioSystem::FOR_MEDIA] == AudioSystem::FORCE_SPEAKER) {
                device &= ~(AUDIO_DEVICE_OUT_WIRED_HEADSET);
                device &= ~(AUDIO_DEVICE_OUT_WIRED_HEADPHONE);
                device &= ~(AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET);
                device |= AUDIO_DEVICE_OUT_SPEAKER;
            }
        }
#endif
    break;

    case STRATEGY_SONIFICATION:

        // If incall, just select the STRATEGY_PHONE device: The rest of the behavior is handled by
        // handleIncallSonification().
        if (isInCall()) {
            device = getDeviceForStrategy(STRATEGY_PHONE, false /*fromCache*/);
            break;
        }
        // FALL THROUGH

    case STRATEGY_ENFORCED_AUDIBLE:
        // strategy STRATEGY_ENFORCED_AUDIBLE uses same routing policy as STRATEGY_SONIFICATION
        // except:
        //   - when in call where it doesn't default to STRATEGY_PHONE behavior
        //   - in countries where not enforced in which case it follows STRATEGY_MEDIA

        if ((strategy == STRATEGY_SONIFICATION) ||
                (mForceUse[AudioSystem::FOR_SYSTEM] == AudioSystem::FORCE_SYSTEM_ENFORCED)) {
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_SPEAKER;
            if (device == AUDIO_DEVICE_NONE) {
                ALOGE("getDeviceForStrategy() speaker device not found for STRATEGY_SONIFICATION");
            }
        }
        // The second device used for sonification is the same as the device used by media strategy
        // FALL THROUGH

    case STRATEGY_MEDIA: {
        uint32_t device2 = AUDIO_DEVICE_NONE;
        if (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_SPEAKER || strategy == STRATEGY_SONIFICATION) {
            if (strategy != STRATEGY_SONIFICATION) {
                // no sonification on remote submix (e.g. WFD)
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_REMOTE_SUBMIX;
            }
            if ((device2 == AUDIO_DEVICE_NONE) &&
                    (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                    !mA2dpSuspended) {
                if (device2 == AUDIO_DEVICE_NONE) {
                    device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP;
                }
                if (device2 == AUDIO_DEVICE_NONE) {
                    device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
                }
                if (device2 == AUDIO_DEVICE_NONE) {
                    device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
                }
            }
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
            }
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET;
            }
#ifdef QCOM_ANC_HEADSET_ENABLED
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_ANC_HEADPHONE;
            }
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_ANC_HEADSET;
            }
#endif
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_ACCESSORY;
            }
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_DEVICE;
            }
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET;
            }
            if ((device2 == AUDIO_DEVICE_NONE) && (strategy != STRATEGY_SONIFICATION)) {
                // no sonification on aux digital (e.g. HDMI)
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_AUX_DIGITAL;
            }
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET;
            }
#ifdef QCOM_FM_ENABLED
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_FM_TX;
            }
#endif
#ifdef QCOM_PROXY_DEVICE_ENABLED
            if ((strategy != STRATEGY_SONIFICATION) && (device2 == AUDIO_DEVICE_NONE)) {
                // no sonification on WFD sink
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_PROXY;
            }
#endif
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_SPEAKER;
            }

            // device is DEVICE_OUT_SPEAKER if we come from case STRATEGY_SONIFICATION or
            // STRATEGY_ENFORCED_AUDIBLE, AUDIO_DEVICE_NONE otherwise
            device |= device2;
            if (!device) {
                device = mDefaultOutputDevice;
            }
            if (device == AUDIO_DEVICE_NONE) {
                ALOGE("getDeviceForStrategy() no device found for STRATEGY_MEDIA");
            }

        } else {
            //AudioSystem::FORCE_SPEAKER for STRATEGY_MEDIA
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_SPEAKER;
        }

        if (isInCall()) {
            // when in call, get the device for Phone strategy
            device = getDeviceForStrategy(STRATEGY_PHONE, false /*fromCache*/);
        }

        } break;

    default:
        ALOGW("getDeviceForStrategy() unknown strategy: %d", strategy);
        break;
    }

    ALOGVV("getDeviceForStrategy() strategy %d, device %x", strategy, device);
    return device;
}

uint32_t AudioPolicyManager::setOutputDevice(audio_io_handle_t output,
                                             audio_devices_t device,
                                             bool force,
                                             int delayMs)

{
    ALOGV("setOutputDevice() output %d device %04x delayMs %d", output, device, delayMs);
    AudioOutputDescriptor *outputDesc = mOutputs.valueFor(output);
    AudioParameter param;
    uint32_t muteWaitMs = 0;

    if (outputDesc->isDuplicated()) {
        muteWaitMs = setOutputDevice(outputDesc->mOutput1->mId, device, force, delayMs);
        muteWaitMs += setOutputDevice(outputDesc->mOutput2->mId, device, force, delayMs);
        return muteWaitMs;
    }
    // no need to proceed if new device is not AUDIO_DEVICE_NONE and not supported by current
    // output profile
    if ((device != AUDIO_DEVICE_NONE) &&
            ((device & outputDesc->mProfile->mSupportedDevices) == 0)) {
        return 0;
    }

    // filter devices according to output selected
    device = (audio_devices_t)(device & outputDesc->mProfile->mSupportedDevices);

    audio_devices_t prevDevice = outputDesc->mDevice;

    ALOGV("setOutputDevice() prevDevice %04x", prevDevice);

    // Device Routing has not been triggered in the following scenario:
    // Start playback on HDMI/USB hs, pause it, unplug and plug HDMI
    //cable/usb hs, resume playback, music starts on speaker. To avoid
    //this, update mDevice even if device is 0 which triggers routing when
    // HDMI cable/usb hs is reconnected
    if (device != AUDIO_DEVICE_NONE ||
        prevDevice == AUDIO_DEVICE_OUT_AUX_DIGITAL ||
        prevDevice == AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET) {
        outputDesc->mDevice = device;
    }

    muteWaitMs = checkDeviceMuteStrategies(outputDesc, prevDevice, delayMs);

    // Do not change the routing if:
    //  - the requested device is AUDIO_DEVICE_NONE
    //  - the requested device is the same as current device and force is not specified.
    // Doing this check here allows the caller to call setOutputDevice() without conditions
    //Force a device switch when HDMI/USB headset is disconnected
    if ((device == AUDIO_DEVICE_NONE && !force) || ((device == prevDevice) && !force)) {
        ALOGV("setOutputDevice() setting same device %04x or null device for output %d", device, output);
        return muteWaitMs;
    }
    if (device == prevDevice) {
        ALOGV("setOutputDevice() Call routing with same device with zero delay ");
        delayMs = 0;
    }
    ALOGV("setOutputDevice() changing device:%x",device);
    // do the routing
    param.addInt(String8(AudioParameter::keyRouting), (int)device);
    mpClientInterface->setParameters(output, param.toString(), delayMs);

    // update stream volumes according to new device
    applyStreamVolumes(output, device, delayMs);


    return muteWaitMs;
}

audio_devices_t AudioPolicyManager::getDeviceForInputSource(int inputSource)
{
    uint32_t device = AUDIO_DEVICE_NONE;

    switch(inputSource) {
    case AUDIO_SOURCE_VOICE_UPLINK:
      if (mAvailableInputDevices & AUDIO_DEVICE_IN_VOICE_CALL) {
          device = AUDIO_DEVICE_IN_VOICE_CALL;
          break;
      }
      // FALL THROUGH

    case AUDIO_SOURCE_DEFAULT:
    case AUDIO_SOURCE_MIC:
    case AUDIO_SOURCE_VOICE_RECOGNITION:
    case AUDIO_SOURCE_HOTWORD:
        if (mForceUse[AudioSystem::FOR_RECORD] == AudioSystem::FORCE_BT_SCO &&
            mAvailableInputDevices & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            device = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_WIRED_HEADSET) {
            device = AUDIO_DEVICE_IN_WIRED_HEADSET;
#ifdef QCOM_ANC_HEADSET_ENABLED
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_ANC_HEADSET) {
            device = AUDIO_DEVICE_IN_ANC_HEADSET;
#endif
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET) {
            device = AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_BUILTIN_MIC) {
            device = AUDIO_DEVICE_IN_BUILTIN_MIC;
        }
        break;
    case AUDIO_SOURCE_VOICE_COMMUNICATION:
         device = AUDIO_DEVICE_IN_COMMUNICATION;
         break;
    case AUDIO_SOURCE_CAMCORDER:
        if (mAvailableInputDevices & AUDIO_DEVICE_IN_BACK_MIC) {
            device = AUDIO_DEVICE_IN_BACK_MIC;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            device = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_WIRED_HEADSET) {
            device = AUDIO_DEVICE_IN_WIRED_HEADSET;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_AUX_DIGITAL) {
            device = AUDIO_DEVICE_IN_AUX_DIGITAL;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_BUILTIN_MIC) {
            device = AUDIO_DEVICE_IN_BUILTIN_MIC;
        }
        break;
    case AUDIO_SOURCE_VOICE_DOWNLINK:
    case AUDIO_SOURCE_VOICE_CALL:
        if (mAvailableInputDevices & AUDIO_DEVICE_IN_VOICE_CALL) {
            device = AUDIO_DEVICE_IN_VOICE_CALL;
        }
        break;
    case AUDIO_SOURCE_REMOTE_SUBMIX:
        if (mAvailableInputDevices & AUDIO_DEVICE_IN_REMOTE_SUBMIX) {
            device = AUDIO_DEVICE_IN_REMOTE_SUBMIX;
        }
        break;
#ifdef QCOM_FM_ENABLED
   case AUDIO_SOURCE_FM_RX:
        device = AUDIO_DEVICE_IN_FM_RX;
        break;
    case AUDIO_SOURCE_FM_RX_A2DP:
        device = AUDIO_DEVICE_IN_FM_RX_A2DP;
        break;
#endif
    default:
        ALOGW("getDeviceForInputSource() invalid input source %d", inputSource);
        break;
    }
    ALOGV("getDeviceForInputSource()input source %d, device %08x", inputSource, device);
    return device;
}




audio_devices_t AudioPolicyManager::getDeviceForVolume(audio_devices_t device)
{
    if (device == AUDIO_DEVICE_NONE) {
        // this happens when forcing a route update and no track is active on an output.
        // In this case the returned category is not important.
        device =  AUDIO_DEVICE_OUT_SPEAKER;
    } else if (AudioSystem::popCount(device) > 1) {
        // Multiple device selection is either:
        //  - speaker + one other device: give priority to speaker in this case.
        //  - one A2DP device + another device: happens with duplicated output. In this case
        // retain the device on the A2DP output as the other must not correspond to an active
        // selection if not the speaker.
        if (device & AUDIO_DEVICE_OUT_SPEAKER) {
            device = AUDIO_DEVICE_OUT_SPEAKER;
        } else if ((device & AUDIO_DEVICE_OUT_WIRED_HEADSET) != 0) {
            device = AUDIO_DEVICE_OUT_WIRED_HEADSET;
        } else if ((device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) != 0) {
            device = AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
        } else {
            device = (audio_devices_t)(device & AUDIO_DEVICE_OUT_ALL_A2DP);
        }
    }

    ALOGW_IF(AudioSystem::popCount(device) != 1,
            "getDeviceForVolume() invalid device combination: %08x",
            device);

    return device;
}

AudioPolicyManager::device_category AudioPolicyManager::getDeviceCategory(audio_devices_t device)
{
    switch(getDeviceForVolume(device)) {
        case AUDIO_DEVICE_OUT_EARPIECE:
            return DEVICE_CATEGORY_EARPIECE;
        case AUDIO_DEVICE_OUT_WIRED_HEADSET:
        case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
#ifdef QCOM_ANC_HEADSET_ENABLED
        case AUDIO_DEVICE_OUT_ANC_HEADSET:
        case AUDIO_DEVICE_OUT_ANC_HEADPHONE:
#endif
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
        case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP:
        case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES:
#ifdef QCOM_FM_ENABLED
        case AUDIO_DEVICE_OUT_FM:
#endif
            return DEVICE_CATEGORY_HEADSET;
        case AUDIO_DEVICE_OUT_SPEAKER:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
        case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER:
        case AUDIO_DEVICE_OUT_AUX_DIGITAL:
        case AUDIO_DEVICE_OUT_USB_ACCESSORY:
        case AUDIO_DEVICE_OUT_USB_DEVICE:
        case AUDIO_DEVICE_OUT_REMOTE_SUBMIX:
#ifdef QCOM_PROXY_DEVICE_ENABLED
        case AUDIO_DEVICE_OUT_PROXY:
#endif
        default:
            return DEVICE_CATEGORY_SPEAKER;
    }
}

bool AudioPolicyManager::isDirectOutput(audio_io_handle_t output) {
    for (size_t i = 0; i < mOutputs.size(); i++) {
        audio_io_handle_t curOutput = mOutputs.keyAt(i);
        AudioOutputDescriptor *desc = mOutputs.valueAt(i);
        if ((curOutput == output) && (desc->mFlags & AUDIO_OUTPUT_FLAG_DIRECT)) {
            return true;
        }
    }
    return false;
}

status_t AudioPolicyManager::checkAndSetVolume(int stream,
                                                   int index,
                                                   audio_io_handle_t output,
                                                   audio_devices_t device,
                                                   int delayMs,
                                                   bool force)
{
    // do not change actual stream volume if the stream is muted
    if (mOutputs.valueFor(output)->mMuteCount[stream] != 0) {
        ALOGVV("checkAndSetVolume() stream %d muted count %d",
              stream, mOutputs.valueFor(output)->mMuteCount[stream]);
        return NO_ERROR;
    }

    // do not change in call volume if bluetooth is connected and vice versa
    if ((stream == AudioSystem::VOICE_CALL && mForceUse[AudioSystem::FOR_COMMUNICATION] == AudioSystem::FORCE_BT_SCO) ||
        (stream == AudioSystem::BLUETOOTH_SCO && mForceUse[AudioSystem::FOR_COMMUNICATION] != AudioSystem::FORCE_BT_SCO)) {
        ALOGV("checkAndSetVolume() cannot set stream %d volume with force use = %d for comm",
             stream, mForceUse[AudioSystem::FOR_COMMUNICATION]);
        return INVALID_OPERATION;
    }

    float volume = computeVolume(stream, index, output, device);
    // We actually change the volume if:
    // - the float value returned by computeVolume() changed
    // - the force flag is set
    if (volume != mOutputs.valueFor(output)->mCurVolume[stream] ||
            force) {
        mOutputs.valueFor(output)->mCurVolume[stream] = volume;
        ALOGVV("checkAndSetVolume() for output %d stream %d, volume %f, delay %d", output, stream, volume, delayMs);
        // Force VOICE_CALL to track BLUETOOTH_SCO stream volume when bluetooth audio is
        // enabled
        if (stream == AudioSystem::BLUETOOTH_SCO) {
            mpClientInterface->setStreamVolume(AudioSystem::VOICE_CALL, volume, output, delayMs);
#ifdef QCOM_FM_ENABLED
        } else if (stream == AudioSystem::MUSIC) {
            float fmVolume = -1.0;
            fmVolume = computeVolume(stream, index, output, device);
            if (fmVolume >= 0) {
                if(output == mPrimaryOutput) {
                    AudioParameter param = AudioParameter();
                    param.addFloat(String8(AudioParameter::keyFmVolume), fmVolume);
                    ALOGV("checkAndSetVolume setParameters fm_volume, volume=:%f delay=:%d",fmVolume,delayMs*2);
                    //Double delayMs to avoid sound burst while device switch.
                    mpClientInterface->setParameters(mPrimaryOutput, param.toString(), delayMs*2);
                }
                else if(mHasA2dp && output == getA2dpOutput()) {
                    mpClientInterface->setStreamVolume((AudioSystem::stream_type)stream, volume, output, delayMs);
                }
            }
            //If you return here, only FM volume would be handled. To handle Music volume as well, this shouldn't return.
            //return NO_ERROR;
#endif
        }
        mpClientInterface->setStreamVolume((AudioSystem::stream_type)stream, volume, output, delayMs);
    }

    if (stream == AudioSystem::VOICE_CALL ||
        stream == AudioSystem::BLUETOOTH_SCO) {
        float voiceVolume;
        // Force voice volume to max for bluetooth SCO as volume is managed by the headset
        if (stream == AudioSystem::VOICE_CALL) {
            voiceVolume = (float)index/(float)mStreams[stream].mIndexMax;
        } else {
            voiceVolume = 1.0;
        }

        voiceVolume = (float)index/(float)mStreams[stream].mIndexMax;

        // Force voice volume to max when Vgs is set for bluetooth SCO as volume is managed by the headset
        if (stream == AudioSystem::BLUETOOTH_SCO) {
            String8 key ("bt_headset_vgs");
            mpClientInterface->getParameters(output,key);
            AudioParameter result(mpClientInterface->getParameters(0,key));
            int value;
            if (result.getInt(String8("isVGS"),value) == NO_ERROR) {
                ALOGV("Use BT-SCO Voice Volume");
                voiceVolume = 1.0;
            }
        }

        if (voiceVolume != mLastVoiceVolume && (output == mPrimaryOutput || isDirectOutput(output))) {
            mpClientInterface->setVoiceVolume(voiceVolume, delayMs);
            mLastVoiceVolume = voiceVolume;
        }
    }

    return NO_ERROR;
}


void AudioPolicyManager::checkA2dpSuspend()
{

    // suspend A2DP output if:
    //      (NOT already suspended) &&
    //      ((SCO device is connected &&
    //       (forced usage for communication || for record is SCO))) ||
    //      (phone state is ringing || in call)
    //
    // restore A2DP output if:
    //      (Already suspended) &&
    //      ((SCO device is NOT connected ||
    //       (forced usage NOT for communication && NOT for record is SCO))) &&
    //      (phone state is NOT ringing && NOT in call)
    //
    if (mA2dpSuspended) {
        if (((mScoDeviceAddress == "") ||
             ((mForceUse[AudioSystem::FOR_COMMUNICATION] != AudioSystem::FORCE_BT_SCO) &&
              (mForceUse[AudioSystem::FOR_RECORD] != AudioSystem::FORCE_BT_SCO))) ||
             ((mPhoneState != AudioSystem::MODE_IN_CALL) &&
              (mPhoneState != AudioSystem::MODE_RINGTONE))) {

            mA2dpSuspended = false;
        }
    } else {
        if (((mScoDeviceAddress != "") &&
             ((mForceUse[AudioSystem::FOR_COMMUNICATION] == AudioSystem::FORCE_BT_SCO) ||
              (mForceUse[AudioSystem::FOR_RECORD] == AudioSystem::FORCE_BT_SCO))) &&
             ((mPhoneState == AudioSystem::MODE_IN_CALL) ||
              (mPhoneState == AudioSystem::MODE_RINGTONE))) {

            mA2dpSuspended = true;
        }
    }
}

audio_io_handle_t AudioPolicyManager::getA2dpOutput()
{
    return 0;
}

//private function, no changes from AudioPolicyManagerBase
void AudioPolicyManager::handleNotificationRoutingForStream(AudioSystem::stream_type stream) {
    switch(stream) {
    case AudioSystem::MUSIC:
        checkOutputForStrategy(STRATEGY_SONIFICATION_RESPECTFUL);
        updateDevicesAndOutputs();
        break;
    default:
        break;
    }
}


AudioPolicyManagerBase::IOProfile *AudioPolicyManager::getProfileForDirectOutput(
                                                               audio_devices_t device,
                                                               uint32_t samplingRate,
                                                               uint32_t format,
                                                               uint32_t channelMask,
                                                               audio_output_flags_t flags)
{
#ifdef QCOM_OUTPUT_FLAGS_ENABLED
    if( !((flags & AUDIO_OUTPUT_FLAG_LPA)   ||
          (flags & AUDIO_OUTPUT_FLAG_TUNNEL)||
          (flags & AUDIO_OUTPUT_FLAG_VOIP_RX)) )
        flags = AUDIO_OUTPUT_FLAG_DIRECT;
#endif
    for (size_t i = 0; i < mHwModules.size(); i++) {
        if (mHwModules[i]->mHandle == 0) {
            continue;
        }
        for (size_t j = 0; j < mHwModules[i]->mOutputProfiles.size(); j++) {
           AudioPolicyManagerBase::IOProfile *profile = mHwModules[i]->mOutputProfiles[j];
           if (isCompatibleProfile(profile, device, samplingRate, format,
                                           channelMask,
                                           flags)) {
               if (mAvailableOutputDevices & profile->mSupportedDevices) {
                   return mHwModules[i]->mOutputProfiles[j];
               }
           }
        }
    }
    return 0;
}

bool AudioPolicyManager::isCompatibleProfile(AudioPolicyManagerBase::IOProfile *profile,
                                             audio_devices_t device,
                                             uint32_t samplingRate,
                                             uint32_t format,
                                             uint32_t channelMask,
                                            audio_output_flags_t flags)
{
    if ((profile->mSupportedDevices & device) != device) {
        return false;
    }
    if (profile->mFlags != flags) {
        return false;
    }
    if (samplingRate != 0) {
        size_t i;
        for (i = 0; i < profile->mSamplingRates.size(); i++)
        {
            if (profile->mSamplingRates[i] == samplingRate) {
                break;
            }
        }
        if (i == profile->mSamplingRates.size()) {
            return false;
        }
    }
    if (format != 0) {
        size_t i;
        for (i = 0; i < profile->mFormats.size(); i++)
        {
            if (profile->mFormats[i] == format) {
                break;
            }
        }
        if (i == profile->mFormats.size()) {
            return false;
        }
    }
    if (channelMask != 0) {
        size_t i;
       for (i = 0; i < profile->mChannelMasks.size(); i++)
        {
            if (profile->mChannelMasks[i] == channelMask) {
                break;
            }
        }
        if (i == profile->mChannelMasks.size()) {
            return false;
        }
    }
    ALOGD(" profile found: device %x, flags %x, samplingrate %d,\
            format %x, channelMask %d",
            device, flags, samplingRate, format, channelMask);
    return true;
}

float AudioPolicyManager::computeVolume(int stream,
                                            int index,
                                            audio_io_handle_t output,
                                            audio_devices_t device)
{
    float volume = 1.0;
    AudioOutputDescriptor *outputDesc = mOutputs.valueFor(output);
    StreamDescriptor &streamDesc = mStreams[stream];

    if (device == AUDIO_DEVICE_NONE) {
        device = outputDesc->device();
    }

    // if volume is not 0 (not muted), force media volume to max on digital output
    if (stream == AudioSystem::MUSIC &&
        index != mStreams[stream].mIndexMin &&
        (device == AUDIO_DEVICE_OUT_AUX_DIGITAL ||
         device == AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET ||
         device == AUDIO_DEVICE_OUT_USB_ACCESSORY ||
         device == AUDIO_DEVICE_OUT_USB_DEVICE ||
         device == AUDIO_DEVICE_OUT_PROXY)) {
        return 1.0;
    }
    return AudioPolicyManagerBase::computeVolume(stream, index, output, device);
}

bool AudioPolicyManager::platform_is_Fusion3()
{
    char platform[128], baseband[128], baseband_arch[128];
    property_get("ro.board.platform", platform, "");
    property_get("ro.baseband", baseband, "");
    property_get("ro.baseband.arch", baseband_arch, "");
    if (!strcmp("msm8960", platform) && (!strcmp("mdm", baseband) || !strcmp("mdm", baseband_arch)))
        return true;
    else
        return false;
}

bool AudioPolicyManager::isTunnelOutputEnabled()
{
    for (int i = mOutputs.size() -1; i >= 0; i--) {
        if(mOutputs.valueAt(i)->mFlags &((audio_output_flags_t) AUDIO_OUTPUT_FLAG_TUNNEL))
            return true;
    }
    return false;
}

extern "C" AudioPolicyInterface* createAudioPolicyManager(AudioPolicyClientInterface *clientInterface)
{
    return new AudioPolicyManager(clientInterface);
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface *interface)
{
    delete interface;
}


}; // namespace android
