/* hfp.c
Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.

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

#define LOG_TAG "audio_hw_hfp"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <cutils/log.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <stdlib.h>
#include <cutils/str_parms.h>

#ifdef HFP_ENABLED
#define AUDIO_PARAMETER_HFP_ENABLE      "hfp_enable"

static int32_t start_hfp(struct audio_device *adev,
                               struct str_parms *parms);

static int32_t stop_hfp(struct audio_device *adev);

struct hfp_module {
    struct pcm *hfp_sco_rx;
    struct pcm *hfp_sco_tx;
    struct pcm *hfp_pcm_rx;
    struct pcm *hfp_pcm_tx;
    bool is_hfp_running;
    int hfp_volume;
};

static struct hfp_module hfpmod = {
    .hfp_sco_rx = NULL,
    .hfp_sco_tx = NULL,
    .hfp_pcm_rx = NULL,
    .hfp_pcm_tx = NULL,
    .hfp_volume = 0,
    .is_hfp_running = 0,
};
static struct pcm_config pcm_config_hfp = {
    .channels = 1,
    .rate = 8000,
    .period_size = 240,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

static int32_t start_hfp(struct audio_device *adev,
                               struct str_parms *parms)
{
    int32_t i, ret = 0;
    struct audio_usecase *uc_info;
    int32_t pcm_dev_rx_id, pcm_dev_tx_id, pcm_dev_asm_rx_id, pcm_dev_asm_tx_id;

    ALOGD("%s: enter", __func__);

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    uc_info->id = USECASE_AUDIO_HFP_SCO;
    uc_info->type = PCM_HFP_CALL;
    uc_info->stream.out = adev->primary_output;
    uc_info->devices = adev->primary_output->devices;
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    list_add_tail(&adev->usecase_list, &uc_info->list);

    select_devices(adev, USECASE_AUDIO_HFP_SCO);

    pcm_dev_rx_id = platform_get_pcm_device_id(uc_info->id, PCM_PLAYBACK);
    pcm_dev_tx_id = platform_get_pcm_device_id(uc_info->id, PCM_CAPTURE);
    pcm_dev_asm_rx_id = HFP_ASM_RX_TX;
    pcm_dev_asm_tx_id = HFP_ASM_RX_TX;
    if (pcm_dev_rx_id < 0 || pcm_dev_tx_id < 0 ||
        pcm_dev_asm_rx_id < 0 || pcm_dev_asm_tx_id < 0 ) {
        ALOGE("%s: Invalid PCM devices (rx: %d tx: %d asm: rx tx %d) for the usecase(%d)",
              __func__, pcm_dev_rx_id, pcm_dev_tx_id, pcm_dev_asm_rx_id, uc_info->id);
        ret = -EIO;
        goto exit;
    }

    ALOGV("%s: HFP PCM devices (hfp rx tx: %d pcm rx tx: %d) for the usecase(%d)",
              __func__, pcm_dev_rx_id, pcm_dev_tx_id, uc_info->id);

    ALOGV("%s: Opening PCM playback device card_id(%d) device_id(%d)",
          __func__, adev->snd_card, pcm_dev_rx_id);
    hfpmod.hfp_sco_rx = pcm_open(adev->snd_card,
                                  pcm_dev_asm_rx_id,
                                  PCM_OUT, &pcm_config_hfp);
    if (hfpmod.hfp_sco_rx && !pcm_is_ready(hfpmod.hfp_sco_rx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_sco_rx));
        ret = -EIO;
        goto exit;
    }
    ALOGD("%s: Opening PCM capture device card_id(%d) device_id(%d)",
          __func__, adev->snd_card, pcm_dev_tx_id);
    hfpmod.hfp_pcm_rx = pcm_open(adev->snd_card,
                                   pcm_dev_rx_id,
                                   PCM_OUT, &pcm_config_hfp);
    if (hfpmod.hfp_pcm_rx && !pcm_is_ready(hfpmod.hfp_pcm_rx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_pcm_rx));
        ret = -EIO;
        goto exit;
    }
    hfpmod.hfp_sco_tx = pcm_open(adev->snd_card,
                                  pcm_dev_asm_tx_id,
                                  PCM_IN, &pcm_config_hfp);
    if (hfpmod.hfp_sco_tx && !pcm_is_ready(hfpmod.hfp_sco_tx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_sco_tx));
        ret = -EIO;
        goto exit;
    }
    ALOGV("%s: Opening PCM capture device card_id(%d) device_id(%d)",
          __func__, adev->snd_card, pcm_dev_tx_id);
    hfpmod.hfp_pcm_tx = pcm_open(adev->snd_card,
                                   pcm_dev_tx_id,
                                   PCM_IN, &pcm_config_hfp);
    if (hfpmod.hfp_pcm_tx && !pcm_is_ready(hfpmod.hfp_pcm_tx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_pcm_tx));
        ret = -EIO;
        goto exit;
    }
    pcm_start(hfpmod.hfp_sco_rx);
    pcm_start(hfpmod.hfp_sco_tx);
    pcm_start(hfpmod.hfp_pcm_rx);
    pcm_start(hfpmod.hfp_pcm_tx);


    hfpmod.is_hfp_running = true;

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return 0;

exit:
    stop_hfp(adev);
    ALOGE("%s: Problem in HFP start: status(%d)", __func__, ret);
    return ret;
}

static int32_t stop_hfp(struct audio_device *adev)
{
    int32_t i, ret = 0;
    struct audio_usecase *uc_info;

    ALOGD("%s: enter", __func__);
    hfpmod.is_hfp_running = false;

    /* 1. Close the PCM devices */
    if (hfpmod.hfp_sco_rx) {
        pcm_close(hfpmod.hfp_sco_rx);
        hfpmod.hfp_sco_rx = NULL;
    }
    if (hfpmod.hfp_sco_tx) {
        pcm_close(hfpmod.hfp_sco_tx);
        hfpmod.hfp_sco_tx = NULL;
    }
    if (hfpmod.hfp_pcm_rx) {
        pcm_close(hfpmod.hfp_pcm_rx);
        hfpmod.hfp_pcm_rx = NULL;
    }
    if (hfpmod.hfp_pcm_tx) {
        pcm_close(hfpmod.hfp_pcm_tx);
        hfpmod.hfp_pcm_tx = NULL;
    }

    uc_info = get_usecase_from_list(adev, USECASE_AUDIO_HFP_SCO);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, USECASE_AUDIO_HFP_SCO);
        return -EINVAL;
    }

    /* 2. Get and set stream specific mixer controls */
    disable_audio_route(adev, uc_info, true);

    /* 3. Disable the rx and tx devices */
    disable_snd_device(adev, uc_info->out_snd_device, false);
    disable_snd_device(adev, uc_info->in_snd_device, true);

    list_remove(&uc_info->list);
    free(uc_info);

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return ret;
}

void audio_extn_hfp_set_parameters(struct audio_device *adev, struct str_parms *parms)
{
    int ret;
    char value[32]={0};

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HFP_ENABLE, value,
                            sizeof(value));
    if (ret >= 0) {
           if(!strncmp(value,"true",sizeof(value)))
               ret = start_hfp(adev,parms);
           else
               stop_hfp(adev);
    }
}
#endif /*HFP_ENABLED*/
