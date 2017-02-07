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

/* Test app to play audio at the HAL layer */

#include <getopt.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <cutils/str_parms.h>
#include <tinyalsa/asoundlib.h>
#include "qahw_api.h"
#include "qahw_defs.h"
#include "qahw_effect_api.h"
#include "qahw_effect_test.h"

#define nullptr NULL

#define LATENCY_NODE "/sys/kernel/debug/audio_out_latency_measurement_node"
#define LATENCY_NODE_INIT_STR "1"

#define AFE_PROXY_SAMPLING_RATE 48000
#define AFE_PROXY_CHANNEL_COUNT 2

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define KV_PAIR_MAX_LENGTH  1000

#define FORMAT_PCM 1
#define WAV_HEADER_LENGTH_MAX 46

#define MAX_PLAYBACK_STREAMS   2
#define PRIMARY_STREAM_INDEX   0

#define KVPAIRS_MAX 100
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[1]))

static int get_wav_header_length (FILE* file_stream);
static void init_streams(void);


enum {
    FILE_WAV = 1,
    FILE_MP3,
    FILE_AAC,
    FILE_AAC_ADTS,
    FILE_FLAC,
    FILE_ALAC,
    FILE_VORBIS,
    FILE_WMA,
    FILE_AC3,
    FILE_AAC_LATM,
    FILE_EAC3,
    FILE_EAC3_JOC,
    FILE_DTS,
    FILE_MP2,
    FILE_APTX
};

typedef enum {
    USB_MODE_DEVICE,
    USB_MODE_HOST,
    USB_MODE_NONE
} usb_mode_type_t;

typedef enum {
    AAC_LC = 1,
    AAC_HE_V1,
    AAC_HE_V2
} aac_format_type_t;

typedef enum {
    WMA = 1,
    WMA_PRO,
    WMA_LOSSLESS
} wma_format_type_t;

struct wav_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t riff_fmt;
    uint32_t fmt_id;
    uint32_t fmt_sz;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_sz;
};

struct audio_config_params {
    qahw_module_handle_t *qahw_mod_handle;
    audio_io_handle_t handle;
    audio_devices_t input_device;
    audio_config_t config;
    audio_input_flags_t flags;
    const char* kStreamName ;
    audio_source_t kInputSource;
    char *file_name;
    volatile bool thread_exit;
};

struct proxy_data {
    struct audio_config_params acp;
    struct wav_header hdr;
};

struct drift_data {
    qahw_module_handle_t *out_handle;
    volatile bool thread_exit;
};

typedef struct {
    qahw_module_handle_t *qahw_in_hal_handle;
    qahw_module_handle_t *qahw_out_hal_handle;
    audio_io_handle_t handle;
    char* filename;
    FILE* file_stream;
    int filetype;
    int stream_index;
    audio_devices_t output_device;
    audio_devices_t input_device;
    audio_config_t config;
    audio_output_flags_t flags;
    qahw_stream_handle_t* out_handle;
    qahw_stream_handle_t* in_handle;
    int channels;
    aac_format_type_t aac_fmt_type;
    wma_format_type_t wma_fmt_type;
    char *kvpair_values;
    bool flags_set;
    usb_mode_type_t usb_mode;
    int effect_index;
    bool drift_query;
    char *device_url;
    thread_func_t ethread_func;
    thread_data_t *ethread_data;
    cmd_data_t cmd_data;
    pthread_cond_t write_cond;
    pthread_mutex_t write_lock;
    pthread_cond_t drain_cond;
    pthread_mutex_t drain_lock;
}stream_config;


qahw_module_handle_t *primary_hal_handle = NULL;
qahw_module_handle_t *usb_hal_handle = NULL;
qahw_module_handle_t *bt_hal_handle = NULL;


FILE * log_file = NULL;
volatile bool stop_playback = false;
const char *log_filename = NULL;
float vol_level = 0.01;
struct proxy_data proxy_params;
pthread_t playback_thread[MAX_PLAYBACK_STREAMS];
bool thread_active[MAX_PLAYBACK_STREAMS] = { false };

stream_config stream_param[MAX_PLAYBACK_STREAMS];
bool kpi_mode;

/*
 * Set to a high number so it doesn't interfere with existing stream handles
 */
audio_io_handle_t stream_handle = 0x999;

#define FLAC_KVPAIR "music_offload_avg_bit_rate=%d;" \
                    "music_offload_flac_max_blk_size=%d;" \
                    "music_offload_flac_max_frame_size=%d;" \
                    "music_offload_flac_min_blk_size=%d;" \
                    "music_offload_flac_min_frame_size=%d;" \
                    "music_offload_sample_rate=%d;"

#define ALAC_KVPAIR "music_offload_alac_avg_bit_rate=%d;" \
                    "music_offload_alac_bit_depth=%d;" \
                    "music_offload_alac_channel_layout_tag=%d;" \
                    "music_offload_alac_compatible_version=%d;" \
                    "music_offload_alac_frame_length=%d;" \
                    "music_offload_alac_kb=%d;" \
                    "music_offload_alac_max_frame_bytes=%d;" \
                    "music_offload_alac_max_run=%d;" \
                    "music_offload_alac_mb=%d;" \
                    "music_offload_alac_num_channels=%d;" \
                    "music_offload_alac_pb=%d;" \
                    "music_offload_alac_sampling_rate=%d;" \
                    "music_offload_avg_bit_rate=%d;" \
                    "music_offload_sample_rate=%d;"

#define VORBIS_KVPAIR "music_offload_avg_bit_rate=%d;" \
                      "music_offload_sample_rate=%d;" \
                      "music_offload_vorbis_bitstream_fmt=%d;"

#define WMA_KVPAIR "music_offload_avg_bit_rate=%d;" \
                   "music_offload_sample_rate=%d;" \
                   "music_offload_wma_bit_per_sample=%d;" \
                   "music_offload_wma_block_align=%d;" \
                   "music_offload_wma_channel_mask=%d;" \
                   "music_offload_wma_encode_option=%d;" \
                   "music_offload_wma_encode_option1=%d;" \
                   "music_offload_wma_encode_option2=%d;" \
                   "music_offload_wma_format_tag=%d;"

void stop_signal_handler(int signal __unused)
{
   stop_playback = true;
}

void usage();
int measure_kpi_values(qahw_stream_handle_t* out_handle, bool is_offload);

static void init_streams(void)
{
    int i = 0;
    for ( i = 0; i < MAX_PLAYBACK_STREAMS; i++) {
        memset(&stream_param[i], 0, sizeof(stream_config));

        stream_param[i].qahw_out_hal_handle                 =   nullptr;
        stream_param[i].qahw_in_hal_handle                  =   nullptr;
        stream_param[i].filename                            =   nullptr;
        stream_param[i].file_stream                         =   nullptr;
        stream_param[i].filetype                            =   FILE_WAV;
        stream_param[i].stream_index                        =   i+1;
        stream_param[i].output_device                       =   AUDIO_DEVICE_OUT_SPEAKER;
        stream_param[i].input_device                        =   AUDIO_DEVICE_NONE;
        stream_param[i].flags                               =   AUDIO_OUTPUT_FLAG_NONE;
        stream_param[i].out_handle                          =   nullptr;
        stream_param[i].in_handle                           =   nullptr;
        stream_param[i].channels                            =   2;
        stream_param[i].config.offload_info.sample_rate     =   44100;
        stream_param[i].config.offload_info.bit_width       =   16;
        stream_param[i].aac_fmt_type                        =   AAC_LC;
        stream_param[i].wma_fmt_type                        =   WMA;
        stream_param[i].kvpair_values                       =   nullptr;
        stream_param[i].flags_set                           =   false;
        stream_param[i].usb_mode                            =   USB_MODE_DEVICE;
        stream_param[i].effect_index                        =   -1;
        stream_param[i].ethread_func                        =   nullptr;
        stream_param[i].ethread_data                        =   nullptr;
        stream_param[i].device_url                          =   "stream";

        pthread_mutex_init(&stream_param[i].write_lock, (const pthread_mutexattr_t *)NULL);
        pthread_cond_init(&stream_param[i].write_cond, (const pthread_condattr_t *) NULL);
        pthread_mutex_init(&stream_param[i].drain_lock, (const pthread_mutexattr_t *)NULL);
        pthread_cond_init(&stream_param[i].drain_cond, (const pthread_condattr_t *) NULL);

        stream_param[i].handle                              =   stream_handle;
        stream_handle--;
    }
}

void read_kvpair(char *kvpair, char* kvpair_values, int filetype)
{
    char *kvpair_type = NULL;
    char *token = NULL;
    int value = 0;
    int len = 0;
    int size = 0;

    switch (filetype) {
    case FILE_FLAC:
        kvpair_type = FLAC_KVPAIR;
        break;
    case FILE_ALAC:
        kvpair_type = ALAC_KVPAIR;
        break;
    case FILE_VORBIS:
        kvpair_type = VORBIS_KVPAIR;
        break;
    case FILE_WMA:
        kvpair_type = WMA_KVPAIR;
        break;
    default:
        break;
    }

    if (kvpair_type) {
        token = strtok(kvpair_values, ",");
        while (token) {
            len = strcspn(kvpair_type, "=");
            size = len + strlen(token) + 2;
            value = atoi(token);
            snprintf(kvpair, size, kvpair_type, value);
            kvpair += size - 1;
            kvpair_type += len + 3;
            token = strtok(NULL, ",");
        }
    }
}

