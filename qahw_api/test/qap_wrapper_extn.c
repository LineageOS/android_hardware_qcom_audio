/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2015 The Android Open Source Project *
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

/* Test app extension to exercise QAP (Non-tunnel Decode) */

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cutils/properties.h>
#include <cutils/list.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <system/audio.h>
#include <qap_api.h>
#include <qti_audio.h>
#include "qahw_playback_test.h"

#undef LOG_TAG
#define LOG_TAG "HAL_TEST"
#undef LOG_NDEBUG
/*#define LOG_NDEBUG 0*/

#if LINUX_ENABLED
#define QAC_LIB_MS12 "/usr/lib/libdolby_ms12_wrapper.so"
#define QAC_LIB_M8   "/usr/lib/libdts_m8_wrapper.so"
#else
#define QAC_LIB_MS12 "/system/lib/libdolby_ms12_wrapper.so"
#define QAC_LIB_M8   "/system/lib/libdts_m8_wrapper.so"
#endif

#define SESSION_BLURAY   1
#define SESSION_BROADCAST 2
#define MAX_OUTPUT_CHANNELS 8
#define FRAME_SIZE 2048*2
#define MAX_BUFFER_SIZE 13000
#define CONTIGUOUS_TIMESTAMP 0x7fffffff

qap_lib_handle_t ms12_lib_handle = NULL;
qap_lib_handle_t m8_lib_handle = NULL;
qap_session_handle_t qap_session_handle = NULL;
qahw_module_handle_t *qap_out_hal_handle = NULL;
qahw_module_handle_t *qap_out_spk_handle = NULL;
qahw_module_handle_t *qap_out_hdmi_handle = NULL;
qahw_module_handle_t *qap_out_hp_handle = NULL;

audio_io_handle_t qap_stream_out_spk_handle = 0x999;
audio_io_handle_t qap_stream_out_mch_handle = 0x998;
audio_io_handle_t qap_stream_out_hp_handle = 0x997;

FILE *fp_output_writer_spk = NULL;
FILE *fp_output_writer_hp = NULL;
FILE *fp_output_writer_mch = NULL;
FILE *fp_output_timestamp_file = NULL;
unsigned char data_buf[MAX_BUFFER_SIZE];
uint32_t output_device_id = 0;
uint16_t input_streams_count = 0;

bool hdmi_connected = false;
bool play_through_bt = false;
bool encode = false;
bool dolby_formats = false;
bool dts_formats = false;
bool timestamp_mode = false;
int  data_write_count = 0;
int data_callback_count = 0;

pthread_t main_input_thread;
pthread_attr_t main_input_thrd_attr;
pthread_cond_t main_eos_cond;
pthread_mutex_t main_eos_lock;
pthread_cond_t sec_eos_cond;
pthread_mutex_t sec_eos_lock;

qap_session_outputs_config_t session_output_config;
bool session_output_configured = false;
clock_t tcold_start, tcold_stop;
clock_t tcont_start, tcont_stop;
bool has_system_input = false;
char session_kv_pairs[256] = {0};

static void qap_wrapper_measure_kpi_values(clock_t tcold_start, clock_t tcold_stop,
                                           clock_t tcont_start, clock_t tcont_stop)
{
    double cold_time_latency, cont_time_latency;
    cold_time_latency = ((double)(tcold_stop - tcold_start))/CLOCKS_PER_SEC;
    cont_time_latency = ((double)(tcont_stop - tcont_start))/CLOCKS_PER_SEC;
    fprintf(stdout, "cold time latency %lf ms and cont time latency %lf ms\n", cold_time_latency*1000, cont_time_latency*1000);
}

static void qap_wrapper_read_frame_size_from_file(qap_audio_buffer_t *buffer, FILE *fp_framesize)
{
    if (NULL != fp_framesize) {
        char tempstr[100];
        fgets(tempstr, sizeof(tempstr), fp_framesize);
        buffer->common_params.size = atoi(tempstr);
    }
}

static void read_bytes_timestamps_from_file(qap_audio_buffer_t *buffer, FILE *fp_timestamp, FILE *fp_input_file)
{
    if (NULL != fp_timestamp) {
        char tempstr[100];
        int seek_offset = 0;
        fgets(tempstr, sizeof(tempstr), fp_timestamp);
        printf("%s and tempstr is %s \n", __FUNCTION__,  tempstr);
        char * token = strtok(tempstr, ",");
        if (token != NULL) {
            buffer->common_params.size = atoi(token);
            if(token!= NULL) {
                token = strtok(NULL, ",");
                if (token!= NULL) {
                    buffer->common_params.timestamp = atoi(token);
                    ALOGV("%s and timestamp to be pushed to queue is %lld", __FUNCTION__, buffer->common_params.timestamp);
                }
                token = strtok(NULL, ",");
                if (token != NULL) {
                    seek_offset = atoi(token);
                    if (fp_input_file && seek_offset > 0)
                        fseek(fp_input_file, seek_offset, SEEK_CUR);
                }
            }
        }
    }
}

bool is_qap_session_active(int argc, char* argv[], char *kvp_string) {
    char *qap_kvp = NULL;
    char *cmd_str = NULL;
    char *tmp_str = NULL;
    int status = 0;
    cmd_str = (char *)qap_wrapper_get_cmd_string_from_arg_array(argc, argv, &status);
    if (status > 0) {
        qap_kvp = qap_wrapper_get_single_kvp("qap", cmd_str, &status);
        if (qap_kvp == NULL) {
            return false;
        }
        strncpy(kvp_string, cmd_str, strlen(cmd_str));
        if (cmd_str != NULL) {
            free(cmd_str);
            cmd_str = NULL;
        }
    }
    return true;
}

#ifdef QAP
char *qap_wrapper_get_single_kvp(const char *key, const char *kv_pairs, int *status)
{
    char *kvp = NULL;
    char *tempstr = NULL;
    char *token = NULL;
    char *context1 = NULL;
    char *context2 = NULL;
    char *temp_kvp = NULL;
    char *temp_key = NULL;

    if (NULL == key || NULL == kv_pairs) {
        *status = -EINVAL;
        return NULL;
    }
    tempstr = strdup(kv_pairs);
    token = strtok_r(tempstr, ";", &context1);
    if (token != NULL) {
        temp_kvp = strdup(token);
        if (temp_kvp != NULL) {
            temp_key = strtok_r(temp_kvp, "=", &context2);
            if (!strncmp(key, temp_key, strlen(key))) {
                kvp = malloc((strlen(token) + 1) * sizeof(char));
                memset(kvp, 0, strlen(token) + 1);
                strncat(kvp, token, strlen(token));
                return kvp;
            }
            free(temp_kvp);
        }
        while (token != NULL) {
            token = strtok_r(NULL, ";", &context1);
            if (token != NULL) {
                temp_kvp = strdup(token);
                if (temp_kvp != NULL) {
                    temp_key = strtok_r(temp_kvp, "=", &context2);
                    if (!strncmp(key, temp_key, strlen(key))) {
                        kvp = malloc((strlen(token) + 1) * sizeof(char));
                        memset(kvp, 0, strlen(token) + 1);
                        strncat(kvp, token, strlen(token));
                        return kvp;
                    }
                    free(temp_kvp);
                    temp_kvp = NULL;
                }
            }
        }
        free(tempstr);
    }
    return NULL;
}
#endif

