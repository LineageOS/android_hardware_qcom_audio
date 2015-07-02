/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_ssr"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <cutils/properties.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <cutils/str_parms.h>
#include <cutils/log.h>
#include <pthread.h>
#include <cutils/sched_policy.h>
#include <sys/resource.h>
#include <system/thread_defs.h>

#include "audio_hw.h"
#include "audio_extn.h"
#include "platform.h"
#include "platform_api.h"
#include "surround_filters_interface.h"
#include "surround_rec_interface.h"

#ifdef SSR_ENABLED
#define COEFF_ARRAY_SIZE            4
#define FILT_SIZE                   ((512+1)* 6)  /* # ((FFT bins)/2+1)*numOutputs */
#define SSR_CHANNEL_INPUT_NUM       4
#define SSR_CHANNEL_OUTPUT_NUM      6
#define SSR_PERIOD_COUNT            8
#define SSR_PERIOD_SIZE             240
#define SSR_INPUT_FRAME_SIZE        (SSR_PERIOD_SIZE * SSR_PERIOD_COUNT)

#define NUM_IN_BUFS                 4
#define NUM_OUT_BUFS                4

#define SURROUND_FILE_1R "/system/etc/surround_sound/filter1r.pcm"
#define SURROUND_FILE_2R "/system/etc/surround_sound/filter2r.pcm"
#define SURROUND_FILE_3R "/system/etc/surround_sound/filter3r.pcm"
#define SURROUND_FILE_4R "/system/etc/surround_sound/filter4r.pcm"

#define SURROUND_FILE_1I "/system/etc/surround_sound/filter1i.pcm"
#define SURROUND_FILE_2I "/system/etc/surround_sound/filter2i.pcm"
#define SURROUND_FILE_3I "/system/etc/surround_sound/filter3i.pcm"
#define SURROUND_FILE_4I "/system/etc/surround_sound/filter4i.pcm"

#define LIB_SURROUND_PROC       "libsurround_proc.so"
#define LIB_SURROUND_3MIC_PROC  "libsurround_3mic_proc.so"
#define LIB_DRC                 "libdrc.so"

#define AUDIO_PARAMETER_SSRMODE_ON        "ssrOn"


typedef int  (*surround_filters_init_t)(void *, int, int, Word16 **,
                                        Word16 **, int, int, int, Profiler *);
typedef void (*surround_filters_release_t)(void *);
typedef int  (*surround_filters_set_channel_map_t)(void *, const int *);
typedef void (*surround_filters_intl_process_t)(void *, Word16 *, Word16 *);

typedef const get_param_data_t* (*surround_rec_get_get_param_data_t)(void);
typedef const set_param_data_t* (*surround_rec_get_set_param_data_t)(void);
typedef int (*surround_rec_init_t)(void **, int, int, int, int, const char *);
typedef void (*surround_rec_deinit_t)(void *);
typedef void (*surround_rec_process_t)(void *, const int16_t *, int16_t *);

typedef int (*DRC_init_t)(void **, int, int, const char *);
typedef void (*DRC_deinit_t)(void *);
typedef int (*DRC_process_t)(void *, const int16_t *, int16_t *);

struct pcm_buffer {
    void *data;
    int length;
};

struct pcm_buffer_queue {
    struct pcm_buffer_queue *next;
    struct pcm_buffer buffer;
};

struct ssr_module {
    int                 ssr_3mic;
    int                 num_out_chan;
    FILE                *fp_4ch;
    FILE                *fp_6ch;
    Word16             **real_coeffs;
    Word16             **imag_coeffs;
    void                *surround_obj;
    Word16              *surround_raw_buffer;
    bool                 is_ssr_enabled;
    struct stream_in    *in;
    void                *drc_obj;

    void *surround_filters_handle;
    surround_filters_init_t surround_filters_init;
    surround_filters_release_t surround_filters_release;
    surround_filters_set_channel_map_t surround_filters_set_channel_map;
    surround_filters_intl_process_t surround_filters_intl_process;

    void *surround_rec_handle;
    surround_rec_get_get_param_data_t surround_rec_get_get_param_data;
    surround_rec_get_set_param_data_t surround_rec_get_set_param_data;
    surround_rec_init_t surround_rec_init;
    surround_rec_deinit_t surround_rec_deinit;
    surround_rec_process_t surround_rec_process;

    void *DRC_handle;
    DRC_init_t DRC_init;
    DRC_deinit_t DRC_deinit;
    DRC_process_t DRC_process;

    pthread_t ssr_process_thread;
    bool ssr_process_thread_started;
    bool ssr_process_thread_stop;
    struct pcm_buffer_queue in_buf_nodes[NUM_IN_BUFS];
    struct pcm_buffer_queue out_buf_nodes[NUM_OUT_BUFS];
    void *in_buf_data;
    void *out_buf_data;
    struct pcm_buffer_queue *out_buf_free;
    struct pcm_buffer_queue *out_buf;
    struct pcm_buffer_queue *in_buf_free;
    struct pcm_buffer_queue *in_buf;
    pthread_mutex_t ssr_process_lock;
    pthread_cond_t cond_process;
    pthread_cond_t cond_read;
    bool is_ssr_mode_on;
};

