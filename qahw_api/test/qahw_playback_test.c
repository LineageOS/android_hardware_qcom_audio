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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "qahw_api.h"
#include "qahw_defs.h"
#define nullptr NULL
#define WAV 1
#define MP3 2


/* Play audio from a WAV file.

 Parameters:
   out_stream: A pointer to the output audio stream.
   in_file: A pointer to a SNDFILE object.
   config: A pointer to struct that contains audio configuration data.

 Returns: An int which has a non-negative number on success.
*/

int play_file(qahw_stream_handle_t* out_handle, FILE* in_file) {

  int rc = 0;
  size_t frames_read = 1;
  size_t bytes_wanted ;
  char  *data = NULL;
  qahw_out_buffer_t out_buf;

  bytes_wanted = qahw_out_get_buffer_size(out_handle);
  data = (char *) malloc (bytes_wanted);
  if (data == NULL) {
      printf("calloc failed!!\n");
      return -ENOMEM;
  }

  while(frames_read != 0) {
      frames_read = fread(data, bytes_wanted , 1, in_file);
      if (frames_read < 1) {
          if (feof(in_file))
              break;
          else
              printf("Error in fread --%d\n",ferror(in_file));
      }
      memset(&out_buf,0, sizeof(qahw_out_buffer_t));
      out_buf.buffer = data;
      out_buf.bytes = frames_read * bytes_wanted;
      rc = qahw_out_write(out_handle, &out_buf);
      if (rc < 0) {
          printf("Writing data to hal failed %d \n",rc);
          break;
      }
  }
  return rc;
}

// Prints usage information if input arguments are missing.
void Usage() {
    fprintf(stderr, "Usage:hal_play [device] [filename] [filetype]\n"
            "device: hex value representing the audio device (see "
            "system/media/audio/include/system/audio.h)\n"
            "filename must be passed as an argument.\n"
            "filetype (1:WAV 2:MP3) \n");
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        Usage();
        return -1;
  }
  // Process command line arguments.
  FILE *filestream = NULL;
  char header[44] = {0};
  int sample_rate = 0;
  int channels = 0;
  const int audio_device_base = 16;
  char* filename = nullptr;
  int filetype;
  qahw_module_handle_t *qahw_mod_handle;
  const  char *mod_name = "audio.primary";

  uint32_t desired_output_device = strtol(
      argv[1], nullptr /* look at full string*/, audio_device_base);

  filename = argv[2];
  filetype = atoi (argv[3]);

  printf("Starting audio hal tests.\n");
  int rc = 0;

  qahw_mod_handle = qahw_load_module(mod_name);

 // Set to a high number so it doesn't interfere with existing stream handles
  audio_io_handle_t handle = 0x999;
  audio_devices_t output_device =
      (audio_devices_t)desired_output_device;
  audio_output_flags_t flags = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD;
  audio_config_t config;

  memset(&config, 0, sizeof(audio_config_t));

  if (filename) {
      printf("filename-----%s\n",filename);
      filestream = fopen (filename,"r");
      if (filestream == NULL) {
          printf("failed to open\n");
          exit(0);
      }
  }

  switch (filetype) {
      case WAV:
          //Read the wave header
          rc = fread (header, 44 , 1, filestream);
          if (rc != 1) {
              printf("Error .Fread failed\n");
              exit(0);
          }
          if (strncmp (header,"RIFF",4) && strncmp (header+8, "WAVE",4)) {
              printf("Not a wave format\n");
              exit (1);
          }
          memcpy (&channels, &header[22], 2);
          memcpy (&sample_rate, &header[24], 4);
          config.channel_mask = audio_channel_out_mask_from_count(channels);
          config.offload_info.channel_mask = config.channel_mask;
          config.offload_info.sample_rate = sample_rate;
          config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
          break;
     case MP3:
          printf("Enter Number of channels:");
          scanf ("%d",&channels);
          config.channel_mask = audio_channel_out_mask_from_count(channels);
          printf("\nEnter Sample Rate:");
          scanf ("%d",&sample_rate);
          config.offload_info.channel_mask = config.channel_mask;
          config.offload_info.sample_rate = sample_rate;
          config.offload_info.format = AUDIO_FORMAT_MP3;
          break;
     default:
          printf("Does not support given filetype\n");
          Usage();
          exit (0);
  }
  config.offload_info.version = AUDIO_OFFLOAD_INFO_VERSION_CURRENT;
  config.offload_info.size = sizeof(audio_offload_info_t);

  printf("Now playing to output_device=%d sample_rate=%d \n",output_device,
          config.offload_info.sample_rate);
  const char* stream_name = "output_stream";

  // Open audio output stream.
  qahw_stream_handle_t* out_handle = nullptr;
  printf("calling open_out_put_stream:\n");
  rc = qahw_open_output_stream(qahw_mod_handle, handle, output_device,
                                        flags, &config, &out_handle,
                                        stream_name);
  printf("open output stream is sucess:%d  out_handhle %p\n",rc,out_handle);
  if (rc) {
    printf("could not open output stream %d \n",rc);
    return -1;
  }

  play_file(out_handle, filestream);

  // Close output stream and device.
  rc = qahw_out_standby(out_handle);
  if (rc) {
      printf("out standby failed %d \n",rc);
  }

  rc = qahw_close_output_stream(out_handle);
  if (rc) {
      printf("could not close output stream %d \n",rc);
  }

  rc = qahw_unload_module(qahw_mod_handle);
  if (rc) {
      printf("could not unload hal  %d \n",rc);
      return -1;
  }

  printf("Done with hal tests \n");
  return 0;
}