int *qap_wrapper_get_int_value_array(const char *kvp, int *count, int *status __unused)
{
    char *tempstr1;
    char *tempstr2;
    char *l1;
    char *l2 __unused;
    char *ctx1;
    char *ctx2 __unused;
    int *val = NULL;
    int i = 0;
    char *s;
    char *endstr;
    int temp = 0;
    char *jump;

    *count = 0;
    if (kvp == NULL) {
        return NULL;
    }
    tempstr1 = strdup(kvp);
    l1 = strtok_r(tempstr1, "=", &ctx1);
    if (l1 != NULL) {
        /* jump from key to value */
        l1 = strtok_r(NULL, "=", &ctx1);
        if (l1 != NULL) {
            tempstr2 = strdup(l1);

            s = tempstr2;
            for (i=0; s[i]; s[i]==',' ? i++ : *s++);

            temp = i;
            val = malloc((i + 1)*sizeof(int));
            i = 0;
            val[i++] = strtol(tempstr2, &endstr, 0);

            while (i <= temp) {
                 jump = endstr + 1;
                val[i++] = strtol(jump, &endstr, 0);
            }
            free(tempstr2);
        }
    }
    free(tempstr1);
    *count = i;
    return val;
}

char * qap_wrapper_get_cmd_string_from_arg_array(int argc, char * argv[], int *status)
{
    char * kvps;
    int idx;
    int has_key = 0;
    int mem = 0;

    fprintf(stdout, "%s %d in", __func__, __LINE__);
    if (argc < 2 || NULL == argv) {
        fprintf(stdout, "%s %d returning EINVAL\n", __func__, __LINE__);
        *status = -EINVAL;
        return NULL;
    }

    for (idx = 0; idx < argc; idx++) {
        mem += (strlen(argv[idx]) + 2);     /* Extra byte to insert delim ';' */
    }

    if (mem > 0)
        kvps = malloc(mem * sizeof(char));
    else {
        *status = -EINVAL;
        fprintf(stdout, "%s %d returning EINVAL\n", __func__, __LINE__);
        return NULL;
    }

    if (NULL == kvps) {
        *status = -ENOMEM;
        fprintf(stdout, "%s %d returning EINVAL\n", __func__, __LINE__);
        return NULL;
    }

    for (idx = 1; idx < argc; idx++) {
        if (( argv[idx][0] == '-') &&
                (argv[idx][1] < '0' || argv[idx][1] > '9')) {
            if (has_key) {
                strcat(kvps, ";");
                has_key = 0;
            }
            strcat(kvps, argv[idx]+1);
            strcat(kvps, "=");
            has_key = 1;
        } else if (has_key) {
            strcat(kvps, argv[idx]);
            strcat(kvps, ";");
            has_key = 0;
        } else {
            *status = -EINVAL;
            if (kvps != NULL) {
                free(kvps);
                kvps = NULL;
            }
            fprintf(stdout, "%s %d returning EINVAL\n", __func__, __LINE__);
            return NULL;
        }
    }
    *status = mem;
    fprintf(stdout, "%s %d returning\n", __func__, __LINE__);
    return kvps;
}

static int qap_wrapper_map_input_format(audio_format_t audio_format, qap_audio_format_t *format)
{
    if (audio_format == AUDIO_FORMAT_AC3) {
        *format = QAP_AUDIO_FORMAT_AC3;
        fprintf(stdout, "File Format is AC3!\n");
    } else if (audio_format == AUDIO_FORMAT_E_AC3) {
        *format = QAP_AUDIO_FORMAT_EAC3;
        fprintf(stdout, "File Format is E_AC3!\n");
    } else if ((audio_format == AUDIO_FORMAT_AAC_ADTS_LC) ||
               (audio_format == AUDIO_FORMAT_AAC_ADTS_HE_V1) ||
               (audio_format == AUDIO_FORMAT_AAC_ADTS_HE_V2) ||
               (audio_format == AUDIO_FORMAT_AAC_LC) ||
               (audio_format == AUDIO_FORMAT_AAC_HE_V1) ||
               (audio_format == AUDIO_FORMAT_AAC_HE_V2) ||
               (audio_format == AUDIO_FORMAT_AAC_LATM_LC) ||
               (audio_format == AUDIO_FORMAT_AAC_LATM_HE_V1) ||
               (audio_format == AUDIO_FORMAT_AAC_LATM_HE_V2)) {
        *format = QAP_AUDIO_FORMAT_AAC_ADTS;
        fprintf(stdout, "File Format is AAC!\n");
    } else if (audio_format == AUDIO_FORMAT_DTS) {
        *format = QAP_AUDIO_FORMAT_DTS;
        fprintf(stdout, "File Format is DTS!\n");
    } else if (audio_format == AUDIO_FORMAT_DTS_HD) {
        *format = QAP_AUDIO_FORMAT_DTS_HD;
        fprintf(stdout, "File Format is DTS_HD!\n");
    } else if (audio_format == AUDIO_FORMAT_PCM_16_BIT) {
        *format = QAP_AUDIO_FORMAT_PCM_16_BIT;
        fprintf(stdout, "File Format is PCM_16!\n");
    } else if (audio_format == AUDIO_FORMAT_PCM_32_BIT) {
        *format = QAP_AUDIO_FORMAT_PCM_32_BIT;
        fprintf(stdout, "File Format is PCM_32!\n");
    } else if (audio_format == AUDIO_FORMAT_PCM_24_BIT_PACKED) {
        *format = QAP_AUDIO_FORMAT_PCM_24_BIT_PACKED;
        fprintf(stdout, "File Format is PCM_24!\n");
    } else if ((audio_format == AUDIO_FORMAT_PCM_8_BIT) ||
               (audio_format == AUDIO_FORMAT_PCM_8_24_BIT)) {
        *format = QAP_AUDIO_FORMAT_PCM_8_24_BIT;
        fprintf(stdout, "File Format is PCM_8_24!\n");
    } else {
        fprintf(stdout, "File Format not supported!\n");
        return -EINVAL;
    }
    return 0;
}

