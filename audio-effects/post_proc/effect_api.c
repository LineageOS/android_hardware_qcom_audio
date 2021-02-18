/*
 * Copyright (c) 2013-2015, 2019-2020 The Linux Foundation. All rights reserved.

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
 * This file was modified by DTS, Inc. The portions of the
 * code modified by DTS, Inc are copyrighted and
 * licensed separately, as follows:
 *
 * (C) 2014 DTS, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "offload_effect_api"
//#define LOG_NDEBUG 0
//#define VERY_VERY_VERBOSE_LOGGING
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <stdbool.h>
#include <errno.h>
#include <log/log.h>
#include <sound/audio_effects.h>
#include <sound/devdep_params.h>
#include <linux/msm_audio.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "effect_api.h"
#include "kvh2xml.h"

#ifdef DTS_EAGLE
#include "effect_util.h"
#endif

#define ARRAY_SIZE(array) (sizeof array / sizeof array[0])
typedef enum eff_mode {
    OFFLOAD,
    HW_ACCELERATOR
} eff_mode_t;

#define OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL 19
const int map_eq_opensl_preset_2_offload_preset[] = {
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL,   /* Normal Preset */
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL+1, /* Classical Preset */
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL+2, /* Dance Preset */
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL+3, /* Flat Preset */
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL+4, /* Folk Preset */
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL+5, /* Heavy Metal Preset */
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL+6, /* Hip Hop Preset */
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL+7, /* Jazz Preset */
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL+8, /* Pop Preset */
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL+9, /* Rock Preset */
    OFFLOAD_PRESET_START_OFFSET_FOR_OPENSL+10 /* FX Booster */
};

const int map_reverb_opensl_preset_2_offload_preset
                  [NUM_OSL_REVERB_PRESETS_SUPPORTED][2] = {
    {1, 15},
    {2, 16},
    {3, 17},
    {4, 18},
    {5, 3},
    {6, 20}
};

void offload_bassboost_set_device(struct bass_boost_params *bassboost,
                                  uint32_t device)
{
    ALOGVV("%s: device 0x%x", __func__, device);
    bassboost->device = device;
}

void offload_bassboost_set_enable_flag(struct bass_boost_params *bassboost,
                                       bool enable)
{
    ALOGVV("%s: enable=%d", __func__, (int)enable);
    bassboost->enable_flag = enable;

#ifdef DTS_EAGLE
    update_effects_node(PCM_DEV_ID, EFFECT_TYPE_BB, EFFECT_ENABLE_PARAM, enable, EFFECT_NO_OP, EFFECT_NO_OP, EFFECT_NO_OP);
#endif
}

int offload_bassboost_get_enable_flag(struct bass_boost_params *bassboost)
{
    ALOGVV("%s: enable=%d", __func__, (int)bassboost->enable_flag);
    return bassboost->enable_flag;
}

void offload_bassboost_set_strength(struct bass_boost_params *bassboost,
                                    int strength)
{
    ALOGVV("%s: strength %d", __func__, strength);
    bassboost->strength = strength;

#ifdef DTS_EAGLE
    update_effects_node(PCM_DEV_ID, EFFECT_TYPE_BB, EFFECT_SET_PARAM, EFFECT_NO_OP, strength, EFFECT_NO_OP, EFFECT_NO_OP);
#endif
}

static int send_kv_payload(pal_stream_handle_t *pal_stream_handle,
                            uint32_t tag, pal_key_vector_t *kvp)
{
   int ret = 0;
   pal_param_payload *pal_payload;
   effect_pal_payload_t *effect_payload = NULL;
   uint8_t *payload = NULL;
   pal_key_vector_t *pal_key_vector = NULL;
   uint32_t payload_size = 0;
   payload_size = sizeof(pal_param_payload) + sizeof(effect_pal_payload_t) +
                  sizeof(pal_key_vector_t) +
                  kvp->num_tkvs * sizeof(pal_key_value_pair_t);

   payload = (uint8_t *) calloc (1, payload_size);
   if (!payload) {
       ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__, payload_size);
       ret = -ENOMEM;
       goto done;
   }
   pal_payload = (pal_param_payload *) payload;
   pal_payload->payload_size = sizeof(effect_pal_payload_t) +
                                  sizeof(pal_key_vector_t) +
                                  kvp->num_tkvs * sizeof(pal_key_value_pair_t);

    effect_payload = (effect_pal_payload_t *)(payload + sizeof(pal_param_payload));
    effect_payload->isTKV = PARAM_TKV;
    effect_payload->tag = tag;
    effect_payload->payloadSize = sizeof(pal_key_vector_t) +
                                  kvp->num_tkvs * sizeof(pal_key_value_pair_t);
    pal_key_vector = (pal_key_vector_t *)(payload +
                                              sizeof(pal_param_payload) +
                                              sizeof(effect_pal_payload_t));

    pal_key_vector->num_tkvs = kvp->num_tkvs;
    memcpy(pal_key_vector->kvp, kvp->kvp,
                        (kvp->num_tkvs * sizeof(pal_key_value_pair_t)));
    ret = pal_stream_set_param(pal_stream_handle, PAL_PARAM_ID_UIEFFECT,
                               pal_payload);
    free(pal_payload);
