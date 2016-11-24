/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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
#include <string.h>
#include <errno.h>
#include "qahw_api.h"
#include "qahw_defs.h"

#define nullptr NULL
FILE * log_file = NULL;
const char *log_filename = NULL;
float vol_level = 0.01;

enum {
    FILE_WAV = 1,
    FILE_MP3,
    FILE_AAC,
    FILE_AAC_ADTS,
    FILE_FLAC,
    FILE_ALAC,
    FILE_VORBIS,
    FILE_WMA
};

typedef enum {
    AAC_LC = 1,
    AAC_HE_V1,
    AAC_HE_V2
} aac_format_type_t;

static pthread_mutex_t write_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t write_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t drain_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t drain_cond = PTHREAD_COND_INITIALIZER;

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
                   "music_offload_wma_format_tag=%d;"

void read_kvpair(char *kvpair, char* kvpair_values, int filetype)
{
    char *kvpair_type;
    char param[100];
    char *token = NULL;
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
            token = atoi(token);
            snprintf(kvpair, size, kvpair_type, token);
            kvpair += size - 1;
            kvpair_type += len + 3;
            token = strtok(NULL, ",");
        }
    }
}

int async_callback(qahw_stream_callback_event_t event, void *param,
                  void *cookie)
{
    switch (event) {
    case QAHW_STREAM_CBK_EVENT_WRITE_READY:
        fprintf(log_file, "QAHW_STREAM_CBK_EVENT_DRAIN_READY\n");
        pthread_mutex_lock(&write_lock);
        pthread_cond_signal(&write_cond);
        pthread_mutex_unlock(&write_lock);
        break;
    case QAHW_STREAM_CBK_EVENT_DRAIN_READY:
        fprintf(log_file, "QAHW_STREAM_CBK_EVENT_DRAIN_READY\n");
        pthread_mutex_lock(&drain_lock);
        pthread_cond_signal(&drain_cond);
        pthread_mutex_unlock(&drain_lock);
    default:
        break;
    }
    return 0;
}


int write_to_hal(qahw_stream_handle_t* out_handle, char *data,
              size_t bytes)
{
    ssize_t ret;
    pthread_mutex_lock(&write_lock);
    qahw_out_buffer_t out_buf;

    memset(&out_buf,0, sizeof(qahw_out_buffer_t));
    out_buf.buffer = data;
    out_buf.bytes = bytes;

    ret = qahw_out_write(out_handle, &out_buf);
    if (ret < 0 || ret == bytes) {
        fprintf(log_file, "Writing data to hal failed or full write %ld, %ld\n",
            ret, bytes);
    } else if (ret != bytes) {
        fprintf(log_file, "ret %ld, bytes %ld\n", ret, bytes);
        fprintf(log_file, "Waiting for event write ready\n");
        pthread_cond_wait(&write_cond, &write_lock);
        fprintf(log_file, "out of wait for event write ready\n");
    }

    pthread_mutex_unlock(&write_lock);
    return ret;
}


/* Play audio from a WAV file.
 *
 * Parameters:
 *  out_stream: A pointer to the output audio stream.
 *  in_file: A pointer to a SNDFILE object.
 *  config: A pointer to struct that contains audio configuration data.
 *
 * Returns: An int which has a non-negative number on success.
 */