char *get_string_value(const char *kvp, int *status)
{
    char *tempstr1 = NULL;
    char *tempstr2 = NULL;
    char *l1;
    char *ctx1;
    if (kvp == NULL)
        return NULL;
    tempstr1 = strdup(kvp);
    l1 = strtok_r(tempstr1, "=", &ctx1);
    if (l1 != NULL) {
        /* jump from key to value */
        l1 = strtok_r(NULL, "=", &ctx1);
        if (l1 != NULL)
            tempstr2 = strdup(l1);
    }
    free(tempstr1);
    return tempstr2;
}

int qap_wrapper_write_to_hal(qahw_stream_handle_t* out_handle, char *data, size_t bytes)
{
    ssize_t ret;
    qahw_out_buffer_t out_buf;

    memset(&out_buf,0, sizeof(qahw_out_buffer_t));
    out_buf.buffer = data;
    out_buf.bytes = bytes;

    ret = qahw_out_write(out_handle, &out_buf);
    if (ret < 0)
        fprintf(stderr, "%s::%d: writing data to hal failed (ret = %zd)\n", __func__, __LINE__, ret);
    else if (ret != bytes)
        fprintf(stdout, "%s::%d provided bytes %zd, written bytes %d\n",__func__, __LINE__, bytes, ret);

    return ret;
}

static void close_output_streams()
{
    int ret;
    if (qap_out_hal_handle && qap_out_spk_handle) {
        ret = qahw_out_standby(qap_out_spk_handle);
        if (ret)
            fprintf(stderr, "%s::%d: out standby failed %d \n", __func__, __LINE__, ret);
        if (play_through_bt) {
            fprintf(stdout, "%s::%d: disconnecting BT\n", __func__, __LINE__);
            char param[100] = {0};
            snprintf(param, sizeof(param), "%s=%d", "disconnect", AUDIO_DEVICE_OUT_BLUETOOTH_A2DP);
            qahw_set_parameters(qap_out_hal_handle, param);
        }
        fprintf(stdout, "%s::%d: closing output stream\n", __func__, __LINE__);
        ret = qahw_close_output_stream(qap_out_spk_handle);
        if (ret)
            fprintf(stderr, "%s::%d: could not close output stream, error - %d\n", __func__, __LINE__, ret);
        qap_out_spk_handle = NULL;
    }
    if (qap_out_hal_handle && qap_out_hp_handle) {
        ret = qahw_out_standby(qap_out_hp_handle);
        if (ret)
            fprintf(stderr, "%s::%d: out standby failed %d \n", __func__, __LINE__, ret);
        fprintf(stdout, "%s::%d: closing output stream\n", __func__, __LINE__);
        ret = qahw_close_output_stream(qap_out_hp_handle);
        if (ret)
            fprintf(stderr, "%s::%d: could not close output stream, error - %d\n", __func__, __LINE__, ret);
        qap_out_hp_handle = NULL;
    }
    if (qap_out_hal_handle && qap_out_hdmi_handle) {
        char param[100] = {0};
        snprintf(param, sizeof(param), "%s=%d", "disconnect", AUDIO_DEVICE_OUT_HDMI);
        ret = qahw_out_standby(qap_out_hdmi_handle);
        if (ret)
            fprintf(stderr, "%s::%d: out standby failed %d\n", __func__, __LINE__, ret);
        qahw_set_parameters(qap_out_hal_handle, param);
        fprintf(stdout, "%s::%d: closing output stream\n", __func__, __LINE__);
        ret = qahw_close_output_stream(qap_out_hdmi_handle);
        if (ret)
            fprintf(stderr, "%s::%d: could not close output stream, error - %d\n", __func__, __LINE__, ret);
        qap_out_hdmi_handle = NULL;
    }
}

