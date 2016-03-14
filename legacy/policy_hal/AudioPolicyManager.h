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


#include <audiopolicy/managerdefault/AudioPolicyManager.h>
#include <audio_policy_conf.h>
#include <Volume.h>


namespace android {

// ----------------------------------------------------------------------------

class AudioPolicyManagerCustom: public AudioPolicyManager
{

public:
        AudioPolicyManagerCustom(AudioPolicyClientInterface *clientInterface);

        virtual ~AudioPolicyManagerCustom() {}

        status_t setDeviceConnectionStateInt(audio_devices_t device,
                                          audio_policy_dev_state_t state,
                                          const char *device_address,
                                          const char *device_name);
        virtual status_t getInputForAttr(const audio_attributes_t *attr,
                                         audio_io_handle_t *input,
                                         audio_session_t session,
                                         uid_t uid,
                                         uint32_t samplingRate,
                                         audio_format_t format,
                                         audio_channel_mask_t channelMask,
                                         audio_input_flags_t flags,
                                         audio_port_handle_t selectedDeviceId,
                                         input_type_t *inputType);

protected:
        // manages A2DP output suspend/restore according to phone state and BT SCO usage
        void checkA2dpSuspend();

        // check that volume change is permitted, compute and send new volume to audio hardware
        virtual status_t checkAndSetVolume(audio_stream_type_t stream, int index,
                                           const sp<AudioOutputDescriptor>& outputDesc,
                                           audio_devices_t device,
                                           int delayMs = 0, bool force = false);

        void updateCallRouting(audio_devices_t rxDevice, int delayMs = 0);

private:
        float mPrevFMVolumeDb;
        bool mFMIsActive;
};

};
