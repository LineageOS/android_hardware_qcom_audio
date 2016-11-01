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

/* Test app to record multiple audio sessions at the HAL layer */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "qahw_api.h"
#include "qahw_defs.h"

struct audio_config_params {
    qahw_module_handle_t *qahw_mod_handle;
    audio_io_handle_t handle;
    audio_devices_t input_device;
    audio_config_t config;
    audio_input_flags_t flags;
    const char* kStreamName ;
    audio_source_t kInputSource;
    char output_filename[256];
    double loopTime;
    char profile[50];
};

#define SOUNDFOCUS_PARAMS "SoundFocus.start_angles;SoundFocus.enable_sectors;" \
                          "SoundFocus.gain_step"
#define SOURCETRACK_PARAMS "SourceTrack.vad;SourceTrack.doa_speech;SourceTrack.doa_noise;"\
                           "SourceTrack.polar_activity;ssr.noise_level;ssr.noise_level_after_ns"
int sourcetrack_done = 0;
static pthread_mutex_t glock;
pthread_cond_t gcond;
int tests_running;
bool gerror;

void *read_sourcetrack_data(void* data)
{
    char kvpair_soundfocus[200] = SOUNDFOCUS_PARAMS;
    char kvpair_sourcetrack[200] = SOURCETRACK_PARAMS;
    char *string = NULL;
    char *token = NULL;
    char choice = '\0';
    int i =0;
    qahw_module_handle_t *qawh_module_handle =
                (qahw_module_handle_t *)data;

    while (1) {
        printf("\nGet SoundFocus Params from app");
        string = qahw_get_parameters(qawh_module_handle, kvpair_soundfocus);
        if (!string) {
            printf("Error.Failed Get SoundFocus Params\n");
        } else {
            token = strtok (string , "=");
            while (token) {
                if (*token == 'S') {
                    choice = *(token + 11);
                    token = strtok (NULL,",;");
                    i=0;
                }
                switch (choice) {
                    case 'g':
                        printf ("\nSoundFocus.gain_step=%s",token);
                        break;
                    case 'e':
                        printf ("\nSoundFocus.enable_sectors[%d]=%s",i,token);
                        i++;
                        break;
                    case 's':
                        printf ("\nSoundFocus.start_angles[%d]=%s",i,token);
                        i++;
                        break;
                }
                token = strtok (NULL,",;=");
            }
        }
        choice = '\0';
        printf ("\nGet SourceTracking Params from app");
        string = qahw_get_parameters(qawh_module_handle, kvpair_sourcetrack);
        if (!string) {
            printf ("Error.Failed Get SourceTrack Params\n");
        } else {
            token = strtok (string , "=");
            while (token) {
                if (*token == 'S') {
                    choice = *(token + 12);
                    if (choice == 'd')
                        choice = *(token + 16);
                    token = strtok (NULL,",;");
                    i=0;
                }
                switch (choice) {
                    case 'p':
                        printf ("\nSourceTrack.polar_activity=%s,",token);
                        choice = '\0';
                        break;
                    case 'v':
                        printf ("\nSourceTrack.vad[%d]=%s",i,token);
                        i++;
                        break;
                    case 's':
                        printf ("\nSourceTrack.doa_speech=%s",token);
                        break;
                    case 'n':
                        printf ("\nSourceTrack.doa_noise[%d]=%s",i,token);
                        i++;
                        break;
                    default :
                        printf ("%s,",token);
                        break;
                }
                token = strtok (NULL,",;=");
            }
        }
        if (sourcetrack_done == 1)
            return NULL;
    }
}