int async_callback(qahw_stream_callback_event_t event, void *param __unused,
                  void *cookie)
{
    if(cookie == NULL) {
        fprintf(log_file, "Invalid callback handle\n");
        fprintf(stderr, "Invalid callback handle\n");
        return 0;
    }

    stream_config *params = (stream_config*) cookie;

    switch (event) {
    case QAHW_STREAM_CBK_EVENT_WRITE_READY:
        fprintf(log_file, "stream %d: received event - QAHW_STREAM_CBK_EVENT_WRITE_READY\n", params->stream_index);
        pthread_mutex_lock(&params->write_lock);
        pthread_cond_signal(&params->write_cond);
        pthread_mutex_unlock(&params->write_lock);
        break;
    case QAHW_STREAM_CBK_EVENT_DRAIN_READY:
        fprintf(log_file, "stream %d: received event - QAHW_STREAM_CBK_EVENT_DRAIN_READY\n", params->stream_index);
        pthread_mutex_lock(&params->drain_lock);
        pthread_cond_signal(&params->drain_cond);
        pthread_mutex_unlock(&params->drain_lock);
    default:
        break;
    }
    return 0;
}

void *proxy_read (void* data)
{
    struct proxy_data* params = (struct proxy_data*) data;
    qahw_module_handle_t *qahw_mod_handle = params->acp.qahw_mod_handle;
    qahw_in_buffer_t in_buf;
    char *buffer;
    int rc = 0;
    int bytes_to_read, bytes_written = 0;
    FILE *fp = NULL;
    qahw_stream_handle_t* in_handle = nullptr;

    rc = qahw_open_input_stream(qahw_mod_handle, params->acp.handle,
              params->acp.input_device, &params->acp.config, &in_handle,
              params->acp.flags, params->acp.kStreamName, params->acp.kInputSource);
    if (rc) {
        fprintf(log_file, "Could not open input stream %d \n",rc);
        fprintf(stderr, "Could not open input stream %d \n",rc);
        pthread_exit(0);
     }

    if (in_handle != NULL) {
        bytes_to_read = qahw_in_get_buffer_size(in_handle);
        buffer = (char *) calloc(1, bytes_to_read);
        if (buffer == NULL) {
            fprintf(log_file, "calloc failed!!\n");
            fprintf(stderr, "calloc failed!!\n");
            pthread_exit(0);
        }

        if ((fp = fopen(params->acp.file_name,"w"))== NULL) {
            fprintf(log_file, "Cannot open file to dump proxy data\n");
            fprintf(stderr, "Cannot open file to dump proxy data\n");
            pthread_exit(0);
        }
        else {
          params->hdr.num_channels = audio_channel_count_from_in_mask(params->acp.config.channel_mask);
          params->hdr.sample_rate = params->acp.config.sample_rate;
          params->hdr.byte_rate = params->hdr.sample_rate * params->hdr.num_channels * 2;
          params->hdr.block_align = params->hdr.num_channels * 2;
          params->hdr.bits_per_sample = 16;
          fwrite(&params->hdr, 1, sizeof(params->hdr), fp);
        }
        memset(&in_buf,0, sizeof(qahw_in_buffer_t));
        in_buf.buffer = buffer;
        in_buf.bytes = bytes_to_read;

        while (!(params->acp.thread_exit)) {
            rc = qahw_in_read(in_handle, &in_buf);
            if (rc > 0) {
                bytes_written += fwrite((char *)(in_buf.buffer), sizeof(char), (int)in_buf.bytes, fp);
            }
        }
        params->hdr.data_sz = bytes_written;
        params->hdr.riff_sz = bytes_written + 36; //sizeof(hdr) - sizeof(riff_id) - sizeof(riff_sz)
        fseek(fp, 0L , SEEK_SET);
        fwrite(&params->hdr, 1, sizeof(params->hdr), fp);
        fclose(fp);
        rc = qahw_in_standby(in_handle);
        if (rc) {
            fprintf(log_file, "in standby failed %d \n", rc);
            fprintf(stderr, "in standby failed %d \n", rc);
        }
        rc = qahw_close_input_stream(in_handle);
        if (rc) {
            fprintf(log_file, "could not close input stream %d \n", rc);
            fprintf(stderr, "could not close input stream %d \n", rc);
        }
        fprintf(log_file, "pcm data saved to file %s", params->acp.file_name);
    }
    return 0;
}

void *drift_read(void* data)
{
    struct drift_data* params = (struct drift_data*) data;
    qahw_stream_handle_t* out_handle = params->out_handle;
    struct qahw_avt_device_drift_param drift_param;
    int rc = -EINVAL;

    printf("drift quried at 100ms interval \n");
    while (!(params->thread_exit)) {
        memset(&drift_param, 0, sizeof(struct qahw_avt_device_drift_param));
        rc = qahw_out_get_param_data(out_handle, QAHW_PARAM_AVT_DEVICE_DRIFT,
                (qahw_param_payload *)&drift_param);
        if (!rc) {
            printf("resync flag = %d, drift %d, av timer %lld\n",
                    drift_param.resync_flag,
                    drift_param.avt_device_drift_value,
                    drift_param.ref_timer_abs_ts);
        } else {
            printf("drift query failed rc = %d retry after 100ms\n", rc);
        }

        usleep(100000);
    }
}
static int is_eof(stream_config *stream) {
    if (stream->filename) {
        if (feof(stream->file_stream)) {
            fprintf(log_file, "stream %d: error in fread, error %d\n", stream->stream_index, ferror(stream->file_stream));
            fprintf(stderr, "stream %d: error in fread, error %d\n", stream->stream_index, ferror(stream->file_stream));
            return true;
        }
    } else if (AUDIO_DEVICE_NONE != stream->input_device)
        /*
         * assuming this is called after we got -ve bytes value from hal read
         */
        return true;
    return false;
}
static int read_bytes(stream_config *stream, void *buff, int size) {
    if (stream->filename)
        return fread(buff, 1, size, stream->file_stream);
    else if (AUDIO_DEVICE_NONE != stream->input_device) {
        qahw_in_buffer_t in_buf;
        memset(&in_buf,0, sizeof(qahw_in_buffer_t));
        in_buf.buffer = buff;
        in_buf.bytes = size;
        return qahw_in_read(stream->in_handle, &in_buf);
    }

}

/* Entry point function for stream playback
 * Opens the stream
 * Reads KV pairs, sets volume, allocates input buffer
 * Opens proxy and effects threads if enabled
 * Starts freading the file and writing to HAL
 * Drains out and close the stream after EOF
 */
