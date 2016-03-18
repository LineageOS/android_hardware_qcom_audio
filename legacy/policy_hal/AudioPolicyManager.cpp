/*
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "AudioPolicyManagerCustom"
//#define LOG_NDEBUG 0

//#define VERY_VERBOSE_LOGGING
#ifdef VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <inttypes.h>
#include <math.h>

#include <cutils/properties.h>
#include <utils/Log.h>
#include <hardware/audio.h>
#include <hardware/audio_effect.h>
#include <media/AudioParameter.h>
#include <soundtrigger/SoundTrigger.h>
#include "AudioPolicyManager.h"
#include <policy.h>

namespace android {

// ----------------------------------------------------------------------------
// AudioPolicyInterface implementation
// ----------------------------------------------------------------------------
extern "C" AudioPolicyInterface* createAudioPolicyManager(
         AudioPolicyClientInterface *clientInterface)
{
     return new AudioPolicyManagerCustom(clientInterface);
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface *interface)
{
     delete interface;
}

void AudioPolicyManagerCustom::checkA2dpSuspend()
{
    bool isScoConnected =
            ((mAvailableInputDevices.types() & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET &
                    ~AUDIO_DEVICE_BIT_IN) != 0) ||
            ((mAvailableOutputDevices.types() & AUDIO_DEVICE_OUT_ALL_SCO) != 0);
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
        if ((!isScoConnected ||
             ((mEngine->getForceUse(AUDIO_POLICY_FORCE_FOR_COMMUNICATION) != AUDIO_POLICY_FORCE_BT_SCO) &&
              (mEngine->getForceUse(AUDIO_POLICY_FORCE_FOR_RECORD) != AUDIO_POLICY_FORCE_BT_SCO))) &&
             ((mEngine->getPhoneState() != AUDIO_MODE_IN_CALL) &&
              (mEngine->getPhoneState() != AUDIO_MODE_RINGTONE))) {

            mA2dpSuspended = false;
        }
    } else {
        if ((isScoConnected &&
             ((mEngine->getForceUse(AUDIO_POLICY_FORCE_FOR_COMMUNICATION) == AUDIO_POLICY_FORCE_BT_SCO) ||
              (mEngine->getForceUse(AUDIO_POLICY_FORCE_FOR_RECORD) == AUDIO_POLICY_FORCE_BT_SCO))) ||
             ((mEngine->getPhoneState() == AUDIO_MODE_IN_CALL) ||
              (mEngine->getPhoneState() == AUDIO_MODE_RINGTONE))) {

            mA2dpSuspended = true;
        }
    }
}

status_t AudioPolicyManagerCustom::setDeviceConnectionState(audio_devices_t device,
                                                            audio_policy_dev_state_t state,
                                                            const char *device_address,
                                                            const char *device_name)
{
    return setDeviceConnectionStateInt(device, state, device_address, device_name);
}

/* NOTE: setDeviceConnectionStateInt is also called by
 * unregisterPolicyMixes
 * registerPolicyMixes
 * stopInput
 * startInput
 */