int play_file(qahw_stream_handle_t* out_handle, FILE* in_file,
              bool is_offload) {
    int rc = 0;
    int offset = 0;
    size_t bytes_wanted = 0;
    size_t write_length = 0;
    size_t bytes_remaining = 0;
    size_t bytes_written = 0;
    size_t bytes_read = 0;
    char  *data = NULL;
    qahw_out_buffer_t out_buf;
    bool exit = false;

    if (is_offload) {
        fprintf(log_file, "Set callback for offload stream\n");
        qahw_out_set_callback(out_handle, async_callback, NULL);
    }

    rc = qahw_out_set_volume(out_handle, vol_level, vol_level);
    if (rc < 0)
        fprintf(log_file, "unable to set volume");

    bytes_wanted = qahw_out_get_buffer_size(out_handle);
    data = (char *) malloc (bytes_wanted);
    if (data == NULL) {
        fprintf(log_file, "calloc failed!!\n");
        return -ENOMEM;
    }

    while (!exit) {
        if (!bytes_remaining) {
            bytes_read = fread(data, 1, bytes_wanted, in_file);
            fprintf(log_file, "fread from file %ld\n", bytes_read);
            if (bytes_read <= 0) {
                if (feof(in_file)) {
                    fprintf(log_file, "End of file");
                    if (is_offload) {
                        pthread_mutex_lock(&drain_lock);
                        if (is_offload) {
                            qahw_out_drain(out_handle, QAHW_DRAIN_ALL);
                            pthread_cond_wait(&drain_cond, &drain_lock);
                            fprintf(log_file, "Out of compress drain\n");
                        }
                        pthread_mutex_unlock(&drain_lock);
                    }
                } else {
                    fprintf(log_file, "Error in fread --%d\n", ferror(in_file));
                    fprintf(stderr, "Error in fread --%d\n", ferror(in_file));
                }
                exit = true;
                continue;
            }
            bytes_remaining = write_length = bytes_read;
        }

        offset = write_length - bytes_remaining;
        fprintf(log_file, "bytes_remaining %ld, offset %d, write length %ld\n",
                bytes_remaining, offset, write_length);
        bytes_written = write_to_hal(out_handle, data+offset, bytes_remaining);
        bytes_remaining -= bytes_written;
        fprintf(log_file, "bytes_written %ld, bytes_remaining %ld\n",
                bytes_written, bytes_remaining);
    }

    return rc;
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
    } else {
        fprintf(log_file, "Invalid filetype provided %d\n", filetype);
        fprintf(stderr, "Invalid filetype provided %d\n", filetype);
    }

    fprintf(log_file, "aac format %d\n", aac_format);
    return aac_format;
}

