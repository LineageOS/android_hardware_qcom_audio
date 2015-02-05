/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 * Not a Contribution.
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

#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <math.h>

#define LOG_TAG "AudioBitstreamStateMachine"
#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0
#include <utils/Log.h>

#include <cutils/properties.h>
#include "audio_hw.h"
#include "platform_api.h"
#include <platform.h>
// ----------------------------------------------------------------------------

/*
Initialize all input and output pointers
Allocate twice the max buffer size of input and output for sufficient buffering
*/
int audio_bitstream_init(struct audio_bitstream_sm *bstream, int buffering_factor)
{
    bstream->buffering_factor = buffering_factor;
    bstream->buffering_factor_cnt = 0;
    bstream->inp_buf_size = SAMPLES_PER_CHANNEL *
                        MAX_INPUT_CHANNELS_SUPPORTED*
                        (bstream->buffering_factor+1);
    bstream->inp_buf = (char *)malloc( bstream->inp_buf_size);

                                // multiplied by 2 to convert to bytes
    if(bstream->inp_buf != NULL) {
        bstream->inp_buf_curr_ptr = bstream->inp_buf;
        bstream->inp_buf_write_ptr = bstream->inp_buf;
    } else {
        ALOGE("MS11 input buffer not allocated");
        bstream->inp_buf_size = 0;
        return 0;
    }

    bstream->enc_out_buf_size = SAMPLES_PER_CHANNEL * MAX_INPUT_CHANNELS_SUPPORTED*
                        FACTOR_FOR_BUFFERING;
    bstream->enc_out_buf =(char *)malloc(bstream->enc_out_buf_size);

    if(bstream->enc_out_buf) {
        bstream->enc_out_buf_write_ptr = bstream->enc_out_buf;
    } else {
        ALOGE("MS11 Enc output buffer not allocated");
        bstream->enc_out_buf_size = 0;
        return 0;
    }
    bstream->pcm_2_out_buf_size =  SAMPLES_PER_CHANNEL*STEREO_CHANNELS *
                    FACTOR_FOR_BUFFERING;
    bstream->pcm_2_out_buf =(char *)malloc(bstream->pcm_2_out_buf_size);
    if(bstream->pcm_2_out_buf) {
        bstream->pcm_2_out_buf_write_ptr = bstream->pcm_2_out_buf;
    } else {
        ALOGE("MS11 PCM2Ch output buffer not allocated");
        bstream->pcm_2_out_buf_size = 0;
        return 0;
    }
    bstream->pcm_mch_out_buf_size = SAMPLES_PER_CHANNEL * MAX_OUTPUT_CHANNELS_SUPPORTED *
                     FACTOR_FOR_BUFFERING;

    bstream->pcm_mch_out_buf =(char *)malloc(bstream->pcm_mch_out_buf_size);
    if(bstream->pcm_mch_out_buf) {
        bstream->pcm_mch_out_buf_write_ptr = bstream->pcm_mch_out_buf;
    } else {
        ALOGE("MS11 PCMMCh output buffer not allocated");
        bstream->pcm_mch_out_buf_size = 0;
        return 0;
    }
    bstream->passt_out_buf_size =SAMPLES_PER_CHANNEL *
                       MAX_INPUT_CHANNELS_SUPPORTED *
                       FACTOR_FOR_BUFFERING;
    bstream->passt_out_buf =(char *)malloc(bstream->passt_out_buf_size);
    if(bstream->passt_out_buf) {
        bstream->passt_out_buf_write_ptr = bstream->passt_out_buf;
    } else {
        ALOGE("MS11 Enc output buffer not allocated");
        bstream->passt_out_buf_size  = 0;
        return 0;
    }
    return 1;
}

/*
Free the allocated memory
*/
int audio_bitstream_close(struct audio_bitstream_sm *bstream)
{
    if(bstream->inp_buf != NULL) {
       free(bstream->inp_buf);
       bstream->inp_buf = NULL;
    }
    if(bstream->enc_out_buf != NULL) {
       free(bstream->enc_out_buf);
       bstream->enc_out_buf = NULL;
    }
    if(bstream->pcm_2_out_buf != NULL) {
       free(bstream->pcm_2_out_buf);
       bstream->pcm_2_out_buf = NULL;
    }
    if(bstream->pcm_mch_out_buf != NULL) {
        free(bstream->pcm_mch_out_buf);
        bstream->pcm_mch_out_buf = NULL;
    }
    if(bstream->passt_out_buf != NULL) {
       free(bstream->passt_out_buf);
       bstream->passt_out_buf = NULL;
    }
    bstream->buffering_factor = 1;
    bstream->buffering_factor_cnt = 0;
    return 0;
}