static struct ssr_module ssrmod = {
    .fp_4ch = NULL,
    .fp_6ch = NULL,
    .real_coeffs = NULL,
    .imag_coeffs = NULL,
    .surround_obj = NULL,
    .surround_raw_buffer = NULL,
    .is_ssr_enabled = 0,
    .in = NULL,
    .drc_obj = NULL,

    .surround_filters_handle = NULL,
    .surround_filters_init = NULL,
    .surround_filters_release = NULL,
    .surround_filters_set_channel_map = NULL,
    .surround_filters_intl_process = NULL,

    .surround_rec_handle = NULL,
    .surround_rec_get_get_param_data = NULL,
    .surround_rec_get_set_param_data = NULL,
    .surround_rec_init = NULL,
    .surround_rec_deinit = NULL,
    .surround_rec_process = NULL,

    .DRC_handle = NULL,
    .DRC_init = NULL,
    .DRC_deinit = NULL,
    .DRC_process = NULL,

    .ssr_process_thread_stop = 0,
    .ssr_process_thread_started = 0,
    .in_buf_data = NULL,
    .out_buf_data = NULL,
    .out_buf_free = NULL,
    .out_buf = NULL,
    .in_buf_free = NULL,
    .in_buf = NULL,
    .cond_process = PTHREAD_COND_INITIALIZER,
    .cond_read = PTHREAD_COND_INITIALIZER,
    .ssr_process_lock = PTHREAD_MUTEX_INITIALIZER,
    .is_ssr_mode_on = false,
};

static void *ssr_process_thread(void *context);

/* Use AAC/DTS channel mapping as default channel mapping: C,FL,FR,Ls,Rs,LFE */
static const int chan_map[] = { 1, 2, 4, 3, 0, 5};

/* Rotine to read coeffs from File and updates real and imaginary
   coeff array member variable */
static int32_t ssr_read_coeffs_from_file()
{
    FILE    *flt1r;
    FILE    *flt2r;
    FILE    *flt3r;
    FILE    *flt4r;
    FILE    *flt1i;
    FILE    *flt2i;
    FILE    *flt3i;
    FILE    *flt4i;
    int i;

    if ( (flt1r = fopen(SURROUND_FILE_1R, "rb")) == NULL ) {
        ALOGE("%s: Cannot open filter co-efficient "
              "file %s", __func__, SURROUND_FILE_1R);
        return -EINVAL;
    }

    if ( (flt2r = fopen(SURROUND_FILE_2R, "rb")) == NULL ) {
        ALOGE("%s: Cannot open filter "
              "co-efficient file %s", __func__, SURROUND_FILE_2R);
        return -EINVAL;
    }

    if ( (flt3r = fopen(SURROUND_FILE_3R, "rb")) == NULL ) {
        ALOGE("%s: Cannot open filter "
              "co-efficient file %s", __func__, SURROUND_FILE_3R);
        return  -EINVAL;
    }

    if ( (flt4r = fopen(SURROUND_FILE_4R, "rb")) == NULL ) {
        ALOGE("%s: Cannot open filter "
              "co-efficient file %s", __func__, SURROUND_FILE_4R);
        return  -EINVAL;
    }

    if ( (flt1i = fopen(SURROUND_FILE_1I, "rb")) == NULL ) {
        ALOGE("%s: Cannot open filter "
              "co-efficient file %s", __func__, SURROUND_FILE_1I);
        return -EINVAL;
    }

    if ( (flt2i = fopen(SURROUND_FILE_2I, "rb")) == NULL ) {
        ALOGE("%s: Cannot open filter "
              "co-efficient file %s", __func__, SURROUND_FILE_2I);
        return -EINVAL;
    }

    if ( (flt3i = fopen(SURROUND_FILE_3I, "rb")) == NULL ) {
        ALOGE("%s: Cannot open filter "
              "co-efficient file %s", __func__, SURROUND_FILE_3I);
        return -EINVAL;
    }

    if ( (flt4i = fopen(SURROUND_FILE_4I, "rb")) == NULL ) {
        ALOGE("%s: Cannot open filter "
              "co-efficient file %s", __func__, SURROUND_FILE_4I);
        return -EINVAL;
    }
    ALOGV("%s: readCoeffsFromFile all filter "
          "files opened", __func__);

    for (i=0; i<COEFF_ARRAY_SIZE; i++) {
        ssrmod.real_coeffs[i] = (Word16 *)calloc(FILT_SIZE, sizeof(Word16));
    }
    for (i=0; i<COEFF_ARRAY_SIZE; i++) {
        ssrmod.imag_coeffs[i] = (Word16 *)calloc(FILT_SIZE, sizeof(Word16));
    }

    /* Read real co-efficients */
    if (NULL != ssrmod.real_coeffs[0]) {
        fread(ssrmod.real_coeffs[0], sizeof(int16), FILT_SIZE, flt1r);
    }
    if (NULL != ssrmod.real_coeffs[0]) {
        fread(ssrmod.real_coeffs[1], sizeof(int16), FILT_SIZE, flt2r);
    }
    if (NULL != ssrmod.real_coeffs[0]) {
        fread(ssrmod.real_coeffs[2], sizeof(int16), FILT_SIZE, flt3r);
    }
    if (NULL != ssrmod.real_coeffs[0]) {
        fread(ssrmod.real_coeffs[3], sizeof(int16), FILT_SIZE, flt4r);
    }

    /* read imaginary co-efficients */
    if (NULL != ssrmod.imag_coeffs[0]) {
        fread(ssrmod.imag_coeffs[0], sizeof(int16), FILT_SIZE, flt1i);
    }
    if (NULL != ssrmod.imag_coeffs[0]) {
        fread(ssrmod.imag_coeffs[1], sizeof(int16), FILT_SIZE, flt2i);
    }
    if (NULL != ssrmod.imag_coeffs[0]) {
        fread(ssrmod.imag_coeffs[2], sizeof(int16), FILT_SIZE, flt3i);
    }
    if (NULL != ssrmod.imag_coeffs[0]) {
        fread(ssrmod.imag_coeffs[3], sizeof(int16), FILT_SIZE, flt4i);
    }

    fclose(flt1r);
    fclose(flt2r);
    fclose(flt3r);
    fclose(flt4r);
    fclose(flt1i);
    fclose(flt2i);
    fclose(flt3i);
    fclose(flt4i);

    return 0;
}