void *start_stream_playback (void* stream_data)
{
    int rc = 0;
    stream_config *params = (stream_config*) stream_data;
    bool proxy_thread_active = false;
    pthread_t proxy_thread;

    bool drift_thread_active = false;
    pthread_t drift_query_thread;
    struct drift_data drift_params;

    if (params->output_device & AUDIO_DEVICE_OUT_ALL_A2DP)
        params->output_device = AUDIO_DEVICE_OUT_PROXY;
    rc = qahw_open_output_stream(params->qahw_out_hal_handle,
                             params->handle,
                             params->output_device,
                             params->flags,
                             &(params->config),
                             &(params->out_handle),
                             params->device_url);

    if (rc) {
        fprintf(log_file, "stream %d: could not open output stream, error - %d \n", params->stream_index, rc);
        fprintf(stderr, "stream %d: could not open output stream, error - %d \n", params->stream_index, rc);
        pthread_exit(0);
    }

    fprintf(log_file, "stream %d: open output stream is success, out_handle %p\n", params->stream_index, params->out_handle);

    if (kpi_mode == true) {
        measure_kpi_values(params->out_handle, params->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD);
        rc = qahw_close_output_stream(params->out_handle);
        if (rc) {
            fprintf(log_file, "stream %d: could not close output stream %d, error - %d \n", params->stream_index, rc);
            fprintf(stderr, "stream %d: could not close output stream %d, error - %d \n", params->stream_index, rc);
        }
        return;
    }

    switch(params->filetype) {
        char kvpair[KV_PAIR_MAX_LENGTH] = {0};
        case FILE_WMA:
        case FILE_VORBIS:
        case FILE_ALAC:
        case FILE_FLAC:
            fprintf(log_file, "%s:calling setparam for kvpairs\n", __func__);
            if (!(params->kvpair_values)) {
               fprintf(log_file, "stream %d: error!!No metadata for the clip\n", params->stream_index);
               fprintf(stderr, "stream %d: error!!No metadata for the clip\n", params->stream_index);
               pthread_exit(0);;
            }
            read_kvpair(kvpair, params->kvpair_values, params->filetype);
            rc = qahw_out_set_parameters(params->out_handle, kvpair);
            if(rc){
                fprintf(log_file, "stream %d: failed to set kvpairs\n", params->stream_index);
                fprintf(stderr, "stream %d: failed to set kvpairs\n", params->stream_index);
                pthread_exit(0);;
            }
            fprintf(log_file, "stream %d: kvpairs are set\n", params->stream_index);
            break;
        default:
            break;
    }

    int offset = 0;
    bool is_offload = params->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD;
    size_t bytes_wanted = 0;
    size_t write_length = 0;
    size_t bytes_remaining = 0;
    size_t bytes_written = 0;
    size_t bytes_read = 0;
    char  *data_ptr = NULL;
    bool exit = false;

    if (is_offload) {
        fprintf(log_file, "stream %d: set callback for offload stream for playback usecase\n", params->stream_index);
        qahw_out_set_callback(params->out_handle, async_callback, params);
    }

    // create effect thread, use thread_data to transfer command
    if (params->ethread_func &&
            (params->flags & (AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_DIRECT))) {

        fprintf(log_file, "stream %d: effect type:%s\n", params->stream_index, effect_str[params->effect_index]);
        params->ethread_data = create_effect_thread(params->effect_index, params->ethread_func);

        // create effect command thread
        params->cmd_data.exit = false;
        params->cmd_data.fx_data_ptr = &(params->ethread_data);
        pthread_attr_init(&(params->cmd_data.attr));
        pthread_attr_setdetachstate(&(params->cmd_data.attr), PTHREAD_CREATE_JOINABLE);
        rc = pthread_create(&(params->cmd_data.cmd_thread), &(params->cmd_data.attr),
                &command_thread_func, &(params->cmd_data));
        if (rc < 0) {
            fprintf(log_file, "stream %d: could not create effect command thread!\n", params->stream_index);
            fprintf(stderr, "stream %d: could not create effect command thread!\n", params->stream_index);
            pthread_exit(0);
        }

        fprintf(log_file, "stream %d: loading effects\n", params->stream_index);
        if (params->ethread_data != nullptr) {
            // load effect module
            notify_effect_command(params->ethread_data, EFFECT_LOAD_LIB, -1, 0, NULL);

            // get effect desc
            notify_effect_command(params->ethread_data, EFFECT_GET_DESC, -1, 0, NULL);

            // create effect
            params->ethread_data->io_handle = params->handle;
            notify_effect_command(params->ethread_data, EFFECT_CREATE, -1, 0, NULL);

            // broadcast device info
            notify_effect_command(params->ethread_data, EFFECT_CMD, QAHW_EFFECT_CMD_SET_DEVICE, sizeof(audio_devices_t), &(params->output_device));

            // enable effect
            notify_effect_command(params->ethread_data, EFFECT_CMD, QAHW_EFFECT_CMD_ENABLE, 0, NULL);
        }
    }

    if (params->output_device & AUDIO_DEVICE_OUT_PROXY) {
        proxy_params.acp.qahw_mod_handle = params->qahw_out_hal_handle;
        proxy_params.acp.handle = stream_handle;
        stream_handle--;
        proxy_params.acp.input_device = AUDIO_DEVICE_IN_PROXY;
        proxy_params.acp.flags = AUDIO_INPUT_FLAG_NONE;
        proxy_params.acp.config.channel_mask = audio_channel_in_mask_from_count(AFE_PROXY_CHANNEL_COUNT);
        proxy_params.acp.config.sample_rate = AFE_PROXY_SAMPLING_RATE;
        proxy_params.acp.config.format = AUDIO_FORMAT_PCM_16_BIT;
        proxy_params.acp.kStreamName = "input_stream";
        proxy_params.acp.kInputSource = AUDIO_SOURCE_UNPROCESSED;
        proxy_params.acp.thread_exit = false;
        fprintf(log_file, "stream %d: create thread to read data from proxy\n", params->stream_index);
        rc = pthread_create(&proxy_thread, NULL, proxy_read, (void *)&proxy_params);
        if (!rc)
            proxy_thread_active = true;
    } else if (params->drift_query &&
              (params->output_device & AUDIO_DEVICE_OUT_HDMI) &&
              !drift_thread_active) {
        drift_params.out_handle = params->out_handle;
        drift_params.thread_exit = false;
        fprintf(log_file, "create thread to read avtime vs hdmi drift\n");
        rc = pthread_create(&drift_query_thread, NULL, drift_read, (void *)&drift_params);
        if (!rc)
            drift_thread_active = true;
        else
            fprintf(log_file, "drift query thread creation failure %d\n", rc);
    }

    rc = qahw_out_set_volume(params->out_handle, vol_level, vol_level);
    if (rc < 0) {
        fprintf(log_file, "stream %d: unable to set volume\n", params->stream_index);
        fprintf(stderr, "stream %d: unable to set volume\n", params->stream_index);
    }

    bytes_wanted = qahw_out_get_buffer_size(params->out_handle);
    data_ptr = (char *) malloc (bytes_wanted);
    if (data_ptr == NULL) {
        fprintf(log_file, "stream %d: failed to allocate data buffer\n", params->stream_index);
        fprintf(stderr, "stream %d: failed to allocate data buffer\n", params->stream_index);
        pthread_exit(0);
    }

    while (!exit && !stop_playback) {
        if (!bytes_remaining) {
            fprintf(log_file, "\nstream %d: reading bytes %zd\n", params->stream_index, bytes_wanted);
            bytes_read = read_bytes(params, data_ptr, bytes_wanted);
            fprintf(log_file, "stream %d: read bytes %zd\n", params->stream_index, bytes_read);
            if (bytes_read <= 0) {
                if (is_eof(params)) {
                    fprintf(log_file, "stream %d: end of file\n", params->stream_index);
                    if (is_offload) {
                        pthread_mutex_lock(&params->drain_lock);
                        qahw_out_drain(params->out_handle, QAHW_DRAIN_ALL);
                        pthread_cond_wait(&params->drain_cond, &params->drain_lock);
                        fprintf(log_file, "stream %d: out of compress drain\n", params->stream_index);
                        fprintf(log_file, "stream %d: playback completed successfully\n", params->stream_index);
                        pthread_mutex_unlock(&params->drain_lock);
                    }
                }
                exit = true;
                continue;
            }
            bytes_remaining = write_length = bytes_read;
        }

        offset = write_length - bytes_remaining;
        fprintf(log_file, "stream %d: writing to hal %zd bytes, offset %d, write length %zd\n",
                params->stream_index, bytes_remaining, offset, write_length);
        bytes_written = write_to_hal(params->out_handle, data_ptr+offset, bytes_remaining, params);
        bytes_remaining -= bytes_written;
        fprintf(log_file, "stream %d: bytes_written %zd, bytes_remaining %zd\n",
                params->stream_index, bytes_written, bytes_remaining);
    }

    if (params->ethread_data != nullptr) {
        fprintf(log_file, "stream %d: un-loading effects\n", params->stream_index);
        // disable effect
        notify_effect_command(params->ethread_data, EFFECT_CMD, QAHW_EFFECT_CMD_DISABLE, 0, NULL);

        // release effect
        notify_effect_command(params->ethread_data, EFFECT_RELEASE, -1, 0, NULL);

        // unload effect module
        notify_effect_command(params->ethread_data, EFFECT_UNLOAD_LIB, -1, 0, NULL);

        // destroy effect thread
        destroy_effect_thread(params->ethread_data);

        free(params->ethread_data);
        params->ethread_data = NULL;

        // destory effect command thread
        params->cmd_data.exit = true;
        usleep(100000);  // give a chance for thread to exit gracefully
        rc = pthread_cancel(params->cmd_data.cmd_thread);
        if (rc != 0) {
            fprintf(log_file, "Fail to cancel thread!\n");
            fprintf(stderr, "Fail to cancel thread!\n");
        }
        rc = pthread_join(params->cmd_data.cmd_thread, NULL);
        if (rc < 0) {
            fprintf(log_file, "Fail to join effect command thread!\n");
            fprintf(stderr, "Fail to join effect command thread!\n");
        }
    }

    if (proxy_thread_active) {
       /*
        * DSP gives drain ack for last buffer which will close proxy thread before
        * app reads last buffer. So add sleep before exiting proxy thread to read
        * last buffer of data. This is not a calculated value.
        */
        usleep(500000);
        proxy_params.acp.thread_exit = true;
        fprintf(log_file, "wait for proxy thread exit\n");
        pthread_join(proxy_thread, NULL);
    }

    if (drift_thread_active) {
        usleep(500000);
        drift_params.thread_exit = true;
        pthread_join(drift_query_thread, NULL);
    }
    rc = qahw_out_standby(params->out_handle);
    if (rc) {
        fprintf(log_file, "stream %d: out standby failed %d \n", params->stream_index, rc);
        fprintf(stderr, "stream %d: out standby failed %d \n", params->stream_index, rc);
    }

    fprintf(log_file, "stream %d: closing output stream\n", params->stream_index);
    rc = qahw_close_output_stream(params->out_handle);
    if (rc) {
        fprintf(log_file, "stream %d: could not close output stream, error - %d \n", params->stream_index, rc);
        fprintf(stderr, "stream %d: could not close output stream, error - %d \n", params->stream_index, rc);
    }

    if (data_ptr)
        free(data_ptr);

    fprintf(log_file, "stream %d: stream closed\n", params->stream_index);
    return;

}

int write_to_hal(qahw_stream_handle_t* out_handle, char *data, size_t bytes, void *params_ptr)
{
    stream_config *stream_params = (stream_config*) params_ptr;

    ssize_t ret;
    pthread_mutex_lock(&stream_params->write_lock);
    qahw_out_buffer_t out_buf;

    memset(&out_buf,0, sizeof(qahw_out_buffer_t));
    out_buf.buffer = data;
    out_buf.bytes = bytes;

    ret = qahw_out_write(out_handle, &out_buf);
    if (ret < 0) {
        fprintf(log_file, "stream %d: writing data to hal failed (ret = %zd)\n", stream_params->stream_index, ret);
    } else if (ret != bytes) {
        fprintf(log_file, "stream %d: provided bytes %zd, written bytes %d\n",stream_params->stream_index, bytes, ret);
        fprintf(log_file, "stream %d: waiting for event write ready\n", stream_params->stream_index);
        pthread_cond_wait(&stream_params->write_cond, &stream_params->write_lock);
        fprintf(log_file, "stream %d: out of wait for event write ready\n", stream_params->stream_index);
    }

    pthread_mutex_unlock(&stream_params->write_lock);
    return ret;
}

