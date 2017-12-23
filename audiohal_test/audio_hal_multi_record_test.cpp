//
// Copyright (c) 2017, The Linux Foundation. All rights reserved.
// Not a Contribution.
//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// Test app to record multiple audio sessions at the HAL layer.

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "audio_defs.h"

struct audioConfigs {
   void* audio_device;
   audio_io_handle_t handle;
   audio_devices_t input_device;
   audio_config_t config;
   audio_input_flags_t flags;
   const char* kStreamName = "input_stream";
   audio_source_t kInputSource;
   char output_filename[256];
   double loopTime;
   char profile[50];
};

static pthread_mutex_t glock;
pthread_cond_t gcond;
int tests_running;
bool gerror;

void *startInput (void *threadParams)
{
  int rc = 0;
  struct audioConfigs* params = (audioConfigs*) threadParams;
  audio_hw_device_t* dev = (audio_hw_device_t*) params->audio_device;

  // Open audio input stream.
  audio_stream_in_t* stream = nullptr;

  rc = dev->open_input_stream(dev, params->handle, params->input_device,
                              &params->config, &stream, params->flags, params->kStreamName,
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
  buffer_size = stream->common.get_buffer_size(&stream->common);
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

  printf("input opened, buffer = %p, size %zu\n", buffer, buffer_size);

  int num_channels = audio_channel_count_from_out_mask(params->config.channel_mask);

  time_t start_time = time(0);
  ssize_t bytes_read=-1;
  char param[100] = "audio_stream_profile=";

  // set profile for the recording session
  strlcat(param, params->profile, sizeof(param));
      stream->common.set_parameters(&stream->common, param);

  printf("Please speak into the microphone for %lf seconds.\n", params->loopTime);

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

  while(true) {
         bytes_read = stream->read(stream, buffer, buffer_size);
     printf("bytes_read:%d \n", (int)bytes_read);

    fwrite(buffer, sizeof(char), buffer_size, fd);
    if(difftime(time(0), start_time) > params->loopTime) {
       printf("\nTest completed.\n");
       break;
    }
  }

  // Close input stream and device.
      dev->close_input_stream(dev, stream);

 // Print instructions to access the file.
  printf("\nThe audio recording has been saved to %s. Please use adb pull to get "
         "the file and play it using audacity. The audio data has the "
         "following characteristics:\nsample rate: %i\nformat: %d\n"
         "num channels: %i\n",
         params->output_filename, params->config.sample_rate, params->config.format, num_channels);

  pthread_mutex_lock(&glock);
  tests_running--;
  pthread_cond_signal(&gcond);
  pthread_mutex_unlock(&glock);
  pthread_exit(0);
  return NULL;
}

int read_config_params_from_user(struct audioConfigs *threadParams, int rec_session) {
  int channels = 0, sample_rate = 0, compress_mode = 0, source = 0, device = 0;

  printf(" \n Enter input device (4->built-in mic, 16->wired_headset .. etc) ::::: ");
  scanf(" %d", &device);
  if (device & AUDIO_DEVICE_IN_BUILTIN_MIC)
      threadParams->input_device = AUDIO_DEVICE_IN_BUILTIN_MIC;
  else if (device & AUDIO_DEVICE_IN_WIRED_HEADSET)
      threadParams->input_device = AUDIO_DEVICE_IN_WIRED_HEADSET;

  printf(" \n Enter the channels (1 -mono, 2 -stereo and 4 -quad channels) ::::: ");
  scanf(" %d", &channels);
  if (channels == 1) {
      threadParams->config.channel_mask = AUDIO_CHANNEL_IN_MONO;
  } else if (channels == 2) {
      threadParams->config.channel_mask = AUDIO_CHANNEL_IN_STEREO;
  } else if (channels == 4) {
      threadParams->config.channel_mask = AUDIO_CHANNEL_INDEX_MASK_4;
  } else {
      printf(" INVALID channels - exit \n");
      gerror = true;
      return -1;
  }

  threadParams->config.format = AUDIO_FORMAT_PCM_16_BIT;

  printf(" \n Enter the sample rate (48000, 16000 etc) :::: ");
  scanf(" %d", &sample_rate);
  threadParams->config.sample_rate = sample_rate;

  printf(" \n Enter profile (none, record_fluence, record_compress etc) :::: ");
  scanf(" %s", threadParams->profile);

  printf(" \n Compress mode enabled ?? ( 1 - Enable , 0 -Disable) ::::: ");
  scanf(" %d", &compress_mode);
  if (compress_mode)
    threadParams->flags = static_cast<audio_input_flags_t> (AUDIO_INPUT_FLAG_NONE|AUDIO_INPUT_FLAG_COMPRESS);
  else
    threadParams->flags = static_cast<audio_input_flags_t> (AUDIO_INPUT_FLAG_NONE);

  printf("\n Enter the audio source ( ref: system/media/audio/include/system/audio.h) :::: ");
  scanf(" %d", &source);
  threadParams->kInputSource = static_cast<audio_source_t> (source);

  if (rec_session == 1) {
    threadParams->handle = 0x999;
    strcpy(threadParams->output_filename, "/data/rec1.raw");
  } else if (rec_session == 2) {
    threadParams->handle = 0x998;
    strcpy(threadParams->output_filename, "/data/rec2.raw");
  } else if (rec_session == 3) {
    threadParams->handle = 0x997;
    strcpy(threadParams->output_filename, "/data/rec3.raw");
  } else if (rec_session == 4) {
    threadParams->handle = 0x996;
    strcpy(threadParams->output_filename, "/data/rec4.raw");
  }

  printf("\n Enter the record duration in seconds ::::  ");
  scanf(" %lf", &threadParams->loopTime);

  return 0;
}

int main() {

  int max_recordings_requested = 0;
  int thread_active[4] = {0};
  int rc = 0;
  const hw_module_t* module = nullptr;

  pthread_cond_init(&gcond, (const pthread_condattr_t *) NULL);
  printf("Starting audio hal multi recording test. \n");
  // Load audio HAL.
  rc = hw_get_module_by_class(AUDIO_HARDWARE_MODULE_ID, "primary", &module);
  if (rc) {
     printf("Could not get primary hw module, Error \n");
     return -1;
  }

  audio_hw_device_t* audio_device = nullptr;
  rc = audio_hw_device_open(module, &audio_device);
  if (rc) {
    printf("Could not open hw device. %d", rc);
    return -1;
  }

  printf(" Enter number of record sessions to be started \n");
  printf("             (Maximum of 4 record sessions are allowed)::::  ");
  scanf(" %d", &max_recordings_requested);

  struct audioConfigs thread1Params,thread2Params,thread3Params,thread4Params;

  switch (max_recordings_requested) {
    case 4:
      printf(" Enter the config params for fourth record session \n");
      thread4Params.audio_device = (void *) audio_device;
      read_config_params_from_user( &thread4Params, 4);
      thread_active[3] = 1;
      printf(" \n");
    case 3:
      printf(" Enter the config params for third record session \n");
      thread3Params.audio_device = (void *) audio_device;
      read_config_params_from_user( &thread3Params, 3);
      thread_active[2] = 1;
      printf(" \n");
    case 2:
      printf(" Enter the config params for second record session \n");
      thread2Params.audio_device = (void *) audio_device;
      read_config_params_from_user( &thread2Params, 2);
      thread_active[1] = 1;
      printf(" \n");
    case 1:
      printf(" Enter the config params for first record session \n");
      thread1Params.audio_device = (void *) audio_device;
      read_config_params_from_user( &thread1Params, 1);
      thread_active[0] = 1;
      printf(" \n");
      break;
    default:
      printf(" INVALID input -- Max record sessions supported is 4 -exit \n");
      gerror = true;
      break;
  }

  pthread_t tid[4];
  int ret = -1;

  if (thread_active[0] == 1) {
      printf("\n Create first record thread \n");
      ret = pthread_create(&tid[0], NULL, startInput, (void *)&thread1Params);
      if (ret) {
          gerror = true;
          printf(" Failed to create first record thread \n ");
          thread_active[0] = 0;
      }
  }
  if (thread_active[1] == 1) {
      printf("Create second record thread \n");
      ret = pthread_create(&tid[1], NULL, startInput, (void *)&thread2Params);
      if (ret) {
          gerror = true;
          printf(" Failed to create second record thread \n ");
          thread_active[1] = 0;
      }
  }
  if (thread_active[2] == 1) {
      printf("Create third record thread \n");
      ret = pthread_create(&tid[2], NULL, startInput, (void *)&thread3Params);
      if (ret) {
          gerror = true;
          printf(" Failed to create third record thread \n ");
          thread_active[2] = 0;
      }
  }
  if (thread_active[3] == 1) {
      printf("Create fourth record thread \n");
      ret = pthread_create(&tid[3], NULL, startInput, (void *)&thread4Params);
      if (ret) {
          gerror = true;
          printf(" Failed to create fourth record thread \n ");
          thread_active[3] = 0;
      }
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

  audio_hw_device_close(audio_device);
  printf("Done with hal record test \n");
  pthread_cond_destroy(&gcond);
  return 0;
}