static int32_t DRC_init_lib(int num_chan, int sample_rate)
{
    int ret = 0;
    const char *cfgFileName;

    if ( ssrmod.drc_obj ) {
        ALOGE("%s: DRC library is already initialized", __func__);
        return 0;
    }

    ssrmod.DRC_handle = dlopen(LIB_DRC, RTLD_NOW);
    if (ssrmod.DRC_handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s", __func__, LIB_DRC);
        ret = -ENOSYS;
        goto init_fail;
    }

    ALOGV("%s: DLOPEN successful for %s", __func__, LIB_DRC);
    ssrmod.DRC_init = (DRC_init_t)
        dlsym(ssrmod.DRC_handle, "DRC_init");
    ssrmod.DRC_deinit = (DRC_deinit_t)
        dlsym(ssrmod.DRC_handle, "DRC_deinit");
    ssrmod.DRC_process = (DRC_process_t)
        dlsym(ssrmod.DRC_handle, "DRC_process");

    if (!ssrmod.DRC_init ||
        !ssrmod.DRC_deinit ||
        !ssrmod.DRC_process){
        ALOGW("%s: Could not find one of the symbols from %s",
              __func__, LIB_DRC);
        ret = -ENOSYS;
        goto init_fail;
    }

    /* TO DO: different config files for different sample rates */
    if (num_chan == 6) {
        cfgFileName = "/system/etc/drc/drc_cfg_5.1.txt";
    } else if (num_chan == 2) {
        cfgFileName = "/system/etc/drc/drc_cfg_AZ.txt";
    }

    ALOGV("%s: Calling DRC_init: num ch: %d, period: %d, cfg file: %s", __func__, num_chan, SSR_PERIOD_SIZE, cfgFileName);
    ret = ssrmod.DRC_init(&ssrmod.drc_obj, num_chan, SSR_PERIOD_SIZE, cfgFileName);
    if (ret) {
        ALOGE("DRC_init failed with ret:%d",ret);
        ret = -EINVAL;
        goto init_fail;
    }

    return 0;

init_fail:
    if (ssrmod.drc_obj) {
        free(ssrmod.drc_obj);
        ssrmod.drc_obj = NULL;
    }
    if(ssrmod.DRC_handle) {
        dlclose(ssrmod.DRC_handle);
        ssrmod.DRC_handle = NULL;
    }
    return ret;
}

static int32_t ssr_init_surround_sound_3mic_lib(unsigned long buffersize, int num_out_chan, int sample_rate)
{
    int ret = 0;
    const char *cfgFileName = NULL;

    if ( ssrmod.surround_obj ) {
        ALOGE("%s: surround sound library is already initialized", __func__);
        return 0;
    }

    /* Allocate memory for input buffer */
    ssrmod.surround_raw_buffer = (Word16 *) calloc(buffersize,
                                              sizeof(Word16));
    if ( !ssrmod.surround_raw_buffer ) {
       ALOGE("%s: Memory allocation failure. Not able to allocate "
             "memory for surroundInputBuffer", __func__);
       ret = -ENOMEM;
       goto init_fail;
    }

    ssrmod.surround_rec_handle = dlopen(LIB_SURROUND_3MIC_PROC, RTLD_NOW);
    if (ssrmod.surround_rec_handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s", __func__, LIB_SURROUND_3MIC_PROC);
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__, LIB_SURROUND_3MIC_PROC);
        ssrmod.surround_rec_get_get_param_data = (surround_rec_get_get_param_data_t)
        dlsym(ssrmod.surround_rec_handle, "surround_rec_get_get_param_data");

        ssrmod.surround_rec_get_set_param_data = (surround_rec_get_set_param_data_t)
        dlsym(ssrmod.surround_rec_handle, "surround_rec_get_set_param_data");
        ssrmod.surround_rec_init = (surround_rec_init_t)
        dlsym(ssrmod.surround_rec_handle, "surround_rec_init");
        ssrmod.surround_rec_deinit = (surround_rec_deinit_t)
        dlsym(ssrmod.surround_rec_handle, "surround_rec_deinit");
        ssrmod.surround_rec_process = (surround_rec_process_t)
        dlsym(ssrmod.surround_rec_handle, "surround_rec_process");

        if (!ssrmod.surround_rec_get_get_param_data ||
            !ssrmod.surround_rec_get_set_param_data ||
            !ssrmod.surround_rec_init ||
            !ssrmod.surround_rec_deinit ||
            !ssrmod.surround_rec_process){
            ALOGW("%s: Could not find the one of the symbols from %s",
                  __func__, LIB_SURROUND_3MIC_PROC);
            ret = -ENOSYS;
            goto init_fail;
        }
    }

    ssrmod.num_out_chan = num_out_chan;

    if (num_out_chan == 6) {
        cfgFileName = "/system/etc/surround_sound_3mic/surround_sound_rec_5.1.cfg";
    } else if (num_out_chan == 2) {
        cfgFileName = "/system/etc/surround_sound_3mic/surround_sound_rec_AZ.cfg";
    } else {
        ALOGE("%s: No cfg file for num_out_chan: %d", __func__, num_out_chan);
    }

    ALOGV("%s: Calling surround_rec_init: in ch: %d, out ch: %d, period: %d, sample rate: %d, cfg file: %s", __func__, SSR_CHANNEL_INPUT_NUM, num_out_chan, SSR_PERIOD_SIZE, sample_rate, cfgFileName);
    ret = ssrmod.surround_rec_init(&ssrmod.surround_obj,
        SSR_CHANNEL_INPUT_NUM, num_out_chan, SSR_PERIOD_SIZE, sample_rate, cfgFileName);
    if (ret) {
        ALOGE("surround_rec_init failed with ret:%d",ret);
        ret = -EINVAL;
        goto init_fail;
    }

    return 0;