void qap_wrapper_session_callback(qap_session_handle_t session_handle __unused, void* priv_data __unused, qap_callback_event_t event_id, int size __unused, void *data)
{
    int ret = 0;
    int bytes_written = 0;
    int bytes_remaining = 0;
    int offset = 0;
    audio_output_flags_t flags;
    flags = (AUDIO_OUTPUT_FLAG_NON_BLOCKING
             | AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD
             | AUDIO_OUTPUT_FLAG_DIRECT);/*Need to add Main and Associated Flags during mixing case*/
    ALOGV("%s %d Received event id %d\n", __func__, __LINE__, event_id);
    switch (event_id) {
        case QAP_CALLBACK_EVENT_EOS:
            ALOGV("%s %d Received Main Input EOS", __func__, __LINE__);
            pthread_mutex_lock(&main_eos_lock);
            pthread_cond_signal(&main_eos_cond);
            pthread_mutex_unlock(&main_eos_lock);

            close_output_streams();
            if (qap_out_hal_handle) {
                unload_hals();
                qap_out_hal_handle = NULL;
            }
            break;
        case QAP_CALLBACK_EVENT_EOS_ASSOC:
        case QAP_CALLBACK_EVENT_MAIN_2_EOS:
            if (!has_system_input){
                ALOGV("%s %d Received Secondary Input EOS", __func__, __LINE__);
                pthread_mutex_lock(&sec_eos_lock);
                pthread_cond_signal(&sec_eos_cond);
                pthread_mutex_unlock(&sec_eos_lock);
            }
            break;
        case QAP_CALLBACK_EVENT_ERROR:
            break;
        case QAP_CALLBACK_EVENT_SUCCESS:
            break;
        case QAP_CALLBACK_EVENT_METADATA:
        case QAP_CALLBACK_EVENT_OUTPUT_CFG_CHANGE:
            break;
        case QAP_CALLBACK_EVENT_DATA:
            if (data != NULL) {
                if (kpi_mode) {
                    data_callback_count++;
                    if (data_callback_count == 1) {
                        tcold_stop = clock();
                    } else if (data_callback_count == 15) {
                        tcont_stop = clock();
                        qap_wrapper_measure_kpi_values(tcold_start, tcold_stop, tcont_start, tcont_stop);
                    }
                }
                qap_audio_buffer_t *buffer = (qap_audio_buffer_t *) data;
                if (qap_out_hal_handle == NULL) {
                    fprintf(stdout, "%s::%d: device id %d\n",__func__, __LINE__, buffer->buffer_parms.output_buf_params.output_id);
                    qap_out_hal_handle = load_hal(buffer->buffer_parms.output_buf_params.output_id);
                    if (qap_out_hal_handle == NULL) {
                        fprintf(stderr, "Failed log load HAL\n");
                        return;
                    }
                }

                if (buffer && timestamp_mode) {
                    char ch[100] = {0};
                    if (fp_output_timestamp_file == NULL) {
                        fp_output_timestamp_file =
                                 fopen("/sdcard/output_timestamp_file.txt","w");
                        if(fp_output_timestamp_file) {
                            fprintf(stdout, "output file :: "
                                   "/sdcard/output_file_timestamp.txt"
                                   " has been generated.");
                            }
                    }
                    if (fp_output_timestamp_file) {
                        sprintf(ch, "%d,%lld\n", buffer->common_params.size, buffer->common_params.timestamp);
                        fprintf(stdout, "%s: %s", __func__, ch);
                        ret = fwrite((char *)&ch, sizeof(char),
                                     strlen(ch), fp_output_timestamp_file);
                        fflush(fp_output_timestamp_file);
                    }
                }

                if (buffer && buffer->common_params.data) {
                    if (buffer->buffer_parms.output_buf_params.output_id &
                            AUDIO_DEVICE_OUT_HDMI) {
                        if (enable_dump && fp_output_writer_mch == NULL) {
                            fp_output_writer_mch =
                                         fopen("/sdcard/output_hdmi.dump","wb");
                            if (fp_output_writer_mch) {
                                fprintf(stdout, "output file :: "
                                      "/sdcard/output_hdmi.dump"
                                      " has been generated.\n");
                            } else {
                                fprintf(stderr, "Failed open hdmi dump file\n");
                            }
                        }
                        if (fp_output_writer_mch) {
                            ret = fwrite((unsigned char *)buffer->common_params.data, sizeof(unsigned char),
                                          buffer->common_params.size, fp_output_writer_mch);
                            fflush(fp_output_writer_mch);
                        }
                        if (!hdmi_connected) {
                            char param[100] = {0};
                            snprintf(param, sizeof(param), "%s=%d", "connect", AUDIO_DEVICE_OUT_HDMI);
                            qahw_set_parameters(qap_out_hal_handle, param);
                            hdmi_connected = true;
                        }
                        if (hdmi_connected && qap_out_hdmi_handle == NULL) {
                            struct audio_config config;
                            audio_devices_t devices;
                            config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                            config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
                            config.sample_rate = config.offload_info.sample_rate =
                                                        session_output_config.output_config->sample_rate;
                            if (session_output_config.output_config->bit_width == 24) {
                                config.format = config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
                                config.offload_info.bit_width = 24;
                            } else {
                                config.format = config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
                                config.offload_info.bit_width = 16;
                            }

                            if (session_output_config.output_config->channels == 2) {
                                config.offload_info.channel_mask = config.channel_mask =
                                    AUDIO_CHANNEL_OUT_STEREO;
                            } else {
                                config.offload_info.channel_mask = config.channel_mask =
                                    audio_channel_out_mask_from_count(MAX_OUTPUT_CHANNELS);
                            }
                            devices = AUDIO_DEVICE_OUT_HDMI;

                            ret = qahw_open_output_stream(qap_out_hal_handle, qap_stream_out_mch_handle, devices,
                                 flags, &config, &qap_out_hdmi_handle, "stream");

                            if (ret) {
                                fprintf(stdout, "%s:%d could not open output stream, error - %d \n", __func__, __LINE__, ret);
                                return;
                            }
                            ret = qahw_out_set_volume(qap_out_hdmi_handle, vol_level, vol_level);
                            if (ret < 0)
                                fprintf(stderr, "unable to set volume\n");
                        }
                        if (qap_out_hdmi_handle) {
                                bytes_written = qap_wrapper_write_to_hal(qap_out_hdmi_handle, buffer->common_params.data, buffer->common_params.size);
                                if (bytes_written == -1) {
                                    fprintf(stderr, "%s::%d write failed in hal\n", __func__, __LINE__);
                                }
                        }
                    }
                    if (buffer->buffer_parms.output_buf_params.output_id & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) {
                        if (enable_dump && fp_output_writer_hp == NULL) {
                            fp_output_writer_hp =
                                         fopen("/sdcard/output_hp.dump","wb");
                            if (fp_output_writer_hp) {
                                fprintf(stdout, "output file :: "
                                      "/sdcard/output_hp.dump"
                                      " has been generated.\n");
                            } else {
                                fprintf(stderr, "Failed open hp dump file\n");
                            }
                        }
                        if (fp_output_writer_hp) {
                            ret = fwrite((unsigned char *)buffer->common_params.data, sizeof(unsigned char),
                                          buffer->common_params.size, fp_output_writer_hp);
                            fflush(fp_output_writer_hp);
                        }
                        if (qap_out_hp_handle == NULL) {
                            struct audio_config config;
                            audio_devices_t devices;
                            config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                            config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
                            config.sample_rate = config.offload_info.sample_rate =
                                                        session_output_config.output_config->sample_rate;
                            if (session_output_config.output_config->bit_width == 24) {
                                config.format = config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
                                config.offload_info.bit_width = 24;
                            } else {
                                config.format = config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
                                config.offload_info.bit_width = 16;
                            }

                            config.offload_info.channel_mask = config.channel_mask =
                                    AUDIO_CHANNEL_OUT_STEREO;
                            devices = AUDIO_DEVICE_OUT_LINE;//ToDO - Need to change to AUDIO_DEVICE_OUT_WIRED_HEADPHONE

                            ret = qahw_open_output_stream(qap_out_hal_handle, qap_stream_out_hp_handle, devices,
                                 flags, &config, &qap_out_hp_handle, "stream");

                            if (ret) {
                                fprintf(stderr, "%s:%d could not open output stream, error - %d \n", __func__, __LINE__, ret);
                                return;
                            }
                            ret = qahw_out_set_volume(qap_out_hp_handle, vol_level, vol_level);
                            if (ret < 0)
                                 fprintf(stderr, "unable to set volume\n");
                        }
                        if (qap_out_hp_handle) {
                                bytes_written = qap_wrapper_write_to_hal(qap_out_hp_handle, buffer->common_params.data, buffer->common_params.size);
                                if (bytes_written == -1) {
                                    fprintf(stderr, "%s::%d write failed in hal\n", __func__, __LINE__);
                                }
                        }
                    }
                    if (buffer->buffer_parms.output_buf_params.output_id & AUDIO_DEVICE_OUT_SPEAKER) {
                        if (enable_dump && fp_output_writer_spk == NULL) {
                            char ch[4] = {0};
                            fp_output_writer_spk =
                                         fopen("/sdcard/output_speaker.dump","wb");
                            if (fp_output_writer_spk) {
                                fprintf(stdout, "output file :: "
                                      "/sdcard/output_speaker.dump"
                                      " has been generated.\n");
                                if (!dts_formats) {
                                    ret = fwrite((unsigned char *)&ch, sizeof(unsigned char),
                                                  4, fp_output_writer_spk);
                                }
                            } else {
                                fprintf(stderr, "Failed open speaker dump file\n");
                            }
                        }
                        if (fp_output_writer_spk) {
                            ret = fwrite((unsigned char *)buffer->common_params.data, sizeof(unsigned char),
                                          buffer->common_params.size, fp_output_writer_spk);
                            fflush(fp_output_writer_spk);
                        }
                        if (qap_out_spk_handle == NULL) {
                            struct audio_config config;
                            audio_devices_t devices;
                            config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                            config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
                            config.sample_rate = config.offload_info.sample_rate =
                                                        session_output_config.output_config->sample_rate;
                            if (session_output_config.output_config->bit_width == 24) {
                                config.format = config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
                                config.offload_info.bit_width = 24;
                            } else {
                                config.format = config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
                                config.offload_info.bit_width = 16;
                            }

                            config.offload_info.channel_mask = config.channel_mask =
                                    AUDIO_CHANNEL_OUT_STEREO;
                            if (play_through_bt) {
                                fprintf(stderr, "%s::%d: connecting BT\n", __func__, __LINE__);
                                char param[100] = {0};
                                snprintf(param, sizeof(param), "%s=%d", "connect", AUDIO_DEVICE_OUT_BLUETOOTH_A2DP);
                                qahw_set_parameters(qap_out_hal_handle, param);
                                devices = AUDIO_DEVICE_OUT_BLUETOOTH_A2DP;
                            } else {
                                devices = AUDIO_DEVICE_OUT_SPEAKER;
                            }
                            fprintf(stderr, "%s::%d: open output for device %d\n", __func__, __LINE__, devices);
                            ret = qahw_open_output_stream(qap_out_hal_handle, qap_stream_out_spk_handle, devices,
                                 flags, &config, &qap_out_spk_handle, "stream");

                            if (ret) {
                                fprintf(stderr, "%s:%d could not open output stream, error - %d \n", __func__, __LINE__, ret);
                                return;
                            }
                            ret = qahw_out_set_volume(qap_out_spk_handle, vol_level, vol_level);
                            if (ret < 0)
                                 fprintf(stderr, "unable to set volume\n");
                        }
                        if (qap_out_spk_handle) {
                                bytes_written = qap_wrapper_write_to_hal(qap_out_spk_handle, buffer->common_params.data, buffer->common_params.size);
                                if (bytes_written == -1) {
                                    fprintf(stderr, "%s::%d write failed in hal\n", __func__, __LINE__);
                                }
                        }
                    }
                }
            }
            break;
        default:
            break;
    }
}