/*
Reset the buffer pointers to start for. This will be help in flush and close
*/
void audio_bitstream_reset_ptr( struct audio_bitstream_sm *bstream)
{
    bstream->inp_buf_curr_ptr = bstream->inp_buf_write_ptr = bstream->inp_buf;
    bstream->enc_out_buf_write_ptr = bstream->enc_out_buf;
    bstream->pcm_2_out_buf_write_ptr = bstream->pcm_2_out_buf;
    bstream->pcm_mch_out_buf_write_ptr = bstream->pcm_mch_out_buf;
    bstream->passt_out_buf_write_ptr = bstream->passt_out_buf;
    bstream->buffering_factor_cnt = 0;
}

/*
Reset the output buffer pointers to start for port reconfiguration
*/
void audio_bitstream_reset_output_bitstream_ptr(
                            struct audio_bitstream_sm *bstream)
{
    bstream->enc_out_buf_write_ptr = bstream->enc_out_buf;
    bstream->pcm_2_out_buf_write_ptr = bstream->pcm_2_out_buf;
    bstream->pcm_mch_out_buf_write_ptr = bstream->pcm_mch_out_buf;
    bstream->passt_out_buf_write_ptr = bstream->passt_out_buf;
}

/*
Copy the bitstream/pcm from Player to internal buffer.
The incoming bitstream is appended to existing bitstream
*/
void audio_bitstream_copy_to_internal_buffer(
                    struct audio_bitstream_sm *bstream,
                    char *buf_ptr, size_t bytes)
{
    // flush the input buffer if input is not consumed
    if( (bstream->inp_buf_write_ptr+bytes) > (bstream->inp_buf+bstream->inp_buf_size) ) {
        ALOGE("Input bitstream is not consumed");
        return;
    }

    memcpy(bstream->inp_buf_write_ptr, buf_ptr, bytes);
    bstream->inp_buf_write_ptr += bytes;
    if(bstream->buffering_factor_cnt < bstream->buffering_factor)
        bstream->buffering_factor_cnt++;
}

/*
Append zeros to the bitstream, so that the entire bitstream in ADIF is pushed
out for decoding
*/
void audio_bitstream_append_silence_internal_buffer(
                    struct audio_bitstream_sm *bstream,
                    uint32_t bytes, unsigned char value)
{
    int32_t bufLen = SAMPLES_PER_CHANNEL*MAX_INPUT_CHANNELS_SUPPORTED*
			(bstream->buffering_factor+1);
    uint32_t i = 0;
    if( (bstream->inp_buf_write_ptr+bytes) > (bstream->inp_buf+bufLen) ) {
        bytes = bufLen + bstream->inp_buf - bstream->inp_buf_write_ptr;
    }
    for(i=0; i< bytes; i++)
        *bstream->inp_buf_write_ptr++ = value;
    if(bstream->buffering_factor_cnt < bstream->buffering_factor)
        bstream->buffering_factor_cnt++;
}

/*
Flags if sufficient bitstream is available to proceed to decode based on
the threshold
*/
int audio_bitstream_sufficient_buffer_to_decode(
                        struct audio_bitstream_sm *bstream,
                        int min_bytes_to_decode)
{
    int proceed_decode = 0;
    if( (bstream->inp_buf_write_ptr -\
		bstream->inp_buf_curr_ptr) > min_bytes_to_decode)
        proceed_decode = 1;
    return proceed_decode;
}

/*
Gets the start address of the bitstream buffer. This is used for start of decode
*/
char* audio_bitstream_get_input_buffer_ptr(
                        struct audio_bitstream_sm *bstream)
{
    return bstream->inp_buf_curr_ptr;
}

/*
Gets the writePtr of the bitstream buffer. This is used for calculating length of
bitstream
*/
char* audio_bitstream_get_input_buffer_write_ptr(
                        struct audio_bitstream_sm *bstream)
{
    return bstream->inp_buf_write_ptr;
}

int audio_bitstream_set_input_buffer_ptr(
                        struct audio_bitstream_sm *bstream, int bytes)
{
    if(((bstream->inp_buf_curr_ptr + bytes) <=
                        (bstream->inp_buf + bstream->inp_buf_size)) &&
        ((bstream->inp_buf_curr_ptr + bytes) >= bstream->inp_buf))

        bstream->inp_buf_curr_ptr += bytes;
     else {
        ALOGE("Invalid input buffer size %d bytes", bytes);
        return -EINVAL;
     }

    return 0;
}

