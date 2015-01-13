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


#include <audiopolicy/AudioPolicyManager.h>
#include <audiopolicy/audio_policy_conf.h>



namespace android {

// ----------------------------------------------------------------------------

class AudioPolicyManagerCustom: public AudioPolicyManager
{

public:
                AudioPolicyManagerCustom(AudioPolicyClientInterface *clientInterface)
                : AudioPolicyManager(clientInterface) {
                    mHdmiAudioDisabled = false;
                    mHdmiAudioEvent = false; }

        virtual ~AudioPolicyManagerCustom() {}

        virtual status_t setDeviceConnectionState(audio_devices_t device,
                                                          audio_policy_dev_state_t state,
                                                          const char *device_address);
        virtual audio_policy_dev_state_t getDeviceConnectionState(audio_devices_t device,
                                                                              const char *device_address);
        virtual void setPhoneState(audio_mode_t state);
        virtual void setForceUse(audio_policy_force_use_t usage,
                                 audio_policy_forced_cfg_t config);
        virtual status_t stopOutput(audio_io_handle_t output,
                                    audio_stream_type_t stream,
                                    int session = 0);
        virtual audio_io_handle_t getInput(audio_source_t inputSource,
                                            uint32_t samplingRate,
                                            audio_format_t format,
                                            audio_channel_mask_t channelMask,
                                            audio_session_t session,
                                            audio_input_flags_t flags);

        // indicates to the audio policy manager that the input starts being used.
        virtual status_t startInput(audio_io_handle_t input,
                                    audio_session_t session);

        // indicates to the audio policy manager that the input stops being used.
        virtual status_t stopInput(audio_io_handle_t input,
                                   audio_session_t session);
        virtual status_t setStreamVolumeIndex(audio_stream_type_t stream,
                                              int index,
                                              audio_devices_t device);
        virtual bool isOffloadSupported(const audio_offload_info_t& offloadInfo);

        // true if given state represents a device in a telephony or VoIP call
        virtual bool isStateInCall(int state);
protected:
        // return the strategy corresponding to a given stream type
        static routing_strategy getStrategy(audio_stream_type_t stream);

        // return appropriate device for streams handled by the specified strategy according to current
        // phone state, connected devices...
        // if fromCache is true, the device is returned from mDeviceForStrategy[],
        // otherwise it is determine by current state
        // (device connected,phone state, force use, a2dp output...)
        // This allows to:
        //  1 speed up process when the state is stable (when starting or stopping an output)
        //  2 access to either current device selection (fromCache == true) or
        // "future" device selection (fromCache == false) when called from a context
        //  where conditions are changing (setDeviceConnectionState(), setPhoneState()...) AND
        //  before updateDevicesAndOutputs() is called.
        virtual audio_devices_t getDeviceForStrategy(routing_strategy strategy,
                                                     bool fromCache);
        // select input device corresponding to requested audio source
        virtual audio_devices_t getDeviceForInputSource(audio_source_t inputSource);

        // compute the actual volume for a given stream according to the requested index and a particular
        // device
        virtual float computeVolume(audio_stream_type_t stream, int index,
                                    audio_io_handle_t output, audio_devices_t device);

        // check that volume change is permitted, compute and send new volume to audio hardware
        status_t checkAndSetVolume(audio_stream_type_t stream, int index, audio_io_handle_t output,
                                   audio_devices_t device, int delayMs = 0, bool force = false);

        // returns the category the device belongs to with regard to volume curve management
        static device_category getDeviceCategory(audio_devices_t device);


        //parameter indicates of HDMI speakers disabled
        bool mHdmiAudioDisabled;
        //parameter indicates if HDMI plug in/out detected
        bool mHdmiAudioEvent;
private:
        static float volIndexToAmpl(audio_devices_t device, const StreamDescriptor& streamDesc,
                int indexInUi);
        // updates device caching and output for streams that can influence the
        //    routing of notifications
        void handleNotificationRoutingForStream(audio_stream_type_t stream);
        static bool isVirtualInputDevice(audio_devices_t device);
        static bool deviceDistinguishesOnAddress(audio_devices_t device);
        uint32_t nextUniqueId();
        // internal method to return the output handle for the given device and format
        audio_io_handle_t getOutputForDevice(
                audio_devices_t device,
                audio_stream_type_t stream,
                uint32_t samplingRate,
                audio_format_t format,
                audio_channel_mask_t channelMask,
                audio_output_flags_t flags,
                const audio_offload_info_t *offloadInfo);

        // Used for voip + voice concurrency usecase
        int mPrevPhoneState;
        int mvoice_call_state;
#ifdef RECORD_PLAY_CONCURRENCY
        // Used for record + playback concurrency
        bool mIsInputRequestOnProgress;
#endif

};

};
