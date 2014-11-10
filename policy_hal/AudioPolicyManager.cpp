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

// A device mask for all audio input devices that are considered "virtual" when evaluating
// active inputs in getActiveInput()
#ifdef AUDIO_EXTN_FM_ENABLED
#define APM_AUDIO_IN_DEVICE_VIRTUAL_ALL  (AUDIO_DEVICE_IN_REMOTE_SUBMIX | AUDIO_DEVICE_IN_FM_RX_A2DP)
#else
#define APM_AUDIO_IN_DEVICE_VIRTUAL_ALL  AUDIO_DEVICE_IN_REMOTE_SUBMIX
#endif
// A device mask for all audio output devices that are considered "remote" when evaluating
// active output devices in isStreamActiveRemotely()
#define APM_AUDIO_OUT_DEVICE_REMOTE_ALL  AUDIO_DEVICE_OUT_REMOTE_SUBMIX
// A device mask for all audio input and output devices where matching inputs/outputs on device
// type alone is not enough: the address must match too
#define APM_AUDIO_DEVICE_MATCH_ADDRESS_ALL (AUDIO_DEVICE_IN_REMOTE_SUBMIX | \
                                            AUDIO_DEVICE_OUT_REMOTE_SUBMIX)

#include <inttypes.h>
#include <math.h>

#include <cutils/properties.h>
#include <utils/Log.h>
#include <hardware/audio.h>
#include <hardware/audio_effect.h>
#include <media/AudioParameter.h>
#include <soundtrigger/SoundTrigger.h>
#include "AudioPolicyManager.h"