void *start_input(void *thread_param)
{
  int rc = 0;
  struct audio_config_params* params = (struct audio_config_params*) thread_param;
  qahw_module_handle_t *qahw_mod_handle = params->qahw_mod_handle;

  // Open audio input stream.
  qahw_stream_handle_t* in_handle = NULL;

  rc = qahw_open_input_stream(qahw_mod_handle,
                              params->handle, params->input_device,
                              &params->config, &in_handle,
                              params->flags, params->kStreamName,
                              params->kInputSource);
  if (rc) {
      printf("ERROR :::: Could not open input stream.\n" );
      pthread_mutex_lock(&glock);
      gerror = true;
      pthread_cond_signal(&gcond);
      pthread_mutex_unlock(&glock);
      pthread_exit(0);
  }

  // Get buffer size to get upper bound on data to read from the HAL.
  size_t buffer_size;
      buffer_size = qahw_in_get_buffer_size(in_handle);
  char *buffer;
  buffer = (char *)calloc(1, buffer_size);
  if (buffer == NULL) {
     printf("calloc failed!!\n");
     pthread_mutex_lock(&glock);
     gerror = true;
     pthread_cond_signal(&gcond);
     pthread_mutex_unlock(&glock);
     pthread_exit(0);
  }

  printf("input opened, buffer = %p, size %zun",
         buffer, buffer_size);

  int num_channels = audio_channel_count_from_in_mask(params->config.channel_mask);

  time_t start_time = time(0);
  ssize_t bytes_read = -1;
  char param[100] = "audio_stream_profile=";
  qahw_in_buffer_t in_buf;

  // set profile for the recording session
  strlcat(param, params->profile, sizeof(param));
  qahw_in_set_parameters(in_handle, param);

  printf("\nPlease speak into the microphone for %lf seconds.\n", params->loopTime);

  FILE *fd = fopen(params->output_filename,"w");
  if (fd == NULL) {
     printf("File open failed \n");
     pthread_mutex_lock(&glock);
     gerror = true;
     pthread_cond_signal(&gcond);
     pthread_mutex_unlock(&glock);
     pthread_exit(0);
  }
  pthread_mutex_lock(&glock);
  tests_running++;
  pthread_cond_signal(&gcond);
  pthread_mutex_unlock(&glock);
  memset(&in_buf,0, sizeof(qahw_in_buffer_t));

  while(true) {
      in_buf.buffer = buffer;
      in_buf.bytes = buffer_size;
      bytes_read = qahw_in_read(in_handle, &in_buf);
      fwrite(in_buf.buffer, sizeof(char), buffer_size, fd);
      if(difftime(time(0), start_time) > params->loopTime) {
          printf("\nTest completed.\n");
          break;
      }
  }

  printf("closing input");

  // Close output stream and device.
  rc = qahw_in_standby(in_handle);
  if (rc) {
      printf("out standby failed %d \n",rc);
  }

  rc = qahw_close_input_stream(in_handle);
  if (rc) {
      printf("could not close input stream %d \n",rc);
  }

  // Print instructions to access the file.
  printf("\nThe audio recording has been saved to %s. Please use adb pull to get "
         "the file and play it using audacity. The audio data has the "
         "following characteristics:\nsample rate: %i\nformat: %d\n"
         "num channels: %i\n",
         params->output_filename, params->config.sample_rate,
         params->config.format, num_channels);

  pthread_mutex_lock(&glock);
  tests_running--;
  pthread_cond_signal(&gcond);
  pthread_mutex_unlock(&glock);
  pthread_exit(0);
  return NULL;
}

