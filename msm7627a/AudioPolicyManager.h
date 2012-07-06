/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
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

enum fm_modes{
   FM_DIGITAL=1,
   FM_ANALOG,
   FM_NONE
};

class AudioPolicyManager: public AudioPolicyManagerBase
{

public:
                AudioPolicyManager(AudioPolicyClientInterface *clientInterface)
                : AudioPolicyManagerBase(clientInterface),fmMode(FM_NONE){
                  mLPADecodeOutput = -1;
                  mLPAMuted = false;
                  mLPAStreamType = AudioSystem::DEFAULT;
                }

        virtual ~AudioPolicyManager() {}

        virtual status_t setDeviceConnectionState(AudioSystem::audio_devices device,
                                                          AudioSystem::device_connection_state state,
                                                          const char *device_address);

        virtual uint32_t getDeviceForStrategy(routing_strategy strategy, bool fromCache = true);
        virtual audio_io_handle_t getSession(AudioSystem::stream_type stream,
                                            uint32_t format,
                                            AudioSystem::output_flags flags,
                                            int32_t  sessionId, uint32_t samplingRate,
                                            uint32_t channels);
        virtual void pauseSession(audio_io_handle_t output, AudioSystem::stream_type stream);
        virtual void resumeSession(audio_io_handle_t output, AudioSystem::stream_type stream);
        virtual void releaseSession(audio_io_handle_t output);
        virtual void setForceUse(AudioSystem::force_use usage, AudioSystem::forced_config config);
protected:
        fm_modes fmMode;

        // true is current platform implements a back microphone
        virtual bool hasBackMicrophone() const { return false; }
#ifdef WITH_A2DP
        // true is current platform supports suplication of notifications and ringtones over A2DP output
        virtual bool a2dpUsedForSonification() const { return true; }
#endif
        // check that volume change is permitted, compute and send new volume to audio hardware
        status_t checkAndSetVolume(int stream, int index, audio_io_handle_t output, uint32_t device, int delayMs = 0, bool force = false);
        // select input device corresponding to requested audio source
        virtual uint32_t getDeviceForInputSource(int inputSource);
        status_t stopInput(audio_io_handle_t input);
        // change the route of the specified output
   virtual void setPhoneState(int state);
   virtual void setOutputDevice(audio_io_handle_t output,uint32_t device,bool force = false,int delayMs = 0);
   virtual status_t startOutput(audio_io_handle_t output,AudioSystem::stream_type stream,int session = 0);
   virtual status_t stopOutput(audio_io_handle_t output,AudioSystem::stream_type stream,int session = 0);
   void setStreamMute(int stream, bool on, audio_io_handle_t output, int delayMs = 0);
   virtual void setFmMode(fm_modes mode) {  fmMode = mode; }
   virtual fm_modes getFMMode() const {  return fmMode; }
   audio_io_handle_t mLPADecodeOutput;           // active output handler
   audio_io_handle_t mLPAActiveOuput;           // LPA Output Handler during inactive state
   bool    mLPAMuted;
   AudioSystem::stream_type  mLPAStreamType;
   AudioSystem::stream_type  mLPAActiveStreamType;


};
};
