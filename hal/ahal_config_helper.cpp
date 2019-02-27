/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "ahal_config_helper"

#include "ahal_config_helper.h"
#include <cutils/properties.h>
#include <log/log.h>

struct AHalConfigHelper {
    static AHalConfigHelper* mConfigHelper;

    AHalConfigHelper() : isRemote(false) { };
    static AHalConfigHelper* getAHalConfInstance() {
        if (!mConfigHelper)
            mConfigHelper = new AHalConfigHelper();
        return mConfigHelper;
    }
    void initDefaultConfig(bool isVendorEnhancedFwk);
    AHalValues* getAHalValues();
    inline void retrieveConfigs();

    AHalValues mConfigs;
    bool isRemote; // configs specified from remote
};

AHalConfigHelper* AHalConfigHelper::mConfigHelper;

void AHalConfigHelper::initDefaultConfig(bool isVendorEnhancedFwk)
{
    ALOGV("%s: enter", __FUNCTION__);
    if (isVendorEnhancedFwk) {
        mConfigs = {
            true,        /* SND_MONITOR */
            false,       /* COMPRESS_CAPTURE */
            true,        /* SOURCE_TRACK */
            true,        /* SSREC */
            true,        /* AUDIOSPHERE */
            true,        /* AFE_PROXY */
            false,       /* USE_DEEP_AS_PRIMARY_OUTPUT */
            true,        /* HDMI_EDID */
            true,        /* KEEP_ALIVE */
            false,       /* HIFI_AUDIO */
            true,        /* RECEIVER_AIDED_STEREO */
            true,        /* KPI_OPTIMIZE */
            true,        /* DISPLAY_PORT */
            true,        /* FLUENCE */
            true,        /* CUSTOM_STEREO */
            true,        /* ANC_HEADSET */
            true,        /* SPKR_PROT */
            true,        /* FM_POWER_OPT */
            false,       /* EXTERNAL_QDSP */
            false,       /* EXTERNAL_SPEAKER */
            false,       /* EXTERNAL_SPEAKER_TFA */
            false,       /* HWDEP_CAL */
            false,       /* DSM_FEEDBACK */
            true,        /* USB_OFFLOAD */
            false,       /* USB_OFFLOAD_BURST_MODE */
            false,       /* USB_OFFLOAD_SIDETONE_VOLM */
            true,        /* A2DP_OFFLOAD */
            true,        /* VBAT */
            true,        /* COMPRESS_METADATA_NEEDED */
            false,       /* COMPRESS_VOIP */
            false,       /* DYNAMIC_ECNS */
        };
    } else {
        mConfigs = {
            true,        /* SND_MONITOR */
            false,       /* COMPRESS_CAPTURE */
            false,       /* SOURCE_TRACK */
            false,       /* SSREC */
            false,       /* AUDIOSPHERE */
            false,       /* AFE_PROXY */
            false,       /* USE_DEEP_AS_PRIMARY_OUTPUT */
            false,       /* HDMI_EDID */
            false,       /* KEEP_ALIVE */
            false,       /* HIFI_AUDIO */
            false,       /* RECEIVER_AIDED_STEREO */
            false,       /* KPI_OPTIMIZE */
            false,       /* DISPLAY_PORT */
            false,       /* FLUENCE */
            false,       /* CUSTOM_STEREO */
            false,       /* ANC_HEADSET */
            true,        /* SPKR_PROT */
            false,       /* FM_POWER_OPT */
            true,        /* EXTERNAL_QDSP */
            true,        /* EXTERNAL_SPEAKER */
            false,       /* EXTERNAL_SPEAKER_TFA */
            true,        /* HWDEP_CAL */
            false,       /* DSM_FEEDBACK */
            true,        /* USB_OFFLOAD */
            false,       /* USB_OFFLOAD_BURST_MODE */
            false,       /* USB_OFFLOAD_SIDETONE_VOLM */
            true,        /* A2DP_OFFLOAD */
            false,       /* VBAT */
            false,       /* COMPRESS_METADATA_NEEDED */
            false,       /* COMPRESS_VOIP */
            false,       /* DYNAMIC_ECNS */
        };
    }
}

AHalValues* AHalConfigHelper::getAHalValues()
{
    ALOGV("%s: enter", __FUNCTION__);
    retrieveConfigs();
    return &mConfigs;
}

void AHalConfigHelper::retrieveConfigs()
{
    ALOGV("%s: enter", __FUNCTION__);
    // ToDo: Add logic to query AHalValues from config store
    // once support is added to it
    return;
}

extern "C" {

AHalValues* confValues = nullptr;

void audio_extn_ahal_config_helper_init(bool is_vendor_enhanced_fwk)
{
    AHalConfigHelper* confInstance = AHalConfigHelper::getAHalConfInstance();
    if (confInstance)
        confInstance->initDefaultConfig(is_vendor_enhanced_fwk);
}

AHalValues* audio_extn_get_feature_values()
{
    AHalConfigHelper* confInstance = AHalConfigHelper::getAHalConfInstance();
    if (confInstance)
        confValues = confInstance->getAHalValues();
    return confValues;
}

bool audio_extn_is_config_from_remote()
{
    AHalConfigHelper* confInstance = AHalConfigHelper::getAHalConfInstance();
    if (confInstance)
        return confInstance->isRemote;
    return false;
}

} // extern C
