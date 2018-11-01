/*
 * Copyright (c) 2013-2018, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "hardware_info"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <stdlib.h>
#include <dlfcn.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_HW_INFO
#include <log_utils.h>
#endif

struct hardware_info {
    char name[HW_INFO_ARRAY_MAX_SIZE];
    char type[HW_INFO_ARRAY_MAX_SIZE];
    /* variables for handling target variants */
    uint32_t num_snd_devices;
    char dev_extn[HW_INFO_ARRAY_MAX_SIZE];
    snd_device_t  *snd_devices;
    bool is_stereo_spkr;
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const snd_device_t taiko_fluid_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
};

static const snd_device_t taiko_CDP_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
    SND_DEVICE_IN_QUAD_MIC,
};

static const snd_device_t taiko_apq8084_CDP_variant_devices[] = {
    SND_DEVICE_IN_HANDSET_MIC,
};

static const snd_device_t taiko_liquid_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
    SND_DEVICE_IN_SPEAKER_MIC,
    SND_DEVICE_IN_HEADSET_MIC,
    SND_DEVICE_IN_VOICE_DMIC,
    SND_DEVICE_IN_VOICE_SPEAKER_DMIC,
    SND_DEVICE_IN_VOICE_REC_DMIC_STEREO,
    SND_DEVICE_IN_VOICE_REC_DMIC_FLUENCE,
    SND_DEVICE_IN_QUAD_MIC,
    SND_DEVICE_IN_HANDSET_STEREO_DMIC,
    SND_DEVICE_IN_SPEAKER_STEREO_DMIC,
};

static const snd_device_t tomtom_msm8994_CDP_variant_devices[] = {
    SND_DEVICE_IN_HANDSET_MIC,
};

static const snd_device_t tomtom_liquid_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_EXTERNAL_1,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
    SND_DEVICE_IN_SPEAKER_MIC,
    SND_DEVICE_IN_HEADSET_MIC,
    SND_DEVICE_IN_VOICE_DMIC,
    SND_DEVICE_IN_VOICE_SPEAKER_DMIC,
    SND_DEVICE_IN_VOICE_REC_DMIC_STEREO,
    SND_DEVICE_IN_VOICE_REC_DMIC_FLUENCE,
    SND_DEVICE_IN_QUAD_MIC,
    SND_DEVICE_IN_HANDSET_STEREO_DMIC,
    SND_DEVICE_IN_SPEAKER_STEREO_DMIC,
};

static const snd_device_t tomtom_stp_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
};

static const snd_device_t taiko_DB_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
    SND_DEVICE_IN_SPEAKER_MIC,
    SND_DEVICE_IN_HEADSET_MIC,
    SND_DEVICE_IN_QUAD_MIC,
};

static const snd_device_t tomtom_DB_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_EXTERNAL_1,
    SND_DEVICE_OUT_SPEAKER_EXTERNAL_2,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_2,
    SND_DEVICE_OUT_VOICE_SPEAKER,
    SND_DEVICE_IN_VOICE_SPEAKER_MIC,
    SND_DEVICE_IN_HANDSET_MIC,
    SND_DEVICE_IN_HANDSET_MIC_EXTERNAL
};

static const snd_device_t tasha_DB_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER
};

static const snd_device_t tasha_sbc_variant_devices[] = {
    SND_DEVICE_IN_HANDSET_MIC
};

static const snd_device_t taiko_apq8084_sbc_variant_devices[] = {
    SND_DEVICE_IN_HANDSET_MIC,
    SND_DEVICE_IN_SPEAKER_MIC,
};

static const snd_device_t tapan_lite_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_VOICE_HEADPHONES,
    SND_DEVICE_OUT_VOICE_TTY_FULL_HEADPHONES,
    SND_DEVICE_OUT_VOICE_TTY_VCO_HEADPHONES,
};

static const snd_device_t tapan_skuf_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
    /*SND_DEVICE_OUT_SPEAKER_AND_ANC_FB_HEADSET,*/
};

static const snd_device_t tapan_lite_skuf_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_VOICE_HEADPHONES,
    SND_DEVICE_OUT_VOICE_TTY_FULL_HEADPHONES,
    SND_DEVICE_OUT_VOICE_TTY_VCO_HEADPHONES,
};

