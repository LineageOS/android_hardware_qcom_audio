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

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include "surround_filters_interface.h"

#ifdef SSR_ENABLED
#define COEFF_ARRAY_SIZE      4
#define FILT_SIZE             ((512+1)* 6)  /* # ((FFT bins)/2+1)*numOutputs */
#define SSR_FRAME_SIZE        512
#define SSR_INPUT_FRAME_SIZE  (SSR_FRAME_SIZE * 4)
#define SSR_OUTPUT_FRAME_SIZE (SSR_FRAME_SIZE * 6)
#define SSR_CHANNEL_COUNT     4
#define SSR_PERIOD_SIZE       256
#define SSR_PERIOD_COUNT      8

#define SURROUND_FILE_1R "/system/etc/surround_sound/filter1r.pcm"
#define SURROUND_FILE_2R "/system/etc/surround_sound/filter2r.pcm"
#define SURROUND_FILE_3R "/system/etc/surround_sound/filter3r.pcm"
#define SURROUND_FILE_4R "/system/etc/surround_sound/filter4r.pcm"

#define SURROUND_FILE_1I "/system/etc/surround_sound/filter1i.pcm"
#define SURROUND_FILE_2I "/system/etc/surround_sound/filter2i.pcm"
#define SURROUND_FILE_3I "/system/etc/surround_sound/filter3i.pcm"
#define SURROUND_FILE_4I "/system/etc/surround_sound/filter4i.pcm"
#define AUDIO_PARAMETER_KEY_SSR "ssr"
#define LIB_SURROUND_PROC       "libsurround_proc.so"

typedef int  (*surround_filters_init_t)(void *, int, int, Word16 **,
                                        Word16 **, int, int, int, Profiler *);
typedef void (*surround_filters_release_t)(void *);
typedef int  (*surround_filters_set_channel_map_t)(void *, const int *);
typedef void (*surround_filters_intl_process_t)(void *, Word16 *, Word16 *);

struct ssr_module {
    FILE                *fp_4ch;
    FILE                *fp_6ch;
    int16_t             **real_coeffs;
    int16_t             **imag_coeffs;
    void                *surround_obj;

    int16_t             *surround_input_buffer;
    int16_t             *surround_output_buffer;
    int                 surround_input_bufferIdx;
    int                 surround_output_bufferIdx;
    bool                is_ssr_enabled;

    void *surround_filters_handle;
    surround_filters_init_t surround_filters_init;
    surround_filters_release_t surround_filters_release;
    surround_filters_set_channel_map_t surround_filters_set_channel_map;
    surround_filters_intl_process_t surround_filters_intl_process;
};

static int32_t ssr_init_surround_sound_lib(unsigned long buffersize);
static int32_t ssr_read_coeffs_from_file();

static struct ssr_module ssrmod = {
    .fp_4ch = NULL,
    .fp_6ch= NULL,
    .real_coeffs = NULL,
    .imag_coeffs = NULL,
    .surround_obj = NULL,
    .surround_output_buffer = NULL,
    .surround_input_buffer = NULL,
    .surround_output_bufferIdx = 0,
    .surround_input_bufferIdx= 0,
    .is_ssr_enabled = 0,