int read_config_params_from_user(struct audio_config_params *thread_param, int rec_session) {
    int channels = 0, format = 0, sample_rate = 0,source = 0, device = 0;

    thread_param->kStreamName = "input_stream";

    printf(" \n Enter input device (4->built-in mic, 16->wired_headset .. etc) ::::: ");
    scanf(" %d", &device);
    if (device & AUDIO_DEVICE_IN_BUILTIN_MIC)
        thread_param->input_device = AUDIO_DEVICE_IN_BUILTIN_MIC;
    else if (device & AUDIO_DEVICE_IN_WIRED_HEADSET)
        thread_param->input_device = AUDIO_DEVICE_IN_WIRED_HEADSET;

    printf(" \n Enter the channels (1 -mono, 2 -stereo and 4 -quad channels) ::::: ");
    scanf(" %d", &channels);
    if (channels == 1) {
        thread_param->config.channel_mask = AUDIO_CHANNEL_IN_MONO;
    } else if (channels == 2) {
        thread_param->config.channel_mask = AUDIO_CHANNEL_IN_STEREO;
    } else if (channels == 4) {
        thread_param->config.channel_mask = AUDIO_CHANNEL_INDEX_MASK_4;
    } else {
        gerror = true;
        printf("\nINVALID channels");
        return -1;
    }

    printf(" \n Enter the format (16 - 16 bit recording, 24 - 24 bit recording) ::::: ");
    scanf(" %d", &format);
    if (format == 16) {
        thread_param->config.format = AUDIO_FORMAT_PCM_16_BIT;
    } else if (format == 24) {
        thread_param->config.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
    } else {
        gerror = true;
        printf("\n INVALID format");
        return -1;
    }

    printf(" \n Enter the sample rate (48000, 16000 etc) :::: ");
    scanf(" %d", &sample_rate);
    thread_param->config.sample_rate = sample_rate;

#ifdef MULTIRECORD_SUPPOT
    printf(" \n Enter profile (none, record_fluence, record_mec, record_unprocessed etc) :::: ");
    scanf(" %s", thread_param->profile);
#else
    thread_param->flags = (audio_input_flags_t)AUDIO_INPUT_FLAG_NONE;
#endif
    printf("\n Enter the audio source ( ref: system/media/audio/include/system/audio.h) :::: ");
    scanf(" %d", &source);
    thread_param->kInputSource = (audio_source_t)source;

    if (rec_session == 1) {
        thread_param->handle = 0x999;
        strcpy(thread_param->output_filename, "/data/rec1.raw");
    } else if (rec_session == 2) {
        thread_param->handle = 0x998;
        strcpy(thread_param->output_filename, "/data/rec2.raw");
    } else if (rec_session == 3) {
        thread_param->handle = 0x997;
        strcpy(thread_param->output_filename, "/data/rec3.raw");
    } else if (rec_session == 4) {
        thread_param->handle = 0x996;
        strcpy(thread_param->output_filename, "/data/rec4.raw");
    }

    printf("\n Enter the record duration in seconds ::::  ");
    scanf(" %lf", &thread_param->loopTime);
    return 0;
}