static const snd_device_t helicon_skuab_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
    SND_DEVICE_OUT_VOICE_SPEAKER,
};

static const snd_device_t tasha_fluid_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
    SND_DEVICE_OUT_VOICE_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_AND_HDMI,
    SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET,
    SND_DEVICE_OUT_SPEAKER_PROTECTED,
    SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED,
};

static const snd_device_t tasha_liquid_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_SPEAKER_EXTERNAL_1,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1,
    SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES,
    SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET,
    SND_DEVICE_IN_SPEAKER_MIC,
    SND_DEVICE_IN_HEADSET_MIC,
    SND_DEVICE_IN_VOICE_DMIC,
    SND_DEVICE_IN_VOICE_SPEAKER_DMIC,
    SND_DEVICE_IN_VOICE_REC_DMIC_STEREO,
    SND_DEVICE_IN_VOICE_REC_DMIC_FLUENCE,
    SND_DEVICE_IN_QUAD_MIC,
    SND_DEVICE_IN_HANDSET_STEREO_DMIC,
    SND_DEVICE_IN_SPEAKER_STEREO_DMIC,
};


static const snd_device_t tavil_qrd_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_VOICE_SPEAKER,
    SND_DEVICE_OUT_HANDSET,
    SND_DEVICE_OUT_VOICE_HANDSET,
    SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET,
};

static const snd_device_t tavil_qrd_msmnile_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER,
    SND_DEVICE_OUT_VOICE_SPEAKER,
    SND_DEVICE_OUT_HANDSET,
    SND_DEVICE_OUT_VOICE_HANDSET,
    SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET,
    SND_DEVICE_IN_VOICE_HEADSET_MIC,
    SND_DEVICE_IN_HANDSET_MIC,
    SND_DEVICE_IN_HANDSET_MIC_AEC,
    SND_DEVICE_IN_HANDSET_MIC_NS,
    SND_DEVICE_IN_HANDSET_MIC_AEC_NS,
    SND_DEVICE_IN_SPEAKER_MIC,
    SND_DEVICE_IN_VOICE_SPEAKER_MIC,
    SND_DEVICE_IN_SPEAKER_MIC_AEC,
    SND_DEVICE_IN_SPEAKER_MIC_NS,
    SND_DEVICE_IN_SPEAKER_MIC_AEC_NS,
    SND_DEVICE_IN_VOICE_DMIC,
    SND_DEVICE_IN_HANDSET_DMIC,
    SND_DEVICE_IN_HANDSET_STEREO_DMIC,
    SND_DEVICE_IN_SPEAKER_STEREO_DMIC,
    SND_DEVICE_IN_VOICE_SPEAKER_DMIC,
    SND_DEVICE_IN_VOICE_SPEAKER_DMIC_BROADSIDE,
    SND_DEVICE_IN_SPEAKER_DMIC_AEC_BROADSIDE,
    SND_DEVICE_IN_SPEAKER_DMIC_NS_BROADSIDE,
    SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_BROADSIDE,
    SND_DEVICE_IN_THREE_MIC,
    SND_DEVICE_IN_HANDSET_TMIC,
    SND_DEVICE_IN_HANDSET_TMIC_FLUENCE_PRO,
    SND_DEVICE_IN_HANDSET_TMIC_AEC,
    SND_DEVICE_IN_HANDSET_TMIC_NS,
    SND_DEVICE_IN_HANDSET_TMIC_AEC_NS,
    SND_DEVICE_IN_VOICE_SPEAKER_TMIC,
    SND_DEVICE_IN_SPEAKER_TMIC_AEC,
    SND_DEVICE_IN_SPEAKER_TMIC_NS,
    SND_DEVICE_IN_SPEAKER_TMIC_AEC_NS,
    SND_DEVICE_IN_QUAD_MIC,
    SND_DEVICE_IN_HANDSET_QMIC,
    SND_DEVICE_IN_SPEAKER_QMIC_AEC,
    SND_DEVICE_IN_SPEAKER_QMIC_NS,
    SND_DEVICE_IN_SPEAKER_QMIC_AEC_NS,
    SND_DEVICE_IN_VOICE_SPEAKER_QMIC,
    SND_DEVICE_IN_AANC_HANDSET_MIC,
    SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET,
    SND_DEVICE_IN_VOICE_FLUENCE_DMIC_AANC,
    SND_DEVICE_OUT_SPEAKER_PROTECTED,
    SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED,
    SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED,
};