init_fail:
    if (ssrmod.surround_obj) {
        free(ssrmod.surround_obj);
        ssrmod.surround_obj = NULL;
    }
    if (ssrmod.surround_raw_buffer) {
        free(ssrmod.surround_raw_buffer);
        ssrmod.surround_raw_buffer = NULL;
    }
    if(ssrmod.surround_rec_handle) {
        dlclose(ssrmod.surround_rec_handle);
        ssrmod.surround_rec_handle = NULL;
    }
    return ret;
}

static int32_t ssr_init_surround_sound_lib(unsigned long buffersize)
{
    /* sub_woofer channel assignment: default as first
       microphone input channel */
    int sub_woofer = 0;
    /* frequency upper bound for sub_woofer:
       frequency=(low_freq-1)/FFT_SIZE*samplingRate, default as 4 */
    int low_freq = 4;
    /* frequency upper bound for spatial processing:
       frequency=(high_freq-1)/FFT_SIZE*samplingRate, default as 100 */
    int high_freq = 100;
    int i, ret = 0;

    if ( ssrmod.surround_obj ) {
        ALOGE("%s: ola filter library is already initialized", __func__);
        return 0;
    }

    ssrmod.num_out_chan = SSR_CHANNEL_OUTPUT_NUM;

    /* Allocate memory for input buffer */
    ssrmod.surround_raw_buffer = (Word16 *) calloc(buffersize,
                                              sizeof(Word16));
    if ( !ssrmod.surround_raw_buffer ) {
       ALOGE("%s: Memory allocation failure. Not able to allocate "
             "memory for surroundInputBuffer", __func__);
       goto init_fail;
    }

    /* Allocate memory for real and imag coeffs array */
    ssrmod.real_coeffs = (Word16 **) calloc(COEFF_ARRAY_SIZE, sizeof(Word16 *));
    if ( !ssrmod.real_coeffs ) {
        ALOGE("%s: Memory allocation failure during real "
              "Coefficient array", __func__);
        goto init_fail;
    }

    ssrmod.imag_coeffs = (Word16 **) calloc(COEFF_ARRAY_SIZE, sizeof(Word16 *));
    if ( !ssrmod.imag_coeffs ) {
        ALOGE("%s: Memory allocation failure during imaginary "
              "Coefficient array", __func__);
        goto init_fail;
    }

    if( ssr_read_coeffs_from_file() != 0) {
        ALOGE("%s: Error while loading coeffs from file", __func__);
        goto init_fail;
    }

    ssrmod.surround_filters_handle = dlopen(LIB_SURROUND_PROC, RTLD_NOW);
    if (ssrmod.surround_filters_handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s", __func__, LIB_SURROUND_PROC);
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__, LIB_SURROUND_PROC);
        ssrmod.surround_filters_init = (surround_filters_init_t)
        dlsym(ssrmod.surround_filters_handle, "surround_filters_init");

        ssrmod.surround_filters_release = (surround_filters_release_t)
         dlsym(ssrmod.surround_filters_handle, "surround_filters_release");

        ssrmod.surround_filters_set_channel_map = (surround_filters_set_channel_map_t)
         dlsym(ssrmod.surround_filters_handle, "surround_filters_set_channel_map");

        ssrmod.surround_filters_intl_process = (surround_filters_intl_process_t)
        dlsym(ssrmod.surround_filters_handle, "surround_filters_intl_process");

        if (!ssrmod.surround_filters_init ||
            !ssrmod.surround_filters_release ||
            !ssrmod.surround_filters_set_channel_map ||
            !ssrmod.surround_filters_intl_process){
            ALOGW("%s: Could not find the one of the symbols from %s",
                  __func__, LIB_SURROUND_PROC);
            goto init_fail;
        }
    }

    /* calculate the size of data to allocate for surround_obj */
    ret = ssrmod.surround_filters_init(NULL,
                  6, // Num output channel
                  4,     // Num input channel
                  ssrmod.real_coeffs,       // Coeffs hardcoded in header
                  ssrmod.imag_coeffs,       // Coeffs hardcoded in header
                  sub_woofer,
                  low_freq,
                  high_freq,
                  NULL);

    if ( ret > 0 ) {
        ALOGV("%s: Allocating surroundObj size is %d", __func__, ret);
        ssrmod.surround_obj = (void *)malloc(ret);
        if (NULL != ssrmod.surround_obj) {
            memset(ssrmod.surround_obj,0,ret);
            /* initialize after allocating the memory for surround_obj */
            ret = ssrmod.surround_filters_init(ssrmod.surround_obj,
                        6,
                        4,
                        ssrmod.real_coeffs,
                        ssrmod.imag_coeffs,
                        sub_woofer,
                        low_freq,
                        high_freq,
                        NULL);
            if (0 != ret) {
               ALOGE("%s: surround_filters_init failed with ret:%d",__func__, ret);
               ssrmod.surround_filters_release(ssrmod.surround_obj);
               goto init_fail;
            }
        } else {
            ALOGE("%s: Allocationg surround_obj failed", __func__);
            goto init_fail;
        }
    } else {
        ALOGE("%s: surround_filters_init(surround_obj=Null) "
              "failed with ret: %d", __func__, ret);
        goto init_fail;
    }

    (void) ssrmod.surround_filters_set_channel_map(ssrmod.surround_obj, chan_map);

    return 0;