static void qap_wrapper_is_dap_enabled(char *kv_pairs, int out_device_id) {
    int status = 0;
    int temp = 0;
    char *dap_kvp = NULL;
    int *dap_value = NULL;
    int dap_enable = 0;

    dap_kvp = qap_wrapper_get_single_kvp("dap_enable", kv_pairs, &status);
    if (dap_kvp != NULL) {
        dap_value = qap_wrapper_get_int_value_array(dap_kvp, &temp, &status);
        if (dap_value != NULL)
            dap_enable = dap_value[0];
        if (dap_enable) {
            fprintf(stdout, "dap enable %d and device id %d\n", dap_enable, out_device_id);
            char *dev_kvp = NULL;
            if (out_device_id == AUDIO_DEVICE_OUT_SPEAKER) {
                dev_kvp = (char *) calloc(1, status + strlen("o_device=1; "));
                if (dev_kvp != NULL) {
                    strcat(dev_kvp, "o_device=1;");
                    strcat(session_kv_pairs, dev_kvp);
                    fprintf(stdout, "session set params %s\n", session_kv_pairs);
                    free(dev_kvp);
                    dev_kvp = NULL;
                }
            } else if ((out_device_id == AUDIO_DEVICE_OUT_LINE)||
                       (out_device_id == AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) {
                dev_kvp = (char *) calloc(1, status + strlen("o_device=2; "));
                if (dev_kvp != NULL) {
                    strcat(dev_kvp, "o_device=2;");
                    strcat(session_kv_pairs, dev_kvp);
                    fprintf(stdout, "session set params %s\n", session_kv_pairs);
                    free(dev_kvp);
                    dev_kvp = NULL;
                }
            }
        }
        free(dap_kvp);
        dap_kvp = NULL;
    }
}

int qap_wrapper_session_open(char *kv_pairs, void* stream_data, int num_of_streams)
{
    int status = 0;
    int ret = 0;
    int i;
    int temp = 0;
    stream_config *stream = (stream_config *)stream_data;
    char *session_type_kvp = NULL;
    char *encode_kvp = NULL;
    int *temp_val = NULL;
    char *bitwidth_kvp = NULL;
    uint8_t session_type = SESSION_BLURAY;

    tcold_start = clock();
    memset(&session_output_config, 0, sizeof(session_output_config));
    strcpy(session_kv_pairs, kv_pairs);

    session_type_kvp = qap_wrapper_get_single_kvp("broadcast", kv_pairs, &status);
    if (session_type_kvp != NULL) {
        session_type = SESSION_BROADCAST;
        fprintf(stdout, "Session Type is Broadcast\n");
        free(session_type_kvp);
        session_type_kvp = NULL;
    } else {
        fprintf(stdout, "Session Type is Bluray\n");
    }

    if (session_type == SESSION_BLURAY) {
        if ((stream->filetype == FILE_WAV) ||
            (stream->filetype == FILE_AAC)) {
            fprintf(stderr, "Format is not supported for BD usecase\n");
            return -EINVAL;
        }
        if (num_of_streams > 1) {
            fprintf(stderr, "Please specifiy proper session type\n");
            return -EINVAL;
        }
    }

    if (stream->filetype == FILE_DTS) {
        m8_lib_handle = (qap_session_handle_t) qap_load_library(QAC_LIB_M8);
        if (m8_lib_handle == NULL) {
            fprintf(stdout, "Failed to load M8 library\n");
            return -EINVAL;
        }
        fprintf(stdout, "loaded M8 library\n");
        dts_formats = true;
    } else if ((stream->filetype == FILE_AC3) ||
                (stream->filetype == FILE_EAC3) ||
                (stream->filetype == FILE_EAC3_JOC) ||
                (stream->filetype == FILE_WAV) ||
                (stream->filetype == FILE_AAC) ||
                (stream->filetype == FILE_AAC_ADTS) ||
                (stream->filetype == FILE_AAC_LATM)) {
        ms12_lib_handle = (qap_session_handle_t) qap_load_library(QAC_LIB_MS12);
        if (ms12_lib_handle == NULL) {
            fprintf(stderr, "Failed to load MS12 library\n");
            return -EINVAL;
        }
        dolby_formats = true;
    }

    qap_wrapper_is_dap_enabled(kv_pairs, stream->output_device);

    encode_kvp = qap_wrapper_get_single_kvp("od", kv_pairs, &status);
    if (encode_kvp != NULL) {
        encode = true;
        free(encode_kvp);
        encode_kvp = NULL;
    }

    encode_kvp = qap_wrapper_get_single_kvp("odp", kv_pairs, &status);
    if (encode_kvp != NULL) {
        encode = true;
        free(encode_kvp);
        encode_kvp = NULL;
    }

    encode_kvp = qap_wrapper_get_single_kvp("odts", kv_pairs, &status);
    if (encode_kvp != NULL) {
        encode = true;
        free(encode_kvp);
        encode_kvp = NULL;
    }

    if (stream->filetype == FILE_DTS)
        session_output_config.output_config->bit_width = 24;

    bitwidth_kvp = qap_wrapper_get_single_kvp("bitwidth", kv_pairs, &status);
    if (bitwidth_kvp != NULL) {
        temp_val = qap_wrapper_get_int_value_array(bitwidth_kvp, &temp, &status);
        if (temp_val != NULL) {
            if (stream->filetype == FILE_DTS)
                session_output_config.output_config->bit_width = temp_val[0];
            free(temp_val);
            temp_val = NULL;
        }
        free(bitwidth_kvp);
        bitwidth_kvp = NULL;
    }

    if ((session_type == SESSION_BROADCAST) && dolby_formats) {
        fprintf(stdout, "%s::%d Setting BROADCAST session for dolby formats\n", __func__, __LINE__);
        qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_BROADCAST, ms12_lib_handle);
        if (qap_session_handle == NULL)
            return -EINVAL;
    } else if ((session_type == SESSION_BROADCAST) && dts_formats) {
        fprintf(stdout, "%s::%d Setting BROADCAST session for dts formats\n", __func__, __LINE__);
        qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_BROADCAST, m8_lib_handle);
        if (qap_session_handle == NULL)
            return -EINVAL;
    } else if (session_type == SESSION_BLURAY) {
        fprintf(stdout, "%s::%d Setting BD session\n", __func__, __LINE__);
        if (!encode && dolby_formats) {
            fprintf(stdout, "%s::%d Setting BD session for decoding dolby formats\n", __func__, __LINE__);
            qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_DECODE_ONLY, ms12_lib_handle);
            if (qap_session_handle == NULL)
                return -EINVAL;
        } else if (!encode && dts_formats) {
            fprintf(stdout, "%s::%d Setting BD session for decoding dts formats \n", __func__, __LINE__, qap_session_handle);
            qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_DECODE_ONLY, m8_lib_handle);
            if (qap_session_handle == NULL)
                return -EINVAL;
        } else if (encode && dolby_formats)  {
            fprintf(stdout, "%s::%d Setting BD session for encoding dolby formats \n", __func__, __LINE__, qap_session_handle);
            qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_ENCODE_ONLY, ms12_lib_handle);
            if (qap_session_handle == NULL)
                return -EINVAL;
        } else if (encode && dts_formats) {
            fprintf(stdout, "%s::%d Setting BD session for encoding dts formats \n", __func__, __LINE__, qap_session_handle);
            qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_ENCODE_ONLY, m8_lib_handle);
            if (qap_session_handle == NULL)
                return -EINVAL;
        }
    }

    ret = qap_session_set_callback(qap_session_handle, &qap_wrapper_session_callback);
    if (ret != QAP_STATUS_OK) {
        fprintf(stderr, "!!!! Please specify appropriate Session\n");
        return -EINVAL;
    }

    if (stream->filetype == FILE_DTS) {
        ALOGV("Session set params %s", session_kv_pairs);
        ret = qap_session_cmd(qap_session_handle, QAP_SESSION_CMD_SET_KVPAIRS, sizeof(session_kv_pairs), session_kv_pairs, NULL, NULL);
        if (ret != QAP_STATUS_OK) {
            fprintf(stderr, "Session set params failed\n");
            return -EINVAL;
        }
    }

    pthread_mutex_init(&main_eos_lock, (const pthread_mutexattr_t *)NULL);
    pthread_mutex_init(&sec_eos_lock, (const pthread_mutexattr_t *)NULL);
    pthread_cond_init(&main_eos_cond, (const pthread_condattr_t *) NULL);
    pthread_cond_init(&sec_eos_cond, (const pthread_condattr_t *) NULL);
    fprintf(stdout, "Session open returing success\n");
    return 0;
}