static const snd_device_t auto_variant_devices[] = {
    SND_DEVICE_OUT_SPEAKER
};

static void  update_hardware_info_8084(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (!strcmp(snd_card_name, "apq8084-taiko-mtp-snd-card") ||
        !strncmp(snd_card_name, "apq8084-taiko-i2s-mtp-snd-card",
                 sizeof("apq8084-taiko-i2s-mtp-snd-card")) ||
        !strncmp(snd_card_name, "apq8084-tomtom-mtp-snd-card",
                 sizeof("apq8084-tomtom-mtp-snd-card"))) {
        strlcpy(hw_info->type, "mtp", sizeof(hw_info->type));
        strlcpy(hw_info->name, "apq8084", sizeof(hw_info->name));
        hw_info->snd_devices = NULL;
        hw_info->num_snd_devices = 0;
        strlcpy(hw_info->dev_extn, "", sizeof(hw_info->dev_extn));
    } else if ((!strcmp(snd_card_name, "apq8084-taiko-cdp-snd-card")) ||
        !strncmp(snd_card_name, "apq8084-tomtom-cdp-snd-card",
                 sizeof("apq8084-tomtom-cdp-snd-card"))) {
        strlcpy(hw_info->type, " cdp", sizeof(hw_info->type));
        strlcpy(hw_info->name, "apq8084", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)taiko_apq8084_CDP_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(taiko_apq8084_CDP_variant_devices);
        strlcpy(hw_info->dev_extn, "-cdp", sizeof(hw_info->dev_extn));
    } else if (!strncmp(snd_card_name, "apq8084-taiko-i2s-cdp-snd-card",
                        sizeof("apq8084-taiko-i2s-cdp-snd-card"))) {
        strlcpy(hw_info->type, " cdp", sizeof(hw_info->type));
        strlcpy(hw_info->name, "apq8084", sizeof(hw_info->name));
        hw_info->snd_devices = NULL;
        hw_info->num_snd_devices = 0;
        strlcpy(hw_info->dev_extn, "", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "apq8084-taiko-liquid-snd-card")) {
        strlcpy(hw_info->type , " liquid", sizeof(hw_info->type));
        strlcpy(hw_info->name, "apq8084", sizeof(hw_info->type));
        hw_info->snd_devices = (snd_device_t *)taiko_liquid_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(taiko_liquid_variant_devices);
        strlcpy(hw_info->dev_extn, "-liquid", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "apq8084-taiko-sbc-snd-card")) {
        strlcpy(hw_info->type, " sbc", sizeof(hw_info->type));
        strlcpy(hw_info->name, "apq8084", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)taiko_apq8084_sbc_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(taiko_apq8084_sbc_variant_devices);
        strlcpy(hw_info->dev_extn, "-sbc", sizeof(hw_info->dev_extn));
    } else {
        ALOGW("%s: Not an 8084 device", __func__);
    }
}

static void  update_hardware_info_8096(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (!strcmp(snd_card_name, "apq8096-tasha-i2c-snd-card")) {
        ALOGW("%s: Updating hardware info for APQ 8096", __func__);
        strlcpy(hw_info->type, "mtp", sizeof(hw_info->type));
        strlcpy(hw_info->name, "apq8096", sizeof(hw_info->name));
        hw_info->snd_devices = NULL;
        hw_info->num_snd_devices = 0;
        strlcpy(hw_info->dev_extn, "", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "apq8096-auto-snd-card")) {
        strlcpy(hw_info->type, " dragon-board", sizeof(hw_info->type));
        strlcpy(hw_info->name, "apq8096", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)auto_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(auto_variant_devices);
        strlcpy(hw_info->dev_extn, "-db", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "apq8096-adp-agave-snd-card")) {
        strlcpy(hw_info->type, " agave", sizeof(hw_info->type));
        strlcpy(hw_info->name, "apq8096", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)auto_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(auto_variant_devices);
        strlcpy(hw_info->dev_extn, "-agave", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "apq8096-adp-mmxf-snd-card")) {
        strlcpy(hw_info->type, " mmxf", sizeof(hw_info->type));
        strlcpy(hw_info->name, "apq8096", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)auto_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(auto_variant_devices);
        strlcpy(hw_info->dev_extn, "-mmxf", sizeof(hw_info->dev_extn));
    } else {
        ALOGW("%s: Not an 8096 device", __func__);
    }
}

