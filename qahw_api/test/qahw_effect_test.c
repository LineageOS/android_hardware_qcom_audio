/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

/* effect test to be applied on HAL layer */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "qahw_api.h"
#include "qahw_defs.h"
#include "qahw_effect_api.h"
#include "qahw_effect_bassboost.h"
#include "qahw_effect_environmentalreverb.h"
#include "qahw_effect_equalizer.h"
#include "qahw_effect_presetreverb.h"
#include "qahw_effect_virtualizer.h"
#include "qahw_effect_visualizer.h"

#include "qahw_effect_test.h"

thread_func_t effect_thread_funcs[EFFECT_NUM] = {
    &bassboost_thread_func,
    &virtualizer_thread_func,
    &equalizer_thread_func,
    &visualizer_thread_func,
    &reverb_thread_func,
};

const char * effect_str[EFFECT_NUM] = {
    "bassboost",
    "virtualizer",
    "equalizer",
    "visualizer",
    "reverb",
};

void *bassboost_thread_func(void* data) {
}

void *virtualizer_thread_func(void* data) {
}

void *equalizer_thread_func(void* data) {
    thread_data_t            *thr_ctxt = (thread_data_t *)data;
    qahw_effect_lib_handle_t lib_handle;
    qahw_effect_handle_t     effect_handle;
    qahw_effect_descriptor_t effect_desc;
    int32_t                  rc;
    int                      reply_data;
    uint32_t                 reply_size = sizeof(int);
    uint32_t                 size = (sizeof(qahw_effect_param_t) + 2 * sizeof(int32_t));
    uint32_t                 buf32[size];
    qahw_effect_param_t      *param = (qahw_effect_param_t *)buf32;
    uint32_t                 preset = EQ_PRESET_NORMAL;

    pthread_mutex_lock(&thr_ctxt->mutex);
    while(!thr_ctxt->exit) {
        // suspend thread till signaled
        fprintf(stdout, "suspend effect thread\n");
        pthread_cond_wait(&thr_ctxt->loop_cond, &thr_ctxt->mutex);
        fprintf(stdout, "awake effect thread\n");

        switch(thr_ctxt->cmd) {
        case(EFFECT_LOAD_LIB):
            lib_handle = qahw_effect_load_library(QAHW_EFFECT_EQUALIZER_LIBRARY);
            break;
        case(EFFECT_GET_DESC):
            rc = qahw_effect_get_descriptor(lib_handle, SL_IID_EQUALIZER_UUID, &effect_desc);
            if (rc != 0) {
                fprintf(stderr, "effect_get_descriptor() returns %d\n", rc);
            }
            break;
        case(EFFECT_CREATE):
            rc = qahw_effect_create(lib_handle, SL_IID_EQUALIZER_UUID,
                                    thr_ctxt->io_handle, &effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_create() returns %d\n", rc);
            }
            break;
        case(EFFECT_CMD):
            if ((thr_ctxt->cmd_code == QAHW_EFFECT_CMD_ENABLE) ||
                (thr_ctxt->cmd_code == QAHW_EFFECT_CMD_DISABLE)) {
                thr_ctxt->reply_size = (uint32_t *)&reply_size;
                thr_ctxt->reply_data = (void *)&reply_data;
            } else if (thr_ctxt->cmd_code == QAHW_EFFECT_CMD_SET_PARAM) {
                param->psize = sizeof(int32_t);
                *(int32_t *)param->data = EQ_PARAM_CUR_PRESET;
                param->vsize = sizeof(int32_t);
                memcpy((param->data + param->psize), &preset, param->vsize);

                thr_ctxt->reply_size = (uint32_t *)&reply_size;
                thr_ctxt->reply_data = (void *)&reply_data;
                thr_ctxt->cmd_size = size;
                thr_ctxt->cmd_data = param;
                preset = (preset + 1) % EQ_PRESET_MAX_NUM; // enumerate through all EQ presets
            }
            rc = qahw_effect_command(effect_handle, thr_ctxt->cmd_code,
                                     thr_ctxt->cmd_size, thr_ctxt->cmd_data,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
            if (rc != 0) {
                fprintf(stderr, "effect_command() returns %d\n", rc);
            }
            break;
        case(EFFECT_PROC):
            //qahw_effect_process();
            break;
        case(EFFECT_RELEASE):
            rc = qahw_effect_release(lib_handle, effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_release() returns %d\n", rc);
            }
            break;
        case(EFFECT_UNLOAD_LIB):
            rc = qahw_effect_unload_library(lib_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_unload_library() returns %d\n", rc);
            }
            break;
        }
    }
    pthread_mutex_unlock(&thr_ctxt->mutex);

    return NULL;
}

void *visualizer_thread_func(void* data) {
}

void *reverb_thread_func(void* data) {
}

thread_data_t *create_effect_thread(thread_func_t func_ptr) {
    int result;

    thread_data_t *ethread_data = (thread_data_t *)calloc(1, sizeof(thread_data_t));
    ethread_data->exit = false;

    pthread_attr_init(&ethread_data->attr);
    pthread_attr_setdetachstate(&ethread_data->attr, PTHREAD_CREATE_JOINABLE);
    pthread_mutex_init(&ethread_data->mutex, NULL);
    if (pthread_cond_init(&ethread_data->loop_cond, NULL) != 0) {
        fprintf(stderr, "pthread_cond_init fails\n");
        return NULL;
    }
    // create effect thread
    result = pthread_create(&ethread_data->effect_thread, &ethread_data->attr,
                            func_ptr, ethread_data);

    if (result < 0) {
        fprintf(stderr, "Could not create effect thread!\n");
        return NULL;
    }

    return ethread_data;
}

void effect_thread_command(thread_data_t *ethread_data,
                           int cmd, uint32_t cmd_code,
                           uint32_t cmd_size, void *cmd_data) {
    if (ethread_data == NULL) {
        fprintf(stderr, "invalid thread data\n");
        return;
    }

    // leave interval to let thread consume the previous cond signal
    usleep(500000);

    pthread_mutex_lock(&ethread_data->mutex);
    ethread_data->cmd = cmd;
    if (cmd_code >= 0) {
        ethread_data->cmd_code = cmd_code;
        ethread_data->cmd_size = cmd_size;
        ethread_data->cmd_data = cmd_data;
    }
    pthread_mutex_unlock(&ethread_data->mutex);
    pthread_cond_signal(&ethread_data->loop_cond);

    return;
}

void destroy_effect_thread(thread_data_t *ethread_data) {
    int result;

    if (ethread_data == NULL) {
        fprintf(stderr, "invalid thread data\n");
        return;
    }

    pthread_mutex_lock(&ethread_data->mutex);
    ethread_data->exit = true;
    pthread_mutex_unlock(&ethread_data->mutex);
    pthread_cond_signal(&ethread_data->loop_cond);

    result = pthread_join(ethread_data->effect_thread, NULL);
    if (result < 0) {
        fprintf(stderr, "Fail to join effect thread!\n");
        return;
    }
    pthread_mutex_destroy(&ethread_data->mutex);
    pthread_cond_destroy(&ethread_data->loop_cond);

    return;
}
