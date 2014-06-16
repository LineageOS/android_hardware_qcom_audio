/*
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 * Not a contribution.
 *
 * Copyright (C) 2009 The Android Open Source Project
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

#define LOG_TAG "AudioPolicyManager"
//#define LOG_NDEBUG 0

//#define VERY_VERBOSE_LOGGING
#ifdef VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

// A device mask for all audio output devices that are considered "remote" when evaluating
// active output devices in isStreamActiveRemotely()
#define APM_AUDIO_OUT_DEVICE_REMOTE_ALL  AUDIO_DEVICE_OUT_REMOTE_SUBMIX

#include <utils/Log.h>
#include "AudioPolicyManager.h"
#include <hardware/audio_effect.h>
#include <hardware/audio.h>
#include <math.h>
#include <hardware_legacy/audio_policy_conf.h>
#include <cutils/properties.h>

namespace android_audio_legacy {

// ----------------------------------------------------------------------------
// AudioPolicyInterface implementation
// ----------------------------------------------------------------------------
const char* AudioPolicyManager::HDMI_SPKR_STR = "hdmi_spkr";
int AudioPolicyManager::mvoice_call_state = 0;

status_t AudioPolicyManager::setDeviceConnectionState(audio_devices_t device,
                                                      AudioSystem::device_connection_state state,
                                                      const char *device_address)
{
    SortedVector <audio_io_handle_t> outputs;

    ALOGD("setDeviceConnectionState() device: %x, state %d, address %s", device, state, device_address);

    // connect/disconnect only 1 device at a time
    if (!audio_is_output_device(device) && !audio_is_input_device(device)) return BAD_VALUE;

    if (strlen(device_address) >= MAX_DEVICE_ADDRESS_LEN) {
        ALOGE("setDeviceConnectionState() invalid address: %s", device_address);
        return BAD_VALUE;
    }

    // handle output devices
    if (audio_is_output_device(device)) {

        if (!mHasA2dp && audio_is_a2dp_device(device)) {
            ALOGE("setDeviceConnectionState() invalid A2DP device: %x", device);
            return BAD_VALUE;
        }
        if (!mHasUsb && audio_is_usb_device(device)) {
            ALOGE("setDeviceConnectionState() invalid USB audio device: %x", device);
            return BAD_VALUE;
        }
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
#ifdef AUDIO_EXTN_HDMI_SPK_ENABLED
                if ((popcount(device) == 1) && (device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
                   if (!strncmp(device_address, HDMI_SPKR_STR, MAX_DEVICE_ADDRESS_LEN)) {
                        mHdmiAudioDisabled = false;
                    } else {
                        mHdmiAudioEvent = true;
                    }
                }
#endif
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

#ifdef AUDIO_EXTN_HDMI_SPK_ENABLED
            if ((popcount(device) == 1) && (device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
                if (!strncmp(device_address, HDMI_SPKR_STR, MAX_DEVICE_ADDRESS_LEN)) {
                    mHdmiAudioDisabled = false;
                } else {
                    mHdmiAudioEvent = true;
                }
                if (mHdmiAudioDisabled || !mHdmiAudioEvent) {
                    mAvailableOutputDevices = (audio_devices_t)(mAvailableOutputDevices & ~device);
                }
            }
#endif
            if (!outputs.isEmpty()) {
                String8 paramStr;
                if (mHasA2dp && audio_is_a2dp_device(device)) {
                    // handle A2DP device connection
                    AudioParameter param;
                    param.add(String8(AUDIO_PARAMETER_A2DP_SINK_ADDRESS), String8(device_address));
                    paramStr = param.toString();
                    mA2dpDeviceAddress = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                    mA2dpSuspended = false;
                } else if (audio_is_bluetooth_sco_device(device)) {
                    // handle SCO device connection
                    mScoDeviceAddress = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                } else if (mHasUsb && audio_is_usb_device(device)) {
                    // handle USB device connection
                    mUsbCardAndDevice = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                    paramStr = mUsbCardAndDevice;
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
#ifdef AUDIO_EXTN_HDMI_SPK_ENABLED
                if ((popcount(device) == 1) && (device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
                    if (!strncmp(device_address, HDMI_SPKR_STR, MAX_DEVICE_ADDRESS_LEN)) {
                        mHdmiAudioDisabled = true;
                    } else {
                        mHdmiAudioEvent = false;
                    }
                }
#endif
                ALOGW("setDeviceConnectionState() device not connected: %x", device);
                return INVALID_OPERATION;
            }

            ALOGV("setDeviceConnectionState() disconnecting device %x", device);
            // remove device from available output devices
            mAvailableOutputDevices = (audio_devices_t)(mAvailableOutputDevices & ~device);

#ifdef AUDIO_EXTN_HDMI_SPK_ENABLED
            if ((popcount(device) == 1) && (device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
                if (!strncmp(device_address, HDMI_SPKR_STR, MAX_DEVICE_ADDRESS_LEN)) {
                    mHdmiAudioDisabled = true;
                } else {
                    mHdmiAudioEvent = false;
                }
            }
#endif
            checkOutputsForDevice(device, state, outputs);
            if (mHasA2dp && audio_is_a2dp_device(device)) {
                // handle A2DP device disconnection
                mA2dpDeviceAddress = "";
                mA2dpSuspended = false;
            } else if (audio_is_bluetooth_sco_device(device)) {
                // handle SCO device disconnection
                mScoDeviceAddress = "";
            } else if (mHasUsb && audio_is_usb_device(device)) {
                // handle USB device disconnection
                mUsbCardAndDevice = "";
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
        audio_devices_t newDevice = getNewDevice(mPrimaryOutput, false /*fromCache*/);
