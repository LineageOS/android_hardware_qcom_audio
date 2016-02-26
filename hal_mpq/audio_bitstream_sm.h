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

#ifndef QCOM_AUDIO_BITSTRM_SM_H
#define QCOM_AUDIO_BITSTRM_SM_H
int audio_bitstream_init(struct audio_bitstream_sm *bstream, int buffering_factor);
int audio_bitstream_close(struct audio_bitstream_sm *bstream);
int audio_bitstream_with_buffering_factor(struct audio_bitstream_sm *bstream,
                       int in_buffering_factor);
void audio_bitstream_reset_ptr( struct audio_bitstream_sm *bstream);
void audio_bitstream_reset_output_bitstream_ptr(
                            struct audio_bitstream_sm *bstream);
void audio_bitstream_copy_to_internal_buffer(
                    struct audio_bitstream_sm *bstream,
                    char *buf_ptr, size_t bytes);
void audio_bitstream_append_silence_internal_buffer(
                    struct audio_bitstream_sm *bstream,
                    uint32_t bytes, unsigned char value);
int audio_bitstream_sufficient_buffer_to_decode(
                        struct audio_bitstream_sm *bstream,
                        int min_bytes_to_decode);
char* audio_bitstream_get_input_buffer_ptr(
                        struct audio_bitstream_sm *bstream);
char* audio_bitstream_get_input_buffer_write_ptr(
                        struct audio_bitstream_sm *bstream);
char* audio_bitstream_get_output_buffer_ptr(
                        struct audio_bitstream_sm *bstream,
                        int format);
char* audio_bitstream_get_output_buffer_write_ptr(
                        struct audio_bitstream_sm *bstream,
                        int format);
size_t audio_bitstream_get_size(struct audio_bitstream_sm *bstream);
void audio_bitstream_copy_residue_to_start(
                    struct audio_bitstream_sm *bstream,
                    size_t bytes_consumed_in_decode);
void audio_bitstream_copy_residue_output_start(
                    struct audio_bitstream_sm *bstream,
                    int format,
                    size_t samplesRendered);
void audio_bitstream_set_output_buffer_write_ptr(
                struct audio_bitstream_sm *bstream,
                int format, size_t output_pcm_sample);
int audio_bitstream_sufficient_sample_to_render(
                        struct audio_bitstream_sm *bstream,
                        int format, int mid_size_reqd);
void audio_bitstream_start_input_buffering_mode(
                        struct audio_bitstream_sm *bstream);
void audio_bitstream_stop_input_buffering_mode(
                        struct audio_bitstream_sm *bstream);
#endif