    .surround_filters_handle = NULL,
    .surround_filters_init = NULL,
    .surround_filters_release = NULL,
    .surround_filters_set_channel_map = NULL,
    .surround_filters_intl_process = NULL,
};

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

    ssrmod.surround_input_bufferIdx = 0;
    ssrmod.surround_output_bufferIdx = 0;

    if ( ssrmod.surround_obj ) {
        ALOGE("%s: ola filter library is already initialized", __func__);
        return 0;
    }

    /* Allocate memory for input buffer */
    ssrmod.surround_input_buffer = (Word16 *) calloc(2 * SSR_INPUT_FRAME_SIZE,
                                              sizeof(Word16));
    if ( !ssrmod.surround_input_buffer ) {
       ALOGE("%s: Memory allocation failure. Not able to allocate "
             "memory for surroundInputBuffer", __func__);
       goto init_fail;
    }

    /* Allocate memory for output buffer */
    ssrmod.surround_output_buffer = (Word16 *) calloc(2 * SSR_OUTPUT_FRAME_SIZE,
                                               sizeof(Word16));
    if ( !ssrmod.surround_output_buffer ) {
       ALOGE("%s: Memory allocation failure. Not able to "
             "allocate memory for surroundOutputBuffer", __func__);
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
        memset(ssrmod.surround_obj,0,ret);
        if (NULL != ssrmod.surround_obj) {
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
    if (ssrmod.surround_output_buffer) {
        free(ssrmod.surround_output_buffer);
        ssrmod.surround_output_buffer = NULL;
    }
    if (ssrmod.surround_input_buffer) {
        free(ssrmod.surround_input_buffer);
        ssrmod.surround_input_buffer = NULL;
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

int32_t audio_extn_ssr_update_enabled(struct audio_device *adev)
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
    return 0;
}

bool audio_extn_ssr_get_enabled()
{
    ALOGV("%s: is_ssr_enabled:%d", __func__, ssrmod.is_ssr_enabled);
    return (ssrmod.is_ssr_enabled ? true: false);
}

int32_t audio_extn_ssr_init(struct audio_device *adev,
                            struct stream_in *in)
{
    uint32_t ret;
    char c_multi_ch_dump[128] = {0};
    uint32_t buffer_size;

    ALOGD("%s: ssr case ", __func__);
    in->config.channels = SSR_CHANNEL_COUNT;
    in->config.period_size = SSR_PERIOD_SIZE;
    in->config.period_count = SSR_PERIOD_COUNT;

    buffer_size = (SSR_PERIOD_SIZE)*(SSR_PERIOD_COUNT);
    ALOGD("%s: buffer_size: %d", __func__, buffer_size);

    ret = ssr_init_surround_sound_lib(buffer_size);
    if (0 != ret) {
        ALOGE("%s: initSurroundSoundLibrary failed: %d  "
              "handle->bufferSize:%d", __func__, ret, buffer_size);
        return ret;
    }

    property_get("ssr.pcmdump",c_multi_ch_dump,"0");
    if (0 == strncmp("true",c_multi_ch_dump, sizeof("ssr.dump-pcm"))) {
    /* Remember to change file system permission of data(e.g. chmod 777 data/),
      otherwise, fopen may fail */
    if ( !ssrmod.fp_4ch)
    ssrmod.fp_4ch = fopen("/data/media/0/4ch_ssr.pcm", "wb");
    if ( !ssrmod.fp_6ch)
    ssrmod.fp_6ch = fopen("/data/media/0/6ch_ssr.pcm", "wb");
    if ((!ssrmod.fp_4ch) || (!ssrmod.fp_6ch))
        ALOGE("%s: mfp_4ch or mfp_6ch open failed: mfp_4ch:%p mfp_6ch:%p",
              __func__, ssrmod.fp_4ch, ssrmod.fp_6ch);
    }

    return 0;
}

int32_t audio_extn_ssr_deinit()
{
    int i;

    if (ssrmod.surround_obj) {
        ALOGD("%s: entry", __func__);
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
        if (ssrmod.surround_output_buffer){
            free(ssrmod.surround_output_buffer);
            ssrmod.surround_output_buffer = NULL;
        }
        if (ssrmod.surround_input_buffer) {
            free(ssrmod.surround_input_buffer);
            ssrmod.surround_input_buffer = NULL;
        }

        if ( ssrmod.fp_4ch ) fclose(ssrmod.fp_4ch);
        if ( ssrmod.fp_6ch ) fclose(ssrmod.fp_6ch);
    }

    if(ssrmod.surround_filters_handle)
    {
        dlclose(ssrmod.surround_filters_handle);
        ssrmod.surround_filters_handle = NULL;
    }
    ALOGD("%s: exit", __func__);

    return 0;
}

int32_t audio_extn_ssr_read(struct audio_stream_in *stream,
                       void *buffer, size_t bytes)
{
    int processed = 0;
    int processed_pending;
    void *buffer_start = buffer;
    unsigned period_bytes;
    unsigned period_samples;
    int read_pending, n;
    size_t read_bytes = 0;
    int samples = bytes >> 1;

    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;

    period_bytes = in->config.period_size;
    ALOGD("%s: period_size: %d", __func__, in->config.period_size);
    period_samples = period_bytes >> 1;

    if (!ssrmod.surround_obj)
        return -ENOMEM;

    do {
            if (ssrmod.surround_output_bufferIdx > 0) {
                ALOGV("%s: copy processed output "
                     "to buffer, surround_output_bufferIdx = %d",
                     __func__, ssrmod.surround_output_bufferIdx);
                /* Copy processed output to buffer */
                processed_pending = ssrmod.surround_output_bufferIdx;
                if (processed_pending > (samples - processed)) {
                    processed_pending = (samples - processed);
                }
                memcpy(buffer, ssrmod.surround_output_buffer, processed_pending * sizeof(Word16));
                buffer = (char*)buffer + processed_pending * sizeof(Word16);
                processed += processed_pending;
                if (ssrmod.surround_output_bufferIdx > processed_pending) {
                    /* Shift leftover samples to beginning of the buffer */
                    memcpy(&ssrmod.surround_output_buffer[0],
                           &ssrmod.surround_output_buffer[processed_pending],
                           (ssrmod.surround_output_bufferIdx - processed_pending) * sizeof(Word16));
                }
                ssrmod.surround_output_bufferIdx -= processed_pending;
            }

            if (processed >= samples) {
                ALOGV("%s: done processing buffer, "
                     "processed = %d", __func__, processed);
                /* Done processing this buffer */
                break;
            }

            /* Fill input buffer until there is enough to process */
            read_pending = SSR_INPUT_FRAME_SIZE - ssrmod.surround_input_bufferIdx;
            read_bytes = ssrmod.surround_input_bufferIdx;
            while (in->pcm && read_pending > 0) {
                n = pcm_read(in->pcm, &ssrmod.surround_input_buffer[read_bytes],
                             period_bytes);
                ALOGV("%s: pcm_read() returned n = %d buffer:%p size:%d", __func__,
                    n, &ssrmod.surround_input_buffer[read_bytes], period_bytes);
                if (n && n != -EAGAIN) {
                    /* Recovery part of pcm_read. TODO:split recovery */
                    return (ssize_t)n;
                }
                else if (n < 0) {
                    /* Recovery is part of pcm_write. TODO split is later */
                    return (ssize_t)n;
                }
                else {
                    read_pending -= period_samples;
                    read_bytes += period_samples;
                }
            }


            if (ssrmod.fp_4ch) {
                fwrite( ssrmod.surround_input_buffer, 1,
                        SSR_INPUT_FRAME_SIZE * sizeof(Word16), ssrmod.fp_4ch);
            }

            /* apply ssr libs to conver 4ch to 6ch */
            ssrmod.surround_filters_intl_process(ssrmod.surround_obj,
                &ssrmod.surround_output_buffer[ssrmod.surround_output_bufferIdx],
                (Word16 *)ssrmod.surround_input_buffer);

            /* Shift leftover samples to beginning of input buffer */
            if (read_pending < 0) {
                memcpy(&ssrmod.surround_input_buffer[0],
                       &ssrmod.surround_input_buffer[SSR_INPUT_FRAME_SIZE],
                       (-read_pending) * sizeof(Word16));
            }
            ssrmod.surround_input_bufferIdx = -read_pending;

            if (ssrmod.fp_6ch) {
                fwrite( &ssrmod.surround_output_buffer[ssrmod.surround_output_bufferIdx],
                        1, SSR_OUTPUT_FRAME_SIZE * sizeof(Word16), ssrmod.fp_6ch);
            }

            ssrmod.surround_output_bufferIdx += SSR_OUTPUT_FRAME_SIZE;
            ALOGV("%s: do_while loop: processed=%d, samples=%d\n", __func__, processed, samples);
        } while (in->pcm && processed < samples);
        read_bytes = processed * sizeof(Word16);
        buffer = buffer_start;

    return 0;
}

void audio_extn_ssr_get_parameters(struct str_parms *query,
                                   struct str_parms *reply)
{
    int ret, val;
    char value[32]={0};

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_SSR, value, sizeof(value));

    if (ret >= 0) {
        memcpy(value, "true", 4);
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_SSR, value);
    }
}
#endif /* SSR_ENABLED */