bool is_valid_aac_format_type(aac_format_type_t format_type)
{
    bool valid_format_type = false;

    switch (format_type) {
    case AAC_LC:
    case AAC_HE_V1:
    case AAC_HE_V2:
        valid_format_type = true;
        break;
    default:
        break;
    }
    return valid_format_type;
}

/*
 * Obtain aac format (refer audio.h) for format type entered.
 */

audio_format_t get_aac_format(int filetype, aac_format_type_t format_type)
{
    audio_format_t aac_format = AUDIO_FORMAT_AAC_ADTS_LC; /* default aac frmt*/

    if (filetype == FILE_AAC_ADTS) {
        switch (format_type) {
        case AAC_LC:
            aac_format = AUDIO_FORMAT_AAC_ADTS_LC;
            break;
        case AAC_HE_V1:
            aac_format = AUDIO_FORMAT_AAC_ADTS_HE_V1;
            break;
        case AAC_HE_V2:
            aac_format = AUDIO_FORMAT_AAC_ADTS_HE_V2;
            break;
        default:
            break;
        }
    } else if (filetype == FILE_AAC) {
        switch (format_type) {
        case AAC_LC:
            aac_format = AUDIO_FORMAT_AAC_LC;
            break;
        case AAC_HE_V1:
            aac_format = AUDIO_FORMAT_AAC_HE_V1;
            break;
        case AAC_HE_V2:
            aac_format = AUDIO_FORMAT_AAC_HE_V2;
            break;
        default:
            break;
        }
    } else if (filetype == FILE_AAC_LATM) {
        switch (format_type) {
        case AAC_LC:
            aac_format = AUDIO_FORMAT_AAC_LATM_LC;
            break;
        case AAC_HE_V1:
            aac_format = AUDIO_FORMAT_AAC_LATM_HE_V1;
            break;
        case AAC_HE_V2:
            aac_format = AUDIO_FORMAT_AAC_LATM_HE_V2;
            break;
        default:
            break;
        }
    } else {
        fprintf(log_file, "Invalid filetype provided %d\n", filetype);
        fprintf(stderr, "Invalid filetype provided %d\n", filetype);
    }

    fprintf(log_file, "aac format %d\n", aac_format);
    return aac_format;
}

static void get_file_format(stream_config *stream_info)
{
    int rc = 0;

    if (!(stream_info->flags_set)) {
        stream_info->flags = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_NON_BLOCKING;
        stream_info->flags |= AUDIO_OUTPUT_FLAG_DIRECT;
    }

    char header[WAV_HEADER_LENGTH_MAX] = {0};
    int wav_header_len = 0;

    switch (stream_info->filetype) {
        case FILE_WAV:
            /*
             * Read the wave header
             */
            if((wav_header_len = get_wav_header_length(stream_info->file_stream)) <= 0) {
                fprintf(log_file, "wav header length is invalid:%d\n", wav_header_len);
                exit(1);
            }
            fseek(stream_info->file_stream, 0, SEEK_SET);
            rc = fread (header, wav_header_len , 1, stream_info->file_stream);
            if (rc != 1) {
               fprintf(log_file, "Error fread failed\n");
               fprintf(stderr, "Error fread failed\n");
               exit(1);
            }
            if (strncmp (header, "RIFF", 4) && strncmp (header+8, "WAVE", 4)) {
               fprintf(log_file, "Not a wave format\n");
               fprintf(stderr, "Not a wave format\n");
               exit (1);
            }
            memcpy (&stream_info->channels, &header[22], 2);
            memcpy (&stream_info->config.offload_info.sample_rate, &header[24], 4);
            memcpy (&stream_info->config.offload_info.bit_width, &header[34], 2);
            if (stream_info->config.offload_info.bit_width == 32)
                stream_info->config.offload_info.format = AUDIO_FORMAT_PCM_32_BIT;
            else if (stream_info->config.offload_info.bit_width == 24)
                stream_info->config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
            else
                stream_info->config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
            if (!(stream_info->flags_set))
                stream_info->flags = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD;
            break;

        case FILE_MP3:
            stream_info->config.offload_info.format = AUDIO_FORMAT_MP3;
            break;

        case FILE_AAC:
        case FILE_AAC_ADTS:
        case FILE_AAC_LATM:
            if (!is_valid_aac_format_type(stream_info->aac_fmt_type)) {
                fprintf(log_file, "Invalid format type for AAC %d\n", stream_info->aac_fmt_type);
                fprintf(stderr, "Invalid format type for AAC %d\n", stream_info->aac_fmt_type);
                return;
            }
            stream_info->config.offload_info.format = get_aac_format(stream_info->filetype, stream_info->aac_fmt_type);
            break;
        case FILE_FLAC:
            stream_info->config.offload_info.format = AUDIO_FORMAT_FLAC;
            break;
        case FILE_ALAC:
            stream_info->config.offload_info.format = AUDIO_FORMAT_ALAC;
            break;
        case FILE_VORBIS:
            stream_info->config.offload_info.format = AUDIO_FORMAT_VORBIS;
            break;
        case FILE_WMA:
            if (stream_info->wma_fmt_type == WMA)
               stream_info->config.offload_info.format = AUDIO_FORMAT_WMA;
            else
               stream_info->config.offload_info.format = AUDIO_FORMAT_WMA_PRO;
            break;
        case FILE_MP2:
            stream_info->config.offload_info.format = AUDIO_FORMAT_MP2;
            break;
        case FILE_AC3:
            stream_info->config.offload_info.format = AUDIO_FORMAT_AC3;
            break;
        case FILE_EAC3:
        case FILE_EAC3_JOC:
            stream_info->config.offload_info.format = AUDIO_FORMAT_E_AC3;
            break;
        case FILE_DTS:
            stream_info->config.offload_info.format = AUDIO_FORMAT_DTS;
            break;
        case FILE_APTX:
            stream_info->config.offload_info.format = AUDIO_FORMAT_APTX;
            break;
        default:
           fprintf(log_file, "Does not support given filetype\n");
           fprintf(stderr, "Does not support given filetype\n");
           usage();
           return;
    }
    stream_info->config.sample_rate = stream_info->config.offload_info.sample_rate;
    stream_info->config.format = stream_info->config.offload_info.format;
    stream_info->config.channel_mask = stream_info->config.offload_info.channel_mask = audio_channel_in_mask_from_count(stream_info->channels);
    return;
}

int measure_kpi_values(qahw_stream_handle_t* out_handle, bool is_offload) {
    int rc = 0;
    int offset = 0;
    size_t bytes_wanted = 0;
    size_t write_length = 0;
    size_t bytes_remaining = 0;
    size_t bytes_written = 0;
    char  *data = NULL;
    int ret = 0, count = 0;
    struct timespec ts_cold, ts_cont;
    uint64_t tcold, tcont, scold = 0, uscold = 0, scont = 0, uscont = 0;

    if (is_offload) {
        fprintf(log_file, "Set callback for offload stream in kpi mesaurement usecase\n");
        qahw_out_set_callback(out_handle, async_callback, &stream_param[PRIMARY_STREAM_INDEX]);
    }

    FILE *fd_latency_node = fopen(LATENCY_NODE, "r+");
    if (fd_latency_node) {
        ret = fwrite(LATENCY_NODE_INIT_STR, sizeof(LATENCY_NODE_INIT_STR), 1, fd_latency_node);
        if (ret<1)
            fprintf(log_file, "error(%d) writing to debug node!", ret);
            fprintf(stderr, "error(%d) writing to debug node!", ret);
        fflush(fd_latency_node);
    } else {
        fprintf(log_file, "debug node(%s) open failed!", LATENCY_NODE);
        fprintf(stderr, "debug node(%s) open failed!", LATENCY_NODE);
        return -1;
    }

    bytes_wanted = qahw_out_get_buffer_size(out_handle);
    data = (char *) calloc (1, bytes_wanted);
    if (data == NULL) {
        fprintf(log_file, "calloc failed!!\n");
        fprintf(stderr, "calloc failed!!\n");
        return -ENOMEM;
    }

    while (count < 64) {
        if (!bytes_remaining) {
            bytes_remaining = write_length = bytes_wanted;
        }
        if (count == 0) {
            ret = clock_gettime(CLOCK_REALTIME, &ts_cold);
            if (ret) {
                fprintf(log_file, "error(%d) fetching start time for cold latency", ret);
                fprintf(stderr, "error(%d) fetching start time for cold latency", ret);
                return -1;
            }
        } else if (count == 16) {
            int *d = (int *)data;
            d[0] = 0x01010000;
            ret = clock_gettime(CLOCK_REALTIME, &ts_cont);
            if (ret) {
                fprintf(log_file, "error(%d) fetching start time for continuous latency", ret);
                fprintf(stderr, "error(%d) fetching start time for continuous latency", ret);
                return -1;
            }
        }

        offset = write_length - bytes_remaining;
        bytes_written = write_to_hal(out_handle, data+offset, bytes_remaining, &stream_param[PRIMARY_STREAM_INDEX]);
        bytes_remaining -= bytes_written;
        fprintf(log_file, "bytes_written %zd, bytes_remaining %zd\n",
                bytes_written, bytes_remaining);

        if (count == 16) {
            int *i = (int *)data;
            i[0] = 0x00000000;
        }
        count++;
    }

    char latency_buf[200] = {0};
    fread((void *) latency_buf, 100, 1, fd_latency_node);
    fclose(fd_latency_node);
    sscanf(latency_buf, " %llu,%llu,%*llu,%*llu,%llu,%llu", &scold, &uscold, &scont, &uscont);
    tcold = scold*1000 - ts_cold.tv_sec*1000 + uscold/1000 - ts_cold.tv_nsec/1000000;
    tcont = scont*1000 - ts_cont.tv_sec*1000 + uscont/1000 - ts_cont.tv_nsec/1000000;
    fprintf(log_file, "\n values from debug node %s\n", latency_buf);
    fprintf(log_file, " cold latency %llums, continuous latency %llums,\n", tcold, tcont);
    fprintf(log_file, " **Note: please add DSP Pipe/PP latency numbers to this, for final latency values\n");
    return rc;
}