void usage() {
    printf(" \n Command \n");
    printf(" \n hal_play_test <file path>    - path of file to be played\n");
    printf(" \n Options\n");
    printf(" -r  --sample-rate <sampling rate>         - Required for Non-WAV streams\n");
    printf("                                             For AAC-HE pls specify half the sample rate\n\n");
    printf(" -c  --channel count <channels>            - Required for Non-WAV streams\n\n");
    printf(" -v  --volume <float volume level>         - Volume level float value between 0.0 - 1.0.\n");
    printf(" -d  --device <decimal value>              - see system/media/audio/include/system/audio.h for device values\n");
    printf("                                             Optional Argument and Default value is 2, i.e Speaker\n\n");
    printf(" -t  --file-type <file type>               - 1:WAV 2:MP3 3:AAC 4:AAC_ADTS 5:FLAC\n");
    printf("                                             6:ALAC 7:VORBIS 8:WMA\n");
    printf("                                             Required for non WAV formats\n\n");
    printf(" -a  --aac-type <aac type>                 - Required for AAC streams\n");
    printf("                                             1: LC 2: HE_V1 3: HE_V2\n\n");
    printf(" -k  --kvpairs <values>                    - Metadata information of clip\n");
    printf("                                             See Example for more info\n\n");
    printf(" -l  --log-file <FILEPATH>                 - File path for debug msg, to print\n");
    printf("                                             on console use stdout or 1 \n\n");
    printf(" \n Examples \n");
    printf(" hal_play_test /etc/Anukoledenadu.wav     -> plays Wav stream with default params\n\n");
    printf(" hal_play_test /etc/MateRani.mp3 -t 2 -d 2 -v 0.01 -r 44100 -c 2 \n");
    printf("                                          -> plays MP3 stream(-t = 2) on speaker device(-d = 2)\n");
    printf("                                          -> 2 channels and 44100 sample rate\n\n");
    printf(" hal_play_test /etc/AACLC-71-48000Hz-384000bps.aac  -t 4 -d 2 -v 0.05 -r 48000 -c 2 -a 1 \n");
    printf("                                          -> plays AAC-ADTS stream(-t = 4) on speaker device(-d = 2)\n");
    printf("                                          -> AAC format type is LC(-a = 1)\n");
    printf("                                          -> 2 channels and 48000 sample rate\n\n");
    printf(" hal_play_test /etc/AACHE-adts-stereo-32000KHz-128000Kbps.aac  -t 4 -d 2 -v 0.05 -r 16000 -c 2 -a 3 \n");
    printf("                                          -> plays AAC-ADTS stream(-t = 4) on speaker device(-d = 2)\n");
    printf("                                          -> AAC format type is HE V2(-a = 3)\n");
    printf("                                          -> 2 channels and 16000 sample rate\n");
    printf("                                          -> note that the sample rate is half the actual sample rate\n\n");
    printf(" hal_play_test /etc/2.0_16bit_48khz.m4a -k 1536000,16,0,0,4096,14,16388,0,10,2,40,48000,1536000,48000 -t 6 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play alac clip (-t = 6)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->alac_avg_bit_rate,alac_bit_depth,alac_channel_layout,alac_compatible_version,\n");
    printf("                                          ->alac_frame_length,alac_kb,alac_max_frame_bytes,alac_max_run,alac_mb,\n");
    printf("                                          ->alac_num_channels,alac_pb,alac_sampling_rate,avg_bit_rate,sample_rate\n\n");
    printf(" hal_play_test /etc/DIL CHAHTA HAI.flac -k 0,4096,13740,4096,14 -t 5 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play flac clip (-t = 5)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->avg_bit_rate,flac_max_blk_size,flac_max_frame_size\n");
    printf("                                          ->flac_min_blk_size,flac_min_frame_size,sample_rate\n");
    printf(" hal_play_test /etc/vorbis.mka -k 500000,48000,1 -t 7 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play vorbis clip (-t = 7)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->avg_bit_rate,sample_rate,vorbis_bitstream_fmt\n");
    printf(" hal_play_test /etc/file.wma -k 192000,48000,16,8192,3,15,353 -t 8 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play wma clip (-t = 8)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->avg_bit_rate,sample_rate,wma_bit_per_sample,wma_block_align\n");
    printf("                                          ->wma_channel_mask,wma_encode_option,wma_format_tag\n");
}