done:
    return ret;
}

static int send_custom_payload(pal_stream_handle_t *pal_stream_handle,
                              uint32_t tag, pal_effect_custom_payload_t *data,
                              uint32_t custom_data_sz)
{
    int ret = 0;
    pal_param_payload *pal_payload;
    effect_pal_payload_t *effect_payload = NULL;
    uint8_t *payload = NULL;
    pal_effect_custom_payload_t *custom_payload = NULL;
    uint32_t payload_size = 0;
    payload_size = sizeof(pal_param_payload) + sizeof(effect_pal_payload_t) +
                   sizeof(pal_effect_custom_payload_t) + custom_data_sz;

    payload = (uint8_t *) calloc (1, payload_size);
    if (!payload) {
        ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__, payload_size);
        ret = -ENOMEM;
        goto done;
    }

    pal_payload = (pal_param_payload *) payload;
    pal_payload->payload_size = sizeof(effect_pal_payload_t) +
                                  sizeof(pal_effect_custom_payload_t) +
                                  custom_data_sz;

    effect_payload = (effect_pal_payload_t *)(payload + sizeof(pal_param_payload));
    effect_payload->isTKV = PARAM_NONTKV;
    effect_payload->tag = tag;
    effect_payload->payloadSize = sizeof(pal_effect_custom_payload_t) +
                                  custom_data_sz;
    custom_payload = (pal_effect_custom_payload_t *)(payload +
                                                   sizeof(pal_param_payload) +
                                                   sizeof(effect_pal_payload_t));

    custom_payload->paramId = data->paramId;
    memcpy(custom_payload->data, data->data, custom_data_sz);
    ret = pal_stream_set_param(pal_stream_handle, PAL_PARAM_ID_UIEFFECT,
                              pal_payload);
    free(pal_payload);
done:
    return ret;
}

void offload_bassboost_set_mode(struct bass_boost_params *bassboost,
                                int mode)
{
    ALOGVV("%s: mode %d", __func__, mode);
    bassboost->mode = mode;
}

static int bassboost_send_params_pal(eff_mode_t mode, pal_stream_handle_t *pal_stream_handle,
                                  struct bass_boost_params *bassboost,
                                 unsigned param_send_flags)
{
    int ret = 0;
    pal_effect_custom_payload_t *custom_payload = NULL;

    if (!pal_stream_handle) {
        ALOGE("%s: pal stream handle is null.\n", __func__);
        return -EINVAL;
    }
    if (param_send_flags & OFFLOAD_SEND_BASSBOOST_ENABLE_FLAG) {
        uint32_t num_kvs = 1;
        pal_key_vector_t *pal_key_vector = NULL;
        pal_key_vector = (pal_key_vector_t *) calloc(1, sizeof(pal_key_vector_t) +
                                            num_kvs * sizeof(pal_key_value_pair_t));
        if (!pal_key_vector) {
            ALOGE("%s:%d calloc failed for size %zu", __func__, __LINE__,
                 sizeof(pal_key_vector_t) + num_kvs * sizeof(pal_key_value_pair_t));
            ret = -ENOMEM;
            goto done;
        }
        pal_key_vector->num_tkvs = num_kvs;
        pal_key_vector->kvp[0].key= BASS_BOOST_SWITCH;
        pal_key_vector->kvp[0].value = bassboost->enable_flag;

        ret = send_kv_payload(pal_stream_handle, TAG_STREAM_BASS_BOOST, pal_key_vector);
        free(pal_key_vector);
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }

    if (param_send_flags & OFFLOAD_SEND_BASSBOOST_STRENGTH) {
        uint32_t custom_data_sz = BASS_BOOST_STRENGTH_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                           sizeof(pal_effect_custom_payload_t) +
                                           custom_data_sz);
        if (!custom_payload) {
            ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                   custom_data_sz);
            ret = -ENOMEM;
            goto done;
        }
        custom_payload->paramId = PARAM_ID_BASS_BOOST_STRENGTH;
        custom_payload->data[0] = bassboost->strength;
        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_BASS_BOOST,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }

    if (param_send_flags & OFFLOAD_SEND_BASSBOOST_MODE) {
        uint32_t custom_data_sz = BASS_BOOST_STRENGTH_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                           sizeof(pal_effect_custom_payload_t) +
                                           custom_data_sz);
        if (!custom_payload) {
            ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                   custom_data_sz);
            ret = -ENOMEM;
            goto done;
        }
        custom_payload->paramId = PARAM_ID_BASS_BOOST_MODE;
        custom_payload->data[0] = bassboost->strength;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_BASS_BOOST,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }

done:
    return ret;
}

int offload_bassboost_send_params_pal(pal_stream_handle_t *pal_handle,
                                  struct bass_boost_params *bassboost,
                                  unsigned param_send_flags)
{
    return bassboost_send_params_pal(OFFLOAD, pal_handle, bassboost,
                                 param_send_flags);
}

void offload_pbe_set_device(struct pbe_params *pbe,
                            uint32_t device)
{
    ALOGV("%s: device=%d", __func__, device);
    pbe->device = device;
}

void offload_pbe_set_enable_flag(struct pbe_params *pbe,
                                 bool enable)
{
    ALOGV("%s: enable=%d", __func__, enable);
    pbe->enable_flag = enable;
}

int offload_pbe_get_enable_flag(struct pbe_params *pbe)
{
    ALOGV("%s: enabled=%d", __func__, pbe->enable_flag);
    return pbe->enable_flag;
}

static int pbe_send_params_pal(eff_mode_t mode, pal_stream_handle_t *pal_stream_handle,
                            struct pbe_params *pbe,
                            unsigned param_send_flags)
{
    int ret = 0;

    if (!pal_stream_handle) {
        ALOGE("%s: pal stream handle is null.\n", __func__);
        return -EINVAL;
    }

    ALOGV("%s: enabled=%d", __func__, pbe->enable_flag);
    if (param_send_flags & OFFLOAD_SEND_PBE_ENABLE_FLAG) {
        uint32_t num_kvs = 1;
        pal_key_vector_t *pal_key_vector = NULL;

        pal_key_vector = (pal_key_vector_t *) calloc (1, sizeof(pal_key_vector_t) +
                                         num_kvs * sizeof(pal_key_value_pair_t));
        if (!pal_key_vector) {
            ALOGE("%s:%d calloc failed for size %zu", __func__, __LINE__,
                 sizeof(pal_key_vector_t) + num_kvs * sizeof(pal_key_value_pair_t));
            ret = -ENOMEM;
            goto done;
        }

        pal_key_vector->num_tkvs = num_kvs;
        pal_key_vector->kvp[0].key= PBE_SWITCH;
        pal_key_vector->kvp[0].value = pbe->enable_flag;

        ret = send_kv_payload(pal_stream_handle, TAG_STREAM_PBE,
                              pal_key_vector);
        free(pal_key_vector);
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
done:
    return ret;
}

int offload_pbe_send_params_pal(pal_stream_handle_t *pal_handle,
                                  struct pbe_params *pbe,
                                  unsigned param_send_flags)
{
    return pbe_send_params_pal(OFFLOAD, pal_handle, pbe,
                                 param_send_flags);
}

void offload_virtualizer_set_device(struct virtualizer_params *virtualizer,
                                    uint32_t device)
{
    ALOGVV("%s: device=0x%x", __func__, device);
    virtualizer->device = device;
}

void offload_virtualizer_set_enable_flag(struct virtualizer_params *virtualizer,
                                         bool enable)
{
    ALOGVV("%s: enable=%d", __func__, (int)enable);
    virtualizer->enable_flag = enable;

#ifdef DTS_EAGLE
    update_effects_node(PCM_DEV_ID, EFFECT_TYPE_VIRT, EFFECT_ENABLE_PARAM, enable, EFFECT_NO_OP, EFFECT_NO_OP, EFFECT_NO_OP);
#endif
}

int offload_virtualizer_get_enable_flag(struct virtualizer_params *virtualizer)
{
    ALOGVV("%s: enabled %d", __func__, (int)virtualizer->enable_flag);
    return virtualizer->enable_flag;
}

void offload_virtualizer_set_strength(struct virtualizer_params *virtualizer,
                                      int strength)
{
    ALOGVV("%s: strength %d", __func__, strength);
    virtualizer->strength = strength;

#ifdef DTS_EAGLE
    update_effects_node(PCM_DEV_ID, EFFECT_TYPE_VIRT, EFFECT_SET_PARAM, EFFECT_NO_OP, strength, EFFECT_NO_OP, EFFECT_NO_OP);
#endif
}

void offload_virtualizer_set_out_type(struct virtualizer_params *virtualizer,
                                      int out_type)
{
    ALOGVV("%s: out_type %d", __func__, out_type);
    virtualizer->out_type = out_type;
}

void offload_virtualizer_set_gain_adjust(struct virtualizer_params *virtualizer,
                                         int gain_adjust)
{
    ALOGVV("%s: gain %d", __func__, gain_adjust);
    virtualizer->gain_adjust = gain_adjust;
}

static int virtualizer_send_params_pal(eff_mode_t mode, pal_stream_handle_t *pal_stream_handle,
                                    struct virtualizer_params *virtualizer,
                                   unsigned param_send_flags)
{
    int ret = 0;
    pal_effect_custom_payload_t *custom_payload = NULL;

    ALOGV("%s: flags 0x%x", __func__, param_send_flags);
    if (param_send_flags & OFFLOAD_SEND_VIRTUALIZER_ENABLE_FLAG) {
        uint32_t num_kvs = 1;
        pal_key_vector_t *pal_key_vector = NULL;
        pal_key_vector = (pal_key_vector_t *) calloc (1, sizeof(pal_key_vector_t) +
                                         num_kvs * sizeof(pal_key_value_pair_t));
        if (!pal_key_vector) {
            ALOGE("%s:%d calloc failed for size %zu", __func__, __LINE__,
                 sizeof(pal_key_vector_t) + num_kvs * sizeof(pal_key_value_pair_t));
            ret = -ENOMEM;
            goto done;
        }

        pal_key_vector->num_tkvs = num_kvs;
        pal_key_vector->kvp[0].key= VIRTUALIZER_SWITCH;
        pal_key_vector->kvp[0].value =  virtualizer->enable_flag;

        ret = send_kv_payload(pal_stream_handle, TAG_STREAM_VIRTUALIZER,
                              pal_key_vector);
        free(pal_key_vector);
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_VIRTUALIZER_STRENGTH) {
        uint32_t custom_data_sz = VIRTUALIZER_STRENGTH_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                          sizeof(pal_effect_custom_payload_t) +
                                          custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_VIRTUALIZER_STRENGTH;
        custom_payload->data[0] = virtualizer->strength;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_VIRTUALIZER,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_VIRTUALIZER_OUT_TYPE) {
        uint32_t custom_data_sz = VIRTUALIZER_STRENGTH_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                         sizeof(pal_effect_custom_payload_t) +
                                         custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_VIRTUALIZER_OUT_TYPE;
        custom_payload->data[0] = virtualizer->out_type;
        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_VIRTUALIZER,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;

        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_VIRTUALIZER_GAIN_ADJUST) {
        uint32_t custom_data_sz = VIRTUALIZER_STRENGTH_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                                sizeof(pal_effect_custom_payload_t) +
                                                custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_VIRTUALIZER_GAIN_ADJUST;
        custom_payload->data[0] = virtualizer->gain_adjust;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_VIRTUALIZER,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
done:
    return ret;
}

int offload_virtualizer_send_params_pal(pal_stream_handle_t *pal_stream_handle,
                                    struct virtualizer_params *virtualizer,
                                    unsigned param_send_flags)
{
    return virtualizer_send_params_pal(OFFLOAD, pal_stream_handle, virtualizer,
                                   param_send_flags);
}

void offload_eq_set_device(struct eq_params *eq, uint32_t device)
{
    ALOGVV("%s: device 0x%x", __func__, device);
    eq->device = device;
}

void offload_eq_set_enable_flag(struct eq_params *eq, bool enable)
{
    ALOGVV("%s: enable=%d", __func__, (int)enable);
    eq->enable_flag = enable;

#ifdef DTS_EAGLE
    update_effects_node(PCM_DEV_ID, EFFECT_TYPE_EQ, EFFECT_ENABLE_PARAM, enable, EFFECT_NO_OP, EFFECT_NO_OP, EFFECT_NO_OP);
#endif
}

int offload_eq_get_enable_flag(struct eq_params *eq)
{
    ALOGVV("%s: enabled=%d", __func__, (int)eq->enable_flag);
    return eq->enable_flag;
}

void offload_eq_set_preset(struct eq_params *eq, int preset)
{
    ALOGVV("%s: preset %d", __func__, preset);
    eq->config.preset_id = preset;
    eq->config.eq_pregain = Q27_UNITY;
}

void offload_eq_set_bands_level(struct eq_params *eq, int num_bands,
                                const uint16_t *band_freq_list,
                                int *band_gain_list)
{
    int i;
    ALOGVV("%s", __func__);
    eq->config.num_bands = num_bands;
    for (i=0; i<num_bands; i++) {
        eq->per_band_cfg[i].band_idx = i;
        eq->per_band_cfg[i].filter_type = EQ_BAND_BOOST;
        eq->per_band_cfg[i].freq_millihertz = band_freq_list[i] * 1000;
        eq->per_band_cfg[i].gain_millibels = band_gain_list[i] * 100;
        eq->per_band_cfg[i].quality_factor = Q8_UNITY;
#ifdef DTS_EAGLE
        update_effects_node(PCM_DEV_ID, EFFECT_TYPE_EQ, EFFECT_SET_PARAM, EFFECT_NO_OP, EFFECT_NO_OP, i, band_gain_list[i] * 100);
#endif
    }

}

static int eq_send_params_pal(eff_mode_t mode, pal_stream_handle_t *pal_stream_handle, struct eq_params *eq,
                          unsigned param_send_flags)
{
    uint32_t i = 0, index = 0;
    int ret = 0;
    pal_effect_custom_payload_t *custom_payload = NULL;

    if (!pal_stream_handle) {
        ALOGE("%s: pal stream handle is null.\n", __func__);
        return -EINVAL;
    }
    ALOGV("%s: flags 0x%x", __func__, param_send_flags);
    if ((eq->config.preset_id < -1) ||
            ((param_send_flags & OFFLOAD_SEND_EQ_PRESET) && (eq->config.preset_id == -1))) {
        ALOGV("No Valid preset to set");
        return 0;
    }

    if (param_send_flags & OFFLOAD_SEND_EQ_ENABLE_FLAG) {
        uint32_t num_kvs = 1;
        pal_key_vector_t *pal_key_vector = NULL;
        pal_key_vector = (pal_key_vector_t *) calloc (1, sizeof(pal_key_vector_t) +
                                           num_kvs * sizeof(pal_key_value_pair_t));
        if (!pal_key_vector) {
            ALOGE("%s:%d calloc failed for size %zu", __func__, __LINE__,
                 sizeof(pal_key_vector_t) + num_kvs * sizeof(pal_key_value_pair_t));
            ret = -ENOMEM;
            goto done;
        }

        pal_key_vector->num_tkvs = num_kvs;
        pal_key_vector->kvp[0].key= EQUALIZER_SWITCH;
        pal_key_vector->kvp[0].value = eq->enable_flag;
        ret = send_kv_payload(pal_stream_handle, TAG_STREAM_EQUALIZER,
                              pal_key_vector);
        free(pal_key_vector);
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }

    if (param_send_flags & OFFLOAD_SEND_EQ_PRESET) {
        uint32_t custom_data_sz = EQ_CONFIG_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                           sizeof(pal_effect_custom_payload_t) +
                                           custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_EQ_CONFIG;
        custom_payload->data[0] = eq->config.eq_pregain;
        custom_payload->data[1] =
            map_eq_opensl_preset_2_offload_preset[eq->config.preset_id];
        custom_payload->data[2] = 0;    // num_of_band must be 0 for preset

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_EQUALIZER,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }

    if (param_send_flags & OFFLOAD_SEND_EQ_BANDS_LEVEL) {
        uint32_t custom_data_sz = (EQ_CONFIG_PARAM_LEN +
         (eq->config.num_bands * EQ_CONFIG_PER_BAND_PARAM_LEN)) * sizeof(uint32_t);

        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                           sizeof(pal_effect_custom_payload_t) +
                                           custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_EQ_CONFIG;
        index = 0;
        custom_payload->data[index++] = eq->config.eq_pregain;
        custom_payload->data[index++] = CUSTOM_OPENSL_PRESET;
        custom_payload->data[index++] = eq->config.num_bands;
        for (i = 0; i < eq->config.num_bands; i++) {
            custom_payload->data[index++] = eq->per_band_cfg[i].filter_type;
            custom_payload->data[index++] = eq->per_band_cfg[i].freq_millihertz;
            custom_payload->data[index++] = eq->per_band_cfg[i].gain_millibels;
            custom_payload->data[index++] = eq->per_band_cfg[i].quality_factor;
            custom_payload->data[index++] = eq->per_band_cfg[i].band_idx;
        }
        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_EQUALIZER,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
done:
    return ret;
}

int offload_eq_send_params_pal(pal_stream_handle_t *pal_handle, struct eq_params *eq,
                           unsigned param_send_flags)
{
    return eq_send_params_pal(OFFLOAD, pal_handle, eq, param_send_flags);
}

void offload_reverb_set_device(struct reverb_params *reverb, uint32_t device)
{
    ALOGVV("%s: device 0x%x", __func__, device);
    reverb->device = device;
}

void offload_reverb_set_enable_flag(struct reverb_params *reverb, bool enable)
{
    ALOGVV("%s: enable=%d", __func__, (int)enable);
    reverb->enable_flag = enable;
}

int offload_reverb_get_enable_flag(struct reverb_params *reverb)
{
    ALOGVV("%s: enabled=%d", __func__, reverb->enable_flag);
    return reverb->enable_flag;
}

void offload_reverb_set_mode(struct reverb_params *reverb, int mode)
{
    ALOGVV("%s", __func__);
    reverb->mode = mode;
}

void offload_reverb_set_preset(struct reverb_params *reverb, int preset)
{
    ALOGVV("%s: preset %d", __func__, preset);
    if (preset && (preset <= NUM_OSL_REVERB_PRESETS_SUPPORTED))
        reverb->preset = map_reverb_opensl_preset_2_offload_preset[preset-1][1];
}

void offload_reverb_set_wet_mix(struct reverb_params *reverb, int wet_mix)
{
    ALOGVV("%s: wet_mix %d", __func__, wet_mix);
    reverb->wet_mix = wet_mix;
}

void offload_reverb_set_gain_adjust(struct reverb_params *reverb,
                                    int gain_adjust)
{
    ALOGVV("%s: gain %d", __func__, gain_adjust);
    reverb->gain_adjust = gain_adjust;
}

void offload_reverb_set_room_level(struct reverb_params *reverb, int room_level)
{
    ALOGVV("%s: level %d", __func__, room_level);
    reverb->room_level = room_level;
}

void offload_reverb_set_room_hf_level(struct reverb_params *reverb,
                                      int room_hf_level)
{
    ALOGVV("%s: level %d", __func__, room_hf_level);
    reverb->room_hf_level = room_hf_level;
}

void offload_reverb_set_decay_time(struct reverb_params *reverb, int decay_time)
{
    ALOGVV("%s: decay time %d", __func__, decay_time);
    reverb->decay_time = decay_time;
}

void offload_reverb_set_decay_hf_ratio(struct reverb_params *reverb,
                                       int decay_hf_ratio)
{
    ALOGVV("%s: decay_hf_ratio %d", __func__, decay_hf_ratio);
    reverb->decay_hf_ratio = decay_hf_ratio;
}

void offload_reverb_set_reflections_level(struct reverb_params *reverb,
                                          int reflections_level)
{
    ALOGVV("%s: ref level %d", __func__, reflections_level);
    reverb->reflections_level = reflections_level;
}

void offload_reverb_set_reflections_delay(struct reverb_params *reverb,
                                          int reflections_delay)
{
    ALOGVV("%s: ref delay", __func__, reflections_delay);
    reverb->reflections_delay = reflections_delay;
}

void offload_reverb_set_reverb_level(struct reverb_params *reverb,
                                     int reverb_level)
{
    ALOGD("%s: reverb level %d", __func__, reverb_level);
    reverb->level = reverb_level;
}

void offload_reverb_set_delay(struct reverb_params *reverb, int delay)
{
    ALOGVV("%s: delay %d", __func__, delay);
    reverb->delay = delay;
}

void offload_reverb_set_diffusion(struct reverb_params *reverb, int diffusion)
{
    ALOGVV("%s: diffusion %d", __func__, diffusion);
    reverb->diffusion = diffusion;
}

void offload_reverb_set_density(struct reverb_params *reverb, int density)
{
    ALOGVV("%s: density %d", __func__, density);
    reverb->density = density;
}


static int reverb_send_params_pal(eff_mode_t mode, pal_stream_handle_t *pal_stream_handle,
                               struct reverb_params *reverb,
                              unsigned param_send_flags)
{
    int ret = 0;
    pal_effect_custom_payload_t *custom_payload = NULL;

    ALOGV("%s: flags 0x%x", __func__, param_send_flags);

    if (param_send_flags & OFFLOAD_SEND_REVERB_ENABLE_FLAG) {
        uint32_t num_kvs = 1;
        pal_key_vector_t *pal_key_vector = NULL;
        pal_key_vector = (pal_key_vector_t *) calloc (1, sizeof(pal_key_vector_t) +
                                         num_kvs * sizeof(pal_key_value_pair_t));
        if (!pal_key_vector) {
            ALOGE("%s:%d calloc failed for size %zu", __func__, __LINE__,
                 sizeof(pal_key_vector_t) + num_kvs * sizeof(pal_key_value_pair_t));
            ret = -ENOMEM;
            goto done;
        }
        pal_key_vector->num_tkvs = num_kvs;
        pal_key_vector->kvp[0].key= REVERB_SWITCH;
        pal_key_vector->kvp[0].value = reverb->enable_flag;
        ret = send_kv_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              pal_key_vector);
        free(pal_key_vector);
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_MODE) {
        uint32_t custom_data_sz = REVERB_MODE_PARAM_LEN * sizeof(uint32_t);

        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                           sizeof(pal_effect_custom_payload_t) +
                                           custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_REVERB_MODE;
        custom_payload->data[0] = reverb->mode;;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_PRESET) {
        uint32_t custom_data_sz = REVERB_PRESET_PARAM_LEN * sizeof(uint32_t);

        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                           sizeof(pal_effect_custom_payload_t) +
                                           custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_REVERB_PRESET;
        custom_payload->data[0] = reverb->preset;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_WET_MIX) {
        // param_id + actual payload
        uint32_t custom_data_sz = REVERB_WET_MIX_PARAM_LEN * sizeof(uint32_t);

        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                            sizeof(pal_effect_custom_payload_t) +
                                            custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }

        custom_payload->paramId = PARAM_ID_REVERB_WET_MIX;
        custom_payload->data[0] = reverb->wet_mix;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_GAIN_ADJUST) {
        uint32_t custom_data_sz = REVERB_GAIN_ADJUST_PARAM_LEN * sizeof(uint32_t);

        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                            sizeof(pal_effect_custom_payload_t) +
                                            custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }

        custom_payload->paramId = PARAM_ID_REVERB_GAIN_ADJUST;
        custom_payload->data[0] = reverb->gain_adjust;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_ROOM_LEVEL) {
        uint32_t custom_data_sz = REVERB_ROOM_LEVEL_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                            sizeof(pal_effect_custom_payload_t) +
                                            custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_REVERB_ROOM_LEVEL;
        custom_payload->data[0] = reverb->room_level;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_ROOM_HF_LEVEL) {
        uint32_t custom_data_sz = REVERB_ROOM_HF_LEVEL_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                            sizeof(pal_effect_custom_payload_t) +
                                            custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_REVERB_ROOM_HF_LEVEL;
        custom_payload->data[0] = reverb->room_hf_level;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_DECAY_TIME) {
        uint32_t custom_data_sz = REVERB_DECAY_TIME_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                              sizeof(pal_effect_custom_payload_t) +
                                              custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_REVERB_DECAY_TIME;
        custom_payload->data[0] = reverb->decay_time;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_DECAY_HF_RATIO) {
        uint32_t custom_data_sz = REVERB_DECAY_HF_RATIO_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                              sizeof(pal_effect_custom_payload_t) +
                                              custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_REVERB_DECAY_HF_RATIO;
        custom_payload->data[0] = reverb->decay_hf_ratio;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_REFLECTIONS_LEVEL) {
        uint32_t custom_data_sz = REVERB_REFLECTIONS_LEVEL_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                            sizeof(pal_effect_custom_payload_t) +
                                            custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }

        custom_payload->paramId = PARAM_ID_REVERB_REFLECTIONS_LEVEL;
        custom_payload->data[0] = reverb->reflections_level;
        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_REFLECTIONS_DELAY) {
        uint32_t custom_data_sz = REVERB_REFLECTIONS_DELAY_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                             sizeof(pal_effect_custom_payload_t) +
                                             custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }

        custom_payload->paramId = PARAM_ID_REVERB_REFLECTIONS_DELAY;
        custom_payload->data[0] = reverb->reflections_delay;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_LEVEL) {
        uint32_t custom_data_sz = REVERB_LEVEL_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                              sizeof(pal_effect_custom_payload_t) +
                                              custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_REVERB_LEVEL;
        custom_payload->data[0] = reverb->level;
        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }

    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_DELAY) {
        uint32_t custom_data_sz = REVERB_DELAY_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                             sizeof(pal_effect_custom_payload_t) +
                                             custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_REVERB_DELAY;
        custom_payload->data[0] = reverb->delay;
        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_DIFFUSION) {
        uint32_t custom_data_sz = REVERB_DIFFUSION_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                           sizeof(pal_effect_custom_payload_t) +
                                           custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_REVERB_DIFFUSION;
        custom_payload->data[0] = reverb->diffusion;

        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            goto done;
        }
    }
    if (param_send_flags & OFFLOAD_SEND_REVERB_DENSITY) {
        uint32_t custom_data_sz = REVERB_DENSITY_PARAM_LEN * sizeof(uint32_t);
        custom_payload = (pal_effect_custom_payload_t *) calloc (1,
                                           sizeof(pal_effect_custom_payload_t) +
                                           custom_data_sz);
        if (!custom_payload) {
             ALOGE("%s:%d calloc failed for size %d", __func__, __LINE__,
                    custom_data_sz);
             ret = -ENOMEM;
             goto done;
        }
        custom_payload->paramId = PARAM_ID_REVERB_DENSITY;
        custom_payload->data[0] = reverb->density;
        ret = send_custom_payload(pal_stream_handle, TAG_STREAM_REVERB,
                              custom_payload, custom_data_sz);
        free(custom_payload);
        custom_payload = NULL;
        if (ret) {
            ALOGE("%s: pal_stream_set_param failed. ret = %d", __func__, ret);
            return ret;
        }
    }
