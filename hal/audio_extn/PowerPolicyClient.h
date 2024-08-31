/* Copyright (c) 2021, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/
/*
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*
*    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef QTI_AUDIO_POWERPOLICYCLIENT_H_
#define QTI_AUDIO_POWERPOLICYCLIENT_H_

#include "PowerPolicyClientBase.h"
#include "audio_hal_plugin.h"

#define LPM_DEFAULT_STATE "\0"
#define HIBER_DEFAULT_STATE "\0"

typedef enum card_status_t {
    CARD_STATUS_OFFLINE,
    CARD_STATUS_ONLINE
} card_status_t;

typedef int32_t (*hal_plugin_send_msg_t) (audio_hal_plugin_msg_type_t, void*, uint32_t);
typedef void (*fp_in_set_power_policy_t) (uint8_t);
typedef void (*fp_out_set_power_policy_t) (uint8_t);
typedef card_status_t (*fp_snd_card_status_t) (void);

typedef struct power_policy_init_config {
    fp_in_set_power_policy_t                      fp_in_set_power_policy;
    fp_out_set_power_policy_t                     fp_out_set_power_policy;
    fp_snd_card_status_t                          fp_snd_card_status;
} power_policy_init_config_t;


class PowerPolicyClient
    : public ::android::frameworks::automotive::powerpolicy::PowerPolicyClientBase {
  public:
    explicit PowerPolicyClient(power_policy_init_config init_config);
    ~PowerPolicyClient();

    void onInitFailed();
    std::vector<::aidl::android::frameworks::automotive::powerpolicy::PowerComponent>
    getComponentsOfInterest() override;
    ::ndk::ScopedAStatus onPolicyChanged(
            const ::aidl::android::frameworks::automotive::powerpolicy::CarPowerPolicy&) override;

  private:
        void* plugin_handle;
        hal_plugin_send_msg_t hal_plugin_send_msg;
        fp_out_set_power_policy_t fp_out_set_power_policy;
        fp_in_set_power_policy_t fp_in_set_power_policy;
        fp_snd_card_status_t fp_snd_card_status;
};

#endif  // QTI_AUDIO_POWERPOLICYCLIENT_H_
