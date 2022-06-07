/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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

/*
Changes from Qualcomm Innovation Center are provided under the following license:
Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the
disclaimer below) provided that the following conditions are met:

	    * Redistributions of source code must retain the above copyright
              notice, this list of conditions and the following disclaimer.

	    * Redistributions in binary form must reproduce the above
              copyright notice, this list of conditions and the following
              disclaimer in the documentation and/or other materials provided
              with the distribution.

            * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
	      contributors may be used to endorse or promote products derived
              from this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define LOG_TAG "AHAL: hfp"
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include "AudioCommon.h"
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <math.h>
#include <cutils/properties.h>
#include "PalApi.h"
#include "AudioDevice.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_PARAMETER_HFP_ENABLE      "hfp_enable"
#define AUDIO_PARAMETER_HFP_SET_SAMPLING_RATE "hfp_set_sampling_rate"
#define AUDIO_PARAMETER_KEY_HFP_VOLUME "hfp_volume"
#define AUDIO_PARAMETER_HFP_PCM_DEV_ID "hfp_pcm_dev_id"

#define AUDIO_PARAMETER_KEY_HFP_MIC_VOLUME "hfp_mic_volume"

struct hfp_module {
    bool is_hfp_running;
    float hfp_volume;
    int ucid;
    float mic_volume;
    bool mic_mute;
    uint32_t sample_rate;
    pal_stream_handle_t *rx_stream_handle;
    pal_stream_handle_t *tx_stream_handle;
};

#define PLAYBACK_VOLUME_MAX 0x2000
#define CAPTURE_VOLUME_DEFAULT                (15.0)
static struct hfp_module hfpmod = {
    .is_hfp_running = 0,
    .hfp_volume = 0,
    .ucid = USECASE_AUDIO_HFP_SCO,
    .mic_volume = CAPTURE_VOLUME_DEFAULT,
    .mic_mute = 0,
    .sample_rate = 16000,
};

static int32_t hfp_set_volume(float value)
{
    int32_t vol, ret = 0;
    struct pal_volume_data *pal_volume = NULL;

    AHAL_VERBOSE("entry");
    AHAL_DBG("(%f)\n", value);

    hfpmod.hfp_volume = value;

    if (value < 0.0) {
        AHAL_DBG("(%f) Under 0.0, assuming 0.0\n", value);
        value = 0.0;
    } else {
        value = ((value > 15.000000) ? 1.0 : (value / 15));
        AHAL_DBG("Volume brought with in range (%f)\n", value);
    }
    vol  = lrint((value * 0x2000) + 0.5);

    if (!hfpmod.is_hfp_running) {
        AHAL_VERBOSE("HFP not active, ignoring set_hfp_volume call");
        return -EIO;
    }

    AHAL_DBG("Setting HFP volume to %d \n", vol);

    pal_volume = (struct pal_volume_data *)malloc(sizeof(struct pal_volume_data)
            +sizeof(struct pal_channel_vol_kv));

    if (!pal_volume)
       return -ENOMEM;

    pal_volume->no_of_volpair = 1;
    pal_volume->volume_pair[0].channel_mask = 0x03;
    pal_volume->volume_pair[0].vol = value;
    ret = pal_stream_set_volume(hfpmod.rx_stream_handle, pal_volume);
    if (ret)
        AHAL_ERR("set volume failed: %d \n", ret);

    free(pal_volume);
    AHAL_VERBOSE("exit");
    return ret;
}

/*Set mic volume to value.
 * *
 * * This interface is used for mic volume control, set mic volume as value(range 0 ~ 15).
 * */