done:
    return ret;
}

int offload_reverb_send_params_pal(pal_stream_handle_t *pal_stream_handle,
                               struct reverb_params *reverb,
                               unsigned param_send_flags)
{
    return reverb_send_params_pal(OFFLOAD, pal_stream_handle, reverb,
                              param_send_flags);
}


void offload_soft_volume_set_enable(struct soft_volume_params *vol, bool enable)
{
    ALOGV("%s", __func__);
    vol->enable_flag = enable;
}

void offload_soft_volume_set_gain_master(struct soft_volume_params *vol, int gain)
{
    ALOGV("%s", __func__);
    vol->master_gain = gain;
}

void offload_soft_volume_set_gain_2ch(struct soft_volume_params *vol,
                                      int l_gain, int r_gain)
{
    ALOGV("%s", __func__);
    vol->left_gain = l_gain;
    vol->right_gain = r_gain;
}

void offload_transition_soft_volume_set_enable(struct soft_volume_params *vol,
                                               bool enable)
{
    ALOGV("%s", __func__);
    vol->enable_flag = enable;
}

void offload_transition_soft_volume_set_gain_master(struct soft_volume_params *vol,
                                                    int gain)
{
    ALOGV("%s", __func__);
    vol->master_gain = gain;
}

void offload_transition_soft_volume_set_gain_2ch(struct soft_volume_params *vol,
                                                 int l_gain, int r_gain)
{
    ALOGV("%s", __func__);
    vol->left_gain = l_gain;
    vol->right_gain = r_gain;
}
