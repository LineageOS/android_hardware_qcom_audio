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

#define LOG_TAG "audio_feature_manager"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <cutils/properties.h>
#include <log/log.h>
#include <unistd.h>
#include "audio_feature_manager.h"
#include <cutils/str_parms.h>

static bool feature_bit_map[MAX_SUPPORTED_FEATURE] = {0};

static void set_default_feature_flags() {
    ALOGI(":: %s: Enter", __func__);
    feature_bit_map[SND_MONITOR] = true;
}

static void set_dynamic_feature_flags() {
    ALOGI(":: %s: Enter", __func__);
    // TBD: Dynamically init feature bit
}

static void set_feature_flags() {
    ALOGI(":: %s: Enter", __func__);
    set_default_feature_flags();
    set_dynamic_feature_flags();
}

void audio_feature_manager_init() {
    ALOGI(":: %s: Enter", __func__);
    set_feature_flags();
}

bool audio_feature_manager_is_feature_enabled(audio_ext_feature feature) {
    bool ret_val = false;

    if (feature >= 0 && feature < MAX_SUPPORTED_FEATURE)
        ret_val = feature_bit_map[feature];

    return ret_val;
}