int main() {
    int max_recordings_requested = 0, source_track = 0;
    int thread_active[4] = {0};
    qahw_module_handle_t *qahw_mod_handle;
    const  char *mod_name = "audio.primary";

    pthread_cond_init(&gcond, (const pthread_condattr_t *) NULL);

    qahw_mod_handle = qahw_load_module(mod_name);
    if(qahw_mod_handle == NULL) {
        printf(" qahw_load_module failed");
        return -1;
    }
#ifdef MULTIRECORD_SUPPOT
    printf("Starting audio hal multi recording test. \n");
    printf(" Enter number of record sessions to be started \n");
    printf("             (Maximum of 4 record sessions are allowed)::::  ");
    scanf(" %d", &max_recordings_requested);
#else
    max_recordings_requested = 1;
#endif
    printf(" \n Source Tracking enabled ??? ( 1 - Enable 0 - Disable)::: ");
    scanf(" %d", &source_track);

    struct audio_config_params thread1_params, thread2_params;
    struct audio_config_params thread3_params, thread4_params;

    switch (max_recordings_requested) {
        case 4:
            printf(" Enter the config params for fourth record session \n");
            thread4_params.qahw_mod_handle = qahw_mod_handle;
            read_config_params_from_user( &thread4_params, 4);
            thread_active[3] = 1;
            printf(" \n");
        case 3:
            printf(" Enter the config params for third record session \n");
            thread3_params.qahw_mod_handle = qahw_mod_handle;
            read_config_params_from_user( &thread3_params, 3);
            thread_active[2] = 1;
            printf(" \n");
        case 2:
            printf(" Enter the config params for second record session \n");
            thread2_params.qahw_mod_handle = qahw_mod_handle;
            read_config_params_from_user( &thread2_params, 2);
            thread_active[1] = 1;
            printf(" \n");
        case 1:
            printf(" Enter the config params for first record session \n");
            thread1_params.qahw_mod_handle = qahw_mod_handle;
            read_config_params_from_user( &thread1_params, 1);
            thread_active[0] = 1;
            printf(" \n");
            break;
        default:
            printf(" INVALID input -- Max record sessions supported is 4 -exit \n");
            gerror = true;
            break;
    }

    pthread_t tid[4];
    pthread_t sourcetrack_thread;
    int ret = -1;

    if (thread_active[0] == 1) {
        printf("\n Create first record thread \n");
        ret = pthread_create(&tid[0], NULL, start_input, (void *)&thread1_params);
        if (ret) {
            gerror = true;
            printf(" Failed to create first record thread \n ");
            thread_active[0] = 0;
        }
    }
    if (thread_active[1] == 1) {
        printf("Create second record thread \n");
        ret = pthread_create(&tid[1], NULL, start_input, (void *)&thread2_params);
        if (ret) {
            gerror = true;
            printf(" Failed to create second record thread \n ");
            thread_active[1] = 0;
        }
    }
    if (thread_active[2] == 1) {
        printf("Create third record thread \n");
        ret = pthread_create(&tid[2], NULL, start_input, (void *)&thread3_params);
        if (ret) {
            gerror = true;
            printf(" Failed to create third record thread \n ");
            thread_active[2] = 0;
        }
    }
    if (thread_active[3] == 1) {
        printf("Create fourth record thread \n");
        ret = pthread_create(&tid[3], NULL, start_input, (void *)&thread4_params);
        if (ret) {
            gerror = true;
            printf(" Failed to create fourth record thread \n ");
            thread_active[3] = 0;
        }
    }
    if (source_track && max_recordings_requested) {
        printf("Create source tracking thread \n");
        ret = pthread_create(&sourcetrack_thread,
                NULL, read_sourcetrack_data,
                (void *)qahw_mod_handle);
        if (ret) {
            printf(" Failed to create source tracking thread \n ");
            source_track = 0;
        }
    }

    // set bad mic param
    while (max_recordings_requested && !source_track) {
        bool test_completed = false;

        pthread_mutex_lock(&glock);
        if (!tests_running && !gerror)
            pthread_cond_wait(&gcond, &glock);
        test_completed = (tests_running == 0);
        gerror = true;
        pthread_mutex_unlock(&glock);

        if (test_completed)
            break;
#ifdef MULTIRECORD_SUPPOT
        char ch;
        printf("\n Bad mic test required (y/n):::");
        scanf(" %c", &ch);
        if (ch == 'y' || ch == 'Y') {
            int bad_mic_ch_index, ret;
            char param[100] = "bad_mic_channel_index=";
            printf("\nEnter bad mic channel index (1, 2, 4 ...):::");
            scanf(" %d", &bad_mic_ch_index);
            snprintf(param, sizeof(param), "%s%d", param, bad_mic_ch_index);
            ret = qahw_set_parameters(qahw_mod_handle, param);
            printf("param %s set to hal with return value %d\n", param, ret);
        } else {
            break;
        }
#endif
    }

    printf(" Waiting for threads exit \n");
    if (thread_active[0] == 1) {
        pthread_join(tid[0], NULL);
        printf("after first record thread exit \n");
    }
    if (thread_active[1] == 1) {
        pthread_join(tid[1], NULL);
        printf("after second record thread exit \n");
    }
    if (thread_active[2] == 1) {
        pthread_join(tid[2], NULL);
        printf("after third record thread exit \n");
    }
    if (thread_active[3] == 1) {
        pthread_join(tid[3], NULL);
        printf("after fourth record thread exit \n");
    }
    if (source_track) {
        sourcetrack_done = 1;
        pthread_join(sourcetrack_thread,NULL);
        printf("after source tracking thread exit \n");
    }

    ret = qahw_unload_module(qahw_mod_handle);
    if (ret) {
        printf("could not unload hal %d \n",ret);
    }


    printf("Done with hal record test \n");
    pthread_cond_destroy(&gcond);
    return 0;
}
