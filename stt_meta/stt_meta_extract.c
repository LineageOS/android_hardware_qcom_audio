/*
* Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/* Test app to capture STT data using ctls from kernel */

#include <tinyalsa/asoundlib.h>
#include <errno.h>
#include <math.h>
#include "stt_meta_extract.h"
#include <log/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#undef LOG_TAG
#define LOG_TAG "stt_meta"
#undef LOG_NDEBUG
/*#define LOG_NDDEBUG 0*/
/*#define LOG_NDEBUG 0*/

#define nullptr NULL

static int get_sourcetrack_metadata(void *payload, unsigned int meta_size,
                                                  struct mixer_ctl *ctl) {

    int ret = 0;
    unsigned int count;

    if (!ctl || !payload) {
        ALOGE("%s: Invalid params, ctl / source track payload is NULL", __func__);
        return -EINVAL;
    }

    ALOGD("%s from mixer", __func__);
    mixer_ctl_update(ctl);

    count = mixer_ctl_get_num_values(ctl);

    if (count != meta_size) {
        ALOGE("%s: mixer_ctl_get_num_values() invalid source tracking data size %d",
                                                                    __func__, count);
        ret = -EINVAL;
        goto done;
    }

    ret = mixer_ctl_get_array(ctl, payload, count);

    if (ret != 0) {
        ALOGE("%s: mixer_ctl_get_array() failed to get Source Tracking Params", __func__);
        ret = -EINVAL;
        goto done;
    }

done:
    ALOGD("%s exit with %d", __func__, ret);
    return ret;
}

static int get_soundfocus_metadata(struct sound_focus_meta *sound_focus_meta,
                                                     struct mixer_ctl *ctl) {
    int ret = 0, count;
    struct timespec ts;

    if (!ctl) {
        ALOGE("%s: not a valid ctrl", __func__);
        return -EINVAL;
    } else {
        ALOGD("%s: from mixer", __func__);
        mixer_ctl_update(ctl);
        count = mixer_ctl_get_num_values(ctl);

        if (count != (sizeof(struct sound_focus_meta) - sizeof(ts))) {
            ALOGE("%s: mixer_ctl_get_num_values() invalid sound focus data size %d",
                                                                           __func__, count);
            ret = -EINVAL;
            goto done;
        }

        ret = mixer_ctl_get_array(ctl, (void *)sound_focus_meta, count);
        if (ret != 0) {
            ALOGE("%s: mixer_ctl_get_array() failed to get Sound Focus Params", __func__);
            ret = -EINVAL;
            goto done;
        }

        clock_gettime(CLOCK_MONOTONIC, &ts);
        sound_focus_meta->ts = ts;
    }

done:
    ALOGD("%s exit with %d", __func__, ret);
    return ret;
}

static int set_soundfocus_metadata(struct sound_focus_meta *sound_focus_meta,
                                                     struct mixer_ctl *ctl) {
    int ret = 0, count;
    struct timespec ts;

    if (!ctl) {
        ALOGE("%s: not a valid ctrl", __func__);
        return -EINVAL;
    } else {
        ALOGD("%s: Setting Sound Focus Params func", __func__);
        mixer_ctl_update(ctl);
        count = mixer_ctl_get_num_values(ctl);

        if (count != (sizeof(struct sound_focus_meta) - sizeof(ts))) {
            ALOGE("%s: mixer_ctl_get_num_values() invalid sound focus data size %d",
                                                                           __func__, count);
            ret = -EINVAL;
            goto done;
        }

        ret = mixer_ctl_set_array(ctl, (void *)sound_focus_meta, count);
        if (ret != 0) {
            ALOGE("%s: mixer_ctl_set_array() failed to set Sound Focus Params", __func__);
            ret = -EINVAL;
            goto done;
        }
    }

done:
    ALOGD("%s exit with %d", __func__, ret);
    return ret;
}

static void updatestt_data(char* str, struct sound_focus_meta *sound_focus_meta) {
    int str_len = strlen(str);
    int j = 0, i;
    int *arr=(int*)malloc(str_len*sizeof(int));

    for (i = 0; str[i] != '\0'; i++) {
        if (str[i] == ',')
            j++;
        else
            arr[j] = arr[j] * DECI + (str[i] - ASCI_NUM);
    }
    j=0;
    for (i = 0; i < NUM_SECTORS; i++) {
        sound_focus_meta->start_angle[i] = arr[j++];
    }
    for (i = 0; i < NUM_SECTORS; i++) {
        sound_focus_meta->enable[i] = arr[j++];
    }
    sound_focus_meta->gain_step = arr[j];
}