void parse_aptx_dec_bt_addr(char *value, struct qahw_aptx_dec_param *aptx_cfg)
{
    int ba[6];
    char *str, *tok;
    uint32_t addr[3];
    int i = 0;

    tok = strtok_r(value, ":", &str);
    while (tok != NULL) {
        ba[i] = strtol(tok, NULL, 16);
        i++;
        tok = strtok_r(NULL, ":", &str);
    }
    addr[0] = (ba[0] << 8) | ba[1];
    addr[1] = ba[2];
    addr[2] = (ba[3] << 16) | (ba[4] << 8) | ba[5];

    aptx_cfg->bt_addr.nap = addr[0];
    aptx_cfg->bt_addr.uap = addr[1];
    aptx_cfg->bt_addr.lap = addr[2];
}

typedef struct {
    char *string;
    int val;
} param_converter_type;

param_converter_type format_table[] = {
    {"AUDIO_FORMAT_PCM_16_BIT",        AUDIO_FORMAT_PCM_16_BIT},
    {"AUDIO_FORMAT_PCM_32_BIT",        AUDIO_FORMAT_PCM_32_BIT},
    {"AUDIO_FORMAT_PCM_8_BIT",         AUDIO_FORMAT_PCM_8_BIT},
    {"AUDIO_FORMAT_PCM_8_24_BIT",      AUDIO_FORMAT_PCM_8_24_BIT},
    {"AUDIO_FORMAT_PCM_24_BIT_PACKED", AUDIO_FORMAT_PCM_24_BIT_PACKED}
};


int rate_table[] = {48000, 44100};


static int get_kvpairs_string(char *kvpairs, const char *key, char *value) {
    struct str_parms *parms = NULL;

    if (!kvpairs)
        return -1;

    parms = str_parms_create_str(kvpairs);
    if (str_parms_get_str(parms, key, value, KVPAIRS_MAX) < 0)
        return -1;

    str_parms_destroy(parms);
    return 1;
}

static int get_pcm_format(char *kvpairs) {
    bool match = false;
    int i = 0;
    char value[KVPAIRS_MAX] = {0};

    if(!kvpairs)
        return -1;


    if (get_kvpairs_string(kvpairs, QAHW_PARAMETER_STREAM_SUP_FORMATS, value) < 0)
        return -1;

    fprintf(log_file, "formats=%s\n", value);

   /*
    * for now we assume usb hal/pcm device announces suport for one format ONLY
    */
    for (i = 0; i < sizeof(format_table); i++) {
        if(!strncmp(format_table[i].string, value, sizeof(value))) {
            match = true;
            break;
        }
    }

    if (match)
        return format_table[i].val;
    else
        return -1;

}


static int get_rate(char *kvpairs) {
    int match = false;
    int rate = 0;
    int i = 0;
    char value[KVPAIRS_MAX] = {0};

    if(!kvpairs)
        return -1;

    if (get_kvpairs_string(kvpairs, QAHW_PARAMETER_STREAM_SUP_SAMPLING_RATES, value) < 0)
        return -1;

    fprintf(log_file, "sample rates=%s\n", value);
   /*
    * for now we assume usb hal/pcm device announces suport for one rate ONLY
    */

    rate = atoi(value);
    for (i = 0; i < ARRAY_SIZE(rate_table); i++)
        if (rate_table[i] == rate)
            match = true;

    if (match)
        return rate;
    else
        return -1;
}


static int get_channels(char *kvpairs) {
    int ch = -1;
    char value[KVPAIRS_MAX] = {0};

    if(!kvpairs)
        return -1;

    if (get_kvpairs_string(kvpairs, QAHW_PARAMETER_STREAM_SUP_CHANNELS, value) < 0)
        return -1;

    fprintf(log_file, "channels=%s\n", value);

   /*
    * this is to work around a bug in usb hal which annouces support for stereo
    * though the pcm dev/host stream is mono.
    */
    if (strstr(value, "MONO"))
        ch = 1;
    else if (strstr(value, "STEREO"))
        ch = 2;

    return ch;
}