int audio_bitstream_set_input_buffer_write_ptr(
                        struct audio_bitstream_sm *bstream, int bytes)
{
    if(((bstream->inp_buf_write_ptr + bytes) <=
                        (bstream->inp_buf + bstream->inp_buf_size)) &&
        ((bstream->inp_buf_write_ptr + bytes) >= bstream->inp_buf))

        bstream->inp_buf_write_ptr += bytes;
     else {
        ALOGE("Invalid input buffer size %d bytes", bytes);
        return -EINVAL;
     }

    return 0;
}

/*
Get the output buffer start pointer to start rendering the pcm sampled to driver
*/
char* audio_bitstream_get_output_buffer_ptr(
                        struct audio_bitstream_sm *bstream,
                        int format)
{
    switch(format) {
    case PCM_MCH_OUT:
        return bstream->pcm_mch_out_buf;
    case PCM_2CH_OUT:
        return bstream->pcm_2_out_buf;
    case COMPRESSED_OUT:
        return bstream->enc_out_buf;
    case TRANSCODE_OUT:
        return bstream->passt_out_buf;
    default:
        return NULL;
    }
}

/*
Output the pointer from where the next PCM samples can be copied to buffer
*/
char* audio_bitstream_get_output_buffer_write_ptr(
                        struct audio_bitstream_sm *bstream,
                        int format)
{
    switch(format) {
    case PCM_MCH_OUT:
        return bstream->pcm_mch_out_buf_write_ptr;
    case PCM_2CH_OUT:
        return bstream->pcm_2_out_buf_write_ptr;
    case COMPRESSED_OUT:
        return bstream->enc_out_buf_write_ptr;
    case TRANSCODE_OUT:
        return bstream->passt_out_buf_write_ptr;
    default:
        return NULL;
    }
}

/*
Provides the bitstream size available in the internal buffer
*/
size_t audio_bitstream_get_size(struct audio_bitstream_sm *bstream)
{
    return (bstream->inp_buf_write_ptr-bstream->inp_buf_curr_ptr);
}

/*
After decode, the residue bitstream in the buffer is moved to start, so as to
avoid circularity constraints
*/
void audio_bitstream_copy_residue_to_start(
                    struct audio_bitstream_sm *bstream,
                    size_t bytes_consumed_in_decode)
{
    size_t remaining_curr_valid_bytes = bstream->inp_buf_write_ptr -
                              (bytes_consumed_in_decode+bstream->inp_buf_curr_ptr);
    size_t remainingTotalBytes = bstream->inp_buf_write_ptr -
                              (bytes_consumed_in_decode+bstream->inp_buf);
    if(bstream->buffering_factor_cnt == bstream->buffering_factor) {
        memcpy(bstream->inp_buf, bstream->inp_buf+bytes_consumed_in_decode, remainingTotalBytes);
        bstream->inp_buf_write_ptr = bstream->inp_buf+remainingTotalBytes;
        bstream->inp_buf_curr_ptr = bstream->inp_buf_write_ptr-remaining_curr_valid_bytes;
    } else {
        bstream->inp_buf_curr_ptr += bytes_consumed_in_decode;
    }
}

/*
Remaing samples less than the one period size required for the pcm driver
is moved to start of the buffer
*/
void audio_bitstream_copy_residue_output_start(
                    struct audio_bitstream_sm *bstream,
                    int format,
                    size_t samplesRendered)
{
    size_t remaining_bytes;
    switch(format) {
    case PCM_MCH_OUT:
        remaining_bytes = bstream->pcm_mch_out_buf_write_ptr-\
                        (bstream->pcm_mch_out_buf+samplesRendered);
        memcpy(bstream->pcm_mch_out_buf,
                    bstream->pcm_mch_out_buf+samplesRendered,
                    remaining_bytes);
        bstream->pcm_mch_out_buf_write_ptr = \
                    bstream->pcm_mch_out_buf + remaining_bytes;
        break;
    case PCM_2CH_OUT:
        remaining_bytes = bstream->pcm_2_out_buf_write_ptr-\
                        (bstream->pcm_2_out_buf+samplesRendered);
        memcpy(bstream->pcm_2_out_buf,
                        bstream->pcm_2_out_buf+samplesRendered,
                        remaining_bytes);
        bstream->pcm_2_out_buf_write_ptr = \
                        bstream->pcm_2_out_buf + remaining_bytes;
        break;
    case COMPRESSED_OUT:
        remaining_bytes = bstream->enc_out_buf_write_ptr-\
                        (bstream->enc_out_buf+samplesRendered);
        memcpy(bstream->enc_out_buf,
                        bstream->enc_out_buf+samplesRendered,
                        remaining_bytes);
        bstream->enc_out_buf_write_ptr = \
                            bstream->enc_out_buf + remaining_bytes;
        break;
    case TRANSCODE_OUT:
        remaining_bytes = bstream->passt_out_buf_write_ptr-\
                                        (bstream->passt_out_buf+samplesRendered);
        memcpy(bstream->passt_out_buf,
                bstream->passt_out_buf+samplesRendered,
                remaining_bytes);
        bstream->passt_out_buf_write_ptr = \
                bstream->passt_out_buf + remaining_bytes;
        break;
    default:
        break;
    }
}