int qap_wrapper_session_close ()
{
    ALOGD("closing QAP session");
    qap_session_close(qap_session_handle);
    qap_session_handle = NULL;
}

void *qap_wrapper_start_stream (void* stream_data)
{
    int ret = 0;
    qap_audio_buffer_t *buffer;
    int8_t first_read = 1;
    int bytes_wanted;
    int bytes_read;
    int bytes_consumed = 0, status = 0;;
    qap_module_handle_t qap_module_handle = NULL;
    stream_config *stream_info = (stream_config *)stream_data;
    FILE *fp_input = stream_info->file_stream;
    int is_buffer_available = 0;
    char *temp_str = NULL;
    void *reply_data;
    char* temp_ptr = NULL;
    qap_audio_format_t format;

    if (fp_input == NULL) {
        fprintf(stderr, "Open File Failed for %s\n", stream_info->filename);
        pthread_exit(0);
    }
    qap_module_handle = stream_info->qap_module_handle;
    buffer = (qap_audio_buffer_t *) calloc(1, sizeof(qap_audio_buffer_t));
    if (buffer == NULL) {
        fprintf(stderr, "%s::%d: Memory Alloc Error\n", __func__, __LINE__);
        pthread_exit(0);
    }
    buffer->common_params.data = calloc(1, FRAME_SIZE);
    if (buffer->common_params.data == NULL) {
        fprintf(stderr, "%s::%d: Memory Alloc Error\n", __func__, __LINE__);
        pthread_exit(0);
        if (NULL != buffer) {
            free( buffer);
            buffer = NULL;
        }
    }
    buffer->buffer_parms.output_buf_params.output_id = output_device_id;
    fprintf(stdout, "%s::%d: output device id %d\n",
                __func__, __LINE__, buffer->buffer_parms.output_buf_params.output_id);

    fprintf(stdout, "Opened Input File %s format %d handle %p\n", stream_info->filename, format, fp_input);

    ret = qap_module_cmd(qap_module_handle, QAP_MODULE_CMD_START, sizeof(QAP_MODULE_CMD_START), NULL, NULL, NULL);
    if (ret != QAP_STATUS_OK) {
        fprintf(stderr, "START failed\n");
        pthread_exit(0);
        if (NULL != buffer &&  NULL != buffer->common_params.data) {
            free( buffer->common_params.data);
            buffer->common_params.data = NULL;
            free( buffer);
            buffer = NULL;
        }
    }

    do {
        if (stream_info->filetype == FILE_WAV) {
            if (first_read) {
                first_read = 0;
                int wav_header_len = get_wav_header_length(stream_info->file_stream);
                fseek(fp_input, wav_header_len, SEEK_SET);
            }
        }
        buffer->buffer_parms.input_buf_params.flags = QAP_BUFFER_NO_TSTAMP;
        buffer->common_params.timestamp = QAP_BUFFER_NO_TSTAMP;
        buffer->common_params.size = stream_info->bytes_to_read;
        if (kpi_mode) {
            if (stream_info->framesize_filename != NULL) {
                if (!stream_info->framesize_file_ptr) {
                    stream_info->framesize_file_ptr = fopen(stream_info->framesize_filename, "r");
                    if (!stream_info->framesize_file_ptr) {
                        fprintf(stderr, "Cannot open audio file %s\n", stream_info->framesize_filename);
                        goto exit;
                    }
                }
                qap_wrapper_read_frame_size_from_file(buffer, stream_info->framesize_file_ptr);
            } else {
                fprintf(stdout, "%s Could not found frame size file\n", __FUNCTION__);
                goto exit;
            }
        }

        if (stream_info->timestamp_filename != NULL) {
            if (!stream_info->timestamp_file_ptr) {
                stream_info->timestamp_file_ptr = fopen(stream_info->timestamp_filename, "r");
                if (!stream_info->timestamp_file_ptr) {
                    fprintf(stderr, "Cannot open audio file %s\n", stream_info->filename);
                    goto exit;
                }
            }
            read_bytes_timestamps_from_file(buffer, stream_info->timestamp_file_ptr, fp_input);
            if (buffer->common_params.timestamp == CONTIGUOUS_TIMESTAMP)
                buffer->buffer_parms.input_buf_params.flags = QAP_BUFFER_TSTAMP_CONTINUE;
            else
                buffer->buffer_parms.input_buf_params.flags = QAP_BUFFER_TSTAMP;
            timestamp_mode = true;
        }

        bytes_wanted = buffer->common_params.size;
        bytes_read = fread(data_buf, sizeof(unsigned char), bytes_wanted, fp_input);

        buffer->common_params.offset = 0;
        buffer->common_params.size = bytes_read;
        memcpy(buffer->common_params.data, data_buf, bytes_read);
        if (bytes_read <= 0) {
            buffer->buffer_parms.input_buf_params.flags = QAP_BUFFER_EOS;
            bytes_consumed = qap_module_process(qap_module_handle, buffer);
            ret = qap_module_cmd(qap_module_handle, QAP_MODULE_CMD_STOP, sizeof(QAP_MODULE_CMD_STOP), NULL, NULL, NULL);
            fprintf(stdout, "Stopped feeding input %s : %p\n", stream_info->filename, fp_input);
            ALOGV("Stopped feeding input %s : %p", stream_info->filename, fp_input);
            break;
        }

        reply_data = (char*) calloc(1, 100);
        is_buffer_available = 0;
        temp_ptr = buffer->common_params.data;
        if (kpi_mode)
            data_write_count++;
        do {
            if (kpi_mode) {
                if (data_write_count == 16) {
                    tcont_start = clock();
                } else if (data_write_count == 18) {
                    buffer->buffer_parms.input_buf_params.flags = QAP_BUFFER_EOS;
                    bytes_consumed = qap_module_process(qap_module_handle, buffer);
                    qap_module_cmd(qap_module_handle, QAP_MODULE_CMD_STOP, sizeof(QAP_MODULE_CMD_STOP), NULL, NULL, NULL);
                    goto wait_for_eos;
                }
            }
            bytes_consumed = qap_module_process(qap_module_handle, buffer);
            if (bytes_consumed > 0) {
                buffer->common_params.data += bytes_consumed;
                buffer->common_params.size -= bytes_consumed;
            }
            ALOGV("%s %d feeding Input of size %d  and bytes_cosumed is %d",
                      __FUNCTION__, __LINE__,bytes_read, bytes_consumed);
            if ((format == QAP_AUDIO_FORMAT_DTS) ||
                 (format == QAP_AUDIO_FORMAT_DTS_HD)) {
                  if (bytes_consumed < 0) {
                      while (!is_buffer_available) {
                          usleep(1000);
                          ret = qap_module_cmd(qap_module_handle, QAP_MODULE_CMD_GET_PARAM,
                                 sizeof(QAP_MODULE_CMD_GET_PARAM), "buf_available", NULL, reply_data
                          );
                          if (reply_data)
                              temp_str = get_string_value(reply_data, &status);
                           if (temp_str) {
                               is_buffer_available = atoi(temp_str);
                               free(temp_str);
                           }
                           ALOGV("%s : %d, dts clip reply_data is %d buffer availabale is %d",
                                 __FUNCTION__, __LINE__, reply_data, is_buffer_available);
                       }
                  }
             }
        } while (buffer->common_params.size > 0);
        if (reply_data)
            free(reply_data);
        buffer->common_params.data = temp_ptr;
        if (!(stream_info->system_input || stream_info->sec_input)) {
            usleep(5000); //To swtich between main and secondary threads incase of dual input
        }
    } while (1);

wait_for_eos:
    if (stream_info->sec_input && !stream_info->aac_fmt_type) {
        pthread_mutex_lock(&sec_eos_lock);
        pthread_cond_wait(&sec_eos_cond, &sec_eos_lock);
        pthread_mutex_unlock(&sec_eos_lock);
        fprintf(stdout, "Received EOS event for secondary input\n");
        ALOGV("Received EOS event for secondary input\n");
    }
    if (!(stream_info->system_input || stream_info->sec_input)){
        pthread_mutex_lock(&main_eos_lock);
        pthread_cond_wait(&main_eos_cond, &main_eos_lock);
        pthread_mutex_unlock(&main_eos_lock);
        fprintf(stdout, "Received EOS event for main input\n");
        ALOGV("Received EOS event for main input\n");
    }

exit:
    if (NULL != buffer &&  NULL != buffer->common_params.data) {
        free( buffer->common_params.data);
        buffer->common_params.data = NULL;
        free( buffer);
        buffer = NULL;
    }
    qap_module_deinit(qap_module_handle);
    fprintf(stdout, "%s::%d , THREAD EXIT \n", __func__, __LINE__);
    ALOGD("%s::%d , THREAD EXIT \n", __func__, __LINE__);
    return NULL;
}