static int detect_stream_params(stream_config *stream) {
    bool detection_needed = false;
    bool is_usb_loopback = false;
    int direction = PCM_OUT;
    audio_devices_t dev = stream->input_device;

    int rc = 0;
    char *param_string = NULL;
    int ch = 0;

    if (AUDIO_DEVICE_IN_USB_DEVICE == stream->input_device ||
        AUDIO_DEVICE_OUT_USB_DEVICE == stream->output_device)
        if (USB_MODE_DEVICE == stream->usb_mode)
            detection_needed = true;

    if (!detection_needed)
    /*
     * we will go with given params through args or with default params.
     */
        return true;

    if (AUDIO_DEVICE_IN_USB_DEVICE == stream->input_device)
        direction = PCM_IN;
    else
        direction = PCM_OUT;

    fprintf(log_file, "%s: opening %s stream\n", __func__, ((direction == PCM_IN)? "input":"output"));

    if (PCM_IN == direction)
        rc = qahw_open_input_stream(stream->qahw_in_hal_handle,
                                stream->handle,
                                stream->input_device,
                                &(stream->config),
                                &(stream->in_handle),
                                AUDIO_OUTPUT_FLAG_NONE,
                                stream->device_url,
                                AUDIO_SOURCE_DEFAULT);
    else
        rc = qahw_open_output_stream(stream->qahw_out_hal_handle,
                                stream->handle,
                                stream->output_device,
                                stream->flags,
                                &(stream->config),
                                &(stream->out_handle),
                                stream->device_url);

    if (rc) {
        fprintf(log_file, "stream could not be opened\n");
        fprintf(stderr, "stream could not be opened\n");
        return rc;
    }

    fprintf(log_file,"\n**Supported Parameters**\n");
    if (PCM_IN == direction)
        param_string = qahw_in_get_parameters(stream->in_handle, QAHW_PARAMETER_STREAM_SUP_SAMPLING_RATES);
    else
        param_string = qahw_out_get_parameters(stream->out_handle, QAHW_PARAMETER_STREAM_SUP_SAMPLING_RATES);

    if ((stream->config.sample_rate = get_rate(param_string)) <= 0) {
        fprintf(log_file, "Unable to extract sample rate val =(%d) string(%s)\n", stream->config.sample_rate, param_string);
        fprintf(stderr, "Unable to extract sample rate val =(%d) string(%s)\n", stream->config.sample_rate, param_string);
        return -1;
    }
    if (PCM_IN == direction)
        param_string = qahw_in_get_parameters(stream->in_handle, QAHW_PARAMETER_STREAM_SUP_CHANNELS);
    else
        param_string = qahw_out_get_parameters(stream->out_handle, QAHW_PARAMETER_STREAM_SUP_CHANNELS);

    if ((ch = get_channels(param_string)) <= 0) {
        fprintf(log_file, "Unable to extract channels =(%d) string(%s)\n", ch, param_string);
        fprintf(stderr, "Unable to extract channels =(%d) string(%s)\n", ch, param_string);
        return -1;
    }
    stream->config.channel_mask = audio_channel_in_mask_from_count(ch);

    if (PCM_IN == direction)
        param_string = qahw_in_get_parameters(stream->in_handle, QAHW_PARAMETER_STREAM_SUP_FORMATS);
    else
        param_string = qahw_out_get_parameters(stream->out_handle, QAHW_PARAMETER_STREAM_SUP_FORMATS);

    if ((stream->config.format = get_pcm_format(param_string)) <= 0) {
        fprintf(log_file, "Unable to extract pcm format val =(%d) string(%s)\n", stream->config.format, param_string);
        fprintf(stderr, "Unable to extract pcm format val =(%d) string(%s)\n", stream->config.format, param_string);
        return -1;
    }
    stream->config.offload_info.format = stream->config.format;
    fprintf(log_file, "\n**Extracted Parameters**\nrate=%d\nch=%d,ch_mask=0x%x\nformats=%d\n\n",
        stream->config.sample_rate,
        ch, stream->config.channel_mask,
        stream->config.format);
    /*
     * Detection done now close usb stream it will be re-open later
     */
    fprintf(log_file, "%s:closing the usb stream\n", __func__);

    if (PCM_IN == direction)
        rc = qahw_close_input_stream(stream->in_handle);
    else
        rc = qahw_close_output_stream(stream->out_handle);

    if (rc) {
        fprintf(log_file, "%s:stream could not be closed\n", __func__);
        fprintf(stderr, "%s:stream could not be closed\n", __func__);
        return rc;
    }
    stream->config.offload_info.sample_rate = stream->config.sample_rate;
    stream->config.offload_info.format = stream->config.format;
    stream->config.offload_info.channel_mask = stream->config.channel_mask;
    return rc;

}
void usage() {
    printf(" \n Command \n");
    printf(" \n hal_play_test -f file-path <options>   - Plays audio file from the path provided\n");
    printf(" \n Options\n");
    printf(" -f  --file-path <file path>               - file path to be used for playback.\n");
    printf("                                             file path must be provided unless -K(--kpi) is used\n\n");
    printf("                                             file path must be provided unless -s(input data is not from a file) is used\n\n");
    printf(" -r  --sample-rate <sampling rate>         - Required for Non-WAV streams\n");
    printf("                                             For AAC-HE pls specify half the sample rate\n\n");
    printf(" -c  --channel count <channels>            - Required for Non-WAV streams\n\n");
    printf(" -b  --bitwidth <bitwidth>                 - Give either 16 or 24.Default value is 16.\n\n");
    printf(" -v  --volume <float volume level>         - Volume level float value between 0.0 - 1.0.\n");
    printf(" -d  --device <decimal value>              - see system/media/audio/include/system/audio.h for device values\n");
    printf("                                             Optional Argument and Default value is 2, i.e Speaker\n\n");
    printf(" -s  --source-device <decimal value>       - see system/media/audio/include/system/audio.h for device values\n");
    printf("                                             obtain data from a device instead of an SD card file\n\n");
    printf("                                             for example catpure data from USB HAL(device) and play it on Regular HAL(device)\n\n");
    printf(" -t  --file-type <file type>               - 1:WAV 2:MP3 3:AAC 4:AAC_ADTS 5:FLAC\n");
    printf("                                             6:ALAC 7:VORBIS 8:WMA 10:AAC_LATM \n");
    printf("                                             Required for non WAV formats\n\n");
    printf(" -a  --aac-type <aac type>                 - Required for AAC streams\n");
    printf("                                             1: LC 2: HE_V1 3: HE_V2\n\n");
    printf(" -w  --wma-type <wma type>                 - Required for WMA clips.Default vlaue is 1\n");
    printf("                                             1: WMA 2: WMAPRO 3:WMA_LOSSLESS \n\n");
    printf(" -k  --kvpairs <values>                    - Metadata information of clip\n");
    printf("                                             See Example for more info\n\n");
    printf(" -l  --log-file <ABSOLUTE FILEPATH>        - File path for debug msg, to print\n");
    printf("                                             on console use stdout or 1 \n\n");
    printf(" -D  --dump-file <ABSOLUTE FILEPATH>       - File path to dump pcm data from proxy\n");
    printf(" -F  --flags <int value for output flags>  - Output flag to be used\n\n");
    printf(" -k  --kpi-mode                            - Required for Latency KPI measurement\n");
    printf("                                             file path is not used here as file playback is not done in this mode\n");
    printf("                                             file path and other file specific options would be ignored in this mode.\n\n");
    printf(" -e  --effect-type <effect type>           - Effect used for test\n");
    printf("                                             0:bassboost 1:virtualizer 2:equalizer 3:visualizer(NA) 4:reverb 5:audiosphere others:null\n\n");
    printf(" -A  --bt-addr <bt device addr>            - Required to set bt device adress for aptx decoder\n\n");
    printf(" -q  --query drift                         - Required for querying avtime vs hdmi drift\n");
    printf(" -P                                        - Argument to do multi-stream playback, currently 2 streams are supported to run concurrently\n");
    printf("                                             Put -P and mention required attributes for the next stream\n");
    printf("                                             0:bassboost 1:virtualizer 2:equalizer 3:visualizer(NA) 4:reverb 5:audiosphere others:null");
    printf(" PLUS                                      - For multi-stream playback, currently 2 streams are supported to play concurrently\n");
    printf("                                             Put PLUS and mention required attributes for the next files");
    printf(" -u  --device-nodeurl                      - URL of PCM device\n");
    printf("                                             in the following format card=x;device=x");
    printf("                                             this option is mandatory in working with USB HAL");
    printf(" -m  --mode                                - usb operating mode(Device Mode is default)\n");
    printf("                                             0:Device Mode(host drives the stream and its params and so no need to give params as input)\n");
    printf("                                             1:Host Mode(user can give stream and stream params via a stream(SD card file) or setup loopback with given params\n");
    printf(" \n Examples \n");
    printf(" hal_play_test -f /data/Anukoledenadu.wav  -> plays Wav stream with default params\n\n");
    printf(" hal_play_test -f /data/MateRani.mp3 -t 2 -d 2 -v 0.01 -r 44100 -c 2 \n");
    printf("                                          -> plays MP3 stream(-t = 2) on speaker device(-d = 2)\n");
    printf("                                          -> 2 channels and 44100 sample rate\n\n");
    printf(" hal_play_test -f /data/v1-CBR-32kHz-stereo-40kbps.mp3 -t 2 -d 128 -v 0.01 -r 32000 -c 2 -D /data/proxy_dump.wav\n");
    printf("                                          -> plays MP3 stream(-t = 2) on BT device(-d = 128)\n");
    printf("                                          -> 2 channels and 32000 sample rate\n");
    printf("                                          -> dumps pcm data to file at /data/proxy_dump.wav\n\n");
    printf(" hal_play_test -f /data/AACLC-71-48000Hz-384000bps.aac  -t 4 -d 2 -v 0.05 -r 48000 -c 2 -a 1 \n");
    printf("                                          -> plays AAC-ADTS stream(-t = 4) on speaker device(-d = 2)\n");
    printf("                                          -> AAC format type is LC(-a = 1)\n");
    printf("                                          -> 2 channels and 48000 sample rate\n\n");
    printf(" hal_play_test -f /data/AACHE-adts-stereo-32000KHz-128000Kbps.aac  -t 4 -d 2 -v 0.05 -r 16000 -c 2 -a 3 \n");
    printf("                                          -> plays AAC-ADTS stream(-t = 4) on speaker device(-d = 2)\n");
    printf("                                          -> AAC format type is HE V2(-a = 3)\n");
    printf("                                          -> 2 channels and 16000 sample rate\n");
    printf("                                          -> note that the sample rate is half the actual sample rate\n\n");
    printf(" hal_play_test -f /data/2.0_16bit_48khz.m4a -k 1536000,16,0,0,4096,14,16388,0,10,2,40,48000,1536000,48000 -t 6 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play alac clip (-t = 6)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->alac_avg_bit_rate,alac_bit_depth,alac_channel_layout,alac_compatible_version,\n");
    printf("                                          ->alac_frame_length,alac_kb,alac_max_frame_bytes,alac_max_run,alac_mb,\n");
    printf("                                          ->alac_num_channels,alac_pb,alac_sampling_rate,avg_bit_rate,sample_rate\n\n");
    printf(" hal_play_test -f /data/DIL CHAHTA HAI.flac -k 0,4096,13740,4096,14 -t 5 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play flac clip (-t = 5)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->avg_bit_rate,flac_max_blk_size,flac_max_frame_size\n");
    printf("                                          ->flac_min_blk_size,flac_min_frame_size,sample_rate\n");
    printf(" hal_play_test -f /data/vorbis.mka -k 500000,48000,1 -t 7 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play vorbis clip (-t = 7)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->avg_bit_rate,sample_rate,vorbis_bitstream_fmt\n");
    printf(" hal_play_test -f /data/file.wma -k 192000,48000,16,8192,3,15,0,0,353 -t 8 -w 1 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play wma clip (-t = 8)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->avg_bit_rate,sample_rate,wma_bit_per_sample,wma_block_align\n");
    printf("                                          ->wma_channel_mask,wma_encode_option,wma_format_tag\n");
    printf(" hal_play_test -f /data/03_Kuch_Khaas_BE.btaptx -t 9 -d 2 -v 0.2 -r 44100 -c 2 -A 00:02:5b:00:ff:03 \n");
    printf("                                          -> Play aptx clip (-t = 9)\n");
    printf("                                          -> 2 channels and 44100 sample rate\n");
    printf("                                          -> BT addr: bt_addr=00:02:5b:00:ff:03\n\n");
    printf(" hal_play_test -f /data/silence.ac3 -t 9 -r 48000 -c 2 -v 0.05 -F 16433 -P -f /data/music_48k.ac3 -t 9 -r 48000 -c 2 -F 32817\n");
    printf("                                          -> Plays a silence clip as main stream and music clip as associated\n\n");
    printf(" hal_play_test -K -F 4                    -> Measure latency KPIs for low latency output\n\n");
    printf(" hal_play_test -f /data/Moto_320kbps.mp3 -t 2 -d 2 -v 0.1 -r 44100 -c 2 -e 2\n");
    printf("                                          -> plays MP3 stream(-t = 2) on speaker device(-d = 2)\n");
    printf("                                          -> 2 channels and 44100 sample rate\n\n");
    printf("                                          -> sound effect equalizer enabled\n\n");
    printf(" hal_play_test -d 2 -v 0.05 -s 2147487744 -u \"card=1\\;device=0\" \n");
    printf("                                          -> capture audio stream from usb device(HAL)@ 44.1 KHz, 2ch\n");
    printf("                                          -> and loop/play it back on to primary HAL \n\n");
    printf("                                          -> Device Mode is default\n");
    printf("                                          -> card=1\\;device=0 -> specifies the URL, pls not that the ';' is escaped\n\n");
    printf(" hal_play_test -f /data/Anukoledenadu.wav -d 16384 -u \"card=1\\;device=0\" \n");
    printf("                                          -> Play PCM to USB out\n\n");
    printf(" hal_play_test -d 16384 -u \"card=1\\;device=0\" -s 2\n");
    printf("                                          ->Capture PCM from Local Mic and play it on USB(usb to primary hal loopback)\n\n");
    printf(" hal_play_test -d 16384 -u \"card=1\\;device=0\" -s 2 -P -d 2 -v 0.05 -s 2147487744 -u \"card=1\\;device=0\"\n");
    printf("                                          ->full duplex, setup both primary to usb and usb to primary loopbacks\n");
    printf("                                          ->Note:-P separates the steam params for both the loopbacks\n");
    printf("                                          ->Note:all the USB device commmands(above) should be accompanied with the host side commands\n\n");
}