init_fail:
    if (ssrmod.surround_obj) {
        free(ssrmod.surround_obj);
        ssrmod.surround_obj = NULL;
    }
    if (ssrmod.surround_raw_buffer) {
        free(ssrmod.surround_raw_buffer);
        ssrmod.surround_raw_buffer = NULL;
    }
    if (ssrmod.real_coeffs){
        for (i =0; i<COEFF_ARRAY_SIZE; i++ ) {
            if (ssrmod.real_coeffs[i]) {
                free(ssrmod.real_coeffs[i]);
                ssrmod.real_coeffs[i] = NULL;
            }
        }
        free(ssrmod.real_coeffs);
        ssrmod.real_coeffs = NULL;
    }
    if (ssrmod.imag_coeffs){
        for (i =0; i<COEFF_ARRAY_SIZE; i++ ) {
            if (ssrmod.imag_coeffs[i]) {
                free(ssrmod.imag_coeffs[i]);
                ssrmod.imag_coeffs[i] = NULL;
            }
        }
        free(ssrmod.imag_coeffs);
        ssrmod.imag_coeffs = NULL;
    }

    return -ENOMEM;
}

void audio_extn_ssr_update_enabled()
{
    char ssr_enabled[PROPERTY_VALUE_MAX] = "false";

    property_get("ro.qc.sdk.audio.ssr",ssr_enabled,"0");
    if (!strncmp("true", ssr_enabled, 4)) {
        ALOGD("%s: surround sound recording is supported", __func__);
        ssrmod.is_ssr_enabled = true;
    } else {
        ALOGD("%s: surround sound recording is not supported", __func__);
        ssrmod.is_ssr_enabled = false;
    }
}

bool audio_extn_ssr_get_enabled()
{
    ALOGV("%s: is_ssr_enabled:%d is_ssr_mode_on:%d ", __func__, ssrmod.is_ssr_enabled, ssrmod.is_ssr_mode_on);

    if(ssrmod.is_ssr_enabled && ssrmod.is_ssr_mode_on)
        return true;

    return false;
}

static void pcm_buffer_queue_push(struct pcm_buffer_queue **queue,
                                  struct pcm_buffer_queue *node)
{
    struct pcm_buffer_queue *iter;

    node->next = NULL;
    if ((*queue) == NULL) {
        *queue = node;
    } else {
        iter = *queue;
        while (iter->next) {
            iter = iter->next;
        }
        iter->next = node;
    }
}

static struct pcm_buffer_queue *pcm_buffer_queue_pop(struct pcm_buffer_queue **queue)
{
    struct pcm_buffer_queue *node = (*queue);
    if (node != NULL) {
        *queue = node->next;
        node->next = NULL;
    }
    return node;
}

static void deinit_ssr_process_thread()
{
    pthread_mutex_lock(&ssrmod.ssr_process_lock);
    ssrmod.ssr_process_thread_stop = 1;
    free(ssrmod.in_buf_data);
    ssrmod.in_buf_data = NULL;
    ssrmod.in_buf = NULL;
    ssrmod.in_buf_free = NULL;
    free(ssrmod.out_buf_data);
    ssrmod.out_buf_data = NULL;
    ssrmod.out_buf = NULL;
    ssrmod.out_buf_free = NULL;
    pthread_cond_broadcast(&ssrmod.cond_process);
    pthread_cond_broadcast(&ssrmod.cond_read);
    pthread_mutex_unlock(&ssrmod.ssr_process_lock);
    if (ssrmod.ssr_process_thread_started) {
        pthread_join(ssrmod.ssr_process_thread, (void **)NULL);
        ssrmod.ssr_process_thread_started = 0;
    }
}

struct stream_in *audio_extn_ssr_get_stream()
{
    return ssrmod.in;
}