#ifdef AUDIO_EXTN_FM_ENABLED
        if(device == AUDIO_DEVICE_OUT_FM) {
            if (state == AudioSystem::DEVICE_STATE_AVAILABLE) {
                mOutputs.valueFor(mPrimaryOutput)->changeRefCount(AudioSystem::MUSIC, 1);
                newDevice = (audio_devices_t)(getNewDevice(mPrimaryOutput, false) | AUDIO_DEVICE_OUT_FM);
            } else {
                mOutputs.valueFor(mPrimaryOutput)->changeRefCount(AudioSystem::MUSIC, -1);
            }

            AudioParameter param = AudioParameter();
            param.addInt(String8("handle_fm"), (int)newDevice);
            ALOGV("setDeviceConnectionState() setParameters handle_fm");
            mpClientInterface->setParameters(mPrimaryOutput, param.toString());
        }
#endif
        for (size_t i = 0; i < mOutputs.size(); i++) {
            // do not force device change on duplicated output because if device is 0, it will
            // also force a device 0 for the two outputs it is duplicated to which may override
            // a valid device selection on those outputs.
            audio_devices_t cachedDevice = getNewDevice(mOutputs.keyAt(i), true /*fromCache*/);
            AudioOutputDescriptor *desc = mOutputs.valueFor(mOutputs.keyAt(i));
            if (cachedDevice == AUDIO_DEVICE_OUT_SPEAKER &&
                device == AUDIO_DEVICE_OUT_PROXY &&
                (desc->mFlags & AUDIO_OUTPUT_FLAG_FAST)) {
                    ALOGI("Avoid routing touch tone to spkr as proxy is being disconnected");
                    break;
            }
            setOutputDevice(mOutputs.keyAt(i),
                            cachedDevice,
                            !mOutputs.valueAt(i)->isDuplicated(),
                            0);
        }

        if (device == AUDIO_DEVICE_OUT_WIRED_HEADSET) {
            device = AUDIO_DEVICE_IN_WIRED_HEADSET;
        } else if (device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO ||
                   device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET ||
                   device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            device = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else if(device == AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET){
            device = AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET;
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

void AudioPolicyManager::setForceUse(AudioSystem::force_use usage, AudioSystem::forced_config config)
{
    ALOGD("setForceUse() usage %d, config %d, mPhoneState %d", usage, config, mPhoneState);

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
#ifdef AUDIO_EXTN_FM_ENABLED
            config != AudioSystem::FORCE_SPEAKER &&
#endif
            config != AudioSystem::FORCE_WIRED_ACCESSORY &&
            config != AudioSystem::FORCE_ANALOG_DOCK &&
            config != AudioSystem::FORCE_DIGITAL_DOCK && config != AudioSystem::FORCE_NONE &&
            config != AudioSystem::FORCE_NO_BT_A2DP) {
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

audio_io_handle_t AudioPolicyManager::getInput(int inputSource,
                                    uint32_t samplingRate,
                                    uint32_t format,
                                    uint32_t channelMask,
                                    AudioSystem::audio_in_acoustics acoustics)
{
    audio_io_handle_t input = 0;
    audio_devices_t device = getDeviceForInputSource(inputSource);

    ALOGD("getInput() inputSource %d, samplingRate %d, format %d, channelMask %x, acoustics %x",
          inputSource, samplingRate, format, channelMask, acoustics);

    if (device == AUDIO_DEVICE_NONE) {
        ALOGW("getInput() could not find device for inputSource %d", inputSource);
        return 0;
    }

#ifdef VOICE_CONCURRENCY

    char propValue[PROPERTY_VALUE_MAX];
    bool prop_rec_enabled=false, prop_voip_enabled = false;

    if(property_get("voice.record.conc.disabled", propValue, NULL)) {
        prop_rec_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if(property_get("voice.voip.conc.disabled", propValue, NULL)) {
        prop_voip_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if (prop_rec_enabled && mvoice_call_state) {
         //check if voice call is active  / running in background
         //some of VoIP apps(like SIP2SIP call) supports resume of VoIP call when call in progress
         //Need to block input request
        if((AudioSystem::MODE_IN_CALL == mPhoneState) ||
           ((AudioSystem::MODE_IN_CALL == mPrevPhoneState) &&
             (AudioSystem::MODE_IN_COMMUNICATION == mPhoneState)))
        {
            switch(inputSource) {
                case AUDIO_SOURCE_VOICE_UPLINK:
                case AUDIO_SOURCE_VOICE_DOWNLINK:
                case AUDIO_SOURCE_VOICE_CALL:
                    ALOGD("Creating input during incall mode for inputSource: %d ",inputSource);
                break;

                case AUDIO_SOURCE_VOICE_COMMUNICATION:
                    if(prop_voip_enabled) {
                       ALOGD("BLOCKING VoIP request during incall mode for inputSource: %d ",inputSource);
                       return 0;
                    }
                break;
                default:
                    ALOGD("BLOCKING input during incall mode for inputSource: %d ",inputSource);
                return 0;
            }
        }
    }//check for VoIP flag
    else if(prop_voip_enabled && mvoice_call_state) {
         //check if voice call is active  / running in background
         //some of VoIP apps(like SIP2SIP call) supports resume of VoIP call when call in progress
         //Need to block input request
        if((AudioSystem::MODE_IN_CALL == mPhoneState) ||
           ((AudioSystem::MODE_IN_CALL == mPrevPhoneState) &&
             (AudioSystem::MODE_IN_COMMUNICATION == mPhoneState)))
        {
            if(inputSource == AUDIO_SOURCE_VOICE_COMMUNICATION) {
                ALOGD("BLOCKING VoIP request during incall mode for inputSource: %d ",inputSource);
                return 0;
            }
        }
    }

#endif
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
    ALOGD("getInput() returns input %d", input);

    return input;
}

AudioPolicyManager::routing_strategy AudioPolicyManager::getStrategy(AudioSystem::stream_type stream)
{
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
#ifdef AUDIO_EXTN_INCALL_MUSIC_ENABLED
        case AudioSystem::INCALL_MUSIC:
#endif
#ifdef QCOM_INCALL_MUSIC_ENABLED
        case AudioSystem::INCALL_MUSIC:
#endif
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
            if (mHasA2dp && !isInCall() &&
                    (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                    (getA2dpOutput() != 0) && !mA2dpSuspended) {
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
                if (device) break;
            }
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
            if (device) break;
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET;
            if (device) break;
            if (mPhoneState != AudioSystem::MODE_IN_CALL) {
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_ACCESSORY;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_DEVICE;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_AUX_DIGITAL;
                if (device) break;
            }

            // Allow voice call on USB ANLG DOCK headset
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
            if (mHasA2dp && !isInCall() &&
                    (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                    (getA2dpOutput() != 0) && !mA2dpSuspended) {
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
                if (device) break;
            }
            if (mPhoneState != AudioSystem::MODE_IN_CALL) {
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_ACCESSORY;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_DEVICE;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET;
                if (device) break;
                device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_AUX_DIGITAL;
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
                // FIXME: Why do need to replace with speaker? If voice call is active
                // We should use device from STRATEGY_PHONE
#ifdef AUDIO_EXTN_FM_ENABLED
        if (mAvailableOutputDevices & AUDIO_DEVICE_OUT_FM) {
            if (mForceUse[AudioSystem::FOR_MEDIA] == AudioSystem::FORCE_SPEAKER) {
                device = AUDIO_DEVICE_OUT_SPEAKER;
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

        if (isInCall() && (device == AUDIO_DEVICE_NONE)) {
            // when in call, get the device for Phone strategy
            device = getDeviceForStrategy(STRATEGY_PHONE, false /*fromCache*/);
            break;
        }
#ifdef AUDIO_EXTN_FM_ENABLED
        if (mForceUse[AudioSystem::FOR_MEDIA] == AudioSystem::FORCE_SPEAKER) {
            device = AUDIO_DEVICE_OUT_SPEAKER;
            break;
        }
#endif

        if (strategy != STRATEGY_SONIFICATION) {
            // no sonification on remote submix (e.g. WFD)
            device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_REMOTE_SUBMIX;
        }
        if ((device2 == AUDIO_DEVICE_NONE) &&
                mHasA2dp && (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                (getA2dpOutput() != 0) && !mA2dpSuspended) {
            device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP;
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
        if (device2 == AUDIO_DEVICE_NONE) {
            device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_ACCESSORY;
        }
        if (device2 == AUDIO_DEVICE_NONE) {
            device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_DEVICE;
        }
        if (device2 == AUDIO_DEVICE_NONE) {
            device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET;
        }
        if ((strategy != STRATEGY_SONIFICATION) && (device == AUDIO_DEVICE_NONE)
             && (device2 == AUDIO_DEVICE_NONE)) {
            // no sonification on aux digital (e.g. HDMI)
            device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_AUX_DIGITAL;
        }
        if ((device2 == AUDIO_DEVICE_NONE) &&
                (mForceUse[AudioSystem::FOR_DOCK] == AudioSystem::FORCE_ANALOG_DOCK)
                && (strategy != STRATEGY_SONIFICATION)) {
            device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET;
        }
#ifdef AUDIO_EXTN_FM_ENABLED
            if ((strategy != STRATEGY_SONIFICATION) && (device == AUDIO_DEVICE_NONE)
                 && (device2 == AUDIO_DEVICE_NONE)) {
                device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_FM_TX;
            }
#endif
#ifdef AUDIO_EXTN_AFE_PROXY_ENABLED
            if ((strategy != STRATEGY_SONIFICATION) && (device == AUDIO_DEVICE_NONE)
                 && (device2 == AUDIO_DEVICE_NONE)) {
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
        if (device) break;
        device = mDefaultOutputDevice;
        if (device == AUDIO_DEVICE_NONE) {
            ALOGE("getDeviceForStrategy() no device found for STRATEGY_MEDIA");
        }
        } break;

    default:
        ALOGW("getDeviceForStrategy() unknown strategy: %d", strategy);
        break;
    }

    ALOGVV("getDeviceForStrategy() strategy %d, device %x", strategy, device);
    return device;
}

audio_devices_t AudioPolicyManager::getDeviceForInputSource(int inputSource)
{
    uint32_t device = AUDIO_DEVICE_NONE;

    switch (inputSource) {
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
    case AUDIO_SOURCE_VOICE_COMMUNICATION:
        if (mForceUse[AudioSystem::FOR_RECORD] == AudioSystem::FORCE_BT_SCO &&
            mAvailableInputDevices & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            device = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_WIRED_HEADSET) {
            device = AUDIO_DEVICE_IN_WIRED_HEADSET;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET) {
            device = AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_BUILTIN_MIC) {
            device = AUDIO_DEVICE_IN_BUILTIN_MIC;
        }
        break;
    case AUDIO_SOURCE_CAMCORDER:
        if (mAvailableInputDevices & AUDIO_DEVICE_IN_BACK_MIC) {
            device = AUDIO_DEVICE_IN_BACK_MIC;
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
#ifdef AUDIO_EXTN_FM_ENABLED
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

AudioPolicyManager::device_category AudioPolicyManager::getDeviceCategory(audio_devices_t device)
{
    switch(getDeviceForVolume(device)) {
        case AUDIO_DEVICE_OUT_EARPIECE:
            return DEVICE_CATEGORY_EARPIECE;
        case AUDIO_DEVICE_OUT_WIRED_HEADSET:
        case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
        case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP:
        case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES:
#ifdef AUDIO_EXTN_FM_ENABLED
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
#ifdef AUDIO_EXTN_AFE_PROXY_ENABLED
        case AUDIO_DEVICE_OUT_PROXY:
#endif
        default:
            return DEVICE_CATEGORY_SPEAKER;
    }
}

status_t AudioPolicyManager::checkAndSetVolume(int stream,
                                               int index,
                                               audio_io_handle_t output,
                                               audio_devices_t device,
                                               int delayMs,
                                               bool force)
{
    ALOGV("checkAndSetVolume: index %d output %d device %x", index, output, device);
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
        ALOGV("checkAndSetVolume() for output %d stream %d, volume %f, delay %d", output, stream, volume, delayMs);
        // Force VOICE_CALL to track BLUETOOTH_SCO stream volume when bluetooth audio is
        // enabled
        if (stream == AudioSystem::BLUETOOTH_SCO) {
            mpClientInterface->setStreamVolume(AudioSystem::VOICE_CALL, volume, output, delayMs);
#ifdef AUDIO_EXTN_FM_ENABLED
        } else if (stream == AudioSystem::MUSIC &&
                   output == mPrimaryOutput) {
            float fmVolume = -1.0;
            fmVolume = computeVolume(stream, index, output, device);
            if (fmVolume >= 0) {
                    AudioParameter param = AudioParameter();
                    param.addFloat(String8("fm_volume"), fmVolume);
                    ALOGV("checkAndSetVolume setParameters fm_volume, volume=:%f delay=:%d",fmVolume,delayMs*2);
                    //Double delayMs to avoid sound burst while device switch.
                    mpClientInterface->setParameters(mPrimaryOutput, param.toString(), delayMs*2);
            }
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

        if (voiceVolume != mLastVoiceVolume && output == mPrimaryOutput) {
            mpClientInterface->setVoiceVolume(voiceVolume, delayMs);
            mLastVoiceVolume = voiceVolume;
        }
    }

    return NO_ERROR;
}


float AudioPolicyManager::computeVolume(int stream,
                                        int index,
                                        audio_io_handle_t output,
                                        audio_devices_t device)
{
    float volume = 1.0;
    AudioOutputDescriptor *outputDesc = mOutputs.valueFor(output);

    if (device == AUDIO_DEVICE_NONE) {
        device = outputDesc->device();
    }

    // if volume is not 0 (not muted), force media volume to max on digital output
    if (stream == AudioSystem::MUSIC &&
        index != mStreams[stream].mIndexMin &&
        (device == AUDIO_DEVICE_OUT_AUX_DIGITAL ||
         device == AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET ||
         device == AUDIO_DEVICE_OUT_USB_ACCESSORY ||
#ifdef AUDIO_EXTN_AFE_PROXY_ENABLED
         device == AUDIO_DEVICE_OUT_PROXY ||
#endif
         device == AUDIO_DEVICE_OUT_USB_DEVICE )) {
        return 1.0;
    }
#ifdef AUDIO_EXTN_INCALL_MUSIC_ENABLED
    if (stream == AudioSystem::INCALL_MUSIC) {
        return 1.0;
    }
#endif
    return AudioPolicyManagerBase::computeVolume(stream, index, output, device);
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
    IOProfile *profile = NULL;

#ifdef VOICE_CONCURRENCY
    char propValue[PROPERTY_VALUE_MAX];
    bool prop_play_enabled=false, prop_voip_enabled = false;

    if(property_get("voice.playback.conc.disabled", propValue, NULL)) {
       prop_play_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if(property_get("voice.voip.conc.disabled", propValue, NULL)) {
       prop_voip_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if (prop_play_enabled && mvoice_call_state) {
        //check if voice call is active  / running in background
        if((AudioSystem::MODE_IN_CALL == mPhoneState) ||
             ((AudioSystem::MODE_IN_CALL == mPrevPhoneState)
                && (AudioSystem::MODE_IN_COMMUNICATION == mPhoneState)))
        {
            if(AUDIO_OUTPUT_FLAG_VOIP_RX  & flags) {
                if(prop_voip_enabled) {
                    ALOGD(" IN call mode returing no output .. for VoIP usecase flags: %x ", flags );
                   // flags = (AudioSystem::output_flags)AUDIO_OUTPUT_FLAG_FAST;
                   return 0;
                }
            }
            else {
                ALOGD(" IN call mode adding ULL flags .. flags: %x ", flags );
                flags = (AudioSystem::output_flags)AUDIO_OUTPUT_FLAG_FAST;
            }
        }
    } else if (prop_voip_enabled && mvoice_call_state) {
        //check if voice call is active  / running in background
        //some of VoIP apps(like SIP2SIP call) supports resume of VoIP call when call in progress
        //return only ULL ouput
        if((AudioSystem::MODE_IN_CALL == mPhoneState) ||
             ((AudioSystem::MODE_IN_CALL == mPrevPhoneState)
                && (AudioSystem::MODE_IN_COMMUNICATION == mPhoneState)))
        {
            if(AUDIO_OUTPUT_FLAG_VOIP_RX  & flags) {
                ALOGD(" IN call mode returing no output .. for VoIP usecase flags: %x ", flags );
               // flags = (AudioSystem::output_flags)AUDIO_OUTPUT_FLAG_FAST;
               return 0;
            }
        }
    }
#endif

#ifdef WFD_CONCURRENCY
    if ((mAvailableOutputDevices & AUDIO_DEVICE_OUT_PROXY)
          && (stream != AudioSystem::MUSIC)) {
        ALOGV(" WFD mode adding ULL flags for non music stream.. flags: %x ", flags );
        //For voip paths
        if(flags & AudioSystem::OUTPUT_FLAG_DIRECT)
            flags = AudioSystem::OUTPUT_FLAG_DIRECT;
        else //route every thing else to ULL path
            flags = (AudioSystem::output_flags)AUDIO_OUTPUT_FLAG_FAST;
    }
#endif

    ALOGD(" getOutput() device %d, stream %d, samplingRate %d, format %x, channelMask %x, flags %x ",
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

    if ((format == AudioSystem::PCM_16_BIT) &&(AudioSystem::popCount(channelMask) > 2)) {
        ALOGV("owerwrite flag(%x) for PCM16 multi-channel(CM:%x) playback", flags ,channelMask);
        flags = (AudioSystem::output_flags)AUDIO_OUTPUT_FLAG_DIRECT;
    }

    // Do not allow offloading if one non offloadable effect is enabled. This prevents from
    // creating an offloaded track and tearing it down immediately after start when audioflinger
    // detects there is an active non offloadable effect.
    // FIXME: We should check the audio session here but we do not have it in this context.
    // This may prevent offloading in rare situations where effects are left active by apps
    // in the background.
    if ((((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) == 0) ||
            !isNonOffloadableEffectEnabled()) &&
            flags & AUDIO_OUTPUT_FLAG_DIRECT) {
        profile = getProfileForDirectOutput(device,
                                           samplingRate,
                                           format,
                                           channelMask,
                                           (audio_output_flags_t)flags);
    }

    if (profile != NULL) {
        AudioOutputDescriptor *outputDesc = NULL;

#ifdef MULTIPLE_OFFLOAD_ENABLED
        bool multiOffloadEnabled = false;
        char value[PROPERTY_VALUE_MAX] = {0};
        property_get("audio.offload.multiple.enabled", value, NULL);
        if (atoi(value) || !strncmp("true", value, 4))
            multiOffloadEnabled = true;
        // if multiple concurrent offload decode is supported
        // do no check for reuse and also don't close previous output if its offload
        // previous output will be closed during track destruction
        if (multiOffloadEnabled)
            goto get_output__new_output_desc;
#endif
        for (size_t i = 0; i < mOutputs.size(); i++) {
            AudioOutputDescriptor *desc = mOutputs.valueAt(i);
            if (!desc->isDuplicated() && (profile == desc->mProfile)) {
                outputDesc = desc;
                // reuse direct output if currently open and configured with same parameters
                if ((samplingRate == outputDesc->mSamplingRate) &&
                        (format == outputDesc->mFormat) &&
                        (channelMask == outputDesc->mChannelMask)) {
                    outputDesc->mDirectOpenCount++;
                    ALOGD("getOutput() reusing direct output %d", mOutputs.keyAt(i));
                    return mOutputs.keyAt(i);
                }
            }
        }
        // close direct output if currently open and configured with different parameters
        if (outputDesc != NULL) {
            closeOutput(outputDesc->mId);
        }
get_output__new_output_desc:
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
        audio_io_handle_t srcOutput = getOutputForEffect();
        addOutput(output, outputDesc);
        audio_io_handle_t dstOutput = getOutputForEffect();
        if (dstOutput == output) {
            mpClientInterface->moveEffects(AUDIO_SESSION_OUTPUT_MIX, srcOutput, dstOutput);
        }
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
    ALOGW_IF((output == 0), "getOutput() could not find output for stream %d, samplingRate %d,"
            "format %d, channels %x, flags %x", stream, samplingRate, format, channelMask, flags);

    ALOGD("getOutput() returns output %d", output);

    return output;
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
    if ((isInCall()) && (outputDesc->mRefCount[stream] == 1)) {
        handleIncallSonification(stream, false, false);
    }

    if (outputDesc->mRefCount[stream] > 0) {
        // decrement usage count of this stream on the output
        outputDesc->changeRefCount(stream, -1);
        // store time at which the stream was stopped - see isStreamActive()
        if (outputDesc->mRefCount[stream] == 0) {
            outputDesc->mStopTime[stream] = systemTime();
            audio_devices_t newDevice = getNewDevice(output, false /*fromCache*/);
            // delay the device switch by twice the latency because stopOutput() is executed when
            // the track stop() command is received and at that time the audio track buffer can
            // still contain data that needs to be drained. The latency only covers the audio HAL
            // and kernel buffers. Also the latency does not always include additional delay in the
            // audio path (audio DSP, CODEC ...)
#ifdef VOICE_CONCURRENCY
            //if newDevice is invalid for voice stream, cancel the unexecuted device routing
            //command(if existed)which could be handled later in command queue for current output
            if (newDevice == AUDIO_DEVICE_NONE && stream == AudioSystem::VOICE_CALL)
                setOutputDevice(output, newDevice, true);
            else
#endif
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

// This function checks for the parameters which can be offloaded.
// This can be enhanced depending on the capability of the DSP and policy
// of the system.
bool AudioPolicyManager::isOffloadSupported(const audio_offload_info_t& offloadInfo)
{
    ALOGD("copl: isOffloadSupported: SR=%u, CM=0x%x, Format=0x%x, StreamType=%d,"
     " BitRate=%u, duration=%lld us, has_video=%d",
     offloadInfo.sample_rate, offloadInfo.channel_mask,
     offloadInfo.format,
     offloadInfo.stream_type, offloadInfo.bit_rate, offloadInfo.duration_us,
     offloadInfo.has_video);

#ifdef VOICE_CONCURRENCY
    char concpropValue[PROPERTY_VALUE_MAX];
    if(property_get("voice.playback.conc.disabled", concpropValue, NULL)) {
         bool propenabled = atoi(concpropValue) || !strncmp("true", concpropValue, 4);
         if (propenabled) {
            if(isInCall())
            {
                ALOGD("\n copl: blocking  compress offload on call mode\n");
                return false;
            }
         }
    }

#endif
    // Check if stream type is music, then only allow offload as of now.
    if (offloadInfo.stream_type != AUDIO_STREAM_MUSIC)
    {
        ALOGD("copl: isOffloadSupported: stream_type != MUSIC, returning false");
        return false;
    }

    char propValue[PROPERTY_VALUE_MAX];
    bool pcmOffload = false;
    if (audio_is_offload_pcm(offloadInfo.format)) {
        if(property_get("audio.offload.pcm.enable", propValue, NULL)) {
            bool prop_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
            if (prop_enabled) {
                ALOGW("PCM offload property is enabled");
                pcmOffload = true;
            }
        }
        if (!pcmOffload) {
            ALOGD("copl: PCM offload disabled by property audio.offload.pcm.enable");
            return false;
        }
    }

    if (!pcmOffload) {
        // Check if offload has been disabled
        if (property_get("audio.offload.disable", propValue, "0")) {
            if (atoi(propValue) != 0) {
                ALOGD("copl: offload disabled by audio.offload.disable=%s", propValue );
                return false;
            }
        }

        //check if it's multi-channel AAC format
        if (AudioSystem::popCount(offloadInfo.channel_mask) > 2
              && offloadInfo.format == AUDIO_FORMAT_AAC) {
            ALOGD("copl: offload disabled for multi-channel AAC format");
            return false;
        }

        if (offloadInfo.has_video)
        {
            if(property_get("av.offload.enable", propValue, NULL)) {
                bool prop_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
                if (!prop_enabled) {
                    ALOGW("offload disabled by av.offload.enable = %s ", propValue );
                    return false;
                }
            } else {
                return false;
            }

            if(offloadInfo.is_streaming) {
                if (property_get("av.streaming.offload.enable", propValue, NULL)) {
                    bool prop_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
                    if (!prop_enabled) {
                       ALOGW("offload disabled by av.streaming.offload.enable = %s ", propValue );
                       return false;
                    }
                } else {
                    //Do not offload AV streamnig if the property is not defined
                    return false;
                }
            }
            ALOGD("copl: isOffloadSupported: has_video == true, property\
                    set to enable offload");
        }
    }

    //If duration is less than minimum value defined in property, return false
    if (property_get("audio.offload.min.duration.secs", propValue, NULL)) {
        if (offloadInfo.duration_us < (atoi(propValue) * 1000000 )) {
            ALOGD("copl: Offload denied by duration < audio.offload.min.duration.secs(=%s)", propValue);
            return false;
        }
    } else if (offloadInfo.duration_us < OFFLOAD_DEFAULT_MIN_DURATION_SECS * 1000000) {
        ALOGD("copl: Offload denied by duration < default min(=%u)", OFFLOAD_DEFAULT_MIN_DURATION_SECS);
        //duration checks only valid for MP3/AAC formats,
        //do not check duration for other audio formats, e.g. dolby AAC/AC3 and amrwb+ formats
        if (offloadInfo.format == AUDIO_FORMAT_MP3 || offloadInfo.format == AUDIO_FORMAT_AAC || pcmOffload)
            return false;
    }

    // Do not allow offloading if one non offloadable effect is enabled. This prevents from
    // creating an offloaded track and tearing it down immediately after start when audioflinger
    // detects there is an active non offloadable effect.
    // FIXME: We should check the audio session here but we do not have it in this context.
    // This may prevent offloading in rare situations where effects are left active by apps
    // in the background.
    if (isNonOffloadableEffectEnabled()) {
        return false;
    }

    // See if there is a profile to support this.
    // AUDIO_DEVICE_NONE
    IOProfile *profile = getProfileForDirectOutput(AUDIO_DEVICE_NONE /*ignore device */,
                                            offloadInfo.sample_rate,
                                            offloadInfo.format,
                                            offloadInfo.channel_mask,
                                            AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD);
    ALOGD("copl: isOffloadSupported() profile %sfound", profile != NULL ? "" : "NOT ");
    return (profile != NULL);
}

void AudioPolicyManager::setPhoneState(int state)

{
    ALOGD("setPhoneState() state %d", state);
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
    int voice_call_state = 0;

    // are we entering or starting a call
    if (!isStateInCall(oldState) && isStateInCall(state)) {
        ALOGV("  Entering call in setPhoneState()");
        // force routing command to audio hardware when starting a call
        // even if no device change is needed
        force = true;
        for (int j = 0; j < DEVICE_CATEGORY_CNT; j++) {
            mStreams[AUDIO_STREAM_DTMF].mVolumeCurve[j] =
                    sVolumeProfiles[AUDIO_STREAM_VOICE_CALL][j];
        }
    } else if (isStateInCall(oldState) && !isStateInCall(state)) {
        ALOGV("  Exiting call in setPhoneState()");
        // force routing command to audio hardware when exiting a call
        // even if no device change is needed
        force = true;
        for (int j = 0; j < DEVICE_CATEGORY_CNT; j++) {
            mStreams[AUDIO_STREAM_DTMF].mVolumeCurve[j] =
                    sVolumeProfiles[AUDIO_STREAM_DTMF][j];
        }
    } else if (isStateInCall(state) && (state != oldState)) {
        ALOGV("  Switching between telephony and VoIP in setPhoneState()");
        // force routing command to audio hardware when switching between telephony and VoIP
        // even if no device change is needed
        force = true;
    }

    // check for device and output changes triggered by new phone state
    newDevice = getNewDevice(mPrimaryOutput, false /*fromCache*/);
    checkA2dpSuspend();
    checkOutputForAllStrategies();
    updateDevicesAndOutputs();

    AudioOutputDescriptor *hwOutputDesc = mOutputs.valueFor(mPrimaryOutput);

    // force routing command to audio hardware when ending call
    // even if no device change is needed
    if (isStateInCall(oldState) && newDevice == AUDIO_DEVICE_NONE) {
        newDevice = hwOutputDesc->device();
    }
#ifdef VOICE_CONCURRENCY
    char propValue[PROPERTY_VALUE_MAX];
    bool prop_playback_enabled = false, prop_rec_enabled=false, prop_voip_enabled = false;

    if(property_get("voice.playback.conc.disabled", propValue, NULL)) {
        prop_playback_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if(property_get("voice.record.conc.disabled", propValue, NULL)) {
        prop_rec_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if(property_get("voice.voip.conc.disabled", propValue, NULL)) {
        prop_voip_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    bool mode_in_call = (AudioSystem::MODE_IN_CALL != oldState) && (AudioSystem::MODE_IN_CALL == state);
    //query if it is a actual voice call initiated by telephony
    if (mode_in_call) {
        String8 valueStr = mpClientInterface->getParameters((audio_io_handle_t)0, String8("in_call"));
        AudioParameter result = AudioParameter(valueStr);
        if (result.getInt(String8("in_call"), voice_call_state) == NO_ERROR)
            ALOGD("SetPhoneState: Voice call state = %d", voice_call_state);
    }

    if (mode_in_call && voice_call_state) {
        ALOGD("Entering to call mode oldState :: %d state::%d ",oldState, state);
        mvoice_call_state = voice_call_state;
        if (prop_playback_enabled) {
            //Call invalidate to reset all opened non ULL audio tracks
            // Move tracks associated to this strategy from previous output to new output
            for (int i = AudioSystem::SYSTEM; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                ALOGV(" Invalidate on call mode for stream :: %d ", i);
                //FIXME see fixme on name change
                mpClientInterface->setStreamOutput((AudioSystem::stream_type)i,
                                                  0 /* ignored */);
            }
        }

        if (prop_rec_enabled) {
            //Close all active inputs
            audio_io_handle_t activeInput = getActiveInput();
            if (activeInput != 0) {
               AudioInputDescriptor *activeDesc = mInputs.valueFor(activeInput);
               switch(activeDesc->mInputSource) {
                   case AUDIO_SOURCE_VOICE_UPLINK:
                   case AUDIO_SOURCE_VOICE_DOWNLINK:
                   case AUDIO_SOURCE_VOICE_CALL:
                       ALOGD("FOUND active input during call active: %d",activeDesc->mInputSource);
                   break;

                   case  AUDIO_SOURCE_VOICE_COMMUNICATION:
                        if(prop_voip_enabled) {
                            ALOGD("CLOSING VoIP input source on call setup :%d ",activeDesc->mInputSource);
                            stopInput(activeInput);
                            releaseInput(activeInput);
                        }
                   break;

                   default:
                       ALOGD("CLOSING input on call setup  for inputSource: %d",activeDesc->mInputSource);
                       stopInput(activeInput);
                       releaseInput(activeInput);
                   break;
               }
           }
        } else if (prop_voip_enabled) {
            audio_io_handle_t activeInput = getActiveInput();
            if (activeInput != 0) {
               AudioInputDescriptor *activeDesc = mInputs.valueFor(activeInput);
                if (AUDIO_SOURCE_VOICE_COMMUNICATION == activeDesc->mInputSource) {
                    ALOGD("CLOSING VoIP on call setup : %d",activeDesc->mInputSource);
                    stopInput(activeInput);
                    releaseInput(activeInput);
                }
            }
        }

        //suspend  PCM (deep-buffer) output & close  compress & direct tracks
        for (size_t i = 0; i < mOutputs.size(); i++) {
            AudioOutputDescriptor *outputDesc = mOutputs.valueAt(i);
            if (!outputDesc || !outputDesc->mProfile) {
               ALOGD("ouput desc / profile is NULL");
               continue;
            }
            if (((!outputDesc->isDuplicated() &&outputDesc->mProfile->mFlags & AUDIO_OUTPUT_FLAG_PRIMARY))
                        && prop_playback_enabled) {
                ALOGD(" calling suspendOutput on call mode for primary output");
                mpClientInterface->suspendOutput(mOutputs.keyAt(i));
            } //Close compress all sessions
            else if ((outputDesc->mProfile->mFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)
                            &&  prop_playback_enabled) {
                ALOGD(" calling closeOutput on call mode for COMPRESS output");
                closeOutput(mOutputs.keyAt(i));
            }
            else if ((outputDesc->mProfile->mFlags & AUDIO_OUTPUT_FLAG_VOIP_RX)
                            && prop_voip_enabled) {
                ALOGD(" calling closeOutput on call mode for DIRECT  output");
                closeOutput(mOutputs.keyAt(i));
            }
        }
   }

   if ((AudioSystem::MODE_IN_CALL == oldState) && (AudioSystem::MODE_IN_CALL != state)
        && prop_playback_enabled && mvoice_call_state) {
        ALOGD("EXITING from call mode oldState :: %d state::%d \n",oldState, state);
        mvoice_call_state = 0;
        //restore PCM (deep-buffer) output after call termination
        for (size_t i = 0; i < mOutputs.size(); i++) {
            AudioOutputDescriptor *outputDesc = mOutputs.valueAt(i);
            if (!outputDesc || !outputDesc->mProfile) {
               ALOGD("ouput desc / profile is NULL");
               continue;
            }
            if (!outputDesc->isDuplicated() &&outputDesc->mProfile->mFlags & AUDIO_OUTPUT_FLAG_PRIMARY) {
                ALOGD("calling restoreOutput after call mode for primary output");
                mpClientInterface->restoreOutput(mOutputs.keyAt(i));
            }
       }
       //call invalidate tracks so that any open streams can fall back to deep buffer/compress path from ULL
       for (int i = AudioSystem::SYSTEM; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
           ALOGD("Invalidate on call mode for stream :: %d ", i);
           //FIXME see fixme on name change
           mpClientInterface->setStreamOutput((AudioSystem::stream_type)i,
                                                  0 /* ignored */);
       }
    }
#endif
    mPrevPhoneState = oldState;

    int delayMs = 0;
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
    ALOGD(" End of setPhoneState ... mPhoneState: %d ",mPhoneState);
}

bool AudioPolicyManager::isStateInCall(int state)
{
    return ((state == AudioSystem::MODE_IN_CALL) || (state == AudioSystem::MODE_IN_COMMUNICATION) ||
       ((state == AudioSystem::MODE_RINGTONE) && (mPrevPhoneState == AudioSystem::MODE_IN_CALL)));
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