namespace android {

// ----------------------------------------------------------------------------
// AudioPolicyInterface implementation
// ----------------------------------------------------------------------------

status_t AudioPolicyManagerCustom::setDeviceConnectionState(audio_devices_t device,
                                                          audio_policy_dev_state_t state,
                                                  const char *device_address)
{
    String8 address = (device_address == NULL) ? String8("") : String8(device_address);

    ALOGV("setDeviceConnectionState() device: %x, state %d, address %s",
            device, state, address.string());

    // connect/disconnect only 1 device at a time
    if (!audio_is_output_device(device) && !audio_is_input_device(device)) return BAD_VALUE;

    // handle output devices
    if (audio_is_output_device(device)) {
        SortedVector <audio_io_handle_t> outputs;

        sp<DeviceDescriptor> devDesc = new DeviceDescriptor(String8(""), device);
        devDesc->mAddress = address;
        ssize_t index = mAvailableOutputDevices.indexOf(devDesc);

        // save a copy of the opened output descriptors before any output is opened or closed
        // by checkOutputsForDevice(). This will be needed by checkOutputForAllStrategies()
        mPreviousOutputs = mOutputs;
        switch (state)
        {
        // handle output device connection
        case AUDIO_POLICY_DEVICE_STATE_AVAILABLE:
            if (index >= 0) {
#ifdef AUDIO_EXTN_HDMI_SPK_ENABLED
                if ((popcount(device) == 1) && (device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
                   if (!strncmp(device_address, "hdmi_spkr", 9)) {
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

            // register new device as available
            index = mAvailableOutputDevices.add(devDesc);

#ifdef AUDIO_EXTN_HDMI_SPK_ENABLED
            if ((popcount(device) == 1) && (device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
                if (!strncmp(device_address, "hdmi_spkr", 9)) {
                    mHdmiAudioDisabled = false;
                } else {
                    mHdmiAudioEvent = true;
                }
                if (mHdmiAudioDisabled || !mHdmiAudioEvent) {
                    mAvailableOutputDevices.remove(devDesc);
                }
            }
#endif
            if (index >= 0) {
                sp<HwModule> module = getModuleForDevice(device);
                if (module == 0) {
                    ALOGD("setDeviceConnectionState() could not find HW module for device %08x",
                          device);
                    mAvailableOutputDevices.remove(devDesc);
                    return INVALID_OPERATION;
                }
                mAvailableOutputDevices[index]->mId = nextUniqueId();
                mAvailableOutputDevices[index]->mModule = module;
            } else {
                return NO_MEMORY;
            }

            if (checkOutputsForDevice(devDesc, state, outputs, address) != NO_ERROR) {
                mAvailableOutputDevices.remove(devDesc);
                return INVALID_OPERATION;
            }
            // outputs should never be empty here
            ALOG_ASSERT(outputs.size() != 0, "setDeviceConnectionState():"
                    "checkOutputsForDevice() returned no outputs but status OK");
            ALOGV("setDeviceConnectionState() checkOutputsForDevice() returned %zu outputs",
                  outputs.size());
            break;
        // handle output device disconnection
        case AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE: {
            if (index < 0) {
#ifdef AUDIO_EXTN_HDMI_SPK_ENABLED
                if ((popcount(device) == 1) && (device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
                    if (!strncmp(device_address, "hdmi_spkr", 9)) {
                        mHdmiAudioDisabled = true;
                    } else {
                        mHdmiAudioEvent = false;
                    }
                }
#endif
                ALOGW("setDeviceConnectionState() device not connected: %x", device);
                return INVALID_OPERATION;
            }

            ALOGV("setDeviceConnectionState() disconnecting output device %x", device);

            // Set Disconnect to HALs
            AudioParameter param = AudioParameter(address);
            param.addInt(String8(AUDIO_PARAMETER_DEVICE_DISCONNECT), device);
            mpClientInterface->setParameters(AUDIO_IO_HANDLE_NONE, param.toString());

            // remove device from available output devices
            mAvailableOutputDevices.remove(devDesc);

#ifdef AUDIO_EXTN_HDMI_SPK_ENABLED
            if ((popcount(device) == 1) && (device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
                if (!strncmp(device_address, "hdmi_spkr", 9)) {
                    mHdmiAudioDisabled = true;
                } else {
                    mHdmiAudioEvent = false;
                }
            }
#endif
            checkOutputsForDevice(devDesc, state, outputs, address);
            } break;

        default:
            ALOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

        // checkA2dpSuspend must run before checkOutputForAllStrategies so that A2DP
        // output is suspended before any tracks are moved to it
        checkA2dpSuspend();
        checkOutputForAllStrategies();
        // outputs must be closed after checkOutputForAllStrategies() is executed
        if (!outputs.isEmpty()) {
            for (size_t i = 0; i < outputs.size(); i++) {
                sp<AudioOutputDescriptor> desc = mOutputs.valueFor(outputs[i]);
                // close unused outputs after device disconnection or direct outputs that have been
                // opened by checkOutputsForDevice() to query dynamic parameters
                if ((state == AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE) ||
                        (((desc->mFlags & AUDIO_OUTPUT_FLAG_DIRECT) != 0) &&
                         (desc->mDirectOpenCount == 0))) {
                    closeOutput(outputs[i]);
                }
            }
            // check again after closing A2DP output to reset mA2dpSuspended if needed
            checkA2dpSuspend();
        }

        updateDevicesAndOutputs();
        audio_devices_t newDevice = getNewOutputDevice(mPrimaryOutput, false /*fromCache*/);
        if (mPhoneState == AUDIO_MODE_IN_CALL) {
            updateCallRouting(newDevice);
        }

#ifdef AUDIO_EXTN_FM_ENABLED
        if(device == AUDIO_DEVICE_OUT_FM) {
            if (state == AUDIO_POLICY_DEVICE_STATE_AVAILABLE) {
                mOutputs.valueFor(mPrimaryOutput)->changeRefCount(AUDIO_STREAM_MUSIC, 1);
                newDevice = (audio_devices_t)(getNewOutputDevice(mPrimaryOutput, false) | AUDIO_DEVICE_OUT_FM);
            } else {
                mOutputs.valueFor(mPrimaryOutput)->changeRefCount(AUDIO_STREAM_MUSIC, -1);
            }

            AudioParameter param = AudioParameter();
            param.addInt(String8("handle_fm"), (int)newDevice);
            ALOGV("setDeviceConnectionState() setParameters handle_fm");
            mpClientInterface->setParameters(mPrimaryOutput, param.toString());
        }
#endif
        for (size_t i = 0; i < mOutputs.size(); i++) {
            audio_io_handle_t output = mOutputs.keyAt(i);
            if ((mPhoneState != AUDIO_MODE_IN_CALL) || (output != mPrimaryOutput)) {
                audio_devices_t newDevice = getNewOutputDevice(mOutputs.keyAt(i),
                                                               true /*fromCache*/);
                // do not force device change on duplicated output because if device is 0, it will
                // also force a device 0 for the two outputs it is duplicated to which may override
                // a valid device selection on those outputs.
                bool force = !mOutputs.valueAt(i)->isDuplicated()
                        && (!deviceDistinguishesOnAddress(device)
                                // always force when disconnecting (a non-duplicated device)
                                || (state == AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE));
                setOutputDevice(output, newDevice, force, 0);
            }
        }

        mpClientInterface->onAudioPortListUpdate();
        return NO_ERROR;
    }  // end if is output device

    // handle input devices
    if (audio_is_input_device(device)) {
        SortedVector <audio_io_handle_t> inputs;

        sp<DeviceDescriptor> devDesc = new DeviceDescriptor(String8(""), device);
        devDesc->mAddress = address;
        ssize_t index = mAvailableInputDevices.indexOf(devDesc);
        switch (state)
        {
        // handle input device connection
        case AUDIO_POLICY_DEVICE_STATE_AVAILABLE: {
            if (index >= 0) {
                ALOGW("setDeviceConnectionState() device already connected: %d", device);
                return INVALID_OPERATION;
            }
            sp<HwModule> module = getModuleForDevice(device);
            if (module == NULL) {
                ALOGW("setDeviceConnectionState(): could not find HW module for device %08x",
                      device);
                return INVALID_OPERATION;
            }
            if (checkInputsForDevice(device, state, inputs, address) != NO_ERROR) {
                return INVALID_OPERATION;
            }

            index = mAvailableInputDevices.add(devDesc);
            if (index >= 0) {
                mAvailableInputDevices[index]->mId = nextUniqueId();
                mAvailableInputDevices[index]->mModule = module;
            } else {
                return NO_MEMORY;
            }
        } break;

        // handle input device disconnection
        case AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE: {
            if (index < 0) {
                ALOGW("setDeviceConnectionState() device not connected: %d", device);
                return INVALID_OPERATION;
            }

            ALOGV("setDeviceConnectionState() disconnecting input device %x", device);

            // Set Disconnect to HALs
            AudioParameter param = AudioParameter(address);
            param.addInt(String8(AUDIO_PARAMETER_DEVICE_DISCONNECT), device);
            mpClientInterface->setParameters(AUDIO_IO_HANDLE_NONE, param.toString());

            checkInputsForDevice(device, state, inputs, address);
            mAvailableInputDevices.remove(devDesc);

        } break;

        default:
            ALOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

        closeAllInputs();

        if (mPhoneState == AUDIO_MODE_IN_CALL) {
            audio_devices_t newDevice = getNewOutputDevice(mPrimaryOutput, false /*fromCache*/);
            updateCallRouting(newDevice);
        }

        mpClientInterface->onAudioPortListUpdate();
        return NO_ERROR;
    } // end if is input device

    ALOGW("setDeviceConnectionState() invalid device: %x", device);
    return BAD_VALUE;
}

audio_policy_dev_state_t AudioPolicyManagerCustom::getDeviceConnectionState(audio_devices_t device,
                                                  const char *device_address)
{
    audio_policy_dev_state_t state = AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE;
    sp<DeviceDescriptor> devDesc = new DeviceDescriptor(String8(""), device);
    devDesc->mAddress = (device_address == NULL) ? String8("") : String8(device_address);
    ssize_t index;
    DeviceVector *deviceVector;

    if (audio_is_output_device(device)) {
        deviceVector = &mAvailableOutputDevices;
    } else if (audio_is_input_device(device)) {
        deviceVector = &mAvailableInputDevices;
    } else {
        ALOGW("getDeviceConnectionState() invalid device type %08x", device);
        return AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE;
    }

    index = deviceVector->indexOf(devDesc);
    if (index >= 0) {
        return AUDIO_POLICY_DEVICE_STATE_AVAILABLE;
    } else {
        return AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE;
    }
}

void AudioPolicyManagerCustom::setPhoneState(audio_mode_t state)
{
    ALOGD("setPhoneState() state %d", state);
    audio_devices_t newDevice = AUDIO_DEVICE_NONE;

    if (state < 0 || state >= AUDIO_MODE_CNT) {
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
        for (int stream = 0; stream < AUDIO_STREAM_CNT; stream++) {
            handleIncallSonification((audio_stream_type_t)stream, false, true);
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
    checkA2dpSuspend();
    checkOutputForAllStrategies();
    updateDevicesAndOutputs();

    sp<AudioOutputDescriptor> hwOutputDesc = mOutputs.valueFor(mPrimaryOutput);

#ifdef VOICE_CONCURRENCY
    int voice_call_state = 0;
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

    bool mode_in_call = (AUDIO_MODE_IN_CALL != oldState) && (AUDIO_MODE_IN_CALL == state);
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
            for (int i = AUDIO_STREAM_SYSTEM; i < (int)AUDIO_STREAM_CNT; i++) {
                ALOGV(" Invalidate on call mode for stream :: %d ", i);
                //FIXME see fixme on name change
                mpClientInterface->invalidateStream((audio_stream_type_t)i);
            }
        }

        if (prop_rec_enabled) {
            //Close all active inputs
            audio_io_handle_t activeInput = getActiveInput();
            if (activeInput != 0) {
               sp<AudioInputDescriptor> activeDesc = mInputs.valueFor(activeInput);
               switch(activeDesc->mInputSource) {
                   case AUDIO_SOURCE_VOICE_UPLINK:
                   case AUDIO_SOURCE_VOICE_DOWNLINK:
                   case AUDIO_SOURCE_VOICE_CALL:
                       ALOGD("FOUND active input during call active: %d",activeDesc->mInputSource);
                   break;

                   case  AUDIO_SOURCE_VOICE_COMMUNICATION:
                        if(prop_voip_enabled) {
                            ALOGD("CLOSING VoIP input source on call setup :%d ",activeDesc->mInputSource);
                            stopInput(activeInput, activeDesc->mSessions.itemAt(0));
                            releaseInput(activeInput, activeDesc->mSessions.itemAt(0));
                        }
                   break;

                   default:
                       ALOGD("CLOSING input on call setup  for inputSource: %d",activeDesc->mInputSource);
                       stopInput(activeInput, activeDesc->mSessions.itemAt(0));
                       releaseInput(activeInput, activeDesc->mSessions.itemAt(0));
                   break;
               }
           }
        } else if (prop_voip_enabled) {
            audio_io_handle_t activeInput = getActiveInput();
            if (activeInput != 0) {
                sp<AudioInputDescriptor> activeDesc = mInputs.valueFor(activeInput);
                if (AUDIO_SOURCE_VOICE_COMMUNICATION == activeDesc->mInputSource) {
                    ALOGD("CLOSING VoIP on call setup : %d",activeDesc->mInputSource);
                    stopInput(activeInput, activeDesc->mSessions.itemAt(0));
                    releaseInput(activeInput, activeDesc->mSessions.itemAt(0));
                }
            }
        }

        //suspend  PCM (deep-buffer) output & close  compress & direct tracks
        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<AudioOutputDescriptor> outputDesc = mOutputs.valueAt(i);
            if ( (outputDesc == NULL) || (outputDesc->mProfile == NULL)) {
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

   if ((AUDIO_MODE_IN_CALL == oldState || AUDIO_MODE_IN_COMMUNICATION == oldState) &&
       (AUDIO_MODE_NORMAL == state) && prop_playback_enabled && mvoice_call_state) {
        ALOGD("EXITING from call mode oldState :: %d state::%d \n",oldState, state);
        mvoice_call_state = 0;
        //restore PCM (deep-buffer) output after call termination
        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<AudioOutputDescriptor> outputDesc = mOutputs.valueAt(i);
            if ( (outputDesc == NULL) || (outputDesc->mProfile == NULL)) {
               ALOGD("ouput desc / profile is NULL");
               continue;
            }
            if (!outputDesc->isDuplicated() && outputDesc->mProfile->mFlags & AUDIO_OUTPUT_FLAG_PRIMARY) {
                ALOGD("calling restoreOutput after call mode for primary output");
                mpClientInterface->restoreOutput(mOutputs.keyAt(i));
            }
       }
       //call invalidate tracks so that any open streams can fall back to deep buffer/compress path from ULL
       for (int i = AUDIO_STREAM_SYSTEM; i < (int)AUDIO_STREAM_CNT; i++) {
           ALOGD("Invalidate after call ends for stream :: %d ", i);
           //FIXME see fixme on name change
           mpClientInterface->invalidateStream((audio_stream_type_t)i);
       }
    }
#endif
#ifdef RECORD_PLAY_CONCURRENCY
    char recConcPropValue[PROPERTY_VALUE_MAX];
    bool prop_rec_play_enabled = false;

    if (property_get("rec.playback.conc.disabled", recConcPropValue, NULL)) {
        prop_rec_play_enabled = atoi(recConcPropValue) || !strncmp("true", recConcPropValue, 4);
    }
    if (prop_rec_play_enabled) {
        if (AUDIO_MODE_IN_COMMUNICATION == mPhoneState) {
            ALOGD("phone state changed to MODE_IN_COMM invlaidating music and voice streams");
            // call invalidate for voice streams, so that it can use deepbuffer with VoIP out device from HAL
            mpClientInterface->invalidateStream(AUDIO_STREAM_VOICE_CALL);
            // call invalidate for music, so that compress will fallback to deep-buffer with VoIP out device
            mpClientInterface->invalidateStream(AUDIO_STREAM_MUSIC);

            // close compress output to make sure session will be closed before timeout(60sec)
            for (size_t i = 0; i < mOutputs.size(); i++) {

                sp<AudioOutputDescriptor> outputDesc = mOutputs.valueAt(i);
                if ((outputDesc == NULL) || (outputDesc->mProfile == NULL)) {
                   ALOGD("ouput desc / profile is NULL");
                   continue;
                }

                if (outputDesc->mProfile->mFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
                    ALOGD("calling closeOutput on call mode for COMPRESS output");
                    closeOutput(mOutputs.keyAt(i));
                }
            }
        } else if ((oldState == AUDIO_MODE_IN_COMMUNICATION) &&
                    (mPhoneState == AUDIO_MODE_NORMAL)) {
            // call invalidate for music so that music can fallback to compress
            mpClientInterface->invalidateStream(AUDIO_STREAM_MUSIC);
        }
    }
#endif

    mPrevPhoneState = oldState;

    int delayMs = 0;
    if (isStateInCall(state)) {
        nsecs_t sysTime = systemTime();
        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<AudioOutputDescriptor> desc = mOutputs.valueAt(i);
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

    // Note that despite the fact that getNewOutputDevice() is called on the primary output,
    // the device returned is not necessarily reachable via this output
    audio_devices_t rxDevice = getNewOutputDevice(mPrimaryOutput, false /*fromCache*/);
    // force routing command to audio hardware when ending call
    // even if no device change is needed
    if (isStateInCall(oldState) && rxDevice == AUDIO_DEVICE_NONE) {
        rxDevice = hwOutputDesc->device();
    }

    if (state == AUDIO_MODE_IN_CALL) {
        updateCallRouting(rxDevice, delayMs);
    } else if (oldState == AUDIO_MODE_IN_CALL) {
        if (mCallRxPatch != 0) {
            mpClientInterface->releaseAudioPatch(mCallRxPatch->mAfPatchHandle, 0);
            mCallRxPatch.clear();
        }
        if (mCallTxPatch != 0) {
            mpClientInterface->releaseAudioPatch(mCallTxPatch->mAfPatchHandle, 0);
            mCallTxPatch.clear();
        }
        setOutputDevice(mPrimaryOutput, rxDevice, force, 0);
    } else {
        setOutputDevice(mPrimaryOutput, rxDevice, force, 0);
    }

    //update device for all non-primary outputs
    for (size_t i = 0; i < mOutputs.size(); i++) {
        audio_io_handle_t output = mOutputs.keyAt(i);
        if (output != mPrimaryOutput) {
            newDevice = getNewOutputDevice(output, false /*fromCache*/);
            setOutputDevice(output, newDevice, (newDevice != AUDIO_DEVICE_NONE));
        }
    }

    // if entering in call state, handle special case of active streams
    // pertaining to sonification strategy see handleIncallSonification()
    if (isStateInCall(state)) {
        ALOGV("setPhoneState() in call state management: new state is %d", state);
        for (int stream = 0; stream < AUDIO_STREAM_CNT; stream++) {
            handleIncallSonification((audio_stream_type_t)stream, true, true);
        }
    }

    // Flag that ringtone volume must be limited to music volume until we exit MODE_RINGTONE
    if (state == AUDIO_MODE_RINGTONE &&
        isStreamActive(AUDIO_STREAM_MUSIC, SONIFICATION_HEADSET_MUSIC_DELAY)) {
        mLimitRingtoneVolume = true;
    } else {
        mLimitRingtoneVolume = false;
    }
}

void AudioPolicyManagerCustom::setForceUse(audio_policy_force_use_t usage,
                                         audio_policy_forced_cfg_t config)
{
    ALOGV("setForceUse() usage %d, config %d, mPhoneState %d", usage, config, mPhoneState);

    bool forceVolumeReeval = false;
    switch(usage) {
    case AUDIO_POLICY_FORCE_FOR_COMMUNICATION:
        if (config != AUDIO_POLICY_FORCE_SPEAKER && config != AUDIO_POLICY_FORCE_BT_SCO &&
            config != AUDIO_POLICY_FORCE_NONE) {
            ALOGW("setForceUse() invalid config %d for FOR_COMMUNICATION", config);
            return;
        }
        forceVolumeReeval = true;
        mForceUse[usage] = config;
        break;
    case AUDIO_POLICY_FORCE_FOR_MEDIA:
        if (config != AUDIO_POLICY_FORCE_HEADPHONES && config != AUDIO_POLICY_FORCE_BT_A2DP &&
#ifdef AUDIO_EXTN_FM_ENABLED
            config != AUDIO_POLICY_FORCE_SPEAKER &&
#endif
            config != AUDIO_POLICY_FORCE_WIRED_ACCESSORY &&
            config != AUDIO_POLICY_FORCE_ANALOG_DOCK &&
            config != AUDIO_POLICY_FORCE_DIGITAL_DOCK && config != AUDIO_POLICY_FORCE_NONE &&
            config != AUDIO_POLICY_FORCE_NO_BT_A2DP) {
            ALOGW("setForceUse() invalid config %d for FOR_MEDIA", config);
            return;
        }
        mForceUse[usage] = config;
        break;
    case AUDIO_POLICY_FORCE_FOR_RECORD:
        if (config != AUDIO_POLICY_FORCE_BT_SCO && config != AUDIO_POLICY_FORCE_WIRED_ACCESSORY &&
            config != AUDIO_POLICY_FORCE_NONE) {
            ALOGW("setForceUse() invalid config %d for FOR_RECORD", config);
            return;
        }
        mForceUse[usage] = config;
        break;
    case AUDIO_POLICY_FORCE_FOR_DOCK:
        if (config != AUDIO_POLICY_FORCE_NONE && config != AUDIO_POLICY_FORCE_BT_CAR_DOCK &&
            config != AUDIO_POLICY_FORCE_BT_DESK_DOCK &&
            config != AUDIO_POLICY_FORCE_WIRED_ACCESSORY &&
            config != AUDIO_POLICY_FORCE_ANALOG_DOCK &&
            config != AUDIO_POLICY_FORCE_DIGITAL_DOCK) {
            ALOGW("setForceUse() invalid config %d for FOR_DOCK", config);
        }
        forceVolumeReeval = true;
        mForceUse[usage] = config;
        break;
    case AUDIO_POLICY_FORCE_FOR_SYSTEM:
        if (config != AUDIO_POLICY_FORCE_NONE &&
            config != AUDIO_POLICY_FORCE_SYSTEM_ENFORCED) {
            ALOGW("setForceUse() invalid config %d for FOR_SYSTEM", config);
        }
        forceVolumeReeval = true;
        mForceUse[usage] = config;
        break;
    case AUDIO_POLICY_FORCE_FOR_HDMI_SYSTEM_AUDIO:
        if (config != AUDIO_POLICY_FORCE_NONE &&
            config != AUDIO_POLICY_FORCE_HDMI_SYSTEM_AUDIO_ENFORCED) {
            ALOGW("setForceUse() invalid config %d forHDMI_SYSTEM_AUDIO", config);
        }
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
    if (mPhoneState == AUDIO_MODE_IN_CALL) {
        audio_devices_t newDevice = getNewOutputDevice(mPrimaryOutput, true /*fromCache*/);
        updateCallRouting(newDevice);
    }
    for (size_t i = 0; i < mOutputs.size(); i++) {
        audio_io_handle_t output = mOutputs.keyAt(i);
        audio_devices_t newDevice = getNewOutputDevice(output, true /*fromCache*/);
        if ((mPhoneState != AUDIO_MODE_IN_CALL) || (output != mPrimaryOutput)) {
            setOutputDevice(output, newDevice, (newDevice != AUDIO_DEVICE_NONE));
        }
        if (forceVolumeReeval && (newDevice != AUDIO_DEVICE_NONE)) {
            applyStreamVolumes(output, newDevice, 0, true);
        }
    }

    audio_io_handle_t activeInput = getActiveInput();
    if (activeInput != 0) {
        setInputDevice(activeInput, getNewInputDevice(activeInput));
    }

}

audio_io_handle_t AudioPolicyManagerCustom::getOutputForDevice(
        audio_devices_t device,
        audio_stream_type_t stream,
        uint32_t samplingRate,
        audio_format_t format,
        audio_channel_mask_t channelMask,
        audio_output_flags_t flags,
        const audio_offload_info_t *offloadInfo)
{
    audio_io_handle_t output = AUDIO_IO_HANDLE_NONE;
    uint32_t latency = 0;
    status_t status;

#ifdef AUDIO_POLICY_TEST
    if (mCurOutput != 0) {
        ALOGV("getOutput() test output mCurOutput %d, samplingRate %d, format %d, channelMask %x, mDirectOutput %d",
                mCurOutput, mTestSamplingRate, mTestFormat, mTestChannels, mDirectOutput);

        if (mTestOutputs[mCurOutput] == 0) {
            ALOGV("getOutput() opening test output");
            sp<AudioOutputDescriptor> outputDesc = new AudioOutputDescriptor(NULL);
            outputDesc->mDevice = mTestDevice;
            outputDesc->mLatency = mTestLatencyMs;
            outputDesc->mFlags =
                    (audio_output_flags_t)(mDirectOutput ? AUDIO_OUTPUT_FLAG_DIRECT : 0);
            outputDesc->mRefCount[stream] = 0;
            audio_config_t config = AUDIO_CONFIG_INITIALIZER;
            config.sample_rate = mTestSamplingRate;
            config.channel_mask = mTestChannels;
            config.format = mTestFormat;
            if (offloadInfo != NULL) {
                config.offload_info = *offloadInfo;
            }
            status = mpClientInterface->openOutput(0,
                                                  &mTestOutputs[mCurOutput],
                                                  &config,
                                                  &outputDesc->mDevice,
                                                  String8(""),
                                                  &outputDesc->mLatency,
                                                  outputDesc->mFlags);
            if (status == NO_ERROR) {
                outputDesc->mSamplingRate = config.sample_rate;
                outputDesc->mFormat = config.format;
                outputDesc->mChannelMask = config.channel_mask;
                AudioParameter outputCmd = AudioParameter();
                outputCmd.addInt(String8("set_id"),mCurOutput);
                mpClientInterface->setParameters(mTestOutputs[mCurOutput],outputCmd.toString());
                addOutput(mTestOutputs[mCurOutput], outputDesc);
            }
        }
        return mTestOutputs[mCurOutput];
    }
#endif //AUDIO_POLICY_TEST

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
        if((AUDIO_MODE_IN_CALL == mPhoneState) ||
             ((AUDIO_MODE_IN_CALL == mPrevPhoneState)
                && (AUDIO_MODE_IN_COMMUNICATION == mPhoneState)))
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
                flags = AUDIO_OUTPUT_FLAG_FAST;
            }
        }
    } else if (prop_voip_enabled && mvoice_call_state) {
        //check if voice call is active  / running in background
        //some of VoIP apps(like SIP2SIP call) supports resume of VoIP call when call in progress
        //return only ULL ouput
        if((AUDIO_MODE_IN_CALL == mPhoneState) ||
             ((AUDIO_MODE_IN_CALL == mPrevPhoneState)
                && (AUDIO_MODE_IN_COMMUNICATION == mPhoneState)))
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
    audio_devices_t availableOutputDeviceTypes = mAvailableOutputDevices.types();
    if ((availableOutputDeviceTypes & AUDIO_DEVICE_OUT_PROXY)
          && (stream != AUDIO_STREAM_MUSIC)) {
        ALOGD(" WFD mode adding ULL flags for non music stream.. flags: %x ", flags );
        //For voip paths
        if(flags & AUDIO_OUTPUT_FLAG_DIRECT)
            flags = AUDIO_OUTPUT_FLAG_DIRECT;
        else //route every thing else to ULL path
            flags = AUDIO_OUTPUT_FLAG_FAST;
    }
#endif

#ifdef RECORD_PLAY_CONCURRENCY
    char recConcPropValue[PROPERTY_VALUE_MAX];
    bool prop_rec_play_enabled = false;

    if (property_get("rec.playback.conc.disabled", recConcPropValue, NULL)) {
        prop_rec_play_enabled = atoi(recConcPropValue) || !strncmp("true", recConcPropValue, 4);
    }
    if ((prop_rec_play_enabled) &&
            ((true == mIsInputRequestOnProgress) || (activeInputsCount() > 0))) {
        if (AUDIO_MODE_IN_COMMUNICATION == mPhoneState) {
            if (AUDIO_OUTPUT_FLAG_VOIP_RX & flags) {
                // allow VoIP using voice path
                // Do nothing
            } else if((flags & AUDIO_OUTPUT_FLAG_FAST) != 0) {
                ALOGD(" MODE_IN_COMM is setforcing deep buffer output for non ULL... flags: %x", flags);
                // use deep buffer path for all non ULL outputs
                flags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
            }
        } else if ((flags & AUDIO_OUTPUT_FLAG_FAST) != 0) {
            ALOGD(" Record mode is on forcing deep buffer output for non ULL... flags: %x ", flags);
            // use deep buffer path for all non ULL outputs
            flags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
        }
    }
    if (prop_rec_play_enabled &&
            (stream == AUDIO_STREAM_ENFORCED_AUDIBLE)) {
           ALOGD("Record conc is on forcing ULL output for ENFORCED_AUDIBLE");
           flags = AUDIO_OUTPUT_FLAG_FAST;
    }
#endif
    // open a direct output if required by specified parameters
    //force direct flag if offload flag is set: offloading implies a direct output stream
    // and all common behaviors are driven by checking only the direct flag
    // this should normally be set appropriately in the policy configuration file
    if ((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) != 0) {
        flags = (audio_output_flags_t)(flags | AUDIO_OUTPUT_FLAG_DIRECT);
    }
    if ((flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) != 0) {
        flags = (audio_output_flags_t)(flags | AUDIO_OUTPUT_FLAG_DIRECT);
    }

    if ((format == AUDIO_FORMAT_PCM_16_BIT) &&(popcount(channelMask) > 2)) {
        ALOGV("owerwrite flag(%x) for PCM16 multi-channel(CM:%x) playback", flags ,channelMask);
        flags = AUDIO_OUTPUT_FLAG_DIRECT;
    }

    sp<IOProfile> profile;

    // skip direct output selection if the request can obviously be attached to a mixed output
    // and not explicitly requested
    if (((flags & AUDIO_OUTPUT_FLAG_DIRECT) == 0) &&
            audio_is_linear_pcm(format) && samplingRate <= MAX_MIXER_SAMPLING_RATE &&
            audio_channel_count_from_out_mask(channelMask) <= 2) {
        goto non_direct_output;
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

    if (profile != 0) {
        sp<AudioOutputDescriptor> outputDesc = NULL;

        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<AudioOutputDescriptor> desc = mOutputs.valueAt(i);
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
            closeOutput(outputDesc->mIoHandle);
        }
        outputDesc = new AudioOutputDescriptor(profile);
        outputDesc->mDevice = device;
        outputDesc->mLatency = 0;
        outputDesc->mFlags =(audio_output_flags_t) (outputDesc->mFlags | flags);
        audio_config_t config = AUDIO_CONFIG_INITIALIZER;
        config.sample_rate = samplingRate;
        config.channel_mask = channelMask;
        config.format = format;
        if (offloadInfo != NULL) {
            config.offload_info = *offloadInfo;
        }
        status = mpClientInterface->openOutput(profile->mModule->mHandle,
                                               &output,
                                               &config,
                                               &outputDesc->mDevice,
                                               String8(""),
                                               &outputDesc->mLatency,
                                               outputDesc->mFlags);

        // only accept an output with the requested parameters
        if (status != NO_ERROR ||
            (samplingRate != 0 && samplingRate != config.sample_rate) ||
            (format != AUDIO_FORMAT_DEFAULT && format != config.format) ||
            (channelMask != 0 && channelMask != config.channel_mask)) {
            ALOGV("getOutput() failed opening direct output: output %d samplingRate %d %d,"
                    "format %d %d, channelMask %04x %04x", output, samplingRate,
                    outputDesc->mSamplingRate, format, outputDesc->mFormat, channelMask,
                    outputDesc->mChannelMask);
            if (output != AUDIO_IO_HANDLE_NONE) {
                mpClientInterface->closeOutput(output);
            }
            return AUDIO_IO_HANDLE_NONE;
        }
        outputDesc->mSamplingRate = config.sample_rate;
        outputDesc->mChannelMask = config.channel_mask;
        outputDesc->mFormat = config.format;
        outputDesc->mRefCount[stream] = 0;
        outputDesc->mStopTime[stream] = 0;
        outputDesc->mDirectOpenCount = 1;

        audio_io_handle_t srcOutput = getOutputForEffect();
        addOutput(output, outputDesc);
        audio_io_handle_t dstOutput = getOutputForEffect();
        if (dstOutput == output) {
            mpClientInterface->moveEffects(AUDIO_SESSION_OUTPUT_MIX, srcOutput, dstOutput);
        }
        mPreviousOutputs = mOutputs;
        ALOGV("getOutput() returns new direct output %d", output);
        mpClientInterface->onAudioPortListUpdate();
        return output;
    }

non_direct_output:

    // ignoring channel mask due to downmix capability in mixer

    // open a non direct output

    // for non direct outputs, only PCM is supported
    if (audio_is_linear_pcm(format)) {
        // get which output is suitable for the specified stream. The actual
        // routing change will happen when startOutput() will be called
        SortedVector<audio_io_handle_t> outputs = getOutputsForDevice(device, mOutputs);

        // at this stage we should ignore the DIRECT flag as no direct output could be found earlier
        flags = (audio_output_flags_t)(flags & ~AUDIO_OUTPUT_FLAG_DIRECT);
        output = selectOutput(outputs, flags, format);
    }
    ALOGW_IF((output == 0), "getOutput() could not find output for stream %d, samplingRate %d,"
            "format %d, channels %x, flags %x", stream, samplingRate, format, channelMask, flags);

    ALOGV("getOutput() returns output %d", output);

    return output;
}


status_t AudioPolicyManagerCustom::stopOutput(audio_io_handle_t output,
                                            audio_stream_type_t stream,
                                            int session)
{
    ALOGV("stopOutput() output %d, stream %d, session %d", output, stream, session);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        ALOGW("stopOutput() unknown output %d", output);
        return BAD_VALUE;
    }

    sp<AudioOutputDescriptor> outputDesc = mOutputs.valueAt(index);

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
            audio_devices_t newDevice = getNewOutputDevice(output, false /*fromCache*/);
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
                sp<AudioOutputDescriptor> desc = mOutputs.valueAt(i);
                if (curOutput != output &&
                        desc->isActive() &&
                        outputDesc->sharesHwModuleWith(desc) &&
                        (newDevice != desc->device())) {
                    setOutputDevice(curOutput,
                                    getNewOutputDevice(curOutput, false /*fromCache*/),
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

audio_io_handle_t AudioPolicyManagerCustom::getInput(audio_source_t inputSource,
                                    uint32_t samplingRate,
                                    audio_format_t format,
                                    audio_channel_mask_t channelMask,
                                    audio_session_t session,
                                    audio_input_flags_t flags)
{
    ALOGV("getInput() inputSource %d, samplingRate %d, format %d, channelMask %x, session %d, "
          "flags %#x",
          inputSource, samplingRate, format, channelMask, session, flags);

    audio_devices_t device = getDeviceForInputSource(inputSource);

    if (device == AUDIO_DEVICE_NONE) {
        ALOGW("getInput() could not find device for inputSource %d", inputSource);
        return AUDIO_IO_HANDLE_NONE;
    }

    /*The below code is intentionally not ported.
    It's not needed to update the channel mask based on source because
    the source is sent to audio HAL through set_parameters().
    For example, if source = VOICE_CALL, does not mean we need to capture two channels.
    If the sound recorder app selects AMR as encoding format but source as RX+TX,
    we need both in ONE channel. So we use the channels set by the app and use source
    to tell the driver what needs to captured (RX only, TX only, or RX+TX ).*/
    // adapt channel selection to input source
    /*switch (inputSource) {
    case AUDIO_SOURCE_VOICE_UPLINK:
        channelMask = AUDIO_CHANNEL_IN_VOICE_UPLINK;
        break;
    case AUDIO_SOURCE_VOICE_DOWNLINK:
        channelMask = AUDIO_CHANNEL_IN_VOICE_DNLINK;
        break;
    case AUDIO_SOURCE_VOICE_CALL:
        channelMask = AUDIO_CHANNEL_IN_VOICE_UPLINK | AUDIO_CHANNEL_IN_VOICE_DNLINK;
        break;
    default:
        break;
    }*/

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
        if((AUDIO_MODE_IN_CALL == mPhoneState) ||
           ((AUDIO_MODE_IN_CALL == mPrevPhoneState) &&
             (AUDIO_MODE_IN_COMMUNICATION == mPhoneState)))
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
        if((AUDIO_MODE_IN_CALL == mPhoneState) ||
           ((AUDIO_MODE_IN_CALL == mPrevPhoneState) &&
             (AUDIO_MODE_IN_COMMUNICATION == mPhoneState)))
        {
            if(inputSource == AUDIO_SOURCE_VOICE_COMMUNICATION) {
                ALOGD("BLOCKING VoIP request during incall mode for inputSource: %d ",inputSource);
                return 0;
            }
        }
    }

#endif

    audio_io_handle_t input = AUDIO_IO_HANDLE_NONE;
    bool isSoundTrigger = false;
    audio_source_t halInputSource = inputSource;
    if (inputSource == AUDIO_SOURCE_HOTWORD) {
        ssize_t index = mSoundTriggerSessions.indexOfKey(session);
        if (index >= 0) {
            input = mSoundTriggerSessions.valueFor(session);
            isSoundTrigger = true;
            flags = (audio_input_flags_t)(flags | AUDIO_INPUT_FLAG_HW_HOTWORD);
            ALOGV("SoundTrigger capture on session %d input %d", session, input);
        } else {
            halInputSource = AUDIO_SOURCE_VOICE_RECOGNITION;
        }
    }

    sp<IOProfile> profile = getInputProfile(device,
                                         samplingRate,
                                         format,
                                         channelMask,
                                         flags);
    if (profile == 0) {
        //retry without flags
        audio_input_flags_t log_flags = flags;
        flags = AUDIO_INPUT_FLAG_NONE;
        profile = getInputProfile(device,
                                 samplingRate,
                                 format,
                                 channelMask,
                                 flags);
        if (profile == 0) {
            ALOGW("getInput() could not find profile for device 0x%X, samplingRate %u, format %#x, "
                    "channelMask 0x%X, flags %#x",
                    device, samplingRate, format, channelMask, log_flags);
            return AUDIO_IO_HANDLE_NONE;
        }
    }

    if (profile->mModule->mHandle == 0) {
        ALOGE("getInput(): HW module %s not opened", profile->mModule->mName);
        return AUDIO_IO_HANDLE_NONE;
    }

    audio_config_t config = AUDIO_CONFIG_INITIALIZER;
    config.sample_rate = samplingRate;
    config.channel_mask = channelMask;
    config.format = format;

    status_t status = mpClientInterface->openInput(profile->mModule->mHandle,
                                                   &input,
                                                   &config,
                                                   &device,
                                                   String8(""),
                                                   halInputSource,
                                                   flags);

    // only accept input with the exact requested set of parameters
    if (status != NO_ERROR ||
        (samplingRate != config.sample_rate) ||
        (format != config.format) ||
        (channelMask != config.channel_mask)) {
        ALOGW("getInput() failed opening input: samplingRate %d, format %d, channelMask %x",
                samplingRate, format, channelMask);
        if (input != AUDIO_IO_HANDLE_NONE) {
            mpClientInterface->closeInput(input);
        }
        return AUDIO_IO_HANDLE_NONE;
    }

    sp<AudioInputDescriptor> inputDesc = new AudioInputDescriptor(profile);
    inputDesc->mInputSource = inputSource;
    inputDesc->mRefCount = 0;
    inputDesc->mOpenRefCount = 1;
    inputDesc->mSamplingRate = samplingRate;
    inputDesc->mFormat = format;
    inputDesc->mChannelMask = channelMask;
    inputDesc->mDevice = device;
    inputDesc->mSessions.add(session);
    inputDesc->mIsSoundTrigger = isSoundTrigger;

    addInput(input, inputDesc);
    mpClientInterface->onAudioPortListUpdate();
    return input;
}

status_t AudioPolicyManagerCustom::startInput(audio_io_handle_t input,
                                        audio_session_t session)
{
    ALOGV("startInput() input %d", input);
    ssize_t index = mInputs.indexOfKey(input);
    if (index < 0) {
        ALOGW("startInput() unknown input %d", input);
        return BAD_VALUE;
    }
    sp<AudioInputDescriptor> inputDesc = mInputs.valueAt(index);

    index = inputDesc->mSessions.indexOf(session);
    if (index < 0) {
        ALOGW("startInput() unknown session %d on input %d", session, input);
        return BAD_VALUE;
    }

    // virtual input devices are compatible with other input devices
    if (!isVirtualInputDevice(inputDesc->mDevice)) {

        // for a non-virtual input device, check if there is another (non-virtual) active input
        audio_io_handle_t activeInput = getActiveInput();
        if (activeInput != 0 && activeInput != input) {

            // If the already active input uses AUDIO_SOURCE_HOTWORD then it is closed,
            // otherwise the active input continues and the new input cannot be started.
            sp<AudioInputDescriptor> activeDesc = mInputs.valueFor(activeInput);
            if (activeDesc->mInputSource == AUDIO_SOURCE_HOTWORD) {
                ALOGW("startInput(%d) preempting low-priority input %d", input, activeInput);
                stopInput(activeInput, activeDesc->mSessions.itemAt(0));
                releaseInput(activeInput, activeDesc->mSessions.itemAt(0));
            } else {
                ALOGE("startInput(%d) failed: other input %d already started", input, activeInput);
                return INVALID_OPERATION;
            }
        }
    }

#ifdef RECORD_PLAY_CONCURRENCY
    mIsInputRequestOnProgress = true;

    char getPropValue[PROPERTY_VALUE_MAX];
    bool prop_rec_play_enabled = false;

    if (property_get("rec.playback.conc.disabled", getPropValue, NULL)) {
        prop_rec_play_enabled = atoi(getPropValue) || !strncmp("true", getPropValue, 4);
    }

    if ((prop_rec_play_enabled) &&(activeInputsCount() == 0)){
        // send update to HAL on record playback concurrency
        AudioParameter param = AudioParameter();
        param.add(String8("rec_play_conc_on"), String8("true"));
        ALOGD("startInput() setParameters rec_play_conc is setting to ON ");
        mpClientInterface->setParameters(0, param.toString());

        // Call invalidate to reset all opened non ULL audio tracks
        // Move tracks associated to this strategy from previous output to new output
        for (int i = AUDIO_STREAM_SYSTEM; i < (int)AUDIO_STREAM_CNT; i++) {
            // Do not call invalidate for ENFORCED_AUDIBLE (otherwise pops are seen for camcorder)
            if (i != AUDIO_STREAM_ENFORCED_AUDIBLE) {
               ALOGD("Invalidate on releaseInput for stream :: %d ", i);
               //FIXME see fixme on name change
               mpClientInterface->invalidateStream((audio_stream_type_t)i);
            }
        }
        // close compress tracks
        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<AudioOutputDescriptor> outputDesc = mOutputs.valueAt(i);
            if ((outputDesc == NULL) || (outputDesc->mProfile == NULL)) {
               ALOGD("ouput desc / profile is NULL");
               continue;
            }
            if (outputDesc->mProfile->mFlags
                            & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
                // close compress  sessions
                ALOGD("calling closeOutput on record conc for COMPRESS output");
                closeOutput(mOutputs.keyAt(i));
            }
        }
    }
#endif

    if (inputDesc->mRefCount == 0) {
        if (activeInputsCount() == 0) {
            SoundTrigger::setCaptureState(true);
        }
        setInputDevice(input, getNewInputDevice(input), true /* force */);

        // Automatically enable the remote submix output when input is started.
        // For remote submix (a virtual device), we open only one input per capture request.
        if (audio_is_remote_submix_device(inputDesc->mDevice)) {
            setDeviceConnectionState(AUDIO_DEVICE_OUT_REMOTE_SUBMIX,
                    AUDIO_POLICY_DEVICE_STATE_AVAILABLE, AUDIO_REMOTE_SUBMIX_DEVICE_ADDRESS);
        }
    }

    ALOGV("AudioPolicyManagerCustom::startInput() input source = %d", inputDesc->mInputSource);

    inputDesc->mRefCount++;
#ifdef RECORD_PLAY_CONCURRENCY
    mIsInputRequestOnProgress = false;
#endif
    return NO_ERROR;
}

status_t AudioPolicyManagerCustom::stopInput(audio_io_handle_t input,
                                       audio_session_t session)
{
    ALOGV("stopInput() input %d", input);
    ssize_t index = mInputs.indexOfKey(input);
    if (index < 0) {
        ALOGW("stopInput() unknown input %d", input);
        return BAD_VALUE;
    }
    sp<AudioInputDescriptor> inputDesc = mInputs.valueAt(index);

    index = inputDesc->mSessions.indexOf(session);
    if (index < 0) {
        ALOGW("stopInput() unknown session %d on input %d", session, input);
        return BAD_VALUE;
    }

    if (inputDesc->mRefCount == 0) {
        ALOGW("stopInput() input %d already stopped", input);
        return INVALID_OPERATION;
    }

    inputDesc->mRefCount--;
    if (inputDesc->mRefCount == 0) {

        // automatically disable the remote submix output when input is stopped
        if (audio_is_remote_submix_device(inputDesc->mDevice)) {
            setDeviceConnectionState(AUDIO_DEVICE_OUT_REMOTE_SUBMIX,
                    AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE, AUDIO_REMOTE_SUBMIX_DEVICE_ADDRESS);
        }

        resetInputDevice(input);

        if (activeInputsCount() == 0) {
            SoundTrigger::setCaptureState(false);
        }
    }

#ifdef RECORD_PLAY_CONCURRENCY
    char propValue[PROPERTY_VALUE_MAX];
    bool prop_rec_play_enabled = false;

    if (property_get("rec.playback.conc.disabled", propValue, NULL)) {
        prop_rec_play_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if ((prop_rec_play_enabled) && (activeInputsCount() == 0)) {

        //send update to HAL on record playback concurrency
        AudioParameter param = AudioParameter();
        param.add(String8("rec_play_conc_on"), String8("false"));
        ALOGD("stopInput() setParameters rec_play_conc is setting to OFF ");
        mpClientInterface->setParameters(0, param.toString());

        //call invalidate tracks so that any open streams can fall back to deep buffer/compress path from ULL
        for (int i = AUDIO_STREAM_SYSTEM; i < (int)AUDIO_STREAM_CNT; i++) {
            //Do not call invalidate for ENFORCED_AUDIBLE (otherwise pops are seen for camcorder stop tone)
            if (i != AUDIO_STREAM_ENFORCED_AUDIBLE) {
               ALOGD(" Invalidate on stopInput for stream :: %d ", i);
               //FIXME see fixme on name change
               mpClientInterface->invalidateStream((audio_stream_type_t)i);
            }
        }
    }
#endif
    return NO_ERROR;
}

status_t AudioPolicyManagerCustom::setStreamVolumeIndex(audio_stream_type_t stream,
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
    if (device == AUDIO_DEVICE_OUT_DEFAULT) {
        mStreams[stream].mIndexCur.clear();
    }
    mStreams[stream].mIndexCur.add(device, index);

    // compute and apply stream volume on all outputs according to connected device
    status_t status = NO_ERROR;
    for (size_t i = 0; i < mOutputs.size(); i++) {
        audio_devices_t curDevice =
                getDeviceForVolume(mOutputs.valueAt(i)->device());
#ifdef AUDIO_EXTN_FM_ENABLED
        audio_devices_t availableOutputDeviceTypes = mAvailableOutputDevices.types();
        if (((device == AUDIO_DEVICE_OUT_DEFAULT) &&
              ((availableOutputDeviceTypes & AUDIO_DEVICE_OUT_FM) != AUDIO_DEVICE_OUT_FM)) ||
              (device == curDevice)) {
#else
        if ((device == AUDIO_DEVICE_OUT_DEFAULT) || (device == curDevice)) {
#endif
            status_t volStatus = checkAndSetVolume(stream, index, mOutputs.keyAt(i), curDevice);
            if (volStatus != NO_ERROR) {
                status = volStatus;
            }
        }
    }
    return status;
}

// This function checks for the parameters which can be offloaded.
// This can be enhanced depending on the capability of the DSP and policy
// of the system.
bool AudioPolicyManagerCustom::isOffloadSupported(const audio_offload_info_t& offloadInfo)
{
    ALOGD("copl: isOffloadSupported: SR=%u, CM=0x%x, Format=0x%x, StreamType=%d,"
     " BitRate=%u, duration=%lld us, has_video=%d",
     offloadInfo.sample_rate, offloadInfo.channel_mask,
     offloadInfo.format,
     offloadInfo.stream_type, offloadInfo.bit_rate, offloadInfo.duration_us,
     offloadInfo.has_video);

#ifdef VOICE_CONCURRENCY
    char concpropValue[PROPERTY_VALUE_MAX];
    if (property_get("voice.playback.conc.disabled", concpropValue, NULL)) {
         bool propenabled = atoi(concpropValue) || !strncmp("true", concpropValue, 4);
         if (propenabled) {
            if (isInCall())
            {
                ALOGD("\n copl: blocking  compress offload on call mode\n");
                return false;
            }
         }
    }
#endif
#ifdef RECORD_PLAY_CONCURRENCY
    char recConcPropValue[PROPERTY_VALUE_MAX];
    bool prop_rec_play_enabled = false;

    if (property_get("rec.playback.conc.disabled", recConcPropValue, NULL)) {
        prop_rec_play_enabled = atoi(recConcPropValue) || !strncmp("true", recConcPropValue, 4);
    }

    if ((prop_rec_play_enabled) &&
         ((true == mIsInputRequestOnProgress) || (activeInputsCount() > 0))) {
        ALOGD("copl: blocking  compress offload for record concurrency");
        return false;
    }
#endif
    // Check if stream type is music, then only allow offload as of now.
    if (offloadInfo.stream_type != AUDIO_STREAM_MUSIC)
    {
        ALOGD("isOffloadSupported: stream_type != MUSIC, returning false");
        return false;
    }

    char propValue[PROPERTY_VALUE_MAX];
    bool pcmOffload = false;
#ifdef PCM_OFFLOAD_ENABLED
    if (audio_is_offload_pcm(offloadInfo.format)) {
        if(property_get("audio.offload.pcm.enable", propValue, NULL)) {
            bool prop_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
            if (prop_enabled) {
                ALOGW("PCM offload property is enabled");
                pcmOffload = true;
            }
        }
        if (!pcmOffload) {
            ALOGD("PCM offload disabled by property audio.offload.pcm.enable");
            return false;
        }
    }
#endif

    if (!pcmOffload) {
        // Check if offload has been disabled
        if (property_get("audio.offload.disable", propValue, "0")) {
            if (atoi(propValue) != 0) {
                ALOGD("offload disabled by audio.offload.disable=%s", propValue );
                return false;
            }
        }

        //check if it's multi-channel AAC (includes sub formats) and FLAC format
        if ((popcount(offloadInfo.channel_mask) > 2) &&
            (((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_FLAC))) {
            ALOGD("offload disabled for multi-channel AAC and FLAC format");
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
        if ((offloadInfo.format == AUDIO_FORMAT_MP3) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_FLAC) ||
            pcmOffload)
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
    sp<IOProfile> profile = getProfileForDirectOutput(AUDIO_DEVICE_NONE /*ignore device */,
                                            offloadInfo.sample_rate,
                                            offloadInfo.format,
                                            offloadInfo.channel_mask,
                                            AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD);
    ALOGD("copl: isOffloadSupported() profile %sfound", profile != 0 ? "" : "NOT ");
    return (profile != 0);
}

uint32_t AudioPolicyManagerCustom::nextUniqueId()
{
    return android_atomic_inc(&mNextUniqueId);
}

AudioPolicyManagerCustom::routing_strategy AudioPolicyManagerCustom::getStrategy(
        audio_stream_type_t stream) {
    // stream to strategy mapping
    switch (stream) {
    case AUDIO_STREAM_VOICE_CALL:
    case AUDIO_STREAM_BLUETOOTH_SCO:
        return STRATEGY_PHONE;
    case AUDIO_STREAM_RING:
    case AUDIO_STREAM_ALARM:
        return STRATEGY_SONIFICATION;
    case AUDIO_STREAM_NOTIFICATION:
        return STRATEGY_SONIFICATION_RESPECTFUL;
    case AUDIO_STREAM_DTMF:
        return STRATEGY_DTMF;
    default:
        ALOGE("unknown stream type");
    case AUDIO_STREAM_SYSTEM:
        // NOTE: SYSTEM stream uses MEDIA strategy because muting music and switching outputs
        // while key clicks are played produces a poor result
    case AUDIO_STREAM_TTS:
    case AUDIO_STREAM_MUSIC:
#ifdef AUDIO_EXTN_INCALL_MUSIC_ENABLED
    case AUDIO_STREAM_INCALL_MUSIC:
#endif
        return STRATEGY_MEDIA;
    case AUDIO_STREAM_ENFORCED_AUDIBLE:
        return STRATEGY_ENFORCED_AUDIBLE;
    }
}

void AudioPolicyManagerCustom::handleNotificationRoutingForStream(audio_stream_type_t stream) {
    switch(stream) {
    case AUDIO_STREAM_MUSIC:
        checkOutputForStrategy(STRATEGY_SONIFICATION_RESPECTFUL);
        updateDevicesAndOutputs();
        break;
    default:
        break;
    }
}

audio_devices_t AudioPolicyManagerCustom::getDeviceForStrategy(routing_strategy strategy,
                                                             bool fromCache)
{
    uint32_t device = AUDIO_DEVICE_NONE;

    if (fromCache) {
        ALOGVV("getDeviceForStrategy() from cache strategy %d, device %x",
              strategy, mDeviceForStrategy[strategy]);
        return mDeviceForStrategy[strategy];
    }
    audio_devices_t availableOutputDeviceTypes = mAvailableOutputDevices.types();
    switch (strategy) {

    case STRATEGY_SONIFICATION_RESPECTFUL:
        if (isInCall()) {
            device = getDeviceForStrategy(STRATEGY_SONIFICATION, false /*fromCache*/);
        } else if (isStreamActiveRemotely(AUDIO_STREAM_MUSIC,
                SONIFICATION_RESPECTFUL_AFTER_MUSIC_DELAY)) {
            // while media is playing on a remote device, use the the sonification behavior.
            // Note that we test this usecase before testing if media is playing because
            //   the isStreamActive() method only informs about the activity of a stream, not
            //   if it's for local playback. Note also that we use the same delay between both tests
            device = getDeviceForStrategy(STRATEGY_SONIFICATION, false /*fromCache*/);
            //user "safe" speaker if available instead of normal speaker to avoid triggering
            //other acoustic safety mechanisms for notification
            if (device == AUDIO_DEVICE_OUT_SPEAKER && (availableOutputDeviceTypes & AUDIO_DEVICE_OUT_SPEAKER_SAFE))
                device = AUDIO_DEVICE_OUT_SPEAKER_SAFE;
        } else if (isStreamActive(AUDIO_STREAM_MUSIC, SONIFICATION_RESPECTFUL_AFTER_MUSIC_DELAY)) {
            // while media is playing (or has recently played), use the same device
            device = getDeviceForStrategy(STRATEGY_MEDIA, false /*fromCache*/);
        } else {
            // when media is not playing anymore, fall back on the sonification behavior
            device = getDeviceForStrategy(STRATEGY_SONIFICATION, false /*fromCache*/);
            //user "safe" speaker if available instead of normal speaker to avoid triggering
            //other acoustic safety mechanisms for notification
            if (device == AUDIO_DEVICE_OUT_SPEAKER && (availableOutputDeviceTypes & AUDIO_DEVICE_OUT_SPEAKER_SAFE))
                device = AUDIO_DEVICE_OUT_SPEAKER_SAFE;
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
        // Force use of only devices on primary output if:
        // - in call AND
        //   - cannot route from voice call RX OR
        //   - audio HAL version is < 3.0 and TX device is on the primary HW module
        if (mPhoneState == AUDIO_MODE_IN_CALL) {
            audio_devices_t txDevice = getDeviceForInputSource(AUDIO_SOURCE_VOICE_COMMUNICATION);
            sp<AudioOutputDescriptor> hwOutputDesc = mOutputs.valueFor(mPrimaryOutput);
            if (((mAvailableInputDevices.types() &
                    AUDIO_DEVICE_IN_TELEPHONY_RX & ~AUDIO_DEVICE_BIT_IN) == 0) ||
                    (((txDevice & availablePrimaryInputDevices() & ~AUDIO_DEVICE_BIT_IN) != 0) &&
                         (hwOutputDesc->getAudioPort()->mModule->mHalVersion <
                             AUDIO_DEVICE_API_VERSION_3_0))) {
                availableOutputDeviceTypes = availablePrimaryOutputDevices();
            }
        }
        // for phone strategy, we first consider the forced use and then the available devices by order
        // of priority
        switch (mForceUse[AUDIO_POLICY_FORCE_FOR_COMMUNICATION]) {
        case AUDIO_POLICY_FORCE_BT_SCO:
            if (!isInCall() || strategy != STRATEGY_DTMF) {
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT;
                if (device) break;
            }
            device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET;
            if (device) break;
            device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_BLUETOOTH_SCO;
            if (device) break;
            // if SCO device is requested but no SCO device is available, fall back to default case
            // FALL THROUGH

        default:    // FORCE_NONE
            // when not in a phone call, phone strategy should route STREAM_VOICE_CALL to A2DP
            if (!isInCall() &&
                    (mForceUse[AUDIO_POLICY_FORCE_FOR_MEDIA] != AUDIO_POLICY_FORCE_NO_BT_A2DP) &&
                    (getA2dpOutput() != 0) && !mA2dpSuspended) {
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP;
                if (device) break;
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
                if (device) break;
            }
            device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
            if (device) break;
            device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_WIRED_HEADSET;
            if (device) break;
            device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_USB_DEVICE;
            if (device) break;
            if (mPhoneState != AUDIO_MODE_IN_CALL) {
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_USB_ACCESSORY;
                if (device) break;
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET;
                if (device) break;
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_AUX_DIGITAL;
                if (device) break;
            }

            // Allow voice call on USB ANLG DOCK headset
            device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET;
            if (device) break;

            device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_EARPIECE;
            if (device) break;
            device = mDefaultOutputDevice->mDeviceType;
            if (device == AUDIO_DEVICE_NONE) {
                ALOGE("getDeviceForStrategy() no device found for STRATEGY_PHONE");
            }
            break;

        case AUDIO_POLICY_FORCE_SPEAKER:
            // when not in a phone call, phone strategy should route STREAM_VOICE_CALL to
            // A2DP speaker when forcing to speaker output
            if (!isInCall() &&
                    (mForceUse[AUDIO_POLICY_FORCE_FOR_MEDIA] != AUDIO_POLICY_FORCE_NO_BT_A2DP) &&
                    (getA2dpOutput() != 0) && !mA2dpSuspended) {
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
                if (device) break;
            }
            if (mPhoneState != AUDIO_MODE_IN_CALL) {
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_USB_ACCESSORY;
                if (device) break;
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_USB_DEVICE;
                if (device) break;
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET;
                if (device) break;
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_AUX_DIGITAL;
                if (device) break;
                device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET;
                if (device) break;
            }
            device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_LINE;
            if (device) break;
            device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_SPEAKER;
            if (device) break;
            device = mDefaultOutputDevice->mDeviceType;
            if (device == AUDIO_DEVICE_NONE) {
                ALOGE("getDeviceForStrategy() no device found for STRATEGY_PHONE, FORCE_SPEAKER");
            }
            break;
        }

        if (isInCall() && (device == AUDIO_DEVICE_NONE)) {
            // when in call, get the device for Phone strategy
            device = getDeviceForStrategy(STRATEGY_PHONE, false /*fromCache*/);
            break;
        }

#ifdef AUDIO_EXTN_FM_ENABLED
        if (availableOutputDeviceTypes & AUDIO_DEVICE_OUT_FM) {
            if (mForceUse[AUDIO_POLICY_FORCE_FOR_MEDIA] == AUDIO_POLICY_FORCE_SPEAKER) {
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
                (mForceUse[AUDIO_POLICY_FORCE_FOR_SYSTEM] == AUDIO_POLICY_FORCE_SYSTEM_ENFORCED)) {
            device = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_SPEAKER;
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
        if (mForceUse[AUDIO_POLICY_FORCE_FOR_MEDIA] == AUDIO_POLICY_FORCE_SPEAKER) {
            device = AUDIO_DEVICE_OUT_SPEAKER;
            break;
        }
#endif

        if (strategy != STRATEGY_SONIFICATION) {
            // no sonification on remote submix (e.g. WFD)
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_REMOTE_SUBMIX;
        }
        if ((device2 == AUDIO_DEVICE_NONE) &&
                (mForceUse[AUDIO_POLICY_FORCE_FOR_MEDIA] != AUDIO_POLICY_FORCE_NO_BT_A2DP) &&
                (getA2dpOutput() != 0) && !mA2dpSuspended) {
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP;
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
            }
            if (device2 == AUDIO_DEVICE_NONE) {
                device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
            }
        }
        if (device2 == AUDIO_DEVICE_NONE) {
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
        }
        if ((device2 == AUDIO_DEVICE_NONE)) {
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_LINE;
        }
        if (device2 == AUDIO_DEVICE_NONE) {
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_WIRED_HEADSET;
        }
        if (device2 == AUDIO_DEVICE_NONE) {
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_USB_ACCESSORY;
        }
        if (device2 == AUDIO_DEVICE_NONE) {
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_USB_DEVICE;
        }
        if (device2 == AUDIO_DEVICE_NONE) {
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET;
        }
        if ((strategy != STRATEGY_SONIFICATION) && (device == AUDIO_DEVICE_NONE)
             && (device2 == AUDIO_DEVICE_NONE)) {
            // no sonification on aux digital (e.g. HDMI)
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_AUX_DIGITAL;
        }
        if ((device2 == AUDIO_DEVICE_NONE) &&
                (mForceUse[AUDIO_POLICY_FORCE_FOR_DOCK] == AUDIO_POLICY_FORCE_ANALOG_DOCK)
                && (strategy != STRATEGY_SONIFICATION)) {
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET;
        }
#ifdef AUDIO_EXTN_FM_ENABLED
            if ((strategy != STRATEGY_SONIFICATION) && (device == AUDIO_DEVICE_NONE)
                 && (device2 == AUDIO_DEVICE_NONE)) {
                device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_FM_TX;
            }
#endif
#ifdef AUDIO_EXTN_AFE_PROXY_ENABLED
            if ((strategy != STRATEGY_SONIFICATION) && (device == AUDIO_DEVICE_NONE)
                 && (device2 == AUDIO_DEVICE_NONE)) {
                // no sonification on WFD sink
                device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_PROXY;
            }
#endif
        if (device2 == AUDIO_DEVICE_NONE) {
            device2 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_SPEAKER;
        }
        int device3 = AUDIO_DEVICE_NONE;
        if (strategy == STRATEGY_MEDIA) {
            // ARC, SPDIF and AUX_LINE can co-exist with others.
            device3 = availableOutputDeviceTypes & AUDIO_DEVICE_OUT_HDMI_ARC;
            device3 |= (availableOutputDeviceTypes & AUDIO_DEVICE_OUT_SPDIF);
            device3 |= (availableOutputDeviceTypes & AUDIO_DEVICE_OUT_AUX_LINE);
        }

        device2 |= device3;
        // device is DEVICE_OUT_SPEAKER if we come from case STRATEGY_SONIFICATION or
        // STRATEGY_ENFORCED_AUDIBLE, AUDIO_DEVICE_NONE otherwise
        device |= device2;

        // If hdmi system audio mode is on, remove speaker out of output list.
        if ((strategy == STRATEGY_MEDIA) &&
            (mForceUse[AUDIO_POLICY_FORCE_FOR_HDMI_SYSTEM_AUDIO] ==
                AUDIO_POLICY_FORCE_HDMI_SYSTEM_AUDIO_ENFORCED)) {
            device &= ~AUDIO_DEVICE_OUT_SPEAKER;
        }

        if (device) break;
        device = mDefaultOutputDevice->mDeviceType;
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

audio_devices_t AudioPolicyManagerCustom::getDeviceForInputSource(audio_source_t inputSource)
{
    uint32_t device = AUDIO_DEVICE_NONE;
    audio_devices_t availableDeviceTypes = mAvailableInputDevices.types() &
                                            ~AUDIO_DEVICE_BIT_IN;
    switch (inputSource) {
    case AUDIO_SOURCE_VOICE_UPLINK:
      if (availableDeviceTypes & AUDIO_DEVICE_IN_VOICE_CALL) {
          device = AUDIO_DEVICE_IN_VOICE_CALL;
          break;
      }
      break;

    case AUDIO_SOURCE_DEFAULT:
    case AUDIO_SOURCE_MIC:
    if (availableDeviceTypes & AUDIO_DEVICE_IN_BLUETOOTH_A2DP) {
        device = AUDIO_DEVICE_IN_BLUETOOTH_A2DP;
    } else if (availableDeviceTypes & AUDIO_DEVICE_IN_WIRED_HEADSET) {
        device = AUDIO_DEVICE_IN_WIRED_HEADSET;
    } else if (availableDeviceTypes & AUDIO_DEVICE_IN_USB_DEVICE) {
        device = AUDIO_DEVICE_IN_USB_DEVICE;
    } else if (availableDeviceTypes & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        device = AUDIO_DEVICE_IN_BUILTIN_MIC;
    }
    break;

    case AUDIO_SOURCE_VOICE_COMMUNICATION:
        // Allow only use of devices on primary input if in call and HAL does not support routing
        // to voice call path.
        if ((mPhoneState == AUDIO_MODE_IN_CALL) &&
                (mAvailableOutputDevices.types() & AUDIO_DEVICE_OUT_TELEPHONY_TX) == 0) {
            availableDeviceTypes = availablePrimaryInputDevices() & ~AUDIO_DEVICE_BIT_IN;
        }

        switch (mForceUse[AUDIO_POLICY_FORCE_FOR_COMMUNICATION]) {
        case AUDIO_POLICY_FORCE_BT_SCO:
            // if SCO device is requested but no SCO device is available, fall back to default case
            if (availableDeviceTypes & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
                device = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
                break;
            }
            // FALL THROUGH

        default:    // FORCE_NONE
            if (availableDeviceTypes & AUDIO_DEVICE_IN_WIRED_HEADSET) {
                device = AUDIO_DEVICE_IN_WIRED_HEADSET;
            } else if (availableDeviceTypes & AUDIO_DEVICE_IN_USB_DEVICE) {
                device = AUDIO_DEVICE_IN_USB_DEVICE;
            } else if (availableDeviceTypes & AUDIO_DEVICE_IN_BUILTIN_MIC) {
                device = AUDIO_DEVICE_IN_BUILTIN_MIC;
            }
            break;

        case AUDIO_POLICY_FORCE_SPEAKER:
            if (availableDeviceTypes & AUDIO_DEVICE_IN_BACK_MIC) {
                device = AUDIO_DEVICE_IN_BACK_MIC;
            } else if (availableDeviceTypes & AUDIO_DEVICE_IN_BUILTIN_MIC) {
                device = AUDIO_DEVICE_IN_BUILTIN_MIC;
            }
            break;
        }
        break;

    case AUDIO_SOURCE_VOICE_RECOGNITION:
    case AUDIO_SOURCE_HOTWORD:
        if (mForceUse[AUDIO_POLICY_FORCE_FOR_RECORD] == AUDIO_POLICY_FORCE_BT_SCO &&
                availableDeviceTypes & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            device = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else if (availableDeviceTypes & AUDIO_DEVICE_IN_WIRED_HEADSET) {
            device = AUDIO_DEVICE_IN_WIRED_HEADSET;
        } else if (availableDeviceTypes & AUDIO_DEVICE_IN_USB_DEVICE) {
            device = AUDIO_DEVICE_IN_USB_DEVICE;
        } else if (availableDeviceTypes & AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET) {
            device = AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET;
        } else if (availableDeviceTypes & AUDIO_DEVICE_IN_BUILTIN_MIC) {
            device = AUDIO_DEVICE_IN_BUILTIN_MIC;
        }
        break;
    case AUDIO_SOURCE_CAMCORDER:
        if (availableDeviceTypes & AUDIO_DEVICE_IN_BACK_MIC) {
            device = AUDIO_DEVICE_IN_BACK_MIC;
        } else if (availableDeviceTypes & AUDIO_DEVICE_IN_BUILTIN_MIC) {
            device = AUDIO_DEVICE_IN_BUILTIN_MIC;
        }
        break;
    case AUDIO_SOURCE_VOICE_DOWNLINK:
    case AUDIO_SOURCE_VOICE_CALL:
        if (availableDeviceTypes & AUDIO_DEVICE_IN_VOICE_CALL) {
            device = AUDIO_DEVICE_IN_VOICE_CALL;
        }
        break;
    case AUDIO_SOURCE_REMOTE_SUBMIX:
        if (availableDeviceTypes & AUDIO_DEVICE_IN_REMOTE_SUBMIX) {
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

bool AudioPolicyManagerCustom::isVirtualInputDevice(audio_devices_t device)
{
    if ((device & AUDIO_DEVICE_BIT_IN) != 0) {
        device &= ~AUDIO_DEVICE_BIT_IN;
        if ((popcount(device) == 1) && ((device & ~APM_AUDIO_IN_DEVICE_VIRTUAL_ALL) == 0))
            return true;
    }
    return false;
}

bool AudioPolicyManagerCustom::deviceDistinguishesOnAddress(audio_devices_t device) {
    return ((device & APM_AUDIO_DEVICE_MATCH_ADDRESS_ALL) != 0);
}

AudioPolicyManagerCustom::device_category AudioPolicyManagerCustom::getDeviceCategory(audio_devices_t device)
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
        case AUDIO_DEVICE_OUT_LINE:
        case AUDIO_DEVICE_OUT_AUX_DIGITAL:
        /*USB?  Remote submix?*/
            return DEVICE_CATEGORY_EXT_MEDIA;
        case AUDIO_DEVICE_OUT_SPEAKER:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
        case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER:
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

float AudioPolicyManagerCustom::volIndexToAmpl(audio_devices_t device, const StreamDescriptor& streamDesc,
        int indexInUi)
{
    device_category deviceCategory = getDeviceCategory(device);
    const VolumeCurvePoint *curve = streamDesc.mVolumeCurve[deviceCategory];

    // the volume index in the UI is relative to the min and max volume indices for this stream type
    int nbSteps = 1 + curve[VOLMAX].mIndex -
            curve[VOLMIN].mIndex;
    int volIdx = (nbSteps * (indexInUi - streamDesc.mIndexMin)) /
            (streamDesc.mIndexMax - streamDesc.mIndexMin);

    // find what part of the curve this index volume belongs to, or if it's out of bounds
    int segment = 0;
    if (volIdx < curve[VOLMIN].mIndex) {         // out of bounds
        return 0.0f;
    } else if (volIdx < curve[VOLKNEE1].mIndex) {
        segment = 0;
    } else if (volIdx < curve[VOLKNEE2].mIndex) {
        segment = 1;
    } else if (volIdx <= curve[VOLMAX].mIndex) {
        segment = 2;
    } else {                                                               // out of bounds
        return 1.0f;
    }

    // linear interpolation in the attenuation table in dB
    float decibels = curve[segment].mDBAttenuation +
            ((float)(volIdx - curve[segment].mIndex)) *
                ( (curve[segment+1].mDBAttenuation -
                        curve[segment].mDBAttenuation) /
                    ((float)(curve[segment+1].mIndex -
                            curve[segment].mIndex)) );

    float amplification = exp( decibels * 0.115129f); // exp( dB * ln(10) / 20 )

    ALOGVV("VOLUME vol index=[%d %d %d], dB=[%.1f %.1f %.1f] ampl=%.5f",
            curve[segment].mIndex, volIdx,
            curve[segment+1].mIndex,
            curve[segment].mDBAttenuation,
            decibels,
            curve[segment+1].mDBAttenuation,
            amplification);

    return amplification;
}

float AudioPolicyManagerCustom::computeVolume(audio_stream_type_t stream,
                                            int index,
                                            audio_io_handle_t output,
                                            audio_devices_t device)
{
    float volume = 1.0;
    sp<AudioOutputDescriptor> outputDesc = mOutputs.valueFor(output);
    StreamDescriptor &streamDesc = mStreams[stream];

    if (device == AUDIO_DEVICE_NONE) {
        device = outputDesc->device();
    }

    // if volume is not 0 (not muted), force media volume to max on digital output
    if (stream == AUDIO_STREAM_MUSIC &&
        index != mStreams[stream].mIndexMin &&
        (device == AUDIO_DEVICE_OUT_AUX_DIGITAL ||
#ifdef AUDIO_EXTN_AFE_PROXY_ENABLED
         device == AUDIO_DEVICE_OUT_PROXY ||
#endif
         device == AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET)) {
        return 1.0;
    }

#ifdef AUDIO_EXTN_INCALL_MUSIC_ENABLED
    if (stream == AUDIO_STREAM_INCALL_MUSIC) {
        return 1.0;
    }
#endif

    volume = volIndexToAmpl(device, streamDesc, index);

    // if a headset is connected, apply the following rules to ring tones and notifications
    // to avoid sound level bursts in user's ears:
    // - always attenuate ring tones and notifications volume by 6dB
    // - if music is playing, always limit the volume to current music volume,
    // with a minimum threshold at -36dB so that notification is always perceived.
    const routing_strategy stream_strategy = getStrategy(stream);
    if ((device & (AUDIO_DEVICE_OUT_BLUETOOTH_A2DP |
            AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES |
            AUDIO_DEVICE_OUT_WIRED_HEADSET |
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) &&
        ((stream_strategy == STRATEGY_SONIFICATION)
                || (stream_strategy == STRATEGY_SONIFICATION_RESPECTFUL)
                || (stream == AUDIO_STREAM_SYSTEM)
                || ((stream_strategy == STRATEGY_ENFORCED_AUDIBLE) &&
                    (mForceUse[AUDIO_POLICY_FORCE_FOR_SYSTEM] == AUDIO_POLICY_FORCE_NONE))) &&
        streamDesc.mCanBeMuted) {
        volume *= SONIFICATION_HEADSET_VOLUME_FACTOR;
        // when the phone is ringing we must consider that music could have been paused just before
        // by the music application and behave as if music was active if the last music track was
        // just stopped
        if (isStreamActive(AUDIO_STREAM_MUSIC, SONIFICATION_HEADSET_MUSIC_DELAY) ||
                mLimitRingtoneVolume) {
            audio_devices_t musicDevice = getDeviceForStrategy(STRATEGY_MEDIA, true /*fromCache*/);
            float musicVol = computeVolume(AUDIO_STREAM_MUSIC,
                               mStreams[AUDIO_STREAM_MUSIC].getVolumeIndex(musicDevice),
                               output,
                               musicDevice);
            float minVol = (musicVol > SONIFICATION_HEADSET_VOLUME_MIN) ?
                                musicVol : SONIFICATION_HEADSET_VOLUME_MIN;
            if (volume > minVol) {
                volume = minVol;
                ALOGV("computeVolume limiting volume to %f musicVol %f", minVol, musicVol);
            }
        }
    }

    return volume;
}

status_t AudioPolicyManagerCustom::checkAndSetVolume(audio_stream_type_t stream,
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
    if ((stream == AUDIO_STREAM_VOICE_CALL &&
            mForceUse[AUDIO_POLICY_FORCE_FOR_COMMUNICATION] == AUDIO_POLICY_FORCE_BT_SCO) ||
        (stream == AUDIO_STREAM_BLUETOOTH_SCO &&
                mForceUse[AUDIO_POLICY_FORCE_FOR_COMMUNICATION] != AUDIO_POLICY_FORCE_BT_SCO)) {
        ALOGV("checkAndSetVolume() cannot set stream %d volume with force use = %d for comm",
             stream, mForceUse[AUDIO_POLICY_FORCE_FOR_COMMUNICATION]);
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
        if (stream == AUDIO_STREAM_BLUETOOTH_SCO) {
            mpClientInterface->setStreamVolume(AUDIO_STREAM_VOICE_CALL, volume, output, delayMs);
#ifdef AUDIO_EXTN_FM_ENABLED
        } else if (stream == AUDIO_STREAM_MUSIC &&
                   output == mPrimaryOutput) {
            if (volume >= 0) {
                AudioParameter param = AudioParameter();
                param.addFloat(String8("fm_volume"), volume);
                ALOGV("checkAndSetVolume setParameters volume, volume=:%f delay=:%d",volume,delayMs*2);
                //Double delayMs to avoid sound burst while device switch.
                mpClientInterface->setParameters(mPrimaryOutput, param.toString(), delayMs*2);
            }
#endif
        }
        mpClientInterface->setStreamVolume(stream, volume, output, delayMs);
    }

    if (stream == AUDIO_STREAM_VOICE_CALL ||
        stream == AUDIO_STREAM_BLUETOOTH_SCO) {
        float voiceVolume;
        // Force voice volume to max for bluetooth SCO as volume is managed by the headset
        if (stream == AUDIO_STREAM_VOICE_CALL) {
            voiceVolume = (float)index/(float)mStreams[stream].mIndexMax;
        } else {
            voiceVolume = 1.0;
        }

        if (voiceVolume != mLastVoiceVolume && ((output == mPrimaryOutput) ||
            isDirectOutput(output))) {
            mpClientInterface->setVoiceVolume(voiceVolume, delayMs);
            mLastVoiceVolume = voiceVolume;
        }
    }

    return NO_ERROR;
}

bool AudioPolicyManagerCustom::isStateInCall(int state) {
    return ((state == AUDIO_MODE_IN_CALL) || (state == AUDIO_MODE_IN_COMMUNICATION) ||
       ((state == AUDIO_MODE_RINGTONE) && (mPrevPhoneState == AUDIO_MODE_IN_CALL)));
}


extern "C" AudioPolicyInterface* createAudioPolicyManager(
        AudioPolicyClientInterface *clientInterface)
{
    return new AudioPolicyManager(clientInterface);
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface *interface)
{
    delete interface;
}

}; // namespace android