static void usage() {
    printf(" \n Command \n");
    printf(" \n stt_meta_extract <options>\n");
    printf(" \n Options\n");
    printf(" -t  --source_track_data         - get source tracking params data with recording\n");
    printf(" -f  --sound_focus_data          - get sound focus params data with recording\n");
    printf(" -s  --sound focus set           - set sound focus params data with recording\n\n");
    printf(" -g  --meta-time-gap             - time between successive get data in msec\n\n");
    printf(" -n  --get-data-iterations       - get iterations cnt, (-ve) for cont get data\n\n");
    printf(" -h  --help                      - Show this help\n\n");
    printf(" \n Examples \n");
    printf(" stt_meta_extract      -> Get source track meta data while record in progress\n\n");
    printf(" stt_meta_extract -t 1 -> Get source track meta data while record in progress\n\n");
    printf(" stt_meta_extract -f 1 -> Get sound focus meta data while record in progress\n\n");
    printf(" stt_meta_extract -s 45,110,235,310,1,0,0,1,50 -> Set sound focus param data\n");
    printf("                                    secort_startangles[4],secotr_enable[4],gain \n\n");
    printf(" stt_meta_extract -g 200 -n 5 ->    Get stt meta data 5 times for every 200msec\n\n");
    printf(" stt_meta_extract -b TX_CDC_DMA_TX_3 -> Get stt meta with be TX_CDC_DMA_TX_3 \n\n");
}

static int derive_mixer_ctl_stt(struct mixer **stt_mixer, struct mixer_ctl **ctl_st, struct mixer_ctl **ctl_sf,
                                 struct mixer_ctl **ctl_st_fnn, char* be_intf, enum fluence_version fversion) {

    char sound_focus_mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = "Sound Focus Audio Tx ";
    char source_tracking_mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = "Source Tracking Audio Tx ";
    char st_fnn_mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = "FNN STM Audio Tx ";
    struct mixer *mixer = NULL;
    int ret = 0, retry_num = 0;
    struct mixer_ctl *ctl = NULL;

    if (!stt_mixer) {
        ALOGE("%s: invalid mixer", __func__);
        return -EINVAL;
    }

    if (!ctl_st) {
        ALOGE("%s: invalid Source tracking mixer ctl", __func__);
        return -EINVAL;
    }

    if (!ctl_sf) {
        ALOGE("%s: invalid Sound focus mixer ctl", __func__);
        return -EINVAL;
    }

    if (!ctl_st_fnn) {
        ALOGE("%s: invalid Sound tracking mixer ctl for fnn", __func__);
        return -EINVAL;
    }

    strlcat(sound_focus_mixer_ctl_name, be_intf, MIXER_PATH_MAX_LENGTH);
    strlcat(source_tracking_mixer_ctl_name, be_intf, MIXER_PATH_MAX_LENGTH);
    strlcat(st_fnn_mixer_ctl_name, be_intf, MIXER_PATH_MAX_LENGTH);

    mixer = mixer_open(SOUND_CARD);

    while (!mixer && retry_num < MIXER_OPEN_MAX_NUM_RETRY) {
        usleep(RETRY_US);
        mixer = mixer_open(SOUND_CARD);
        retry_num++;
    }

    if (!mixer) {
        ALOGE("%s: ERROR. Unable to open the mixer, aborting", __func__);
        ret = -EINVAL;
        goto clean;
    } else {
        *stt_mixer = mixer;
    }

    switch (fversion) {
        case FV_11:
        {
            ctl = mixer_get_ctl_by_name(mixer, source_tracking_mixer_ctl_name);

            if (!ctl) {
                ALOGE("%s: Could not get ctl for mixer cmd - %s",
                        __func__, source_tracking_mixer_ctl_name);
                ret = -EINVAL;
                goto clean;
            } else {
                *ctl_st = ctl;
            }

            ctl = mixer_get_ctl_by_name(mixer, sound_focus_mixer_ctl_name);
            if (!ctl) {
                ALOGE("%s: Could not get ctl for mixer cmd - %s",
                        __func__, source_tracking_mixer_ctl_name);
                ret = -EINVAL;
                goto clean;
            } else {
                *ctl_sf = ctl;
            }
            break;
        }
        case FV_13:
        {
            ctl = mixer_get_ctl_by_name(mixer, st_fnn_mixer_ctl_name);
            if (!ctl) {
                ALOGE("%s: Could not get ctl for mixer cmd - %s",
                        __func__, st_fnn_mixer_ctl_name);
                ret = -EINVAL;
                goto clean;
            } else {
                *ctl_st_fnn = ctl;
            }
            break;
        }
        default:
            ALOGE("%s: invalid fluence type", __func__);
            break;
    }

    return ret;

clean:
    if (mixer)
        mixer_close(mixer);

    return ret;
}

