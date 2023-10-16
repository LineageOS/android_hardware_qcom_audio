/*
* Copyright (c) 2021,2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
*/



#ifndef STT_META_EXTRACT_H
#define STT_META_EXTRACT_H
#include <getopt.h>
#include <time.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SOUND_CARD 0
#define MIXER_OPEN_MAX_NUM_RETRY 10
#define RETRY_US 500000
#define MAX_SECTORS 8
#define NUM_SECTORS 4
#define TOTAL_DEGREES 360
#define TOTAL_SPEAKERS 5
#define NSEC_MSEC_CONVERT 1000
#define ASCI_NUM 48
#define DECI 10
#define MIXER_PATH_MAX_LENGTH 100

enum fluence_version {
    FV_11,
    FV_13
};

struct sound_focus_param {
    uint16_t  start_angle[MAX_SECTORS];
    uint8_t   enable[MAX_SECTORS];
    uint16_t  gain_step;
} __attribute__((packed));

struct sound_focus_meta {
    uint16_t  start_angle[MAX_SECTORS];
    uint8_t   enable[MAX_SECTORS];
    uint16_t  gain_step;
    struct   timespec ts;
} __attribute__((packed));

struct source_tracking_param_fnn {
    int32_t  speech_probablity_q20;
    int16_t  speakers[TOTAL_SPEAKERS];
    int16_t  reserved;
    uint8_t  polarActivity[TOTAL_DEGREES];
    uint32_t  session_time_lsw;
    uint32_t  session_time_msw;
} __attribute__((packed));

struct source_track_meta_fnn {
    int32_t  speech_probablity_q20;
    int16_t  speakers[TOTAL_SPEAKERS];
    int16_t  reserved;
    uint8_t  polarActivity[TOTAL_DEGREES];
    uint32_t  session_time_lsw;
    uint32_t  session_time_msw;
} __attribute__((packed));

struct source_tracking_param {
    uint8_t   vad[MAX_SECTORS];
    uint16_t  doa_speech;
    uint16_t  doa_noise[NUM_SECTORS-1];
    uint8_t   polar_activity[TOTAL_DEGREES];
} __attribute__((packed));

struct source_track_meta {
    uint8_t   vad[MAX_SECTORS];
    uint16_t  doa_speech;
    uint16_t  doa_noise[NUM_SECTORS-1];
    uint8_t   polar_activity[TOTAL_DEGREES];
    struct   timespec ts;
} __attribute__((packed));

static int get_sourcetrack_metadata(void *source_track_meta, unsigned int meta_size,
                                                             struct mixer_ctl *ctl);

static int get_soundfocus_metadata(struct sound_focus_meta *sound_focus_meta,
                                                      struct mixer_ctl *ctl);

static int set_soundfocus_metadata(struct sound_focus_meta *sound_focus_meta,
                                                      struct mixer_ctl *ctl);

#endif
