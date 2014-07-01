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
                : AudioPolicyManagerBase(clientInterface) {
                            mPrimarySuspended = 0; mFastSuspended = 0;}

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
        virtual audio_io_handle_t getOutput(AudioSystem::stream_type stream,
                                            uint32_t samplingRate = 0,
                                            uint32_t format = AudioSystem::FORMAT_DEFAULT,
                                            uint32_t channels = 0,
                                            AudioSystem::output_flags flags =
                                                    AudioSystem::OUTPUT_FLAG_INDIRECT,
                                            const audio_offload_info_t *offloadInfo = NULL);
        virtual status_t startOutput(audio_io_handle_t output,
                                             AudioSystem::stream_type stream,
                                             int session = 0);
        virtual status_t stopOutput(audio_io_handle_t output,
                                            AudioSystem::stream_type stream,
                                            int session = 0 );
        virtual void releaseOutput(audio_io_handle_t output);
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

        // mute/unmute strategies using an incompatible device combination
        // if muting, wait for the audio in pcm buffer to be drained before proceeding
        // if unmuting, unmute only after the specified delay
        // Returns the number of ms waited
        uint32_t  checkDeviceMuteStrategies(AudioOutputDescriptor *outputDesc,
                                            audio_devices_t prevDevice,
                                            uint32_t delayMs);

        // change the route of the specified output. Returns the number of ms we have slept to
        // allow new routing to take effect in certain cases.
        uint32_t setOutputDevice(audio_io_handle_t output,
                                 audio_devices_t device,
                                 bool force = false,
                                 int delayMs = 0);

        // check that volume change is permitted, compute and send new volume to audio hardware
        status_t checkAndSetVolume(int stream, int index, audio_io_handle_t output, audio_devices_t device, int delayMs = 0, bool force = false);

        // close an output and its companion duplicating output.
        void closeOutput(audio_io_handle_t output);

        // returns the category the device belongs to with regard to volume curve management
        static device_category getDeviceCategory(audio_devices_t device);
    private :
        void handleNotificationRoutingForStream(AudioSystem::stream_type stream);
#ifdef HDMI_PASSTHROUGH_ENABLED
        void checkAndSuspendOutputs();
        void checkAndRestoreOutputs();
        audio_devices_t handleHDMIPassthrough(audio_devices_t device,
                         audio_io_handle_t output,
                         int stream = -1,
                         int strategy = -1);
        audio_io_handle_t getPassthroughOutput(
                                    AudioSystem::stream_type stream,
                                    uint32_t samplingRate,
                                    uint32_t format,
                                    uint32_t channelMask,
                                    AudioSystem::output_flags flags,
                                    const audio_offload_info_t *offloadInfo,
                                    audio_devices_t device);
        bool isEffectEnabled();
        void updateAndCloseOutputs();
#endif
        uint32_t mPrimarySuspended;
        uint32_t mFastSuspended;

        int mOldPhoneState;
        bool isExternalModem();

};
};
