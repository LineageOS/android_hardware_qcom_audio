/*
 * Copyright (c) 2013-2017, The Linux Foundation. All rights reserved.
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
 *
 * This file was modified by Dolby Laboratories, Inc. The portions of the
 * code that are surrounded by "DOLBY..." are copyrighted and
 * licensed separately, as follows:
 *
 *  (C) 2015 Dolby Laboratories, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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

// A device mask for all audio output devices that are considered "remote" when evaluating
// active output devices in isStreamActiveRemotely()
#define APM_AUDIO_OUT_DEVICE_REMOTE_ALL  AUDIO_DEVICE_OUT_REMOTE_SUBMIX
// A device mask for all audio input and output devices where matching inputs/outputs on device
// type alone is not enough: the address must match too
#define APM_AUDIO_DEVICE_MATCH_ADDRESS_ALL (AUDIO_DEVICE_IN_REMOTE_SUBMIX | \
                                            AUDIO_DEVICE_OUT_REMOTE_SUBMIX)
#define SAMPLE_RATE_8000 8000
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
#ifdef DOLBY_ENABLE
#include "DolbyAudioPolicy_impl.h"
#endif // DOLBY_END

namespace android {
#ifdef VOICE_CONCURRENCY
audio_output_flags_t AudioPolicyManagerCustom::getFallBackPath()
{
    audio_output_flags_t flag = AUDIO_OUTPUT_FLAG_FAST;
    char propValue[PROPERTY_VALUE_MAX];

    if (property_get("vendor.voice.conc.fallbackpath", propValue, NULL)) {
        if (!strncmp(propValue, "deep-buffer", 11)) {
            flag = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
        }
        else if (!strncmp(propValue, "fast", 4)) {
            flag = AUDIO_OUTPUT_FLAG_FAST;
        }
        else {
            ALOGD("voice_conc:not a recognised path(%s) in prop voice.conc.fallbackpath",
                propValue);
        }
    }
    else {
        ALOGD("voice_conc:prop voice.conc.fallbackpath not set");
    }

    ALOGD("voice_conc:picked up flag(0x%x) from prop voice.conc.fallbackpath",
        flag);

    return flag;
}
#endif /*VOICE_CONCURRENCY*/

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
    ALOGD("setDeviceConnectionStateInt() device: 0x%X, state %d, address %s name %s",
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
                    ALOGW("HDMI sink not connected, do not route audio to HDMI out");
                    return INVALID_OPERATION;
                }
            }
#endif
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

            // Before checking outputs, broadcast connect event to allow HAL to retrieve dynamic
            // parameters on newly connected devices (instead of opening the outputs...)
            broadcastDeviceConnectionState(device, state, devDesc->mAddress);

            if (checkOutputsForDevice(devDesc, state, outputs, devDesc->mAddress) != NO_ERROR) {
                mAvailableOutputDevices.remove(devDesc);

                broadcastDeviceConnectionState(device, AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE,
                                               devDesc->mAddress);

                return INVALID_OPERATION;
            }
            // Propagate device availability to Engine
            mEngine->setDeviceConnectionState(devDesc, state);

            // outputs should never be empty here
            ALOG_ASSERT(outputs.size() != 0, "setDeviceConnectionState():"
                    "checkOutputsForDevice() returned no outputs but status OK");
            ALOGV("setDeviceConnectionState() checkOutputsForDevice() returned %zu outputs",
                  outputs.size());

            } break;
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

            // Send Disconnect to HALs
            broadcastDeviceConnectionState(device, state, devDesc->mAddress);

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
#ifdef DOLBY_ENABLE
        // Before closing the opened outputs, update endpoint property with device capabilities
        audio_devices_t audioOutputDevice = getDeviceForStrategy(getStrategy(AUDIO_STREAM_MUSIC), true);
        mDolbyAudioPolicy.setEndpointSystemProperty(audioOutputDevice, mHwModules);
#endif // DOLBY_END
        if (mEngine->getPhoneState() == AUDIO_MODE_IN_CALL && hasPrimaryOutput()) {
            audio_devices_t newDevice = getNewOutputDevice(mPrimaryOutput, false /*fromCache*/);
            updateCallRouting(newDevice);
        }

#ifdef FM_POWER_OPT
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
           param.addInt(String8("handle_fm"), (int)newDevice);
           mpClientInterface->setParameters(mPrimaryOutput->mIoHandle, param.toString());
        }