qap_module_handle_t qap_wrapper_stream_open(void* stream_data)
{
    qap_module_config_t *input_config = NULL;
    int ret = 0;
    int i = 0;
    stream_config *stream_info = (stream_config *)stream_data;
    qap_module_handle_t qap_module_handle = NULL;

    input_config = (qap_module_config_t *) calloc(1, sizeof(qap_module_config_t));
    if (input_config == NULL) {
        fprintf(stderr, "%s::%d Memory Alloc Error\n", __func__, __LINE__);
        return NULL;
    }
    input_config->sample_rate = stream_info->config.sample_rate;
    input_config->channels = stream_info->channels;
    input_config->bit_width = stream_info->config.offload_info.bit_width;

    stream_info->bytes_to_read = 1024;
    input_streams_count++;
    if (input_streams_count == 2) {
        if (stream_info->filetype == FILE_WAV) {
            input_config->flags = QAP_MODULE_FLAG_SYSTEM_SOUND;
            stream_info->system_input = true;
            has_system_input = true;
            ALOGV("%s::%d Set Secondary System Sound Flag", __func__, __LINE__);
        } else if (stream_info->filetype != FILE_WAV) {
            if (stream_info->flags & AUDIO_OUTPUT_FLAG_ASSOCIATED) {
                 ALOGV("%s::%d Set Secondary Assoc Input Flag", __func__, __LINE__);
                 input_config->flags = QAP_MODULE_FLAG_SECONDARY;
                 stream_info->sec_input = true;
            } else {
                ALOGV("%s::%d Set Secondary Main Input Flag", __func__, __LINE__);
                input_config->flags = QAP_MODULE_FLAG_PRIMARY;
                stream_info->sec_input = true;
            }
        }
        stream_info->bytes_to_read = 2048;
    } else {
        if (stream_info->filetype == FILE_WAV) {
            ALOGV("%s::%d Set Secondary System Sound Flag", __func__, __LINE__);
            input_config->flags = QAP_MODULE_FLAG_SYSTEM_SOUND;
            stream_info->system_input = true;
        } else {
            ALOGV("%s::%d Set Primary Main Input Flag", __func__, __LINE__);
            input_config->flags = QAP_MODULE_FLAG_PRIMARY;
        }
    }

    if (!encode)
        input_config->module_type = QAP_MODULE_DECODER;
    else
        input_config->module_type = QAP_MODULE_ENCODER;

    ret = qap_wrapper_map_input_format(stream_info->config.offload_info.format, &input_config->format);
    if (ret == -EINVAL)
        return NULL;

    if (!session_output_configured) {
        session_output_config.output_config->channels = input_config->channels;
        session_output_config.output_config->sample_rate = input_config->sample_rate;
        output_device_id = stream_info->output_device;
        if (output_device_id == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
            output_device_id = AUDIO_DEVICE_OUT_SPEAKER;
            play_through_bt = true;
        }
        if (output_device_id == AUDIO_DEVICE_OUT_LINE) {
            output_device_id = AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
        }
        session_output_config.output_config->id = output_device_id;
        if (stream_info->filetype != FILE_DTS)
            session_output_config.output_config->bit_width = input_config->bit_width;
        session_output_config.output_config->is_interleaved = input_config->is_interleaved;
        session_output_config.num_output = 1;

        ret = qap_session_cmd(qap_session_handle, QAP_SESSION_CMD_SET_OUTPUTS, sizeof(session_output_config), &session_output_config, NULL, NULL);
        if (ret != QAP_STATUS_OK) {
            fprintf(stderr, "Output config failed\n");
            return NULL;
        }

        if (stream_info->filetype != FILE_DTS) {
            ALOGV("Session set params %s", session_kv_pairs);
            ret = qap_session_cmd(qap_session_handle, QAP_SESSION_CMD_SET_KVPAIRS, sizeof(session_kv_pairs), session_kv_pairs, NULL, NULL);
            if (ret != QAP_STATUS_OK) {
                fprintf(stderr, "Session set params failed\n");
                return NULL;
            }
        }
        session_output_configured = true;
    }

    ret = qap_module_init(qap_session_handle, input_config, &qap_module_handle);
    if (qap_module_handle == NULL) {
        fprintf(stderr, "%s Module Handle is Null\n", __func__);
        return NULL;
    }

    return qap_module_handle;

}

