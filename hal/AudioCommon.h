/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <log/log.h>

#define AHAL_LOG_ERR             (0x1) /**< error message, represents code bugs that should be debugged and fixed.*/
#define AHAL_LOG_INFO            (0x2) /**< info message, additional info to support debug */
#define AHAL_LOG_DBG             (0x4) /**< debug message, required at minimum for debug.*/
#define AHAL_LOG_VERBOSE         (0x8)/**< verbose message, useful primarily to help developers debug low-level code */

static uint32_t ahal_log_lvl = AHAL_LOG_ERR|AHAL_LOG_INFO|AHAL_LOG_DBG; /*TODO make this dynamic*/


#define AHAL_ERR(arg,...)                                          \
    if (ahal_log_lvl & AHAL_LOG_ERR) {                              \
        ALOGE("%s: %d: "  arg, __func__, __LINE__, ##__VA_ARGS__);\
    }
#define AHAL_DBG(arg,...)                                           \
    if (ahal_log_lvl & AHAL_LOG_DBG) {                               \
        ALOGD("%s: %d: "  arg, __func__, __LINE__, ##__VA_ARGS__); \
    }
#define AHAL_INFO(arg,...)                                         \
    if (ahal_log_lvl & AHAL_LOG_INFO) {                             \
        ALOGI("%s: %d: "  arg, __func__, __LINE__, ##__VA_ARGS__);\
    }
#define AHAL_VERBOSE(arg,...)                                      \
    if (ahal_log_lvl & AHAL_LOG_VERBOSE) {                          \
        ALOGV("%s: %d: "  arg, __func__, __LINE__, ##__VA_ARGS__);\
    }