status_t AudioPolicyManagerCustom::setDeviceConnectionStateInt(audio_devices_t device,
                                                               audio_policy_dev_state_t state,
                                                               const char *device_address,
                                                               const char *device_name)
{
    ALOGV("setDeviceConnectionStateInt() device: 0x%X, state %d, address %s name %s",
             device, state, device_address, device_name);

    // connect/disconnect only 1 device at a time
    if (!audio_is_output_device(device) && !audio_is_input_device(device)) return BAD_VALUE;

    sp<DeviceDescriptor> devDesc =
            mHwModules.getDeviceDescriptor(device, device_address, device_name);

    // handle output devices
    if (audio_is_output_device(device)) {
        SortedVector <audio_io_handle_t> outputs;

        ssize_t index = mAvailableOutputDevices.indexOf(devDesc);

        // save a copy of the opened output descriptors before any output is opened or closed
        // by checkOutputsForDevice(). This will be needed by checkOutputForAllStrategies()
        mPreviousOutputs = mOutputs;
        switch (state)
        {
        // handle output device connection
        case AUDIO_POLICY_DEVICE_STATE_AVAILABLE: {
            if (index >= 0) {
                ALOGW("setDeviceConnectionState() device already connected: %x", device);
                return INVALID_OPERATION;
            }
            ALOGV("setDeviceConnectionState() connecting device %x", device);

            if (device & AUDIO_DEVICE_OUT_ALL_A2DP) {
               AudioParameter param;
               param.add(String8("a2dp_connected"), String8("true"));
               mpClientInterface->setParameters(0, param.toString());
            }

            if (device & AUDIO_DEVICE_OUT_USB_ACCESSORY) {
               AudioParameter param;
               param.add(String8("usb_connected"), String8("true"));
               mpClientInterface->setParameters(0, param.toString());
            }

            // register new device as available
            index = mAvailableOutputDevices.add(devDesc);
            if (index >= 0) {
                sp<HwModule> module = mHwModules.getModuleForDevice(device);
                if (module == 0) {
                    ALOGD("setDeviceConnectionState() could not find HW module for device %08x",
                          device);
                    mAvailableOutputDevices.remove(devDesc);
                    return INVALID_OPERATION;
                }
                mAvailableOutputDevices[index]->attach(module);
            } else {
                return NO_MEMORY;
            }

            if (checkOutputsForDevice(devDesc, state, outputs, devDesc->mAddress) != NO_ERROR) {
                mAvailableOutputDevices.remove(devDesc);
                return INVALID_OPERATION;
            }
            // Propagate device availability to Engine
            mEngine->setDeviceConnectionState(devDesc, state);

            // outputs should never be empty here
            ALOG_ASSERT(outputs.size() != 0, "setDeviceConnectionState():"
                    "checkOutputsForDevice() returned no outputs but status OK");
            ALOGV("setDeviceConnectionState() checkOutputsForDevice() returned %zu outputs",
                  outputs.size());

            // Send connect to HALs
            AudioParameter param = AudioParameter(devDesc->mAddress);
            param.addInt(String8(AUDIO_PARAMETER_DEVICE_CONNECT), device);
            mpClientInterface->setParameters(AUDIO_IO_HANDLE_NONE, param.toString());

            } break;
        // handle output device disconnection
        case AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE: {
            if (index < 0) {
                ALOGW("setDeviceConnectionState() device not connected: %x", device);
                return INVALID_OPERATION;
            }

            ALOGV("setDeviceConnectionState() disconnecting output device %x", device);

            // Send Disconnect to HALs
            AudioParameter param = AudioParameter(devDesc->mAddress);
            param.addInt(String8(AUDIO_PARAMETER_DEVICE_DISCONNECT), device);
            mpClientInterface->setParameters(AUDIO_IO_HANDLE_NONE, param.toString());

            // remove device from available output devices
            mAvailableOutputDevices.remove(devDesc);

            if (device & AUDIO_DEVICE_OUT_ALL_A2DP) {
               AudioParameter param;
               param.add(String8("a2dp_connected"), String8("false"));
               mpClientInterface->setParameters(0, param.toString());
            }

            if (device & AUDIO_DEVICE_OUT_USB_ACCESSORY) {
               AudioParameter param;
               param.add(String8("usb_connected"), String8("true"));
               mpClientInterface->setParameters(0, param.toString());
            }

            checkOutputsForDevice(devDesc, state, outputs, devDesc->mAddress);

            // Propagate device availability to Engine
            mEngine->setDeviceConnectionState(devDesc, state);
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
                sp<SwAudioOutputDescriptor> desc = mOutputs.valueFor(outputs[i]);
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
        if (mEngine->getPhoneState() == AUDIO_MODE_IN_CALL && hasPrimaryOutput()) {
            audio_devices_t newDevice = getNewOutputDevice(mPrimaryOutput, false /*fromCache*/);
            updateCallRouting(newDevice);
        }

#ifdef QCOM_FM_ENABLED
        // handle FM device connection state to trigger FM AFE loopback
        if (device == AUDIO_DEVICE_OUT_FM && hasPrimaryOutput()) {
           audio_devices_t newDevice = AUDIO_DEVICE_NONE;
           if (state == AUDIO_POLICY_DEVICE_STATE_AVAILABLE) {
               mPrimaryOutput->changeRefCount(AUDIO_STREAM_MUSIC, 1);
               newDevice = (audio_devices_t)(getNewOutputDevice(mPrimaryOutput, false)|AUDIO_DEVICE_OUT_FM);
               mFMIsActive = true;
           } else {
               newDevice = (audio_devices_t)(getNewOutputDevice(mPrimaryOutput, false));
               mFMIsActive = false;
               mPrimaryOutput->changeRefCount(AUDIO_STREAM_MUSIC, -1);
           }
           AudioParameter param = AudioParameter();
           float volumeDb = mPrimaryOutput->mCurVolume[AUDIO_STREAM_MUSIC];
           mPrevFMVolumeDb = volumeDb;
           param.addFloat(String8("fm_volume"), Volume::DbToAmpl(volumeDb));
           param.addInt(String8("handle_fm"), (int)newDevice);
           mpClientInterface->setParameters(mPrimaryOutput->mIoHandle, param.toString());
        }
#endif /* QCOM_FM_ENABLED end */

        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<SwAudioOutputDescriptor> desc = mOutputs.valueAt(i);
            if ((mEngine->getPhoneState() != AUDIO_MODE_IN_CALL) || (desc != mPrimaryOutput)) {
                audio_devices_t newDevice = getNewOutputDevice(desc, true /*fromCache*/);
                // do not force device change on duplicated output because if device is 0, it will
                // also force a device 0 for the two outputs it is duplicated to which may override
                // a valid device selection on those outputs.
                bool force = !desc->isDuplicated()
                        && (!device_distinguishes_on_address(device)
                                // always force when disconnecting (a non-duplicated device)
                                || (state == AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE));
                setOutputDevice(desc, newDevice, force, 0);
            }
        }

        mpClientInterface->onAudioPortListUpdate();
        return NO_ERROR;
    }  // end if is output device

    // handle input devices
    if (audio_is_input_device(device)) {
        SortedVector <audio_io_handle_t> inputs;

        ssize_t index = mAvailableInputDevices.indexOf(devDesc);
        switch (state)
        {
        // handle input device connection
        case AUDIO_POLICY_DEVICE_STATE_AVAILABLE: {
            if (index >= 0) {
                ALOGW("setDeviceConnectionState() device already connected: %d", device);
                return INVALID_OPERATION;
            }
            sp<HwModule> module = mHwModules.getModuleForDevice(device);
            if (module == NULL) {
                ALOGW("setDeviceConnectionState(): could not find HW module for device %08x",
                      device);
                return INVALID_OPERATION;
            }
            if (checkInputsForDevice(devDesc, state, inputs, devDesc->mAddress) != NO_ERROR) {
                return INVALID_OPERATION;
            }

            index = mAvailableInputDevices.add(devDesc);
            if (index >= 0) {
                mAvailableInputDevices[index]->attach(module);
            } else {
                return NO_MEMORY;
            }

            // Set connect to HALs
            AudioParameter param = AudioParameter(devDesc->mAddress);
            param.addInt(String8(AUDIO_PARAMETER_DEVICE_CONNECT), device);
            mpClientInterface->setParameters(AUDIO_IO_HANDLE_NONE, param.toString());

            // Propagate device availability to Engine
            mEngine->setDeviceConnectionState(devDesc, state);
        } break;

        // handle input device disconnection
        case AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE: {
            if (index < 0) {
                ALOGW("setDeviceConnectionState() device not connected: %d", device);
                return INVALID_OPERATION;
            }

            ALOGV("setDeviceConnectionState() disconnecting input device %x", device);

            // Set Disconnect to HALs
            AudioParameter param = AudioParameter(devDesc->mAddress);
            param.addInt(String8(AUDIO_PARAMETER_DEVICE_DISCONNECT), device);
            mpClientInterface->setParameters(AUDIO_IO_HANDLE_NONE, param.toString());

            checkInputsForDevice(devDesc, state, inputs, devDesc->mAddress);
            mAvailableInputDevices.remove(devDesc);

            // Propagate device availability to Engine
            mEngine->setDeviceConnectionState(devDesc, state);
        } break;

        default:
            ALOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

        closeAllInputs();

        if (mEngine->getPhoneState() == AUDIO_MODE_IN_CALL && hasPrimaryOutput()) {
            audio_devices_t newDevice = getNewOutputDevice(mPrimaryOutput, false /*fromCache*/);
            updateCallRouting(newDevice);
        }

        mpClientInterface->onAudioPortListUpdate();
        return NO_ERROR;
    } // end if is input device

    ALOGW("setDeviceConnectionState() invalid device: %x", device);
    return BAD_VALUE;
}

/* NOTE: updateCallRouting is called by:
 * setForceUse
 * setPhoneState
 * setDeviceConnectionStateInt
 */
void AudioPolicyManagerCustom::updateCallRouting(audio_devices_t rxDevice, int delayMs)
{
    bool createTxPatch = false;
    struct audio_patch patch;
    patch.num_sources = 1;
    patch.num_sinks = 1;
    status_t status;
    audio_patch_handle_t afPatchHandle;
    DeviceVector deviceList;

    if(!hasPrimaryOutput()) {
        return;
    }
    audio_devices_t txDevice = getDeviceAndMixForInputSource(AUDIO_SOURCE_VOICE_CALL);
    ALOGV("updateCallRouting device rxDevice %08x txDevice %08x", rxDevice, txDevice);

    // release existing RX patch if any
    if (mCallRxPatch != 0) {
        mpClientInterface->releaseAudioPatch(mCallRxPatch->mAfPatchHandle, 0);
        mCallRxPatch.clear();
    }
    // release TX patch if any
    if (mCallTxPatch != 0) {
        mpClientInterface->releaseAudioPatch(mCallTxPatch->mAfPatchHandle, 0);
        mCallTxPatch.clear();
    }

    // If the RX device is on the primary HW module, then use legacy routing method for voice calls
    // via setOutputDevice() on primary output.
    // Otherwise, create two audio patches for TX and RX path.
    if (availablePrimaryOutputDevices() & rxDevice) {
        setOutputDevice(mPrimaryOutput, rxDevice, true, delayMs);
        // If the TX device is also on the primary HW module, setOutputDevice() will take care
        // of it due to legacy implementation. If not, create a patch.
        if ((availablePrimaryInputDevices() & txDevice & ~AUDIO_DEVICE_BIT_IN)
                == AUDIO_DEVICE_NONE) {
            createTxPatch = true;
        }
    } else {
        // create RX path audio patch
        deviceList = mAvailableOutputDevices.getDevicesFromType(rxDevice);
        ALOG_ASSERT(!deviceList.isEmpty(),
                    "updateCallRouting() selected device not in output device list");
        sp<DeviceDescriptor> rxSinkDeviceDesc = deviceList.itemAt(0);
        deviceList = mAvailableInputDevices.getDevicesFromType(AUDIO_DEVICE_IN_TELEPHONY_RX);
        ALOG_ASSERT(!deviceList.isEmpty(),
                    "updateCallRouting() no telephony RX device");
        sp<DeviceDescriptor> rxSourceDeviceDesc = deviceList.itemAt(0);

        rxSourceDeviceDesc->toAudioPortConfig(&patch.sources[0]);
        rxSinkDeviceDesc->toAudioPortConfig(&patch.sinks[0]);

        // request to reuse existing output stream if one is already opened to reach the RX device
        SortedVector<audio_io_handle_t> outputs =
                                getOutputsForDevice(rxDevice, mOutputs);
        audio_io_handle_t output = selectOutput(outputs,
                                                AUDIO_OUTPUT_FLAG_NONE,
                                                AUDIO_FORMAT_INVALID);
        if (output != AUDIO_IO_HANDLE_NONE) {
            // close active input (if any) before opening new input
            audio_io_handle_t activeInput = mInputs.getActiveInput();
            if (activeInput != 0) {
                ALOGV("updateCallRouting() close active input before opening new input");
                sp<AudioInputDescriptor> activeDesc = mInputs.valueFor(activeInput);
                stopInput(activeInput, activeDesc->mSessions.itemAt(0));
                releaseInput(activeInput, activeDesc->mSessions.itemAt(0));
            }
            sp<SwAudioOutputDescriptor> outputDesc = mOutputs.valueFor(output);
            ALOG_ASSERT(!outputDesc->isDuplicated(),
                        "updateCallRouting() RX device output is duplicated");
            outputDesc->toAudioPortConfig(&patch.sources[1]);
            patch.sources[1].ext.mix.usecase.stream = AUDIO_STREAM_PATCH;
            patch.num_sources = 2;
        }

        afPatchHandle = AUDIO_PATCH_HANDLE_NONE;
        status = mpClientInterface->createAudioPatch(&patch, &afPatchHandle, 0);
        ALOGW_IF(status != NO_ERROR, "updateCallRouting() error %d creating RX audio patch",
                                               status);
        if (status == NO_ERROR) {
            mCallRxPatch = new AudioPatch(&patch, mUidCached);
            mCallRxPatch->mAfPatchHandle = afPatchHandle;
            mCallRxPatch->mUid = mUidCached;
        }
        createTxPatch = true;
    }
    if (createTxPatch) {

        struct audio_patch patch;
        patch.num_sources = 1;
        patch.num_sinks = 1;
        deviceList = mAvailableInputDevices.getDevicesFromType(txDevice);
        ALOG_ASSERT(!deviceList.isEmpty(),
                    "updateCallRouting() selected device not in input device list");
        sp<DeviceDescriptor> txSourceDeviceDesc = deviceList.itemAt(0);
        txSourceDeviceDesc->toAudioPortConfig(&patch.sources[0]);
        deviceList = mAvailableOutputDevices.getDevicesFromType(AUDIO_DEVICE_OUT_TELEPHONY_TX);
        ALOG_ASSERT(!deviceList.isEmpty(),
                    "updateCallRouting() no telephony TX device");
        sp<DeviceDescriptor> txSinkDeviceDesc = deviceList.itemAt(0);
        txSinkDeviceDesc->toAudioPortConfig(&patch.sinks[0]);

        SortedVector<audio_io_handle_t> outputs =
                                getOutputsForDevice(AUDIO_DEVICE_OUT_TELEPHONY_TX, mOutputs);
        audio_io_handle_t output = selectOutput(outputs,
                                                AUDIO_OUTPUT_FLAG_NONE,
                                                AUDIO_FORMAT_INVALID);
        // request to reuse existing output stream if one is already opened to reach the TX
        // path output device
        if (output != AUDIO_IO_HANDLE_NONE) {
            sp<AudioOutputDescriptor> outputDesc = mOutputs.valueFor(output);
            ALOG_ASSERT(!outputDesc->isDuplicated(),
                        "updateCallRouting() RX device output is duplicated");
            outputDesc->toAudioPortConfig(&patch.sources[1]);
            patch.sources[1].ext.mix.usecase.stream = AUDIO_STREAM_PATCH;
            patch.num_sources = 2;
        }

        // terminate active capture if on the same HW module as the call TX source device
        // FIXME: would be better to refine to only inputs whose profile connects to the
        // call TX device but this information is not in the audio patch and logic here must be
        // symmetric to the one in startInput()
        audio_io_handle_t activeInput = mInputs.getActiveInput();
        if (activeInput != 0) {
            sp<AudioInputDescriptor> activeDesc = mInputs.valueFor(activeInput);
            if (activeDesc->getModuleHandle() == txSourceDeviceDesc->getModuleHandle()) {
                audio_session_t activeSession = activeDesc->mSessions.itemAt(0);
                stopInput(activeInput, activeSession);
                releaseInput(activeInput, activeSession);
            }
        }

        afPatchHandle = AUDIO_PATCH_HANDLE_NONE;
        status = mpClientInterface->createAudioPatch(&patch, &afPatchHandle, 0);
        ALOGW_IF(status != NO_ERROR, "setPhoneState() error %d creating TX audio patch",
                                               status);
        if (status == NO_ERROR) {
            mCallTxPatch = new AudioPatch(&patch, mUidCached);
            mCallTxPatch->mAfPatchHandle = afPatchHandle;
            mCallTxPatch->mUid = mUidCached;
        }
    }
}

status_t AudioPolicyManagerCustom::getInputForAttr(const audio_attributes_t *attr,
                                             audio_io_handle_t *input,
                                             audio_session_t session,
                                             uid_t uid,
                                             uint32_t samplingRate,
                                             audio_format_t format,
                                             audio_channel_mask_t channelMask,
                                             audio_input_flags_t flags,
                                             audio_port_handle_t selectedDeviceId,
                                             input_type_t *inputType)
{
    ALOGV("getInputForAttr() source %d, samplingRate %d, format %d, channelMask %x,"
            "session %d, flags %#x",
          attr->source, samplingRate, format, channelMask, session, flags);

    *input = AUDIO_IO_HANDLE_NONE;
    *inputType = API_INPUT_INVALID;
    audio_devices_t device;
    // handle legacy remote submix case where the address was not always specified
    String8 address = String8("");
    bool isSoundTrigger = false;
    audio_source_t inputSource = attr->source;
    audio_source_t halInputSource;
    AudioMix *policyMix = NULL;

    if (inputSource == AUDIO_SOURCE_DEFAULT) {
        inputSource = AUDIO_SOURCE_MIC;
    }
    halInputSource = inputSource;

    // Explicit routing?
    sp<DeviceDescriptor> deviceDesc;
    for (size_t i = 0; i < mAvailableInputDevices.size(); i++) {
        if (mAvailableInputDevices[i]->getId() == selectedDeviceId) {
            deviceDesc = mAvailableInputDevices[i];
            break;
        }
    }
    mInputRoutes.addRoute(session, SessionRoute::STREAM_TYPE_NA, inputSource, deviceDesc, uid);

    if (inputSource == AUDIO_SOURCE_REMOTE_SUBMIX &&
            strncmp(attr->tags, "addr=", strlen("addr=")) == 0) {
        status_t ret = mPolicyMixes.getInputMixForAttr(*attr, &policyMix);
        if (ret != NO_ERROR) {
            return ret;
        }
        *inputType = API_INPUT_MIX_EXT_POLICY_REROUTE;
        device = AUDIO_DEVICE_IN_REMOTE_SUBMIX;
        address = String8(attr->tags + strlen("addr="));
    } else {
        device = getDeviceAndMixForInputSource(inputSource, &policyMix);
        if (device == AUDIO_DEVICE_NONE) {
            ALOGW("getInputForAttr() could not find device for source %d", inputSource);
            return BAD_VALUE;
        }
        // block request to open input on USB during voice call
        if((AUDIO_MODE_IN_CALL == mEngine->getPhoneState()) &&
            (device == AUDIO_DEVICE_IN_USB_DEVICE)) {
            ALOGV("getInputForAttr(): blocking the request to open input on USB device");
            return BAD_VALUE;
        }
        if (policyMix != NULL) {
            address = policyMix->mRegistrationId;
            if (policyMix->mMixType == MIX_TYPE_RECORDERS) {
                // there is an external policy, but this input is attached to a mix of recorders,
                // meaning it receives audio injected into the framework, so the recorder doesn't
                // know about it and is therefore considered "legacy"
                *inputType = API_INPUT_LEGACY;
            } else {
                // recording a mix of players defined by an external policy, we're rerouting for
                // an external policy
                *inputType = API_INPUT_MIX_EXT_POLICY_REROUTE;
            }
        } else if (audio_is_remote_submix_device(device)) {
            address = String8("0");
            *inputType = API_INPUT_MIX_CAPTURE;
        } else if (device == AUDIO_DEVICE_IN_TELEPHONY_RX) {
            *inputType = API_INPUT_TELEPHONY_RX;
        } else {
            *inputType = API_INPUT_LEGACY;
        }

        // adapt channel selection to input source
        switch (inputSource) {
        case AUDIO_SOURCE_VOICE_UPLINK:
            channelMask |= AUDIO_CHANNEL_IN_VOICE_UPLINK;
            break;
        case AUDIO_SOURCE_VOICE_DOWNLINK:
            channelMask |= AUDIO_CHANNEL_IN_VOICE_DNLINK;
            break;
        case AUDIO_SOURCE_VOICE_CALL:
            channelMask |= AUDIO_CHANNEL_IN_VOICE_UPLINK | AUDIO_CHANNEL_IN_VOICE_DNLINK;
            break;
        default:
            break;
        }

        if (inputSource == AUDIO_SOURCE_HOTWORD) {
            ssize_t index = mSoundTriggerSessions.indexOfKey(session);
            if (index >= 0) {
                *input = mSoundTriggerSessions.valueFor(session);
                isSoundTrigger = true;
                flags = (audio_input_flags_t)(flags | AUDIO_INPUT_FLAG_HW_HOTWORD);
                ALOGV("SoundTrigger capture on session %d input %d", session, *input);
            } else {
                halInputSource = AUDIO_SOURCE_VOICE_RECOGNITION;
            }
        }
    }

    // find a compatible input profile (not necessarily identical in parameters)
    sp<IOProfile> profile;
    // samplingRate and flags may be updated by getInputProfile
    uint32_t profileSamplingRate = samplingRate;
    audio_format_t profileFormat = format;
    audio_channel_mask_t profileChannelMask = channelMask;
    audio_input_flags_t profileFlags = flags;
    for (;;) {
        profile = getInputProfile(device, address,
                                  profileSamplingRate, profileFormat, profileChannelMask,
                                  profileFlags);
        if (profile != 0) {
            break; // success
        } else if (profileFlags != AUDIO_INPUT_FLAG_NONE) {
            profileFlags = AUDIO_INPUT_FLAG_NONE; // retry
        } else { // fail
            ALOGW("getInputForAttr() could not find profile for device 0x%X, samplingRate %u,"
                    "format %#x, channelMask 0x%X, flags %#x",
                    device, samplingRate, format, channelMask, flags);
            return BAD_VALUE;
        }
    }

    if (profile->getModuleHandle() == 0) {
        ALOGE("getInputForAttr(): HW module %s not opened", profile->getModuleName());
        return NO_INIT;
    }

    audio_config_t config = AUDIO_CONFIG_INITIALIZER;
    config.sample_rate = profileSamplingRate;
    config.channel_mask = profileChannelMask;
    config.format = profileFormat;

    status_t status = mpClientInterface->openInput(profile->getModuleHandle(),
                                                   input,
                                                   &config,
                                                   &device,
                                                   address,
                                                   halInputSource,
                                                   profileFlags);

    // only accept input with the exact requested set of parameters
    if (status != NO_ERROR || *input == AUDIO_IO_HANDLE_NONE ||
        (profileSamplingRate != config.sample_rate) ||
        (profileFormat != config.format) ||
        (profileChannelMask != config.channel_mask)) {
        ALOGW("getInputForAttr() failed opening input: samplingRate %d, format %d,"
                " channelMask %x",
                samplingRate, format, channelMask);
        if (*input != AUDIO_IO_HANDLE_NONE) {
            mpClientInterface->closeInput(*input);
        }
        return BAD_VALUE;
    }

    sp<AudioInputDescriptor> inputDesc = new AudioInputDescriptor(profile);
    inputDesc->mInputSource = inputSource;
    inputDesc->mRefCount = 0;
    inputDesc->mOpenRefCount = 1;
    inputDesc->mSamplingRate = profileSamplingRate;
    inputDesc->mFormat = profileFormat;
    inputDesc->mChannelMask = profileChannelMask;
    inputDesc->mDevice = device;
    inputDesc->mSessions.add(session);
    inputDesc->mIsSoundTrigger = isSoundTrigger;
    inputDesc->mPolicyMix = policyMix;

    ALOGV("getInputForAttr() returns input type = %d", *inputType);

    addInput(*input, inputDesc);
    mpClientInterface->onAudioPortListUpdate();

    return NO_ERROR;
}

void AudioPolicyManagerCustom::setPhoneState(audio_mode_t state)
{
    ALOGV("setPhoneState() state %d", state);
    // store previous phone state for management of sonification strategy below
    int oldState = mEngine->getPhoneState();

    if (mEngine->setPhoneState(state) != NO_ERROR) {
        ALOGW("setPhoneState() invalid or same state %d", state);
        return;
    }
    /// Opens: can these line be executed after the switch of volume curves???
    // if leaving call state, handle special case of active streams
    // pertaining to sonification strategy see handleIncallSonification()
    if (isStateInCall(oldState)) {
        ALOGV("setPhoneState() in call state management: new state is %d", state);
        for (int stream = 0; stream < AUDIO_STREAM_CNT; stream++) {
            if (stream == AUDIO_STREAM_PATCH) {
                continue;
            }
            handleIncallSonification((audio_stream_type_t)stream, false, true);
        }

        // force reevaluating accessibility routing when call stops
        mpClientInterface->invalidateStream(AUDIO_STREAM_ACCESSIBILITY);
    }

    /**
     * Switching to or from incall state or switching between telephony and VoIP lead to force
     * routing command.
     */
    bool force = ((is_state_in_call(oldState) != is_state_in_call(state))
                  || (is_state_in_call(state) && (state != oldState)));

    // check for device and output changes triggered by new phone state
    checkA2dpSuspend();
    checkOutputForAllStrategies();
    updateDevicesAndOutputs();

    int delayMs = 0;
    if (isStateInCall(state)) {
        nsecs_t sysTime = systemTime();
        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<SwAudioOutputDescriptor> desc = mOutputs.valueAt(i);
            // mute media and sonification strategies and delay device switch by the largest
            // latency of any output where either strategy is active.
            // This avoid sending the ring tone or music tail into the earpiece or headset.
            if ((isStrategyActive(desc, STRATEGY_MEDIA,
                                  SONIFICATION_HEADSET_MUSIC_DELAY,
                                  sysTime) ||
                 isStrategyActive(desc, STRATEGY_SONIFICATION,
                                  SONIFICATION_HEADSET_MUSIC_DELAY,
                                  sysTime)) &&
                    (delayMs < (int)desc->latency()*2)) {
                delayMs = desc->latency()*2;
            }
            setStrategyMute(STRATEGY_MEDIA, true, desc);
            setStrategyMute(STRATEGY_MEDIA, false, desc, MUTE_TIME_MS,
                getDeviceForStrategy(STRATEGY_MEDIA, true /*fromCache*/));
            setStrategyMute(STRATEGY_SONIFICATION, true, desc);
            setStrategyMute(STRATEGY_SONIFICATION, false, desc, MUTE_TIME_MS,
                getDeviceForStrategy(STRATEGY_SONIFICATION, true /*fromCache*/));
        }
    }

    if (hasPrimaryOutput()) {
        // Note that despite the fact that getNewOutputDevice() is called on the primary output,
        // the device returned is not necessarily reachable via this output
        audio_devices_t rxDevice = getNewOutputDevice(mPrimaryOutput, false /*fromCache*/);
        // force routing command to audio hardware when ending call
        // even if no device change is needed
        if (isStateInCall(oldState) && rxDevice == AUDIO_DEVICE_NONE) {
            rxDevice = mPrimaryOutput->device();
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
    }
    // if entering in call state, handle special case of active streams
    // pertaining to sonification strategy see handleIncallSonification()
    if (isStateInCall(state)) {
        ALOGV("setPhoneState() in call state management: new state is %d", state);
        for (int stream = 0; stream < AUDIO_STREAM_CNT; stream++) {
            if (stream == AUDIO_STREAM_PATCH) {
                continue;
            }
            handleIncallSonification((audio_stream_type_t)stream, true, true);
        }

        // force reevaluating accessibility routing when call starts
        mpClientInterface->invalidateStream(AUDIO_STREAM_ACCESSIBILITY);
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
    ALOGV("setForceUse() usage %d, config %d, mPhoneState %d", usage, config, mEngine->getPhoneState());

    if (mEngine->setForceUse(usage, config) != NO_ERROR) {
        ALOGW("setForceUse() could not set force cfg %d for usage %d", config, usage);
        return;
    }
    bool forceVolumeReeval = (usage == AUDIO_POLICY_FORCE_FOR_COMMUNICATION) ||
            (usage == AUDIO_POLICY_FORCE_FOR_DOCK) ||
            (usage == AUDIO_POLICY_FORCE_FOR_SYSTEM);

    // check for device and output changes triggered by new force usage
    checkA2dpSuspend();
    checkOutputForAllStrategies();
    updateDevicesAndOutputs();
    if (mEngine->getPhoneState() == AUDIO_MODE_IN_CALL && hasPrimaryOutput()) {
        audio_devices_t newDevice = getNewOutputDevice(mPrimaryOutput, true /*fromCache*/);
        updateCallRouting(newDevice);
    }
    for (size_t i = 0; i < mOutputs.size(); i++) {
        sp<SwAudioOutputDescriptor> outputDesc = mOutputs.valueAt(i);
        audio_devices_t newDevice = getNewOutputDevice(outputDesc, true /*fromCache*/);
        if ((mEngine->getPhoneState() != AUDIO_MODE_IN_CALL) || (outputDesc != mPrimaryOutput)) {
            setOutputDevice(outputDesc, newDevice, (newDevice != AUDIO_DEVICE_NONE));
        }
        if (forceVolumeReeval && (newDevice != AUDIO_DEVICE_NONE)) {
            applyStreamVolumes(outputDesc, newDevice, 0, true);
        }
    }

    audio_io_handle_t activeInput = mInputs.getActiveInput();
    if (activeInput != 0) {
        sp<AudioInputDescriptor> activeDesc = mInputs.valueFor(activeInput);
        audio_devices_t newDevice = getNewInputDevice(activeInput);
        // Force new input selection if the new device can not be reached via current input
        if (activeDesc->mProfile->mSupportedDevices.types() & (newDevice & ~AUDIO_DEVICE_BIT_IN)) {
            setInputDevice(activeInput, newDevice);
        } else {
            closeInput(activeInput);
        }
    }
}

status_t AudioPolicyManagerCustom::checkAndSetVolume(audio_stream_type_t stream,
                                                   int index,
                                                   const sp<AudioOutputDescriptor>& outputDesc,
                                                   audio_devices_t device,
                                                   int delayMs,
                                                   bool force)
{
    // do not change actual stream volume if the stream is muted
    if (outputDesc->mMuteCount[stream] != 0) {
        ALOGVV("checkAndSetVolume() stream %d muted count %d",
              stream, outputDesc->mMuteCount[stream]);
        return NO_ERROR;
    }
    audio_policy_forced_cfg_t forceUseForComm =
            mEngine->getForceUse(AUDIO_POLICY_FORCE_FOR_COMMUNICATION);
    // do not change in call volume if bluetooth is connected and vice versa
    if ((stream == AUDIO_STREAM_VOICE_CALL && forceUseForComm == AUDIO_POLICY_FORCE_BT_SCO) ||
        (stream == AUDIO_STREAM_BLUETOOTH_SCO && forceUseForComm != AUDIO_POLICY_FORCE_BT_SCO)) {
        ALOGV("checkAndSetVolume() cannot set stream %d volume with force use = %d for comm",
             stream, forceUseForComm);
        return INVALID_OPERATION;
    }

    if (device == AUDIO_DEVICE_NONE) {
        device = outputDesc->device();
    }

    float volumeDb = computeVolume(stream, index, device);
    if (outputDesc->isFixedVolume(device)) {
        volumeDb = 0.0f;
    }

    outputDesc->setVolume(volumeDb, stream, device, delayMs, force);

    if (stream == AUDIO_STREAM_VOICE_CALL ||
        stream == AUDIO_STREAM_BLUETOOTH_SCO) {
        float voiceVolume;
        // Force voice volume to max for bluetooth SCO as volume is managed by the headset
        if (stream == AUDIO_STREAM_VOICE_CALL) {
            voiceVolume = (float)index/(float)mStreams.valueFor(stream).getVolumeIndexMax();
        } else {
            voiceVolume = 1.0;
        }

        if (voiceVolume != mLastVoiceVolume && outputDesc == mPrimaryOutput) {
            mpClientInterface->setVoiceVolume(voiceVolume, delayMs);
            mLastVoiceVolume = voiceVolume;
        }
#ifdef QCOM_FM_ENABLED
    } else if (stream == AUDIO_STREAM_MUSIC && hasPrimaryOutput() &&
               outputDesc == mPrimaryOutput && mFMIsActive) {
        /* Avoid unnecessary set_parameter calls as it puts the primary
           outputs FastMixer in HOT_IDLE leading to breaks in audio */
        if (volumeDb != mPrevFMVolumeDb) {
            mPrevFMVolumeDb = volumeDb;
            AudioParameter param = AudioParameter();
            param.addFloat(String8("fm_volume"), Volume::DbToAmpl(volumeDb));
            //Double delayMs to avoid sound burst while device switch.
            mpClientInterface->setParameters(mPrimaryOutput->mIoHandle, param.toString(), delayMs*2);
        }
#endif /* QCOM_FM_ENABLED end */
    }

    return NO_ERROR;
}

AudioPolicyManagerCustom::AudioPolicyManagerCustom(AudioPolicyClientInterface *clientInterface)
    : AudioPolicyManager(clientInterface),
      mPrevFMVolumeDb(0.0f),
      mFMIsActive(false)
{
    char ssr_enabled[PROPERTY_VALUE_MAX] = {0};
    bool prop_ssr_enabled = false;

    if (property_get("ro.qc.sdk.audio.ssr", ssr_enabled, NULL)) {
        prop_ssr_enabled = atoi(ssr_enabled) || !strncmp("true", ssr_enabled, 4);
    }

    for (size_t i = 0; i < mHwModules.size(); i++) {
        ALOGV("Hw module %d", i);
        for (size_t j = 0; j < mHwModules[i]->mInputProfiles.size(); j++) {
            const sp<IOProfile> inProfile = mHwModules[i]->mInputProfiles[j];
            ALOGV("Input profile ", j);
            for (size_t k = 0; k  < inProfile->mChannelMasks.size(); k++) {
                audio_channel_mask_t channelMask =
                    inProfile->mChannelMasks.itemAt(k);
                ALOGV("Channel Mask %x size %d", channelMask,
                    inProfile->mChannelMasks.size());
                if (AUDIO_CHANNEL_IN_5POINT1 == channelMask) {
                    if (!prop_ssr_enabled) {
                        ALOGI("removing AUDIO_CHANNEL_IN_5POINT1 from"
                            " input profile as SSR(surround sound record)"
                            " is not supported on this chipset variant");
                        inProfile->mChannelMasks.removeItemsAt(k, 1);
                        ALOGV("Channel Mask size now %d",
                            inProfile->mChannelMasks.size());
                    }
                }
            }
        }
    }
}

}