void hal_test_qap_usage() {
    printf(" \n qap commands \n");
    printf(" -qap                                      - Enabling playback through QAP for nun tunnel decoding mode\n");
    printf(" -bd                                       - Enabling Broadcast Decode/Encode session through QAP\n");
    printf(" -broadcast                                - Enabling playback through QAP for nun tunnel decoding mode\n");
    printf(" -y  --timestamp filename                  - Input timestamp file to be used to send timestamp and bytes to be read from main input file.\n");
    printf(" -z  --framesize filename                  - Input framesize file to be used to send bytes to be read from main input file.\n");
    printf(" hal_play_test -qap -broadcast -f /data/5ch_dd_25fps_channeld_id.ac3 -t 9 -d 2 -v 0.01 -r 48000 -c 6 \n");
    printf("                                          -> plays AC3 stream(-t = 9) on speaker device(-d = 2)\n");
    printf("                                          -> 6 channels and 48000 sample rate\n\n");
    printf("                                          -> using QAP with Broadcast session\n\n");
    printf(" hal_play_test -qap -bd -f /data/200_48_16_ieq_mix_voice_40s.ec3 -t 11 -d 2 -v 0.01 -r 48000 -c 2 \n");
    printf("                                          -> plays EAC3 stream(-t = 11) on speaker device(-d = 2)\n");
    printf("                                          -> 2 channels and 48000 sample rate\n\n");
    printf("                                          -> using QAP with Bluray session\n\n");
}
