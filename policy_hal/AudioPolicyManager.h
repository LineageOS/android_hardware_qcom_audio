/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
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

        virtual status_t setDeviceConnectionState(audio_devices_t device,
                                                          AudioSystem::device_connection_state state,
                                                          const char *device_address);
        virtual void setForceUse(AudioSystem::force_use usage, AudioSystem::forced_config config);
        virtual audio_io_handle_t getInput(int inputSource,
                                            uint32_t samplingRate,
                                            uint32_t format,
                                            uint32_t channels,
                                            AudioSystem::audio_in_acoustics acoustics);
        virtual bool isOffloadSupported(const audio_offload_info_t& offloadInfo);
        virtual void setPhoneState(int state);
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
        // select input device corresponding to requested audio source
        virtual audio_devices_t getDeviceForInputSource(int inputSource);

        // compute the actual volume for a given stream according to the requested index and a particular
        // device
        virtual float computeVolume(int stream, int index, audio_io_handle_t output, audio_devices_t device);

        // check that volume change is permitted, compute and send new volume to audio hardware
        status_t checkAndSetVolume(int stream, int index, audio_io_handle_t output, audio_devices_t device, int delayMs = 0, bool force = false);

        // returns the category the device belongs to with regard to volume curve management
        static device_category getDeviceCategory(audio_devices_t device);

        // returns true if give output is direct output
        bool isDirectOutput(audio_io_handle_t output);

        static const char* HDMI_SPKR_STR;

        //parameter indicates of HDMI speakers disabled from the Qualcomm settings
        bool mHdmiAudioDisabled;

        //parameter indicates if HDMI plug in/out detected
        bool mHdmiAudioEvent;

private:
        // Used for voip + voice concurrency usecase
        int mPrevPhoneState;

};
};