int main(int argc, char* argv[]) {

    FILE *file_stream = NULL;
    char header[44] = {0};
    char* filename = nullptr;
    qahw_module_handle_t *qahw_mod_handle;
    const char *mod_name = "audio.primary";
    qahw_stream_handle_t* out_handle = nullptr;
    int rc = 0;
    char *kvpair_values = NULL;
    char kvpair[1000] = {0};

    /*
     * Default values
     */
    int filetype = FILE_WAV;
    int sample_rate = 44100;
    int channels = 2;
    const int audio_device_base = 0x2;/* spkr device*/
    aac_format_type_t format_type = AAC_LC;
    log_file = stdout;
    audio_devices_t output_device = AUDIO_DEVICE_OUT_SPEAKER;

    struct option long_options[] = {
        /* These options set a flag. */
        {"device",        required_argument,    0, 'd'},
        {"sample-rate",   required_argument,    0, 'r'},
        {"channels",      required_argument,    0, 'c'},
        {"volume",        required_argument,    0, 'v'},
        {"log-file",      required_argument,    0, 'l'},
        {"file-type",     required_argument,    0, 't'},
        {"aac-type",      required_argument,    0, 'a'},
        {"kvpairs",       required_argument,    0, 'k'},
        {"help",          no_argument,          0, 'h'},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;
    while ((opt = getopt_long(argc,
                              argv,
                              "-r:c:d:v:l::t:a:k:h",
                              long_options,
                              &option_index)) != -1) {
            switch (opt) {
            case 'r':
                sample_rate = atoi(optarg);
                break;
            case 'c':;
                channels = atoi(optarg);
                break;
            case 'd':
                output_device = atoi(optarg);
                break;
            case 'v':
                vol_level = atof(optarg);
                break;
            case 'l':
                /*
                 * Fix Me: unable to log to a given file.
                 */
                log_filename = optarg;
                if((log_file = fopen(log_filename,"wb"))== NULL) {
                    fprintf(stderr, "Cannot open log file %s\n", log_filename);
                    /*
                     * continue to log to std out.
                     */
                    log_file = stdout;
                }

                break;
            case 't':
                filetype = atoi(optarg);
                break;
            case 'a':
                format_type = atoi(optarg);
                break;
            case 'k':
                kvpair_values = optarg;
                break;
            case 'h':
                usage();
                return 0;
                break;
         }
    }

    filename = argv[1];
    if((file_stream = fopen(filename, "r"))== NULL) {
        fprintf(stderr, "Cannot Open Audio File %s\n", filename);
        goto EXIT;
    }

    /*
     * Set to a high number so it doesn't interfere with existing stream handles
     */

    audio_io_handle_t handle = 0x999;
    audio_output_flags_t flags = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD;

    fprintf(stdout, "Playing:%s\n", filename);
    fprintf(stdout, "File Type:%d\n", filetype);
    fprintf(stdout, "Sample Rate:%d\n", sample_rate);
    fprintf(stdout, "Channels:%d\n", channels);
    fprintf(stdout, "Log file:%s\n", log_filename);
    fprintf(stdout, "Volume level:%f\n", vol_level);
    fprintf(stdout, "Output Device:%d\n", output_device);
    fprintf(stdout, "Format Type:%d\n", format_type);
    fprintf(stdout, "kvpair values:%s\n", kvpair_values);

    fprintf(stdout, "Starting audio hal tests.\n");

    qahw_mod_handle = qahw_load_module(mod_name);

    audio_config_t config;
    memset(&config, 0, sizeof(audio_config_t));

    switch (filetype) {
    case FILE_WAV:
        /*
         * Read the wave header
         */
        rc = fread (header, 44 , 1, file_stream);
        if (rc != 1) {
           fprintf(stdout, "Error .Fread failed\n");
           exit(0);
        }
        if (strncmp (header, "RIFF", 4) && strncmp (header+8, "WAVE", 4)) {
           fprintf(stdout, "Not a wave format\n");
           exit (1);
        }
        memcpy (&channels, &header[22], 2);
        memcpy (&sample_rate, &header[24], 4);
        config.channel_mask = audio_channel_out_mask_from_count(channels);
        config.offload_info.channel_mask = config.channel_mask;
        config.offload_info.sample_rate = sample_rate;
        config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
        break;

    case FILE_MP3:
        config.channel_mask = audio_channel_out_mask_from_count(channels);
        config.offload_info.channel_mask = config.channel_mask;
        config.sample_rate = sample_rate;
        config.offload_info.sample_rate = sample_rate;
        config.offload_info.format = AUDIO_FORMAT_MP3;
        flags |= AUDIO_OUTPUT_FLAG_NON_BLOCKING;
        break;

    case FILE_AAC:
    case FILE_AAC_ADTS:
        config.channel_mask = audio_channel_out_mask_from_count(channels);
        config.offload_info.channel_mask = config.channel_mask;
        config.sample_rate = sample_rate;
        config.offload_info.sample_rate = sample_rate;
        if (!is_valid_aac_format_type(format_type)) {
            fprintf(log_file, "Invalid format type for AAC %d\n", format_type);
            goto EXIT;
        }
        config.offload_info.format = get_aac_format(filetype, format_type);
        flags |= AUDIO_OUTPUT_FLAG_NON_BLOCKING;
        break;
    case FILE_FLAC:
        config.channel_mask = audio_channel_out_mask_from_count(channels);
        config.offload_info.channel_mask = config.channel_mask;
        config.sample_rate = sample_rate;
        config.offload_info.sample_rate = sample_rate;
        config.offload_info.format = AUDIO_FORMAT_FLAC;
        flags |= AUDIO_OUTPUT_FLAG_NON_BLOCKING;
        break;
    case FILE_ALAC:
        config.channel_mask = audio_channel_out_mask_from_count(channels);
        config.offload_info.channel_mask = config.channel_mask;
        config.sample_rate = sample_rate;
        config.offload_info.sample_rate = sample_rate;
        config.offload_info.format = AUDIO_FORMAT_ALAC;
        flags |= AUDIO_OUTPUT_FLAG_NON_BLOCKING;
        break;
    case FILE_VORBIS:
        config.channel_mask = audio_channel_out_mask_from_count(channels);
        config.offload_info.channel_mask = config.channel_mask;
        config.sample_rate = sample_rate;
        config.offload_info.sample_rate = sample_rate;
        config.offload_info.format = AUDIO_FORMAT_VORBIS;
        flags |= AUDIO_OUTPUT_FLAG_NON_BLOCKING;
        break;
    case FILE_WMA:
        config.channel_mask = audio_channel_out_mask_from_count(channels);
        config.offload_info.channel_mask = config.channel_mask;
        config.sample_rate = sample_rate;
        config.offload_info.sample_rate = sample_rate;
        config.offload_info.format = AUDIO_FORMAT_WMA;
        flags |= AUDIO_OUTPUT_FLAG_NON_BLOCKING;
        break;
    default:
       fprintf(stderr, "Does not support given filetype\n");
       usage();
       return 0;
    }
    config.offload_info.version = AUDIO_OFFLOAD_INFO_VERSION_CURRENT;
    config.offload_info.size = sizeof(audio_offload_info_t);

    fprintf(log_file, "Now playing to output_device=%d sample_rate=%d \n"
        , output_device, config.offload_info.sample_rate);
    const char* stream_name = "output_stream";

    fprintf(log_file, "calling open_out_put_stream:\n");
    rc = qahw_open_output_stream(qahw_mod_handle,
                                 handle,
                                 output_device,
                                 flags,
                                 &config,
                                 &out_handle,
                                 stream_name);
    fprintf(log_file, "open output stream is sucess:%d  out_handhle %p\n"
        , rc, out_handle);
    if (rc) {
        fprintf(stdout, "could not open output stream %d \n", rc);
        goto EXIT;
    }

    switch(filetype) {
    case FILE_WMA:
    case FILE_VORBIS:
    case FILE_ALAC:
    case FILE_FLAC:
        if (!kvpair_values) {
           fprintf (log_file, "Error!!No metadata for the Clip\n");
           fprintf (stdout, "Error!!No metadata for the Clip\n");
           goto EXIT;
        }
        read_kvpair(kvpair, kvpair_values, filetype);
        qahw_out_set_parameters(out_handle, kvpair);
        break;
    default:
        break;
    }

    play_file(out_handle,
              file_stream,
             (flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD));

EXIT:

    if (out_handle != nullptr) {
        rc = qahw_out_standby(out_handle);
        if (rc) {
            fprintf(stdout, "out standby failed %d \n", rc);
        }

        rc = qahw_close_output_stream(out_handle);
        if (rc) {
            fprintf(stdout, "could not close output stream %d \n", rc);
        }

        rc = qahw_unload_module(qahw_mod_handle);
        if (rc) {
            fprintf(stdout, "could not unload hal  %d \n", rc);
            return -1;
        }
    }

    if ((log_file != stdout) && (log_file != nullptr))
        fclose(log_file);

    if (file_stream != nullptr)
        fclose(file_stream);

    fprintf(stdout, "\nBYE BYE\n");
    return 0;
}