static void  update_hardware_info_8994(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (!strcmp(snd_card_name, "msm8994-tomtom-mtp-snd-card")) {
        strlcpy(hw_info->type, " mtp", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8994", sizeof(hw_info->name));
        hw_info->snd_devices = NULL;
        hw_info->num_snd_devices = 0;
        strlcpy(hw_info->dev_extn, "", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8994-tomtom-cdp-snd-card")) {
        strlcpy(hw_info->type, " cdp", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8994", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tomtom_msm8994_CDP_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tomtom_msm8994_CDP_variant_devices);
        strlcpy(hw_info->dev_extn, "-cdp", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8994-tomtom-stp-snd-card")) {
        strlcpy(hw_info->type, " stp", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8994", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tomtom_stp_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tomtom_stp_variant_devices);
        strlcpy(hw_info->dev_extn, "-stp", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8994-tomtom-liquid-snd-card")) {
        strlcpy(hw_info->type, " liquid", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8994", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tomtom_liquid_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tomtom_liquid_variant_devices);
        strlcpy(hw_info->dev_extn, "-liquid", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8994-tomtom-db-snd-card")) {
        strlcpy(hw_info->type, " dragon-board", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8994", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tomtom_DB_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tomtom_DB_variant_devices);
        strlcpy(hw_info->dev_extn, "-db", sizeof(hw_info->dev_extn));
    } else {
        ALOGW("%s: Not an 8994 device", __func__);
    }
}

static void  update_hardware_info_8996(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (!strcmp(snd_card_name, "msm8996-tasha-fluid-snd-card")) {
        strlcpy(hw_info->type, " fluid", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8996", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tasha_fluid_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tasha_fluid_variant_devices);
        strlcpy(hw_info->dev_extn, "-fluid", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8996-tasha-liquid-snd-card")) {
        strlcpy(hw_info->type, " liquid", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8996", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tasha_liquid_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tasha_liquid_variant_devices);
        strlcpy(hw_info->dev_extn, "-liquid", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8996-tasha-db-snd-card")) {
        strlcpy(hw_info->type, " dragon-board", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8996", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tasha_DB_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tasha_DB_variant_devices);
        strlcpy(hw_info->dev_extn, "-db", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8996-tasha-sbc-snd-card")) {
        strlcpy(hw_info->type, " sbc", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8996", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tasha_sbc_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tasha_sbc_variant_devices);
        strlcpy(hw_info->dev_extn, "-sbc", sizeof(hw_info->dev_extn));
    } else {
        ALOGW("%s: Not a 8996 device", __func__);
    }
}

static void  update_hardware_info_msm8998(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (!strcmp(snd_card_name, "msm8998-tasha-fluid-snd-card")) {
        strlcpy(hw_info->type, " fluid", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8998", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tasha_fluid_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tasha_fluid_variant_devices);
        hw_info->is_stereo_spkr = false;
        strlcpy(hw_info->dev_extn, "-fluid", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8998-tasha-liquid-snd-card")) {
        strlcpy(hw_info->type, " liquid", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8998", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tasha_liquid_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tasha_liquid_variant_devices);
        strlcpy(hw_info->dev_extn, "-liquid", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8998-tasha-db-snd-card")) {
        strlcpy(hw_info->type, " dragon-board", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8998", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tasha_DB_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tasha_DB_variant_devices);
        strlcpy(hw_info->dev_extn, "-db", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8998-qvr-tavil-snd-card")) {
        hw_info->is_stereo_spkr = false;
    } else if (!strcmp(snd_card_name, "msm8998-skuk-tavil-snd-card")) {
        hw_info->is_stereo_spkr = false;
    } else {
        ALOGW("%s: Not a msm8998 device", __func__);
    }
}

static void  update_hardware_info_sdm845(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (!strcmp(snd_card_name, "sdm845-tavil-qrd-snd-card")) {
        strlcpy(hw_info->type, " qrd", sizeof(hw_info->type));
        strlcpy(hw_info->name, "sdm845", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tavil_qrd_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tavil_qrd_variant_devices);
        hw_info->is_stereo_spkr = false;
        strlcpy(hw_info->dev_extn, "-qrd", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "sdm845-tavil-hdk-snd-card")) {
        strlcpy(hw_info->type, " hdk", sizeof(hw_info->type));
        strlcpy(hw_info->name, "sdm845", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tavil_qrd_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tavil_qrd_variant_devices);
        hw_info->is_stereo_spkr = false;
        strlcpy(hw_info->dev_extn, "-hdk", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "sdm845-qvr-tavil-snd-card")) {
        hw_info->is_stereo_spkr = false;
    } else {
        ALOGW("%s: Not a sdm845 device", __func__);
    }
}

static void  update_hardware_info_msmnile(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (strstr(snd_card_name, "qrd")) {
        strlcpy(hw_info->type, " qrd", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msmnile", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tavil_qrd_msmnile_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tavil_qrd_msmnile_variant_devices);
        hw_info->is_stereo_spkr = false;
        strlcpy(hw_info->dev_extn, "-qrd", sizeof(hw_info->dev_extn));
    } else if (strstr(snd_card_name, "pahu")) {
        strlcpy(hw_info->name, "msmnile", sizeof(hw_info->name));
        hw_info->is_stereo_spkr = false;
    } else if (strstr(snd_card_name, "adp")) {
        strlcpy(hw_info->type, "adp", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msmnile", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)auto_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(auto_variant_devices);
        strlcpy(hw_info->dev_extn, "-adp", sizeof(hw_info->dev_extn));
    } else if (strstr(snd_card_name, "custom")) {
        strlcpy(hw_info->type, "custom", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msmnile", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)auto_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(auto_variant_devices);
        strlcpy(hw_info->dev_extn, "-custom", sizeof(hw_info->dev_extn));
    } else {
        ALOGW("%s: Not a msmnile device", __func__);
    }
}

static void  update_hardware_info_sdx(struct hardware_info *hw_info __unused, const char *snd_card_name __unused)
{
    ALOGW("%s: Not a sdx device", __func__);
}

static void  update_hardware_info_8974(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (!strcmp(snd_card_name, "msm8974-taiko-mtp-snd-card")) {
        strlcpy(hw_info->type, " mtp", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8974", sizeof(hw_info->name));
        hw_info->snd_devices = NULL;
        hw_info->num_snd_devices = 0;
        strlcpy(hw_info->dev_extn, "", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8974-taiko-cdp-snd-card")) {
        strlcpy(hw_info->type, " cdp", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8974", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)taiko_CDP_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(taiko_CDP_variant_devices);
        strlcpy(hw_info->dev_extn, "-cdp", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8974-taiko-fluid-snd-card")) {
        strlcpy(hw_info->type, " fluid", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8974", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *) taiko_fluid_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(taiko_fluid_variant_devices);
        strlcpy(hw_info->dev_extn, "-fluid", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8974-taiko-liquid-snd-card")) {
        strlcpy(hw_info->type, " liquid", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8974", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)taiko_liquid_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(taiko_liquid_variant_devices);
        strlcpy(hw_info->dev_extn, "-liquid", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "apq8074-taiko-db-snd-card")) {
        strlcpy(hw_info->type, " dragon-board", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8974", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)taiko_DB_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(taiko_DB_variant_devices);
        strlcpy(hw_info->dev_extn, "-DB", sizeof(hw_info->dev_extn));
    } else {
        ALOGW("%s: Not an 8974 device", __func__);
    }
}

static void update_hardware_info_8610(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (!strcmp(snd_card_name, "msm8x10-snd-card")) {
        strlcpy(hw_info->type, "", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8x10", sizeof(hw_info->name));
        hw_info->snd_devices = NULL;
        hw_info->num_snd_devices = 0;
        strlcpy(hw_info->dev_extn, "", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8x10-skuab-snd-card")) {
        strlcpy(hw_info->type, "skuab", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8x10", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)helicon_skuab_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(helicon_skuab_variant_devices);
        strlcpy(hw_info->dev_extn, "-skuab", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8x10-skuaa-snd-card")) {
        strlcpy(hw_info->type, " skuaa", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8x10", sizeof(hw_info->name));
        hw_info->snd_devices = NULL;
        hw_info->num_snd_devices = 0;
        strlcpy(hw_info->dev_extn, "", sizeof(hw_info->dev_extn));
    } else {
        ALOGW("%s: Not an  8x10 device", __func__);
    }
}

static void update_hardware_info_8226(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (!strcmp(snd_card_name, "msm8226-tapan-snd-card")) {
        strlcpy(hw_info->type, "", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8226", sizeof(hw_info->name));
        hw_info->snd_devices = NULL;
        hw_info->num_snd_devices = 0;
        strlcpy(hw_info->dev_extn, "", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8226-tapan9302-snd-card")) {
        strlcpy(hw_info->type, "tapan_lite", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8226", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tapan_lite_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tapan_lite_variant_devices);
        strlcpy(hw_info->dev_extn, "-lite", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8226-tapan-skuf-snd-card")) {
        strlcpy(hw_info->type, " skuf", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8226", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *) tapan_skuf_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tapan_skuf_variant_devices);
        strlcpy(hw_info->dev_extn, "-skuf", sizeof(hw_info->dev_extn));
    } else if (!strcmp(snd_card_name, "msm8226-tapan9302-skuf-snd-card")) {
        strlcpy(hw_info->type, " tapan9302-skuf", sizeof(hw_info->type));
        strlcpy(hw_info->name, "msm8226", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tapan_lite_skuf_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tapan_lite_skuf_variant_devices);
        strlcpy(hw_info->dev_extn, "-skuf-lite", sizeof(hw_info->dev_extn));
    } else {
        ALOGW("%s: Not an  8x26 device", __func__);
    }
}

static void update_hardware_info_bear(struct hardware_info *hw_info, const char *snd_card_name)
{
    if (!strncmp(snd_card_name, "sdm660-snd-card",
                 sizeof("sdm660-snd-card"))) {
        strlcpy(hw_info->name, "sdm660", sizeof(hw_info->name));
    } else if (!strncmp(snd_card_name, "sdm660-tavil-snd-card",
                      sizeof("sdm660-tavil-snd-card"))) {
        strlcpy(hw_info->name, "sdm660", sizeof(hw_info->name));
    } else if (!strncmp(snd_card_name, "sdm670-skuw-snd-card",
                      sizeof("sdm670-skuw-snd-card"))) {
        hw_info->is_stereo_spkr = false;
    } else if ( !strncmp(snd_card_name, "sdm670-tavil-hdk-snd-card",
                      sizeof("sdm670-tavil-hdk-snd-card"))) {
        strlcpy(hw_info->type, " hdk", sizeof(hw_info->type));
        strlcpy(hw_info->name, "sdm670", sizeof(hw_info->name));
        hw_info->snd_devices = (snd_device_t *)tavil_qrd_variant_devices;
        hw_info->num_snd_devices = ARRAY_SIZE(tavil_qrd_variant_devices);
        hw_info->is_stereo_spkr = false;
        strlcpy(hw_info->dev_extn, "-hdk", sizeof(hw_info->dev_extn));
    } else if (!strncmp(snd_card_name, "sm6150-idp-snd-card",
                 sizeof("sm6150-idp-snd-card"))) {
        strlcpy(hw_info->name, "sm6150", sizeof(hw_info->name));
    } else if (!strncmp(snd_card_name, "sm6150-qrd-snd-card",
                 sizeof("sm6150-qrd-snd-card"))) {
        hw_info->is_stereo_spkr = false;
        strlcpy(hw_info->name, "sm6150", sizeof(hw_info->name));
    } else if (!strncmp(snd_card_name, "sm6150-tavil-snd-card",
                 sizeof("sm6150-tavil-snd-card"))) {
        strlcpy(hw_info->name, "sm6150", sizeof(hw_info->name));

    } else {
        ALOGW("%s: Not an SDM device", __func__);
    }
}

void *hw_info_init(const char *snd_card_name)
{
    struct hardware_info *hw_info;

    hw_info = malloc(sizeof(struct hardware_info));
    if (!hw_info) {
        ALOGE("failed to allocate mem for hardware info");
        return NULL;
    }

    hw_info->snd_devices = NULL;
    hw_info->num_snd_devices = 0;
    hw_info->is_stereo_spkr = true;
    strlcpy(hw_info->dev_extn, "", sizeof(hw_info->dev_extn));
    strlcpy(hw_info->type, "", sizeof(hw_info->type));
    strlcpy(hw_info->name, "", sizeof(hw_info->name));

    if(strstr(snd_card_name, "msm8974") ||
              strstr(snd_card_name, "apq8074")) {
        ALOGV("8974 - variant soundcard");
        update_hardware_info_8974(hw_info, snd_card_name);
    } else if(strstr(snd_card_name, "msm8226")) {
        ALOGV("8x26 - variant soundcard");
        update_hardware_info_8226(hw_info, snd_card_name);
    } else if(strstr(snd_card_name, "msm8x10")) {
        ALOGV("8x10 - variant soundcard");
        update_hardware_info_8610(hw_info, snd_card_name);
    } else if(strstr(snd_card_name, "apq8084")) {
        ALOGV("8084 - variant soundcard");
        update_hardware_info_8084(hw_info, snd_card_name);
    } else if(strstr(snd_card_name, "msm8994")) {
        ALOGV("8994 - variant soundcard");
        update_hardware_info_8994(hw_info, snd_card_name);
    } else if(strstr(snd_card_name, "apq8096")) {
        ALOGV("8096 - variant soundcard");
        update_hardware_info_8096(hw_info, snd_card_name);
    } else if(strstr(snd_card_name, "msm8996")) {
        ALOGV("8996 - variant soundcard");
        update_hardware_info_8996(hw_info, snd_card_name);
    } else if((strstr(snd_card_name, "msm8998")) || (strstr(snd_card_name, "apq8098_latv"))) {
        ALOGV("MSM8998 - variant soundcard");
        update_hardware_info_msm8998(hw_info, snd_card_name);
    } else if(strstr(snd_card_name, "sdm845")) {
        ALOGV("SDM845 - variant soundcard");
        update_hardware_info_sdm845(hw_info, snd_card_name);
    } else if (strstr(snd_card_name, "sdm660") || strstr(snd_card_name, "sdm670")
               || strstr(snd_card_name, "sm6150")) {
        ALOGV("Bear - variant soundcard");
        update_hardware_info_bear(hw_info, snd_card_name);
    } else if (strstr(snd_card_name, "sdx")) {
        ALOGV("SDX - variant soundcard");
        update_hardware_info_sdx(hw_info, snd_card_name);
    } else if (strstr(snd_card_name, "pahu") || strstr(snd_card_name, "tavil") ||
            strstr(snd_card_name, "sa8155")) {
        ALOGV("MSMNILE - variant soundcard");
        update_hardware_info_msmnile(hw_info, snd_card_name);
    } else {
        ALOGE("%s: Unsupported target %s:",__func__, snd_card_name);
        free(hw_info);
        hw_info = NULL;
    }

    return hw_info;
}

void hw_info_deinit(void *hw_info)
{
    struct hardware_info *my_data = (struct hardware_info*) hw_info;

    if(!my_data)
        free(my_data);
}

void hw_info_append_hw_type(void *hw_info, snd_device_t snd_device,
                            char *device_name)
{
    struct hardware_info *my_data = (struct hardware_info*) hw_info;
    uint32_t i = 0;

    snd_device_t *snd_devices =
            (snd_device_t *) my_data->snd_devices;

    if(snd_devices != NULL) {
        for (i = 0; i <  my_data->num_snd_devices; i++) {
            if (snd_device == (snd_device_t)snd_devices[i]) {
                ALOGV("extract dev_extn device %d, extn = %s",
                        (snd_device_t)snd_devices[i],  my_data->dev_extn);
                CHECK(strlcat(device_name,  my_data->dev_extn,
                        DEVICE_NAME_MAX_SIZE) < DEVICE_NAME_MAX_SIZE);
                break;
            }
        }
    }
    ALOGD("%s : device_name = %s", __func__,device_name);
}

bool hw_info_is_stereo_spkr(void *hw_info)
{
    struct hardware_info *my_data = (struct hardware_info*) hw_info;

    return my_data->is_stereo_spkr;
}