int main(int argc, char* argv[]) {
    int get_data_iter = 1, get_data_time_gap = RETRY_US, idx, count, sect, ret = 0;
    bool is_source_track_get = true, is_sound_focus_get = false, is_sound_focus_set = false;
    char *be_intf = "TX_CDC_DMA_TX_3";
    FILE * log_file = NULL;
    const char *log_filename = NULL;
    char fluence_property[PROPERTY_VALUE_MAX];
    enum fluence_version fv;
    struct timespec ts;

    struct sound_focus_meta sound_focus_metadataset;
    struct source_track_meta source_track_metadata;
    struct sound_focus_meta sound_focus_metadata;
    struct source_track_meta_fnn source_track_metadata_fnn;
    /* Open mixer for snd card 0 */
    struct mixer *stt_mixer = NULL;
    struct mixer_ctl *ctl_st = NULL, *ctl_sf = NULL, *ctl_st_fnn = NULL;

    struct option long_options[] = {
        {"meta-time-gap",       required_argument,    0, 'g'},    // time-gap between two meta data
        {"get-data-iterations", required_argument,    0, 'n'},    // number of meta data events
        {"source_track_data",   required_argument,    0, 't'},    // Extract Source track meta data
        {"sound_focus_data",    required_argument,    0, 'f'},    // Extract Sound focus meta data
        {"sound focus set",     required_argument,    0, 's'},   // Set Sound focus meta data
        {"audio be interface",  required_argument,    0, 'b'},   // update audio back end interface
        {"log file_name",       required_argument,    0, 'l'},   // update log file name
        {"help",                no_argument,          0, 'h'}
    };

    int opt = 0;
    int option_index = 0;
    char *str;
    log_file = stdout;

    memset(&sound_focus_metadataset, 0x0, sizeof(struct sound_focus_meta));
    memset(&source_track_metadata_fnn, 0xFF, sizeof(struct source_track_meta_fnn));
    memset(&source_track_metadata, 0xFF, sizeof(struct source_track_meta));
    memset(&sound_focus_metadata, 0x0, sizeof(struct sound_focus_meta));

    while ((opt = getopt_long(argc,
                              argv,
                              "-g:n:t:f:s:b:l:h:",
                              long_options,
                              &option_index)) != -1) {
        printf("for argument %c, value is %s\n", opt, optarg);

        switch (opt) {
        case 'g':
            get_data_time_gap = NSEC_MSEC_CONVERT * atoi(optarg);
            break;
        case 'n':
            get_data_iter = atoi(optarg);     /* -ve value for cont get data, +ve for iterations */
            break;
        case 't':
            is_source_track_get = atoi(optarg);
            break;
        case 'f':
            is_sound_focus_get = atoi(optarg);
            break;
        case 's':
            str = optarg;
            updatestt_data(str, &sound_focus_metadataset);
            is_sound_focus_set = true;
            break;
        case 'b':
            be_intf = optarg;
            break;
        case 'l':
            log_filename = optarg;
            if (strcasecmp(log_filename, "stdout") &&
                strcasecmp(log_filename, "1") &&
                (log_file = fopen(log_filename,"wb")) == NULL) {
                fprintf(log_file, "Cannot open log file %s\n", log_filename);
                fprintf(stderr, "Cannot open log file %s\n", log_filename);
                /* continue to log to std out. */
                log_file = stdout;
            }
            break;
        case 'h':
            usage();
            return 0;
            break;
        default:
            usage();
            return 0;
        }
    }

    printf("STT GET META \n");

    property_get("ro.vendor.audio.sdk.fluencetype", fluence_property, NULL);

    if (property_get_bool("ro.vendor.audio.sdk.fluence.nn.enabled",false)) {
        if((!strncmp("fluencenn", fluence_property, sizeof("fluencenn"))) ||
                       (!strncmp("fluence", fluence_property, sizeof("fluence"))))
            fv = FV_13;
        else
            fv = FV_11;
    }
    else {
        fv = FV_11;
    }

    ret = derive_mixer_ctl_stt(&stt_mixer, &ctl_st, &ctl_sf, &ctl_st_fnn, be_intf, fv);

    if (ret != 0) {
        printf("failed to derive mixer controls %d", ret);
        goto done;
    }

    fprintf(log_file, "get_source_track meta data with gap of %d us \n", get_data_time_gap);

    if (is_sound_focus_set) {
        if (fv != FV_11) {
            printf("sound_focus set is not supported for fluence version %d ", fv);
            ret = -1;
            goto done;
        }

        ret = set_soundfocus_metadata(&sound_focus_metadataset, ctl_sf);

        if (ret != 0) {
            printf("failed to set soundfocus metadata %d", ret);
            goto done;
        }

        ret = get_soundfocus_metadata(&sound_focus_metadata, ctl_sf);

        if (ret != 0) {
            printf("failed to get soundfocus metadata %d", ret);
            goto done;
        }

    }

    while (get_data_iter != 0) {
        switch (fv) {
            case FV_13:
            {
                if (is_source_track_get) {
                    ret = get_sourcetrack_metadata((void *)&source_track_metadata_fnn,
                          sizeof(struct source_track_meta_fnn), ctl_st_fnn);
                    if (ret != 0) {
                        printf("failed to get source track meta data %d", ret);
                        goto done;
                    }

                }

                /* Print meta data */
                fprintf(log_file, "time stamp session_time_lsw %u session_time_msw %u \n",
                source_track_metadata_fnn.session_time_lsw,source_track_metadata_fnn.session_time_msw);

                for (idx = 0; idx < TOTAL_SPEAKERS; idx++)
                    printf("speakers[%d]=%d ",idx, source_track_metadata_fnn.speakers[idx]);

                fprintf(log_file, "\nreserved=%d\n",source_track_metadata_fnn.reserved);

                for (idx = 0; idx < TOTAL_DEGREES; idx++)
                    printf("polarActivity[%d]=%d ",idx, source_track_metadata_fnn.polarActivity[idx]);

                break;
            }
            case FV_11:
            {
                if (is_sound_focus_get) {
                    ret = get_soundfocus_metadata(&sound_focus_metadata, ctl_sf);
                    if (ret != 0) {
                        printf("failed to get soundfocus metadata %d", ret);
                        goto done;
                    }
                }

                if (is_source_track_get) {
                    ret = get_sourcetrack_metadata((void *)&source_track_metadata, sizeof(struct source_track_meta) - sizeof(struct timespec), ctl_st);
                    if (ret != 0) {
                        printf("failed to get source track meta data %d", ret);
                        goto done;
                    }
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    source_track_metadata.ts = ts;
                }

                /* Print meta data */
                fprintf(log_file, "time stamp sec %ld msec %ld \n", (source_track_metadata.ts).tv_sec,
                                              (source_track_metadata.ts).tv_nsec / NSEC_MSEC_CONVERT);
                for (idx = 0; idx < NUM_SECTORS; idx++){
                    printf("vad[%d]=%d ",idx, source_track_metadata.vad[idx]);
                    if (idx < (NUM_SECTORS-1))
                        printf("doa_noise[%d]=%d \n",
                                idx, source_track_metadata.doa_noise[idx]);
                }

                fprintf(log_file, "doa_speech=%d\n",source_track_metadata.doa_speech);
                if (is_sound_focus_get || is_sound_focus_set) {
                    fprintf(log_file, "polar_activity:");
                    for (sect = 0; sect < NUM_SECTORS; sect++ ){
                        fprintf(log_file, "Sector No-%d:",sect + 1);
                        idx = sound_focus_metadata.start_angle[sect];
                        fprintf(log_file, "idx %d:",idx);
                        count = sound_focus_metadata.start_angle[(sect + 1)%NUM_SECTORS] -
                                    sound_focus_metadata.start_angle[sect];
                        fprintf(log_file, "count %d:",count);
                        if (count < 0)
                            count = count + TOTAL_DEGREES;
                        do {
                            fprintf(log_file, "%d ",
                                source_track_metadata.polar_activity[idx%TOTAL_DEGREES]);
                            count--;
                            idx++;
                        } while (count);
                    }
                }
                break;
            }
            default:
                printf("fluence version is not supported");
                break;
        }

        usleep(get_data_time_gap);
        if (get_data_iter > 0)
            get_data_iter--;
    }

done:
    mixer_close(stt_mixer);
    stt_mixer = NULL;

    if ((log_file != stdout) && (log_file != nullptr))
        fclose(log_file);

    fprintf(log_file, "\nADL: BYE BYE\n");

    return ret;
}