static int get_wav_header_length (FILE* file_stream)
{
    int subchunk_size = 0, wav_header_len = 0;

    fseek(file_stream, 16, SEEK_SET);
    if(fread(&subchunk_size, 4, 1, file_stream) != 1) {
        fprintf(log_file, "Unable to read subchunk:\n");
        fprintf(stderr, "Unable to read subchunk:\n");
        exit (1);
    }
    if(subchunk_size < 16) {
        fprintf(log_file, "This is not a valid wav file \n");
        fprintf(stderr, "This is not a valid wav file \n");
    } else {
          switch (subchunk_size) {
          case 16:
              fprintf(log_file, "44-byte wav header \n");
              wav_header_len = 44;
              break;
          case 18:
              fprintf(log_file, "46-byte wav header \n");
              wav_header_len = 46;
              break;
          default:
              fprintf(log_file, "Header contains extra data and is larger than 46 bytes: subchunk_size=%d \n", subchunk_size);
              wav_header_len = subchunk_size;
              break;
          }
    }
    return wav_header_len;
}

static qahw_module_handle_t * load_hal(audio_devices_t dev) {
    qahw_module_handle_t *hal = NULL;

    if ((AUDIO_DEVICE_IN_USB_DEVICE == dev) ||
        (AUDIO_DEVICE_OUT_USB_DEVICE == dev)){
        if (!usb_hal_handle) {
            fprintf(log_file,"\nLoading usb HAL\n");
            if ((usb_hal_handle = qahw_load_module(QAHW_MODULE_ID_USB)) == NULL) {
                fprintf(log_file,"failure in Loading usb HAL\n");
                fprintf(stderr,"failure in Loading usb HAL\n");
                return NULL;
            }
        }
        hal = usb_hal_handle;
    }
/*  else if ((AUDIO_DEVICE_IN_BLUETOOTH_A2DP == dev) ||
               (AUDIO_DEVICE_OUT_BLUETOOTH_A2DP == dev)){
        if (!bt_hal_handle) {
            fprintf(log_file,"Loading BT HAL\n");
            if ((bt_hal_handle = qahw_load_module(QAHW_MODULE_ID_A2DP)) == NULL) {
                fprintf(log_file,"failure in Loading BT HAL\n");
                fprintf(stderr,"failure in Loading BT HAL\n");
                return NULL;
            }
        }
        hal = bt_hal_handle;
    }*/
    else {
        if (!primary_hal_handle) {
            fprintf(log_file,"\nLoading Primary HAL\n");
            if ((primary_hal_handle = qahw_load_module(QAHW_MODULE_ID_PRIMARY)) == NULL) {
                fprintf(log_file,"failure in Loading Primary HAL\n");
                fprintf(stderr,"failure in Loading primary HAL\n");
                return NULL;
            }
        }
        hal = primary_hal_handle;
    }
    return hal;
}
/*
 * this function unloads all the loaded hal modules so this should be called
 * after all the stream playback are concluded.
 */
static int unload_hals() {

    if (usb_hal_handle) {
        fprintf(log_file,"\nUnLoading usb HAL\n");
        if (qahw_unload_module(usb_hal_handle) < 0) {
            fprintf(log_file,"failure in Un Loading usb HAL\n");
            fprintf(stderr,"failure in Un Loading usb HAL\n");
            return -1;
        }
    }
    if (bt_hal_handle) {
        fprintf(log_file,"UnLoading BT HAL\n");
        if (qahw_unload_module(bt_hal_handle) < 0) {
            fprintf(log_file,"failure in UnLoading BT HAL\n");
            fprintf(stderr,"failure in Un Loading BT HAL\n");
            return -1;
        }
    }
    if (primary_hal_handle) {
        fprintf(log_file,"\nUnLoading Primary HAL\n");
        if (qahw_unload_module(primary_hal_handle) < 0) {
            fprintf(log_file,"failure in Un Loading Primary HAL\n");
            fprintf(stderr,"failure in Un Loading primary HAL\n");
            return -1;
        }
    }
    return 1;
}