int32_t audio_extn_ssr_init(struct stream_in *in, int num_out_chan)
{
    uint32_t ret;
    char c_multi_ch_dump[128] = {0};
    char c_ssr_3mic[128] = {0};
    uint32_t buffer_size;

    ALOGD("%s: ssr case, sample rate %d", __func__, in->config.rate);

    if (ssrmod.surround_obj != NULL) {
        ALOGV("%s: reinitializing surround sound library", __func__);
        audio_extn_ssr_deinit();
    }

    in->config.channels = SSR_CHANNEL_INPUT_NUM;
    in->config.period_size = SSR_PERIOD_SIZE;
    in->config.period_count = SSR_PERIOD_COUNT;

    /* use 4k hardcoded buffer size for ssr*/
    buffer_size = SSR_INPUT_FRAME_SIZE;
    ALOGV("%s: buffer_size: %d", __func__, buffer_size);

    property_get("persist.audio.ssr.3mic",c_ssr_3mic,"0");
    if (0 == strncmp("true", c_ssr_3mic, sizeof(c_ssr_3mic)-1)) {
        ssrmod.ssr_3mic = 1;
    } else {
        ssrmod.ssr_3mic = 0;
    }

    if (ssrmod.ssr_3mic != 0) {
        ret = ssr_init_surround_sound_3mic_lib(buffer_size, num_out_chan, in->config.rate);
        if (0 != ret) {
            ALOGE("%s: ssr_init_surround_sound_3mic_lib failed: %d  "
                  "buffer_size:%d", __func__, ret, buffer_size);
            goto fail;
        }
    } else {
        ret = ssr_init_surround_sound_lib(buffer_size);
        if (0 != ret) {
            ALOGE("%s: initSurroundSoundLibrary failed: %d  "
                  "handle->bufferSize:%d", __func__, ret, buffer_size);
            goto fail;
        }
    }

    /* Initialize DRC if available */
    ret = DRC_init_lib(num_out_chan, in->config.rate);
    if (0 != ret) {
        ALOGE("%s: DRC_init_lib failed, ret %d", __func__, ret);
    }

    pthread_mutex_lock(&ssrmod.ssr_process_lock);
    if (!ssrmod.ssr_process_thread_started) {
        int i;
        int output_buf_size = SSR_PERIOD_SIZE * sizeof(int16_t) * num_out_chan;

        ssrmod.in_buf_data = (void *)calloc(buffer_size, NUM_IN_BUFS);
        if (ssrmod.in_buf_data == NULL) {
            ALOGE("%s: failed to allocate input buffer", __func__);
            pthread_mutex_unlock(&ssrmod.ssr_process_lock);
            ret = -ENOMEM;
            goto fail;
        }
        ssrmod.out_buf_data = (void *)calloc(output_buf_size, NUM_OUT_BUFS);
        if (ssrmod.out_buf_data == NULL) {
            ALOGE("%s: failed to allocate output buffer", __func__);
            pthread_mutex_unlock(&ssrmod.ssr_process_lock);
            ret = -ENOMEM;
            // ssrmod.in_buf_data will be freed in deinit_ssr_process_thread()
            goto fail;
        }

        ssrmod.in_buf = NULL;
        ssrmod.in_buf_free = NULL;
        ssrmod.out_buf = NULL;
        ssrmod.out_buf_free = NULL;

        for (i=0; i < NUM_IN_BUFS; i++) {
            struct pcm_buffer_queue *buf = &ssrmod.in_buf_nodes[i];
            buf->buffer.data = &(((char *)ssrmod.in_buf_data)[i*buffer_size]);
            buf->buffer.length = buffer_size;
            pcm_buffer_queue_push(&ssrmod.in_buf_free, buf);
        }

        for (i=0; i < NUM_OUT_BUFS; i++) {
            struct pcm_buffer_queue *buf = &ssrmod.out_buf_nodes[i];
            buf->buffer.data = &(((char *)ssrmod.out_buf_data)[i*output_buf_size]);
            buf->buffer.length = output_buf_size;
            pcm_buffer_queue_push(&ssrmod.out_buf, buf);
        }

        ssrmod.ssr_process_thread_stop = 0;
        ALOGV("%s: creating thread", __func__);
        ret = pthread_create(&ssrmod.ssr_process_thread,
                             (const pthread_attr_t *) NULL,
                             ssr_process_thread, NULL);
        if (ret != 0) {
            ALOGE("%s: failed to create thread for surround sound recording.",
                  __func__);
            pthread_mutex_unlock(&ssrmod.ssr_process_lock);
            goto fail;
        }

        ssrmod.ssr_process_thread_started = 1;
        ALOGV("%s: done creating thread", __func__);
    }
    pthread_mutex_unlock(&ssrmod.ssr_process_lock);

    property_get("ssr.pcmdump",c_multi_ch_dump,"0");
    if (0 == strncmp("true", c_multi_ch_dump, sizeof("ssr.dump-pcm"))) {
        /* Remember to change file system permission of data(e.g. chmod 777 data/),
          otherwise, fopen may fail */
        if ( !ssrmod.fp_4ch)
            ssrmod.fp_4ch = fopen("/data/misc/audio/4ch.pcm", "wb");
        if ( !ssrmod.fp_6ch)
            ssrmod.fp_6ch = fopen("/data/misc/audio/6ch.pcm", "wb");
        if ((!ssrmod.fp_4ch) || (!ssrmod.fp_6ch))
            ALOGE("%s: mfp_4ch or mfp_6ch open failed: mfp_4ch:%p mfp_6ch:%p",
                  __func__, ssrmod.fp_4ch, ssrmod.fp_6ch);
    }

    ssrmod.in = in;

    ALOGV("%s: exit", __func__);
    return 0;

fail:
    (void) audio_extn_ssr_deinit();
    return ret;
}

