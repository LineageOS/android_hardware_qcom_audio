/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (C) 2011-2012, Code Aurora Forum. All rights reserved.
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
        virtual status_t setDeviceConnectionState(AudioSystem::audio_devices device, AudioSystem::device_connection_state state, const char *device_address);
        virtual void setPhoneState(int state);
        virtual void setForceUse(AudioSystem::force_use usage, AudioSystem::forced_config config);
        virtual status_t startOutput(audio_io_handle_t output, AudioSystem::stream_type stream, int session = 0);
        virtual status_t stopOutput(audio_io_handle_t output, AudioSystem::stream_type stream, int session = 0);
        virtual status_t startInput(audio_io_handle_t input);

        // return appropriate device for streams handled by the specified strategy according to current
        // phone state, connected devices...
        // if fromCache is true, the device is returned from mDeviceForStrategy[], otherwise it is determined
        // by current state (device connected, phone state, force use, a2dp output...)
        // This allows to:
        //  1 speed up process when the state is stable (when starting or stopping an output)
        //  2 access to either current device selection (fromCache == true) or
        // "future" device selection (fromCache == false) when called from a context
        //  where conditions are changing (setDeviceConnectionState(), setPhoneState()...) AND
        //  before updateDeviceForStrategy() is called.
        virtual audio_devices_t getDeviceForStrategy(routing_strategy strategy, bool fromCache = true);

        virtual AudioSystem::device_connection_state getDeviceConnectionState(AudioSystem::audio_devices device,
                                                                              const char *device_address);

        void checkA2dpSuspend();

        // returns the A2DP output handle if it is open or 0 otherwise
        audio_io_handle_t getA2dpOutput();

protected:
        // true is current platform implements a back microphone
        virtual bool hasBackMicrophone() const { return false; }
        // true is current platform supports suplication of notifications and ringtones over A2DP output
        virtual bool a2dpUsedForSonification() const { return true; }
        // change the route of the specified output
        virtual uint32_t setOutputDevice(audio_io_handle_t output, audio_devices_t device, bool force = false, int delayMs = 0);
        // select input device corresponding to requested audio source
        virtual audio_devices_t getDeviceForInputSource(int inputSource);
        // check that volume change is permitted, compute and send new volume to audio hardware
        virtual status_t checkAndSetVolume(int stream, int index, audio_io_handle_t output, audio_devices_t device, int delayMs = 0, bool force = false);
        // Mute or unmute the stream on the specified output
        virtual void setStreamMute(int stream, bool on, audio_io_handle_t output, int delayMs = 0, audio_devices_t device = (audio_devices_t)0);

private:
        void handleNotificationRoutingForStream(AudioSystem::stream_type stream);
};
};
