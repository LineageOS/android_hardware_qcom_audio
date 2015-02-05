/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
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


#include <stdint.h>
#include <sys/types.h>
#include <utils/Timers.h>
#include <utils/Errors.h>
#include <utils/KeyedVector.h>
#include <hardware_legacy/AudioPolicyManagerBase.h>


namespace android_audio_legacy {

// ----------------------------------------------------------------------------

class AudioPolicyManager: public AudioPolicyManagerBase
{

public:
                AudioPolicyManager(AudioPolicyClientInterface *clientInterface)
                : AudioPolicyManagerBase(clientInterface) {}

        virtual ~AudioPolicyManager() {}

        // AudioPolicyInterface
        virtual status_t setDeviceConnectionState(audio_devices_t device,
                                                          AudioSystem::device_connection_state state,
                                                          const char *device_address);
        uint32_t checkDeviceMuteStrategies(AudioOutputDescriptor *outputDesc,
                                           audio_devices_t prevDevice,
                                           uint32_t delayMs);

	void setStrategyMute(routing_strategy strategy,
						 bool on,
						 audio_io_handle_t output,
						 int delayMs = 0,
						 audio_devices_t device = (audio_devices_t)0);
	void setStreamMute(int stream, bool on, audio_io_handle_t output,
						int delayMs = 0,
						audio_devices_t device = (audio_devices_t)0);

        virtual AudioSystem::device_connection_state getDeviceConnectionState(audio_devices_t device,
                                                                              const char *device_address);
        virtual void setPhoneState(int state);
        virtual void setForceUse(AudioSystem::force_use usage, AudioSystem::forced_config config);
        virtual audio_io_handle_t getOutput(AudioSystem::stream_type stream,
                                            uint32_t samplingRate = 0,
                                            uint32_t format = AudioSystem::FORMAT_DEFAULT,
                                            uint32_t channels = 0,
                                            AudioSystem::output_flags flags =
                                                    AudioSystem::OUTPUT_FLAG_INDIRECT, const audio_offload_info_t *offloadInfo = NULL);
        virtual status_t startOutput(audio_io_handle_t output,
                                     AudioSystem::stream_type stream,
                                     int session = 0);
        virtual status_t stopOutput(audio_io_handle_t output,
                                    AudioSystem::stream_type stream,
                                    int session = 0);
        virtual void releaseOutput(audio_io_handle_t output);
        virtual audio_io_handle_t getInput(int inputSource,
                                            uint32_t samplingRate,
                                            uint32_t format,
                                            uint32_t channels,
                                            AudioSystem::audio_in_acoustics acoustics);

        // indicates to the audio policy manager that the input starts being used.
        virtual status_t startInput(audio_io_handle_t input);

        // indicates to the audio policy manager that the input stops being used.
        virtual status_t stopInput(audio_io_handle_t input);
        virtual status_t setStreamVolumeIndex(AudioSystem::stream_type stream,
                                              int index,
                                              audio_devices_t device);
protected:
        // return the strategy corresponding to a given stream type
        static routing_strategy getStrategy(AudioSystem::stream_type stream);

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
                                                     bool fromCache = true);

        // change the route of the specified output. Returns the number of ms we have slept to
        // allow new routing to take effect in certain cases.
        uint32_t setOutputDevice(audio_io_handle_t output,
                             audio_devices_t device,
                             bool force = false,
                             int delayMs = 0);

        // select input device corresponding to requested audio source
        virtual audio_devices_t getDeviceForInputSource(int inputSource);

        // check that volume change is permitted, compute and send new volume to audio hardware
        status_t checkAndSetVolume(int stream, int index, audio_io_handle_t output, audio_devices_t device, int delayMs = 0, bool force = false);


        // when a device is connected, checks if an open output can be routed
        // to this device. If none is open, tries to open one of the available outputs.
        // Returns an output suitable to this device or 0.
        // when a device is disconnected, checks if an output is not used any more and
        // returns its handle if any.
        // transfers the audio tracks and effects from one output thread to another accordingly.
        status_t checkOutputsForDevice(audio_devices_t device,
                                       AudioSystem::device_connection_state state,
                                       SortedVector<audio_io_handle_t>& outputs);
        // manages A2DP output suspend/restore according to phone state and BT SCO usage
        void checkA2dpSuspend();

        // returns the A2DP output handle if it is open or 0 otherwise
        audio_io_handle_t getA2dpOutput();

        // returns true if give output is direct output
        bool isDirectOutput(audio_io_handle_t output);

        virtual AudioPolicyManagerBase::IOProfile* getProfileForDirectOutput(
                                                     audio_devices_t device,
                                                     uint32_t samplingRate,
                                                     uint32_t format,
                                                     uint32_t channelMask,
                                                     audio_output_flags_t flags);
        bool    isCompatibleProfile(AudioPolicyManagerBase::IOProfile *profile,
                                    audio_devices_t device,
                                    uint32_t samplingRate,
                                    uint32_t format,
                                    uint32_t channelMask,
                                    audio_output_flags_t flags);
        // selects the most appropriate device on output for current state
        // must be called every time a condition that affects the device choice for a given output is
        // changed: connected device, phone state, force use, output start, output stop..
        // see getDeviceForStrategy() for the use of fromCache parameter

        audio_devices_t getNewDevice(audio_io_handle_t output, bool fromCache);


        // returns the category the device belongs to with regard to volume curve management
        static device_category getDeviceCategory(audio_devices_t device);

        // extract one device relevant for volume control from multiple device selection
        static audio_devices_t getDeviceForVolume(audio_devices_t device);
        // true is current platform implements a back microphone
        virtual bool hasBackMicrophone() const { return false; }
        // true is current platform supports suplication of notifications and ringtones over A2DP output
        virtual bool a2dpUsedForSonification() const { return true; }

private:

        void handleNotificationRoutingForStream(AudioSystem::stream_type stream);
        bool platform_is_Fusion3();
};
};