int32_t audio_extn_ssr_deinit()
{
    int i;

    ALOGV("%s: entry", __func__);
    deinit_ssr_process_thread();

    if (ssrmod.drc_obj) {
        ssrmod.DRC_deinit(ssrmod.drc_obj);
        ssrmod.drc_obj = NULL;
    }

    if (ssrmod.surround_obj) {

        if (ssrmod.ssr_3mic) {
            ssrmod.surround_rec_deinit(ssrmod.surround_obj);
            ssrmod.surround_obj = NULL;
        } else {
            ssrmod.surround_filters_release(ssrmod.surround_obj);
            if (ssrmod.surround_obj)
                free(ssrmod.surround_obj);
            ssrmod.surround_obj = NULL;
            if (ssrmod.real_coeffs){
                for (i =0; i<COEFF_ARRAY_SIZE; i++ ) {
                    if (ssrmod.real_coeffs[i]) {
                        free(ssrmod.real_coeffs[i]);
                        ssrmod.real_coeffs[i] = NULL;
                    }
                }
                free(ssrmod.real_coeffs);
                ssrmod.real_coeffs = NULL;
            }
            if (ssrmod.imag_coeffs){
                for (i =0; i<COEFF_ARRAY_SIZE; i++ ) {
                    if (ssrmod.imag_coeffs[i]) {
                        free(ssrmod.imag_coeffs[i]);
                        ssrmod.imag_coeffs[i] = NULL;
                    }
                }
                free(ssrmod.imag_coeffs);
                ssrmod.imag_coeffs = NULL;
            }
        }
        if (ssrmod.surround_raw_buffer) {
            free(ssrmod.surround_raw_buffer);
            ssrmod.surround_raw_buffer = NULL;
        }
        if (ssrmod.fp_4ch)
            fclose(ssrmod.fp_4ch);
        if (ssrmod.fp_6ch)
            fclose(ssrmod.fp_6ch);
    }

    if(ssrmod.DRC_handle) {
        dlclose(ssrmod.DRC_handle);
        ssrmod.DRC_handle = NULL;
    }

    if(ssrmod.surround_rec_handle) {
        dlclose(ssrmod.surround_rec_handle);
        ssrmod.surround_rec_handle = NULL;
    }

    if(ssrmod.surround_filters_handle) {
        dlclose(ssrmod.surround_filters_handle);
        ssrmod.surround_filters_handle = NULL;
    }

    ssrmod.in = NULL;
    ssrmod.is_ssr_mode_on = false;
    ALOGV("%s: exit", __func__);

    return 0;
}

static void *ssr_process_thread(void *context)
{
    int32_t ret;

    ALOGV("%s: enter", __func__);

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_URGENT_AUDIO);
    set_sched_policy(0, SP_FOREGROUND);

    pthread_mutex_lock(&ssrmod.ssr_process_lock);
    while (!ssrmod.ssr_process_thread_stop) {
        struct pcm_buffer_queue *out_buf;
        struct pcm_buffer_queue *in_buf;

        while ((!ssrmod.ssr_process_thread_stop) &&
               ((ssrmod.out_buf_free == NULL) ||
                (ssrmod.in_buf == NULL))) {
            ALOGV("%s: waiting for buffers", __func__);
            pthread_cond_wait(&ssrmod.cond_process, &ssrmod.ssr_process_lock);
        }
        if (ssrmod.ssr_process_thread_stop) {
            break;
        }
        ALOGV("%s: got buffers", __func__);

        out_buf = pcm_buffer_queue_pop(&ssrmod.out_buf_free);
        in_buf = pcm_buffer_queue_pop(&ssrmod.in_buf);

        pthread_mutex_unlock(&ssrmod.ssr_process_lock);

        /* apply ssr libs to convert 4ch to 6ch */
        if (ssrmod.ssr_3mic) {
            ssrmod.surround_rec_process(ssrmod.surround_obj,
                (int16_t *) in_buf->buffer.data,
                (int16_t *) out_buf->buffer.data);
        } else {
            ssrmod.surround_filters_intl_process(ssrmod.surround_obj,
                (uint16_t *) out_buf->buffer.data, in_buf->buffer.data);
        }

        /* Run DRC if initialized */
        if (ssrmod.drc_obj != NULL) {
            ALOGV("%s: Running DRC", __func__);
            ret = ssrmod.DRC_process(ssrmod.drc_obj, out_buf->buffer.data, out_buf->buffer.data);
            if (ret != 0) {
                ALOGE("%s: DRC_process returned %d", __func__, ret);
            }
        }

        /*dump for raw pcm data*/
        if (ssrmod.fp_4ch)
            fwrite(in_buf->buffer.data, 1, in_buf->buffer.length, ssrmod.fp_4ch);
        if (ssrmod.fp_6ch)
            fwrite(out_buf->buffer.data, 1, out_buf->buffer.length, ssrmod.fp_6ch);

        pthread_mutex_lock(&ssrmod.ssr_process_lock);

        pcm_buffer_queue_push(&ssrmod.out_buf, out_buf);
        pcm_buffer_queue_push(&ssrmod.in_buf_free, in_buf);

        /* Read thread should go on without waiting for condition
         * variable. If it has to wait (due to this thread not keeping
         * up with the read requests), let this thread use the remainder
         * of its buffers before waking up the read thread. */
        if (ssrmod.in_buf == NULL) {
            pthread_cond_signal(&ssrmod.cond_read);
        }
    }
    pthread_mutex_unlock(&ssrmod.ssr_process_lock);

    ALOGV("%s: exit", __func__);

    pthread_exit(NULL);
}

