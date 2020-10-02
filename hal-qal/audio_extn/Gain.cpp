/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_extn"
#include "QalApi.h"
#include "QalDefs.h"
#include "audio_extn.h"

extern "C" {

__attribute__ ((visibility ("default")))
int audio_hw_get_gain_level_mapping(struct qal_amp_db_and_gain_table *mapping_tbl,
                                      int table_size) {
    int ret = 0;
    size_t payload_size = 0;
    qal_param_gain_lvl_map_t gain_lvl_map;
    gain_lvl_map.mapping_tbl = mapping_tbl;
    gain_lvl_map.table_size  = table_size;
    gain_lvl_map.filled_size = 0;

    ret = qal_get_param(QAL_PARAM_ID_GAIN_LVL_MAP,
            (void **)&gain_lvl_map,
            &payload_size, nullptr);

    if (ret != 0) {
        ALOGE("%s: fail to get QAL_PARAM_ID_GAIN_LVL_MAP %d", __func__, ret);
        gain_lvl_map.filled_size = 0;
    }

    return gain_lvl_map.filled_size;
}

__attribute__ ((visibility ("default")))
bool audio_hw_send_gain_dep_calibration(int level) {
    int32_t ret = 0;
    qal_param_gain_lvl_cal_t gain_lvl_cal;
    gain_lvl_cal.level = level;

    ret = qal_set_param(QAL_PARAM_ID_GAIN_LVL_CAL, (void*)&gain_lvl_cal, sizeof(qal_param_gain_lvl_cal_t));
    if (ret != 0) {
        ALOGE("%s: fail to set QAL_PARAM_ID_GAIN_LVL_CAL %d", __func__, ret);
    }

    return (ret != 0) ? false: true;
}

} /* extern "C" */