static int hfp_set_mic_volume(float value)
{
    int volume, ret = 0;
    struct pal_volume_data *pal_volume = NULL;

    AHAL_DBG("enter, value=%f", value);

    if (!hfpmod.is_hfp_running) {
        AHAL_ERR("HFP not active, ignoring set_hfp_mic_volume call");
        return -EIO;
    }

    if (value < 0.0) {
        AHAL_DBG("(%f) Under 0.0, assuming 0.0\n", value);
        value = 0.0;
    } else if (value > CAPTURE_VOLUME_DEFAULT) {
        value = CAPTURE_VOLUME_DEFAULT;
        AHAL_DBG("Volume brought within range (%f)\n", value);
    }

    value = value / CAPTURE_VOLUME_DEFAULT;

    volume = (int)(value * PLAYBACK_VOLUME_MAX);

    pal_volume = (struct pal_volume_data *)malloc(sizeof(struct pal_volume_data)
            +sizeof(struct pal_channel_vol_kv));
    if (!pal_volume) {
        AHAL_ERR("Failed to allocate memory for pal_volume");
        return -ENOMEM;
    }
    pal_volume->no_of_volpair = 1;
    pal_volume->volume_pair[0].channel_mask = 0x03;
    pal_volume->volume_pair[0].vol = value;
    if (pal_stream_set_volume(hfpmod.tx_stream_handle, pal_volume) < 0) {
        AHAL_ERR("Couldn't set HFP Volume: [%d]", volume);
        free(pal_volume);
        pal_volume = NULL;
        return -EINVAL;
    }

    free(pal_volume);
    pal_volume = NULL;
    hfpmod.mic_volume = value;

    return ret;
}

static float hfp_get_mic_volume(std::shared_ptr<AudioDevice> adev __unused)
{
    return hfpmod.mic_volume;
}

static int32_t start_hfp(std::shared_ptr<AudioDevice> adev __unused,
        struct str_parms *parms __unused)
{
    int32_t ret = 0;
    uint32_t no_of_devices = 2;
    struct pal_stream_attributes stream_attr = {};
    struct pal_stream_attributes stream_tx_attr = {};
    struct pal_device devices[2] = {};
    struct pal_channel_info ch_info;

    AHAL_DBG("HFP start enter");
    if (hfpmod.rx_stream_handle || hfpmod.tx_stream_handle)
        return 0; //hfp already running;

    pal_param_device_connection_t param_device_connection;

    param_device_connection.id = PAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
    param_device_connection.connection_state = true;
    ret =  pal_set_param(PAL_PARAM_ID_DEVICE_CONNECTION,
                        (void*)&param_device_connection,
                        sizeof(pal_param_device_connection_t));
    if (ret != 0) {
        AHAL_ERR("Set PAL_PARAM_ID_DEVICE_CONNECTION for %d failed", param_device_connection.id);
        return ret;
    }

    param_device_connection.id = PAL_DEVICE_OUT_BLUETOOTH_SCO;
    param_device_connection.connection_state = true;
    ret =  pal_set_param(PAL_PARAM_ID_DEVICE_CONNECTION,
                        (void*)&param_device_connection,
                        sizeof(pal_param_device_connection_t));
    if (ret != 0) {
        AHAL_ERR("Set PAL_PARAM_ID_DEVICE_CONNECTION for %d failed", param_device_connection.id);
        return ret;
    }

    pal_param_btsco_t param_btsco;

    param_btsco.bt_sco_on = true;
    ret =  pal_set_param(PAL_PARAM_ID_BT_SCO,
                        (void*)&param_btsco,
                        sizeof(pal_param_btsco_t));
    if (ret != 0) {
        AHAL_ERR("Set PAL_PARAM_ID_BT_SCO failed");
        return ret;
    }

    if (hfpmod.sample_rate == 16000) {
        param_btsco.bt_wb_speech_enabled = true;
    }
    else
    {
        param_btsco.bt_wb_speech_enabled = false;
    }

    ret =  pal_set_param(PAL_PARAM_ID_BT_SCO_WB,
                        (void*)&param_btsco,
                        sizeof(pal_param_btsco_t));
    if (ret != 0) {
        AHAL_ERR("Set PAL_PARAM_ID_BT_SCO_WB failed");
        return ret;
    }

    ch_info.channels = 1;
    ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;

    /* BT SCO -> Spkr */
    stream_attr.type = PAL_STREAM_LOOPBACK;
    stream_attr.info.opt_stream_info.loopback_type = PAL_STREAM_LOOPBACK_HFP_RX;
    stream_attr.direction = PAL_AUDIO_INPUT_OUTPUT;
    stream_attr.in_media_config.sample_rate = hfpmod.sample_rate;
    stream_attr.in_media_config.bit_width = 16;
    stream_attr.in_media_config.ch_info = ch_info;
    stream_attr.in_media_config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;

    stream_attr.out_media_config.sample_rate = 48000;
    stream_attr.out_media_config.bit_width = 16;
    stream_attr.out_media_config.ch_info = ch_info;
    stream_attr.out_media_config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;

    devices[0].id = PAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
    devices[0].config.sample_rate = hfpmod.sample_rate;
    devices[0].config.bit_width = 16;
    devices[0].config.ch_info = ch_info;
    devices[0].config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;