int main(int argc, char* argv[]) {
    char *ba = NULL;
    qahw_param_payload payload;
    qahw_param_id param_id;
    struct qahw_aptx_dec_param aptx_params;
    int rc = 0;
    int i = 0;
    int j = 0;
    kpi_mode = false;

    log_file = stdout;
    proxy_params.acp.file_name = "/data/pcm_dump.wav";
    stream_config *stream = NULL;
    init_streams();

    int num_of_streams = 1;

    struct option long_options[] = {
        /* These options set a flag. */
        {"file-path",     required_argument,    0, 'f'},
        {"output-device", required_argument,    0, 'd'},
        {"input-device",  required_argument,    0, 's'},
        {"sample-rate",   required_argument,    0, 'r'},
        {"channels",      required_argument,    0, 'c'},
        {"bitwidth",      required_argument,    0, 'b'},
        {"volume",        required_argument,    0, 'v'},
        {"log-file",      required_argument,    0, 'l'},
        {"dump-file",     required_argument,    0, 'D'},
        {"file-type",     required_argument,    0, 't'},
        {"aac-type",      required_argument,    0, 'a'},
        {"wma-type",      required_argument,    0, 'w'},
        {"kvpairs",       required_argument,    0, 'k'},
        {"flags",         required_argument,    0, 'F'},
        {"kpi-mode",      no_argument,          0, 'K'},
        {"plus",          no_argument,          0, 'P'},
        {"effect-path",   required_argument,    0, 'e'},
        {"bt-addr",       required_argument,    0, 'A'},
        {"query drift",   no_argument,          0, 'q'},
        {"device-nodeurl",required_argument,    0, 'u'},
        {"mode",          required_argument,    0, 'm'},
        {"help",          no_argument,          0, 'h'},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;

    proxy_params.hdr.riff_id = ID_RIFF;
    proxy_params.hdr.riff_sz = 0;
    proxy_params.hdr.riff_fmt = ID_WAVE;
    proxy_params.hdr.fmt_id = ID_FMT;
    proxy_params.hdr.fmt_sz = 16;
    proxy_params.hdr.audio_format = FORMAT_PCM;
    proxy_params.hdr.num_channels = 2;
    proxy_params.hdr.sample_rate = 44100;
    proxy_params.hdr.byte_rate = proxy_params.hdr.sample_rate * proxy_params.hdr.num_channels * 2;
    proxy_params.hdr.block_align = proxy_params.hdr.num_channels * 2;
    proxy_params.hdr.bits_per_sample = 16;
    proxy_params.hdr.data_id = ID_DATA;
    proxy_params.hdr.data_sz = 0;

    while ((opt = getopt_long(argc,
                              argv,
                              "-f:r:c:b:d:s:v:l:t:a:w:k:PD:KF:e:A:u:m:qh",
                              long_options,
                              &option_index)) != -1) {

        fprintf(log_file, "for argument %c, value is %s\n", opt, optarg);

        switch (opt) {
        case 'f':
            stream_param[i].filename = optarg;
            break;
        case 'r':
            stream_param[i].config.offload_info.sample_rate = atoi(optarg);
            stream_param[i].config.sample_rate = atoi(optarg);
            break;
        case 'c':
            stream_param[i].channels = atoi(optarg);
            stream_param[i].config.channel_mask = audio_channel_out_mask_from_count(atoi(optarg));
            break;
        case 'b':
            stream_param[i].config.offload_info.bit_width = atoi(optarg);
            break;
        case 'd':
            stream_param[i].output_device = atoll(optarg);
            break;
        case 's':
            stream_param[i].input_device = atoll(optarg);
            break;
        case 'v':
            vol_level = atof(optarg);
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
        case 't':
            stream_param[i].filetype = atoi(optarg);
            break;
        case 'a':
            stream_param[i].aac_fmt_type = atoi(optarg);
            break;
        case 'w':
            stream_param[i].wma_fmt_type = atoi(optarg);
            break;
        case 'k':
            stream_param[i].kvpair_values = optarg;
            break;
        case 'D':
            proxy_params.acp.file_name = optarg;
            break;
        case 'K':
            kpi_mode = true;
            break;
        case 'F':
            stream_param[i].flags = atoll(optarg);
            stream_param[i].flags_set = true;
            break;
        case 'e':
            stream_param[i].effect_index = atoi(optarg);
            if (stream_param[i].effect_index < 0 || stream_param[i].effect_index >= EFFECT_MAX) {
                fprintf(log_file, "Invalid effect type %d\n", stream_param[i].effect_index);
                fprintf(stderr, "Invalid effect type %d\n", stream_param[i].effect_index);
                stream_param[i].effect_index = -1;
            } else if (stream_param[i].effect_index == 3) {
                // visualizer is a special effect that is not perceivable by hearing
                // hence, add as an exception in test app.
                fprintf(log_file, "visualizer effect testing is not available\n");
                stream_param[i].effect_index = -1;
            } else {
                stream_param[i].ethread_func = effect_thread_funcs[stream_param[i].effect_index];
            }
            break;
        case 'A':
            ba = optarg;
            break;
        case 'q':
             stream_param[i].drift_query = true;
             break;
        case 'P':
            if(i >= MAX_PLAYBACK_STREAMS - 1) {
                fprintf(log_file, "cannot have more than %d streams\n", MAX_PLAYBACK_STREAMS);
                fprintf(stderr, "cannot have more than %d streams\n", MAX_PLAYBACK_STREAMS);
                return 0;
            }
            i++;
            fprintf(log_file, "Stream index incremented to %d\n", i);
            break;
        case 'u':
            stream_param[i].device_url = optarg;
            break;
        case 'm':
            stream_param[i].usb_mode = atoi(optarg);
            break;
        case 'h':
            usage();
            return 0;
            break;

        }
    }

    num_of_streams = i+1;
    fprintf(log_file, "Starting audio hal tests for streams : %d\n", num_of_streams);

    if (kpi_mode == true && num_of_streams > 1) {
        fprintf(log_file, "kpi-mode is not supported for multi-playback usecase\n");
        fprintf(stderr, "kpi-mode is not supported for multi-playback usecase\n");
        goto exit;
    }

    if (num_of_streams > 1 && stream_param[num_of_streams-1].output_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
        fprintf(log_file, "Proxy thread is not supported for multi-playback usecase\n");
        fprintf(stderr, "Proxy thread is not supported for multi-playback usecase\n");
        goto exit;
    }

    /* Register the SIGINT to close the App properly */
    if (signal(SIGINT, stop_signal_handler) == SIG_ERR) {
        fprintf(log_file, "Failed to register SIGINT:%d\n",errno);
        fprintf(stderr, "Failed to register SIGINT:%d\n",errno);
    }

    for (i = 0; i < num_of_streams; i++) {
        stream = &stream_param[i];

        if ((kpi_mode == false) && 
            (AUDIO_DEVICE_NONE == stream->input_device)){
            if (stream_param[PRIMARY_STREAM_INDEX].filename == nullptr) {
                fprintf(log_file, "Primary file name is must for non kpi-mode\n");
                fprintf(stderr, "Primary file name is must for non kpi-mode\n");
                goto exit;
            }
        }

        if (stream->output_device != AUDIO_DEVICE_NONE)
            if ((stream->qahw_out_hal_handle = load_hal(stream->output_device)) <= 0)
                goto exit;

        if (stream->input_device != AUDIO_DEVICE_NONE)
            if ((stream->qahw_in_hal_handle = load_hal(stream->input_device))== 0)
                goto exit;

        if ((AUDIO_DEVICE_NONE != stream->output_device) &&
            (AUDIO_DEVICE_NONE != stream->input_device))
            /*
             * hal loopback at what params we need to probably detect.
             */
             if(detect_stream_params(stream) < 0)
                goto exit;

        if (stream->filename) {
            if ((stream->file_stream = fopen(stream->filename, "r"))== NULL) {
                fprintf(log_file, "Cannot open audio file %s\n", stream->filename);
                fprintf(stderr, "Cannot open audio file %s\n", stream->filename);
                goto exit;
            }
            fprintf(log_file, "Playing from:%s\n", stream->filename);
            get_file_format(&stream_param[i]);
        } else if (AUDIO_DEVICE_NONE != stream->input_device) {
            fprintf(log_file, "Playing from device:%x\n", stream->input_device);
            fprintf(log_file, "Playing from url:%s\n", stream->device_url);
            fprintf(log_file, "setting up input hal and stream:%s\n", stream->device_url);

            rc = qahw_open_input_stream(stream->qahw_in_hal_handle,
                                    stream->handle,
                                    stream->input_device,
                                    &(stream->config),
                                    &(stream->in_handle),
                                    AUDIO_OUTPUT_FLAG_NONE,
                                    stream->device_url,
                                    AUDIO_SOURCE_UNPROCESSED);
            if (rc) {
                fprintf(log_file, "input stream could not be re-opened\n");
                fprintf(stderr, "input stream could not be re-opened\n");
                return rc;
            }
            stream->flags = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_NON_BLOCKING;
            stream->flags |= AUDIO_OUTPUT_FLAG_DIRECT;
        } else if (kpi_mode == true)
            stream->config.format = stream->config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;

        if (stream->output_device & AUDIO_DEVICE_OUT_ALL_A2DP)
            fprintf(log_file, "Saving pcm data to file: %s\n", proxy_params.acp.file_name);

        /* Set device connection state for HDMI */
        if (stream->output_device == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            char param[100] = {0};
            snprintf(param, sizeof(param), "%s=%d", "connect", AUDIO_DEVICE_OUT_AUX_DIGITAL);
            qahw_set_parameters(stream->qahw_out_hal_handle, param);
        }

        fprintf(log_file, "stream %d: File Type:%d\n", stream->stream_index, stream->filetype);
        fprintf(log_file, "stream %d: Audio Format:%d\n", stream->stream_index, stream->config.offload_info.format);
        fprintf(log_file, "stream %d: Output Device:%d\n", stream->stream_index, stream->output_device);
        fprintf(log_file, "stream %d: Output Flags:%d\n", stream->stream_index, stream->flags);
        fprintf(log_file, "stream %d: Sample Rate:%d\n", stream->stream_index, stream->config.offload_info.sample_rate);
        fprintf(log_file, "stream %d: Channels:%d\n", stream->stream_index, stream->channels);
        fprintf(log_file, "stream %d: Bitwidth:%d\n", stream->stream_index, stream->config.offload_info.bit_width);
        fprintf(log_file, "stream %d: AAC Format Type:%d\n", stream->stream_index, stream->aac_fmt_type);
        fprintf(log_file, "stream %d: Kvpair Values:%s\n", stream->stream_index, stream->kvpair_values);
        fprintf(log_file, "Log file:%s\n", log_filename);
        fprintf(log_file, "Volume level:%f\n", vol_level);

        stream->config.offload_info.version = AUDIO_OFFLOAD_INFO_VERSION_CURRENT;
        stream->config.offload_info.size = sizeof(audio_offload_info_t);

        if (stream->filetype == FILE_APTX) {
            if (ba != NULL) {
                parse_aptx_dec_bt_addr(ba, &aptx_params);
                payload = (qahw_param_payload)aptx_params;
                param_id = QAHW_PARAM_APTX_DEC;
                fprintf(log_file, "Send BT addr nap %d, uap %d lap %d to HAL\n", aptx_params.bt_addr.nap,
                            aptx_params.bt_addr.uap, aptx_params.bt_addr.lap);
                rc = qahw_set_param_data(stream->qahw_out_hal_handle, param_id, &payload);
                if (rc != 0)
                     fprintf(log_file, "Error.Failed Set BT addr\n");
                     fprintf(stderr, "Error.Failed Set BT addr\n");
            } else {
                fprintf(log_file, "BT addr is NULL, Need valid BT addr for aptx file playback to work\n");
                fprintf(stderr, "BT addr is NULL, Need valid BT addr for aptx file playback to work\n");
                goto exit;
            }
        }

        rc = pthread_create(&playback_thread[i], NULL, start_stream_playback, (void *)&stream_param[i]);
        if (rc) {
            fprintf(log_file, "stream %d: failed to create thread\n", stream->stream_index);
            fprintf(stderr, "stream %d: failed to create thread\n", stream->stream_index);
            exit(0);
        }

        thread_active[i] = true;

    }

exit:

    for (i=0; i<MAX_PLAYBACK_STREAMS; i++) {
        if(thread_active[i])
            pthread_join(playback_thread[i], NULL);
    }

    /*
     * reset device connection state for HDMI and close the file streams
     */
     for (i = 0; i < num_of_streams; i++) {
         if (stream_param[i].output_device == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
             char param[100] = {0};
             snprintf(param, sizeof(param), "%s=%d", "disconnect", AUDIO_DEVICE_OUT_AUX_DIGITAL);
             qahw_set_parameters(stream_param[i].qahw_out_hal_handle, param);
         }

        if (stream_param[i].file_stream != nullptr)
            fclose(stream_param[i].file_stream);
        else if (AUDIO_DEVICE_NONE != stream_param[i].input_device) {
            if (stream->in_handle) {
                rc = qahw_close_input_stream(stream->in_handle);
                if (rc) {
                    fprintf(log_file, "input stream could not be closed\n");
                    fprintf(stderr, "input stream could not be closed\n");
                    return rc;
                }
            }
        }
    }

    rc = unload_hals();

    if ((log_file != stdout) && (log_file != nullptr))
        fclose(log_file);

    fprintf(log_file, "\nBYE BYE\n");
    return 0;
}
