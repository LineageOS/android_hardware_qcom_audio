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

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// A device mask for all audio output devices that are considered "remote" when evaluating
// active output devices in isStreamActiveRemotely()
#define APM_AUDIO_OUT_DEVICE_REMOTE_ALL  AUDIO_DEVICE_OUT_REMOTE_SUBMIX
// A device mask for all audio input and output devices where matching inputs/outputs on device
// type alone is not enough: the address must match too
#define APM_AUDIO_DEVICE_MATCH_ADDRESS_ALL (AUDIO_DEVICE_IN_REMOTE_SUBMIX | \
                                            AUDIO_DEVICE_OUT_REMOTE_SUBMIX)
// Following delay should be used if the calculated routing delay from all active
// input streams is higher than this value
#define MAX_VOICE_CALL_START_DELAY_MS 100

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
// This function checks for the parameters which can be offloaded.
// This can be enhanced depending on the capability of the DSP and policy
// of the system.
bool AudioPolicyManagerCustom::isOffloadSupported(const audio_offload_info_t& offloadInfo)
{
    ALOGV("isOffloadSupported: SR=%u, CM=0x%x, Format=0x%x, StreamType=%d,"
     " BitRate=%u, duration=%" PRId64 " us, has_video=%d",
     offloadInfo.sample_rate, offloadInfo.channel_mask,
     offloadInfo.format,
     offloadInfo.stream_type, offloadInfo.bit_rate, offloadInfo.duration_us,
     offloadInfo.has_video);

    // Check if offload has been disabled
    char propValue[PROPERTY_VALUE_MAX];
    if (property_get("audio.offload.disable", propValue, "0")) {
        if (atoi(propValue) != 0) {
            ALOGV("offload disabled by audio.offload.disable=%s", propValue );
            return false;
        }
    }

    // Check if stream type is music, then only allow offload as of now.
    if (offloadInfo.stream_type != AUDIO_STREAM_MUSIC)
    {
        ALOGV("isOffloadSupported: stream_type != MUSIC, returning false");
        return false;
    }
    //check if it's multi-channel AAC (includes sub formats) and FLAC format
    if ((popcount(offloadInfo.channel_mask) > 2) &&
       (((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC) ||
        ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_FLAC)||
        ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_VORBIS))) {
           ALOGD("offload disabled for multi-channel AAC,FLAC and VORBIS format");
           return false;
    }

    //TODO: enable audio offloading with video when ready
    const bool allowOffloadWithVideo =
            property_get_bool("audio.offload.video", false /* default_value */);
    if (offloadInfo.has_video && !allowOffloadWithVideo) {
        ALOGV("isOffloadSupported: has_video == true, returning false");
        return false;
    }

    //If duration is less than minimum value defined in property, return false
    if (property_get("audio.offload.min.duration.secs", propValue, NULL)) {
        if (offloadInfo.duration_us < (atoi(propValue) * 1000000 )) {
            ALOGV("Offload denied by duration < audio.offload.min.duration.secs(=%s)", propValue);
            return false;
        }
    } else if (offloadInfo.duration_us < OFFLOAD_DEFAULT_MIN_DURATION_SECS * 1000000) {
        ALOGV("Offload denied by duration < default min(=%u)", OFFLOAD_DEFAULT_MIN_DURATION_SECS);
        //duration checks only valid for MP3/AAC/ formats,
        //do not check duration for other audio formats, e.g. dolby AAC/AC3 and amrwb+ formats
        if ((offloadInfo.format == AUDIO_FORMAT_MP3) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_FLAC) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_VORBIS) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_WMA) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_WMA_PRO) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_ALAC) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_APE))
            return false;

    }

    // Do not allow offloading if one non offloadable effect is enabled. This prevents from
    // creating an offloaded track and tearing it down immediately after start when audioflinger
    // detects there is an active non offloadable effect.
    // FIXME: We should check the audio session here but we do not have it in this context.
    // This may prevent offloading in rare situations where effects are left active by apps
    // in the background.
    if (mEffects.isNonOffloadableEffectEnabled()) {
        return false;
    }
    // Check for soundcard status
    String8 valueStr = mpClientInterface->getParameters((audio_io_handle_t)0,
    String8("SND_CARD_STATUS"));
    AudioParameter result = AudioParameter(valueStr);
    int isonline = 0;
    if ((result.getInt(String8("SND_CARD_STATUS"), isonline) == NO_ERROR)
           && !isonline) {
         ALOGD("copl: soundcard is offline rejecting offload request");
         return false;
    }
    // See if there is a profile to support this.
    // AUDIO_DEVICE_NONE
    sp<IOProfile> profile = getProfileForDirectOutput(AUDIO_DEVICE_NONE /*ignore device */,
                                            offloadInfo.sample_rate,
                                            offloadInfo.format,
                                            offloadInfo.channel_mask,
                                            AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD);
    ALOGV("isOffloadSupported() profile %sfound", profile != 0 ? "" : "NOT ");
    return (profile != 0);
}
audio_devices_t AudioPolicyManagerCustom::getNewOutputDevice(const sp<AudioOutputDescriptor>& outputDesc,
                                                       bool fromCache)
{
    audio_devices_t device = AUDIO_DEVICE_NONE;

    ssize_t index = mAudioPatches.indexOfKey(outputDesc->mPatchHandle);
    if (index >= 0) {
        sp<AudioPatch> patchDesc = mAudioPatches.valueAt(index);
        if (patchDesc->mUid != mUidCached) {
            ALOGV("getNewOutputDevice() device %08x forced by patch %d",
                  outputDesc->device(), outputDesc->mPatchHandle);
            return outputDesc->device();
        }
    }

    // check the following by order of priority to request a routing change if necessary:
    // 1: the strategy enforced audible is active and enforced on the output:
    //      use device for strategy enforced audible
    // 2: we are in call or the strategy phone is active on the output:
    //      use device for strategy phone
    // 3: the strategy for enforced audible is active but not enforced on the output:
    //      use the device for strategy enforced audible
    // 4: the strategy sonification is active on the output:
    //      use device for strategy sonification
    // 5: the strategy "respectful" sonification is active on the output:
    //      use device for strategy "respectful" sonification
    // 6: the strategy accessibility is active on the output:
    //      use device for strategy accessibility
    // 7: the strategy media is active on the output:
    //      use device for strategy media
    // 8: the strategy DTMF is active on the output:
    //      use device for strategy DTMF
    // 9: the strategy for beacon, a.k.a. "transmitted through speaker" is active on the output:
    //      use device for strategy t-t-s
    if (isStrategyActive(outputDesc, STRATEGY_ENFORCED_AUDIBLE) &&
        mEngine->getForceUse(AUDIO_POLICY_FORCE_FOR_SYSTEM) == AUDIO_POLICY_FORCE_SYSTEM_ENFORCED) {
        device = getDeviceForStrategy(STRATEGY_ENFORCED_AUDIBLE, fromCache);
    } else if (isInCall() ||
                    isStrategyActive(outputDesc, STRATEGY_PHONE)||
                    isStrategyActive(mPrimaryOutput, STRATEGY_PHONE)) {
        device = getDeviceForStrategy(STRATEGY_PHONE, fromCache);
    } else if (isStrategyActive(outputDesc, STRATEGY_ENFORCED_AUDIBLE)) {
        device = getDeviceForStrategy(STRATEGY_ENFORCED_AUDIBLE, fromCache);
    } else if (isStrategyActive(outputDesc, STRATEGY_SONIFICATION)||
                (isStrategyActive(mPrimaryOutput,STRATEGY_SONIFICATION)
                && (!isStrategyActive(mPrimaryOutput,STRATEGY_MEDIA)))) {
        device = getDeviceForStrategy(STRATEGY_SONIFICATION, fromCache);
    } else if (isStrategyActive(outputDesc, STRATEGY_SONIFICATION_RESPECTFUL)||
                (isStrategyActive(mPrimaryOutput,STRATEGY_SONIFICATION_RESPECTFUL)
                && (!isStrategyActive(mPrimaryOutput, STRATEGY_MEDIA)))) {
        device = getDeviceForStrategy(STRATEGY_SONIFICATION_RESPECTFUL, fromCache);
    } else if (isStrategyActive(outputDesc, STRATEGY_ACCESSIBILITY)) {
        device = getDeviceForStrategy(STRATEGY_ACCESSIBILITY, fromCache);
    } else if (isStrategyActive(outputDesc, STRATEGY_MEDIA)) {
        device = getDeviceForStrategy(STRATEGY_MEDIA, fromCache);
    } else if (isStrategyActive(outputDesc, STRATEGY_DTMF)) {
        device = getDeviceForStrategy(STRATEGY_DTMF, fromCache);
    } else if (isStrategyActive(outputDesc, STRATEGY_TRANSMITTED_THROUGH_SPEAKER)) {
        device = getDeviceForStrategy(STRATEGY_TRANSMITTED_THROUGH_SPEAKER, fromCache);
    } else if (isStrategyActive(outputDesc, STRATEGY_REROUTING)) {
        device = getDeviceForStrategy(STRATEGY_REROUTING, fromCache);
    }

    ALOGV("getNewOutputDevice() selected device %x", device);
    return device;
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
    if (isInCall()) {
        ALOGV("setPhoneState() in call state management: new state is %d", state);
        for (size_t j = 0; j < mOutputs.size(); j++) {
            audio_io_handle_t curOutput = mOutputs.keyAt(j);
            for (int stream = 0; stream < AUDIO_STREAM_CNT; stream++) {
                if (stream == AUDIO_STREAM_PATCH) {
                    continue;
                }

            handleIncallSonification((audio_stream_type_t)stream, false, true, curOutput);
            }
        }

        // force reevaluating accessibility routing when call starts
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

    sp<SwAudioOutputDescriptor> hwOutputDesc = mPrimaryOutput;

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
        ALOGV("Setting the delay from %dms to %dms", delayMs,
                MIN(delayMs, MAX_VOICE_CALL_START_DELAY_MS));
         delayMs = MIN(delayMs, MAX_VOICE_CALL_START_DELAY_MS);
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
        for (size_t j = 0; j < mOutputs.size(); j++) {
            audio_io_handle_t curOutput = mOutputs.keyAt(j);
            for (int stream = 0; stream < AUDIO_STREAM_CNT; stream++) {
               if (stream == AUDIO_STREAM_PATCH) {
                    continue;
                }
            handleIncallSonification((audio_stream_type_t)stream, true, true, curOutput);
           }
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
status_t AudioPolicyManagerCustom::stopSource(sp<SwAudioOutputDescriptor> outputDesc,
                                            audio_stream_type_t stream,
                                            bool forceDeviceUpdate)
{
    // always handle stream stop, check which stream type is stopping
    handleEventForBeacon(stream == AUDIO_STREAM_TTS ? STOPPING_BEACON : STOPPING_OUTPUT);

    // handle special case for sonification while in call
    if (isInCall()) {
        if (outputDesc->isDuplicated()) {
            handleIncallSonification(stream, false, false, outputDesc->mIoHandle);
            handleIncallSonification(stream, false, false, outputDesc->mIoHandle);
        }
        handleIncallSonification(stream, false, false, outputDesc->mIoHandle);
    }

    if (outputDesc->mRefCount[stream] > 0) {
        // decrement usage count of this stream on the output
        outputDesc->changeRefCount(stream, -1);

        // store time at which the stream was stopped - see isStreamActive()
        if (outputDesc->mRefCount[stream] == 0 || forceDeviceUpdate) {
            outputDesc->mStopTime[stream] = systemTime();
            audio_devices_t newDevice = getNewOutputDevice(outputDesc, false /*fromCache*/);
            // delay the device switch by twice the latency because stopOutput() is executed when
            // the track stop() command is received and at that time the audio track buffer can
            // still contain data that needs to be drained. The latency only covers the audio HAL
            // and kernel buffers. Also the latency does not always include additional delay in the
            // audio path (audio DSP, CODEC ...)
            setOutputDevice(outputDesc, newDevice, false, outputDesc->latency()*2);

            // force restoring the device selection on other active outputs if it differs from the
            // one being selected for this output
            for (size_t i = 0; i < mOutputs.size(); i++) {
                audio_io_handle_t curOutput = mOutputs.keyAt(i);
                sp<AudioOutputDescriptor> desc = mOutputs.valueAt(i);
                if (desc != outputDesc &&
                        desc->isActive() &&
                        outputDesc->sharesHwModuleWith(desc) &&
                        (newDevice != desc->device())) {
                    setOutputDevice(desc,
                                    getNewOutputDevice(desc, false /*fromCache*/),
                                    true,
                                    outputDesc->latency()*2);
                }
            }
            // update the outputs if stopping one with a stream that can affect notification routing
            handleNotificationRoutingForStream(stream);
        }
        return NO_ERROR;
    } else {
        ALOGW("stopOutput() refcount is already 0");
        return INVALID_OPERATION;
    }
}
status_t AudioPolicyManagerCustom::startSource(sp<SwAudioOutputDescriptor> outputDesc,
                                             audio_stream_type_t stream,
                                             audio_devices_t device,
                                             uint32_t *delayMs)
{
    // cannot start playback of STREAM_TTS if any other output is being used
    uint32_t beaconMuteLatency = 0;

    *delayMs = 0;
    if (stream == AUDIO_STREAM_TTS) {
        ALOGV("\t found BEACON stream");
        if (mOutputs.isAnyOutputActive(AUDIO_STREAM_TTS /*streamToIgnore*/)) {
            return INVALID_OPERATION;
        } else {
            beaconMuteLatency = handleEventForBeacon(STARTING_BEACON);
        }
    } else {
        // some playback other than beacon starts
        beaconMuteLatency = handleEventForBeacon(STARTING_OUTPUT);
    }

    // increment usage count for this stream on the requested output:
    // NOTE that the usage count is the same for duplicated output and hardware output which is
    // necessary for a correct control of hardware output routing by startOutput() and stopOutput()
    outputDesc->changeRefCount(stream, 1);

    if (outputDesc->mRefCount[stream] == 1 || device != AUDIO_DEVICE_NONE) {
        // starting an output being rerouted?
        if (device == AUDIO_DEVICE_NONE) {
            device = getNewOutputDevice(outputDesc, false /*fromCache*/);
        }
        routing_strategy strategy = getStrategy(stream);
        bool shouldWait = (strategy == STRATEGY_SONIFICATION) ||
                            (strategy == STRATEGY_SONIFICATION_RESPECTFUL) ||
                            (beaconMuteLatency > 0);
        uint32_t waitMs = beaconMuteLatency;
        bool force = false;
        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<AudioOutputDescriptor> desc = mOutputs.valueAt(i);
            if (desc != outputDesc) {
                // force a device change if any other output is managed by the same hw
                // module and has a current device selection that differs from selected device.
                // In this case, the audio HAL must receive the new device selection so that it can
                // change the device currently selected by the other active output.
                if (outputDesc->sharesHwModuleWith(desc) &&
                    desc->device() != device) {
                    force = true;
                }
                // wait for audio on other active outputs to be presented when starting
                // a notification so that audio focus effect can propagate, or that a mute/unmute
                // event occurred for beacon
                uint32_t latency = desc->latency();
                if (shouldWait && desc->isActive(latency * 2) && (waitMs < latency)) {
                    waitMs = latency;
                }
            }
        }
        uint32_t muteWaitMs = setOutputDevice(outputDesc, device, force);

        // handle special case for sonification while in call
        if (isInCall()) {
            handleIncallSonification(stream, true, false, outputDesc->mIoHandle);
        }

        // apply volume rules for current stream and device if necessary
        checkAndSetVolume(stream,
                          mStreams.valueFor(stream).getVolumeIndex(device),
                          outputDesc,
                          device);

        // update the outputs if starting an output with a stream that can affect notification
        // routing
        handleNotificationRoutingForStream(stream);

        // force reevaluating accessibility routing when ringtone or alarm starts
        if (strategy == STRATEGY_SONIFICATION) {
            mpClientInterface->invalidateStream(AUDIO_STREAM_ACCESSIBILITY);
        }
    }
    else {
            // handle special case for sonification while in call
            if (isInCall()) {
                handleIncallSonification(stream, true, false, outputDesc->mIoHandle);
              }
        }
    return NO_ERROR;
}
void AudioPolicyManagerCustom::handleIncallSonification(audio_stream_type_t stream,
                                                      bool starting, bool stateChange,
                                                      audio_io_handle_t output)
{
    if(!hasPrimaryOutput()) {
        return;
    }
    // no action needed for AUDIO_STREAM_PATCH stream type, it's for internal flinger tracks
    if (stream == AUDIO_STREAM_PATCH) {
        return;
    }
    // if the stream pertains to sonification strategy and we are in call we must
    // mute the stream if it is low visibility. If it is high visibility, we must play a tone
    // in the device used for phone strategy and play the tone if the selected device does not
    // interfere with the device used for phone strategy
    // if stateChange is true, we are called from setPhoneState() and we must mute or unmute as
    // many times as there are active tracks on the output
    const routing_strategy stream_strategy = getStrategy(stream);
    if ((stream_strategy == STRATEGY_SONIFICATION) ||
            ((stream_strategy == STRATEGY_SONIFICATION_RESPECTFUL))) {
        sp<SwAudioOutputDescriptor> outputDesc = mOutputs.valueFor(output);
        ALOGV("handleIncallSonification() stream %d starting %d device %x stateChange %d",
                stream, starting, outputDesc->mDevice, stateChange);
        if (outputDesc->mRefCount[stream]) {
            int muteCount = 1;
            if (stateChange) {
                muteCount = outputDesc->mRefCount[stream];
            }
            if (audio_is_low_visibility(stream)) {
                ALOGV("handleIncallSonification() low visibility, muteCount %d", muteCount);
                for (int i = 0; i < muteCount; i++) {
                    setStreamMute(stream, starting, outputDesc);
                }
            } else {
                ALOGV("handleIncallSonification() high visibility");
                if (outputDesc->device() &
                        getDeviceForStrategy(STRATEGY_PHONE, true /*fromCache*/)) {
                    ALOGV("handleIncallSonification() high visibility muted, muteCount %d", muteCount);
                    for (int i = 0; i < muteCount; i++) {
                        setStreamMute(stream, starting, outputDesc);
                    }
                }
                if (starting) {
                    mpClientInterface->startTone(AUDIO_POLICY_TONE_IN_CALL_NOTIFICATION,
                                                 AUDIO_STREAM_VOICE_CALL);
                } else {
                    mpClientInterface->stopTone();
                }
            }
        }
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
status_t AudioPolicyManagerCustom::checkAndSetVolume(audio_stream_type_t stream,
                                                   int index,
                                                   const sp<SwAudioOutputDescriptor>& outputDesc,
                                                   audio_devices_t device,
                                                   int delayMs, bool force)
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

        if (voiceVolume != mLastVoiceVolume && ((outputDesc == mPrimaryOutput) ||
            isDirectOutput(outputDesc->mIoHandle) || device & AUDIO_DEVICE_OUT_ALL_USB)) {
            mpClientInterface->setVoiceVolume(voiceVolume, delayMs);
            mLastVoiceVolume = voiceVolume;
        }
    }

    return NO_ERROR;
}
bool AudioPolicyManagerCustom::isDirectOutput(audio_io_handle_t output) {
    for (size_t i = 0; i < mOutputs.size(); i++) {
        audio_io_handle_t curOutput = mOutputs.keyAt(i);
        sp<SwAudioOutputDescriptor> desc = mOutputs.valueAt(i);
        if ((curOutput == output) && (desc->mFlags & AUDIO_OUTPUT_FLAG_DIRECT)) {
            return true;
        }
    }
    return false;
}
audio_io_handle_t AudioPolicyManagerCustom::getOutputForDevice(
        audio_devices_t device,
        audio_session_t session __unused,
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
            sp<AudioOutputDescriptor> outputDesc = new SwAudioOutputDescriptor(NULL,
                                                                               mpClientInterface);
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
    if (((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) != 0) &&
            (stream != AUDIO_STREAM_MUSIC)) {
        // compress should not be used for non-music streams
        ALOGE("Offloading only allowed with music stream");
        return 0;
    }
    /*
    * WFD audio routes back to target speaker when starting a ringtone playback.
    * This is because primary output is reused for ringtone, so output device is
    * updated based on SONIFICATION strategy for both ringtone and music playback.
    * The same issue is not seen on remoted_submix HAL based WFD audio because
    * primary output is not reused and a new output is created for ringtone playback.
    * Issue is fixed by updating output flag to AUDIO_OUTPUT_FLAG_FAST when there is
    * a non-music stream playback on WFD, so primary output is not reused for ringtone.
    */
    audio_devices_t availableOutputDeviceTypes = mAvailableOutputDevices.types();
    if ((availableOutputDeviceTypes & AUDIO_DEVICE_OUT_PROXY)
          && (stream != AUDIO_STREAM_MUSIC)) {
        ALOGD("WFD audio: use OUTPUT_FLAG_FAST for non music stream. flags:%x", flags );
        //For voip paths
        if(flags & AUDIO_OUTPUT_FLAG_DIRECT)
            flags = AUDIO_OUTPUT_FLAG_DIRECT;
        else //route every thing else to ULL path
            flags = AUDIO_OUTPUT_FLAG_FAST;
    }
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
    // only allow deep buffering for music stream type
    if (stream != AUDIO_STREAM_MUSIC) {
        flags = (audio_output_flags_t)(flags &~AUDIO_OUTPUT_FLAG_DEEP_BUFFER);
    }
    if (stream == AUDIO_STREAM_TTS) {
        flags = AUDIO_OUTPUT_FLAG_TTS;
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

    if (((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) == 0) ||
            !mEffects.isNonOffloadableEffectEnabled()) {
        profile = getProfileForDirectOutput(device,
                                           samplingRate,
                                           format,
                                           channelMask,
                                           (audio_output_flags_t)flags);
    }

    if (profile != 0) {
        sp<SwAudioOutputDescriptor> outputDesc = NULL;

        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<SwAudioOutputDescriptor> desc = mOutputs.valueAt(i);
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

        // if the selected profile is offloaded and no offload info was specified,
        // create a default one
        audio_offload_info_t defaultOffloadInfo = AUDIO_INFO_INITIALIZER;
        if ((profile->mFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) && !offloadInfo) {
            flags = (audio_output_flags_t)(flags | AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD);
            defaultOffloadInfo.sample_rate = samplingRate;
            defaultOffloadInfo.channel_mask = channelMask;
            defaultOffloadInfo.format = format;
            defaultOffloadInfo.stream_type = stream;
            defaultOffloadInfo.bit_rate = 0;
            defaultOffloadInfo.duration_us = -1;
            defaultOffloadInfo.has_video = true; // conservative
            defaultOffloadInfo.is_streaming = true; // likely
            offloadInfo = &defaultOffloadInfo;
        }

        outputDesc = new SwAudioOutputDescriptor(profile, mpClientInterface);
        outputDesc->mDevice = device;
        outputDesc->mLatency = 0;
        outputDesc->mFlags = (audio_output_flags_t)(outputDesc->mFlags | flags);
        audio_config_t config = AUDIO_CONFIG_INITIALIZER;
        config.sample_rate = samplingRate;
        config.channel_mask = channelMask;
        config.format = format;
        if (offloadInfo != NULL) {
            config.offload_info = *offloadInfo;
        }
        status = mpClientInterface->openOutput(profile->getModuleHandle(),
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
            // fall back to mixer output if possible when the direct output could not be open
            if (audio_is_linear_pcm(format) && samplingRate <= MAX_MIXER_SAMPLING_RATE) {
                goto non_direct_output;
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

    ALOGV("  getOutputForDevice() returns output %d", output);

    return output;
}

audio_devices_t AudioPolicyManagerCustom::getDeviceForStrategy(routing_strategy strategy, bool fromCache)
{
    audio_devices_t availableOutputDeviceTypes = mAvailableOutputDevices.types();
    audio_devices_t device = AUDIO_DEVICE_NONE;
    switch (strategy) {
        case STRATEGY_SONIFICATION:
        case STRATEGY_ENFORCED_AUDIBLE:
        case STRATEGY_ACCESSIBILITY:
        case STRATEGY_REROUTING:
        case STRATEGY_MEDIA:
            if (strategy != STRATEGY_SONIFICATION){
                // no sonification on WFD sink
                device |= availableOutputDeviceTypes & AUDIO_DEVICE_OUT_PROXY;
                if (device != AUDIO_DEVICE_NONE) {
                    ALOGV("Found proxy for strategy %d", strategy);
                    return device;
                }
            }
            break;
        default:
            ALOGV("getDeviceForStrategy() unknown strategy: %d", strategy);
            break;
    }
    device = AudioPolicyManager::getDeviceForStrategy(strategy, fromCache);
    return device;
}
}