    devices[1].id = PAL_DEVICE_OUT_SPEAKER;

    ret = pal_stream_open(&stream_attr,
            no_of_devices, devices,
            0,
            NULL,
            NULL,
            0,
            &hfpmod.rx_stream_handle);
    if (ret != 0) {
        AHAL_ERR("HFP rx stream (BT SCO->Spkr) open failed, rc %d", ret);
        return ret;
    }
    ret = pal_stream_start(hfpmod.rx_stream_handle);
    if (ret != 0) {
        AHAL_ERR("HFP rx stream (BT SCO->Spkr) start failed, rc %d", ret);
        pal_stream_close(hfpmod.rx_stream_handle);
        return ret;
    }

    /* Mic -> BT SCO */
    stream_tx_attr.type = PAL_STREAM_LOOPBACK;
    stream_tx_attr.info.opt_stream_info.loopback_type = PAL_STREAM_LOOPBACK_HFP_TX;
    stream_tx_attr.direction = PAL_AUDIO_INPUT_OUTPUT;
    stream_tx_attr.in_media_config.sample_rate = hfpmod.sample_rate;
    stream_tx_attr.in_media_config.bit_width = 16;
    stream_tx_attr.in_media_config.ch_info = ch_info;
    stream_tx_attr.in_media_config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;

    stream_tx_attr.out_media_config.sample_rate = 48000;
    stream_tx_attr.out_media_config.bit_width = 16;
    stream_tx_attr.out_media_config.ch_info = ch_info;
    stream_tx_attr.out_media_config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;

    devices[0].id = PAL_DEVICE_OUT_BLUETOOTH_SCO;
    devices[0].config.sample_rate = hfpmod.sample_rate;
    devices[0].config.bit_width = 16;
    devices[0].config.ch_info = ch_info;
    devices[0].config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;

    devices[1].id = PAL_DEVICE_IN_SPEAKER_MIC;

    ret = pal_stream_open(&stream_tx_attr,
            no_of_devices, devices,
            0,
            NULL,
            NULL,
            0,
            &hfpmod.tx_stream_handle);
    if (ret != 0) {
        AHAL_ERR("HFP tx stream (Mic->BT SCO) open failed, rc %d", ret);
        pal_stream_stop(hfpmod.rx_stream_handle);
        pal_stream_close(hfpmod.rx_stream_handle);
        hfpmod.rx_stream_handle = NULL;
        return ret;
    }
    ret = pal_stream_start(hfpmod.tx_stream_handle);
    if (ret != 0) {
        AHAL_ERR("HFP tx stream (Mic->BT SCO) start failed, rc %d", ret);
        pal_stream_close(hfpmod.tx_stream_handle);
        pal_stream_stop(hfpmod.rx_stream_handle);
        pal_stream_close(hfpmod.rx_stream_handle);
        hfpmod.rx_stream_handle = NULL;
        hfpmod.tx_stream_handle = NULL;
        return ret;
    }
    hfpmod.mic_mute = false;
    hfpmod.is_hfp_running = true;
    hfp_set_volume(hfpmod.hfp_volume);

    AHAL_DBG("HFP start end");
    return ret;
}

static int32_t stop_hfp()
{
    int32_t ret = 0;

    AHAL_DBG("HFP stop enter");
    hfpmod.is_hfp_running = false;
    if (hfpmod.rx_stream_handle) {
        pal_stream_stop(hfpmod.rx_stream_handle);
        pal_stream_close(hfpmod.rx_stream_handle);
        hfpmod.rx_stream_handle = NULL;
    }
    if (hfpmod.tx_stream_handle) {
        pal_stream_stop(hfpmod.tx_stream_handle);
        pal_stream_close(hfpmod.tx_stream_handle);
        hfpmod.tx_stream_handle = NULL;
    }

    pal_param_btsco_t param_btsco;

    param_btsco.bt_sco_on = true;
    ret =  pal_set_param(PAL_PARAM_ID_BT_SCO,
                        (void*)&param_btsco,
                        sizeof(pal_param_btsco_t));
    if (ret != 0) {
        AHAL_ERR("Set PAL_PARAM_ID_BT_SCO failed");
    }

    pal_param_device_connection_t param_device_connection;

    param_device_connection.id = PAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
    param_device_connection.connection_state = false;
    ret =  pal_set_param(PAL_PARAM_ID_DEVICE_CONNECTION,
                        (void*)&param_device_connection,
                        sizeof(pal_param_device_connection_t));
    if (ret != 0) {
        AHAL_ERR("Set PAL_PARAM_ID_DEVICE_DISCONNECTION for %d failed", param_device_connection.id);
    }

    param_device_connection.id = PAL_DEVICE_OUT_BLUETOOTH_SCO;
    param_device_connection.connection_state = false;
    ret =  pal_set_param(PAL_PARAM_ID_DEVICE_CONNECTION,
                        (void*)&param_device_connection,
                        sizeof(pal_param_device_connection_t));
    if (ret != 0) {
        AHAL_ERR("Set PAL_PARAM_ID_DEVICE_DISCONNECTION for %d failed", param_device_connection.id);
    }

    AHAL_DBG("HFP stop end");
    return ret;
}