#endif /* FM_POWER_OPT end */

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

        if (state == AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE) {
            cleanUpForDevice(devDesc);
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

            // Before checking intputs, broadcast connect event to allow HAL to retrieve dynamic
            // parameters on newly connected devices (instead of opening the inputs...)
            broadcastDeviceConnectionState(device, state, devDesc->mAddress);

            if (checkInputsForDevice(devDesc, state, inputs, devDesc->mAddress) != NO_ERROR) {
                broadcastDeviceConnectionState(device, AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE,
                                               devDesc->mAddress);
                return INVALID_OPERATION;
            }

            index = mAvailableInputDevices.add(devDesc);
            if (index >= 0) {
                mAvailableInputDevices[index]->attach(module);
            } else {
                return NO_MEMORY;
            }

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
            broadcastDeviceConnectionState(device, state, devDesc->mAddress);

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

        if (state == AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE) {
            cleanUpForDevice(devDesc);
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

     if (mMasterMono) {
        return false; // no offloading if mono is set.
     }

#ifdef VOICE_CONCURRENCY
    char concpropValue[PROPERTY_VALUE_MAX];
    if (property_get("vendor.voice.playback.conc.disabled", concpropValue, NULL)) {
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

    if (property_get("vendor.audio.rec.playback.conc.disabled", recConcPropValue, NULL)) {
        prop_rec_play_enabled = atoi(recConcPropValue) || !strncmp("true", recConcPropValue, 4);
    }

    if ((prop_rec_play_enabled) &&
         ((true == mIsInputRequestOnProgress) || (mInputs.activeInputsCountOnDevices() > 0))) {
        ALOGD("copl: blocking  compress offload for record concurrency");
        return false;
    }
#endif
    // Check if stream type is music, then only allow offload as of now.
    if (offloadInfo.stream_type != AUDIO_STREAM_MUSIC)
    {
        ALOGV("isOffloadSupported: stream_type != MUSIC, returning false");
        return false;
    }

    // Check if offload has been disabled
    bool offloadDisabled = property_get_bool("audio.offload.disable", false);
    if (offloadDisabled) {
        ALOGI("offload disabled by audio.offload.disable=%d", offloadDisabled);
        return false;
    }

    //check if it's multi-channel AAC (includes sub formats) and FLAC format
    if ((popcount(offloadInfo.channel_mask) > 2) &&
        (((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC) ||
        ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_VORBIS))) {
        ALOGD("offload disabled for multi-channel AAC,FLAC and VORBIS format");
        return false;
    }

#ifdef AUDIO_EXTN_FORMATS_ENABLED
    //check if it's multi-channel FLAC/ALAC/WMA format with sample rate > 48k
    if ((popcount(offloadInfo.channel_mask) > 2) &&
        (((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_FLAC) ||
        (((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_ALAC) && (offloadInfo.sample_rate > 48000)) ||
        (((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_WMA) && (offloadInfo.sample_rate > 48000)) ||
        (((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_WMA_PRO) && (offloadInfo.sample_rate > 48000)) ||
        ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC_ADTS))) {
            ALOGD("offload disabled for multi-channel FLAC/ALAC/WMA/AAC_ADTS clips with sample rate > 48kHz");
        return false;
    }
#endif
    //TODO: enable audio offloading with video when ready
    const bool allowOffloadWithVideo =
            property_get_bool("audio.offload.video", false /* default_value */);
    if (offloadInfo.has_video && !allowOffloadWithVideo) {
        ALOGV("isOffloadSupported: has_video == true, returning false");
        return false;
    }

    const bool allowOffloadStreamingWithVideo = property_get_bool("vendor.audio.av.streaming.offload.enable",
                                                               false /*default value*/);
    if (offloadInfo.has_video && offloadInfo.is_streaming && !allowOffloadStreamingWithVideo) {
        ALOGW("offload disabled by vendor.audio.av.streaming.offload.enable %d",allowOffloadStreamingWithVideo);
        return false;
    }

    //If duration is less than minimum value defined in property, return false
    char propValue[PROPERTY_VALUE_MAX];
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
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_VORBIS))
            return false;

#ifdef AUDIO_EXTN_FORMATS_ENABLED
        if (((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_WMA) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_WMA_PRO) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_ALAC) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_APE) ||
            ((offloadInfo.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC_ADTS))
            return false;
#endif
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

void AudioPolicyManagerCustom::setPhoneState(audio_mode_t state)
{
    ALOGD("setPhoneState() state %d", state);
    // store previous phone state for management of sonification strategy below
    audio_devices_t newDevice = AUDIO_DEVICE_NONE;
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
        for (size_t j = 0; j < mOutputs.size(); j++) {
            audio_io_handle_t curOutput = mOutputs.keyAt(j);
            for (int stream = 0; stream < AUDIO_STREAM_FOR_POLICY_CNT; stream++) {
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
#ifdef VOICE_CONCURRENCY
    char propValue[PROPERTY_VALUE_MAX];
    bool prop_playback_enabled = false, prop_rec_enabled=false, prop_voip_enabled = false;

    if(property_get("vendor.voice.playback.conc.disabled", propValue, NULL)) {
        prop_playback_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if(property_get("vendor.voice.record.conc.disabled", propValue, NULL)) {
        prop_rec_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if(property_get("vendor.voice.voip.conc.disabled", propValue, NULL)) {
        prop_voip_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if ((AUDIO_MODE_IN_CALL != oldState) && (AUDIO_MODE_IN_CALL == state)) {
        ALOGD("voice_conc:Entering to call mode oldState :: %d state::%d ",
            oldState, state);
        mvoice_call_state = state;
        if (prop_rec_enabled) {
            //Close all active inputs
            Vector<sp <AudioInputDescriptor> > activeInputs = mInputs.getActiveInputs();
            if (activeInputs.size() != 0) {
                for (size_t i = 0; i <  activeInputs.size(); i++) {
                    sp<AudioInputDescriptor> activeInput = activeInputs[i];
                    switch(activeInput->inputSource()) {
                        case AUDIO_SOURCE_VOICE_UPLINK:
                        case AUDIO_SOURCE_VOICE_DOWNLINK:
                        case AUDIO_SOURCE_VOICE_CALL:
                            ALOGD("voice_conc:FOUND active input during call active: %d",activeInput->inputSource());
                        break;

                        case  AUDIO_SOURCE_VOICE_COMMUNICATION:
                            if(prop_voip_enabled) {
                                ALOGD("voice_conc:CLOSING VoIP input source on call setup :%d ",activeInput->inputSource());
                                AudioSessionCollection activeSessions = activeInput->getAudioSessions(true);
                                audio_session_t activeSession = activeSessions.keyAt(0);
                                stopInput(activeInput->mIoHandle, activeSession);
                                releaseInput(activeInput->mIoHandle, activeSession);
                            }
                        break;

                       default:
                           ALOGD("voice_conc:CLOSING input on call setup  for inputSource: %d",activeInput->inputSource());
                           AudioSessionCollection activeSessions = activeInput->getAudioSessions(true);
                           audio_session_t activeSession = activeSessions.keyAt(0);
                           stopInput(activeInput->mIoHandle, activeSession);
                           releaseInput(activeInput->mIoHandle, activeSession);
                       break;
                   }
               }
           }
        } else if (prop_voip_enabled) {
            Vector<sp <AudioInputDescriptor> > activeInputs = mInputs.getActiveInputs();
            if (activeInputs.size() != 0) {
                for (size_t i = 0; i <  activeInputs.size(); i++) {
                    sp<AudioInputDescriptor> activeInput = activeInputs[i];
                    if (AUDIO_SOURCE_VOICE_COMMUNICATION == activeInput->inputSource()) {
                        ALOGD("voice_conc:CLOSING VoIP on call setup : %d",activeInput->inputSource());
                        AudioSessionCollection activeSessions = activeInput->getAudioSessions(true);
                        audio_session_t activeSession = activeSessions.keyAt(0);
                        stopInput(activeInput->mIoHandle, activeSession);
                        releaseInput(activeInput->mIoHandle, activeSession);
                    }
                }
            }
        }
        if (prop_playback_enabled) {
            // Move tracks associated to this strategy from previous output to new output
            for (int i = AUDIO_STREAM_SYSTEM; i < AUDIO_STREAM_FOR_POLICY_CNT; i++) {
                ALOGV("voice_conc:Invalidate on call mode for stream :: %d ", i);
                if (AUDIO_OUTPUT_FLAG_DEEP_BUFFER == mFallBackflag) {
                    if ((AUDIO_STREAM_MUSIC == i) ||
                        (AUDIO_STREAM_VOICE_CALL == i) ) {
                        ALOGD("voice_conc:Invalidate stream type %d", i);
                        mpClientInterface->invalidateStream((audio_stream_type_t)i);
                    }
                } else if (AUDIO_OUTPUT_FLAG_FAST == mFallBackflag) {
                    ALOGD("voice_conc:Invalidate stream type %d", i);
                    mpClientInterface->invalidateStream((audio_stream_type_t)i);
                }
            }
        }

        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<SwAudioOutputDescriptor> outputDesc = mOutputs.valueAt(i);
            if ( (outputDesc == NULL) || (outputDesc->mProfile == NULL)) {
               ALOGD("voice_conc:ouput desc / profile is NULL");
               continue;
            }

            bool isFastFallBackNeeded =
               ((AUDIO_OUTPUT_FLAG_DEEP_BUFFER | AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD | AUDIO_OUTPUT_FLAG_DIRECT_PCM) & outputDesc->mProfile->getFlags());

            if ((AUDIO_OUTPUT_FLAG_FAST == mFallBackflag) && isFastFallBackNeeded) {
                if (((!outputDesc->isDuplicated() && outputDesc->mProfile->getFlags() & AUDIO_OUTPUT_FLAG_PRIMARY))
                            && prop_playback_enabled) {
                    ALOGD("voice_conc:calling suspendOutput on call mode for primary output");
                    mpClientInterface->suspendOutput(mOutputs.keyAt(i));
                } //Close compress all sessions
                else if ((outputDesc->mProfile->getFlags() & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)
                                &&  prop_playback_enabled) {
                    ALOGD("voice_conc:calling closeOutput on call mode for COMPRESS output");
                    closeOutput(mOutputs.keyAt(i));
                }
                else if ((outputDesc->mProfile->getFlags() & AUDIO_OUTPUT_FLAG_VOIP_RX)
                                && prop_voip_enabled) {
                    ALOGD("voice_conc:calling closeOutput on call mode for DIRECT  output");
                    closeOutput(mOutputs.keyAt(i));
                }
            } else if (AUDIO_OUTPUT_FLAG_DEEP_BUFFER == mFallBackflag) {
                if ((outputDesc->mProfile->getFlags() & AUDIO_OUTPUT_FLAG_DIRECT)
                                &&  prop_playback_enabled) {
                    ALOGD("voice_conc:calling closeOutput on call mode for COMPRESS output");
                    closeOutput(mOutputs.keyAt(i));
                }
            }
        }
    }

    if ((AUDIO_MODE_IN_CALL == oldState || AUDIO_MODE_IN_COMMUNICATION == oldState) &&
       (AUDIO_MODE_NORMAL == state) && prop_playback_enabled && mvoice_call_state) {
        ALOGD("voice_conc:EXITING from call mode oldState :: %d state::%d \n",oldState, state);
        mvoice_call_state = 0;
        if (AUDIO_OUTPUT_FLAG_FAST == mFallBackflag) {
            //restore PCM (deep-buffer) output after call termination
            for (size_t i = 0; i < mOutputs.size(); i++) {
                sp<SwAudioOutputDescriptor> outputDesc = mOutputs.valueAt(i);
                if ( (outputDesc == NULL) || (outputDesc->mProfile == NULL)) {
                   ALOGD("voice_conc:ouput desc / profile is NULL");
                   continue;
                }
                if (!outputDesc->isDuplicated() && outputDesc->mProfile->getFlags() & AUDIO_OUTPUT_FLAG_PRIMARY) {
                    ALOGD("voice_conc:calling restoreOutput after call mode for primary output");
                    mpClientInterface->restoreOutput(mOutputs.keyAt(i));
                }
           }
        }
       //call invalidate tracks so that any open streams can fall back to deep buffer/compress path from ULL
        for (int i = AUDIO_STREAM_SYSTEM; i < AUDIO_STREAM_FOR_POLICY_CNT; i++) {
            ALOGV("voice_conc:Invalidate on call mode for stream :: %d ", i);
            if (AUDIO_OUTPUT_FLAG_DEEP_BUFFER == mFallBackflag) {
                if ((AUDIO_STREAM_MUSIC == i) ||
                    (AUDIO_STREAM_VOICE_CALL == i) ) {
                    mpClientInterface->invalidateStream((audio_stream_type_t)i);
                }
            } else if (AUDIO_OUTPUT_FLAG_FAST == mFallBackflag) {
                mpClientInterface->invalidateStream((audio_stream_type_t)i);
            }
        }
    }

#endif
#ifdef RECORD_PLAY_CONCURRENCY
    char recConcPropValue[PROPERTY_VALUE_MAX];
    bool prop_rec_play_enabled = false;

    if (property_get("vendor.audio.rec.playback.conc.disabled", recConcPropValue, NULL)) {
        prop_rec_play_enabled = atoi(recConcPropValue) || !strncmp("true", recConcPropValue, 4);
    }
    if (prop_rec_play_enabled) {
        if (AUDIO_MODE_IN_COMMUNICATION == mEngine->getPhoneState()) {
            ALOGD("phone state changed to MODE_IN_COMM invlaidating music and voice streams");
            // call invalidate for voice streams, so that it can use deepbuffer with VoIP out device from HAL
            mpClientInterface->invalidateStream(AUDIO_STREAM_VOICE_CALL);
            // call invalidate for music, so that compress will fallback to deep-buffer with VoIP out device
            mpClientInterface->invalidateStream(AUDIO_STREAM_MUSIC);

            // close compress output to make sure session will be closed before timeout(60sec)
            for (size_t i = 0; i < mOutputs.size(); i++) {

                sp<SwAudioOutputDescriptor> outputDesc = mOutputs.valueAt(i);
                if ((outputDesc == NULL) || (outputDesc->mProfile == NULL)) {
                   ALOGD("ouput desc / profile is NULL");
                   continue;
                }

                if (outputDesc->mProfile->getFlags() & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
                    ALOGD("calling closeOutput on call mode for COMPRESS output");
                    closeOutput(mOutputs.keyAt(i));
                }
            }
        } else if ((oldState == AUDIO_MODE_IN_COMMUNICATION) &&
                    (mEngine->getPhoneState() == AUDIO_MODE_NORMAL)) {
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
    //update device for all non-primary outputs
    for (size_t i = 0; i < mOutputs.size(); i++) {
        audio_io_handle_t output = mOutputs.keyAt(i);
        if (output != mPrimaryOutput->mIoHandle) {
            newDevice = getNewOutputDevice(mOutputs.valueFor(output), false /*fromCache*/);
            setOutputDevice(mOutputs.valueFor(output), newDevice, (newDevice != AUDIO_DEVICE_NONE));
        }
    }
    // if entering in call state, handle special case of active streams
    // pertaining to sonification strategy see handleIncallSonification()
    if (isStateInCall(state)) {
        ALOGV("setPhoneState() in call state management: new state is %d", state);
        for (size_t j = 0; j < mOutputs.size(); j++) {
            audio_io_handle_t curOutput = mOutputs.keyAt(j);
            for (int stream = 0; stream < AUDIO_STREAM_FOR_POLICY_CNT; stream++) {
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

void AudioPolicyManagerCustom::setForceUse(audio_policy_force_use_t usage,
                                         audio_policy_forced_cfg_t config)
{
    ALOGD("setForceUse() usage %d, config %d, mPhoneState %d", usage, config, mEngine->getPhoneState());
    if (config == mEngine->getForceUse(usage)) {
        return;
    }

    audio_policy_forced_cfg_t originalConfig = mEngine->getForceUse(usage);
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

    // Did surround forced use change?
    if ((usage == AUDIO_POLICY_FORCE_FOR_ENCODED_SURROUND)
            && (originalConfig != config)) {
        const char *device_address  = "";
        // Is it currently connected? If so then cycle the connection
        // so that the supported surround formats will be reloaded.
        //
        // FIXME As S/PDIF is not a removable device we have to handle this differently.
        // Probably by updating the device descriptor directly and manually
        // tearing down active playback on S/PDIF
        if (getDeviceConnectionState(AUDIO_DEVICE_OUT_HDMI, device_address) ==
                                             AUDIO_POLICY_DEVICE_STATE_AVAILABLE) {
            // Disconnect and reconnect output devices so that the surround
            // encodings can be updated.
            const char *device_name = "";
            // disconnect
            setDeviceConnectionStateInt(AUDIO_DEVICE_OUT_HDMI,
                        AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE,
                        device_address, device_name);
            // reconnect
            setDeviceConnectionStateInt(AUDIO_DEVICE_OUT_HDMI,
                        AUDIO_POLICY_DEVICE_STATE_AVAILABLE,
                        device_address, device_name);
        }
    }

    if (mEngine->getPhoneState() == AUDIO_MODE_IN_CALL && hasPrimaryOutput()) {
        audio_devices_t newDevice = getNewOutputDevice(mPrimaryOutput, true /*fromCache*/);
        updateCallRouting(newDevice);
    }
    // Use reverse loop to make sure any low latency usecases (generally tones)
    // are not routed before non LL usecases (generally music).
    // We can safely assume that LL output would always have lower index,
    // and use this work-around to avoid routing of output with music stream
    // from the context of short lived LL output.
    // Note: in case output's share backend(HAL sharing is implicit) all outputs
    //       gets routing update while processing first output itself.
    for (size_t i = mOutputs.size(); i > 0; i--) {
        sp<SwAudioOutputDescriptor> outputDesc = mOutputs.valueAt(i-1);
        audio_devices_t newDevice = getNewOutputDevice(outputDesc, true /*fromCache*/);
        if ((mEngine->getPhoneState() != AUDIO_MODE_IN_CALL) || outputDesc != mPrimaryOutput) {
            setOutputDevice(outputDesc, newDevice, (newDevice != AUDIO_DEVICE_NONE));
        }
        if (forceVolumeReeval && (newDevice != AUDIO_DEVICE_NONE)) {
            applyStreamVolumes(outputDesc, newDevice, 0, true);
        }
    }

    Vector<sp <AudioInputDescriptor> > activeInputs = mInputs.getActiveInputs();
    for (size_t i = 0; i <  activeInputs.size(); i++) {
        sp<AudioInputDescriptor> activeDesc = activeInputs[i];
        audio_devices_t newDevice = getNewInputDevice(activeDesc);
        // Force new input selection if the new device can not be reached via current input
        if (activeDesc->mProfile->getSupportedDevices().types() & (newDevice & ~AUDIO_DEVICE_BIT_IN)) {
            setInputDevice(activeDesc->mIoHandle, newDevice);
        } else {
            closeInput(activeDesc->mIoHandle);
        }
    }
}

status_t AudioPolicyManagerCustom::stopSource(const sp<AudioOutputDescriptor>& outputDesc,
                                            audio_stream_type_t stream,
                                            bool forceDeviceUpdate)
{
    if (stream < 0 || stream >= AUDIO_STREAM_CNT) {
        ALOGW("stopSource() invalid stream %d", stream);
        return INVALID_OPERATION;
    }
    // always handle stream stop, check which stream type is stopping
    handleEventForBeacon(stream == AUDIO_STREAM_TTS ? STOPPING_BEACON : STOPPING_OUTPUT);

    // handle special case for sonification while in call
    if (isInCall()) {
        if (outputDesc->isDuplicated()) {
            handleIncallSonification(stream, false, false, outputDesc->subOutput1()->mIoHandle);
            handleIncallSonification(stream, false, false, outputDesc->subOutput2()->mIoHandle);
        }
        handleIncallSonification(stream, false, false, outputDesc->mIoHandle);
    }

    if (outputDesc->mRefCount[stream] > 0) {
        // decrement usage count of this stream on the output
        outputDesc->changeRefCount(stream, -1);

        // store time at which the stream was stopped - see isStreamActive()
        if (outputDesc->mRefCount[stream] == 0 || forceDeviceUpdate) {
            outputDesc->mStopTime[stream] = systemTime();
            audio_devices_t prevDevice = outputDesc->device();
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
                        audio_devices_t dev = getNewOutputDevice(mOutputs.valueFor(curOutput), false /*fromCache*/);
                        bool force = desc->device() != dev;
                        uint32_t delayMs;
                        if (dev == prevDevice) {
                            delayMs = 0;
                        } else {
                            delayMs = outputDesc->latency()*2;
                        }
                        setOutputDevice(desc,
                                    dev,
                                    force,
                                    delayMs);
                        // re-apply device specific volume if not done by setOutputDevice()
                        if (!force) {
                            applyStreamVolumes(desc, dev, delayMs);
                        }
                }
            }
            // update the outputs if stopping one with a stream that can affect notification routing
            handleNotificationRoutingForStream(stream);
        }
        if (stream == AUDIO_STREAM_MUSIC) {
            selectOutputForMusicEffects();
        }
        return NO_ERROR;
    } else {
        ALOGW("stopOutput() refcount is already 0");
        return INVALID_OPERATION;
    }
}

status_t AudioPolicyManagerCustom::startSource(const sp<AudioOutputDescriptor>& outputDesc,
                                             audio_stream_type_t stream,
                                             audio_devices_t device,
                                             uint32_t *delayMs)
{
    // cannot start playback of STREAM_TTS if any other output is being used
    uint32_t beaconMuteLatency = 0;

    if (stream < 0 || stream >= AUDIO_STREAM_CNT) {
        ALOGW("startSource() invalid stream %d", stream);
        return INVALID_OPERATION;
    }

    *delayMs = 0;
    if (stream == AUDIO_STREAM_TTS) {
        ALOGV("\t found BEACON stream");
        if (!mTtsOutputAvailable && mOutputs.isAnyOutputActive(AUDIO_STREAM_TTS /*streamToIgnore*/)) {
            return INVALID_OPERATION;
        } else {
            beaconMuteLatency = handleEventForBeacon(STARTING_BEACON);
        }
    } else {
        // some playback other than beacon starts
        beaconMuteLatency = handleEventForBeacon(STARTING_OUTPUT);
    }

    // check active before incrementing usage count
    bool force = !outputDesc->isActive();

    // increment usage count for this stream on the requested output:
    // NOTE that the usage count is the same for duplicated output and hardware output which is
    // necessary for a correct control of hardware output routing by startOutput() and stopOutput()
    outputDesc->changeRefCount(stream, 1);

    if (stream == AUDIO_STREAM_MUSIC) {
        selectOutputForMusicEffects();
    }

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
                          mVolumeCurves->getVolumeIndex(stream, device),
                          outputDesc,
                          device);

        // update the outputs if starting an output with a stream that can affect notification
        // routing
        handleNotificationRoutingForStream(stream);

        // force reevaluating accessibility routing when ringtone or alarm starts
        if (strategy == STRATEGY_SONIFICATION) {
            mpClientInterface->invalidateStream(AUDIO_STREAM_ACCESSIBILITY);
        }
    } else {
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
                                                   const sp<AudioOutputDescriptor>& outputDesc,
                                                   audio_devices_t device,
                                                   int delayMs,
                                                   bool force)
{
    if (stream < 0 || stream >= AUDIO_STREAM_CNT) {
        ALOGW("checkAndSetVolume() invalid stream %d", stream);
        return INVALID_OPERATION;
    }
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
            voiceVolume = (float)index/(float)mVolumeCurves->getVolumeIndexMax(stream);
        } else {
            voiceVolume = 1.0;
        }

        if (voiceVolume != mLastVoiceVolume) {
            mpClientInterface->setVoiceVolume(voiceVolume, delayMs);
            mLastVoiceVolume = voiceVolume;
        }
#ifdef FM_POWER_OPT
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
#endif /* FM_POWER_OPT end */
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

bool static tryForDirectPCM(audio_output_flags_t flags)
{
    bool trackDirectPCM = false;  // Output request for track created by other apps
    if (flags == AUDIO_OUTPUT_FLAG_NONE) {
        trackDirectPCM = property_get_bool("vendor.audio.offload.track.enable", true);
    }
    return trackDirectPCM;
}

status_t AudioPolicyManagerCustom::getOutputForAttr(const audio_attributes_t *attr,
                                                    audio_io_handle_t *output,
                                                    audio_session_t session,
                                                    audio_stream_type_t *stream,
                                                    uid_t uid,
                                                    const audio_config_t *config,
                                                    audio_output_flags_t flags,
                                                    audio_port_handle_t *selectedDeviceId,
                                                    audio_port_handle_t *portId)
{
    audio_offload_info_t tOffloadInfo = AUDIO_INFO_INITIALIZER;
    audio_config_t tConfig;

    uint32_t bitWidth = (audio_bytes_per_sample(config->format) * 8);


    memcpy(&tConfig, config, sizeof(audio_config_t));
    if (flags == AUDIO_OUTPUT_FLAG_DIRECT || tryForDirectPCM(flags) &&
        (!memcmp(&config->offload_info, &tOffloadInfo, sizeof(audio_offload_info_t)))) {
        tConfig.offload_info.sample_rate = config->sample_rate;
        tConfig.offload_info.channel_mask = config->channel_mask;
        tConfig.offload_info.format = config->format;
        tConfig.offload_info.stream_type = *stream;
        tConfig.offload_info.bit_width = bitWidth;
        if (attr != NULL) {
            ALOGV("found attribute .. setting usage %d ", attr->usage);
            tConfig.offload_info.usage = attr->usage;
        } else {
            ALOGD("%s:: attribute is NULL .. no usage set", __func__);
        }
    }

    return AudioPolicyManager::getOutputForAttr(attr, output, session, stream,
                                                (uid_t)uid, &tConfig,
                                                flags, selectedDeviceId,
                                                portId);
}

audio_io_handle_t AudioPolicyManagerCustom::getOutputForDevice(
        audio_devices_t device,
        audio_session_t session,
        audio_stream_type_t stream,
        uint32_t samplingRate,
        audio_format_t format,
        audio_channel_mask_t channelMask,
        audio_output_flags_t flags,
        const audio_offload_info_t *offloadInfo)
{
    audio_io_handle_t output = AUDIO_IO_HANDLE_NONE;
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

    if ((stream == AUDIO_STREAM_VOICE_CALL) &&
        (channelMask == 1) &&
        (samplingRate == 8000 || samplingRate == 16000)) {
        // Allow Voip direct output only if:
        // audio mode is MODE_IN_COMMUNCATION; AND
        // voip output is not opened already; AND
        // requested sample rate matches with that of voip input stream (if opened already)
        int value = 0;
        uint32_t mode = 0, voipOutCount = 1, voipSampleRate = 1;
        String8 valueStr = mpClientInterface->getParameters((audio_io_handle_t)0,
                                                           String8("audio_mode"));
        AudioParameter result = AudioParameter(valueStr);
        if (result.getInt(String8("audio_mode"), value) == NO_ERROR) {
            mode = value;
        }

        valueStr =  mpClientInterface->getParameters((audio_io_handle_t)0,
                                              String8("voip_out_stream_count"));
        result = AudioParameter(valueStr);
        if (result.getInt(String8("voip_out_stream_count"), value) == NO_ERROR) {
            voipOutCount = value;
        }

        valueStr = mpClientInterface->getParameters((audio_io_handle_t)0,
                                              String8("voip_sample_rate"));
        result = AudioParameter(valueStr);
        if (result.getInt(String8("voip_sample_rate"), value) == NO_ERROR) {
            voipSampleRate = value;
        }

        if ((mode == AUDIO_MODE_IN_COMMUNICATION) && (voipOutCount == 0) &&
            ((voipSampleRate == 0) || (voipSampleRate == samplingRate))) {
            if (audio_is_linear_pcm(format)) {
                char propValue[PROPERTY_VALUE_MAX] = {0};
                property_get("vendor.voice.path.for.pcm.voip", propValue, "0");
                bool voipPcmSysPropEnabled = !strncmp("true", propValue, sizeof("true"));
                if (voipPcmSysPropEnabled && (format == AUDIO_FORMAT_PCM_16_BIT)) {
                    flags = (audio_output_flags_t)(AUDIO_OUTPUT_FLAG_VOIP_RX |
                                                 AUDIO_OUTPUT_FLAG_DIRECT);
                    ALOGD("Set VoIP and Direct output flags for PCM format");
                }
            }
        }
    }

#ifdef VOICE_CONCURRENCY
    char propValue[PROPERTY_VALUE_MAX];
    bool prop_play_enabled=false, prop_voip_enabled = false;

    if(property_get("vendor.voice.playback.conc.disabled", propValue, NULL)) {
       prop_play_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if(property_get("vendor.voice.voip.conc.disabled", propValue, NULL)) {
       prop_voip_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    bool isDeepBufferFallBackNeeded =
        ((AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD | AUDIO_OUTPUT_FLAG_DIRECT_PCM) & flags);
    bool isFastFallBackNeeded =
        ((AUDIO_OUTPUT_FLAG_DEEP_BUFFER | AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD | AUDIO_OUTPUT_FLAG_DIRECT_PCM) & flags);

    if (prop_play_enabled && mvoice_call_state) {
        //check if voice call is active  / running in background
        if((AUDIO_MODE_IN_CALL == mEngine->getPhoneState()) ||
             ((AUDIO_MODE_IN_CALL == mPrevPhoneState)
                && (AUDIO_MODE_IN_COMMUNICATION == mEngine->getPhoneState())))
        {
            if(AUDIO_OUTPUT_FLAG_VOIP_RX  & flags) {
                if(prop_voip_enabled) {
                   ALOGD("voice_conc:getoutput:IN call mode return no o/p for VoIP %x",
                        flags );
                   return 0;
                }
            }
            else {
                if (isFastFallBackNeeded &&
                    (AUDIO_OUTPUT_FLAG_FAST == mFallBackflag)) {
                    ALOGD("voice_conc:IN call mode adding ULL flags .. flags: %x ", flags );
                    flags = AUDIO_OUTPUT_FLAG_FAST;
                } else if (isDeepBufferFallBackNeeded &&
                           (AUDIO_OUTPUT_FLAG_DEEP_BUFFER == mFallBackflag)) {
                    if (AUDIO_STREAM_MUSIC == stream) {
                        flags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
                        ALOGD("voice_conc:IN call mode adding deep-buffer flags %x ", flags );
                    }
                    else {
                        flags = AUDIO_OUTPUT_FLAG_FAST;
                        ALOGD("voice_conc:IN call mode adding fast flags %x ", flags );
                    }
                }
            }
        }
    } else if (prop_voip_enabled && mvoice_call_state) {
        //check if voice call is active  / running in background
        //some of VoIP apps(like SIP2SIP call) supports resume of VoIP call when call in progress
        //return only ULL ouput
        if((AUDIO_MODE_IN_CALL == mEngine->getPhoneState()) ||
             ((AUDIO_MODE_IN_CALL == mPrevPhoneState)
                && (AUDIO_MODE_IN_COMMUNICATION == mEngine->getPhoneState())))
        {
            if(AUDIO_OUTPUT_FLAG_VOIP_RX  & flags) {
                    ALOGD("voice_conc:getoutput:IN call mode return no o/p for VoIP %x",
                        flags );
               return 0;
            }
        }
     }
#endif
#ifdef RECORD_PLAY_CONCURRENCY
    char recConcPropValue[PROPERTY_VALUE_MAX];
    bool prop_rec_play_enabled = false;

    if (property_get("vendor.audio.rec.playback.conc.disabled", recConcPropValue, NULL)) {
        prop_rec_play_enabled = atoi(recConcPropValue) || !strncmp("true", recConcPropValue, 4);
    }
    if ((prop_rec_play_enabled) &&
            ((true == mIsInputRequestOnProgress) || (mInputs.activeInputsCountOnDevices() > 0))) {
        if (AUDIO_MODE_IN_COMMUNICATION == mEngine->getPhoneState()) {
            if (AUDIO_OUTPUT_FLAG_VOIP_RX & flags) {
                // allow VoIP using voice path
                // Do nothing
            } else if((flags & AUDIO_OUTPUT_FLAG_FAST) == 0) {
                ALOGD("voice_conc:MODE_IN_COMM is setforcing deep buffer output for non ULL... flags: %x", flags);
                // use deep buffer path for all non ULL outputs
                flags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
            }
        } else if ((flags & AUDIO_OUTPUT_FLAG_FAST) == 0) {
            ALOGD("voice_conc:Record mode is on forcing deep buffer output for non ULL... flags: %x ", flags);
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

#ifdef AUDIO_EXTN_AFE_PROXY_ENABLED
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
#endif

    // open a direct output if required by specified parameters
    // force direct flag if offload flag is set: offloading implies a direct output stream
    // and all common behaviors are driven by checking only the direct flag
    // this should normally be set appropriately in the policy configuration file
    if ((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) != 0) {
        flags = (audio_output_flags_t)(flags | AUDIO_OUTPUT_FLAG_DIRECT);
    }
    if ((flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) != 0) {
        flags = (audio_output_flags_t)(flags | AUDIO_OUTPUT_FLAG_DIRECT);
    }

    // Do internal direct magic here
    bool offload_disabled = property_get_bool("audio.offload.disable", false);
    if ((flags == AUDIO_OUTPUT_FLAG_NONE) &&
        (stream == AUDIO_STREAM_MUSIC) &&
        (offloadInfo != NULL) && !offload_disabled &&
        ((offloadInfo->usage == AUDIO_USAGE_MEDIA) || (offloadInfo->usage == AUDIO_USAGE_GAME))) {
        audio_output_flags_t old_flags = flags;
        flags = (audio_output_flags_t)(AUDIO_OUTPUT_FLAG_DIRECT);
        ALOGD("Force Direct Flag .. old flags(0x%x)", old_flags);
    } else if (flags == AUDIO_OUTPUT_FLAG_DIRECT &&
                (offload_disabled || stream != AUDIO_STREAM_MUSIC)) {
        ALOGD("Offloading is disabled or Stream is not music --> Force Remove Direct Flag");
        flags = (audio_output_flags_t)(AUDIO_OUTPUT_FLAG_NONE);
    }

    // check if direct output for pcm/track offload already exits
    bool direct_pcm_already_in_use = false;
    if (flags == AUDIO_OUTPUT_FLAG_DIRECT) {
        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<SwAudioOutputDescriptor> desc = mOutputs.valueAt(i);
            if (desc->mFlags == AUDIO_OUTPUT_FLAG_DIRECT) {
                direct_pcm_already_in_use = true;
                ALOGD("Direct PCM already in use");
                break;
            }
        }
        // prevent direct pcm for non-music stream blindly if direct pcm already in use
        // for other music stream concurrency is handled after checking direct ouput usage
        // and checking client
        if (direct_pcm_already_in_use == true && stream != AUDIO_STREAM_MUSIC) {
            ALOGD("disabling offload for non music stream as direct pcm is already in use");
            flags = (audio_output_flags_t)(AUDIO_OUTPUT_FLAG_NONE);
        }
    }

    bool forced_deep = false;
    // only allow deep buffering for music stream type
    if (stream != AUDIO_STREAM_MUSIC) {
        flags = (audio_output_flags_t)(flags &~AUDIO_OUTPUT_FLAG_DEEP_BUFFER);
    } else if (/* stream == AUDIO_STREAM_MUSIC && */
            (flags == AUDIO_OUTPUT_FLAG_NONE || flags == AUDIO_OUTPUT_FLAG_DIRECT) &&
            property_get_bool("audio.deep_buffer.media", false /* default_value */)) {
            forced_deep = true;
    }
    if (stream == AUDIO_STREAM_TTS) {
        flags = AUDIO_OUTPUT_FLAG_TTS;
    }

    sp<IOProfile> profile;

    // skip direct output selection if the request can obviously be attached to a mixed output
    // and not explicitly requested
    if (((flags & AUDIO_OUTPUT_FLAG_DIRECT) == 0) &&
            audio_is_linear_pcm(format) && samplingRate <= SAMPLE_RATE_HZ_MAX &&
            audio_channel_count_from_out_mask(channelMask) <= 2) {
        goto non_direct_output;
    }

    // Do not allow offloading if one non offloadable effect is enabled or MasterMono is enabled.
    //This prevents from creating an offloaded track and tearing it down immediately after start
    // when audioflinger detects there is an active non offloadable effect.
    // FIXME: We should check the audio session here but we do not have it in this context.
    // This may prevent offloading in rare situations where effects are left active by apps
    // in the background.

    if ((flags & (AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_DIRECT)) ||
            !(mEffects.isNonOffloadableEffectEnabled() || mMasterMono)) {
        profile = getProfileForDirectOutput(device,
                                           samplingRate,
                                           format,
                                           channelMask,
                                           (audio_output_flags_t)flags);
    }

    if (profile != 0) {

        if (!(flags & AUDIO_OUTPUT_FLAG_DIRECT) &&
             (profile->getFlags() & AUDIO_OUTPUT_FLAG_DIRECT)) {
            ALOGI("got Direct without requesting ... reject ");
            profile = NULL;
            goto non_direct_output;
        }

        sp<SwAudioOutputDescriptor> outputDesc = NULL;

        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<SwAudioOutputDescriptor> desc = mOutputs.valueAt(i);
            if (!desc->isDuplicated() && (profile == desc->mProfile)) {
                outputDesc = desc;
                // reuse direct output if currently open by the same client
                // and configured with same parameters
                if ((samplingRate == outputDesc->mSamplingRate) &&
                        audio_formats_match(format, outputDesc->mFormat) &&
                        (channelMask == outputDesc->mChannelMask)) {
                    if (session == outputDesc->mDirectClientSession) {
                        outputDesc->mDirectOpenCount++;
                        ALOGV("getOutput() reusing direct output %d for session ",
                               mOutputs.keyAt(i), session);
                        return mOutputs.keyAt(i);
                    } else {
                        ALOGV("getOutput() do not reuse direct output because current client (%d) "
                              "is not the same as requesting client (%d)",
                              outputDesc->mDirectClientSession, session);
                        goto non_direct_output;
                    }
                }
            }
        }
        if (flags == AUDIO_OUTPUT_FLAG_DIRECT &&
            direct_pcm_already_in_use == true &&
            session != outputDesc->mDirectClientSession) {
            ALOGV("getOutput() do not reuse direct pcm output because current client (%d) "
                  "is not the same as requesting client (%d) for different output conf",
                  outputDesc->mDirectClientSession, session);
            goto non_direct_output;
        }
        // close direct output if currently open and configured with different parameters
        if (outputDesc != NULL) {
            closeOutput(outputDesc->mIoHandle);
        }

        // if the selected profile is offloaded and no offload info was specified,
        // create a default one
        audio_offload_info_t defaultOffloadInfo = AUDIO_INFO_INITIALIZER;
        if ((profile->getFlags() & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) && !offloadInfo) {
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
        DeviceVector outputDevices = mAvailableOutputDevices.getDevicesFromType(device);
        String8 address = outputDevices.size() > 0 ? outputDevices.itemAt(0)->mAddress
                : String8("");
        status = mpClientInterface->openOutput(profile->getModuleHandle(),
                                               &output,
                                               &config,
                                               &outputDesc->mDevice,
                                               address,
                                               &outputDesc->mLatency,
                                               outputDesc->mFlags);

        // only accept an output with the requested parameters
        if (status != NO_ERROR ||
            (samplingRate != 0 && samplingRate != config.sample_rate) ||
            (format != AUDIO_FORMAT_DEFAULT && !audio_formats_match(format, config.format)) ||
            (channelMask != 0 && channelMask != config.channel_mask)) {
            ALOGV("getOutput() failed opening direct output: output %d samplingRate %d %d,"
                    "format %d %d, channelMask %04x %04x", output, samplingRate,
                    outputDesc->mSamplingRate, format, outputDesc->mFormat, channelMask,
                    outputDesc->mChannelMask);
            if (output != AUDIO_IO_HANDLE_NONE) {
                mpClientInterface->closeOutput(output);
            }
            // fall back to mixer output if possible when the direct output could not be open
            if (audio_is_linear_pcm(format) && samplingRate <= SAMPLE_RATE_HZ_MAX) {
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
        outputDesc->mDirectClientSession = session;

        addOutput(output, outputDesc);
        mPreviousOutputs = mOutputs;
        ALOGV("getOutput() returns new direct output %d", output);
        mpClientInterface->onAudioPortListUpdate();
        return output;
    }

non_direct_output:

    // A request for HW A/V sync cannot fallback to a mixed output because time
    // stamps are embedded in audio data
    if ((flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) != 0) {
        return AUDIO_IO_HANDLE_NONE;
    }

    // ignoring channel mask due to downmix capability in mixer

    // open a non direct output

    // for non direct outputs, only PCM is supported
    if (audio_is_linear_pcm(format)) {
        // get which output is suitable for the specified stream. The actual
        // routing change will happen when startOutput() will be called
        SortedVector<audio_io_handle_t> outputs = getOutputsForDevice(device, mOutputs);

        // at this stage we should ignore the DIRECT flag as no direct output could be found earlier
        flags = (audio_output_flags_t)(flags & ~AUDIO_OUTPUT_FLAG_DIRECT);

        if (forced_deep) {
            flags = (audio_output_flags_t)(AUDIO_OUTPUT_FLAG_DEEP_BUFFER);
            ALOGI("setting force DEEP buffer now ");
        } else if(flags == AUDIO_OUTPUT_FLAG_NONE) {
            // no deep buffer playback is requested hence fallback to primary
            flags = (audio_output_flags_t)(AUDIO_OUTPUT_FLAG_PRIMARY);
            ALOGI("FLAG None hence request for a primary output");
        }

        output = selectOutput(outputs, flags, format);
    }
    ALOGW_IF((output == 0), "getOutput() could not find output for stream %d, samplingRate %d,"
            "format %d, channels %x, flags %x", stream, samplingRate, format, channelMask, flags);

    ALOGV("getOutputForDevice() returns output %d", output);

    return output;
}

status_t AudioPolicyManagerCustom::getInputForAttr(const audio_attributes_t *attr,
                                             audio_io_handle_t *input,
                                             audio_session_t session,
                                             uid_t uid,
                                             const audio_config_base_t *config,
                                             audio_input_flags_t flags,
                                             audio_port_handle_t *selectedDeviceId,
                                             input_type_t *inputType,
                                             audio_port_handle_t *portId)
{
    audio_source_t inputSource;
    inputSource = attr->source;
#ifdef VOICE_CONCURRENCY

    char propValue[PROPERTY_VALUE_MAX];
    bool prop_rec_enabled=false, prop_voip_enabled = false;

    if(property_get("vendor.voice.record.conc.disabled", propValue, NULL)) {
        prop_rec_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if(property_get("vendor.voice.voip.conc.disabled", propValue, NULL)) {
        prop_voip_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
     }

    if (prop_rec_enabled && mvoice_call_state) {
         //check if voice call is active  / running in background
         //some of VoIP apps(like SIP2SIP call) supports resume of VoIP call when call in progress
         //Need to block input request
        if((AUDIO_MODE_IN_CALL == mEngine->getPhoneState()) ||
           ((AUDIO_MODE_IN_CALL == mPrevPhoneState) &&
             (AUDIO_MODE_IN_COMMUNICATION == mEngine->getPhoneState())))
        {
            switch(inputSource) {
                case AUDIO_SOURCE_VOICE_UPLINK:
                case AUDIO_SOURCE_VOICE_DOWNLINK:
                case AUDIO_SOURCE_VOICE_CALL:
                    ALOGD("voice_conc:Creating input during incall mode for inputSource: %d",
                        inputSource);
                break;

                case AUDIO_SOURCE_VOICE_COMMUNICATION:
                    if(prop_voip_enabled) {
                       ALOGD("voice_conc:BLOCK VoIP requst incall mode for inputSource: %d",
                        inputSource);
                       return NO_INIT;
                    }
                break;
                default:
                    ALOGD("voice_conc:BLOCK VoIP requst incall mode for inputSource: %d",
                        inputSource);
                return NO_INIT;
            }
        }
    }//check for VoIP flag
    else if(prop_voip_enabled && mvoice_call_state) {
         //check if voice call is active  / running in background
         //some of VoIP apps(like SIP2SIP call) supports resume of VoIP call when call in progress
         //Need to block input request
        if((AUDIO_MODE_IN_CALL == mEngine->getPhoneState()) ||
           ((AUDIO_MODE_IN_CALL == mPrevPhoneState) &&
             (AUDIO_MODE_IN_COMMUNICATION == mEngine->getPhoneState())))
        {
            if(inputSource == AUDIO_SOURCE_VOICE_COMMUNICATION) {
                ALOGD("BLOCKING VoIP request during incall mode for inputSource: %d ",inputSource);
                return NO_INIT;
            }
        }
    }

#endif

    return AudioPolicyManager::getInputForAttr(attr,
                                               input,
                                               session,
                                               uid,
                                               config,
                                               flags,
                                               selectedDeviceId,
                                               inputType,
                                               portId);
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

    sp<AudioSession> audioSession = inputDesc->getAudioSession(session);
    if (audioSession == 0) {
        ALOGW("startInput() unknown session %d on input %d", session, input);
        return BAD_VALUE;
    }

// FIXME: disable concurrent capture until UI is ready
#if 0
    if (!isConcurentCaptureAllowed(inputDesc, audioSession)) {
        ALOGW("startInput(%d) failed: other input already started", input);
        return INVALID_OPERATION;
    }
    if (isInCall()) {
        *concurrency |= API_INPUT_CONCURRENCY_CALL;
    }
    if (mInputs.activeInputsCountOnDevices() != 0) {
        *concurrency |= API_INPUT_CONCURRENCY_CAPTURE;
    }
#else
    if (!is_virtual_input_device(inputDesc->mDevice)) {
        if (mCallTxPatch != 0 &&
            inputDesc->getModuleHandle() == mCallTxPatch->mPatch.sources[0].ext.device.hw_module) {
            ALOGW("startInput(%d) failed: call in progress", input);
            return INVALID_OPERATION;
        }

        Vector< sp<AudioInputDescriptor> > activeInputs = mInputs.getActiveInputs();
        for (size_t i = 0; i < activeInputs.size(); i++) {
            sp<AudioInputDescriptor> activeDesc = activeInputs[i];

            if (is_virtual_input_device(activeDesc->mDevice)) {
                continue;
            }

            audio_source_t activeSource = activeDesc->inputSource(true);
            if (audioSession->inputSource() == AUDIO_SOURCE_HOTWORD) {
                if (activeSource == AUDIO_SOURCE_HOTWORD) {
                    if (activeDesc->hasPreemptedSession(session)) {
                        ALOGW("startInput(%d) failed for HOTWORD: "
                                "other input %d already started for HOTWORD",
                              input, activeDesc->mIoHandle);
                        return INVALID_OPERATION;
                    }
                } else {
                    ALOGV("startInput(%d) failed for HOTWORD: other input %d already started",
                          input, activeDesc->mIoHandle);
                    return INVALID_OPERATION;
                }
            } else {
                if (activeSource != AUDIO_SOURCE_HOTWORD) {
                    ALOGW("startInput(%d) failed: other input %d already started",
                          input, activeDesc->mIoHandle);
                    return INVALID_OPERATION;
                }
            }
        }

        // if capture is allowed, preempt currently active HOTWORD captures
        for (size_t i = 0; i < activeInputs.size(); i++) {
            sp<AudioInputDescriptor> activeDesc = activeInputs[i];

            if (is_virtual_input_device(activeDesc->mDevice)) {
                continue;
            }

            audio_source_t activeSource = activeDesc->inputSource(true);
            if (activeSource == AUDIO_SOURCE_HOTWORD) {
                AudioSessionCollection activeSessions =
                        activeDesc->getAudioSessions(true /*activeOnly*/);
                audio_session_t activeSession = activeSessions.keyAt(0);
                audio_io_handle_t activeHandle = activeDesc->mIoHandle;
                SortedVector<audio_session_t> sessions = activeDesc->getPreemptedSessions();
                sessions.add(activeSession);
                inputDesc->setPreemptedSessions(sessions);
                stopInput(activeHandle, activeSession);
                releaseInput(activeHandle, activeSession);
                ALOGV("startInput(%d) for HOTWORD preempting HOTWORD input %d",
                      input, activeDesc->mIoHandle);
            }
        }
    }
#endif

#ifdef RECORD_PLAY_CONCURRENCY
    mIsInputRequestOnProgress = true;

    char getPropValue[PROPERTY_VALUE_MAX];
    bool prop_rec_play_enabled = false;

    if (property_get("vendor.audio.rec.playback.conc.disabled", getPropValue, NULL)) {
        prop_rec_play_enabled = atoi(getPropValue) || !strncmp("true", getPropValue, 4);
    }

    if ((prop_rec_play_enabled) && (mInputs.activeInputsCountOnDevices() == 0)){
        // send update to HAL on record playback concurrency
        AudioParameter param = AudioParameter();
        param.add(String8("rec_play_conc_on"), String8("true"));
        ALOGD("startInput() setParameters rec_play_conc is setting to ON ");
        mpClientInterface->setParameters(0, param.toString());

        // Call invalidate to reset all opened non ULL audio tracks
        // Move tracks associated to this strategy from previous output to new output
        for (int i = AUDIO_STREAM_SYSTEM; i < AUDIO_STREAM_FOR_POLICY_CNT; i++) {
            // Do not call invalidate for ENFORCED_AUDIBLE (otherwise pops are seen for camcorder)
            if (i != AUDIO_STREAM_ENFORCED_AUDIBLE) {
               ALOGD("Invalidate on releaseInput for stream :: %d ", i);
               //FIXME see fixme on name change
               mpClientInterface->invalidateStream((audio_stream_type_t)i);
            }
        }
        // close compress tracks
        for (size_t i = 0; i < mOutputs.size(); i++) {
            sp<SwAudioOutputDescriptor> outputDesc = mOutputs.valueAt(i);
            if ((outputDesc == NULL) || (outputDesc->mProfile == NULL)) {
               ALOGD("ouput desc / profile is NULL");
               continue;
            }
            if (outputDesc->mProfile->getFlags()
                            & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
                // close compress  sessions
                ALOGD("calling closeOutput on record conc for COMPRESS output");
                closeOutput(mOutputs.keyAt(i));
            }
        }
    }
#endif

    if (!inputDesc->isActive() || mInputRoutes.hasRouteChanged(session)) {
        sp<AudioPolicyMix> policyMix = inputDesc->mPolicyMix.promote();
        // if input maps to a dynamic policy with an activity listener, notify of state change
        if ((policyMix != NULL)
                && ((policyMix->mCbFlags & AudioMix::kCbFlagNotifyActivity) != 0)) {
            mpClientInterface->onDynamicPolicyMixStateUpdate(policyMix->mDeviceAddress,
                    MIX_STATE_MIXING);
        }

        // indicate active capture to sound trigger service if starting capture from a mic on
        // primary HW module
        audio_devices_t device = getNewInputDevice(inputDesc);
        audio_devices_t primaryInputDevices = availablePrimaryInputDevices();
        if (((device & primaryInputDevices & ~AUDIO_DEVICE_BIT_IN) != 0) &&
                mInputs.activeInputsCountOnDevices(primaryInputDevices) == 0) {
            SoundTrigger::setCaptureState(true);
        }
        setInputDevice(input, device, true /* force */);

        // automatically enable the remote submix output when input is started if not
        // used by a policy mix of type MIX_TYPE_RECORDERS
        // For remote submix (a virtual device), we open only one input per capture request.
        if (audio_is_remote_submix_device(inputDesc->mDevice)) {
            String8 address = String8("");
            if (policyMix == NULL) {
                address = String8("0");
            } else if (policyMix->mMixType == MIX_TYPE_PLAYERS) {
                address = policyMix->mDeviceAddress;
            }
            if (address != "") {
                setDeviceConnectionStateInt(AUDIO_DEVICE_OUT_REMOTE_SUBMIX,
                        AUDIO_POLICY_DEVICE_STATE_AVAILABLE,
                        address, "remote-submix");
            }
        }
    }

    ALOGV("AudioPolicyManager::startInput() input source = %d", audioSession->inputSource());

    audioSession->changeActiveCount(1);
#ifdef RECORD_PLAY_CONCURRENCY
    mIsInputRequestOnProgress = false;
#endif
    return NO_ERROR;
}

status_t AudioPolicyManagerCustom::stopInput(audio_io_handle_t input,
                                       audio_session_t session)
{
    status_t status;
    status = AudioPolicyManager::stopInput(input, session);
#ifdef RECORD_PLAY_CONCURRENCY
    char propValue[PROPERTY_VALUE_MAX];
    bool prop_rec_play_enabled = false;

    if (property_get("vendor.audio.rec.playback.conc.disabled", propValue, NULL)) {
        prop_rec_play_enabled = atoi(propValue) || !strncmp("true", propValue, 4);
    }

    if ((prop_rec_play_enabled) && (mInputs.activeInputsCountOnDevices() == 0)) {

        //send update to HAL on record playback concurrency
        AudioParameter param = AudioParameter();
        param.add(String8("rec_play_conc_on"), String8("false"));
        ALOGD("stopInput() setParameters rec_play_conc is setting to OFF ");
        mpClientInterface->setParameters(0, param.toString());

        //call invalidate tracks so that any open streams can fall back to deep buffer/compress path from ULL
        for (int i = AUDIO_STREAM_SYSTEM; i < (int)AUDIO_STREAM_CNT; i++) {
            //Do not call invalidate for ENFORCED_AUDIBLE (otherwise pops are seen for camcorder stop tone)
            if ((i != AUDIO_STREAM_ENFORCED_AUDIBLE) && (i != AUDIO_STREAM_PATCH)) {
               ALOGD(" Invalidate on stopInput for stream :: %d ", i);
               //FIXME see fixme on name change
               mpClientInterface->invalidateStream((audio_stream_type_t)i);
            }
        }
    }
#endif
    return status;
}

void AudioPolicyManagerCustom::closeAllInputs() {
    bool patchRemoved = false;

    for(size_t input_index = mInputs.size(); input_index > 0; input_index--) {
        sp<AudioInputDescriptor> inputDesc = mInputs.valueAt(input_index-1);
        ssize_t patch_index = mAudioPatches.indexOfKey(inputDesc->getPatchHandle());
        if (patch_index >= 0) {
            sp<AudioPatch> patchDesc = mAudioPatches.valueAt(patch_index);
            (void) mpClientInterface->releaseAudioPatch(patchDesc->mAfPatchHandle, 0);
            mAudioPatches.removeItemsAt(patch_index);
            patchRemoved = true;
        }
        if ((inputDesc->getOpenRefCount() > 0) && inputDesc->isSoundTrigger()
            && (mInputs.size() == 1)) {
            ALOGD("Do not close sound trigger input handle");
        } else {
            mpClientInterface->closeInput(mInputs.keyAt(input_index-1));
            mInputs.removeItem(mInputs.keyAt(input_index-1));
        }
    }
    mInputs.clear();
    SoundTrigger::setCaptureState(false);
    nextAudioPortGeneration();

    if (patchRemoved) {
        mpClientInterface->onAudioPatchListUpdate();
    }
}

AudioPolicyManagerCustom::AudioPolicyManagerCustom(AudioPolicyClientInterface *clientInterface)
    : AudioPolicyManager(clientInterface),
      mHdmiAudioDisabled(false),
      mHdmiAudioEvent(false),
#ifndef FM_POWER_OPT
      mPrevPhoneState(0)
#else
      mPrevPhoneState(0),
      mPrevFMVolumeDb(0.0f),
      mFMIsActive(false)
#endif
{

#ifdef USE_XML_AUDIO_POLICY_CONF
    ALOGD("USE_XML_AUDIO_POLICY_CONF is TRUE");
#else
    ALOGD("USE_XML_AUDIO_POLICY_CONF is FALSE");
#endif

#ifdef RECORD_PLAY_CONCURRENCY
    mIsInputRequestOnProgress = false;
#endif

#ifdef VOICE_CONCURRENCY
    mFallBackflag = getFallBackPath();
#endif
}
}