int32_t audio_extn_ssr_read(struct audio_stream_in *stream,
                       void *buffer, size_t bytes)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    int32_t ret = 0;
    struct pcm_buffer_queue *in_buf;
    struct pcm_buffer_queue *out_buf;

    ALOGV("%s: entry", __func__);

    if (!ssrmod.surround_obj) {
        ALOGE("%s: surround_obj not initialized", __func__);
        return -ENOMEM;
    }

    ret = pcm_read(in->pcm, ssrmod.surround_raw_buffer, SSR_INPUT_FRAME_SIZE);
    if (ret < 0) {
        ALOGE("%s: %s ret:%d", __func__, pcm_get_error(in->pcm),ret);
        return ret;
    }

    pthread_mutex_lock(&ssrmod.ssr_process_lock);

    if (!ssrmod.ssr_process_thread_started) {
        pthread_mutex_unlock(&ssrmod.ssr_process_lock);
        ALOGV("%s: ssr_process_thread not initialized", __func__);
        return -EINVAL;
    }

    if ((ssrmod.in_buf_free == NULL) || (ssrmod.out_buf == NULL)) {
        ALOGE("%s: waiting for buffers", __func__);
        pthread_cond_wait(&ssrmod.cond_read, &ssrmod.ssr_process_lock);
        if ((ssrmod.in_buf_free == NULL) || (ssrmod.out_buf == NULL)) {
            pthread_mutex_unlock(&ssrmod.ssr_process_lock);
            ALOGE("%s: failed to acquire buffers", __func__);
            return -EINVAL;
        }
    }

    in_buf = pcm_buffer_queue_pop(&ssrmod.in_buf_free);
    out_buf = pcm_buffer_queue_pop(&ssrmod.out_buf);

    memcpy(in_buf->buffer.data, ssrmod.surround_raw_buffer, in_buf->buffer.length);
    pcm_buffer_queue_push(&ssrmod.in_buf, in_buf);

    memcpy(buffer, out_buf->buffer.data, bytes);
    pcm_buffer_queue_push(&ssrmod.out_buf_free, out_buf);

    pthread_cond_signal(&ssrmod.cond_process);

    pthread_mutex_unlock(&ssrmod.ssr_process_lock);

    ALOGV("%s: exit", __func__);
    return ret;
}

void audio_extn_ssr_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms)
{
    int err;
    char value[4096] = {0};

	//Do not update SSR mode during recording
    if ( !ssrmod.surround_obj) {
        int ret = 0;
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_SSRMODE_ON, value,
                                sizeof(value));
        if (ret >= 0) {
            if (strcmp(value, "true") == 0) {
				ALOGD("Received SSR session request..setting SSR mode to true");
                ssrmod.is_ssr_mode_on = true;
            } else {
                ALOGD("resetting SSR mode to false");
                ssrmod.is_ssr_mode_on = false;
            }
        }
    }
    if (ssrmod.ssr_3mic && ssrmod.surround_obj) {
        const set_param_data_t *set_params = ssrmod.surround_rec_get_set_param_data();
        if (set_params != NULL) {
            while (set_params->name != NULL && set_params->set_param_fn != NULL) {
                err = str_parms_get_str(parms, set_params->name, value, sizeof(value));
                if (err >= 0) {
                    ALOGV("Set %s to %s\n", set_params->name, value);
                    set_params->set_param_fn(ssrmod.surround_obj, value);
                }
                set_params++;
            }
        }
    }
}

void audio_extn_ssr_get_parameters(const struct audio_device *adev,
                                   struct str_parms *parms,
                                   struct str_parms *reply)
{
    int err;
    char value[4096] = {0};

    if (ssrmod.ssr_3mic && ssrmod.surround_obj) {
        const get_param_data_t *get_params = ssrmod.surround_rec_get_get_param_data();
        int get_all = 0;
        err = str_parms_get_str(parms, "ssr.all", value, sizeof(value));
        if (err >= 0) {
            get_all = 1;
        }
        if (get_params != NULL) {
            while (get_params->name != NULL && get_params->get_param_fn != NULL) {
                err = str_parms_get_str(parms, get_params->name, value, sizeof(value));
                if (get_all || (err >= 0)) {
                    ALOGV("Getting parameter %s", get_params->name);
                    char *val = get_params->get_param_fn(ssrmod.surround_obj);
                    if (val != NULL) {
                        str_parms_add_str(reply, get_params->name, val);
                        free(val);
                    }
                }
                get_params++;
            }
        }
    }
}

#endif /* SSR_ENABLED */