void hfp_init()
{
    return;
}

bool hfp_is_active(std::shared_ptr<AudioDevice> adev __unused)
{
    return hfpmod.is_hfp_running;
}

audio_usecase_t hfp_get_usecase()
{
    return hfpmod.ucid;
}

/*Set mic mute state.
 * *
 * * This interface is used for mic mute state control
 * */
int hfp_set_mic_mute(std::shared_ptr<AudioDevice> adev,
        bool state)
{
    int rc = 0;

    if (state == hfpmod.mic_mute) {
        AHAL_DBG("mute state already %d", state);
        return rc;
    }

    if (state == true) {
        hfpmod.mic_volume = hfp_get_mic_volume(adev);
    }
    rc = hfp_set_mic_volume((state == true) ? 0.0 : hfpmod.mic_volume);
    hfpmod.mic_mute = state;
    AHAL_DBG("Setting mute state %d, rc %d\n", state, rc);
    return rc;
}

int hfp_set_mic_mute2(std::shared_ptr<AudioDevice> adev __unused, bool state __unused)
{
    AHAL_DBG("Unsupported\n");
    return 0;
}

void hfp_set_parameters(std::shared_ptr<AudioDevice> adev, struct str_parms *parms)
{
    int status = 0;
    char value[32]={0};
    float vol;
    int val;
    int rate;

    AHAL_DBG("enter");

    status = str_parms_get_str(parms, AUDIO_PARAMETER_HFP_ENABLE, value,
                                    sizeof(value));
    if (status >= 0) {
        if (!strncmp(value, "true", sizeof(value)) && !hfpmod.is_hfp_running)
            status = start_hfp(adev, parms);
        else if (!strncmp(value, "false", sizeof(value)) && hfpmod.is_hfp_running)
            stop_hfp();
        else
            AHAL_ERR("hfp_enable=%s is unsupported", value);
    }

    memset(value, 0, sizeof(value));
    status = str_parms_get_str(parms,AUDIO_PARAMETER_HFP_SET_SAMPLING_RATE, value,
                                    sizeof(value));
    if (status >= 0) {
        rate = atoi(value);
        if (rate == 8000){
            hfpmod.ucid = USECASE_AUDIO_HFP_SCO;
            hfpmod.sample_rate = (uint32_t) rate;
        } else if (rate == 16000){
            hfpmod.ucid = USECASE_AUDIO_HFP_SCO_WB;
            hfpmod.sample_rate = (uint32_t) rate;
        } else
            AHAL_ERR("Unsupported rate.. %d", rate);
    }

    memset(value, 0, sizeof(value));
    status = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HFP_VOLUME,
                                    value, sizeof(value));
    if (status >= 0) {
        if (sscanf(value, "%f", &vol) != 1){
            AHAL_ERR("error in retrieving hfp volume");
            status = -EIO;
            goto exit;
        }
        AHAL_DBG("set_hfp_volume usecase, Vol: [%f]", vol);
        hfp_set_volume(vol);
    }

    memset(value, 0, sizeof(value));
    status = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HFP_MIC_VOLUME,
                                    value, sizeof(value));
    if (status >= 0) {
        if (sscanf(value, "%f", &vol) != 1){
            AHAL_ERR("error in retrieving hfp mic volume");
            status = -EIO;
            goto exit;
        }
        AHAL_DBG("set_hfp_mic_volume usecase, Vol: [%f]", vol);
        hfp_set_mic_volume(vol);
    }

exit:
    AHAL_VERBOSE("Exit");
}

#ifdef __cplusplus
}
#endif