/*
The write pointer is updated after the incoming PCM samples are copied to the
output buffer
*/
void audio_bitstream_set_output_buffer_write_ptr(
                struct audio_bitstream_sm *bstream,
                int format, size_t output_pcm_sample)
{
    int alloc_bytes;
    switch(format) {
    case PCM_MCH_OUT:
        alloc_bytes = SAMPLES_PER_CHANNEL*\
                        MAX_OUTPUT_CHANNELS_SUPPORTED*FACTOR_FOR_BUFFERING;
        if (bstream->pcm_mch_out_buf + alloc_bytes >\
                         bstream->pcm_mch_out_buf_write_ptr + output_pcm_sample)
            bstream->pcm_mch_out_buf_write_ptr += output_pcm_sample;
        break;
    case PCM_2CH_OUT:
        alloc_bytes = SAMPLES_PER_CHANNEL*STEREO_CHANNELS*FACTOR_FOR_BUFFERING;
        if(bstream->pcm_2_out_buf + alloc_bytes > \
                         bstream->pcm_2_out_buf_write_ptr + output_pcm_sample)
            bstream->pcm_2_out_buf_write_ptr += output_pcm_sample;
        break;
    case COMPRESSED_OUT:
        alloc_bytes = SAMPLES_PER_CHANNEL*\
                        MAX_INPUT_CHANNELS_SUPPORTED*FACTOR_FOR_BUFFERING;
        if (bstream->enc_out_buf + alloc_bytes > \
                        bstream->enc_out_buf_write_ptr + output_pcm_sample)
            bstream->enc_out_buf_write_ptr += output_pcm_sample;
        break;
    case TRANSCODE_OUT:
        alloc_bytes = SAMPLES_PER_CHANNEL*\
                        MAX_INPUT_CHANNELS_SUPPORTED*FACTOR_FOR_BUFFERING;
        if (bstream->passt_out_buf + alloc_bytes > \
                        bstream->passt_out_buf_write_ptr + output_pcm_sample)
            bstream->passt_out_buf_write_ptr += output_pcm_sample;
        break;
    default:
        break;
    }
}

/*
Flags if sufficient samples are available to render to PCM driver
*/
int audio_bitstream_sufficient_sample_to_render(
                        struct audio_bitstream_sm *bstream,
                        int format, int mid_size_reqd)
{
    int status = 0;
    char *buf_ptr = NULL, *buf_write_ptr = NULL;
    switch(format) {
    case PCM_MCH_OUT:
        buf_ptr = bstream->pcm_mch_out_buf;
        buf_write_ptr = bstream->pcm_mch_out_buf_write_ptr;
        break;
    case PCM_2CH_OUT:
        buf_ptr = bstream->pcm_2_out_buf;
        buf_write_ptr = bstream->pcm_2_out_buf_write_ptr;
        break;
    case COMPRESSED_OUT:
        buf_ptr = bstream->enc_out_buf;
        buf_write_ptr = bstream->enc_out_buf_write_ptr;
        break;
    case TRANSCODE_OUT:
        buf_ptr = bstream->passt_out_buf;
        buf_write_ptr = bstream->passt_out_buf_write_ptr;
        break;
    default:
        break;
    }
    if( (buf_write_ptr-buf_ptr) >= mid_size_reqd )
        status = 1;
    return status;
}

void audio_bitstream_start_input_buffering_mode(
                        struct audio_bitstream_sm *bstream)
{
    bstream->buffering_factor_cnt = 0;
}

void audio_bitstream_stop_input_buffering_mode(
                        struct audio_bitstream_sm *bstream)
{
    size_t remaining_curr_valid_bytes = \
                    bstream->inp_buf_write_ptr - bstream->inp_buf_curr_ptr;
    bstream->buffering_factor_cnt = bstream->buffering_factor;
    memcpy(bstream->inp_buf,
                bstream->inp_buf_curr_ptr,
                remaining_curr_valid_bytes);
    bstream->inp_buf_curr_ptr = bstream->inp_buf;
    bstream->inp_buf_write_ptr = bstream->inp_buf + remaining_curr_valid_bytes;
}
